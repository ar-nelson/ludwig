#include "views/webapp.h++"
#include "controllers/instance.h++"
#include "db.h++"
#include "models/enums.h++"
#include "services/search_engine.h++"
#include "static/default-theme.min.css.h++"
#include "static/feather-sprite.svg.h++"
#include "static/htmx.min.js.h++"
#include "static/ludwig.js.h++"
#include "static/twemoji-piano.ico.h++"
#include "util/lambda_macros.h++"
#include "util/rich_text.h++"
#include "util/router.h++"
#include "util/web.h++"
#include "util/zstd_db_dump.h++"
#include <iterator>
#include <regex>
#include <semaphore>
#include <xxhash.h>

using std::match_results, std::monostate, std::nullopt, std::optional, std::regex,
    std::regex_search, std::shared_ptr, std::stoull, std::string, std::string_view, std::variant,
    std::visit, fmt::format, fmt::format_to, fmt::operator""_cf;

using namespace std::placeholders;
namespace chrono = std::chrono;

#define COOKIE_NAME "ludwig_session"

namespace Ludwig {
  static const regex cookie_regex(
    R"((?:^|;)\s*)" COOKIE_NAME R"(\s*=\s*([^;]+))",
    regex::ECMAScript
  );

  enum class SortFormType : uint8_t {
    Board,
    Comments,
    User
  };

  enum class SubmenuAction : uint8_t {
    None,
    Reply,
    Edit,
    Delete,
    Share,
    Save,
    Unsave,
    Hide,
    Unhide,
    Report,
    MuteUser,
    UnmuteUser,
    MuteBoard,
    UnmuteBoard,
    ModRestore,
    ModApprove,
    ModFlag,
    ModLock,
    ModRemove,
    ModRemoveUser,
    AdminRestore,
    AdminApprove,
    AdminFlag,
    AdminLock,
    AdminRemove,
    AdminRemoveUser,
    AdminPurge,
    AdminPurgeUser
  };

  enum class SiteAdminTab : uint8_t {
    Settings,
    ImportExport,
    Applications,
    Invites
  };

  enum class UserSettingsTab : uint8_t {
    Settings,
    Profile,
    Account,
    Invites
  };

  static inline auto format_as(SubmenuAction a) { return fmt::underlying(a); };

  static inline auto describe_mod_state(ModState s) -> string_view {
    switch (s) {
      case ModState::Flagged: return "Flagged";
      case ModState::Locked: return "Locked";
      case ModState::Unapproved: return "Not Approved";
      case ModState::Removed: return "Removed";
      default: return "";
    }
  }

  static inline auto check(bool b) -> string_view {
    return b ? " checked" : "";
  }

  template <typename T>
  static inline auto select(T n, T v) -> string_view {
    return n == v ? " selected" : "";
  }

# define die(STATUS, MESSAGE) throw ApiError(MESSAGE, STATUS)
# define die_fmt(STATUS, MESSAGE, ...) throw ApiError(format(MESSAGE ""_cf, __VA_ARGS__), STATUS)

  template <bool SSL>
  struct Webapp : public std::enable_shared_from_this<Webapp<SSL>> {
    shared_ptr<InstanceController> controller;
    shared_ptr<KeyedRateLimiter> rate_limiter; // may be null!

    Webapp(
      shared_ptr<InstanceController> controller,
      shared_ptr<KeyedRateLimiter> rl
    ) : controller(controller), rate_limiter(rl) {}

    using App = uWS::TemplatedApp<SSL>;
    using Response = uWS::HttpResponse<SSL>*;
    using Request = uWS::HttpRequest*;

    struct Context : public RequestContext<SSL, shared_ptr<Webapp<SSL>>> {
      chrono::steady_clock::time_point start;
      optional<uint64_t> logged_in_user_id;
      optional<string> session_cookie;
      string ip;
      bool is_htmx;
      const SiteDetail* site = nullptr;
      Webapp<SSL>* app = nullptr;
      optional<LocalUserDetail> login;

      void pre_try(const uWS::HttpResponse<SSL>* rsp, Request req) noexcept override {
        start = chrono::steady_clock::now();
        is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty();
      }

      void pre_request(Response rsp, Request req, shared_ptr<Webapp<SSL>> app) override {
        using namespace chrono;
        this->app = app.get();
        ip = get_ip(rsp, req);

        if (app->rate_limiter && !app->rate_limiter->try_acquire(ip, req->getMethod() == "GET" ? 1 : 10)) {
          die(429, "Rate limited, try again later");
        }

        const auto [new_session, cookie] = get_auth_cookie(req, ip);
        session_cookie = cookie;
        site = app->controller->site_detail();
        if (!new_session) {
          if (site->require_login_to_view && req->getUrl() != "/login") {
            die(401, "Login is required to view this page");
          }
          if (!site->setup_done && req->getUrl() != "/login") {
            die(401, "First-run setup is not complete. Log in as an admin user to complete site setup. If no admin user exists, check console output for a randomly-generated password.");
          }
        } else if (!site->setup_done) {
          if (req->getUrl() != "/" && req->getUrl() != "/login" && req->getUrl() != "/logout" && req->getUrl() != "/site_admin/first_run_setup") {
            die(403, "First-run setup is not complete. This page is not yet accessible.");
          }
        }

        logged_in_user_id = new_session.transform(λx(x.user_id));
      }

      void error_response(const ApiError& e, Response rsp) noexcept override {
        if (!is_htmx) {
          if (this->method == "get" && e.http_status == 401) {
            rsp->writeStatus(http_status(303))
              ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT")
              ->writeHeader("Location", "/login")
              ->end();
            return;
          } else if (app) {
            try {
              auto txn = app->controller->open_read_txn();
              populate(txn);
              app->writer(rsp->writeStatus(http_status(e.http_status)))
                .write_html_header(*this, {})
                .write_fmt(R"(<main><div class="error-page"><h2>Error {}</h2><p>{}</p></div></main>)"_cf, http_status(e.http_status), e.message)
                .write_html_footer(*this)
                .finish();
              return;
            } catch (...) {
              spdlog::warn("Error when rendering error page");
            }
          }
        }
        rsp->writeStatus(http_status(e.http_status))
          ->writeHeader("Content-Type", TYPE_HTML)
          ->end(format("Error {:d}: {}"_cf, e.http_status, Escape(e.message)));
      }

      auto populate(ReadTxn& txn) {
        if (logged_in_user_id) {
          if (*logged_in_user_id) login.emplace(LocalUserDetail::get_login(txn, *logged_in_user_id));
          else if (!site->setup_done) {
            spdlog::warn("Using temporary admin user");
            login.emplace(LocalUserDetail::temp_admin());
          }
        }
      }

      auto require_login() {
        if (!logged_in_user_id) die(401, "Login is required");
        const auto id = *logged_in_user_id;
        if (!id && site->setup_done) die(401, "Site is set up, temporary login is no longer valid");
        return id;
      }

      auto require_login(ReadTxn& txn) -> const LocalUserDetail& {
        if (!logged_in_user_id) die(401, "Login is required");
        if (!login) populate(txn);
        if (!login) die(401, "Site is set up, temporary login is no longer valid");
        return *login;
      }

      auto write_cookie(Response rsp) const noexcept {
        if (session_cookie) rsp->writeHeader("Set-Cookie", *session_cookie);
      }

      auto time_elapsed() const noexcept {
        const auto end = chrono::steady_clock::now();
        return chrono::duration_cast<chrono::microseconds>(end - start).count();
      }

      auto get_auth_cookie(Request req, const std::string& ip) -> std::pair<optional<LoginResponse>, optional<string>> {
        const auto cookies = req->getHeader("cookie");
        match_results<string_view::const_iterator> match;
        if (!regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) return {{}, {}};
        try {
          auto txn = app->controller->open_read_txn();
          const auto old_session = stoull(match[1], nullptr, 16);
          auto new_session = app->controller->validate_or_regenerate_session(
            txn, old_session, ip, req->getHeader("user-agent")
          );
          if (!new_session) throw std::runtime_error("expired session");
          if (new_session->session_id != old_session) {
            spdlog::debug("Regenerated session {:x} as {:x}", old_session, new_session->session_id);
            return {
              new_session,
              format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}"_cf,
                new_session->session_id, fmt::gmtime(new_session->expiration))
            };
          }
          return {new_session, {}};
        } catch (...) {
          spdlog::debug("Auth cookie is invalid; requesting deletion");
          return {{}, COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"};
        }
      }
    };

    static auto display_name_as_text(const User& user) -> string {
      if (user.display_name_type() && user.display_name_type()->size()) {
        return rich_text_to_plain_text(user.display_name_type(), user.display_name());
      }
      const auto name = user.name()->string_view();
      return string(name.substr(0, name.find('@')));
    }

    static auto display_name_as_text(const Board& board) -> string {
      if (board.display_name_type() && board.display_name_type()->size()) {
        return rich_text_to_plain_text(board.display_name_type(), board.display_name());
      }
      const auto name = board.name()->string_view();
      return string(name.substr(0, name.find('@')));
    }

    static auto display_name_as_text(const Thread& thread) -> string {
      return rich_text_to_plain_text(thread.title_type(), thread.title());
    }

    struct ResponseWriter {
      InstanceController& controller;
      Response rsp;
      string buf;
      std::back_insert_iterator<string> inserter = std::back_inserter(buf);

      ResponseWriter(Webapp<SSL>* w, Response rsp) : controller(*w->controller), rsp(rsp) { buf.reserve(1024); }
      operator Response() { return rsp; }
      auto write(string_view s) noexcept -> ResponseWriter& { buf.append(s); return *this; }
      auto finish() -> void { rsp->end(buf); }
      template <typename T, typename... Args> auto write_fmt(T fmt, Args&&... args) noexcept -> ResponseWriter& {
        format_to(inserter, fmt, std::forward<Args>(args)...);
        return *this;
      }

      auto write_toast(string_view content, string_view extra_classes = "") noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<div hx-swap-oob="afterbegin:#toasts">)"
          R"(<p class="toast{}" aria-live="polite" hx-get="data:text/html," hx-trigger="click, every 30s" hx-swap="delete">{}</p>)"
          "</div>"_cf,
          extra_classes, Escape{content}
        );
      }

      auto write_qualified_display_name(const User* user) noexcept -> ResponseWriter& {
        const auto name = user->name()->string_view();
        if (user->display_name_type() && user->display_name_type()->size()) {
          write(rich_text_to_html_emojis_only(user->display_name_type(), user->display_name(), {}));
          const auto at_index = name.find('@');
          if (at_index != string_view::npos) write(name.substr(at_index));
        } else {
          write(name);
        }
        return *this;
      }

      auto write_qualified_display_name(const Board* board) noexcept -> ResponseWriter& {
        const auto name = board->name()->string_view();
        if (board->display_name_type() && board->display_name_type()->size()) {
          write(rich_text_to_html_emojis_only(board->display_name_type(), board->display_name(), {}));
          const auto at_index = name.find('@');
          if (at_index != string_view::npos) write(name.substr(at_index));
        } else {
          write(name);
        }
        return *this;
      }

      auto display_name_as_html(const User& user) -> string {
        if (user.display_name_type() && user.display_name_type()->size()) {
          return rich_text_to_html_emojis_only(user.display_name_type(), user.display_name(), {});
        }
        const auto name = user.name()->string_view();
        return format("{}"_cf, Escape(name.substr(0, name.find('@'))));
      }

      auto display_name_as_html(const Board& board) -> string {
        if (board.display_name_type() && board.display_name_type()->size()) {
          return rich_text_to_html_emojis_only(board.display_name_type(), board.display_name(), {});
        }
        const auto name = board.name()->string_view();
        return format("{}"_cf, Escape(name.substr(0, name.find('@'))));
      }

      struct HtmlHeaderOptions {
        optional<string_view> canonical_path, banner_link, page_title;
        optional<string> banner_title, banner_image, card_image;
      };

      auto write_html_header(const Context& c, HtmlHeaderOptions opt) noexcept -> ResponseWriter& {
        assert(c.site != nullptr);
        rsp->writeHeader("Content-Type", TYPE_HTML);
        c.write_cookie(rsp);
        write_fmt(
          R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
          R"(<meta name="viewport" content="width=device-width,initial-scale=1">)"
          R"(<meta name="referrer" content="same-origin"><title>{}{}{}</title>)"
          R"(<style type="text/css">body{}--color-accent:{}!important;--color-accent-dim:{}!important;--color-accent-hover:{}!important;{}</style>)"
          R"(<link rel="stylesheet" href="/static/default-theme.css">)"_cf,
          Escape{c.site->name},
          (opt.page_title || opt.banner_title) ? " - " : "",
          Escape{
            opt.page_title ? *opt.page_title :
            opt.banner_title ? *opt.banner_title :
            ""
          },
          "{",
          c.site->color_accent,
          c.site->color_accent_dim,
          c.site->color_accent_hover,
          "}"
        );
        if (c.site->javascript_enabled) {
          write(
            R"(<script src="/static/htmx.min.js"></script>)"
            R"(<script src="/static/ludwig.js"></script>)"
          );
        }
        if (opt.canonical_path) {
          write_fmt(
            R"(<link rel="canonical" href="{0}{1}">)"
            R"(<meta property="og:url" content="{0}{1}">)"
            R"(<meta property="twitter:url" content="{0}{1}">)"_cf,
            Escape{c.site->base_url}, Escape{*opt.canonical_path}
          );
        }
        if (opt.page_title) {
          write_fmt(
            R"(<meta property="title" href="{0} - {1}">)"
            R"(<meta property="og:title" content="{0} - {1}">)"
            R"(<meta property="twitter:title" content="{0} - {1}">)"
            R"(<meta property="og:type" content="website">)"_cf,
            Escape{c.site->name}, Escape{*opt.page_title}
          );
        }
        if (opt.card_image) {
          write_fmt(
            R"(<meta property="og:image" content="{0}">)"
            R"(<meta property="twitter:image" content="{0}>)"
            R"(<meta property="twitter:card" content="summary_large_image">)"_cf,
            Escape{*opt.card_image}
          );
        }
        write_fmt(
          R"(</head><body><script>document.body.classList.add("has-js")</script>)"
          R"(<nav class="topbar"><div class="site-name">🎹 {}</div><ul class="quick-boards">)"
          R"(<li><a href="/">Home</a>)"
          R"(<li><a href="/local">Local</a>)"
          R"(<li><a href="/all">All</a>)"
          R"(<li><a href="/boards">Boards</a>)"
          R"(<li><a href="/users">Users</a>)"_cf,
          Escape{c.site->name}
        );
        if (c.login) {
          write_fmt(
            R"(</ul><ul>)"
            R"(<li id="topbar-user"><a href="/u/{}">{}</a> ({:d}))"
            R"(<li><a href="/settings">Settings</a>{}<li><a href="/logout">Logout</a></ul></nav>)"_cf,
            Escape(c.login->user().name()),
            display_name_as_html(c.login->user()),
            c.login->stats().thread_karma() + c.login->stats().comment_karma(),
            InstanceController::can_change_site_settings(c.login) ? R"(<li><a href="/site_admin">Site admin</a>)" : ""
          );
        } else if (c.site->registration_enabled) {
          write(R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)");
        } else {
          write(R"(</ul><ul><li><a href="/login">Login</a></ul></nav>)");
        }
        if (c.login) {
          if (c.login->user().mod_state() >= ModState::Locked) {
            write(R"(<div id="banner-locked" class="banner">Your account is locked. You cannot post, vote, or subscribe to boards.</div>)");
          }
        }
        write(R"(<div id="toasts"></div>)");
        if (opt.banner_title) {
          write(R"(<header id="page-header")");
          if (opt.banner_image) {
            write_fmt(R"( class="banner-image" style="background-image:url('{}');")"_cf, Escape{*opt.banner_image});
          }
          if (opt.banner_link) {
            write_fmt(
              R"(><h1><a class="page-header-link" href="{}">{}</a></h1></header>)"_cf,
              Escape{*opt.banner_link}, Escape{*opt.banner_title}
            );
          } else {
            write_fmt("><h1>{}</h1></header>"_cf, Escape{*opt.banner_title});
          }
        }
        return *this;
      }

      auto write_html_footer(const Context& c) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<div class="spacer"></div><footer><small>Powered by <a href="https://github.com/ar-nelson/ludwig">Ludwig</a>)"
          R"( · v{})"
#         ifdef LUDWIG_DEBUG
          " (DEBUG BUILD)"
#         endif
          R"( · Generated in {:L}μs</small></footer></body></html>)"_cf,
          VERSION,
          c.time_elapsed()
        );
      }

      auto write_subscribe_button(string_view name, bool is_unsubscribe) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form method="post" action="/b/{0}/subscribe" hx-post="/b/{0}/subscribe" hx-swap="outerHTML">{1})"
          R"(<button type="submit" class="big-button">{2}</button>)"
          "</form>"_cf,
          Escape{name},
          is_unsubscribe ? R"(<input type="hidden" name="unsubscribe" value="1">)" : "",
          is_unsubscribe ? "Unsubscribe" : "Subscribe"
        );
      }

      static constexpr string_view HONEYPOT_FIELD =
        R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)"
        R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)";

      auto write_sidebar(
        Login login,
        const SiteDetail* site,
        variant<
          monostate,
          const BoardDetail,
          const UserDetail
        > detail = monostate()
      ) noexcept -> ResponseWriter& {
        write(
          R"(<label id="sidebar-toggle-label" for="sidebar-toggle">)" ICON("menu") R"( Menu</label>)"
          R"(<input type="checkbox" name="sidebar-toggle" id="sidebar-toggle" class="a11y">)"
          R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
          R"(<form action="/search" id="search-form">)"
          R"(<label for="search"><span class="a11y">Search</span>)"
          R"(<input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)"
        );
        const auto hide_cw = login && login->local_user().hide_cw_posts();
        const optional<BoardDetail> board =
          std::holds_alternative<const BoardDetail>(detail) ? optional(std::get<const BoardDetail>(detail)) : nullopt;
        if (board) write_fmt(R"(<input type="hidden" name="board" value="{:x}">)"_cf, board->id);
        if (!hide_cw || board) {
          write(R"(<details id="search-options"><summary>Search Options</summary><fieldset>)");
          if (board) {
            write_fmt(
              R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to {}</label>)"_cf,
              display_name_as_html(board->board())
            );
          }
          if (!hide_cw) {
            write(R"(<label for="include_cw"><input type="checkbox" name="include_cw" id="include_cw" checked> Include results with Content Warnings</label>)");
          }
          write("</fieldset></details>");
        }
        write("</form></section>");
        if (!login) {
          write_fmt(
            R"(<section id="login-section"><h2>Login</h2><form method="post" action="/login" id="login-form">{})"
            R"(<label for="actual_username"><span class="a11y">Username or email</span><input type="text" name="actual_username" id="actual_username" placeholder="Username or email"></label>)"
            R"(<label for="password"><span class="a11y">Password</span><input type="password" name="password" id="password" placeholder="Password"></label>)"
            R"(<label for="remember"><input type="checkbox" name="remember" id="remember"> Remember me</label>)"
            R"(<input type="submit" value="Login" class="big-button"></form>)"
            R"({}</section>)"_cf,
            HONEYPOT_FIELD,
            site->registration_enabled ? R"(<a href="/register" class="big-button">Register</a>)" : ""
          );
        } else {
          visit(overload{
            [&](std::monostate) {
              if (controller.can_create_board(login)) {
                write(
                  R"(<section id="actions-section"><h2>Actions</h2>)"
                  R"(<a class="big-button" href="/create_board">Create a new board</a>)"
                  R"(</section>)"
                );
              }
            },
            [&](const BoardDetail& board) {
              write(R"(<section id="actions-section"><h2>Actions</h2>)");
              write_subscribe_button(board.board().name()->string_view(), board.subscribed);
              if (board.can_create_thread(login)) {
                write_fmt(
                  R"(<a class="big-button" href="/b/{0}/create_thread">Submit a new link</a>)"
                  R"(<a class="big-button" href="/b/{0}/create_thread?text=1">Submit a new text post</a>)"_cf,
                  Escape(board.board().name())
                );
              }
              if (board.can_change_settings(login)) {
                write_fmt(
                  R"(<a class="big-button" href="/b/{0}/settings">Board settings</a>)"_cf,
                  Escape(board.board().name())
                );
              }
              write("</section>");
            },
            [&](const UserDetail&) {}
          }, detail);
        }

        visit(overload{
          [&](std::monostate) {
            write_fmt(R"(<section id="site-sidebar"><h2>{}</h2>)"_cf, Escape{site->name});
            if (site->banner_url) {
              write_fmt(
                R"(<div class="sidebar-banner"><img src="{}" alt="{} banner"></div>)"_cf,
                Escape{*site->banner_url}, Escape{site->name}
              );
            }
            write_fmt("<p>{}</p>"_cf, Escape{site->description});
          },
          [&](const BoardDetail& board) {
            write_fmt(R"(<section id="board-sidebar"><h2>{}</h2>)"_cf, display_name_as_html(board.board()));
            // TODO: Banner image
            if (board.board().description_type() && board.board().description_type()->size()) {
              write_fmt(R"(<div class="markdown">{}</div>)"_cf, rich_text_to_html(
                board.board().description_type(),
                board.board().description(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          },
          [&](const UserDetail& user) {
            write_fmt(R"(<section id="user-sidebar"><h2>{}</h2>)"_cf, display_name_as_html(user.user()));
            if (user.user().bio_type() && user.user().bio_type()->size()) {
              write_fmt(R"(<div class="markdown">{}</div>)"_cf, rich_text_to_html(
                user.user().bio_type(),
                user.user().bio(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          }
        }, detail);

        return write("</section></aside>");
      }

      auto write_datetime(Timestamp timestamp) noexcept -> ResponseWriter& {
        return write_fmt(R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)"_cf,
          fmt::gmtime(timestamp), fmt::localtime(chrono::system_clock::to_time_t(timestamp)), RelativeTime{timestamp});
      }

      auto write_user_avatar(const User& user, Login login = {}) noexcept -> ResponseWriter& {
        if (user.avatar_url() && (!login || login->local_user().show_avatars())) {
          return write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/user/{}/avatar.webp">)"_cf,
            Escape{user.name()}
          );
        }
        return write(ICON("user"));
      }

      auto write_user_tags(const User& user, bool user_is_admin, uint64_t /* board_id */ = 0) noexcept -> ResponseWriter& {
        if (user.deleted_at()) {
          write(R"( <span class="tag tag-deleted">Deleted</span>)");
        }
        if (user_is_admin) {
          write(R"( <span class="tag tag-admin">Admin</span>)");
        }
        if (user.bot()) {
          write(R"( <span class="tag tag-bot">Bot</span>)");
        }
        // TODO: board-specific mod_state
        if (user.mod_state() > ModState::Normal) {
          if (user.mod_reason()) {
            write_fmt(R"( <abbr class="tag tag-mod-state" title="{0}: {1}">{0}</abbr>)"_cf,
              describe_mod_state(user.mod_state()),
              Escape(user.mod_reason())
            );
          } else {
            write_fmt(R"( <span class="tag tag-mod-state">{}</span>)"_cf, describe_mod_state(user.mod_state()));
          }
        }
        return *this;
      }

      auto write_user_link(const User& user, bool user_is_admin, Login login, uint64_t board_id = 0) noexcept -> ResponseWriter& {
        return write_fmt(R"(<a class="user-link" href="/u/{}">)"_cf, Escape(user.name()))
          .write_user_avatar(user, login)
          .write_qualified_display_name(&user)
          .write("</a>")
          .write_user_tags(user, user_is_admin, board_id);
      }

      auto write_board_icon(const Board& board) noexcept -> ResponseWriter& {
        if (board.icon_url()) {
          return write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/board/{}/icon.webp">)"_cf,
            Escape(board.name())
          );
        }
        return write(ICON("folder"));
      }

      auto write_board_tags(const Board& board) noexcept -> ResponseWriter& {
        if (board.content_warning()) {
          write_fmt(R"( <abbr class="tag tag-cw" title="Content Warning: {}">CW</abbr>)"_cf, Escape(board.content_warning()));
        }
        if (board.deleted_at()) {
          write(R"( <span class="tag tag-deleted">Deleted</span>)");
        }
        const auto mod_state = board.mod_state();
        if (mod_state > ModState::Normal) {
          if (board.mod_reason()) {
            write_fmt(R"( <abbr class="tag tag-mod-state" title="{0}: {1}">{0}</abbr>)"_cf,
              describe_mod_state(board.mod_state()),
              Escape(board.mod_reason())
            );
          } else {
            write_fmt(R"( <span class="tag tag-mod-state">{}</span>)"_cf, describe_mod_state(board.mod_state()));
          }
        }
        return *this;
      }

      auto write_board_link(const Board& board) noexcept -> ResponseWriter& {
        return write_fmt(R"(<a class="board-link" href="/b/{}">)"_cf, Escape(board.name()))
          .write_board_icon(board)
          .write_qualified_display_name(&board)
          .write("</a>")
          .write_board_tags(board);
      }

      auto write_board_list_entry(const BoardDetail& entry) {
        write(R"(<li class="board-list-entry"><div class="board-list-desc"><p class="board-list-name">)");
        write_board_link(entry.board());
        if (entry.board().display_name() && entry.board().display_name()->size()) {
          write_fmt(R"(</p><p class="account-name"><small>{}</small>)"_cf, Escape{entry.board().name()});
        }
        write_fmt(
          R"(</p><p>{}</p></div><div class="board-list-stats"><dl>)"
          R"(<dt>Subscribers</dt><dd>{:d}</dd>)"
          R"(<dt>Threads</dt><dd>{:d}</dd>)"
          R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)"_cf,
          rich_text_to_html(entry.board().description_type(), entry.board().description()),
          entry.stats().subscriber_count(),
          entry.stats().thread_count(),
          RelativeTime{uint_to_timestamp(entry.stats().latest_post_time())}
        );
      }

      auto write_user_list_entry(const UserDetail& entry, Login login = {}) {
        write(R"(<li class="user-list-entry"><div class="user-list-desc"><p class="user-list-name">)");
        write_user_link(entry.user(), entry.maybe_local_user().transform(λx(x.get().admin())).value_or(false), login);
        if (entry.user().display_name() && entry.user().display_name()->size()) {
          write_fmt(R"(</p><p class="account-name"><small>{}</small>)"_cf, Escape{entry.user().name()});
        }
        write_fmt(
          R"(</p><p>{}</p></div><div class="user-list-stats"><dl>)"
          R"(<dt>Threads</dt><dd>{:d}</dd>)"
          R"(<dt>Comments</dt><dd>{:d}</dd>)"
          R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)"_cf,
          rich_text_to_html(entry.user().bio_type(), entry.user().bio()),
          entry.stats().thread_count(),
          entry.stats().comment_count(),
          RelativeTime{uint_to_timestamp(entry.stats().latest_post_time())}
        );
      }

      template <class T> auto write_sort_select(string_view, T) noexcept -> ResponseWriter&;

      template <> auto write_sort_select<SortType>(string_view name, SortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="Active"{}>Active)"
          R"(<option value="Hot"{}>Hot)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="MostComments"{}>Most Comments)"
          R"(<option value="NewComments"{}>New Comments)"
          R"(<option value="TopAll"{}>Top All)"
          R"(<option value="TopYear"{}>Top Year)"
          R"(<option value="TopSixMonths"{}>Top Six Months)"
          R"(<option value="TopThreeMonths"{}>Top Three Months)"
          R"(<option value="TopMonth"{}>Top Month)"
          R"(<option value="TopWeek"{}>Top Week)"
          R"(<option value="TopDay"{}>Top Day)"
          R"(<option value="TopTwelveHour"{}>Top Twelve Hour)"
          R"(<option value="TopSixHour"{}>Top Six Hour)"
          R"(<option value="TopHour"{}>Top Hour)"
          "</select>"_cf,
          name, name,
          select(value, SortType::Active),
          select(value, SortType::Hot),
          select(value, SortType::New),
          select(value, SortType::Old),
          select(value, SortType::MostComments),
          select(value, SortType::NewComments),
          select(value, SortType::TopAll),
          select(value, SortType::TopYear),
          select(value, SortType::TopSixMonths),
          select(value, SortType::TopThreeMonths),
          select(value, SortType::TopMonth),
          select(value, SortType::TopWeek),
          select(value, SortType::TopDay ),
          select(value, SortType::TopTwelveHour),
          select(value, SortType::TopSixHour),
          select(value, SortType::TopHour)
        );
      }

      template <> auto write_sort_select<CommentSortType>(string_view name, CommentSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="Hot"{}>Hot)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="Top"{}>Top)"
          "</select>"_cf,
          name, name,
          select(value, CommentSortType::Hot),
          select(value, CommentSortType::New),
          select(value, CommentSortType::Old),
          select(value, CommentSortType::Top)
        );
      }

      template <> auto write_sort_select<UserPostSortType>(string_view name, UserPostSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="Top"{}>Top)"
          "</select>"_cf,
          name, name,
          select(value, UserPostSortType::New),
          select(value, UserPostSortType::Old),
          select(value, UserPostSortType::Top)
        );
      }

      template <> auto write_sort_select<UserSortType>(string_view name, UserSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="MostPosts"{}>Most Posts)"
          R"(<option value="NewPosts"{}>New Posts)"
          "</select>"_cf,
          name, name,
          select(value, UserSortType::New),
          select(value, UserSortType::Old),
          select(value, UserSortType::MostPosts),
          select(value, UserSortType::NewPosts)
        );
      }

      template <> auto write_sort_select<BoardSortType>(string_view name, BoardSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="MostPosts"{}>Most Posts)"
          R"(<option value="NewPosts"{}>New Posts)"
          R"(<option value="MostSubscribers"{}>Most Subscribers)"
          "</select>"_cf,
          name, name,
          select(value, BoardSortType::New),
          select(value, BoardSortType::Old),
          select(value, BoardSortType::MostPosts),
          select(value, BoardSortType::NewPosts),
          select(value, BoardSortType::MostSubscribers)
        );
      }

      auto write_show_threads_toggle(bool show_threads) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
          R"(<input class="a11y" name="type" type="radio" value="threads" id="type-threads"{}><label for="type-threads" class="toggle-button">Threads</label>)"
          R"(<input class="a11y" name="type" type="radio" value="comments" id="type-comments"{}><label for="type-comments" class="toggle-button">Comments</label></fieldset>)"_cf,
          check(show_threads), check(!show_threads)
        );
      }

      auto write_local_toggle(bool local_only) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
          R"(<input class="a11y" name="local" type="radio" value="1" id="local-1"{}><label for="local-1" class="toggle-button">Local</label>)"
          R"(<input class="a11y" name="local" type="radio" value="0" id="local-0"{}><label for="local-0" class="toggle-button">All</label></fieldset>)"_cf,
          check(local_only), check(!local_only)
        );
      }

      auto write_show_images_toggle(bool show_images) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(</label><label for="images"><input class="a11y" name="images" id="images" type="checkbox" value="1"{}><div class="toggle-switch"></div> Images</label>)"
          R"(<input class="no-js" type="submit" value="Apply"></form>)"_cf,
          check(show_images)
        );
      }

      auto write_subscribed_toggle(bool show_images) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(</label><label for="sub"><input class="a11y" name="sub" id="sub" type="checkbox" value="1"{}><div class="toggle-switch"></div> Subscribed Only</label>)"
          R"(<input class="no-js" type="submit" value="Apply"></form>)"_cf,
          check(show_images)
        );
      }

      template <class T> auto write_toggle_1(bool) noexcept -> ResponseWriter& { return *this; }
      template <> auto write_toggle_1<SortType>(bool t) noexcept -> ResponseWriter& { return write_show_threads_toggle(t); }
      template <> auto write_toggle_1<UserPostSortType>(bool t) noexcept -> ResponseWriter& { return write_show_threads_toggle(t); }
      template <> auto write_toggle_1<UserSortType>(bool t) noexcept -> ResponseWriter& { return write_local_toggle(t); }
      template <> auto write_toggle_1<BoardSortType>(bool t) noexcept -> ResponseWriter& { return write_local_toggle(t); }

      template <class T> auto write_toggle_2(bool) noexcept -> ResponseWriter& {
        return write(R"(</label><input class="no-js" type="submit" value="Apply"></form>)");
      };
      template <> auto write_toggle_2<SortType>(bool t) noexcept -> ResponseWriter& { return write_show_images_toggle(t); }
      template <> auto write_toggle_2<CommentSortType>(bool t) noexcept -> ResponseWriter& { return write_show_images_toggle(t); }
      template <> auto write_toggle_2<UserPostSortType>(bool t) noexcept -> ResponseWriter& { return write_show_images_toggle(t); }
      template <> auto write_toggle_2<BoardSortType>(bool t) noexcept -> ResponseWriter& { return write_subscribed_toggle(t); }

      template <typename T> auto write_sort_options(
        string_view base_url,
        T sort,
        bool toggle_1,
        bool toggle_2,
        string_view hx_target = "#top-level-list"
      ) noexcept -> ResponseWriter& {
        return write_fmt(
            R"(<form class="sort-form" method="get" action="{0}" hx-get="{0}" hx-trigger="change" hx-target="{1}" hx-swap="outerHTML" hx-push-url="true">)"_cf,
            Escape{base_url}, Escape{hx_target}
          )
          .template write_toggle_1<T>(toggle_1)
          .write(R"(<label for="sort"><span class="a11y">Sort</span>)")
          .write_sort_select("sort", sort)
          .template write_toggle_2<T>(toggle_2);
      }

      template <class T> auto write_vote_buttons(const T& entry, const SiteDetail* site, Login login) noexcept -> ResponseWriter& {
        const auto can_upvote = entry.can_upvote(login, site),
          can_downvote = entry.can_downvote(login, site);
        if (can_upvote || can_downvote) {
          write_fmt(
            R"(<form class="vote-buttons" id="votes-{0:x}" method="post" action="/{1}/{0:x}/vote" hx-post="/{1}/{0:x}/vote" hx-swap="outerHTML">)"_cf,
            entry.id, T::noun
          );
        } else {
          write_fmt(R"(<div class="vote-buttons" id="votes-{:x}">)"_cf, entry.id);
        }
        if (entry.should_show_votes(login, site)) {
          if (!login || login->local_user().show_karma()) {
            write_fmt(R"(<output class="karma" id="karma-{:x}">{}</output>)"_cf, entry.id, Suffixed{entry.stats().karma()});
          } else {
            write(R"(<div class="karma">&nbsp;</div>)");
          }
          write_fmt(
            R"(<label class="upvote"><button type="submit" name="vote" {0}{2}>)"
            ICON("chevron-up") R"(<span class="a11y">Upvote</span></button></label>)"
            R"(<label class="downvote"><button type="submit" name="vote" {1}{3}>)"
            ICON("chevron-down") R"(<span class="a11y">Downvote</span></button></label>)"_cf,
            can_upvote ? "" : "disabled ", can_downvote ? "" : "disabled ",
            entry.your_vote == Vote::Upvote ? R"(class="voted" value="0")" : R"(value="1")",
            entry.your_vote == Vote::Downvote ? R"(class="voted" value="0")" : R"(value="-1")"
          );
        }
        return write((can_upvote || can_downvote) ? "</form>" : "</div>");
      }

      auto write_pagination(
        string_view base_url,
        bool is_first,
        PageCursor next,
        bool infinite_scroll_enabled = true
      ) noexcept -> ResponseWriter& {
        const auto sep = base_url.find('?') == string_view::npos ? "?" : "&amp;";
        write(R"(<div class="pagination" id="pagination" hx-swap-oob="true")");
        if (next && infinite_scroll_enabled) {
          write_fmt(
            R"( hx-get="{}{}from={}" hx-target="#top-level-list" hx-swap="beforeend" hx-trigger="revealed">)"_cf,
            Escape{base_url}, sep, next.to_string()
          );
        } else write(">");
        if (!is_first) {
          write_fmt(R"(<a class="big-button no-js" href="{}">← First</a>)"_cf, Escape{base_url});
        }
        if (next) {
          write_fmt(
            R"(<a class="big-button no-js" href="{0}{1}from={2}">Next →</a>)"
            R"(<a class="more-link js" href="{0}{1}from={2}" hx-get="{0}{1}from={2}" hx-target="#top-level-list" hx-swap="beforeend">Load more…</a>)"_cf,
            Escape{base_url}, sep, next.to_string()
          );
        }
        return write(R"(<div class="spinner">Loading…</div></div>)");
      }

      template <class T> auto write_controls_submenu(
        const T& post,
        Login login,
        PostContext context
      ) noexcept -> ResponseWriter& {
        if (!login) return *this;
        write_fmt(
          R"(<form class="controls-submenu" id="controls-submenu-{0:x}" method="post" action="/{1}/{0:x}/action">)"
          R"(<input type="hidden" name="context" value="{2:d}">)"
          R"(<label for="action"><span class="a11y">Action</span>)" ICON("chevron-down")
          R"(<select name="action" autocomplete="off" hx-post="/{1}/{0:x}/action" hx-trigger="change" hx-target="#controls-submenu-{0:x}">)"
          R"(<option selected hidden value="{3:d}">Actions)"_cf,
          post.id, T::noun, static_cast<unsigned>(context), SubmenuAction::None
        );
        if (context != PostContext::View && post.can_reply_to(login)) {
          write_fmt(R"(<option value="{:d}">💬 Reply)"_cf, SubmenuAction::Reply);
        }
        if (post.can_edit(login)) {
          write_fmt(R"(<option value="{:d}">✏️ Edit)"_cf, SubmenuAction::Edit);
        }
        if (post.can_delete(login)) {
          write_fmt(R"(<option value="{:d}">🗑️ Delete)"_cf, SubmenuAction::Delete);
        }
        write_fmt(
          R"(<option value="{:d}">{})"
          R"(<option value="{:d}">{})"_cf,
          post.saved ? SubmenuAction::Unsave : SubmenuAction::Save, post.saved ? "🚫 Unsave" : "🔖 Save",
          post.hidden ? SubmenuAction::Unhide : SubmenuAction::Hide, post.hidden ? "🔈 Unhide" : "🔇 Hide"
        );
        if (context != PostContext::User) {
          write_fmt(R"(<option value="{:d}">{})"_cf,
            post.user_hidden ? SubmenuAction::UnmuteUser : SubmenuAction::MuteUser,
            post.user_hidden ? "🔈 Unmute user" : "🔇 Mute user"
          );
        }
        if (context != PostContext::Board) {
          write_fmt(R"(<option value="{:d}">{})"_cf,
            post.board_hidden ? SubmenuAction::UnmuteBoard : SubmenuAction::MuteBoard,
            post.board_hidden ? "🔈 Unhide board" : "🔇 Hide board"
          );
        }
        if (login->local_user().admin()) {
          SubmenuAction a1, a2, a3;
          string_view b1, b2, b3;
          // FIXME: This is not the right mod_state, will do weird things if
          // user or board has a mod_state > Normal
          switch (post.mod_state().state) {
            case ModState::Normal:
              a1 = SubmenuAction::AdminFlag;
              a2 = SubmenuAction::AdminLock;
              a3 = SubmenuAction::AdminRemove;
              b1 = "🚩 Flag";
              b2 = "🔒 Lock";
              b3 = "✂️ Remove";
              break;
            case ModState::Flagged:
              a1 = SubmenuAction::AdminRestore;
              a2 = SubmenuAction::AdminLock;
              a3 = SubmenuAction::AdminRemove;
              b1 = "🏳️ Unflag";
              b2 = "🔒 Lock";
              b3 = "✂️ Remove";
              break;
            case ModState::Locked:
              a1 = SubmenuAction::AdminRestore;
              a2 = SubmenuAction::AdminFlag;
              a3 = SubmenuAction::AdminRemove;
              b1 = "🔓 Unlock";
              b2 = "🚩 Unlock and Flag";
              b3 = "✂️ Remove";
              break;
            case ModState::Unapproved:
              a1 = SubmenuAction::AdminApprove;
              a2 = SubmenuAction::AdminFlag;
              a3 = SubmenuAction::AdminRemove;
              b1 = "✔️ Approve";
              b2 = "🚩 Approve and Flag";
              b3 = "❌ Reject";
              break;
            default:
              a1 = SubmenuAction::AdminRestore;
              a2 = SubmenuAction::AdminFlag;
              a3 = SubmenuAction::AdminLock;
              b1 = "♻️ Restore";
              b2 = "🚩 Restore and Flag";
              b3 = "🔒 Restore and Lock";
              break;
          }
          write_fmt(
            R"(<optgroup label="Admin">)"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">🔨 Ban user)"
            R"(<option value="{:d}">☣️ Purge {})"
            R"(<option value="{:d}">☣️ Purge user)"
            "</optgroup>"_cf,
            a1, b1, a2, b2, a3, b3,
            SubmenuAction::AdminRemoveUser,
            SubmenuAction::AdminPurge, T::noun,
            SubmenuAction::AdminPurgeUser
          );
        }
        return write(R"(</select></label><button class="no-js" type="submit">Apply</button></form>)");
      }

      template<class T> static auto mod_state_prefix_suffix(ModStateSubject s) noexcept -> std::pair<string_view, string_view>;
      template<> static inline auto mod_state_prefix_suffix<ThreadDetail>(ModStateSubject s) noexcept -> std::pair<string_view, string_view> {
        switch (s) {
          case ModStateSubject::Instance: return {"Instance ", ""};
          case ModStateSubject::Board: return {"Board ", ""};
          case ModStateSubject::User: return {"User ", " by Admin"};
          case ModStateSubject::UserInBoard: return {"User ", " by Moderator"};
          case ModStateSubject::Thread:
          case ModStateSubject::Comment: return {"", " by Admin"};
          case ModStateSubject::ThreadInBoard:
          case ModStateSubject::CommentInBoard: return {"", " by Moderator"};
        }
      }
      template<> static inline auto mod_state_prefix_suffix<CommentDetail>(ModStateSubject s) noexcept -> std::pair<string_view, string_view> {
        switch (s) {
          case ModStateSubject::Instance: return {"Instance ", ""};
          case ModStateSubject::Board: return {"Board ", ""};
          case ModStateSubject::User: return {"User ", " by Admin"};
          case ModStateSubject::UserInBoard: return {"User ", " by Moderator"};
          case ModStateSubject::Thread: return {"Thread ", " by Admin"};
          case ModStateSubject::ThreadInBoard: return {"Thread ", " by Moderator"};
          case ModStateSubject::Comment: return {"", " by Admin"};
          case ModStateSubject::CommentInBoard: return {"", " by Moderator"};
        }
      }

      template<class T> static auto content_warning_prefix(ContentWarningSubject s) noexcept -> string_view;
      template<> static inline auto content_warning_prefix<ThreadDetail>(ContentWarningSubject s) noexcept -> string_view {
        return s == ContentWarningSubject::Board ? "Board ": "";
      }
      template<> static inline auto content_warning_prefix<CommentDetail>(ContentWarningSubject s) noexcept -> string_view {
        switch (s) {
          case ContentWarningSubject::Board: return "Board ";
          case ContentWarningSubject::Thread: return "Thread ";
          default: return "";
        }
      }

      template<class T> auto write_warnings(const T& post, PostContext context) noexcept -> ResponseWriter& {
        const auto mod_state = post.mod_state(context);
        write(R"(<p class="content-warning">)");
        if (
          mod_state.state > ModState::Normal &&
          (context == PostContext::View || context == PostContext::Reply || mod_state.subject >= ModStateSubject::ThreadInBoard)
        ) {
          const auto [prefix, suffix] = mod_state_prefix_suffix<T>(mod_state.subject);
          if (mod_state.reason) {
            write_content_warning(
              format("{}{}{}"_cf, prefix, describe_mod_state(mod_state.state), suffix),
              true,
              *mod_state.reason
            );
          } else {
            write_fmt(
              R"(<span class="tag tag-mod-state">{}{}{}</span>)"_cf,
              prefix, describe_mod_state(mod_state.state), suffix
            );
          }
        }
        if (const auto cw = post.content_warning(context)) {
          if (context == PostContext::View || context == PostContext::Reply || cw->subject >= ContentWarningSubject::Thread) {
            const auto prefix = content_warning_prefix<T>(cw->subject);
            write_content_warning("Content Warning", false, cw->content_warning, prefix);
          }
        }
        return write("</p>");
      }

      template<class T> auto write_post_tags(const T& post, PostContext context) noexcept -> ResponseWriter& {
        const auto mod_state = post.mod_state(context);
        if (mod_state.state > ModState::Normal) {
          auto [prefix, suffix] = mod_state_prefix_suffix<T>(mod_state.subject);
          write_fmt(R"( <abbr class="tag tag-mod-state" title="{0}{1}{2}{3}{4}">{1}</abbr>)"_cf,
            prefix, describe_mod_state(mod_state.state), suffix, mod_state.reason ? ": " : "", Escape(mod_state.reason.value_or(""))
          );
        }
        if (const auto cw = post.content_warning(context)) {
          const auto prefix = content_warning_prefix<T>(cw->subject);
          write_fmt(R"( <abbr class="tag tag-cw" title="{}Content Warning: {}">CW</abbr>)"_cf,
            prefix, Escape(cw->content_warning)
          );
        }
        return *this;
      }

      auto write_thread_entry(
        const ThreadDetail& thread,
        const SiteDetail* site,
        Login login,
        PostContext context,
        bool show_images
      ) noexcept -> ResponseWriter& {
        // TODO: thread-source (link URL)
        write_fmt(
          R"({} class="thread" id="thread-{:x}"><h2 class="thread-title">)"_cf,
          context == PostContext::View ? "<div" : "<li><article",
          thread.id
        );
        const auto title = rich_text_to_html_emojis_only(thread.thread().title_type(), thread.thread().title(), {});
        if (context != PostContext::View || thread.thread().content_url()) {
          write_fmt(R"(<a class="thread-title-link" href="{}">{}</a></h2>)"_cf,
            Escape{
              thread.thread().content_url()
                ? thread.thread().content_url()->string_view()
                : format("/thread/{:x}"_cf, thread.id)
            },
            title
          );
        } else {
          write_fmt("{}</h2>"_cf, title);
        }
        const auto cw = thread.content_warning(context);
        // TODO: Selectively show CW'd images, maybe use blurhash
        if (show_images && !cw && thread.link_card().image_url()) {
          write_fmt(
            R"(<div class="thumbnail"><img src="/media/thread/{:x}/thumbnail.webp" aria-hidden="true"></div>)"_cf,
            thread.id
          );
        } else {
          write_fmt(
            R"(<div class="thumbnail">)" ICON("{}") "</div>"_cf,
            cw ? "alert-octagon" : (thread.thread().content_url() ? "link" : "file-text")
          );
        }
        if (
          (cw || thread.mod_state(context).state > ModState::Normal) &&
          (context != PostContext::View || !thread.has_text_content())
        ) {
          write(R"(<div class="thread-warnings">)");
          write_warnings(thread, context);
          write(R"(</div>)");
        }
        write(R"(<div class="thread-info"><span>submitted )");
        write_datetime(thread.created_at());
        if (context != PostContext::User) {
          write("</span><span>by ");
          write_user_link(thread.author(), thread.user_is_admin, login);
        }
        if (context != PostContext::Board) {
          write("</span><span>to ");
          write_board_link(thread.board());
        }
        write("</span></div>");
        write_vote_buttons(thread, site, login);
        if (context != PostContext::View) {
          write_fmt(R"(<div class="controls"><a id="comment-link-{0:x}" href="/thread/{0:x}#comments">{1:d}{2}</a>)"_cf,
            thread.id,
            thread.stats().descendant_count(),
            thread.stats().descendant_count() == 1 ? " comment" : " comments"
          );
        } else {
          write(R"(<div class="controls"><span></span>)");
        }
        write_controls_submenu(thread, login, context);
        return write(context == PostContext::View ? "</div></div>" : "</div></article>");
      }

      auto write_comment_header(const CommentDetail& comment, Login login, PostContext context) noexcept -> ResponseWriter& {
        const string_view tag = context == PostContext::Reply ? "h3" : "h2";
        write_fmt(R"(<{} class="comment-info" id="comment-info-{:x}"><span>)"_cf, tag, comment.id);
        if (context != PostContext::User) {
          write_user_link(comment.author(), comment.user_is_admin, login);
          write("</span><span>");
        }
        write("commented ");
        write_datetime(comment.created_at());
        if (context != PostContext::Reply) {
          write_fmt(R"(</span><span>on <a href="/thread/{:x}">{}</a>)"_cf,
            comment.comment().thread(),
            rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title(), {})
          );
          // TODO: Use thread tags, not comment tags
          write_post_tags(comment, context);
          if (context != PostContext::Board) {
            write(R"(</span><span>in )");
            write_board_link(comment.board());
          }
        }
        return write_fmt(R"(</span></{}>)"_cf, tag);
      }

      auto write_comment_body(
        const CommentDetail& comment,
        const SiteDetail* site,
        Login login,
        PostContext context,
        bool show_images
      ) noexcept -> ResponseWriter& {
        const bool has_warnings = comment.content_warning(context) || comment.mod_state(context).state > ModState::Normal;
        const auto content = rich_text_to_html(
          comment.comment().content_type(),
          comment.comment().content(),
          { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
        );
        write_fmt(
          R"(<div class="comment-body" id="comment-body-{:x}"><div class="comment-content markdown">)"_cf,
          comment.id
        );
        if (has_warnings) {
          write(R"(<details class="content-warning-collapse"><summary>Content hidden (click to show))");
          write_warnings(comment, context);
          write_fmt(R"(</summary><div>{}</div></details></div>)"_cf, content);
        } else {
          write_fmt(R"({}</div>)"_cf, content);
        }
        write_vote_buttons(comment, site, login);
        write(R"(<div class="controls">)");
        if (context != PostContext::Reply) {
          write_fmt(R"(<a id="comment-link-{0:x}" href="/comment/{0:x}#replies">{1:d}{2}</a>)"_cf,
            comment.id,
            comment.stats().descendant_count(),
            comment.stats().descendant_count() == 1 ? " reply" : " replies"
          );
        } else {
          write_fmt(R"(<a href="/comment/{:x}">Permalink</a>)"_cf, comment.id);
        }
        write_controls_submenu(comment, login, context);
        return write("</div></div>");
      }

      auto write_comment_entry(
        const CommentDetail& comment,
        const SiteDetail* site,
        Login login,
        PostContext context,
        bool show_images
      ) noexcept -> ResponseWriter& {
        return write_fmt(R"(<li><article class="comment{}" id="comment-{:x}">)"_cf,
          comment.should_show_votes(login, site) ? "" : " no-votes",
          comment.id
        ) .write_comment_header(comment, login, context)
          .write_comment_body(comment, site, login, context, show_images)
          .write("</article>");
      }

      auto write_search_result_list(
        std::vector<InstanceController::SearchResultDetail> list,
        const SiteDetail* site,
        Login login,
        bool include_ol
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="search-list" id="top-level-list">)");
        for (const auto& entry : list) {
          visit(overload{
            [&](const UserDetail& user) {
              write("<li>");
              write_user_link(user.user(), user.maybe_local_user().transform(λx(x.get().admin())).value_or(false), login);
            },
            [&](const BoardDetail& board) {
              write("<li>");
              write_board_link(board.board());
            },
            [&](const ThreadDetail& thread) {
              write_thread_entry(thread, site, login, PostContext::Feed, true);
            },
            [&](const CommentDetail& comment) {
              write_comment_entry(comment, site, login, PostContext::Feed, true);
            },
          }, entry);
        }
        if (include_ol) write("</ol>");
        return *this;
      }

      auto write_comment_tree(
        const CommentTree& comments,
        uint64_t root,
        CommentSortType sort,
        const SiteDetail* site,
        Login login,
        bool show_images,
        bool is_top_level,
        bool include_ol,
        bool is_alt = false
      ) noexcept -> ResponseWriter& {
        // TODO: Include existing query params
        if (include_ol) write_fmt(R"(<ol class="comment-list comment-tree" id="comments-{:x}">)"_cf, root);
        auto range = comments.comments.equal_range(root);
        if (range.first == range.second) {
          if (is_top_level) write(R"(<li class="no-comments">No comments</li>)");
        } else {
          const bool infinite_scroll_enabled =
            site->infinite_scroll_enabled && (!login || login->local_user().infinite_scroll_enabled());
          for (auto iter = range.first; iter != range.second; iter++) {
            const auto& comment = iter->second;
            write_fmt(
              R"(<li><article class="comment-with-comments{}{}">)"
              R"(<details open class="comment-collapse" id="comment-{:x}"><summary>)"_cf,
              comment.should_show_votes(login, site) ? "" : " no-votes",
              is_alt ? " odd-depth" : "",
              comment.id
            );
            write_comment_header(comment, login, PostContext::Reply);
            write_fmt(
              R"(<small class="comment-reply-count">({:d} repl{})</small>)"_cf,
              comment.stats().descendant_count(),
              comment.stats().descendant_count() == 1 ? "y" : "ies"
            );
            write("</summary>");
            write_comment_body(comment, site, login, PostContext::Reply, show_images);
            const auto cont = comments.continued.find(comment.id);
            if (cont != comments.continued.end() && !cont->second) {
              write_fmt(
                R"(<a class="more-link{0}" id="continue-{1:x}" href="/comment/{1:x}">More comments…</a>)"_cf,
                is_alt ? "" : " odd-depth", comment.id
              );
            } else if (comment.stats().child_count()) {
              write(R"(<section class="comments" aria-title="Replies">)");
              write_comment_tree(comments, comment.id, sort, site, login, show_images, false, true, !is_alt);
              write("</section>");
            }
            write("</details></article>");
          }
          const auto cont = comments.continued.find(root);
          if (cont != comments.continued.end()) {
            write_fmt(R"(<li id="comment-replace-{:x}")"_cf, root);
            if (infinite_scroll_enabled) {
              write_fmt(
                R"( hx-get="/{0}/{1:x}?sort={2}&from={3}" hx-swap="outerHTML" hx-trigger="revealed")"_cf,
                is_top_level ? "thread" : "comment", root,
                EnumNameCommentSortType(sort), cont->second.to_string()
              );
            }
            write_fmt(
              R"(><a class="more-link{0}" id="continue-{1:x}" href="/{2}/{1:x}?sort={3}&from={4}")"
              R"( hx-get="/{2}/{1:x}?sort={3}&from={4}" hx-target="#comment-replace-{1:x}" hx-swap="outerHTML">More comments…</a>)"_cf,
              is_alt ? " odd-depth" : "", root, is_top_level ? "thread" : "comment",
              EnumNameCommentSortType(sort), cont->second.to_string()
            );
          }
        }
        if (include_ol) write("</ol>");
        return *this;
      }

      auto write_content_warning_field(string_view existing_value = "") noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<label for="content_warning_toggle" class="js"><span>Content warning</span>)"
          R"(<input type="checkbox" id="content_warning_toggle" name="content_warning_toggle" class="a11y" autocomplete="off" )"
          R"html(onclick="document.querySelector('label[for=content_warning]').setAttribute('class', this.checked ? '' : 'no-js')"{}>)html"
          R"(<div class="toggle-switch"></div>)"
          R"(</label><label for="content_warning"{}>)"
          R"(<span class="no-js">Content warning (optional)</span>)"
          R"(<span class="js">Content warning text</span>)"
          R"(<input type="text" name="content_warning" id="content_warning" autocomplete="off" value="{}">)"
          R"(</label>)"_cf,
          check(!existing_value.empty()),
          existing_value.empty() ? R"( class="no-js")" : "",
          Escape{existing_value}
        );
      }

      auto write_content_warning(string_view label, bool is_mod, string_view content, string_view prefix = "") noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<p class="tag tag-cw content-warning"><strong class="{}-warning-label">{}{}<span class="a11y">:</span></strong> {}</p>)"_cf,
          is_mod ? "mod" : "content", prefix, label, Escape{content}
        );
      }

      template <class T> auto write_reply_form(const T& parent) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<form data-component="Form" id="reply-{1:x}" class="form reply-form" method="post" action="/{0}/{1:x}/reply" )"
          R"html(hx-post="/{0}/{1:x}/reply" hx-target="#comments-{1:x}" hx-swap="afterbegin" hx-on::after-request="this.reset()">)html"
          R"(<a name="reply"></a>)"
          HTML_TEXTAREA("text_content", "Reply", R"( required placeholder="Write your reply here")", "")
          ""_cf,
          T::noun, parent.id
        );
        write_content_warning_field();
        return write(R"(<input type="submit" value="Reply"></form>)");
      }

      auto write_thread_view(
        const ThreadDetail& thread,
        const CommentTree& comments,
        const SiteDetail* site,
        Login login,
        CommentSortType sort,
        bool show_images = false
      ) noexcept -> ResponseWriter& {
        write_fmt(R"(<article class="thread-with-comments{}">)"_cf,
          thread.should_show_votes(login, site) ? "" : " no-votes"
        );
        write_thread_entry(thread, site, login, PostContext::View, show_images);
        if (thread.has_text_content()) {
          const auto content = rich_text_to_html(
            thread.thread().content_text_type(),
            thread.thread().content_text(),
            { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
          );
          if (thread.thread().content_warning() || thread.board().content_warning() || thread.thread().mod_state() > ModState::Normal) {
            write(R"(<div class="thread-content markdown"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
            write_warnings(thread, PostContext::View);
            write_fmt(R"(</summary><div>{}</div></details></div>)"_cf, content);
          } else {
            write_fmt(R"(<div class="thread-content markdown">{}</div>)"_cf, content);
          }
        }
        write_fmt(R"(<section class="comments" id="comments"><h2>{:d} comments</h2>)"_cf, thread.stats().descendant_count());
        write_sort_options(format("/thread/{:x}"_cf, thread.id), sort, false, show_images, format("#comments-{:x}", thread.id));
        if (thread.can_reply_to(login)) {
          write_reply_form(thread);
        }
        write_comment_tree(comments, thread.id, sort, site, login, show_images, true, true);
        return write("</section></article>");
      }

      auto write_comment_view(
        const CommentDetail& comment,
        const CommentTree& comments,
        const SiteDetail* site,
        Login login,
        CommentSortType sort,
        bool show_images = false
      ) noexcept -> ResponseWriter& {
        write_fmt(R"(<article class="comment-with-comments"><section class="comment{}" id="comment-{:x}">)"_cf,
          comment.should_show_votes(login, site) ? "" : " no-votes",
          comment.id
        );
        write_comment_header(comment, login, PostContext::View);
        write_comment_body(comment, site, login, PostContext::View, show_images);
        write_fmt(R"(</section><section class="comments" id="comments"><h2>{:d} replies</h2>)"_cf, comment.stats().descendant_count());
        write_sort_options(format("/comment/{:x}"_cf, comment.id), sort, false, show_images, format("#comments-{:x}"_cf, comment.id));
        if (comment.can_reply_to(login)) {
          write_reply_form(comment);
        }
        write_comment_tree(comments, comment.id, sort, site, login, show_images, false, true);
        return write("</section></article>");
      }

      static auto error_banner(optional<string_view> error) noexcept -> string {
        if (!error) return "";
        return format(R"(<p class="error-message"><strong>Error:</strong> {}</p>)"_cf, Escape{*error});
      }

      auto write_login_form(optional<string_view> error = {}) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<main><form class="form form-page" method="post" action="/login">{}{})"
          HTML_FIELD("actual_username", "Username or email", "text", "")
          HTML_FIELD("password", "Password", "password", "")
          HTML_CHECKBOX("remember", "Remember me", "")
          R"(<input type="submit" value="Login"></form></main>)"_cf,
          error_banner(error), HONEYPOT_FIELD
        );
      }

      auto write_register_form(const SiteDetail* site, optional<string_view> error = {}) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/register">{})"_cf,
          error_banner(error)
        ).write(
          R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)"
          R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)"
          HTML_FIELD("actual_username", "Username", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(")")
          HTML_FIELD("email", "Email address", "email", " required")
          HTML_FIELD("password", "Password", "password", " required")
          HTML_FIELD("confirm_password", "Confirm password", "password", " required")
        );
        if (site->registration_invite_required) {
          write(HTML_FIELD("invite_code", "Invite code", "text", R"( required pattern=")" INVITE_CODE_REGEX_SRC R"(")"));
        }
        if (site->registration_application_required) {
          write_fmt(
            R"(<label for="application_reason"><span>{}</span><textarea name="application_reason" required autocomplete="off"></textarea></label>)"_cf,
            Escape{site->application_question.value_or("Why do you want to join?")}
          );
        }
        return write(R"(<input type="submit" value="Register"></form></main>)");
      }

      auto write_create_board_form(
        const SiteDetail* site,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/create_board"><h2>Create Board</h2>{})"_cf,
          error_banner(error)
        ).write(
          HTML_FIELD("name", "Name", "text", R"( autocomplete="off" placeholder="my_cool_board" pattern=")" USERNAME_REGEX_SRC R"(" required)")
          HTML_FIELD("display_name", "Display name", "text", R"( autocomplete="off" placeholder="My Cool Board")")
          HTML_FIELD("content_warning", "Content warning (optional)", "text", R"( autocomplete="off")")
          HTML_CHECKBOX("private", "Private (only visible to members)", "")
          HTML_CHECKBOX("restricted_posting", "Restrict posting to moderators", "")
          HTML_CHECKBOX("approve_subscribe", "Approval required to join", "")
          //HTML_CHECKBOX("invite_required", "Invite code required to join", "")
          //HTML_CHECKBOX("invite_mod_only", "Only moderators can invite new members", "")
        ).write_voting_select(
          site->votes_enabled,
          site->downvotes_enabled,
          site->votes_enabled,
          site->downvotes_enabled
        ).write(R"(<input type="submit" value="Submit"></form></main>)");
      }

      auto write_create_thread_form(
        bool show_url,
        const BoardDetail board,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/b/{}/create_thread"><h2>Create Thread</h2>{})"
          R"(<p class="thread-info"><span>Posting as )"_cf,
          Escape(board.board().name()), error_banner(error)
        );
        write_user_link(login.user(), login.local_user().admin(), login);
        write("</span><span>to ");
        write_board_link(board.board());
        write("</span></p><br>" HTML_FIELD("title", "Title", "text", R"( autocomplete="off" required)"));
        if (show_url) {
          write(
            HTML_FIELD("submission_url", "Submission URL", "text", R"( autocomplete="off" required)")
            HTML_TEXTAREA("text_content", "Description (optional)", "", "")
          );
        } else {
          write(HTML_TEXTAREA("text_content", "Text content", " required", ""));
        }
        return write(R"(<input type="submit" value="Submit"></form></main>)");
      }

      auto write_edit_thread_form(
        const ThreadDetail& thread,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/thread/{:x}/edit"><h2>Edit Thread</h2>{})"
          R"(<p class="thread-info"><span>Posting as )"_cf,
          thread.id, error_banner(error)
        );
        write_user_link(login.user(), login.local_user().admin(), login);
        write("</span><span>to ");
        write_board_link(thread.board());
        write_fmt(
          "</span></p><br>"
          HTML_FIELD("title", "Title", "text", R"( value="{}" autocomplete="off" required)")
          HTML_TEXTAREA("text_content", "Text content", "{}", "{}")
          ""_cf,
          Escape(display_name_as_text(thread.thread())),
          thread.thread().content_url() ? "" : " required",
          Escape(thread.thread().content_text_raw())
        );
        write_content_warning_field(thread.thread().content_warning() ? thread.thread().content_warning()->string_view() : "");
        return write(R"(<input type="submit" value="Submit"></form></main>)");
      }

      template <typename T> auto write_tab(T tab, T selected, string_view name, string_view url) {
        if (tab == selected) write_fmt(R"(<li><span class="selected">{}</span>)"_cf, name);
        else write_fmt(R"(<li><a href="{}">{}</a>)"_cf, url, name);
      }

      auto write_site_admin_tabs(const SiteDetail* site, SiteAdminTab selected) noexcept -> ResponseWriter& {
        write(R"(<ul class="tabs">)");
        write_tab(SiteAdminTab::Settings, selected, "Settings", "/site_admin");
        write_tab(SiteAdminTab::ImportExport, selected, "Import/Export", "/site_admin/import_export");
        if (site->registration_application_required) {
          write_tab(SiteAdminTab::Applications, selected, "Applications", "/site_admin/applications");
        }
        if (site->registration_invite_required) {
          write_tab(SiteAdminTab::Invites, selected, "Invites", "/site_admin/invites");
        }
        return write("</ul>");
      }

      auto write_home_page_type_select(HomePageType selected = HomePageType::Subscribed) noexcept -> ResponseWriter& {
        return write_fmt(R"(<label for="home_page_type"><span>Home page type{}</span>)"
            R"(<select name="home_page_type" id="home_page_type" autocomplete="off">)"
            R"(<option value="Subscribed"{}>Subscribed - Display the user's subscribed boards, or Local boards if not logged in)"
            R"(<option value="Local"{}>Local - Display top content from all boards on this site)"
            R"(<option value="All" disabled{}>All - Display top content from all federated sites (not yet supported))"
            R"(<option value="BoardList"{}>Board List - Display a curated list of boards, like a classic forum)"
            R"(<option value="SingleBoard"{}>Single Board - The site has only one board, which is always the homepage)"
          "</select></label>"_cf,
          selected == HomePageType::SingleBoard ? "<br><strong>Important: Once you select an option other than Single Board, you can never select Single Board again!</strong>" : "",
          select(selected, HomePageType::Subscribed),
          select(selected, HomePageType::Local),
          select(selected, HomePageType::All),
          select(selected, HomePageType::BoardList),
          select(selected, HomePageType::SingleBoard)
        );
      }

      auto write_voting_select(
        bool voting_enabled = true,
        bool downvotes_enabled = true,
        bool sitewide_voting_enabled = true,
        bool sitewide_downvotes_enabled = true
      ) noexcept -> ResponseWriter& {
        if (!sitewide_voting_enabled) return write(R"(<input type="hidden" name="voting" value="0">)");
        return write_fmt(R"(<label for="voting"><span>Voting</span><select name="voting" autocomplete="off">)"
            R"(<option value="2"{}{}>Rank posts using upvotes and downvotes)"
            R"(<option value="1"{}>Rank posts using only upvotes)"
            R"(<option value="0"{}>No voting, posts can only be ranked by age and comments)"
          R"(</select></label>)"_cf,
          sitewide_downvotes_enabled ? "" : " disabled",
          voting_enabled && downvotes_enabled ? " selected" : "",
          voting_enabled && !downvotes_enabled ? " selected" : "",
          voting_enabled ? "" : " selected"
        );
      }

      auto write_site_admin_form(const SiteDetail* site, optional<string_view> error = {}) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/site_admin"><h2>Site settings</h2>{})"
          HTML_FIELD("name", "Site name", "text", R"( value="{}" autocomplete="off" required)")
          HTML_TEXTAREA("description", "Sidebar description", "", "{}")
          HTML_FIELD("icon_url", "Icon URL", "text", R"( value="{}" autocomplete="off")")
          HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}" autocomplete="off")")
          HTML_FIELD("color_accent", "Accent Color", "color", R"( value="{}" autocomplete="off")")
          HTML_FIELD("color_accent_dim", "Accent Color (Dim)", "color", R"( value="{}" autocomplete="off")")
          HTML_FIELD("color_accent_hover", "Accent Color (Hover)", "color", R"( value="{}" autocomplete="off")")
          ""_cf,
          error_banner(error),
          Escape{site->name}, Escape{site->description},
          Escape{site->icon_url.value_or("")}, Escape{site->banner_url.value_or("")},
          site->color_accent,
          site->color_accent_dim,
          site->color_accent_hover
        )
        .write_home_page_type_select(site->home_page_type)
        .write_voting_select(site->votes_enabled, site->downvotes_enabled)
        .write_fmt(
          HTML_CHECKBOX("cws_enabled", "Allow posts with content warnings (also known as NSFW posts)?", R"( {} autocomplete="off")")
          HTML_CHECKBOX("not_board_creation_admin_only", "Allow non-admin users to create boards?", R"( {} autocomplete="off")")
          HTML_CHECKBOX("registation_enabled", "Allow new users to register?", R"( {} autocomplete="off")")
          HTML_CHECKBOX("registation_application_required", "Require admin approval for registration?", R"( {} autocomplete="off")")
          HTML_TEXTAREA("application_question", "Application question", "", "{}")
          HTML_CHECKBOX("registation_invite_required", "Require invite codes for registration?", R"( {} autocomplete="off")")
          HTML_CHECKBOX("not_invite_admin_only", "Allow non-admin users to generate invite codes?", R"( {} autocomplete="off")")
          R"(<details><summary>Advanced</summary><fieldset><legend class="a11y">Advanced</legend>)"
            HTML_FIELD("post_max_length", "Max post length (bytes)", "number", R"( min="512" value="{:d}" autocomplete="off")")
            HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( {} autocomplete="off")")
            HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( {} autocomplete="off")")
          R"(</fieldset></details><input type="submit" value="Submit"></form>)"_cf,
          site->cws_enabled ? "checked" : "", site->board_creation_admin_only ? "" : "checked",
          site->registration_enabled ? "checked" : "", site->registration_application_required ? "checked" : "",
          Escape{site->application_question.value_or("")},
          site->registration_invite_required ? "checked" : "", site->invite_admin_only ? "" : "checked",
          site->post_max_length, site->javascript_enabled ? "checked" : "", site->infinite_scroll_enabled ? "checked" : ""
        );
      }

      auto write_site_admin_import_export_form() noexcept -> ResponseWriter& {
        return write(
          R"(<form class="form form-page" method="post" action="/site_admin/export"><h2>Export Database</h2>)"
          R"(<input type="hidden" name="for_reals" value="yes">)"
          R"(<p>This will export the <strong>entire database</strong> as a <code>.dbdump.zst</code> file.</p>)"
          R"(<p>The exported file can later be imported using the <code>--import</code> command-line option.</p>)"
          R"(<p>⚠️ <strong>Warning: This is a huge file, and it can take a long time to download!</strong> ⚠️</p>)"
          R"(<input type="submit" value="Download All The Things"></form>)"
        );
      }

      auto write_site_admin_applications_list(
        InstanceController& instance,
        ReadTxn& txn,
        Login login,
        optional<uint64_t> cursor = {},
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<div class="table-page"><h2>Registration Applications</h2>{}<table>)"
          R"(<thead><th>Name<th>Email<th>Date<th>IP Addr<th>User Agent<th class="table-reason">Reason<th>Approved</thead>)"
          R"(<tbody id="application-table">)"_cf,
          error_banner(error)
        );
        bool any_entries = false;
        instance.list_applications([&](auto p){
          any_entries = true;
          auto& [application, detail] = p;
          write_fmt(
            R"(<tr><td>{}<td>{}<td>{:%D}<td>{}<td>{}<td class="table-reason"><div class="reason">{}</div><td class="table-approve">)"_cf,
            Escape{detail.user().name()},
            Escape{detail.local_user().email()},
            detail.created_at(),
            Escape{application.ip()},
            Escape{application.user_agent()},
            Escape{application.text()}
          );
          if (detail.local_user().accepted_application()) {
            write(R"(<span class="a11y">Approved</span>)" ICON("check") "</tr>");
          } else {
            write_fmt(
              R"(<form method="post"><button type="submit" formaction="/site_admin/applications/approve/{0:x}">)"
              R"(<span class="a11y">Approve</span>)" ICON("check") "</button>"
              R"(&nbsp;<button type="submit" formaction="/site_admin/applications/reject/{0:x}">)"
              R"(<span class="a11y">Reject</span>)" ICON("x") "</button></form></tr>"_cf,
              detail.id
            );
          }
        }, txn, login, cursor);
        if (!any_entries) write(R"(<tr><td colspan="7">There's nothing here.</tr>)");
        // TODO: Pagination
        return write("</tbody></table></div>");
      }

      auto write_invites_list(
        InstanceController& instance,
        ReadTxn& txn,
        const LocalUserDetail& login,
        string_view cursor = "",
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<div class="table-page"><h2>Invite Codes</h2>{})"
          R"(<form action="invites/new" method="post"><input type="submit" value="Generate New Invite Code"></form><table>)"
          R"(<thead><th>Code<th>Created<th>Expires<th>Accepted<th>Acceptor</thead>)"
          R"(<tbody id="invite-table">)"_cf,
          error_banner(error)
        );
        bool any_entries = false;
        instance.list_invites_from_user([&](auto p){
          any_entries = true;
          auto& [id, invite] = p;
          write_fmt(
            R"(<tr><td>{}<td>{:%D}<td>)"_cf,
            invite_id_to_code(id),
            uint_to_timestamp(invite.created_at())
          );
          if (auto to = invite.to()) {
            write_fmt(
              R"(N/A<td>{:%D}<td>)"_cf,
              uint_to_timestamp(*invite.accepted_at())
            );
            try {
              auto u = LocalUserDetail::get(txn, *to, login);
              write_user_link(u.user(), u.local_user().admin(), login);
              write("</tr>");
            } catch (...) {
              write("[error]</tr>");
            }
          } else {
            write_fmt(
              R"({:%D}<td>N/A<td>N/A</tr>)"_cf,
              uint_to_timestamp(invite.expires_at())
            );
          }
        }, txn, login.id, cursor);
        if (!any_entries) write(R"(<tr><td colspan="5">There's nothing here.</tr>)");
        // TODO: Pagination
        return write("</tbody></table></div>");
      }

      auto write_first_run_setup_form(
        const FirstRunSetupOptions& options,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/site_admin/first_run_setup">{})"
          HTML_FIELD("name", "What is this server's name?", "text", R"( required value="Ludwig" autocomplete="off")")
          "{}"_cf,
          error_banner(error),
          options.base_url_set ? "" : HTML_FIELD("base_url",
            "What domain will this server be accessed at?<br><strong>Important: This cannot be changed later!</strong>",
            "text",
            R"( required placeholder="https://ludwig.example" pattern="https?://[a-zA-Z0-9_\-]+([.][a-zA-Z0-9_\-]+)*(:\d{1,5})?" autocomplete="off")"
          )
        );
        if (!options.home_page_type_set) write_home_page_type_select();
        write_voting_select();
        return write_fmt(
          HTML_CHECKBOX("cws_enabled", "Allow posts with content warnings (also known as NSFW posts)?", R"( checked autocomplete="off")")
          HTML_CHECKBOX("not_board_creation_admin_only", "Allow non-admin users to create boards?", R"( checked autocomplete="off")")
          HTML_CHECKBOX("registation_enabled", "Allow new users to register?", R"( checked autocomplete="off")")
          HTML_CHECKBOX("registation_application_required", "Require admin approval for registration?", R"( checked autocomplete="off")")
          HTML_TEXTAREA("application_question", "Application question", "", "Why do you want to join?")
          HTML_CHECKBOX("registation_invite_required", "Require invite codes for registration?", R"( autocomplete="off")")
          HTML_CHECKBOX("not_invite_admin_only", "Allow non-admin users to generate invite codes?", R"( autocomplete="off")")
          R"(<details><summary>Advanced</summary><fieldset><legend class="a11y">Advanced</legend><blockquote>)"
            HTML_FIELD("post_max_length", "Max post length (bytes)", "number", R"( min="512" value="1048576" autocomplete="off")")
            HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( checked autocomplete="off")")
            HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( checked autocomplete="off")")
          R"(</blockquote></fieldset></details>{}{}<input type="submit" value="Submit"></form>)"_cf,
          options.admin_exists ? "" : "<fieldset><legend>Create Admin Account</legend>"
            HTML_FIELD("admin_username", "Admin Username", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(" placeholder="admin")")
            HTML_FIELD("admin_password", "Admin Password", "password", " required")
            "</fieldset>",
          options.default_board_exists ? "" : "<fieldset><legend>Create Default Board</legend>"
            HTML_FIELD("default_board_name", "Board Name", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(" placeholder="home")")
            "</fieldset>"
        );
      }

      auto write_user_settings_tabs(const SiteDetail* site, UserSettingsTab selected) noexcept -> ResponseWriter& {
        write(R"(<ul class="tabs">)");
        write_tab(UserSettingsTab::Settings, selected, "Settings", "/settings");
        write_tab(UserSettingsTab::Profile, selected, "Profile", "/settings/profile");
        write_tab(UserSettingsTab::Account, selected, "Account", "/settings/account");
        if (site->registration_invite_required && !site->invite_admin_only) {
          write_tab(UserSettingsTab::Invites, selected, "Invites", "/settings/invites");
        }
        return write("</ul>");
      }

      auto write_user_settings_form(
        const SiteDetail* site,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        int cw_mode = 1;
        const auto& u = login.local_user();
        if (u.hide_cw_posts()) cw_mode = 0;
        else if (u.expand_cw_images()) cw_mode = 3;
        else if (u.expand_cw_posts()) cw_mode = 2;
        write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/settings"><h2>User settings</h2>{})"
          R"(<fieldset><legend>Sorting</legend>)"
          R"(<label for="default_sort_type"><span>Default sort</span>)"_cf,
          error_banner(error)
        );
        write_sort_select("default_sort_type", u.default_sort_type());
        write(R"(</label><label for="default_comment_sort_type"><span>Default comment sort</span>)");
        write_sort_select("default_comment_sort_type", u.default_comment_sort_type());
        write_fmt(
          R"(</label></fieldset><fieldset><legend>Show/Hide</legend>)"
          HTML_CHECKBOX("show_avatars", "Show avatars", "{}")
          ""_cf,
          check(u.show_avatars())
        );
        if (site->votes_enabled) {
          write_fmt(HTML_CHECKBOX("show_karma", "Show karma (score)", "{}") ""_cf, check(u.show_karma()));
        }
        write_fmt(
          HTML_CHECKBOX("show_images_threads", "Show images on threads by default", "{}")
          HTML_CHECKBOX("show_images_comments", "Show inline images in comments by default", "{}")
          HTML_CHECKBOX("show_bot_accounts", "Show bot accounts", "{}")
          HTML_CHECKBOX("show_new_post_notifs", "Show new post notifications", "{}")
          HTML_CHECKBOX("show_read_posts", "Show read posts", "{}")
          ""_cf,
          check(u.show_images_threads()),
          check(u.show_images_comments()),
          check(u.show_bot_accounts()),
          check(u.show_new_post_notifs()),
          check(u.show_read_posts())
        );
        if (site->cws_enabled) {
          write_fmt(
            R"(<label><span>Content warnings</span><select name="content_warnings" autocomplete="off">)"
              R"(<option value="0"{}> Hide posts with content warnings completely)"
              R"(<option value="1"{}> Collapse posts with content warnings (default))"
              R"(<option value="2"{}> Expand text content of posts with content warnings but hide images)"
              R"(<option value="3"{}> Always expand text and images with content warnings)"
            R"(</select></label>)"_cf,
            select(cw_mode, 0), select(cw_mode, 1), select(cw_mode, 2), select(cw_mode, 3)
          );
        }
        write_fmt(
          R"(</fieldset><fieldset><legend>Misc</legend>)"
          HTML_CHECKBOX("open_links_in_new_tab", "Open links in new tab", "{}")
          HTML_CHECKBOX("send_notifications_to_email", "Send notifications to email", "{}")
          ""_cf,
          check(u.open_links_in_new_tab()),
          check(u.send_notifications_to_email())
        );
        if (site->javascript_enabled) {
          write_fmt(HTML_CHECKBOX("javascript_enabled", "JavaScript enabled", "{}") ""_cf, check(u.javascript_enabled()));
        }
        if (site->infinite_scroll_enabled) {
          write_fmt(HTML_CHECKBOX("infinite_scroll_enabled", "Infinite scroll enabled", "{}") ""_cf, check(u.infinite_scroll_enabled()));
        }
        return write(R"(</fieldset><input type="submit" value="Submit"></form>)");
      }

      auto write_user_settings_profile_form(
        const SiteDetail* site,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/settings/profile"><h2>Profile</h2>{})"
          R"(<label for="name"><span>Username</span><output name="name" id="name">{}</output></label>)"
          HTML_FIELD("display_name", "Display name", "text", R"( value="{}")")
          HTML_FIELD("email", "Email address", "email", R"( required value="{}")")
          HTML_TEXTAREA("bio", "Bio", "", "{}")
          HTML_FIELD("avatar_url", "Avatar URL", "text", R"( value="{}")")
          HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}")")
          R"(<input type="submit" value="Submit"></form>)"_cf,
          error_banner(error),
          Escape(login.user().name()),
          Escape(rich_text_to_plain_text(login.user().display_name_type(), login.user().display_name())),
          Escape(login.local_user().email()),
          Escape(login.user().bio_raw()),
          Escape(login.user().avatar_url()),
          Escape(login.user().banner_url())
        );
      }

      auto write_user_settings_account_form(
        const SiteDetail* site,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/settings/account"><h2>Change password</h2>{})"
          HTML_FIELD("old_password", "Old password", "password", R"( required autocomplete="off")")
          HTML_FIELD("password", "New password", "password", R"( required autocomplete="off")")
          HTML_FIELD("confirm_password", "Confirm new password", "password", R"( required autocomplete="off")")
          R"(<input type="submit" value="Submit"></form><br>)"
          R"(<form data-component="Form" class="form form-page" method="post" action="/settings/delete_account"><h2>Delete account</h2>)"
          R"(<p>⚠️ <strong>Warning: This cannot be undone!</strong> ⚠️</p>)"
          HTML_FIELD("delete_password", "Type your password here", "password", R"( required autocomplete="off")")
          HTML_FIELD("delete_confirm", R"(Type "delete" here to confirm)", "text", R"( required autocomplete="off")")
          HTML_CHECKBOX("delete_posts", "Also delete all of my posts", R"( autocomplete="off")")
          R"(<input type="submit" value="Delete Account"></form>)"_cf,
          error_banner(error)
        );
      }

      auto write_board_settings_form(
        const SiteDetail* site,
        const LocalBoardDetail& board,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/b/{}/settings"><h2>Board settings</h2>{})"
          HTML_FIELD("display_name", "Display name", "text", R"( autocomplete="off" value="{}")")
          HTML_TEXTAREA("description", "Sidebar description", "", "{}")
          HTML_FIELD("content_warning", "Content warning (optional)", "text", R"( autocomplete="off" value="{}")")
          HTML_FIELD("icon_url", "Icon URL", "text", R"( autocomplete="off" value="{}")")
          HTML_FIELD("banner_url", "Banner URL", "text", R"( autocomplete="off" value="{}")")
          HTML_CHECKBOX("private", "Private (only visible to members)", "{}")
          HTML_CHECKBOX("restricted_posting", "Restrict posting to moderators", "{}")
          HTML_CHECKBOX("approve_subscribe", "Approval required to join", "{}")
          //HTML_CHECKBOX("invite_required", "Invite code required to join", "{}")
          //HTML_CHECKBOX("invite_mod_only", "Only moderators can invite new members", "{}")
          ""_cf,
          Escape(board.board().name()), error_banner(error),
          Escape(rich_text_to_plain_text(board.board().display_name_type(), board.board().display_name())),
          Escape(board.board().description_raw()),
          Escape(board.board().content_warning()),
          Escape(board.board().icon_url()),
          Escape(board.board().banner_url()),
          check(board.local_board().private_()),
          check(board.board().restricted_posting()),
          check(board.board().approve_subscribe())
        ) .write_voting_select(board.board().can_upvote(), board.board().can_downvote(), site->votes_enabled, site->downvotes_enabled)
          .write(R"(<input type="submit" value="Submit"></form>)");
      }
    };

    auto writer(Response rsp) -> ResponseWriter { return ResponseWriter(this, rsp); }

    static inline auto write_redirect_to(Response rsp, const Context& c, string_view location) noexcept -> void {
      if (c.is_htmx) {
        rsp->writeStatus(http_status(204))
          ->writeHeader("HX-Redirect", location);
      } else {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Location", location);
      }
      rsp->end();
    }

    static inline auto write_redirect_back(Response rsp, string_view referer) noexcept -> void {
      if (referer.empty()) {
        rsp->writeStatus(http_status(202));
      } else {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Location", referer);
      }
      rsp->end();
    }

    auto serve_static(
      App& app,
      string path,
      string_view mimetype,
      string_view src
    ) noexcept -> void {
      const auto hash = format("\"{:016x}\""_cf, XXH3_64bits(src.data(), src.length()));
      app.get(path, [src, mimetype, hash](auto* res, auto* req) {
        if (req->getHeader("if-none-match") == hash) {
          res->writeStatus(http_status(304))->end();
        } else {
          res->writeHeader("Content-Type", mimetype)
            ->writeHeader("Etag", hash)
            ->end(src);
        }
      });
    }

    static inline auto user_name_param(ReadTxn& txn, Request req, uint16_t param) {
      const auto name = req->getParameter(param);
      const auto user_id = txn.get_user_id_by_name(name);
      if (!user_id) die_fmt(410, R"(User "{}" does not exist)", name);
      return *user_id;
    }

    static inline auto board_name_param(ReadTxn& txn, Request req, uint16_t param) {
      const auto name = req->getParameter(param);
      const auto board_id = txn.get_board_id_by_name(name);
      if (!board_id) die_fmt(410, R"(Board "{}" does not exist)", name);
      return *board_id;
    }

    template <class T>
    auto do_submenu_action(WriteTxn txn, SubmenuAction action, uint64_t user, uint64_t id) -> optional<string> {
      switch (action) {
        case SubmenuAction::Reply:
          return format("/{}/{:x}#reply"_cf, T::noun, id);
        case SubmenuAction::Edit:
          return format("/{}/{:x}/edit"_cf, T::noun, id);
        case SubmenuAction::Delete:
          die(500, "Delete is not yet implemented");
        case SubmenuAction::Share:
          die(500, "Share is not yet implemented");
        case SubmenuAction::Save:
          controller->save_post(std::move(txn), user, id, true);
          return {};
        case SubmenuAction::Unsave:
          controller->save_post(std::move(txn), user, id, false);
          return {};
        case SubmenuAction::Hide:
          controller->hide_post(std::move(txn), user, id, true);
          return {};
        case SubmenuAction::Unhide:
          controller->hide_post(std::move(txn), user, id, false);
          return {};
        case SubmenuAction::Report:
          die(500, "Report is not yet implemented");
        case SubmenuAction::MuteUser: {
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_user(std::move(txn), user, e.author_id(), true);
          return {};
        }
        case SubmenuAction::UnmuteUser: {
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_user(std::move(txn), user, e.author_id(), false);
          return {};
        }
        case SubmenuAction::MuteBoard: {
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_board(std::move(txn), user, e.thread().board(), true);
          return {};
        }
        case SubmenuAction::UnmuteBoard:{
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_board(std::move(txn), user, e.thread().board(), false);
          return {};
        }
        case SubmenuAction::ModRestore:
        case SubmenuAction::ModApprove:
        case SubmenuAction::ModFlag:
        case SubmenuAction::ModLock:
        case SubmenuAction::ModRemove:
        case SubmenuAction::ModRemoveUser:
          die(500, "Mod actions are not yet implemented");
        case SubmenuAction::AdminRestore:
        case SubmenuAction::AdminApprove:
        case SubmenuAction::AdminFlag:
        case SubmenuAction::AdminLock:
        case SubmenuAction::AdminRemove:
        case SubmenuAction::AdminRemoveUser:
        case SubmenuAction::AdminPurge:
        case SubmenuAction::AdminPurgeUser:
          die(500, "Admin actions are not yet implemented");
        case SubmenuAction::None:
          die(400, "No action selected");
      }
      throw ApiError("Invalid action", 400, format("Unrecognized SubmenuAction: {:d}"_cf, action));
    }

    auto feed_route(uint64_t feed_id, Response rsp, Request req, Context& c) -> void {
      auto txn = controller->open_read_txn();
      c.populate(txn);
      const auto sort = parse_sort_type(req->getQuery("sort"), c.login);
      const auto show_threads = req->getQuery("type") != "comments",
        show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
      const auto base_url = format("{}?type={}&sort={}&images={}"_cf,
        req->getUrl(),
        show_threads ? "threads" : "comments",
        EnumNameSortType(sort),
        show_images ? 1 : 0
      );
      if (
        feed_id == InstanceController::FEED_HOME &&
        (!c.logged_in_user_id || txn.list_subscribed_boards(*c.logged_in_user_id).is_done())
      ) {
        feed_id = InstanceController::FEED_LOCAL;
      }
      auto r = writer(rsp);
      if (c.is_htmx) {
        rsp->writeHeader("Content-Type", TYPE_HTML);
        c.write_cookie(rsp);
      } else {
        string title;
        switch (feed_id) {
          case InstanceController::FEED_ALL: title = "All"; break;
          case InstanceController::FEED_LOCAL: title = c.site->name; break;
          case InstanceController::FEED_HOME: title = "Subscribed"; break;
          default: title = "Unknown Feed";
        }
        r.write_html_header(c, {
            .canonical_path = req->getUrl(),
            .banner_link = req->getUrl(),
            .page_title = feed_id == InstanceController::FEED_LOCAL ? "Local" : title,
            .banner_title = title
          })
          .write("<div>")
          .write_sidebar(c.login, c.site)
          .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
          .write_sort_options(req->getUrl(), sort, show_threads, show_images)
          .write(R"(</section><main>)");
      }
      r.write_fmt(R"(<ol class="{}-list{}" id="top-level-list">)"_cf,
        show_threads ? "thread" : "comment",
        c.site->votes_enabled ? "" : " no-votes"
      );
      const auto from = req->getQuery("from");
      bool any_entries = false;
      const auto next = show_threads ?
        controller->list_feed_threads(
          [&](auto& e){r.write_thread_entry(e, c.site, c.login, PostContext::Feed, show_images); any_entries = true;},
          txn, feed_id, sort, c.login, from
        ) :
        controller->list_feed_comments(
          [&](auto& e){r.write_comment_entry(e, c.site, c.login, PostContext::Feed, show_images); any_entries = true;},
          txn, feed_id, sort, c.login, from
        );
      if (!c.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
      r.write("</ol>").write_pagination(base_url, from.empty(), next);
      if (!c.is_htmx) r.write("</main></div>").write_html_footer(c);
      r.finish();
    }

    static inline auto board_header_options(Request req, const Board& board, optional<string_view> title = {}) -> ResponseWriter::HtmlHeaderOptions {
      return {
        .canonical_path = req->getUrl(),
        .banner_link = req->getUrl(),
        .page_title = title,
        .banner_title = display_name_as_text(board),
        .banner_image = board.banner_url() ? optional(format("/media/board/{}/banner.webp"_cf, board.name()->string_view())) : nullopt,
        .card_image = board.icon_url() ? optional(format("/media/board/{}/icon.webp"_cf, board.name()->string_view())) : nullopt
      };
    }

    static inline auto form_to_site_update(QueryString<string_view> body) -> SiteUpdate {
      const auto voting = body.optional_uint("voting");
      return {
        .name = body.optional_string("name"),
        .description = body.optional_string("description"),
        .icon_url = body.optional_string("icon_url"),
        .banner_url = body.optional_string("banner_url"),
        .application_question = body.optional_string("application_question"),
        .post_max_length = body.optional_uint("post_max_length"),
        .remote_post_max_length = body.optional_uint("remote_post_max_length"),
        .home_page_type = body.optional_string("home_page_type").transform(parse_home_page_type),
        .javascript_enabled = body.optional_bool("javascript_enabled"),
        .infinite_scroll_enabled = body.optional_bool("infinite_scroll_enabled"),
        .votes_enabled = voting.transform(λx(x > 0)),
        .downvotes_enabled = voting.transform(λx(x > 1)),
        .cws_enabled = body.optional_bool("cws_enabled"),
        .require_login_to_view = body.optional_bool("require_login_to_view"),
        .board_creation_admin_only = !body.optional_bool("not_board_creation_admin_only"),
        .registration_enabled = body.optional_bool("registation_enabled"),
        .registration_application_required = body.optional_bool("registation_application_required"),
        .registration_invite_required = body.optional_bool("registation_invite_required"),
        .invite_admin_only = !body.optional_bool("not_invite_admin_only")
      };
    }

    static inline void require_admin(const shared_ptr<Webapp<SSL>>& self, Context& c) {
      auto txn = self->controller->open_read_txn();
      const auto login = c.require_login(txn);
      if (!InstanceController::can_change_site_settings(login)) {
        die(403, "Admin login required to perform this action");
      }
    }

    auto register_routes(App& app) -> void {

      // Static Files
      /////////////////////////////////////////////////////

      serve_static(app, "/favicon.ico", "image/vnd.microsoft.icon", twemoji_piano_ico_str());
      serve_static(app, "/static/default-theme.css", TYPE_CSS, default_theme_min_css_str());
      serve_static(app, "/static/htmx.min.js", TYPE_JS, htmx_min_js_str());
      serve_static(app, "/static/ludwig.js", TYPE_JS, ludwig_js_str());
      serve_static(app, "/static/feather-sprite.svg", TYPE_SVG, feather_sprite_svg_str());

      // Pages
      /////////////////////////////////////////////////////

      using Coro = RouterCoroutine<Context>;
      shared_ptr<Webapp<SSL>> self = this->shared_from_this();
      Router<SSL, Context, shared_ptr<Webapp<SSL>>> r(app, self);
      r.get("/", [self](auto* rsp, auto* req, Context& c) {
        if (c.site->setup_done) {
          self->feed_route(c.logged_in_user_id ? InstanceController::FEED_HOME : InstanceController::FEED_LOCAL, rsp, req, c);
        } else {
          auto txn = self->controller->open_read_txn();
          if (!c.require_login(txn).local_user().admin()) {
            die(403, "Only an admin user can perform first-run setup.");
          }
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/",
              .banner_title = "First-Run Setup",
            })
            .write("<main>")
            .write_first_run_setup_form(self->controller->first_run_setup_options(txn))
            .write("</main>")
            .write_html_footer(c)
            .finish();
        }
      });
      r.get("/all", [self](auto* rsp, auto* req, Context& c) {
        self->feed_route(InstanceController::FEED_ALL, rsp, req, c);
      });
      r.get("/local", [self](auto* rsp, auto* req, Context& c) {
        self->feed_route(InstanceController::FEED_LOCAL, rsp, req, c);
      });
      r.get("/boards", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_board_sort_type(req->getQuery("sort"));
        const auto sub = req->getQuery("sub") == "1";
        const auto base_url = format("/boards?local={}&sort={}&sub={}"_cf,
          local ? "1" : "0",
          EnumNameBoardSortType(sort),
          sub ? "1" : "0"
        );
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
        } else {
          r.write_html_header(c, {
              .canonical_path = "/boards",
              .banner_link = "/boards",
              .banner_title = "Boards",
            })
            .write(R"(<div><section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options("/boards", sort, local, sub)
            .write(R"(</section><main>)");
        }
        r.write(R"(<ol class="board-list" id="top-level-list">)");
        bool any_entries = false;
        const auto next = self->controller->list_boards(
          [&](auto& b) { r.write_board_list_entry(b); any_entries = true; },
          txn, sort, local, sub, c.login, req->getQuery("from")
        );
        if (!c.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, req->getQuery("from").empty(), next);
        if (!c.is_htmx) r.write("</main></div>").write_html_footer(c);
        r.finish();
      });
      r.get("/users", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_user_sort_type(req->getQuery("sort"));
        const auto base_url = format("/users?local={}&sort={}"_cf,
          local ? "1" : "0",
          EnumNameUserSortType(sort)
        );
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
        } else {
          r.write_html_header(c, {
              .canonical_path = "/users",
              .banner_link = "/users",
              .banner_title = "Users",
            })
            .write(R"(<div><section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options("/users", sort, local, false)
            .write(R"(</section><main>)");
        }
        r.write(R"(<ol class="user-list" id="top-level-list">)");
        bool any_entries = false;
        const auto next = self->controller->list_users(
          [&](auto& e){r.write_user_list_entry(e, c.login); any_entries = true; },
          txn, sort, local, c.login, req->getQuery("from")
        );
        if (!c.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, req->getQuery("from").empty(), next);
        if (!c.is_htmx) r.write("</main></div>").write_html_footer(c);
        r.finish();
      });
      r.get("/c/:name", [self](auto* rsp, auto* req, Context& c) {
        // Compatibility alias for Lemmy community URLs
        // Needed because some Lemmy apps expect URLs in exactly this format
        write_redirect_to(rsp, c, format("/b/{}"_cf, req->getParameter(0)));
      });
      r.get("/b/:name", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto board_id = board_name_param(txn, req, 0);
        const auto board = self->controller->board_detail(txn, board_id, c.login);
        const auto sort = parse_sort_type(req->getQuery("sort"), c.login);
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
        const auto base_url = format("/b/{}?type={}&sort={}&images={}"_cf,
          board.board().name()->string_view(),
          show_threads ? "threads" : "comments",
          EnumNameSortType(sort),
          show_images ? 1 : 0
        );
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
        } else {
          r.write_html_header(c, board_header_options(req, board.board()))
            .write("<div>")
            .write_sidebar(c.login, c.site, board)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        r.write_fmt(R"(<ol class="{}-list{}" id="top-level-list">)"_cf,
          show_threads ? "thread" : "comment",
          board.should_show_votes(c.login, c.site) ? "" : " no-votes"
        );
        bool any_entries = false;
        const auto from = req->getQuery("from");
        const auto next = show_threads ?
          self->controller->list_board_threads(
            [&](auto& e){r.write_thread_entry(e, c.site, c.login, PostContext::Board, show_images); any_entries = true;},
            txn, board_id, sort, c.login, from
          ) :
          self->controller->list_board_comments(
            [&](auto& e){r.write_comment_entry(e, c.site, c.login, PostContext::Board, show_images); any_entries = true;},
            txn, board_id, sort, c.login, from
          );
        if (!c.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, from.empty(), next);
        if (!c.is_htmx) r.write("</main></div>").write_html_footer(c);
        r.finish();
      });
      r.get("/b/:name/create_thread", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto show_url = req->getQuery("text") != "1";
        const auto login = c.require_login(txn);
        const auto board = self->controller->board_detail(txn, board_id, c.login);
        self->writer(rsp)
          .write_html_header(c, board_header_options(req, board.board(), "Create Thread"))
          .write_create_thread_form(show_url, board, login)
          .write_html_footer(c)
          .finish();
      });
      r.get("/u/:name", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto user_id = user_name_param(txn, req, 0);
        const auto user = self->controller->user_detail(txn, user_id, c.login);
        const auto sort = parse_user_post_sort_type(req->getQuery("sort"));
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
        const auto base_url = format("/u/{}?type={}&sort={}&images={}"_cf,
          user.user().name()->string_view(),
          show_threads ? "threads" : "comments",
          EnumNameUserPostSortType(sort),
          show_images ? 1 : 0
        );
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
        } else {
          r.write_html_header(c, {
              .canonical_path = req->getUrl(),
              .banner_link = req->getUrl(),
              .banner_title = display_name_as_text(user.user()),
              .banner_image = user.user().banner_url() ? optional(format("/media/user/{}/banner.webp"_cf, user.user().name()->string_view())) : nullopt,
              .card_image = user.user().avatar_url() ? optional(format("/media/user/{}/avatar.webp"_cf, user.user().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(c.login, c.site, user)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        r.write_fmt(R"(<ol class="{}-list{}" id="top-level-list">)"_cf,
          show_threads ? "thread" : "comment",
          c.site->votes_enabled ? "" : " no-votes"
        );
        bool any_entries = false;
        const auto from = req->getQuery("from");
        const auto next = show_threads ?
          self->controller->list_user_threads(
            [&](auto& e){r.write_thread_entry(e, c.site, c.login, PostContext::User, show_images); any_entries = true;},
            txn, user_id, sort, c.login, from
          ) :
          self->controller->list_user_comments(
            [&](auto& e){r.write_comment_entry(e, c.site, c.login, PostContext::User, show_images); any_entries = true;},
            txn, user_id, sort, c.login, from
          );
        if (!c.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, from.empty(), next);
        if (!c.is_htmx) r.write("</main></div>").write_html_footer(c);
        r.finish();
      });
      r.get("/thread/:id", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), c.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->thread_detail(txn, id, sort, c.login, req->getQuery("from"));
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, c.site, c.login, show_images, false, false);
        } else {
          r.write_html_header(c, board_header_options(req, detail.board(),
              format("{} - {}"_cf, display_name_as_text(detail.board()), display_name_as_text(detail.thread()))))
            .write("<div>")
            .write_sidebar(c.login, c.site, self->controller->board_detail(txn, detail.thread().board(), c.login))
            .write("<main>")
            .write_thread_view(detail, comments, c.site, c.login, sort, show_images)
            .write("</main></div>")
            .write_html_footer(c);
        }
        r.finish();
      });
      r.get("/thread/:id/edit", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        const auto id = hex_id_param(req, 0);
        const auto login = c.require_login(txn);
        const auto thread = ThreadDetail::get(txn, id, login);
        if (!thread.can_edit(login)) die(403, "Cannot edit this post");
        self->writer(rsp)
          .write_html_header(c, board_header_options(req, thread.board(), "Edit Thread"))
          .write_edit_thread_form(thread, login)
          .write_html_footer(c)
          .finish();
      });
      r.get("/comment/:id", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), c.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->comment_detail(txn, id, sort, c.login, req->getQuery("from"));
        auto r = self->writer(rsp);
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, c.site, c.login, show_images, false, false);
        } else {
          r.write_html_header(c, board_header_options(req, detail.board(),
              format("{} - {}'s comment on “{}”"_cf,
                display_name_as_text(detail.board()),
                display_name_as_text(detail.author()),
                display_name_as_text(detail.thread()))))
            .write("<div>")
            .write_sidebar(c.login, c.site, self->controller->board_detail(txn, detail.thread().board(), c.login))
            .write("<main>")
            .write_comment_view(detail, comments, c.site, c.login, sort, show_images)
            .write("</main></div>")
            .write_html_footer(c);
        }
        r.finish();
      });
      r.get_async("/search", [self](auto* rsp, auto _c) -> Coro {
        auto query = co_await _c.with_request([](Request req) {
          return SearchQuery{
            .query = req->getQuery("search"),
            /// TODO: Other parameters
            .include_threads = true,
            .include_comments = true
          };
        });
        auto& c = co_await _c;
        /*
        self->controller->search_step_1(query, [self, rsp, m = std::move(m), resume = std::move(resume)](auto results) mutable {
          resume([self, rsp, results, m = std::move(m)] {
            auto txn = self->controller->open_read_txn();
            m->populate(txn);
            const auto results_detail = self->controller->search_step_2(txn, results, ITEMS_PER_PAGE, m->login);
            self->writer(rsp)
              .write_html_header(*m, {
                .canonical_path = "/search",
                .banner_title = "Search",
              })
              .write("<div>")
              .write_sidebar(m->login, m->site)
              .write("<main>")
              .write_search_result_list(results_detail, m->site, m->login, true)
              .write("</main></div>")
              .write_html_footer(*m)
              .finish();
          });
        });
        */
      });
      r.get("/create_board", [self](auto* rsp, auto*, Context& c) {
        auto txn = self->controller->open_read_txn();
        const auto& login = c.require_login(txn);
        if (!self->controller->can_create_board(login)) {
          die(403, "User cannot create boards");
        }
        self->writer(rsp)
          .write_html_header(c, {
            .canonical_path = "/create_board",
            .banner_title = "Create Board",
          })
          .write("<main>")
          .write_create_board_form(c.site)
          .write("</main>")
          .write_html_footer(c)
          .finish();
      });
      r.get("/login", [self](auto* rsp, auto*, Context& c) {
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        if (c.login) {
          rsp->writeStatus(http_status(303))
            ->writeHeader("Location", "/")
            ->end();
        } else {
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/login",
              .banner_title = "Login",
            })
            .write_login_form(
              c.site->setup_done ? nullopt : optional(
                txn.get_admin_list().empty()
                  ? "This server is not yet set up. A username and random password should be"
                    " displayed in the server's console log. Log in as this user to continue."
                  : "This server is not yet set up. Log in as an admin user to continue."))
            .write_html_footer(c)
            .finish();
        }
      });
      r.get("/register", [self](auto* rsp, auto*, Context& c) {
        if (!c.site->registration_enabled) die(403, "Registration is not enabled on this site");
        auto txn = self->controller->open_read_txn();
        c.populate(txn);
        if (c.login) {
          rsp->writeStatus(http_status(303))
            ->writeHeader("Location", "/")
            ->end();
        } else {
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/register",
              .banner_title = "Register",
            })
            .write_register_form(c.site)
            .write_html_footer(c)
            .finish();
        }
      });
#     define SETTINGS_PAGE(PATH, TAB, CONTENT, CTX) \
        self->writer(rsp) \
          .write_html_header(CTX, { \
            .canonical_path = PATH, \
            .banner_title = "User Settings", \
          }) \
          .write("<main>") \
          .write_user_settings_tabs((CTX).site, UserSettingsTab::TAB) \
          .CONTENT \
          .write("</main>") \
          .write_html_footer(CTX) \
          .finish();
#     define SETTINGS_ROUTE(PATH, TAB, CONTENT) r.get(PATH, [self](auto* rsp, auto*, Context& c) { \
        auto txn = self->controller->open_read_txn(); \
        const auto login = c.require_login(txn); \
        SETTINGS_PAGE(PATH, TAB, CONTENT, c) \
      });
      SETTINGS_ROUTE("/settings", Settings, write_user_settings_form(c.site, login))
      SETTINGS_ROUTE("/settings/profile", Profile, write_user_settings_profile_form(c.site, login))
      SETTINGS_ROUTE("/settings/account", Account, write_user_settings_account_form(c.site, login))
      SETTINGS_ROUTE("/settings/invites", Invites, write_invites_list(*self->controller, txn, login, ""))
      r.get("/b/:name/settings", [self](auto* rsp, auto* req, Context& c) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto login = c.require_login(txn);
        const auto board = self->controller->local_board_detail(txn, board_id, c.login);
        if (!login.local_user().admin() && login.id != board.local_board().owner()) {
          die(403, "Must be admin or board owner to view this page");
        }
        self->writer(rsp)
          .write_html_header(c, board_header_options(req, board.board(), "Board Settings"))
          .write("<main>")
          .write_board_settings_form(c.site, board)
          .write("</main>")
          .write_html_footer(c)
          .finish();
      });
#     define ADMIN_PAGE(PATH, TAB, CONTENT, CTX) \
        self->writer(rsp) \
          .write_html_header(CTX, { \
            .canonical_path = PATH, \
            .banner_title = "Site Admin", \
          }) \
          .write("<main>") \
          .write_site_admin_tabs((CTX).site, SiteAdminTab::TAB) \
          .CONTENT \
          .write("</main>") \
          .write_html_footer(CTX) \
          .finish();
#     define ADMIN_ROUTE(PATH, TAB, CONTENT) r.get(PATH, [self](auto* rsp, auto*, Context& c) { \
        auto txn = self->controller->open_read_txn(); \
        const auto login = c.require_login(txn); \
        if (!InstanceController::can_change_site_settings(login)) { \
          die(403, "Admin login required to view this page"); \
        } \
        ADMIN_PAGE(PATH, TAB, CONTENT, c) \
      });
      ADMIN_ROUTE("/site_admin", Settings, write_site_admin_form(c.site))
      ADMIN_ROUTE("/site_admin/import_export", ImportExport, write_site_admin_import_export_form())
      ADMIN_ROUTE("/site_admin/applications", Applications, write_site_admin_applications_list(*self->controller, txn, c.login, {}))
      ADMIN_ROUTE("/site_admin/invites", Invites, write_invites_list(*self->controller, txn, login, ""))

      // API Actions
      //////////////////////////////////////////////////////

#     define WRITE_TXN co_await self->controller->template open_write_txn<Context>()

      r.get("/logout", [self](auto* rsp, auto* req, Context&) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
        if (req->getHeader("referer").empty()) rsp->writeHeader("Location", "/");
        else rsp->writeHeader("Location", req->getHeader("referer"));
        rsp->end();
      });
      r.post_form("/login", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        if (c.logged_in_user_id) die(403, "Already logged in");
        auto referer = co_await _c.with_request([](Request req){ return string(req->getHeader("referer")); });
        auto form = co_await body;
        if (form.optional_string("username") /* actually a honeypot */) {
          spdlog::warn("Caught a bot with honeypot field on login");
          // just leave the connecting hanging, let the bots time out
          rsp->writeStatus(http_status(418));
          co_return;
        }
        bool remember = form.optional_bool("remember");
        try {
          auto login = self->controller->login(
            WRITE_TXN,
            form.required_string("actual_username"),
            form.required_string("password"),
            c.ip,
            c.user_agent,
            remember
          );
          rsp->writeStatus(http_status(303))
            ->writeHeader("Set-Cookie",
              format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}"_cf,
                login.session_id, fmt::gmtime(login.expiration)))
            ->writeHeader("Location", (referer.empty() || referer == "/login" || !self->controller->site_detail()->setup_done) ? "/" : referer)
            ->end();
        } catch (ApiError e) {
          rsp->writeStatus(http_status(e.http_status));
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/login",
              .banner_title = "Login",
            })
            .write_login_form({e.message})
            .write_html_footer(c)
            .finish();
        }
      });
      r.post_form("/register", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        if (!c.site->registration_enabled) die(403, "Registration is not enabled on this site");
        if (c.logged_in_user_id) die(403, "Already logged in");
        auto referer = co_await _c.with_request([](Request req){ return string(req->getHeader("referer")); });
        auto form = co_await body;
        if (form.optional_string("username") /* actually a honeypot */) {
          spdlog::warn("Caught a bot with honeypot field on register");
          // just leave the connecting hanging, let the bots time out
          rsp->writeStatus(http_status(418));
          co_return;
        }
        try {
          SecretString password = form.required_string("password"),
            confirm_password = form.required_string("confirm_password");
          if (password.data != confirm_password.data) {
            die(400, "Passwords do not match");
          }
          self->controller->register_local_user(
            WRITE_TXN,
            form.required_string("actual_username"),
            form.required_string("email"),
            std::move(password),
            rsp->getRemoteAddressAsText(),
            c.user_agent,
            form.optional_string("invite_code").and_then(invite_code_to_id),
            form.optional_string("application_reason")
          );
        } catch (ApiError e) {
          rsp->writeStatus(http_status(e.http_status));
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/register",
              .banner_title = "Register",
            })
            .write_register_form(c.site, {e.message})
            .write_html_footer(c)
            .finish();
          co_return;
        }
        self->writer(rsp)
          .write_html_header(c, {
            .canonical_path = "/register",
            .banner_title = "Register",
          })
          .write(R"(<main><div class="form form-page"><h2>Registration complete!</h2>)"
            R"(<p>Log in to your new account:</p><p><a class="big-button" href="/login">Login</a></p>)"
            "</div></main>")
          .write_html_footer(c)
          .finish();
      });
      r.post_form("/create_board", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        auto user = c.require_login();
        auto form = co_await body;
        const auto name = form.required_string("name");
        self->controller->create_local_board(
          WRITE_TXN,
          user,
          name,
          form.optional_string("display_name"),
          form.optional_string("content_warning"),
          form.optional_bool("private"),
          form.optional_bool("restricted_posting"),
          form.optional_bool("local_only")
        );
        rsp->writeStatus(http_status(303));
        c.write_cookie(rsp);
        rsp->writeHeader("Location", format("/b/{}"_cf, name))->end();
      });
      r.post_form("/b/:name/create_thread", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        auto user = c.require_login();
        const auto board_id = co_await _c.with_request([&](Request req) {
          auto txn = self->controller->open_read_txn();
          return board_name_param(txn, req, 0);
        });
        auto form = co_await body;
        const auto id = self->controller->create_local_thread(
          WRITE_TXN,
          user,
          board_id,
          form.required_string("title"),
          form.optional_string("submission_url"),
          form.optional_string("text_content"),
          form.optional_string("content_warning")
        );
        rsp->writeStatus(http_status(303));
        c.write_cookie(rsp);
        rsp->writeHeader("Location", format("/thread/{:x}"_cf, id))->end();
      });
      r.post_form("/thread/:id/reply", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto thread_id = co_await _c.with_request([](Request req){ return hex_id_param(req, 0); });
        auto user = c.require_login();
        auto form = co_await body;
        const auto id = self->controller->create_local_comment(
          WRITE_TXN,
          user,
          thread_id,
          form.required_string("text_content"),
          form.optional_string("content_warning")
        );
        if (c.is_htmx) {
          auto txn = self->controller->open_read_txn();
          CommentTree tree;
          tree.emplace(thread_id, CommentDetail::get(txn, id, c.login));
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          self->writer(rsp)
            .write_comment_tree(tree, thread_id, CommentSortType::New, c.site, c.login, true, true, false)
            .write_toast("Reply submitted")
            .finish();
        } else {
          rsp->writeStatus(http_status(303));
          c.write_cookie(rsp);
          rsp->writeHeader("Location", format("/thread/{:x}"_cf, thread_id))->end();
        }
      });
      r.post_form("/comment/:id/reply", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto comment_id = co_await _c.with_request([](Request req){ return hex_id_param(req, 0); });
        auto user = c.require_login();
        auto form = co_await body;
        const auto id = self->controller->create_local_comment(
          WRITE_TXN,
          user,
          comment_id,
          form.required_string("text_content"),
          form.optional_string("content_warning")
        );
        if (c.is_htmx) {
          auto txn = self->controller->open_read_txn();
          CommentTree tree;
          tree.emplace(comment_id, CommentDetail::get(txn, id, c.login));
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          self->writer(rsp)
            .write_comment_tree(tree, comment_id, CommentSortType::New, c.site, c.login, true, true, false)
            .write_toast("Reply submitted")
            .finish();
        } else {
          rsp->writeStatus(http_status(303));
          c.write_cookie(rsp);
          rsp->writeHeader("Location", format("/comment/{:x}"_cf, comment_id))->end();
        }
      });
      r.post_form("/thread/:id/action", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto [id, referer] = co_await _c.with_request([](Request req) {
          return std::pair(hex_id_param(req, 0), string(req->getHeader("referer")));
        });
        auto user = c.require_login();
        auto form = co_await body;
        const auto action = static_cast<SubmenuAction>(form.required_int("action"));
        const auto redirect = self->template do_submenu_action<ThreadDetail>(
          WRITE_TXN, action, user, id
        );
        if (redirect) {
          write_redirect_to(rsp, c, *redirect);
        } else if (c.is_htmx) {
          const auto context = static_cast<PostContext>(form.required_int("context"));
          auto txn = self->controller->open_read_txn();
          const auto thread = ThreadDetail::get(txn, id, c.login);
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          self->writer(rsp)
            .write_controls_submenu(thread, c.login, context)
            .finish();
        } else {
          write_redirect_back(rsp, referer);
        }
      });
      r.post_form("/comment/:id/action", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto [id, referer] = co_await _c.with_request([](Request req) {
          return std::pair(hex_id_param(req, 0), string(req->getHeader("referer")));
        });
        auto user = c.require_login();
        auto form = co_await body;
        const auto action = static_cast<SubmenuAction>(form.required_int("action"));
        const auto redirect = self->template do_submenu_action<CommentDetail>(
          WRITE_TXN, action, user, id
        );
        if (redirect) {
          write_redirect_to(rsp, c, *redirect);
        } else if (c.is_htmx) {
          const auto context = static_cast<PostContext>(form.required_int("context"));
          auto txn = self->controller->open_read_txn();
          const auto comment = CommentDetail::get(txn, id, c.login);
          rsp->writeHeader("Content-Type", TYPE_HTML);
          c.write_cookie(rsp);
          self->writer(rsp)
            .write_controls_submenu(comment, c.login, context)
            .finish();
        } else {
          write_redirect_back(rsp, referer);
        }
      });
      r.post_form("/thread/:id/vote", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto [post_id, referer] = co_await _c.with_request([](Request req) {
          return std::pair(hex_id_param(req, 0), string(req->getHeader("referer")));
        });
        auto user = c.require_login();
        auto form = co_await body;
        const auto vote = form.required_vote("vote");
        self->controller->vote(WRITE_TXN, user, post_id, vote);
        if (c.is_htmx) {
          auto txn = self->controller->open_read_txn();
          const auto thread = ThreadDetail::get(txn, post_id, c.login);
          rsp->writeHeader("Content-Type", TYPE_HTML);
          self->writer(rsp).write_vote_buttons(thread, c.site, c.login).finish();
        } else {
          write_redirect_back(rsp, referer);
        }
      });
      r.post_form("/comment/:id/vote", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto [post_id, referer] = co_await _c.with_request([](Request req) {
          return std::pair(hex_id_param(req, 0), string(req->getHeader("referer")));
        });
        auto user = c.require_login();
        auto form = co_await body;
        const auto vote = form.required_vote("vote");
        self->controller->vote(WRITE_TXN, user, post_id, vote);
        if (c.is_htmx) {
          auto txn = self->controller->open_read_txn();
          const auto comment = CommentDetail::get(txn, post_id, c.login);
          rsp->writeHeader("Content-Type", TYPE_HTML);
          self->writer(rsp).write_vote_buttons(comment, c.site, c.login).finish();
        } else {
          write_redirect_back(rsp, referer);
        }
      });
      r.post_form("/b/:name/subscribe", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        const auto [name, board_id, referer] = co_await _c.with_request([&](Request req) {
          auto txn = self->controller->open_read_txn();
          return std::tuple(string(req->getParameter(0)), board_name_param(txn, req, 0), string(req->getHeader("referer")));
        });
        auto user = c.require_login();
        auto form = co_await body;
        self->controller->subscribe(WRITE_TXN, user, board_id, !form.optional_bool("unsubscribe"));
        if (c.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          self->writer(rsp).write_subscribe_button(name, !form.optional_bool("unsubscribe")).finish();
        } else {
          write_redirect_back(rsp, referer);
        }
      });
      r.post("/settings/invites/new", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        if (!c.site->registration_invite_required || c.site->invite_admin_only) {
          die(403, "Users cannot generate invite codes on this server");
        }
        auto txn = self->controller->open_read_txn();
        const auto login = c.require_login(txn);
        if (login.mod_state().state >= ModState::Locked) {
          die(403, "User does not have permission to create an invite code");
        }
        self->controller->create_site_invite(WRITE_TXN, login.id);
        write_redirect_back(rsp, "/settings/invites");
      });
      r.post_form("/site_admin", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        require_admin(self, c);
        auto form = co_await body;
        try {
          self->controller->update_site(WRITE_TXN, form_to_site_update(form), c.logged_in_user_id);
          write_redirect_back(rsp, "/site_admin");
        } catch (const ApiError& e) {
          rsp->writeStatus(http_status(e.http_status));
          ADMIN_PAGE("/site_admin", Settings, write_site_admin_form(c.site, {e.message}), c)
        }
      });
      r.post_form("/site_admin/first_run_setup", [self](auto* rsp, auto _c, auto body) -> Coro {
        auto& c = co_await _c;
        if (c.site->setup_done) {
          die(403, "First-run setup is already complete");
        }
        require_admin(self, c);
        auto form = co_await body;
        try {
          self->controller->first_run_setup(WRITE_TXN, {
            form_to_site_update(form),
            form.optional_string("base_url"),
            form.optional_string("default_board_name"),
            form.optional_string("admin_username"),
            form.optional_string("admin_password").transform(λx(SecretString(x)))
          });
          write_redirect_back(rsp, "/");
        } catch (const ApiError& e) {
          rsp->writeStatus(http_status(e.http_status));
          auto txn = self->controller->open_read_txn();
          self->writer(rsp)
            .write_html_header(c, {
              .canonical_path = "/",
              .banner_title = "First-Run Setup",
            })
            .write("<main>")
            .write_first_run_setup_form(self->controller->first_run_setup_options(txn), e.message)
            .write("</main>")
            .write_html_footer(c)
            .finish();
        }
      });
      r.post("/site_admin/export", [self](auto* rsp, auto _c, auto) -> Coro {
        auto& c = co_await _c;
        require_admin(self, c);
        rsp->writeHeader("Content-Type", "application/zstd")
          ->writeHeader(
            "Content-Disposition",
            format(R"(attachment; filename="ludwig-{:%F-%H%M%S}.dbdump.zst")"_cf, now_t())
          );
        struct DumpAwaiter : public RouterAwaiter<std::monostate, Context> {
          DumpAwaiter(shared_ptr<Webapp<SSL>> self, Context& c) {
            std::thread([this, self, &c] mutable {
              spdlog::info("Beginning database dump");
              std::binary_semaphore lock(0);
              try {
                auto txn = self->controller->open_read_txn();
                for (auto chunk : zstd_db_dump_export(txn)) {
                  {
                    std::lock_guard<std::mutex> g(this->mutex);
                    if (this->canceled) return;
                  }
                  c.on_response_thread([&](Response rsp) {
                    std::lock_guard<std::mutex> g(this->mutex);
                    if (!this->canceled) {
                      rsp->write(string_view{(const char*)chunk.data(), chunk.size()});
                    }
                    lock.release();
                  });
                  lock.acquire();
                }
                spdlog::info("Database dump completed successfully");
                this->set_value({});
              } catch (const std::exception& e) {
                spdlog::error("Database dump failed: {}", e.what());
                this->cancel();
              }
            }).detach();
          }
        };
        co_await DumpAwaiter(self, c);
        rsp->end();
      });
      r.post("/site_admin/applications/:action/:id", [self](auto* rsp, auto _c, auto) -> Coro {
        auto [is_approve, id] = co_await _c.with_request([](Request req) {
          bool is_approve;
          if (req->getParameter(0) == "approve") is_approve = true;
          else if (req->getParameter(0) == "reject") is_approve = false;
          else die(404, "Page not found");
          return std::pair(is_approve, hex_id_param(req, 1));
        });
        auto& c = co_await _c;
        require_admin(self, c);
        try {
          if (is_approve) {
            self->controller->approve_local_user_application(WRITE_TXN, id, c.logged_in_user_id);
          } else {
            self->controller->reject_local_user_application(WRITE_TXN, id, c.logged_in_user_id);
          }
          write_redirect_back(rsp, "/site_admin/applications");
        } catch (const ApiError& e) {
          rsp->writeStatus(http_status(e.http_status));
          auto txn = self->controller->open_read_txn();
          ADMIN_PAGE("/site_admin/applications", Applications, write_site_admin_applications_list(*self->controller, txn, c.login, {}, e.message), c)
        }
      });
      r.post("/site_admin/invites/new", [self](auto* rsp, auto _c, auto) -> Coro {
        auto& c = co_await _c;
        require_admin(self, c);
        self->controller->create_site_invite(WRITE_TXN, c.logged_in_user_id);
        write_redirect_back(rsp, "/site_admin/invites");
      });
      r.any("/*", [](auto*, auto*, auto&) {
        die(404, "Page not found");
      });
    }
  };

  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void {
    auto router = std::make_shared<Webapp<SSL>>(controller, rl);
    router->register_routes(app);
  }

#ifndef LUDWIG_DEBUG
  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;
#endif

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;
}
