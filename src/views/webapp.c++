#include "views/webapp.h++"
#include "util/web.h++"
#include "util/zstd_db_dump.h++"
#include "models/enums.h++"
#include "static/default-theme.css.h++"
#include "static/htmx.min.js.h++"
#include "static/ludwig.js.h++"
#include "static/feather-sprite.svg.h++"
#include "static/twemoji-piano.ico.h++"
#include <iterator>
#include <regex>
#include <spdlog/fmt/chrono.h>
#include <xxhash.h>
#include "util/lambda_macros.h++"

using std::bind, std::match_results, std::monostate, std::nullopt,
    std::optional, std::regex, std::regex_search, std::shared_ptr, std::stoull,
    std::string, std::string_view, std::variant, std::visit;

using namespace std::placeholders;
namespace chrono = std::chrono;

#define COOKIE_NAME "ludwig_session"

namespace Ludwig {
  struct Suffixed { int64_t n; };
  struct RelativeTime { chrono::system_clock::time_point t; };
}

namespace fmt {
  template <> struct formatter<Ludwig::Suffixed> : public Ludwig::CustomFormatter {
    // Adapted from https://programming.guide/java/formatting-byte-size-to-human-readable-format.html
    auto format(Ludwig::Suffixed x, format_context& ctx) const {
      static constexpr auto SUFFIXES = "KMBTqQ";
      auto n = x.n;
      if (-1000 < n && n < 1000) return format_to(ctx.out(), "{:d}", n);
      uint8_t i = 0;
      while (n <= -999'950 || n >= 999'950) {
        n /= 1000;
        i++;
      }
      return format_to(ctx.out(), "{:.3g}{:c}", (double)n / 1000.0, SUFFIXES[i]);
      // SUFFIXES[i] can never overflow, max 64-bit int is ~18 quintillion (Q)
    }
  };

  template <> struct formatter<Ludwig::RelativeTime> : public Ludwig::CustomFormatter {
    static auto write(format_context& ctx, const char s[]) {
      return std::copy(s, s + std::char_traits<char>::length(s), ctx.out());
    }

    auto format(Ludwig::RelativeTime x, format_context& ctx) const {
      using namespace chrono;
      const auto now = system_clock::now();
      if (x.t > now) return write(ctx, "in the future");
      const auto diff = now - x.t;
      if (diff < 1min) return write(ctx, "just now");
      if (diff < 2min) return write(ctx, "1 minute ago");
      if (diff < 1h) return format_to(ctx.out(), "{:d} minutes ago", duration_cast<minutes>(diff).count());
      if (diff < 2h) return write(ctx, "1 hour ago");
      if (diff < days{1}) return format_to(ctx.out(), "{:d} hours ago", duration_cast<hours>(diff).count());
      if (diff < days{2}) return write(ctx, "1 day ago");
      if (diff < weeks{1}) return format_to(ctx.out(), "{:d} days ago", duration_cast<days>(diff).count());
      if (diff < weeks{2}) return write(ctx, "1 week ago");
      if (diff < months{1}) return format_to(ctx.out(), "{:d} weeks ago", duration_cast<weeks>(diff).count());
      if (diff < months{2}) return write(ctx, "1 month ago");
      if (diff < years{1}) return format_to(ctx.out(), "{:d} months ago", duration_cast<months>(diff).count());
      if (diff < years{2}) return write(ctx, "1 year ago");
      return format_to(ctx.out(), "{:d} years ago", duration_cast<years>(diff).count());
    }
  };
}

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
    ModFlag,
    ModLock,
    ModRemove,
    ModBan,
    ModPurge,
    ModPurgeUser
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
      case ModState::Removed: return "Removed";
      default: return "";
    }
  }

  template <bool SSL> struct Webapp : public std::enable_shared_from_this<Webapp<SSL>> {
    shared_ptr<InstanceController> controller;
    shared_ptr<KeyedRateLimiter> rate_limiter; // may be null!

    Webapp(
      shared_ptr<InstanceController> controller,
      shared_ptr<KeyedRateLimiter> rl
    ) : controller(controller), rate_limiter(rl) {}

    using App = uWS::TemplatedApp<SSL>;
    using Response = uWS::HttpResponse<SSL>*;
    using Request = uWS::HttpRequest*;

    struct ErrorMeta {
      bool is_get, is_htmx;
    };

    auto error_middleware(const uWS::HttpResponse<SSL>*, Request req) noexcept -> ErrorMeta {
      return {
        .is_get = req->getMethod() == "get",
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty()
      };
    }

    struct Meta {
      chrono::time_point<chrono::steady_clock> start;
      optional<uint64_t> logged_in_user_id;
      optional<string> session_cookie;
      string ip;
      bool is_htmx;
      const SiteDetail* site;
      optional<LocalUserDetail> login;

      auto populate(ReadTxnBase& txn) {
        if (logged_in_user_id) {
          if (*logged_in_user_id) login.emplace(LocalUserDetail::get_login(txn, *logged_in_user_id));
          else if (!site->setup_done) {
            spdlog::warn("Using temporary admin user");
            login.emplace(LocalUserDetail::temp_admin());
          }
        }
      }

      auto require_login() {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        const auto id = *logged_in_user_id;
        if (!id && site->setup_done) throw ApiError("Site is set up, temporary login is no longer valid", 401);
        return id;
      }

      auto require_login(ReadTxnBase& txn) -> const LocalUserDetail& {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        if (!login) populate(txn);
        if (!login) throw ApiError("Site is set up, temporary login is no longer valid", 401);
        return *login;
      }

      auto write_cookie(Response rsp) const noexcept {
        if (session_cookie) rsp->writeHeader("Set-Cookie", *session_cookie);
      }

      auto time_elapsed() const noexcept {
        const auto end = chrono::steady_clock::now();
        return chrono::duration_cast<chrono::microseconds>(end - start).count();
      }
    };

    auto get_auth_cookie(Request req, const std::string& ip) -> std::pair<optional<LoginResponse>, optional<string>> {
      const auto cookies = req->getHeader("cookie");
      match_results<string_view::const_iterator> match;
      if (!regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) return {{}, {}};
      try {
        auto txn = controller->open_read_txn();
        const auto old_session = stoull(match[1], nullptr, 16);
        auto new_session = controller->validate_or_regenerate_session(
          txn, old_session, ip, req->getHeader("user-agent")
        );
        if (!new_session) throw std::runtime_error("expired session");
        if (new_session->session_id != old_session) {
          spdlog::debug("Regenerated session {:x} as {:x}", old_session, new_session->session_id);
          return {
            new_session,
            fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
              new_session->session_id, fmt::gmtime(new_session->expiration))
          };
        }
        return {new_session, {}};
      } catch (...) {
        spdlog::debug("Auth cookie is invalid; requesting deletion");
        return {{}, COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"};
      }
    }

    auto middleware(Response rsp, Request req) -> Meta {
      using namespace chrono;
      const auto start = steady_clock::now();
      const string ip(get_ip(rsp, req));

      if (rate_limiter && !rate_limiter->try_acquire(ip, req->getMethod() == "GET" ? 1 : 10)) {
        throw ApiError("Rate limited, try again later", 429);
      }

      const auto [new_session, session_cookie] = get_auth_cookie(req, ip);
      auto site = controller->site_detail();
      if (!new_session) {
        if (site->require_login_to_view && req->getUrl() != "/login") {
          throw ApiError("Login is required to view this page", 401);
        }
        if (!site->setup_done && req->getUrl() != "/login") {
          throw ApiError("First-run setup is not complete. Log in as an admin user to complete site setup. If no admin user exists, check console output for a randomly-generated password.", 401);
        }
      } else if (!site->setup_done) {
        if (req->getUrl() != "/" && req->getUrl() != "/login" && req->getUrl() != "/logout" && req->getUrl() != "/site_admin/first_run_setup") {
          throw ApiError("First-run setup is not complete. This page is not yet accessible.", 403);
        }
      }

      return {
        .start = start,
        .logged_in_user_id = new_session.transform(λx(x.user_id)),
        .session_cookie = session_cookie,
        .ip = ip,
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty(),
        .site = site
      };
    }

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

      ResponseWriter(Webapp<SSL>* w, Response rsp) : controller(*w->controller), rsp(rsp) { buf.reserve(1024); }
      operator Response() { return rsp; }
      auto write(string_view s) noexcept -> ResponseWriter& { buf.append(s); return *this; }
      auto finish() -> void { rsp->end(buf); }
      template <typename... Args> auto write_fmt(fmt::format_string<Args...> fmt, Args&&... args) noexcept -> ResponseWriter& {
        fmt::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
        return *this;
      }

      auto write_toast(string_view content, string_view extra_classes = "") noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<div hx-swap-oob="afterbegin:#toasts">)"
          R"(<p class="toast{}" aria-live="polite" hx-get="data:text/html," hx-trigger="click, every 30s" hx-swap="delete">{}</p>)"
          "</div>",
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
        return fmt::format("{}", Escape(name.substr(0, name.find('@'))));
      }

      auto display_name_as_html(const Board& board) -> string {
        if (board.display_name_type() && board.display_name_type()->size()) {
          return rich_text_to_html_emojis_only(board.display_name_type(), board.display_name(), {});
        }
        const auto name = board.name()->string_view();
        return fmt::format("{}", Escape(name.substr(0, name.find('@'))));
      }

      struct HtmlHeaderOptions {
        optional<string_view> canonical_path, banner_link, page_title;
        optional<string> banner_title, banner_image, card_image;
      };

      auto write_html_header(const Meta& m, HtmlHeaderOptions opt) noexcept -> ResponseWriter& {
        assert(m.site != nullptr);
        rsp->writeHeader("Content-Type", TYPE_HTML);
        m.write_cookie(rsp);
        write_fmt(
          R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
          R"(<meta name="viewport" content="width=device-width,initial-scale=1">)"
          R"(<meta name="referrer" content="same-origin"><title>{}{}{}</title>)"
          R"(<style type="text/css">body{}--color-accent:{}!important;--color-accent-dim:{}!important;--color-accent-hover:{}!important;{}</style>)"
          R"(<link rel="stylesheet" href="/static/default-theme.css">)",
          Escape{m.site->name},
          (opt.page_title || opt.banner_title) ? " - " : "",
          Escape{
            opt.page_title ? *opt.page_title :
            opt.banner_title ? *opt.banner_title :
            ""
          },
          "{",
          m.site->color_accent,
          m.site->color_accent_dim,
          m.site->color_accent_hover,
          "}"
        );
        if (m.site->javascript_enabled) {
          write(
            R"(<script src="/static/htmx.min.js"></script>)"
            R"(<script src="/static/ludwig.js"></script>)"
          );
        }
        if (opt.canonical_path) {
          write_fmt(
            R"(<link rel="canonical" href="{0}{1}">)"
            R"(<meta property="og:url" content="{0}{1}">)"
            R"(<meta property="twitter:url" content="{0}{1}">)",
            Escape{m.site->base_url}, Escape{*opt.canonical_path}
          );
        }
        if (opt.page_title) {
          write_fmt(
            R"(<meta property="title" href="{0} - {1}">)"
            R"(<meta property="og:title" content="{0} - {1}">)"
            R"(<meta property="twitter:title" content="{0} - {1}">)"
            R"(<meta property="og:type" content="website">)",
            Escape{m.site->name}, Escape{*opt.page_title}
          );
        }
        if (opt.card_image) {
          write_fmt(
            R"(<meta property="og:image" content="{0}">)"
            R"(<meta property="twitter:image" content="{0}>)"
            R"(<meta property="twitter:card" content="summary_large_image">)",
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
          R"(<li><a href="/users">Users</a>)",
          Escape{m.site->name}
        );
        if (m.login) {
          write_fmt(
            R"(</ul><ul>)"
            R"(<li id="topbar-user"><a href="/u/{}">{}</a> ({:d}))"
            R"(<li><a href="/settings">Settings</a>{}<li><a href="/logout">Logout</a></ul></nav>)",
            Escape(m.login->user().name()),
            display_name_as_html(m.login->user()),
            m.login->stats().thread_karma() + m.login->stats().comment_karma(),
            InstanceController::can_change_site_settings(m.login) ? R"(<li><a href="/site_admin">Site admin</a>)" : ""
          );
        } else if (m.site->registration_enabled) {
          write(R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)");
        } else {
          write(R"(</ul><ul><li><a href="/login">Login</a></ul></nav>)");
        }
        if (m.login) {
          if (!m.login->local_user().approved()) {
            write(R"(<div id="banner-not-approved" class="banner">Your account is not yet approved. You cannot post, vote, or subscribe to boards.</div>)");
          }
          if (m.login->user().mod_state() >= ModState::Locked) {
            write(R"(<div id="banner-locked" class="banner">Your account is locked. You cannot post, vote, or subscribe to boards.</div>)");
          }
        }
        write(R"(<div id="toasts"></div>)");
        if (opt.banner_title) {
          write(R"(<header id="page-header")");
          if (opt.banner_image) {
            write_fmt(R"( class="banner-image" style="background-image:url('{}');")", Escape{*opt.banner_image});
          }
          if (opt.banner_link) {
            write_fmt(
              R"(><h1><a class="page-header-link" href="{}">{}</a></h1></header>)",
              Escape{*opt.banner_link}, Escape{*opt.banner_title}
            );
          } else {
            write_fmt("><h1>{}</h1></header>", Escape{*opt.banner_title});
          }
        }
        return *this;
      }

      auto write_html_footer(const Meta& m) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<div class="spacer"></div><footer><small>Powered by <a href="https://github.com/ar-nelson/ludwig">Ludwig</a>)"
          R"( · v{})"
#         ifdef LUDWIG_DEBUG
          " (DEBUG BUILD)"
#         endif
          R"( · Generated in {:L}μs</small></footer></body></html>)",
          VERSION,
          m.time_elapsed()
        );
      }

#     define HTML_FIELD(ID, LABEL, TYPE, EXTRA) \
        "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"" TYPE "\" name=\"" ID "\" id=\"" ID "\"" EXTRA "></label>"
#     define HTML_CHECKBOX(ID, LABEL, EXTRA) \
        "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"checkbox\" class=\"a11y\" name=\"" ID "\" id=\"" ID "\"" EXTRA "><div class=\"toggle-switch\"></div></label>"
#     define HTML_TEXTAREA(ID, LABEL, EXTRA, CONTENT) \
        "<label for=\"" ID "\"><span>" LABEL "</span><div><textarea name=\"" ID "\" id=\"" ID "\"" EXTRA ">" CONTENT \
        R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"

      auto write_subscribe_button(string_view name, bool is_unsubscribe) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form method="post" action="/b/{0}/subscribe" hx-post="/b/{0}/subscribe" hx-swap="outerHTML">{1})"
          R"(<button type="submit" class="big-button">{2}</button>)"
          "</form>",
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
          R"(<label id="sidebar-toggle-label" for="sidebar-toggle"><svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#menu"></svg> Menu</label>)"
          R"(<input type="checkbox" name="sidebar-toggle" id="sidebar-toggle" class="a11y">)"
          R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
          R"(<form action="/search" id="search-form">)"
          R"(<label for="search"><span class="a11y">Search</span>)"
          R"(<input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)"
        );
        const auto hide_cw = login && login->local_user().hide_cw_posts();
        const optional<BoardDetail> board =
          std::holds_alternative<const BoardDetail>(detail) ? optional(std::get<const BoardDetail>(detail)) : nullopt;
        if (board) write_fmt(R"(<input type="hidden" name="board" value="{:x}">)", board->id);
        if (!hide_cw || board) {
          write(R"(<details id="search-options"><summary>Search Options</summary><fieldset>)");
          if (board) {
            write_fmt(
              R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to {}</label>)",
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
            R"({}</section>)",
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
                  R"(<a class="big-button" href="/b/{0}/create_thread?text=1">Submit a new text post</a>)",
                  Escape(board.board().name())
                );
              }
              if (board.can_change_settings(login)) {
                write_fmt(
                  R"(<a class="big-button" href="/b/{0}/settings">Board settings</a>)",
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
            write_fmt(R"(<section id="site-sidebar"><h2>{}</h2>)", Escape{site->name});
            if (site->banner_url) {
              write_fmt(
                R"(<div class="sidebar-banner"><img src="{}" alt="{} banner"></div>)",
                Escape{*site->banner_url}, Escape{site->name}
              );
            }
            write_fmt("<p>{}</p>", Escape{site->description});
          },
          [&](const BoardDetail& board) {
            write_fmt(R"(<section id="board-sidebar"><h2>{}</h2>)", display_name_as_html(board.board()));
            // TODO: Banner image
            if (board.board().description_type() && board.board().description_type()->size()) {
              write_fmt("<p>{}</p>", rich_text_to_html(
                board.board().description_type(),
                board.board().description(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          },
          [&](const UserDetail& user) {
            write_fmt(R"(<section id="user-sidebar"><h2>{}</h2>)", display_name_as_html(user.user()));
            if (user.user().bio_type() && user.user().bio_type()->size()) {
              write_fmt("<p>{}</p>", rich_text_to_html(
                user.user().bio_type(),
                user.user().bio(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          }
        }, detail);

        return write("</section></aside>");
      }

      auto write_datetime(chrono::system_clock::time_point timestamp) noexcept -> ResponseWriter& {
        return write_fmt(R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)",
          fmt::gmtime(timestamp), fmt::localtime(timestamp), RelativeTime{timestamp});
      }

      auto write_user_link(OptRef<User> user_opt, Login login) noexcept -> ResponseWriter& {
        if (!user_opt || user_opt->get().deleted_at()) {
          write("<em>[deleted]</em>");
          return *this;
        }
        const auto& user = user_opt->get();
        write_fmt(R"(<a class="user-link" href="/u/{}">)", Escape(user.name()));
        if (user.avatar_url() && (!login || login->local_user().show_avatars())) {
          write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/user/{}/avatar.webp">)",
            Escape{user.name()}
          );
        } else {
          write(R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#user"></svg>)");
        }
        write_qualified_display_name(&user);
        write("</a>");
        if (user.bot()) write(R"( <span class="tag">Bot</span>)");
        if (user.mod_state() > ModState::Visible) {
          write_fmt(R"( <abbr class="tag mod-warning-tag" title="{0} by Moderator: {1}">{0}</abbr>)",
            describe_mod_state(user.mod_state()), Escape(user.mod_reason()));
        }
        return *this;
      }

      auto write_board_link(OptRef<Board> board_opt) noexcept -> ResponseWriter& {
        if (!board_opt) {
          write("<em>[deleted]</em>");
          return *this;
        }
        const auto& board = board_opt->get();
        write_fmt(R"(<a class="board-link" href="/b/{}">)", Escape(board.name()));
        if (board.icon_url()) {
          write_fmt(R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/board/{}/icon.webp">)",
            Escape(board.name())
          );
        } else {
          write(R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#book"></svg>)");
        }
        write_qualified_display_name(&board);
        if (board.mod_state() > ModState::Visible) {
          write_fmt(R"( <abbr class="tag mod-warning-tag" title="{0} by Moderator: {1}">{0}</abbr>)",
            describe_mod_state(board.mod_state()), Escape(board.mod_reason()));
        }
        write("</a>");
        if (board.content_warning()) {
          write_fmt(R"(<abbr class="tag content-warning-tag" title="Content Warning: {}">CW</abbr>)",
            Escape(board.content_warning())
          );
        }
        return *this;
      }

      auto write_board_list_entry(const BoardDetail& entry) {
        write(R"(<li class="board-list-entry"><div class="board-list-desc"><p class="board-list-name">)");
        write_board_link(entry.board());
        if (entry.board().display_name() && entry.board().display_name()->size()) {
          write_fmt(R"(</p><p class="account-name"><small>{}</small>)", Escape{entry.board().name()});
        }
        write_fmt(
          R"(</p><p>{}</p></div><div class="board-list-stats"><dl>)"
          R"(<dt>Subscribers</dt><dd>{:d}</dd>)"
          R"(<dt>Threads</dt><dd>{:d}</dd>)"
          R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)",
          rich_text_to_html(entry.board().description_type(), entry.board().description()),
          entry.stats().subscriber_count(),
          entry.stats().thread_count(),
          RelativeTime{chrono::system_clock::time_point(chrono::seconds(entry.stats().latest_post_time()))}
        );
      }

      auto write_user_list_entry(const UserDetail& entry, Login login = {}) {
        write(R"(<li class="user-list-entry"><div class="user-list-desc"><p class="user-list-name">)");
        write_user_link(entry.user(), login);
        if (entry.user().display_name() && entry.user().display_name()->size()) {
          write_fmt(R"(</p><p class="account-name"><small>{}</small>)", Escape{entry.user().name()});
        }
        write_fmt(
          R"(</p><p>{}</p></div><div class="user-list-stats"><dl>)"
          R"(<dt>Threads</dt><dd>{:d}</dd>)"
          R"(<dt>Comments</dt><dd>{:d}</dd>)"
          R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)",
          rich_text_to_html(entry.user().bio_type(), entry.user().bio()),
          entry.stats().thread_count(),
          entry.stats().comment_count(),
          RelativeTime{chrono::system_clock::time_point(chrono::seconds(entry.stats().latest_post_time()))}
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
          "</select>",
          name, name,
          value == SortType::Active ? " selected" : "",
          value == SortType::Hot ? " selected" : "",
          value == SortType::New ? " selected" : "",
          value == SortType::Old ? " selected" : "",
          value == SortType::MostComments ? " selected" : "",
          value == SortType::NewComments ? " selected" : "",
          value == SortType::TopAll ? " selected" : "",
          value == SortType::TopYear ? " selected" : "",
          value == SortType::TopSixMonths ? " selected" : "",
          value == SortType::TopThreeMonths ? " selected" : "",
          value == SortType::TopMonth ? " selected" : "",
          value == SortType::TopWeek ? " selected" : "",
          value == SortType::TopDay ? " selected" : "",
          value == SortType::TopTwelveHour ? " selected" : "",
          value == SortType::TopSixHour ? " selected" : "",
          value == SortType::TopHour ? " selected" : ""
        );
      }

      template <> auto write_sort_select<CommentSortType>(string_view name, CommentSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="Hot"{}>Hot)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="Top"{}>Top)"
          "</select>",
          name, name,
          value == CommentSortType::Hot ? " selected" : "",
          value == CommentSortType::New ? " selected" : "",
          value == CommentSortType::Old ? " selected" : "",
          value == CommentSortType::Top ? " selected" : ""
        );
      }

      template <> auto write_sort_select<UserPostSortType>(string_view name, UserPostSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="Top"{}>Top)"
          "</select>",
          name, name,
          value == UserPostSortType::New ? " selected" : "",
          value == UserPostSortType::Old ? " selected" : "",
          value == UserPostSortType::Top ? " selected" : ""
        );
      }

      template <> auto write_sort_select<UserSortType>(string_view name, UserSortType value) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<select name="{}" id="{}" autocomplete="off">)"
          R"(<option value="New"{}>New)"
          R"(<option value="Old"{}>Old)"
          R"(<option value="MostPosts"{}>Most Posts)"
          R"(<option value="NewPosts"{}>New Posts)"
          "</select>",
          name, name,
          value == UserSortType::New ? " selected" : "",
          value == UserSortType::Old ? " selected" : "",
          value == UserSortType::MostPosts ? " selected" : "",
          value == UserSortType::NewPosts ? " selected" : ""
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
          "</select>",
          name, name,
          value == BoardSortType::New ? " selected" : "",
          value == BoardSortType::Old ? " selected" : "",
          value == BoardSortType::MostPosts ? " selected" : "",
          value == BoardSortType::NewPosts ? " selected" : "",
          value == BoardSortType::MostSubscribers ? " selected" : ""
        );
      }

      auto write_show_threads_toggle(bool show_threads) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
          R"(<input class="a11y" name="type" type="radio" value="threads" id="type-threads"{}><label for="type-threads" class="toggle-button">Threads</label>)"
          R"(<input class="a11y" name="type" type="radio" value="comments" id="type-comments"{}><label for="type-comments" class="toggle-button">Comments</label></fieldset>)",
          show_threads ? " checked" : "", show_threads ? "" : " checked"
        );
      }

      auto write_local_toggle(bool local_only) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
          R"(<input class="a11y" name="local" type="radio" value="1" id="local-1"{}><label for="local-1" class="toggle-button">Local</label>)"
          R"(<input class="a11y" name="local" type="radio" value="0" id="local-0"{}><label for="local-0" class="toggle-button">All</label></fieldset>)",
          local_only ? " checked" : "", local_only ? "" : " checked"
        );
      }

      auto write_show_images_toggle(bool show_images) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(</label><label for="images"><input class="a11y" name="images" id="images" type="checkbox" value="1"{}><div class="toggle-switch"></div> Images</label>)"
          R"(<input class="no-js" type="submit" value="Apply"></form>)",
          show_images ? " checked" : ""
        );
      }

      auto write_subscribed_toggle(bool show_images) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(</label><label for="sub"><input class="a11y" name="sub" id="sub" type="checkbox" value="1"{}><div class="toggle-switch"></div> Subscribed Only</label>)"
          R"(<input class="no-js" type="submit" value="Apply"></form>)",
          show_images ? " checked" : ""
        );
      }

      template <class T> auto write_toggle_1(bool) noexcept -> void {}
      template <> auto write_toggle_1<SortType>(bool t) noexcept -> void { write_show_threads_toggle(t); }
      template <> auto write_toggle_1<UserPostSortType>(bool t) noexcept -> void { write_show_threads_toggle(t); }
      template <> auto write_toggle_1<UserSortType>(bool t) noexcept -> void { write_local_toggle(t); }
      template <> auto write_toggle_1<BoardSortType>(bool t) noexcept -> void { write_local_toggle(t); }

      template <class T> auto write_toggle_2(bool) noexcept -> void {
        write(R"(</label><input class="no-js" type="submit" value="Apply"></form>)");
      };
      template <> auto write_toggle_2<SortType>(bool t) noexcept -> void { write_show_images_toggle(t); }
      template <> auto write_toggle_2<CommentSortType>(bool t) noexcept -> void { write_show_images_toggle(t); }
      template <> auto write_toggle_2<UserPostSortType>(bool t) noexcept -> void { write_show_images_toggle(t); }
      template <> auto write_toggle_2<BoardSortType>(bool t) noexcept -> void { write_subscribed_toggle(t); }

      template <typename T> auto write_sort_options(string_view base_url, T sort, bool toggle_1, bool toggle_2, string_view hx_target = "#top-level-list") noexcept -> ResponseWriter& {
        write_fmt(
          R"(<form class="sort-form" method="get" action="{0}" hx-get="{0}" hx-trigger="change" hx-target="{1}" hx-push-url="true">)",
          Escape{base_url}, Escape{hx_target}
        );
        write_toggle_1<T>(toggle_1);
        write(R"(<label for="sort"><span class="a11y">Sort</span>)");
        write_sort_select("sort", sort);
        write_toggle_2<T>(toggle_2);
        return *this;
      }

      template <class T> auto write_vote_buttons(const T& entry, const SiteDetail* site, Login login) noexcept -> ResponseWriter& {
        const auto can_upvote = entry.can_upvote(login, site),
          can_downvote = entry.can_downvote(login, site);
        if (can_upvote || can_downvote) {
          write_fmt(
            R"(<form class="vote-buttons" id="votes-{0:x}" method="post" action="/{1}/{0:x}/vote" hx-post="/{1}/{0:x}/vote" hx-swap="outerHTML">)",
            entry.id, T::noun
          );
        } else {
          write_fmt(R"(<div class="vote-buttons" id="votes-{:x}">)", entry.id);
        }
        if (entry.should_show_votes(login, site)) {
          if (!login || login->local_user().show_karma()) {
            write_fmt(R"(<output class="karma" id="karma-{:x}">{}</output>)", entry.id, Suffixed{entry.stats().karma()});
          } else {
            write(R"(<div class="karma">&nbsp;</div>)");
          }
          write_fmt(
            R"(<label class="upvote"><button type="submit" name="vote" {0}{2}><span class="a11y">Upvote</span></button></label>)"
            R"(<label class="downvote"><button type="submit" name="vote" {1}{3}><span class="a11y">Downvote</span></button></label>)",
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
            R"( hx-get="{}{}from={}" hx-target="#top-level-list" hx-swap="beforeend" hx-trigger="revealed">)",
            Escape{base_url}, sep, next.to_string()
          );
        } else write(">");
        if (!is_first) {
          write_fmt(R"(<a class="big-button no-js" href="{}">← First</a>)", Escape{base_url});
        }
        if (next) {
          write_fmt(
            R"(<a class="big-button no-js" href="{0}{1}from={2}">Next →</a>)"
            R"(<a class="more-link js" href="{0}{1}from={2}" hx-get="{0}{1}from={2}" hx-target="#top-level-list" hx-swap="beforeend">Load more…</a>)",
            Escape{base_url}, sep, next.to_string()
          );
        }
        return write(R"(<div class="spinner">Loading…</div></div>)");
      }

      template <class T> auto write_controls_submenu(
        const T& post,
        Login login,
        bool show_user,
        bool show_board
      ) noexcept -> ResponseWriter& {
        if (!login) return *this;
        write_fmt(
          R"(<form class="controls-submenu" id="controls-submenu-{0:x}" method="post" action="/{1}/{0:x}/action">)"
          R"(<input type="hidden" name="show_user" value="{2:d}"><input type="hidden" name="show_board" value="{3:d}">)"
          R"(<label for="action"><span class="a11y">Action</span><svg class="icon"><use href="/static/feather-sprite.svg#chevron-down"></svg>)"
          R"(<select name="action" autocomplete="off" hx-post="/{1}/{0:x}/action" hx-trigger="change" hx-target="#controls-submenu-{0:x}">)"
          R"(<option selected hidden value="{4:d}">Actions)",
          post.id, T::noun, show_user, show_board, SubmenuAction::None
        );
        if (post.can_reply_to(login)) {
          write_fmt(R"(<option value="{:d}">💬 Reply)", SubmenuAction::Reply);
        }
        if (post.can_edit(login)) {
          write_fmt(R"(<option value="{:d}">✏️ Edit)", SubmenuAction::Edit);
        }
        if (post.can_delete(login)) {
          write_fmt(R"(<option value="{:d}">🗑️ Delete)", SubmenuAction::Delete);
        }
        write_fmt(
          R"(<option value="{:d}">{})"
          R"(<option value="{:d}">{})",
          post.saved ? SubmenuAction::Unsave : SubmenuAction::Save, post.saved ? "🚫 Unsave" : "🔖 Save",
          post.hidden ? SubmenuAction::Unhide : SubmenuAction::Hide, post.hidden ? "🔈 Unhide" : "🔇 Hide"
        );
        if (show_user) {
          write_fmt(R"(<option value="{:d}">{})",
            post.user_hidden ? SubmenuAction::UnmuteUser : SubmenuAction::MuteUser,
            post.user_hidden ? "🔈 Unmute user" : "🔇 Mute user"
          );
        }
        if (show_board) {
          write_fmt(R"(<option value="{:d}">{})",
            post.board_hidden ? SubmenuAction::UnmuteBoard : SubmenuAction::MuteBoard,
            post.board_hidden ? "🔈 Unhide board" : "🔇 Hide board"
          );
        }
        if (login->local_user().admin()) {
          SubmenuAction a1, a2, a3;
          string_view b1, b2, b3;
          switch (post.mod_state()) {
            case ModState::Visible:
              a1 = SubmenuAction::ModFlag;
              a2 = SubmenuAction::ModLock;
              a3 = SubmenuAction::ModRemove;
              b1 = "🚩 Flag";
              b2 = "🔒 Lock";
              b3 = "✂️ Remove";
              break;
            case ModState::Flagged:
              a1 = SubmenuAction::ModRestore;
              a2 = SubmenuAction::ModLock;
              a3 = SubmenuAction::ModRemove;
              b1 = "🏳️ Unflag";
              b2 = "🔒 Lock";
              b3 = "✂️ Remove";
              break;
            case ModState::Locked:
              a1 = SubmenuAction::ModRestore;
              a2 = SubmenuAction::ModFlag;
              a3 = SubmenuAction::ModRemove;
              b1 = "🔓 Unlock";
              b2 = "🔓 Unlock (leave flagged)";
              b3 = "✂️ Remove";
              break;
            default:
              a1 = SubmenuAction::ModRestore;
              a2 = SubmenuAction::ModFlag;
              a3 = SubmenuAction::ModLock;
              b1 = "♻️ Restore";
              b2 = "♻️ Restore (leave flagged)";
              b3 = "♻️ Restore (leave locked)";
              break;
          }
          write_fmt(
            R"(<optgroup label="Moderation">)"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">{})"
            R"(<option value="{:d}">🔨 Ban user)"
            R"(<option value="{:d}">☣️ Purge {})"
            R"(<option value="{:d}">☣️ Purge user)"
            "</optgroup>",
            a1, b1, a2, b2, a3, b3,
            SubmenuAction::ModBan,
            SubmenuAction::ModPurge, T::noun,
            SubmenuAction::ModPurgeUser
          );
        }
        return write(R"(</select></label><button class="no-js" type="submit">Apply</button></form>)");
      }

      template <class T> auto write_warnings(const T& thing, string_view prefix = "") noexcept -> ResponseWriter& {
        if (thing.mod_state() > ModState::Visible) {
          if (thing.mod_reason()) {
            write_content_warning(
              fmt::format("{} by Moderator", describe_mod_state(thing.mod_state())),
              true,
              thing.mod_reason()->string_view(),
              prefix
            );
          } else {
            write_fmt(
              R"(<p class="content-warning"><span class="tag mod-warning-tag">{}{} by Moderator</span></p>)",
              prefix, describe_mod_state(thing.mod_state())
            );
          }
        }
        if (thing.content_warning()) {
          write_content_warning("Content Warning", false, thing.content_warning()->string_view(), prefix);
        }
        return *this;
      }

      template <class T> auto write_short_warnings(const T& thing) noexcept -> ResponseWriter& {
        if (thing.mod_state() > ModState::Visible) {
          write_fmt(R"( <abbr class="tag mod-warning-tag" title="{0} by Moderator: {1}">{0}</abbr>)",
            describe_mod_state(thing.mod_state()), Escape(thing.mod_reason()));
        }
        if (thing.content_warning()) {
          write_fmt(R"( <abbr class="tag content-warning-tag" title="Content Warning: {}">CW</abbr>)",
            Escape(thing.content_warning()));
        }
        return *this;
      }

      auto write_thread_entry(
        const ThreadDetail& thread,
        const SiteDetail* site,
        Login login,
        bool is_list_item,
        bool show_user,
        bool show_board,
        bool show_images
      ) noexcept -> ResponseWriter& {
        // TODO: thread-source (link URL)
        write_fmt(
          R"({} class="thread" id="thread-{:x}"><h2 class="thread-title">)",
          is_list_item ? "<li><article" : "<div",
          thread.id
        );
        const auto title = rich_text_to_html_emojis_only(thread.thread().title_type(), thread.thread().title(), {});
        if (is_list_item || thread.thread().content_url()) {
          write_fmt(R"(<a class="thread-title-link" href="{}">{}</a></h2>)",
            Escape{
              thread.thread().content_url()
                ? thread.thread().content_url()->string_view()
                : fmt::format("/thread/{:x}", thread.id)
            },
            title
          );
        } else {
          write_fmt("{}</h2>", title);
        }
        // TODO: Selectively show CW'd images, maybe use blurhash
        if (show_images && !thread.thread().content_warning() && thread.link_card().image_url()) {
          write_fmt(
            R"(<div class="thumbnail"><img src="/media/thread/{:x}/thumbnail.webp" aria-hidden="true"></div>)",
            thread.id
          );
        } else {
          write_fmt(
            R"(<div class="thumbnail"><svg class="icon"><use href="/static/feather-sprite.svg#{}"></svg></div>)",
            thread.thread().content_warning() ? "alert-octagon" : (thread.thread().content_url() ? "link" : "file-text")
          );
        }
        if (thread.thread().content_warning() || thread.thread().mod_state() > ModState::Visible) {
          write(R"(<div class="thread-warnings">)");
          if (!is_list_item && thread.thread().content_text_type() && thread.thread().content_text_type()->size()) {
            write_short_warnings(thread.thread());
          }
          else write_warnings(thread.thread());
          write(R"(</div>)");
        }
        write(R"(<div class="thread-info"><span>submitted )");
        write_datetime(thread.created_at());
        if (show_user) {
          write("</span><span>by ");
          write_user_link(thread._author, login);
        }
        if (show_board) {
          write("</span><span>to ");
          write_board_link(thread._board);
        }
        write("</span></div>");
        write_vote_buttons(thread, site, login);
        if (is_list_item) {
          write_fmt(R"(<div class="controls"><a id="comment-link-{0:x}" href="/thread/{0:x}#comments">{1:d}{2}</a>)",
            thread.id,
            thread.stats().descendant_count(),
            thread.stats().descendant_count() == 1 ? " comment" : " comments"
          );
        } else {
          write(R"(<div class="controls"><span></span>)");
        }
        write_controls_submenu(thread, login, show_user, show_board);
        return write(is_list_item ? "</div></article>" : "</div></div>");
      }

      auto write_comment_entry(
        const CommentDetail& comment,
        const SiteDetail* site,
        Login login,
        bool is_list_item,
        bool is_tree_item,
        bool show_user,
        bool show_thread,
        bool show_images
      ) noexcept -> ResponseWriter& {
        write_fmt(R"({} class="comment" id="comment-{:x}"><{} class="comment-info"><span>)",
          is_list_item ? "<li><article" : "<div",
          comment.id,
          is_tree_item ? "h3" : "h2"
        );
        if (show_user) {
          write_user_link(comment._author, login);
          write("</span><span>");
        }
        write("commented ");
        write_datetime(comment.created_at());
        if (show_thread) {
          write_fmt(R"(</span><span>on <a href="/thread/{:x}">{}</a>)",
            comment.comment().thread(),
            rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title(), {})
          );
          if (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible) {
            write_short_warnings(comment.thread());
          }
        }
        const bool has_warnings = comment.comment().content_warning() || comment.comment().mod_state() > ModState::Visible,
          thread_warnings = show_thread && (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible);
        const auto content = rich_text_to_html(
          comment.comment().content_type(),
          comment.comment().content(),
          { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
        );
        if (has_warnings || thread_warnings) {
          write(R"(</span></h2><div class="comment-content"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
          if (thread_warnings) write_warnings(comment.thread(), "Thread ");
          if (has_warnings) write_warnings(comment.comment());
          write_fmt(R"(</summary><div>{}</div></details></div>)", content);
        } else {
          write_fmt(R"(</span></{}><div class="comment-content">{}</div>)",
            is_tree_item ? "h3" : "h2",
            content
          );
        }
        write_vote_buttons(comment, site, login);
        write(R"(<div class="controls">)");
        if (is_list_item) {
          write_fmt(R"(<a id="comment-link-{0:x}" href="/comment/{0:x}#replies">{1:d}{2}</a>)",
            comment.id,
            comment.stats().descendant_count(),
            comment.stats().descendant_count() == 1 ? " reply" : " replies"
          );
        } else if (is_tree_item) {
          write_fmt(R"(<a href="/comment/{:x}">Permalink</a>)", comment.id);
        } else {
          write("<span></span>");
        }
        write_controls_submenu(comment, login, show_user, show_thread);
        return write(is_list_item ? "</div></article>" : "</div></div>");
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
              write_user_link(user.user(), login);
            },
            [&](const BoardDetail& board) {
              write("<li>");
              write_board_link(board.board());
            },
            [&](const ThreadDetail& thread) {
              write_thread_entry(thread, site, login, true, true, true, true);
            },
            [&](const CommentDetail& comment) {
              write_comment_entry(comment, site, login, true, false, true, true, true);
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
        bool is_thread,
        bool include_ol,
        bool is_alt = false
      ) noexcept -> ResponseWriter& {
        // TODO: Include existing query params
        auto range = comments.comments.equal_range(root);
        if (range.first == range.second) {
          if (is_thread) write(R"(<div class="no-comments">No comments</div>)");
          return *this;
        }
        const bool infinite_scroll_enabled =
          site->infinite_scroll_enabled && (!login || login->local_user().infinite_scroll_enabled());
        if (include_ol) write_fmt(R"(<ol class="comment-list comment-tree" id="comments-{:x}">)", root);
        for (auto iter = range.first; iter != range.second; iter++) {
          const auto& comment = iter->second;
          write_fmt(
            R"(<li><article class="comment-with-comments{}">)",
            is_alt ? " odd-depth" : "", comment.id
          );
          write_comment_entry(comment, site, login, false, true, true, false, show_images);
          const auto cont = comments.continued.find(comment.id);
          if (cont != comments.continued.end() && !cont->second) {
            write_fmt(
              R"(<a class="more-link{0}" id="continue-{1:x}" href="/comment/{1:x}">More comments…</a>)",
              is_alt ? "" : " odd-depth", comment.id
            );
          } else if (comment.stats().child_count()) {
            write(R"(<section class="comments" aria-title="Replies">)");
            write_comment_tree(comments, comment.id, sort, site, login, show_images, false, true, !is_alt);
            write("</section>");
          }
          write("</article>");
        }
        const auto cont = comments.continued.find(root);
        if (cont != comments.continued.end()) {
          write_fmt(R"(<li id="comment-replace-{:x}")", root);
          if (infinite_scroll_enabled) {
            write_fmt(
              R"( hx-get="/{0}/{1:x}?sort={2}&from={3}" hx-swap="outerHTML" hx-trigger="revealed")",
              is_thread ? "thread" : "comment", root,
              EnumNameCommentSortType(sort), cont->second.to_string()
            );
          }
          write_fmt(
            R"(><a class="more-link{0}" id="continue-{1:x}" href="/{2}/{1:x}?sort={3}&from={4}")"
            R"( hx-get="/{2}/{1:x}?sort={3}&from={4}" hx-target="#comment-replace-{1:x}" hx-swap="outerHTML">More comments…</a>)",
            is_alt ? " odd-depth" : "", root, is_thread ? "thread" : "comment",
            EnumNameCommentSortType(sort), cont->second.to_string()
          );
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
          R"(</label>)",
          existing_value.empty() ? "" : " checked",
          existing_value.empty() ? R"( class="no-js")" : "",
          Escape{existing_value}
        );
      }

      auto write_content_warning(string_view label, bool is_mod, string_view content, string_view prefix = "") noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<p class="tag content-warning content-warning-tag"><strong class="{}-warning-label">{}{}<span class="a11y">:</span></strong> {}</p>)",
          is_mod ? "mod" : "content", prefix, label, Escape{content}
        );
      }

      template <class T> auto write_reply_form(const T& parent) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<form data-component="Form" id="reply-{1:x}" class="form reply-form" method="post" action="/{0}/{1:x}/reply" )"
          R"html(hx-post="/{0}/{1:x}/reply" hx-target="#comments-{1:x}" hx-swap="afterbegin" hx-on::after-request="this.reset()">)html"
          R"(<a name="reply"></a>)"
          HTML_TEXTAREA("text_content", "Reply", R"( required placeholder="Write your reply here")", ""),
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
        write(R"(<article class="thread-with-comments">)");
        write_thread_entry(thread, site, login, false, true, true, show_images);
        if (
          thread.thread().content_text() &&
          thread.thread().content_text()->size() &&
          !(thread.thread().content_text()->size() == 1 &&
            thread.thread().content_text_type()->GetEnum<RichText>(0) == RichText::Text &&
            !thread.thread().content_text()->GetAsString(0)->size())
        ) {
          const auto content = rich_text_to_html(
            thread.thread().content_text_type(),
            thread.thread().content_text(),
            { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
          );
          if (thread.thread().content_warning() || thread.board().content_warning() || thread.thread().mod_state() > ModState::Visible) {
            write(R"(<div class="thread-content"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
            write_warnings(thread.thread());
            write_fmt(R"(</summary><div>{}</div></details></div>)", content);
          } else {
            write_fmt(R"(<div class="thread-content">{}</div>)", content);
          }
        }
        write_fmt(R"(<section class="comments" id="comments"><h2>{:d} comments</h2>)", thread.stats().descendant_count());
        write_sort_options(fmt::format("/thread/{:x}", thread.id), sort, false, show_images, fmt::format("#comments-{:x}", thread.id));
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
        write(R"(<article class="comment-with-comments">)");
        write_comment_entry(
          comment, site, login,
          false, false,
          true, true, show_images
        );
        write_fmt(R"(<section class="comments" id="comments"><h2>{:d} replies</h2>)", comment.stats().descendant_count());
        write_sort_options(fmt::format("/comment/{:x}", comment.id), sort, false, show_images, fmt::format("#comments-{:x}", comment.id));
        if (comment.can_reply_to(login)) {
          write_reply_form(comment);
        }
        write_comment_tree(comments, comment.id, sort, site, login, show_images, false, true);
        return write("</section></article>");
      }

      static auto error_banner(optional<string_view> error) noexcept -> string {
        if (!error) return "";
        return fmt::format(R"(<p class="error-message"><strong>Error:</strong> {}</p>)", Escape{*error});
      }

      auto write_login_form(optional<string_view> error = {}) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<main><form class="form form-page" method="post" action="/login">{}{})"
          HTML_FIELD("actual_username", "Username or email", "text", "")
          HTML_FIELD("password", "Password", "password", "")
          HTML_CHECKBOX("remember", "Remember me", "")
          R"(<input type="submit" value="Login"></form></main>)",
          error_banner(error), HONEYPOT_FIELD
        );
      }

      auto write_register_form(const SiteDetail* site, optional<string_view> error = {}) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/register">{})",
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
            R"(<label for="application_reason"><span>{}</span><textarea name="application_reason" required autocomplete="off"></textarea></label>)",
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
          R"(<main><form data-component="Form" class="form form-page" method="post" action="/create_board"><h2>Create Board</h2>{})",
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
          R"(<p class="thread-info"><span>Posting as )",
          Escape(board.board().name()), error_banner(error)
        );
        write_user_link(login._user, login);
        write("</span><span>to ");
        write_board_link(board._board);
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
          R"(<p class="thread-info"><span>Posting as )",
          thread.id, error_banner(error)
        );
        write_user_link(login._user, login);
        write("</span><span>to ");
        write_board_link(thread._board);
        write_fmt(
          "</span></p><br>"
          HTML_FIELD("title", "Title", "text", R"( value="{}" autocomplete="off" required)")
          HTML_TEXTAREA("text_content", "Text content", "{}", "{}"),
          Escape(display_name_as_text(thread.thread())),
          thread.thread().content_url() ? "" : " required",
          Escape(thread.thread().content_text_raw())
        );
        write_content_warning_field(thread.thread().content_warning() ? thread.thread().content_warning()->string_view() : "");
        return write(R"(<input type="submit" value="Submit"></form></main>)");
      }

      template <typename T> auto write_tab(T tab, T selected, string_view name, string_view url) {
        if (tab == selected) write_fmt(R"(<li><span class="selected">{}</span>)", name);
        else write_fmt(R"(<li><a href="{}">{}</a>)", url, name);
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
          "</select></label>",
          selected == HomePageType::SingleBoard ? "<br><strong>Important: Once you select an option other than Single Board, you can never select Single Board again!</strong>" : "",
          selected == HomePageType::Subscribed ? " selected" : "",
          selected == HomePageType::Local ? " selected" : "",
          selected == HomePageType::All ? " selected" : "",
          selected == HomePageType::BoardList ? " selected" : "",
          selected == HomePageType::SingleBoard ? " selected" : " disabled"
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
          R"(</select></label>)",
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
          HTML_FIELD("color_accent_hover", "Accent Color (Hover)", "color", R"( value="{}" autocomplete="off")"),
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
            HTML_FIELD("max_post_length", "Max post length (bytes)", "number", R"( min="512" value="{:d}" autocomplete="off")")
            HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( {} autocomplete="off")")
            HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( {} autocomplete="off")")
          R"(</fieldset></details><input type="submit" value="Submit"></form>)",
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
        ReadTxnBase& txn,
        Login login,
        optional<uint64_t> cursor = {},
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<div class="table-page"><h2>Registration Applications</h2>{}<table>)"
          R"(<thead><th>Name<th>Email<th>Date<th>IP Addr<th>User Agent<th class="table-reason">Reason<th>Approve</thead>)"
          R"(<tbody id="application-table">)",
          error_banner(error)
        );
        bool any_entries = false;
        instance.list_applications([&](auto p){
          any_entries = true;
          auto& [application, detail] = p;
          write_fmt(
            R"(<tr><td>{}<td>{}<td>{:%D}<td>{}<td>{}<td class="table-reason"><div class="reason">{}</div><td><form method="post" action="/site_admin/application/approve/{:x}"><input type="submit" value="Approve"></form></tr>)",
            Escape{detail.user().name()},
            Escape{detail.local_user().email()},
            fmt::localtime(detail.created_at()),
            Escape{application.ip()},
            Escape{application.user_agent()},
            Escape{application.text()},
            detail.id
          );
        }, txn, login, cursor);
        if (!any_entries) write(R"(<tr><td colspan="7">There's nothing here.</tr>)");
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
          "{}",
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
            HTML_FIELD("max_post_length", "Max post length (bytes)", "number", R"( min="512" value="1048576" autocomplete="off")")
            HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( checked autocomplete="off")")
            HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( checked autocomplete="off")")
          R"(</blockquote></fieldset></details>{}{}<input type="submit" value="Submit"></form>)",
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
        uint8_t cw_mode = 1;
        if (login.local_user().hide_cw_posts()) cw_mode = 0;
        else if (login.local_user().expand_cw_images()) cw_mode = 3;
        else if (login.local_user().expand_cw_posts()) cw_mode = 2;
        write_fmt(
          R"(<form data-component="Form" class="form form-page" method="post" action="/settings"><h2>User settings</h2>{})"
          HTML_CHECKBOX("open_links_in_new_tab", "Open links in new tab", "{}")
          HTML_CHECKBOX("show_avatars", "Show avatars", "{}")
          HTML_CHECKBOX("show_images_threads", "Show images on threads by default", "{}")
          HTML_CHECKBOX("show_images_comments", "Show inline images in comments by default", "{}")
          HTML_CHECKBOX("show_bot_accounts", "Show bot accounts", "{}"),
          error_banner(error),
          login.local_user().open_links_in_new_tab() ? " checked" : "",
          login.local_user().show_avatars() ? " checked" : "",
          login.local_user().show_images_threads() ? " checked" : "",
          login.local_user().show_images_comments() ? " checked" : "",
          login.local_user().show_bot_accounts() ? " checked" : ""
        );
        if (site->votes_enabled) {
          write_fmt(
            HTML_CHECKBOX("show_karma", "Show karma", "{}"),
            login.local_user().show_karma() ? " checked" : ""
          );
        }
        if (site->cws_enabled) {
          write_fmt(
            R"(<label><span>Content warnings</span><select name="content_warnings" autocomplete="off">)"
              R"(<option value="0"{}> Hide posts with content warnings completely)"
              R"(<option value="1"{}> Collapse posts with content warnings (default))"
              R"(<option value="2"{}> Expand text content of posts with content warnings but hide images)"
              R"(<option value="3"{}> Always expand text and images with content warnings)"
            R"(</select></label>)",
            cw_mode == 0 ? " selected" : "", cw_mode == 1 ? " selected" : "", cw_mode == 2 ? " selected" : "", cw_mode == 3 ? " selected" : ""
          );
        }
        if (site->javascript_enabled) {
          write_fmt(
            HTML_CHECKBOX("javascript_enabled", "JavaScript enabled", "{}"),
            login.local_user().javascript_enabled() ? " checked" : ""
          );
        }
        if (site->infinite_scroll_enabled) {
          write_fmt(
            HTML_CHECKBOX("infinite_scroll_enabled", "Infinite scroll enabled", "{}"),
            login.local_user().infinite_scroll_enabled() ? " checked" : ""
          );
        }
        // TODO: Default sort, default comment sort
        return write(R"(<input type="submit" value="Submit"></form>)");
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
          R"(<input type="submit" value="Submit"></form>)",
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
          R"(<input type="submit" value="Delete Account"></form>)",
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
          ,
          Escape(board.board().name()), error_banner(error),
          Escape(rich_text_to_plain_text(board.board().display_name_type(), board.board().display_name())),
          Escape(board.board().description_raw()),
          Escape(board.board().content_warning()),
          Escape(board.board().icon_url()),
          Escape(board.board().banner_url()),
          board.local_board().private_() ? " checked" : "",
          board.board().restricted_posting() ? " checked" : "",
          board.board().approve_subscribe() ? " checked" : ""
        ) .write_voting_select(board.board().can_upvote(), board.board().can_downvote(), site->votes_enabled, site->downvotes_enabled)
          .write(R"(<input type="submit" value="Submit"></form>)");
      }
    };

    auto writer(Response rsp) -> ResponseWriter { return ResponseWriter(this, rsp); }

    auto error_page(Response rsp, const ApiError& e, const ErrorMeta& m) noexcept -> void {
      if (m.is_htmx) {
        writer(
          rsp->writeStatus(http_status(e.http_status))
            ->writeHeader("Content-Type", TYPE_HTML)
        ) .write_fmt("Error {:d}: {}", e.http_status, Escape(e.message))
          .finish();
      } else if (m.is_get && e.http_status == 401) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT")
          ->writeHeader("Location", "/login")
          ->end();
      } else try {
        Meta m { .start = chrono::steady_clock::now(), .is_htmx = false, .site = controller->site_detail() };
        auto txn = controller->open_read_txn();
        m.populate(txn);
        writer(rsp->writeStatus(http_status(e.http_status)))
          .write_html_header(m, {})
          .write_fmt(R"(<main><div class="error-page"><h2>Error {}</h2><p>{}</p></div></main>)", http_status(e.http_status), e.message)
          .write_html_footer(m)
          .finish();
      } catch (...) {
        spdlog::warn("Error when rendering error page");
        writer(
          rsp->writeStatus(http_status(e.http_status))
            ->writeHeader("Content-Type", TYPE_HTML)
        ) .write_fmt("Error {:d}: {}", e.http_status, Escape(e.message))
          .finish();
      }
    }

    static inline auto write_redirect_to(Response rsp, const Meta& m, string_view location) noexcept -> void {
      if (m.is_htmx) {
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
      const auto hash = fmt::format("\"{:016x}\"", XXH3_64bits(src.data(), src.length()));
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

    static inline auto user_name_param(ReadTxnBase& txn, Request req, uint16_t param) {
      const auto name = req->getParameter(param);
      const auto user_id = txn.get_user_id_by_name(name);
      if (!user_id) throw ApiError(fmt::format("User \"{}\" does not exist", name), 410);
      return *user_id;
    }

    static inline auto board_name_param(ReadTxnBase& txn, Request req, uint16_t param) {
      const auto name = req->getParameter(param);
      const auto board_id = txn.get_board_id_by_name(name);
      if (!board_id) throw ApiError(fmt::format("Board \"{}\" does not exist", name), 410);
      return *board_id;
    }

    template <class T>
    auto do_submenu_action(SubmenuAction action, uint64_t user, uint64_t id) -> optional<string> {
      switch (action) {
        case SubmenuAction::Reply:
          return fmt::format("/{}/{:x}#reply", T::noun, id);
        case SubmenuAction::Edit:
          return fmt::format("/{}/{:x}/edit", T::noun, id);
        case SubmenuAction::Delete:
          throw ApiError("Delete is not yet implemented", 500);
        case SubmenuAction::Share:
          throw ApiError("Share is not yet implemented", 500);
        case SubmenuAction::Save:
          controller->save_post(user, id, true);
          break;
        case SubmenuAction::Unsave:
          controller->save_post(user, id, false);
          break;
        case SubmenuAction::Hide:
          controller->hide_post(user, id, true);
          break;
        case SubmenuAction::Unhide:
          controller->hide_post(user, id, false);
          break;
        case SubmenuAction::Report:
          throw ApiError("Report is not yet implemented", 500);
        case SubmenuAction::MuteUser: {
          auto txn = controller->open_read_txn();
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_user(user, e.author_id(), true);
          break;
        }
        case SubmenuAction::UnmuteUser: {
          auto txn = controller->open_read_txn();
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_user(user, e.author_id(), false);
          break;
        }
        case SubmenuAction::MuteBoard: {
          auto txn = controller->open_read_txn();
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_board(user, e.thread().board(), true);
          break;
        }
        case SubmenuAction::UnmuteBoard:{
          auto txn = controller->open_read_txn();
          auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
          controller->hide_board(user, e.thread().board(), false);
          break;
        }
        case SubmenuAction::ModRestore:
        case SubmenuAction::ModFlag:
        case SubmenuAction::ModLock:
        case SubmenuAction::ModRemove:
        case SubmenuAction::ModBan:
        case SubmenuAction::ModPurge:
        case SubmenuAction::ModPurgeUser:
          throw ApiError("Mod actions are not yet implemented", 500);
        default:
          throw ApiError("No action selected", 400);
      }
      return {};
    }

    auto feed_route(uint64_t feed_id, Response rsp, Request req, Meta& m) -> void {
      auto txn = controller->open_read_txn();
      m.populate(txn);
      const auto sort = parse_sort_type(req->getQuery("sort"), m.login);
      const auto show_threads = req->getQuery("type") != "comments",
        show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_threads() : false);
      const auto base_url = fmt::format("{}?type={}&sort={}&images={}",
        req->getUrl(),
        show_threads ? "threads" : "comments",
        EnumNameSortType(sort),
        show_images ? 1 : 0
      );
      auto r = writer(rsp);
      if (m.is_htmx) {
        rsp->writeHeader("Content-Type", TYPE_HTML);
        m.write_cookie(rsp);
      } else {
        string title;
        switch (feed_id) {
          case InstanceController::FEED_ALL: title = "All"; break;
          case InstanceController::FEED_LOCAL: title = m.site->name; break;
          case InstanceController::FEED_HOME: title = "Subscribed"; break;
          default: title = "Unknown Feed";
        }
        r.write_html_header(m, {
            .canonical_path = req->getUrl(),
            .banner_link = req->getUrl(),
            .page_title = feed_id == InstanceController::FEED_LOCAL ? "Local" : title,
            .banner_title = title
          })
          .write("<div>")
          .write_sidebar(m.login, m.site)
          .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
          .write_sort_options(req->getUrl(), sort, show_threads, show_images)
          .write(R"(</section><main>)");
      }
      r.write_fmt(R"(<ol class="{}-list" id="top-level-list">)", show_threads ? "thread" : "comment");
      const auto from = req->getQuery("from");
      bool any_entries = false;
      const auto next = show_threads ?
        controller->list_feed_threads(
          [&](auto& e){r.write_thread_entry(e, m.site, m.login, true, true, true, show_images); any_entries = true;},
          txn, feed_id, sort, m.login, from
        ) :
        controller->list_feed_comments(
          [&](auto& e){r.write_comment_entry(e, m.site, m.login, true, false, true, true, show_images); any_entries = true;},
          txn, feed_id, sort, m.login, from
        );
      if (!m.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
      r.write("</ol>").write_pagination(base_url, from.empty(), next);
      if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
      r.finish();
    }

    static inline auto board_header_options(Request req, const Board& board, optional<string_view> title = {}) -> ResponseWriter::HtmlHeaderOptions {
      return {
        .canonical_path = req->getUrl(),
        .banner_link = req->getUrl(),
        .page_title = title,
        .banner_title = display_name_as_text(board),
        .banner_image = board.banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.name()->string_view())) : nullopt,
        .card_image = board.icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.name()->string_view())) : nullopt
      };
    }

    static inline auto form_to_site_update(QueryString<string> body) -> SiteUpdate {
      const auto voting = body.optional_uint("voting");
      return {
        .name = body.optional_string("name"),
        .description = body.optional_string("description"),
        .icon_url = body.optional_string("icon_url"),
        .banner_url = body.optional_string("banner_url"),
        .application_question = body.optional_string("application_question"),
        .max_post_length = body.optional_uint("max_post_length"),
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

    auto register_routes(App& app) -> void {

      // Static Files
      /////////////////////////////////////////////////////

      serve_static(app, "/favicon.ico", "image/vnd.microsoft.icon", twemoji_piano_ico_str());
      serve_static(app, "/static/default-theme.css", TYPE_CSS, default_theme_css_str());
      serve_static(app, "/static/htmx.min.js", TYPE_JS, htmx_min_js_str());
      serve_static(app, "/static/ludwig.js", TYPE_JS, ludwig_js_str());
      serve_static(app, "/static/feather-sprite.svg", TYPE_SVG, feather_sprite_svg_str());

      // Pages
      /////////////////////////////////////////////////////

      auto self = this->shared_from_this();
      Router<SSL, Meta, ErrorMeta>(app,
        bind(&Webapp::middleware, self, _1, _2),
        bind(&Webapp::error_middleware, self, _1, _2),
        bind(&Webapp::error_page, self, _1, _2, _3)
      )
      .get("/", [self](auto* rsp, auto* req, Meta& m) {
        if (m.site->setup_done) {
          self->feed_route(m.logged_in_user_id ? InstanceController::FEED_HOME : InstanceController::FEED_LOCAL, rsp, req, m);
        } else {
          auto txn = self->controller->open_read_txn();
          if (!m.require_login(txn).local_user().admin()) {
            throw ApiError("Only an admin user can perform first-run setup.", 403);
          }
          self->writer(rsp)
            .write_html_header(m, {
              .canonical_path = "/",
              .banner_title = "First-Run Setup",
            })
            .write("<main>")
            .write_first_run_setup_form(self->controller->first_run_setup_options(txn))
            .write("</main>")
            .write_html_footer(m)
            .finish();
        }
      })
      .get("/all", [self](auto* rsp, auto* req, Meta& m) {
        self->feed_route(InstanceController::FEED_ALL, rsp, req, m);
      })
      .get("/local", [self](auto* rsp, auto* req, Meta& m) {
        self->feed_route(InstanceController::FEED_LOCAL, rsp, req, m);
      })
      .get("/boards", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_board_sort_type(req->getQuery("sort"));
        const auto sub = req->getQuery("sub") == "1";
        const auto base_url = fmt::format("/boards?local={}&sort={}&sub={}",
          local ? "1" : "0",
          EnumNameBoardSortType(sort),
          sub ? "1" : "0"
        );
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
        } else {
          r.write_html_header(m, {
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
          txn, sort, local, sub, m.login, req->getQuery("from")
        );
        if (!m.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, req->getQuery("from").empty(), next);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/users", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_user_sort_type(req->getQuery("sort"));
        const auto base_url = fmt::format("/users?local={}&sort={}",
          local ? "1" : "0",
          EnumNameUserSortType(sort)
        );
        // ---
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
        } else {
          r.write_html_header(m, {
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
          [&](auto& e){r.write_user_list_entry(e, m.login); any_entries = true; },
          txn, sort, local,m.login, req->getQuery("from")
        );
        if (!m.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, req->getQuery("from").empty(), next);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/c/:name", [self](auto* rsp, auto* req, Meta& m) {
        // Compatibility alias for Lemmy community URLs
        // Needed because some Lemmy apps expect URLs in exactly this format
        write_redirect_to(rsp, m, fmt::format("/b/{}", req->getParameter(0)));
      })
      .get("/b/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto board_id = board_name_param(txn, req, 0);
        const auto board = self->controller->board_detail(txn, board_id, m.login);
        const auto sort = parse_sort_type(req->getQuery("sort"), m.login);
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_threads() : false);
        const auto base_url = fmt::format("/b/{}?type={}&sort={}&images={}",
          board.board().name()->string_view(),
          show_threads ? "threads" : "comments",
          EnumNameSortType(sort),
          show_images ? 1 : 0
        );
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          r.write_html_header(m, board_header_options(req, board.board()))
            .write("<div>")
            .write_sidebar(m.login, m.site, board)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        r.write_fmt(R"(<ol class="{}-list" id="top-level-list">)", show_threads ? "thread" : "comment");
        bool any_entries = false;
        const auto from = req->getQuery("from");
        const auto next = show_threads ?
          self->controller->list_board_threads(
            [&](auto& e){r.write_thread_entry(e, m.site, m.login, true, true, false, show_images); any_entries = true;},
            txn, board_id, sort, m.login, from
          ) :
          self->controller->list_board_comments(
            [&](auto& e){r.write_comment_entry(e, m.site, m.login, true, false, true, true, show_images); any_entries = true;},
            txn, board_id, sort, m.login, from
          );
        if (!m.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, from.empty(), next);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/b/:name/create_thread", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto show_url = req->getQuery("text") != "1";
        const auto login = m.require_login(txn);
        const auto board = self->controller->board_detail(txn, board_id, m.login);
        self->writer(rsp)
          .write_html_header(m, board_header_options(req, board.board(), "Create Thread"))
          .write_create_thread_form(show_url, board, login)
          .write_html_footer(m)
          .finish();
      })
      .get("/u/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto user_id = user_name_param(txn, req, 0);
        const auto user = self->controller->user_detail(txn, user_id, m.login);
        const auto sort = parse_user_post_sort_type(req->getQuery("sort"));
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_threads() : false);
        const auto base_url = fmt::format("/u/{}?type={}&sort={}&images={}",
          user.user().name()->string_view(),
          show_threads ? "threads" : "comments",
          EnumNameUserPostSortType(sort),
          show_images ? 1 : 0
        );
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          r.write_html_header(m, {
              .canonical_path = req->getUrl(),
              .banner_link = req->getUrl(),
              .banner_title = display_name_as_text(user.user()),
              .banner_image = user.user().banner_url() ? optional(fmt::format("/media/user/{}/banner.webp", user.user().name()->string_view())) : nullopt,
              .card_image = user.user().avatar_url() ? optional(fmt::format("/media/user/{}/avatar.webp", user.user().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(m.login, m.site, user)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        r.write_fmt(R"(<ol class="{}-list" id="top-level-list">)", show_threads ? "thread" : "comment");
        bool any_entries = false;
        const auto from = req->getQuery("from");
        const auto next = show_threads ?
          self->controller->list_user_threads(
            [&](auto& e){r.write_thread_entry(e, m.site, m.login, true, false, false, show_images); any_entries = true;},
            txn, user_id, sort, m.login, from
          ) :
          self->controller->list_user_comments(
            [&](auto& e){r.write_comment_entry(e, m.site, m.login, true, false, false, true, show_images); any_entries = true;},
            txn, user_id, sort, m.login, from
          );
        if (!m.is_htmx && !any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
        r.write("</ol>").write_pagination(base_url, from.empty(), next);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/thread/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), m.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->thread_detail(txn, id, sort, m.login, req->getQuery("from"));
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, m.site, m.login, show_images, true, false);
        } else {
          r.write_html_header(m, board_header_options(req, detail.board(),
              fmt::format("{} - {}", display_name_as_text(detail.board()), display_name_as_text(detail.thread()))))
            .write("<div>")
            .write_sidebar(m.login, m.site, self->controller->board_detail(txn, detail.thread().board(), m.login))
            .write("<main>")
            .write_thread_view(detail, comments, m.site, m.login, sort, show_images)
            .write("</main></div>")
            .write_html_footer(m);
        }
        r.finish();
      })
      .get("/thread/:id/edit", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto id = hex_id_param(req, 0);
        const auto login = m.require_login(txn);
        const auto thread = ThreadDetail::get(txn, id, login);
        if (!thread.can_edit(login)) throw ApiError("Cannot edit this post", 403);
        self->writer(rsp)
          .write_html_header(m, board_header_options(req, thread.board(), "Edit Thread"))
          .write_edit_thread_form(thread, login)
          .write_html_footer(m)
          .finish();
      })
      .get("/comment/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), m.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->comment_detail(txn, id, sort, m.login, req->getQuery("from"));
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, m.site, m.login, show_images, false, false);
        } else {
          r.write_html_header(m, board_header_options(req, detail.board(),
              fmt::format("{} - {}'s comment on “{}”",
                display_name_as_text(detail.board()),
                display_name_as_text(detail.author()),
                display_name_as_text(detail.thread()))))
            .write("<div>")
            .write_sidebar(m.login, m.site, self->controller->board_detail(txn, detail.thread().board(), m.login))
            .write("<main>")
            .write_comment_view(detail, comments, m.site, m.login, sort, show_images)
            .write("</main></div>")
            .write_html_footer(m);
        }
        r.finish();
      })
      .get_async("/search", [self](auto* rsp, auto* req, auto m, auto&& resume) {
        SearchQuery query {
          .query = req->getQuery("search"),
          /// TODO: Other parameters
          .include_threads = true,
          .include_comments = true
        };
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
      })
      .get("/create_board", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto& login = m.require_login(txn);
        if (!self->controller->can_create_board(login)) {
          throw ApiError("User cannot create boards", 403);
        }
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/create_board",
            .banner_title = "Create Board",
          })
          .write("<main>")
          .write_create_board_form(m.site)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/login", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        if (m.login) {
          rsp->writeStatus(http_status(303))
            ->writeHeader("Location", "/")
            ->end();
        } else {
          self->writer(rsp)
            .write_html_header(m, {
              .canonical_path = "/login",
              .banner_title = "Login",
            })
            .write_login_form(
              m.site->setup_done ? nullopt : optional(
                txn.get_admin_list().empty()
                  ? "This server is not yet set up. A username and random password should be"
                    " displayed in the server's console log. Log in as this user to continue."
                  : "This server is not yet set up. Log in as an admin user to continue."))
            .write_html_footer(m)
            .finish();
        }
      })
      .get("/register", [self](auto* rsp, auto*, Meta& m) {
        if (!m.site->registration_enabled) throw ApiError("Registration is not enabled on this site", 403);
        auto txn = self->controller->open_read_txn();
        m.populate(txn);
        if (m.login) {
          rsp->writeStatus(http_status(303))
            ->writeHeader("Location", "/")
            ->end();
        } else {
          self->writer(rsp)
            .write_html_header(m, {
              .canonical_path = "/register",
              .banner_title = "Register",
            })
            .write_register_form(m.site)
            .write_html_footer(m)
            .finish();
        }
      })
      .get("/settings", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/settings",
            .banner_title = "User Settings",
          })
          .write("<main>")
          .write_user_settings_tabs(m.site, UserSettingsTab::Settings)
          .write_user_settings_form(m.site, login)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/settings/profile", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/settings/profile",
            .banner_title = "User Settings",
          })
          .write("<main>")
          .write_user_settings_tabs(m.site, UserSettingsTab::Profile)
          .write_user_settings_profile_form(m.site, login)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/settings/account", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/settings/account",
            .banner_title = "User Settings",
          })
          .write("<main>")
          .write_user_settings_tabs(m.site, UserSettingsTab::Account)
          .write_user_settings_account_form(m.site, login)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/b/:name/settings", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto login = m.require_login(txn);
        const auto board = self->controller->local_board_detail(txn, board_id, m.login);
        if (!login.local_user().admin() && login.id != board.local_board().owner()) {
          throw ApiError("Must be admin or board owner to view this page", 403);
        }
        self->writer(rsp)
          .write_html_header(m, board_header_options(req, board.board(), "Board Settings"))
          .write("<main>")
          .write_board_settings_form(m.site, board)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/site_admin", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        if (!InstanceController::can_change_site_settings(login)) {
          throw ApiError("Admin login required to view this page", 403);
        }
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/site_admin",
            .banner_title = "Site Admin",
          })
          .write("<main>")
          .write_site_admin_tabs(m.site, SiteAdminTab::Settings)
          .write_site_admin_form(m.site)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/site_admin/import_export", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        if (!InstanceController::can_change_site_settings(login)) {
          throw ApiError("Admin login required to view this page", 403);
        }
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/site_admin/import_export",
            .banner_title = "Site Admin",
          })
          .write("<main>")
          .write_site_admin_tabs(m.site, SiteAdminTab::ImportExport)
          .write_site_admin_import_export_form()
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/site_admin/applications", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(txn);
        if (!InstanceController::can_change_site_settings(login)) {
          throw ApiError("Admin login required to view this page", 403);
        }
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/site_admin/applications",
            .banner_title = "Site Admin",
          })
          .write("<main>")
          .write_site_admin_tabs(m.site, SiteAdminTab::Applications)
          .write_site_admin_applications_list(*self->controller, txn, m.login, {})
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })

      // API Actions
      //////////////////////////////////////////////////////

      .get("/logout", [self](auto* rsp, auto* req, Meta&) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
        if (req->getHeader("referer").empty()) rsp->writeHeader("Location", "/");
        else rsp->writeHeader("Location", req->getHeader("referer"));
        rsp->end();
      })
      .post_form("/login", [self](auto* req, auto m) {
        if (m->logged_in_user_id) throw ApiError("Already logged in", 403);
        return [self,
          user_agent = string(req->getHeader("user-agent")),
          referer = string(req->getHeader("referer")),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          if (body.optional_string("username") /* actually a honeypot */) {
            spdlog::warn("Caught a bot with honeypot field on login");
            // just leave the connecting hanging, let the bots time out
            write([](auto* rsp) { rsp->writeStatus(http_status(418)); });
            return;
          }
          bool remember = body.optional_bool("remember");
          try {
            write([=, login = self->controller->login(
              body.required_string("actual_username"),
              body.required_string("password"),
              m->ip,
              user_agent,
              remember
            )](auto* rsp) mutable {
              rsp->writeStatus(http_status(303))
                ->writeHeader("Set-Cookie",
                  fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
                    login.session_id, fmt::gmtime(login.expiration)))
                ->writeHeader("Location", (referer.empty() || referer == "/login" || !self->controller->site_detail()->setup_done) ? "/" : referer)
                ->end();
            });
          } catch (ApiError e) {
            write([=, m=std::move(m)](auto* rsp) mutable {
              rsp->writeStatus(http_status(e.http_status));
              self->writer(rsp)
                .write_html_header(*m, {
                  .canonical_path = "/login",
                  .banner_title = "Login",
                })
                .write_login_form({e.message})
                .write_html_footer(*m)
                .finish();
            });
          }
        };
      })
      .post_form("/register", [self](auto* req, auto m) {
        if (!m->site->registration_enabled) throw ApiError("Registration is not enabled on this site", 403);
        if (m->logged_in_user_id) throw ApiError("Already logged in", 403);
        return [self,
          user_agent = string(req->getHeader("user-agent")),
          referer = string(req->getHeader("referer")),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          write([=, m = std::move(m)] (auto* rsp) mutable {
            if (body.optional_string("username") /* actually a honeypot */) {
              spdlog::warn("Caught a bot with honeypot field on register");
              // just leave the connecting hanging, let the bots time out
              rsp->writeStatus(http_status(418));
              return;
            }
            try {
              SecretString password = body.required_string("password"),
                confirm_password = body.required_string("confirm_password");
              if (password.data != confirm_password.data) {
                throw ApiError("Passwords do not match", 400);
              }
              self->controller->register_local_user(
                body.required_string("actual_username"),
                body.required_string("email"),
                std::move(password),
                rsp->getRemoteAddressAsText(),
                user_agent,
                body.optional_string("invite_code").transform(invite_code_to_id),
                body.optional_string("application_reason")
              );
            } catch (ApiError e) {
              rsp->writeStatus(http_status(e.http_status));
              self->writer(rsp)
                .write_html_header(*m, {
                  .canonical_path = "/register",
                  .banner_title = "Register",
                })
                .write_register_form(m->site, {e.message})
                .write_html_footer(*m)
                .finish();
              return;
            }
            self->writer(rsp)
              .write_html_header(*m, {
                .canonical_path = "/register",
                .banner_title = "Register",
              })
              .write(R"(<main><div class="form form-page"><h2>Registration complete!</h2>)"
                R"(<p>Log in to your new account:</p><p><a class="big-button" href="/login">Login</a></p>)"
                "</div></main>")
              .write_html_footer(*m)
              .finish();
          });
        };
      })
      .post_form("/create_board", [self](auto*, auto m) {
        return [self, user = m->require_login(), m = std::move(m)](auto body, auto&& write) mutable {
          const auto name = body.required_string("name");
          self->controller->create_local_board(
            user,
            name,
            body.optional_string("display_name"),
            body.optional_string("content_warning"),
            body.optional_bool("private"),
            body.optional_bool("restricted_posting"),
            body.optional_bool("local_only")
          );
          write([=, m = std::move(m)](auto* rsp) mutable {
            rsp->writeStatus(http_status(303));
            m->write_cookie(rsp);
            rsp->writeHeader("Location", fmt::format("/b/{}", name))
              ->end();
          });
        };
      })
      .post_form("/b/:name/create_thread", [self](auto* req, auto m) {
        auto txn = self->controller->open_read_txn();
        return [self,
          board_id = board_name_param(txn, req, 0),
          user = m->require_login(),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          const auto id = self->controller->create_local_thread(
            user,
            board_id,
            body.required_string("title"),
            body.optional_string("submission_url"),
            body.optional_string("text_content"),
            body.optional_string("content_warning")
          );
          write([=, m = std::move(m)](auto* rsp) mutable {
            rsp->writeStatus(http_status(303));
            m->write_cookie(rsp);
            rsp->writeHeader("Location", fmt::format("/thread/{:x}", id))
              ->end();
          });
        };
      })
      .post_form("/thread/:id/reply", [self](auto* req, auto m) {
        return [self,
          user = m->require_login(),
          thread_id = hex_id_param(req, 0),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          const auto id = self->controller->create_local_comment(
            user,
            thread_id,
            body.required_string("text_content"),
            body.optional_string("content_warning")
          );
          write([=, m = std::move(m)] (auto* rsp) mutable {
            if (m->is_htmx) {
              auto txn = self->controller->open_read_txn();
              m->populate(txn);
              const auto comment = CommentDetail::get(txn, id, m->login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              m->write_cookie(rsp);
              self->writer(rsp)
                .write_comment_entry(comment, m->site, m->login, false, true, true, false, true)
                .write_toast("Reply submitted")
                .finish();
            } else {
              rsp->writeStatus(http_status(303));
              m->write_cookie(rsp);
              rsp->writeHeader("Location", fmt::format("/thread/{:x}", thread_id))
                ->end();
            }
          });
        };
      })
      .post_form("/comment/:id/reply", [self](auto* req, auto m) {
        return [self,
          user = m->require_login(),
          comment_id = hex_id_param(req, 0),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          const auto id = self->controller->create_local_comment(
            user,
            comment_id,
            body.required_string("text_content"),
            body.optional_string("content_warning")
          );
          write([=, m = std::move(m)] (auto* rsp) mutable {
            if (m->is_htmx) {
              auto txn = self->controller->open_read_txn();
              m->populate(txn);
              const auto comment = CommentDetail::get(txn, id, m->login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              m->write_cookie(rsp);
              self->writer(rsp)
                .write_comment_entry(comment, m->site, m->login, false, true, true, false, true)
                .write_toast("Reply submitted")
                .finish();
            } else {
              rsp->writeStatus(http_status(303));
              m->write_cookie(rsp);
              rsp->writeHeader("Location", fmt::format("/comment/{:x}", comment_id))
                ->end();
            }
          });
        };
      })
      .post_form("/thread/:id/action", [self](auto* req, auto m) {
        return [self,
          id = hex_id_param(req, 0),
          user = m->require_login(),
          referer = string(req->getHeader("referer")),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          const auto action = static_cast<SubmenuAction>(body.required_int("action"));
          const auto redirect = self->template do_submenu_action<ThreadDetail>(action, user, id);
          write([=, m = std::move(m)] (auto* rsp) mutable {
            if (redirect) {
              write_redirect_to(rsp, *m, *redirect);
            } else if (m->is_htmx) {
              const auto show_user = body.optional_bool("show_user"),
                show_board = body.optional_bool("show_board");
              auto txn = self->controller->open_read_txn();
              const auto login = LocalUserDetail::get_login(txn, user);
              const auto thread = ThreadDetail::get(txn, id, login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              m->write_cookie(rsp);
              self->writer(rsp)
                .write_controls_submenu(thread, login, show_user, show_board)
                .finish();
            } else {
              write_redirect_back(rsp, referer);
            }
          });
        };
      })
      .post_form("/comment/:id/action", [self](auto* req, auto m) {
        return [self,
          id = hex_id_param(req, 0),
          user = m->require_login(),
          referer = string(req->getHeader("referer")),
          m = std::move(m)
        ](auto body, auto&& write) mutable {
          const auto action = static_cast<SubmenuAction>(body.required_int("action"));
          const auto redirect = self->template do_submenu_action<CommentDetail>(action, user, id);
          write([=, m = std::move(m)] (auto* rsp) mutable {
            if (redirect) {
              write_redirect_to(rsp, *m, *redirect);
            } else if (m->is_htmx) {
              const auto show_user = body.optional_bool("show_user"),
                show_board = body.optional_bool("show_board");
              auto txn = self->controller->open_read_txn();
              const auto login = LocalUserDetail::get_login(txn, user);
              const auto comment = CommentDetail::get(txn, id, login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              m->write_cookie(rsp);
              self->writer(rsp)
                .write_controls_submenu(comment, login, show_user, show_board)
                .finish();
            } else {
              write_redirect_back(rsp, referer);
            }
          });
        };
      })
      .post_form("/thread/:id/vote", [self](auto* req, auto m) {
        return [self,
          user = m->require_login(),
          site = m->site,
          post_id = hex_id_param(req, 0),
          referer = string(req->getHeader("referer")),
          is_htmx = m->is_htmx
        ](auto body, auto&& write) {
          const auto vote = body.required_vote("vote");
          self->controller->vote(user, post_id, vote);
          write([=](auto* rsp) {
            if (is_htmx) {
              auto txn = self->controller->open_read_txn();
              const auto login = LocalUserDetail::get_login(txn, user);
              const auto thread = ThreadDetail::get(txn, post_id, login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              self->writer(rsp).write_vote_buttons(thread, site, login).finish();
            } else {
              write_redirect_back(rsp, referer);
            }
          });
        };
      })
      .post_form("/comment/:id/vote", [self](auto* req, auto m) {
        return [self,
          user = m->require_login(),
          site = m->site,
          post_id = hex_id_param(req, 0),
          referer = string(req->getHeader("referer")),
          is_htmx = m->is_htmx
        ](auto body, auto&& write) {
          const auto vote = body.required_vote("vote");
          self->controller->vote(user, post_id, vote);
          write([=](auto* rsp) {
            if (is_htmx) {
              auto txn = self->controller->open_read_txn();
              const auto login = LocalUserDetail::get_login(txn, user);
              const auto comment = CommentDetail::get(txn, post_id, login);
              rsp->writeHeader("Content-Type", TYPE_HTML);
              self->writer(rsp).write_vote_buttons(comment, site, login).finish();
            } else {
              write_redirect_back(rsp, referer);
            }
          });
        };
      })
      .post_form("/b/:name/subscribe", [self](auto* req, auto m) {
        auto txn = self->controller->open_read_txn();
        return [self,
          board_id = board_name_param(txn, req, 0),
          name = string(req->getParameter(0)),
          user = m->require_login(),
          referer = string(req->getHeader("referer")),
          is_htmx = m->is_htmx
        ](QueryString<string> body, auto&& write) {
          self->controller->subscribe(user, board_id, !body.optional_bool("unsubscribe"));
          write([=](auto* rsp) mutable {
            if (is_htmx) {
              rsp->writeHeader("Content-Type", TYPE_HTML);
              self->writer(rsp).write_subscribe_button(name, !body.optional_bool("unsubscribe")).finish();
            } else {
              write_redirect_back(rsp, referer);
            }
          });
        };
      })
      .post_form("/site_admin", [self](auto*, auto m) {
        {
          auto txn = self->controller->open_read_txn();
          const auto login = m->require_login(txn);
          if (!InstanceController::can_change_site_settings(login)) {
            throw ApiError("Admin login required to perform this action", 403);
          }
        }
        return [self, m=std::move(m)](QueryString<string> body, auto&& write) mutable {
          try {
            self->controller->update_site(form_to_site_update(body), m->logged_in_user_id);
            write([](auto* rsp) {
              write_redirect_back(rsp, "/site_admin");
            });
          } catch (const ApiError& e) {
            write([=, m=std::move(m)](auto* rsp) mutable {
              rsp->writeStatus(http_status(e.http_status));
              self->writer(rsp)
                .write_html_header(*m, {
                  .canonical_path = "/site_admin",
                  .banner_title = "Site Admin",
                })
                .write("<main>")
                .write_site_admin_tabs(m->site, SiteAdminTab::Settings)
                .write_site_admin_form(m->site, {e.message})
                .write("</main>")
                .write_html_footer(*m)
                .finish();
            });
          }
        };
      })
      .post_form("/site_admin/first_run_setup", [self](auto*, auto m) {
        {
          if (m->site->setup_done) {
            throw ApiError("First-run setup is already complete", 403);
          }
          auto txn = self->controller->open_read_txn();
          const auto login = m->require_login(txn);
          if (!InstanceController::can_change_site_settings(login)) {
            throw ApiError("Admin login required to perform this action", 403);
          }
        }
        return [self, m=std::move(m)](QueryString<string> body, auto&& write) mutable {
          try {
            self->controller->first_run_setup({
              form_to_site_update(body),
              body.optional_string("base_url"),
              body.optional_string("default_board_name"),
              body.optional_string("admin_username"),
              body.optional_string("admin_password").transform(λx(SecretString(x)))
            });
            write([](auto* rsp) {
              write_redirect_back(rsp, "/");
            });
          } catch (const ApiError& e) {
            write([=, m=std::move(m)](auto* rsp) mutable {
              rsp->writeStatus(http_status(e.http_status));
              auto txn = self->controller->open_read_txn();
              self->writer(rsp)
                .write_html_header(*m, {
                  .canonical_path = "/",
                  .banner_title = "First-Run Setup",
                })
                .write("<main>")
                .write_first_run_setup_form(self->controller->first_run_setup_options(txn), e.message)
                .write("</main>")
                .write_html_footer(*m)
                .finish();
            });
          }
        };
      })
      .post("/site_admin/export", [self](auto*, auto m) {
        {
          auto txn = self->controller->open_read_txn();
          const auto login = m->require_login(txn);
          if (!InstanceController::can_change_site_settings(login)) {
            throw ApiError("Admin login required to perform this action", 403);
          }
        }
        return [self](string, auto&& write) {
          write([](auto rsp) {
            rsp->writeHeader("Content-Type", "application/zstd")
              ->writeHeader(
                "Content-Disposition",
                fmt::format(
                  R"(attachment; filename="ludwig-{:%F-%H%M%S}.dbdump.zst")",
                  fmt::localtime(chrono::system_clock::now())
                )
              );
          });
          std::thread([self, write = std::move(write)] mutable {
            spdlog::info("Beginning database dump");
            try {
              auto txn = self->controller->open_read_txn();
              zstd_db_dump_export(txn, [&write](auto&& buf, auto sz) {
                write([buf = std::move(buf), sz](auto rsp) {
                  rsp->write(string_view{(const char*)buf.get(), sz});
                });
              });
              spdlog::info("Database dump completed successfully");
            } catch (const std::exception& e) {
              spdlog::error("Database dump failed: {}", e.what());
            }
            write([](auto rsp) { rsp->end(); });
          }).detach();
        };
      })
      .post("/site_admin/applications/approve/:id", [self](auto* req, auto m) {
        {
          auto txn = self->controller->open_read_txn();
          const auto login = m->require_login(txn);
          if (!InstanceController::can_change_site_settings(login)) {
            throw ApiError("Admin login required to perform this action", 403);
          }
        }
        return [self, id=hex_id_param(req, 0), m=std::move(m)](string, auto&& write) mutable {
          try {
            self->controller->approve_local_user_application(id, m->logged_in_user_id);
            write([](auto* rsp) {
              write_redirect_back(rsp, "/site_admin/applications");
            });
          } catch (const ApiError& e) {
            write([=, m=std::move(m)](auto* rsp) mutable {
              rsp->writeStatus(http_status(e.http_status));
              auto txn = self->controller->open_read_txn();
              self->writer(rsp)
                .write_html_header(*m, {
                  .canonical_path = "/site_admin/applications",
                  .banner_title = "Site Admin",
                })
                .write("<main>")
                .write_site_admin_tabs(m->site, SiteAdminTab::Applications)
                .write_site_admin_applications_list(*self->controller, txn, m->login, {}, e.message)
                .write("</main>")
                .write_html_footer(*m)
                .finish();
            });
          }
        };
      })
      .any("/*", [](auto*, auto*, auto&) {
        throw ApiError("Page not found", 404);
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

  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;
}
