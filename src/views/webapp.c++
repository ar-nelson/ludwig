#include "views/webapp.h++"
#include "util/web.h++"
#include "static/default-theme.css.h++"
#include "static/htmx.min.js.h++"
#include "static/feather-sprite.svg.h++"
#include <iterator>
#include <regex>
#include <spdlog/fmt/chrono.h>
#include "xxhash.h"
#include "util/lambda_macros.h++"

using std::bind, std::match_results, std::nullopt, std::optional, std::regex,
    std::regex_search, std::shared_ptr, std::stoull, std::string,
    std::string_view, std::to_string, std::variant, std::visit;

using namespace std::placeholders;
namespace chrono = std::chrono;

#define COOKIE_NAME "ludwig_session"

namespace Ludwig {
  static const regex cookie_regex(
    R"((?:^|;)\s*)" COOKIE_NAME R"(\s*=\s*([^;]+))",
    regex::ECMAScript
  );

  enum class SortFormType {
    Board,
    Comments,
    User
  };

  enum class SubmenuAction {
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

  static inline auto format_as(SubmenuAction a) { return fmt::underlying(a); };

  static inline auto mod_state(const ThreadDetail& thread) -> ModState {
    return thread.thread().mod_state();
  }

  static inline auto mod_state(const CommentDetail& comment) -> ModState {
    return comment.comment().mod_state();
  }

  // Adapted from https://programming.guide/java/formatting-byte-size-to-human-readable-format.html
  static auto suffixed_short_number(int64_t n) -> string {
    static constexpr auto SUFFIXES = "KMBTqQ";
    if (-1000 < n && n < 1000) return to_string(n);
    uint8_t i = 0;
    while (n <= -999'950 || n >= 999'950) {
      n /= 1000;
      i++;
    }
    return fmt::format("{:.3g}{:c}", (double)n / 1000.0, SUFFIXES[i]);
    // SUFFIXES[i] can never overflow, max 64-bit int is ~18 quintillion (Q)
  }

  static constexpr auto describe_mod_state(ModState s) -> string_view {
    switch (s) {
      case ModState::Flagged: return "Flagged";
      case ModState::Locked: return "Locked";
      case ModState::Removed: return "Removed";
      default: return "";
    }
  }

  template <typename T> static constexpr auto post_word() -> string_view;
  template <> constexpr auto post_word<ThreadDetail>() -> string_view { return "thread"; }
  template <> constexpr auto post_word<CommentDetail>() -> string_view { return "comment"; }

  template <bool SSL> struct Webapp : public std::enable_shared_from_this<Webapp<SSL>> {
    shared_ptr<InstanceController> controller;
    shared_ptr<RichTextParser> rt;
    shared_ptr<KeyedRateLimiter> rate_limiter; // may be null!

    Webapp(
      shared_ptr<InstanceController> controller,
      shared_ptr<RichTextParser> rt,
      shared_ptr<KeyedRateLimiter> rl
    ) : controller(controller), rt(rt), rate_limiter(rl) {}

    using App = uWS::TemplatedApp<SSL>;
    using Response = uWS::HttpResponse<SSL>*;
    using Request = uWS::HttpRequest*;

    struct ErrorMeta {
      bool is_htmx;
    };

    auto error_middleware(const uWS::HttpResponse<SSL>*, Request req) noexcept -> ErrorMeta {
      return {
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty()
      };
    }

    struct Meta {
      chrono::time_point<chrono::steady_clock> start;
      optional<uint64_t> logged_in_user_id;
      optional<string> session_cookie;
      bool is_htmx;
      const SiteDetail* site;
      optional<LocalUserDetail> login;

      auto populate(shared_ptr<Webapp<SSL>> self, ReadTxnBase& txn) {
        site = self->controller->site_detail();
        if (logged_in_user_id) {
          login.emplace(LocalUserDetail::get(txn, *logged_in_user_id));
        }
      }

      auto require_login() {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        return *logged_in_user_id;
      }

      auto require_login(shared_ptr<Webapp<SSL>> self, ReadTxnBase& txn) -> const LocalUserDetail& {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        if (site == nullptr) populate(self, txn);
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

    auto middleware(Response rsp, Request req) -> Meta {
      const auto start = chrono::steady_clock::now();
      const string ip(get_ip(rsp, req));

      if (rate_limiter && !rate_limiter->try_acquire(ip, req->getMethod() == "GET" ? 1 : 10)) {
        throw ApiError("Rate limited, try again later", 429);
      }

      optional<string> session_cookie;
      optional<LoginResponse> new_session;
      const auto cookies = req->getHeader("cookie");
      match_results<string_view::const_iterator> match;
      if (regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) {
        try {
          auto txn = controller->open_read_txn();
          const auto old_session = stoull(match[1], nullptr, 16);
          new_session = controller->validate_or_regenerate_session(
            txn, old_session, ip, req->getHeader("user-agent")
          );
          if (!new_session) throw std::runtime_error("expired session");
          if (new_session->session_id != old_session) {
            spdlog::debug("Regenerated session {:x} as {:x}", old_session, new_session->session_id);
            session_cookie =
              fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
                new_session->session_id, fmt::gmtime((time_t)new_session->expiration));
          }
        } catch (...) {
          spdlog::debug("Auth cookie is invalid; requesting deletion");
          session_cookie = COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";
        }
      }
      return {
        .start = start,
        .logged_in_user_id = new_session.transform(λx(x.user_id)),
        .session_cookie = session_cookie,
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty(),
      };
    }

    static auto display_name_as_text(const User& user) -> string {
      if (user.display_name_type()->size()) {
        return RichTextParser::plain_text_with_emojis_to_text_content(user.display_name_type(), user.display_name());
      }
      const auto name = user.name()->string_view();
      return string(name.substr(0, name.find('@')));
    }

    static auto display_name_as_text(const Board& board) -> string {
      if (board.display_name_type()->size()) {
        return RichTextParser::plain_text_with_emojis_to_text_content(board.display_name_type(), board.display_name());
      }
      const auto name = board.name()->string_view();
      return string(name.substr(0, name.find('@')));
    }

    struct ResponseWriter {
      RichTextParser& rt;
      InstanceController& controller;
      Response rsp;
      string buf;

      ResponseWriter(Webapp<SSL>* w, Response rsp) : rt(*w->rt), controller(*w->controller), rsp(rsp) { buf.reserve(1024); }
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
        if (user->display_name_type()->size()) {
          write(RichTextParser::plain_text_with_emojis_to_html(user->display_name_type(), user->display_name(), {}));
          const auto at_index = name.find('@');
          if (at_index != string_view::npos) write(name.substr(at_index));
        } else {
          write(name);
        }
        return *this;
      }

      auto write_qualified_display_name(const Board* board) noexcept -> ResponseWriter& {
        const auto name = board->name()->string_view();
        if (board->display_name_type()->size()) {
          write(RichTextParser::plain_text_with_emojis_to_html(board->display_name_type(), board->display_name(), {}));
          const auto at_index = name.find('@');
          if (at_index != string_view::npos) write(name.substr(at_index));
        } else {
          write(name);
        }
        return *this;
      }

      auto display_name_as_html(const User& user) -> string {
        if (user.display_name_type()->size()) {
          return rt.plain_text_with_emojis_to_html(user.display_name_type(), user.display_name(), {});
        }
        const auto name = user.name()->string_view();
        return fmt::format("{}", Escape(name.substr(0, name.find('@'))));
      }

      auto display_name_as_html(const Board& board) -> string {
        if (board.display_name_type()->size()) {
          return rt.plain_text_with_emojis_to_html(board.display_name_type(), board.display_name(), {});
        }
        const auto name = board.name()->string_view();
        return fmt::format("{}", Escape(name.substr(0, name.find('@'))));
      }

      struct HtmlHeaderOptions {
        optional<string_view> canonical_path, banner_title, banner_link, banner_image, page_title, card_image;
      };

      auto write_html_header(const Meta& m, HtmlHeaderOptions opt) noexcept -> ResponseWriter& {
        assert(m.site != nullptr);
        rsp->writeHeader("Content-Type", TYPE_HTML);
        m.write_cookie(rsp);
        write_fmt(
          R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
          R"(<meta name="viewport" content="width=device-width,initial-scale=1">)"
          R"(<meta name="referrer" content="same-origin"><title>{}{}{}</title>)"
          R"(<link rel="stylesheet" href="/static/default-theme.css">)"
          R"(<script src="/static/htmx.min.js"></script>)",
          Escape{m.site->name},
          (opt.page_title || opt.banner_title) ? " - " : "",
          Escape{
            opt.page_title ? *opt.page_title :
            opt.banner_title ? *opt.banner_title :
            ""
          }
        );
        if (opt.canonical_path) {
          write_fmt(
            R"(<link rel="canonical" href="{0}{1}">)"
            R"(<meta property="og:url" content="{0}{1}">)"
            R"(<meta property="twitter:url" content="{0}{1}">)",
            Escape{m.site->domain}, Escape{*opt.canonical_path}
          );
        }
        if (opt.page_title) {
          write_fmt(
            R"(<meta property="title" href="{0} - {1}">)"
            R"(<meta property="og:title" content="{0} - {1}">)"
            R"(<meta property="twitter:title" content="{0} - {1}">)"
            R"(<meta property="og:type" content="website">)",
            Escape{m.site->domain}, Escape{*opt.page_title}
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
        } else {
          write(R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)");
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
          R"(<div class="spacer"></div><footer><small>Powered by Ludwig · Generated in {:L}μs</small></footer></body></html>)",
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
        variant<
          const SiteDetail*,
          const BoardDetail,
          const UserDetail
        > detail
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
            R"(<a href="/register" class="big-button">Register</a></section>)",
            HONEYPOT_FIELD
          );
        } else {
          visit(overload{
            [&](const SiteDetail*) {
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
          [&](const SiteDetail* site) {
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
            if (board.board().description_type()->size()) {
              write_fmt("<p>{}</p>", rt.blocks_to_html(
                board.board().description_type(),
                board.board().description(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          },
          [&](const UserDetail& user) {
            write_fmt(R"(<section id="user-sidebar"><h2>{}</h2>)", display_name_as_html(user.user()));
            if (user.user().bio_type()->size()) {
              write_fmt("<p>{}</p>", rt.blocks_to_html(
                user.user().bio_type(),
                user.user().bio(),
                { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
              ));
            }
          }
        }, detail);

        return write("</section></aside>");
      }

      auto write_datetime(uint64_t timestamp) noexcept -> ResponseWriter& {
        return write_fmt(R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)",
          fmt::gmtime((time_t)timestamp), fmt::localtime((time_t)timestamp), relative_time(timestamp));
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

      auto write_board_list(
        const PageOf<BoardDetail>& list,
        string_view base_url,
        bool include_ol = true
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="board-list" id="top-level-list">)");
        for (auto& entry : list.entries) {
          write(R"(<li class="board-list-entry"><h2 class="board-title">)");
          write_board_link(entry.board());
          write("</h2></li>");
        }
        if (include_ol) write("</ol>");
        return write_pagination(base_url, list.is_first, list.next);
      }

      auto write_user_list(
        const PageOf<UserDetail>& list,
        string_view base_url,
        Login login = {},
        bool include_ol = true
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="user-list" id="top-level-list">)");
        for (auto& entry : list.entries) {
          write(R"(<li class="user-list-entry">)");
          write_user_link(entry.user(), login);
        }
        if (include_ol) write("</ol>");
        return write_pagination(base_url, list.is_first, list.next);
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

      template <class T> auto write_vote_buttons(const T& entry, Login login) noexcept -> ResponseWriter& {
        const auto can_upvote = entry.can_upvote(login),
          can_downvote = entry.can_downvote(login);
        if (can_upvote || can_downvote) {
          write_fmt(
            R"(<form class="vote-buttons" id="votes-{0:x}" method="post" action="/{1}/{0:x}/vote" hx-post="/{1}/{0:x}/vote" hx-swap="outerHTML">)"
            R"(<output class="karma" id="karma-{0:x}">{2}</output>)"
            R"(<label class="upvote"><button type="submit" name="vote" {3}{5}><span class="a11y">Upvote</span></button></label>)"
            R"(<label class="downvote"><button type="submit" name="vote" {4}{6}><span class="a11y">Downvote</span></button></label>)"
            "</form>",
            entry.id, post_word<T>(), suffixed_short_number(entry.stats().karma()),
            can_upvote ? "" : "disabled ", can_downvote ? "" : "disabled ",
            entry.your_vote == Vote::Upvote ? R"(class="voted" value="0")" : R"(value="1")",
            entry.your_vote == Vote::Downvote ? R"(class="voted" value="0")" : R"(value="-1")"
          );
        } else {
          write_fmt(
            R"(<div class="vote-buttons" id="votes-{0:x}"><output class="karma" id="karma-{0:x}">{1}</output>)"
            R"(<div class="upvote"><button type="button" disabled><span class="a11y">Upvote</span></button></div>)"
            R"(<div class="downvote"><button type="button" disabled><span class="a11y">Downvote</span></button></div></div>)",
            entry.id, suffixed_short_number(entry.stats().karma())
          );
        }
        return *this;
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
          post.id, post_word<T>(), show_user, show_board, SubmenuAction::None
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
          switch (mod_state(post)) {
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
            SubmenuAction::ModPurge, post_word<T>(),
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
        if (is_list_item || thread.thread().content_url()) {
          write_fmt(R"(<a class="thread-title-link" href="{}">{}</a></h2>)",
            Escape{
              thread.thread().content_url()
                ? thread.thread().content_url()->string_view()
                : fmt::format("/thread/{:x}", thread.id)
            },
            Escape(thread.thread().title())
          );
        } else {
          write_fmt("{}</h2>", Escape(thread.thread().title()));
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
          if (!is_list_item && thread.thread().content_text_type()->size()) {
            write_short_warnings(thread.thread());
          }
          else write_warnings(thread.thread());
          write(R"(</div>)");
        }
        write(R"(<div class="thread-info"><span>submitted )");
        write_datetime(thread.thread().created_at());
        if (show_user) {
          write("</span><span>by ");
          write_user_link(thread._author, login);
        }
        if (show_board) {
          write("</span><span>to ");
          write_board_link(thread._board);
        }
        write("</span></div>");
        write_vote_buttons(thread, login);
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

      auto write_thread_list(
        const PageOf<ThreadDetail>& list,
        string_view base_url,
        Login login,
        bool include_ol,
        bool show_user,
        bool show_board,
        bool show_images
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="thread-list" id="top-level-list">)");
        for (const auto& thread : list.entries) {
          write_thread_entry(thread, login, true, show_user, show_board, show_images);
        }
        if (include_ol) write("</ol>");
        return write_pagination(base_url, list.is_first, list.next);
      }

      auto write_comment_entry(
        const CommentDetail& comment,
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
        write_datetime(comment.comment().created_at());
        if (show_thread) {
          write_fmt(R"(</span><span>on <a href="/thread/{:x}">{}</a>)",
            comment.comment().thread(), Escape(comment.thread().title()));
          if (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible) {
            write_short_warnings(comment.thread());
          }
        }
        const bool has_warnings = comment.comment().content_warning() || comment.comment().mod_state() > ModState::Visible,
          thread_warnings = show_thread && (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible);
        const auto content = rt.blocks_to_html(
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
        write_vote_buttons(comment, login);
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

      auto write_comment_list(
        const PageOf<CommentDetail>& list,
        string_view base_url,
        Login login,
        bool include_ol,
        bool show_user,
        bool show_thread,
        bool show_images
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="comment-list" id="top-level-list">)");
        for (const auto& comment : list.entries) {
          write_comment_entry(
            comment, login,
            true, false,
            show_user, show_thread, show_images
          );
        }
        if (include_ol) write("</ol>");
        return write_pagination(base_url, list.is_first, list.next);
      }

      auto write_search_result_list(
        std::vector<InstanceController::SearchResultDetail> list,
        Login login,
        bool include_ol
      ) noexcept -> ResponseWriter& {
        if (include_ol) write(R"(<ol class="search-list" id="top-level-list">)");
        for (const auto& entry : list) {
          std::visit(overload{
            [&](const UserDetail& user) {
              write("<li>");
              write_user_link(user.user(), login);
            },
            [&](const BoardDetail& board) {
              write("<li>");
              write_board_link(board.board());
            },
            [&](const ThreadDetail& thread) {
              write_thread_entry(thread, login, true, true, true, true);
            },
            [&](const CommentDetail& comment) {
              write_comment_entry(comment, login, true, false, true, true, true);
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
          write_comment_entry(comment, login, false, true, true, false, show_images);
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
          R"(<form id="reply-{1:x}" class="form reply-form" method="post" action="/{0}/{1:x}/reply" )"
          R"html(hx-post="/{0}/{1:x}/reply" hx-target="#comments-{1:x}" hx-swap="afterbegin" hx-on::after-request="this.reset()">)html"
          R"(<a name="reply"></a>)"
          HTML_TEXTAREA("text_content", "Reply", R"( placeholder="Write your reply here")", ""),
          post_word<T>(), parent.id
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
        write_thread_entry(thread, login, false, true, true, show_images);
        if (thread.thread().content_text_type()->size()) {
          const auto content = rt.blocks_to_html(
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
          comment, login,
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

      auto write_register_form(optional<string_view> error = {}) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<main><form class="form form-page" method="post" action="/register">{})"
          R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)"
          R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)"
          HTML_FIELD("actual_username", "Username", "text", "")
          HTML_FIELD("email", "Email address", "email", "")
          HTML_FIELD("password", "Password", "password", "")
          HTML_FIELD("confirm_password", "Confirm password", "password", "")
          R"(<input type="submit" value="Register">)"
          "</form></main>",
          error_banner(error)
        );
      }

      auto write_create_board_form(
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<main><form class="form form-page" method="post" action="/create_board"><h2>Create Board</h2>{})"
          HTML_FIELD("name", "Name", "text", R"( autocomplete="off" required)")
          HTML_FIELD("display_name", "Display name", "text", R"( autocomplete="off")")
          HTML_FIELD("content_warning", "Content warning (optional)", "text", R"( autocomplete="off")")
          HTML_CHECKBOX("private", "Private (only visible to members)", "")
          HTML_CHECKBOX("restricted_posting", "Restrict posting to moderators", "")
          HTML_CHECKBOX("approve_subscribe", "Approval required to join", "")
          //HTML_CHECKBOX("invite_required", "Invite code required to join", "")
          //HTML_CHECKBOX("invite_mod_only", "Only moderators can invite new members", "")
          R"(<fieldset><legend>Voting</legend>)"
            R"(<label for="vote_both"><input type="radio" id="vote_both" name="voting" value="2" checked> Allow voting</label>)"
            R"(<label for="vote_up"><input type="radio" id="vote_up" name="voting" value="1"> Only upvotes allowed</label>)"
            R"(<label for="vote_none"><input type="radio" id="vote_none" name="voting" value="0"> No voting or karma</label>)"
          R"(</fieldset>)"
          R"(<input type="submit" value="Submit">)"
          "</form></main>",
          error_banner(error)
        );
      }

      auto write_create_thread_form(
        bool show_url,
        const BoardDetail board,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form class="form form-page" method="post" action="/b/{}/create_thread"><h2>Create Thread</h2>{})"
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
        return *this;
      }

      auto write_edit_thread_form(
        const ThreadDetail& thread,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        write_fmt(
          R"(<main><form class="form form-page" method="post" action="/thread/{:x}/edit"><h2>Edit Thread</h2>{})"
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
          Escape(thread.thread().title()), thread.thread().content_url() ? "" : " required",
          Escape(thread.thread().content_text_raw())
        );
        write_content_warning_field(thread.thread().content_warning() ? thread.thread().content_warning()->string_view() : "");
        return write(R"(<input type="submit" value="Submit"></form></main>)");
      }

      auto write_site_admin_tabs(const SiteDetail* site, uint8_t selected) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<ul class="tabs"><li>{}<li>{}{}</ul>)",
          selected == 0 ? R"(<a href="/site_admin">Settings</a>)" : "<div>Settings</div>",
          selected == 1 ? R"(<a href="/site_admin/importexport">Import/Export</a>)" : "<div>Import/Export</div>",
          site->registration_invite_required
            ? (selected == 2 ? R"(<li><a href="/site_admin/invites">Invites</a>)" : "<li><div>Invites</div>")
            : ""
        );
      }

      auto write_site_admin_form(const SiteDetail* site, optional<string_view> error = {}) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form class="form form-page" method="post" action="/site_admin"><h2>Site settings</h2>{})"
          HTML_FIELD("name", "Site name", "text", R"( value="{}")")
          HTML_TEXTAREA("description", "Sidebar description", "", "{}")
          HTML_FIELD("icon_url", "Icon URL", "text", R"( value="{}")")
          HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}")")
          HTML_FIELD("max_post_length", "Max post length (bytes)", "number", R"( min="512" value="{:d}")")
          HTML_CHECKBOX("javascript_enabled", "JavaScript enabled", "{}")
          HTML_CHECKBOX("board_creation_admin_only", "Only admins can create boards", "{}")
          HTML_CHECKBOX("registration_enabled", "Registration enabled", "{}")
          HTML_CHECKBOX("registration_application_required", "Application required for registration", "{}")
          HTML_CHECKBOX("registration_invite_required", "Invite code required for registration", "{}")
          HTML_CHECKBOX("invite_admin_only", "Only admins can invite new users", "{}")
          R"(<input type="submit" value="Submit"></form>)",
          error_banner(error),
          Escape{site->name}, Escape{site->description},
          Escape{site->icon_url.value_or("")}, Escape{site->banner_url.value_or("")},
          site->max_post_length,
          site->javascript_enabled ? " checked" : "", site->board_creation_admin_only ? " checked" : "",
          site->registration_enabled ? " checked" : "", site->registration_application_required ? " checked" : "",
          site->registration_invite_required ? " checked" : "", site->invite_admin_only ? " checked" : ""
        );
      }

      auto write_user_settings_tabs(const SiteDetail* site, uint8_t selected) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<ul class="tabs"><li>{}<li>{}<li>{}{}</ul>)",
          selected == 0 ? R"(<a href="/settings">Settings</a>)" : "<div>Settings</div>",
          selected == 1 ? R"(<a href="/settings/profile">Profile</a>)" : "<div>Profile</div>",
          selected == 2 ? R"(<a href="/settings/account">Account</a>)" : "<div>Account</div>",
          site->registration_invite_required && !site->invite_admin_only
            ? (selected == 3 ? R"(<li><a href="/settings/invites">Invites</a>)" : "<li><div>Invites</div>")
            : ""
        );
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
          R"(<form class="form form-page" method="post" action="/settings"><h2>User settings</h2>{})"
          HTML_CHECKBOX("open_links_in_new_tab", "Open links in new tab", "{}")
          HTML_CHECKBOX("show_avatars", "Show avatars", "{}")
          HTML_CHECKBOX("show_images_threads", "Show images on threads by default", "{}")
          HTML_CHECKBOX("show_images_comments", "Show inline images in comments by default", "{}")
          HTML_CHECKBOX("show_bot_accounts", "Show bot accounts", "{}")
          HTML_CHECKBOX("show_karma", "Show karma", "{}")
          R"(<fieldset><legend>Content warnings</legend>)"
            R"(<label for="cw_hide"><input type="radio" id="cw_hide" name="content_warnings" value="0"{}> Hide posts with content warnings completely</label>)"
            R"(<label for="cw_default"><input type="radio" id="cw_default" name="content_warnings" value="1"{}> Collapse posts with content warnings (default)</label>)"
            R"(<label for="cw_show"><input type="radio" id="cw_show" name="content_warnings" value="2"{}> Expand text content of posts with content warnings but hide images</label>)"
            R"(<label for="cw_show_images"><input type="radio" id="cw_show_images" name="content_warnings" value="3"{}> Always expand text and images with content warnings</label>)"
          R"(</fieldset>)",
          error_banner(error),
          login.local_user().open_links_in_new_tab() ? " checked" : "",
          login.local_user().show_avatars() ? " checked" : "",
          login.local_user().show_images_threads() ? " checked" : "",
          login.local_user().show_images_comments() ? " checked" : "",
          login.local_user().show_bot_accounts() ? " checked" : "",
          login.local_user().show_karma() ? " checked" : "",
          cw_mode == 0 ? " checked" : "", cw_mode == 1 ? " checked" : "", cw_mode == 2 ? " checked" : "", cw_mode == 3 ? " checked" : ""
        );
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

      auto write_user_profile_form(
        const SiteDetail* site,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form class="form form-page" method="post" action="/settings/profile"><h2>Profile</h2>{})"
          R"(<label for="name"><span>Username</span><output name="name" id="name">{}</output></label>)"
          HTML_FIELD("display_name", "Display name", "text", R"( value="{}")")
          HTML_FIELD("email", "Email address", "email", R"( required value="{}")")
          HTML_TEXTAREA("bio", "Bio", "", "{}")
          HTML_FIELD("avatar_url", "Avatar URL", "text", R"( value="{}")")
          HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}")")
          R"(<input type="submit" value="Submit"></form>)",
          error_banner(error),
          Escape(login.user().name()),
          Escape(rt.plain_text_with_emojis_to_text_content(login.user().display_name_type(), login.user().display_name())),
          Escape(login.local_user().email()),
          Escape(login.user().bio_raw()),
          Escape(login.user().avatar_url()),
          Escape(login.user().banner_url())
        );
      }

      auto write_user_account_forms(
        const SiteDetail* site,
        const LocalUserDetail& login,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form class="form form-page" method="post" action="/settings/account"><h2>Change password</h2>{})"
          HTML_FIELD("old_password", "Old password", "password", R"( required autocomplete="off")")
          HTML_FIELD("password", "New password", "password", R"( required autocomplete="off")")
          HTML_FIELD("confirm_password", "Confirm new password", "password", R"( required autocomplete="off")")
          R"(<input type="submit" value="Submit"></form><br>)"
          R"(<form class="form form-page" method="post" action="/settings/delete_account"><h2>Delete account</h2>)"
          R"(<p>Warning: this cannot be undone!</p>)"
          HTML_FIELD("delete_password", "Type your password here", "password", R"( required autocomplete="off")")
          HTML_FIELD("delete_confirm", R"(Type "delete" here to confirm)", "text", R"( required autocomplete="off")")
          HTML_CHECKBOX("delete_posts", "Also delete all of my posts", R"( autocomplete="off")"),
          R"(<input type="submit" value="Delete Account"></form>)",
          error_banner(error)
        );
      }

      auto write_board_settings_form(
        const LocalBoardDetail& board,
        optional<string_view> error = {}
      ) noexcept -> ResponseWriter& {
        return write_fmt(
          R"(<form class="form form-page" method="post" action="/b/{}/settings"><h2>Board settings</h2>{})"
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
          R"(<fieldset><legend>Voting</legend>)"
            R"(<label for="vote_both"><input type="radio" id="vote_both" name="voting" value="2"{}> Allow voting</label>)"
            R"(<label for="vote_up"><input type="radio" id="vote_up" name="voting" value="1"{}> Only upvotes allowed</label>)"
            R"(<label for="vote_none"><input type="radio" id="vote_none" name="voting" value="0"{}> No voting or karma</label>)"
          R"(</fieldset>)"
          R"(<input type="submit" value="Submit"></form>)",
          Escape(board.board().name()), error_banner(error),
          Escape(rt.plain_text_with_emojis_to_text_content(board.board().display_name_type(), board.board().display_name())),
          Escape(board.board().description_raw()),
          Escape(board.board().content_warning()),
          Escape(board.board().icon_url()),
          Escape(board.board().banner_url()),
          board.local_board().private_() ? " checked" : "",
          board.board().restricted_posting() ? " checked" : "",
          board.board().approve_subscribe() ? " checked" : "",
          board.board().can_downvote() ? " checked" : "",
          board.board().can_upvote() ? " checked" : "",
          !board.board().can_downvote() && !board.board().can_upvote() ? " checked" : ""
        );
      }
    };

    auto writer(Response rsp) -> ResponseWriter { return ResponseWriter(this, rsp); }

    auto error_page(Response rsp, const ApiError& e, const ErrorMeta& m) noexcept -> void {
      if (m.is_htmx) {
        writer(
          rsp->writeStatus(http_status(200))
            ->writeHeader("Content-Type", TYPE_HTML)
            ->writeHeader("HX-Retarget", "#toasts")
            ->writeHeader("HX-Reswap", "afterbegin"))
          .write_toast(e.message, " toast-error")
          .finish();
      } else if (e.http_status == 401) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Location", "/login")
          ->end();
      } else {
        writer(
          rsp->writeStatus(http_status(e.http_status))
            ->writeHeader("Content-Type", TYPE_HTML))
          .write_fmt("Error {:d}: {}", e.http_status, Escape(e.message))
          .finish();
      }
    }

    static auto relative_time(uint64_t timestamp) noexcept -> string {
      const uint64_t now = now_s();
      if (timestamp > now) return "in the future";
      const uint64_t diff = now - timestamp;
      static constexpr uint64_t
        MINUTE = 60,
        HOUR = MINUTE * 60,
        DAY = HOUR * 24,
        WEEK = DAY * 7,
        MONTH = DAY * 30,
        YEAR = DAY * 365;
      if (diff < MINUTE) return "just now";
      if (diff < MINUTE * 2) return "1 minute ago";
      if (diff < HOUR) return to_string(diff / MINUTE) + " minutes ago";
      if (diff < HOUR * 2) return "1 hour ago";
      if (diff < DAY) return to_string(diff / HOUR) + " hours ago";
      if (diff < DAY * 2) return "1 day ago";
      if (diff < WEEK) return to_string(diff / DAY) + " days ago";
      if (diff < WEEK * 2) return "1 week ago";
      if (diff < MONTH) return to_string(diff / WEEK) + " weeks ago";
      if (diff < MONTH * 2) return "1 month ago";
      if (diff < YEAR) return to_string(diff / MONTH) + " months ago";
      if (diff < YEAR * 2) return "1 year ago";
      return to_string(diff / YEAR) + " years ago";
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
      string_view filename,
      string_view mimetype,
      string_view src
    ) noexcept -> void {
      const auto hash = fmt::format("\"{:016x}\"", XXH3_64bits(src.data(), src.length()));
      app.get(fmt::format("/static/{}", filename), [src, mimetype, hash](auto* res, auto* req) {
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
      if (!user_id) throw ApiError(fmt::format("User \"{}\" does not exist", name), 404);
      return *user_id;
    }

    static inline auto board_name_param(ReadTxnBase& txn, Request req, uint16_t param) {
      const auto name = req->getParameter(param);
      const auto board_id = txn.get_board_id_by_name(name);
      if (!board_id) throw ApiError(fmt::format("Board \"{}\" does not exist", name), 404);
      return *board_id;
    }

    using PostList = std::variant<PageOf<ThreadDetail>, PageOf<CommentDetail>>;

    auto feed_route(uint64_t feed_id, Response rsp, Request req, Meta& m) -> void {
      auto txn = controller->open_read_txn();
      m.populate(this->shared_from_this(), txn);
      const auto sort = parse_sort_type(req->getQuery("sort"), m.login);
      const auto show_threads = req->getQuery("type") != "comments",
        show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_threads() : false);
      const auto base_url = fmt::format("{}?type={}&sort={}&images={}",
        req->getUrl(),
        show_threads ? "threads" : "comments",
        EnumNameSortType(sort),
        show_images ? 1 : 0
      );
      const auto list = show_threads
        ? PostList(controller->list_feed_threads(txn, feed_id, sort, m.login, req->getQuery("from")))
        : PostList(controller->list_feed_comments(txn, feed_id, sort, m.login, req->getQuery("from")));
      // ---
      auto r = writer(rsp);
      if (m.is_htmx) {
        rsp->writeHeader("Content-Type", TYPE_HTML);
        m.write_cookie(rsp);
      } else {
        r.write_html_header(m, {
            .canonical_path = req->getUrl(),
            .banner_title = m.site->name,
            .banner_link = req->getUrl()
          })
          .write("<div>")
          .write_sidebar(m.login, m.site)
          .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
          .write_sort_options(req->getUrl(), sort, show_threads, show_images)
          .write(R"(</section><main>)");
      }
      std::visit(overload{
        [&](const PageOf<ThreadDetail>& l){r.write_thread_list(l, base_url, m.login, !m.is_htmx, true, true, show_images);},
        [&](const PageOf<CommentDetail>& l){r.write_comment_list(l, base_url, m.login, !m.is_htmx, true, true, show_images);}
      }, list);
      if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
      r.finish();
    }

    auto register_routes(App& app) -> void {

      // -----------------------------------------------------------------------
      // STATIC FILES
      // -----------------------------------------------------------------------
      serve_static(app, "default-theme.css", TYPE_CSS, default_theme_css_str());
      serve_static(app, "htmx.min.js", TYPE_JS, htmx_min_js_str());
      serve_static(app, "feather-sprite.svg", TYPE_SVG, feather_sprite_svg_str());

      // -----------------------------------------------------------------------
      // PAGES
      // -----------------------------------------------------------------------
      auto self = this->shared_from_this();
      Router<SSL, Meta, ErrorMeta>(app,
        bind(&Webapp::middleware, self, _1, _2),
        bind(&Webapp::error_middleware, self, _1, _2),
        bind(&Webapp::error_page, self, _1, _2, _3)
      )
      .get("/", [self](auto* rsp, auto* req, Meta& m) {
        self->feed_route(m.logged_in_user_id ? InstanceController::FEED_HOME : InstanceController::FEED_LOCAL, rsp, req, m);
      })
      .get("/all", [self](auto* rsp, auto* req, Meta& m) {
        self->feed_route(InstanceController::FEED_ALL, rsp, req, m);
      })
      .get("/local", [self](auto* rsp, auto* req, Meta& m) {
        self->feed_route(InstanceController::FEED_LOCAL, rsp, req, m);
      })
      .get("/boards", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_board_sort_type(req->getQuery("sort"));
        const auto sub = req->getQuery("sub") == "1";
        const auto boards = self->controller->list_boards(txn, sort, local, sub, m.login, req->getQuery("from"));
        const auto base_url = fmt::format("/boards?local={}&sort={}&sub={}",
          local ? "1" : "0",
          EnumNameBoardSortType(sort),
          sub ? "1" : "0"
        );
        // ---
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
        } else {
          r.write_html_header(m, {
              .canonical_path = "/boards",
              .banner_title = "Boards",
              .banner_link = "/boards",
            })
            .write("<div>")
            .write_sidebar(m.login, m.site)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options("/boards", sort, local, sub)
            .write(R"(</section><main>)");
        }
        r.write_board_list(boards, base_url);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/users", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto local = req->getQuery("local") == "1";
        const auto sort = parse_user_sort_type(req->getQuery("sort"));
        const auto users = self->controller->list_users(txn, sort, local, m.login, req->getQuery("from"));
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
              .banner_title = "Users",
              .banner_link = "/users",
            })
            .write("<div>")
            .write_sidebar(m.login, m.site)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options("/users", sort, local, false)
            .write(R"(</section><main>)");
        }
        r.write_user_list(users, base_url, m.login, !m.is_htmx);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/b/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
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
        const auto list = show_threads
          ? PostList(self->controller->list_board_threads(txn, board_id, sort, m.login, req->getQuery("from")))
          : PostList(self->controller->list_board_comments(txn, board_id, sort, m.login, req->getQuery("from")));
        // ---
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          r.write_html_header(m, {
              .canonical_path = req->getUrl(),
              .banner_title = display_name_as_text(board.board()),
              .banner_link = req->getUrl(),
              .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
              .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(m.login, board)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        std::visit(overload{
          [&](const PageOf<ThreadDetail>& l){r.write_thread_list(l, base_url, m.login, !m.is_htmx, true, false, show_images);},
          [&](const PageOf<CommentDetail>& l){r.write_comment_list(l, base_url, m.login, !m.is_htmx, true, true, show_images);}
        }, list);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/b/:name/create_thread", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto show_url = req->getQuery("text") != "1";
        const auto login = m.require_login(self, txn);
        const auto board = self->controller->board_detail(txn, board_id, m.login);
        // ---
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = fmt::format("/b/{}/create_thread", board.board().name()->string_view()),
            .banner_title = display_name_as_text(board.board()),
            .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
            .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
            .page_title = "Create Thread",
            .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
          })
          .write_create_thread_form(show_url, board, login)
          .write_html_footer(m)
          .finish();
      })
      .get("/u/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
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
        const auto list = show_threads
          ? PostList(self->controller->list_user_threads(txn, user_id, sort, m.login, req->getQuery("from")))
          : PostList(self->controller->list_user_comments(txn, user_id, sort, m.login, req->getQuery("from")));
        // ---
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          r.write_html_header(m, {
              .canonical_path = req->getUrl(),
              .banner_title = display_name_as_text(user.user()),
              .banner_link = req->getUrl(),
              .banner_image = user.user().banner_url() ? optional(fmt::format("/media/user/{}/banner.webp", user.user().name()->string_view())) : nullopt,
              .card_image = user.user().avatar_url() ? optional(fmt::format("/media/user/{}/avatar.webp", user.user().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(m.login, user)
            .write(R"(<section><h2 class="a11y">Sort and filter</h2>)")
            .write_sort_options(req->getUrl(), sort, show_threads, show_images)
            .write(R"(</section><main>)");
        }
        std::visit(overload{
          [&](const PageOf<ThreadDetail>& l){r.write_thread_list(l, base_url, m.login, !m.is_htmx, false, false, show_images);},
          [&](const PageOf<CommentDetail>& l){r.write_comment_list(l, base_url, m.login, !m.is_htmx, false, true, show_images);}
        }, list);
        if (!m.is_htmx) r.write("</main></div>").write_html_footer(m);
        r.finish();
      })
      .get("/thread/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), m.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->thread_detail(txn, id, sort, m.login, req->getQuery("from"));
        const auto board = self->controller->board_detail(txn, detail.thread().board(), m.login);
        // ---
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, m.site, m.login, show_images, true, false);
        } else {
          r.write_html_header(m, {
              .canonical_path = req->getUrl(),
              .banner_title = display_name_as_text(board.board()),
              .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
              .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
              .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(m.login, board)
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
        const auto login = m.require_login(self, txn);
        const auto thread = ThreadDetail::get(txn, id, login);
        if (!thread.can_edit(login)) throw ApiError("Cannot edit this post", 403);
        // ---
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = req->getUrl(),
            .banner_title = display_name_as_text(thread.board()),
            .banner_link = fmt::format("/b/{}", thread.board().name()->string_view()),
            .banner_image = thread.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", thread.board().name()->string_view())) : nullopt,
          })
          .write_edit_thread_form(thread, login)
          .write_html_footer(m)
          .finish();
      })
      .get("/comment/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto id = hex_id_param(req, 0);
        const auto sort = parse_comment_sort_type(req->getQuery("sort"), m.login);
        const auto show_images = req->getQuery("images") == "1" ||
          (req->getQuery("sort").empty() ? !m.login || m.login->local_user().show_images_comments() : false);
        const auto [detail, comments] = self->controller->comment_detail(txn, id, sort, m.login, req->getQuery("from"));
        const auto board = self->controller->board_detail(txn, detail.thread().board(), m.login);
        // ----
        auto r = self->writer(rsp);
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          r.write_comment_tree(comments, detail.id, sort, m.site, m.login, show_images, false, false);
        } else {
          r.write_html_header(m, {
              .canonical_path = req->getUrl(),
              .banner_title = display_name_as_text(board.board()),
              .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
              .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
              .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
            })
            .write("<div>")
            .write_sidebar(m.login, board)
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
            m->populate(self, txn);
            const auto results_detail = self->controller->search_step_2(txn, results, ITEMS_PER_PAGE, m->login);
            self->writer(rsp)
              .write_html_header(*m, {
                .canonical_path = "/search",
                .banner_title = "Search",
              })
              .write("<div>")
              .write_sidebar(m->login, m->site)
              .write("<main>")
              .write_search_result_list(results_detail, m->login, true)
              .write("</main></div>")
              .write_html_footer(*m)
              .finish();
          });
        });
      })
      .get("/create_board", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto& login = m.require_login(self, txn);
        if (!self->controller->can_create_board(login)) {
          throw ApiError("User cannot create boards", 403);
        }
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/create_board",
            .banner_title = "Create Board",
          })
          .write("<main>")
          .write_create_board_form(login)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/login", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
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
            .write_login_form()
            .write_html_footer(m)
            .finish();
        }
      })
      .get("/register", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
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
            .write_register_form()
            .write_html_footer(m)
            .finish();
        }
      })
      .get("/settings", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(self, txn);
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/settings",
            .banner_title = "User Settings",
          })
          .write("<main>")
          .write_user_settings_form(m.site, login)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/b/:name/settings", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto login = m.require_login(self, txn);
        const auto board = self->controller->local_board_detail(txn, board_id, m.login);
        if (!login.local_user().admin() && login.id != board.local_board().owner()) {
          throw ApiError("Must be admin or board owner to view this page", 403);
        }
        // --
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = fmt::format("/b/{}/settings", board.board().name()->string_view()),
            .banner_title = display_name_as_text(board.board()),
            .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
            .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
            .page_title = "Board Settings",
            .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
          })
          .write("<main>")
          .write_board_settings_form(board)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })
      .get("/site_admin", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(self, txn);
        if (!InstanceController::can_change_site_settings(login)) {
          throw ApiError("Admin login required to view this page", 403);
        }
        // --
        self->writer(rsp)
          .write_html_header(m, {
            .canonical_path = "/site_admin",
            .banner_title = "Site Admin",
          })
          .write("<main>")
          .write_site_admin_form(m.site)
          .write("</main>")
          .write_html_footer(m)
          .finish();
      })

      // -----------------------------------------------------------------------
      // API ACTIONS
      // -----------------------------------------------------------------------
      .get("/logout", [self](auto* rsp, auto* req, Meta&) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
        if (req->getHeader("referer").empty()) rsp->writeHeader("Location", "/");
        else rsp->writeHeader("Location", req->getHeader("referer"));
        rsp->end();
      })
      .post_form("/login", [self](auto* rsp, auto* req, auto m) {
        if (m->logged_in_user_id) throw ApiError("Already logged in", 403);
        const auto user_agent = string(req->getHeader("user-agent")),
          referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), user_agent, referer](auto body) {
          if (body.optional_string("username") /* actually a honeypot */) {
            spdlog::warn("Caught a bot with honeypot field on login");
            // just leave the connecting hanging, let the bots time out
            rsp->writeStatus(http_status(418));
            return;
          }
          LoginResponse login;
          bool remember = body.optional_bool("remember");
          try {
            login = self->controller->login(
              body.required_string("actual_username"),
              body.required_string("password"),
              rsp->getRemoteAddressAsText(),
              user_agent,
              remember
            );
          } catch (ApiError e) {
            rsp->writeStatus(http_status(e.http_status));
            m->site = self->controller->site_detail();
            self->writer(rsp)
              .write_html_header(*m, {
                .canonical_path = "/login",
                .banner_title = "Login",
              })
              .write_login_form({e.message})
              .write_html_footer(*m)
              .finish();
            return;
          }
          rsp->writeStatus(http_status(303))
            ->writeHeader("Set-Cookie",
              fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
                login.session_id, fmt::gmtime((time_t)login.expiration)))
            ->writeHeader("Location", referer.empty() || referer == "/login" ? "/" : referer)
            ->end();
        };
      })
      .post_form("/register", [self](auto* rsp, auto* req, auto m) {
        if (m->logged_in_user_id) throw ApiError("Already logged in", 403);
        const auto user_agent = string(req->getHeader("user-agent")),
          referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), user_agent, referer](auto body) {
          if (body.optional_string("username") /* actually a honeypot */) {
            spdlog::warn("Caught a bot with honeypot field on register");
            // just leave the connecting hanging, let the bots time out
            rsp->writeStatus(http_status(418));
            return;
          }
          m->site = self->controller->site_detail();
          try {
            SecretString password = body.required_string("password"),
              confirm_password = body.required_string("confirm_password");
            if (password.str != confirm_password.str) {
              throw ApiError("Passwords do not match", 400);
            }
            self->controller->register_local_user(
              body.required_string("actual_username"),
              body.required_string("email"),
              std::move(password),
              rsp->getRemoteAddressAsText(),
              user_agent,
              body.optional_string("invite").transform(invite_code_to_id),
              body.optional_string("application")
            );
          } catch (ApiError e) {
            rsp->writeStatus(http_status(e.http_status));
            self->writer(rsp)
              .write_html_header(*m, {
                .canonical_path = "/register",
                .banner_title = "Register",
              })
              .write_register_form({e.message})
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
        };
      })
      .post_form("/create_board", [self](auto* rsp, auto*, auto m) {
        const auto user = m->require_login();
        return [self, rsp, m = std::move(m), user](auto body) {
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
          rsp->writeStatus(http_status(303));
          m->write_cookie(rsp);
          rsp->writeHeader("Location", fmt::format("/b/{}", name))
            ->end();
        };
      })
      .post_form("/b/:name/create_thread", [self](auto* rsp, auto* req, auto m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto user = m->require_login();
        return [self, rsp, m = std::move(m), user, board_id](auto body) {
          const auto id = self->controller->create_local_thread(
            user,
            board_id,
            body.required_string("title"),
            body.optional_string("submission_url"),
            body.optional_string("text_content"),
            body.optional_string("content_warning")
          );
          rsp->writeStatus(http_status(303));
          m->write_cookie(rsp);
          rsp->writeHeader("Location", fmt::format("/thread/{:x}", id))
            ->end();
        };
      })
      .post_form("/thread/:id/reply", [self](auto* rsp, auto* req, auto m) {
        const auto user = m->require_login();
        const auto thread_id = hex_id_param(req, 0);
        return [self, rsp, m = std::move(m), user, thread_id](auto body) {
          const auto id = self->controller->create_local_comment(
            user,
            thread_id,
            body.required_string("text_content"),
            body.optional_string("content_warning")
          );
          if (m->is_htmx) {
            auto txn = self->controller->open_read_txn();
            m->populate(self, txn);
            const auto comment = CommentDetail::get(txn, id, m->login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->writer(rsp)
              .write_comment_entry(comment, m->login, false, true, true, false, true)
              .write_toast("Reply submitted")
              .finish();
          } else {
            rsp->writeStatus(http_status(303));
            m->write_cookie(rsp);
            rsp->writeHeader("Location", fmt::format("/thread/{:x}", thread_id))
              ->end();
          }
        };
      })
      .post_form("/comment/:id/reply", [self](auto* rsp, auto* req, auto m) {
        const auto user = m->require_login();
        const auto comment_id = hex_id_param(req, 0);
        return [self, rsp, m = std::move(m), user, comment_id](auto body) {
          const auto id = self->controller->create_local_comment(
            user,
            comment_id,
            body.required_string("text_content"),
            body.optional_string("content_warning")
          );
          if (m->is_htmx) {
            auto txn = self->controller->open_read_txn();
            m->populate(self, txn);
            const auto comment = CommentDetail::get(txn, id, m->login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->writer(rsp)
              .write_comment_entry(comment, m->login, false, true, true, false, true)
              .write_toast("Reply submitted")
              .finish();
          } else {
            rsp->writeStatus(http_status(303));
            m->write_cookie(rsp);
            rsp->writeHeader("Location", fmt::format("/comment/{:x}", comment_id))
              ->end();
          }
        };
      })
      .post_form("/thread/:id/action", [self](auto* rsp, auto* req, auto m) {
        const auto id = hex_id_param(req, 0);
        const auto user = m->require_login();
        const auto referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), id, user, referer](auto body) {
          const auto action = static_cast<SubmenuAction>(body.required_int("action"));
          switch (action) {
            case SubmenuAction::Reply:
              write_redirect_to(rsp, *m, fmt::format("/thread/{:x}#reply", id));
              return;
            case SubmenuAction::Edit:
              write_redirect_to(rsp, *m, fmt::format("/thread/{:x}/edit", id));
              return;
            case SubmenuAction::Delete:
              throw ApiError("Delete is not yet implemented", 500);
            case SubmenuAction::Share:
              throw ApiError("Share is not yet implemented", 500);
            case SubmenuAction::Save:
              self->controller->save_post(user, id, true);
              break;
            case SubmenuAction::Unsave:
              self->controller->save_post(user, id, false);
              break;
            case SubmenuAction::Hide:
              self->controller->hide_post(user, id, true);
              break;
            case SubmenuAction::Unhide:
              self->controller->hide_post(user, id, false);
              break;
            case SubmenuAction::Report:
              throw ApiError("Report is not yet implemented", 500);
            case SubmenuAction::MuteUser: {
              auto txn = self->controller->open_read_txn();
              const auto thread = txn.get_thread(id);
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_user(user, thread->get().author(), true);
              break;
            }
            case SubmenuAction::UnmuteUser: {
              auto txn = self->controller->open_read_txn();
              const auto thread = txn.get_thread(id);
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_user(user, thread->get().author(), false);
              break;
            }
            case SubmenuAction::MuteBoard: {
              auto txn = self->controller->open_read_txn();
              const auto thread = txn.get_thread(id);
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_board(user, thread->get().board(), true);
              break;
            }
            case SubmenuAction::UnmuteBoard:{
              auto txn = self->controller->open_read_txn();
              const auto thread = txn.get_thread(id);
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_board(user, thread->get().board(), false);
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
          if (m->is_htmx) {
            const auto show_user = body.optional_bool("show_user"),
              show_board = body.optional_bool("show_board");
            auto txn = self->controller->open_read_txn();
            const auto login = LocalUserDetail::get(txn, user);
            const auto thread = ThreadDetail::get(txn, id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->writer(rsp)
              .write_controls_submenu(thread, login, show_user, show_board)
              .finish();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      })
      .post_form("/comment/:id/action", [self](auto* rsp, auto* req, auto m) {
        const auto id = hex_id_param(req, 0);
        const auto user = m->require_login();
        const auto referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), id, user, referer](auto body) {
          const auto action = static_cast<SubmenuAction>(body.required_int("action"));
          switch (action) {
            case SubmenuAction::Reply:
              write_redirect_to(rsp, *m, fmt::format("/comment/{:x}#reply", id));
              return;
            case SubmenuAction::Edit:
              write_redirect_to(rsp, *m, fmt::format("/comment/{:x}/edit", id));
              return;
            case SubmenuAction::Delete:
              throw ApiError("Delete is not yet implemented", 500);
            case SubmenuAction::Share:
              throw ApiError("Share is not yet implemented", 500);
            case SubmenuAction::Save:
              self->controller->save_post(user, id, true);
              break;
            case SubmenuAction::Unsave:
              self->controller->save_post(user, id, false);
              break;
            case SubmenuAction::Hide:
              self->controller->hide_post(user, id, true);
              break;
            case SubmenuAction::Unhide:
              self->controller->hide_post(user, id, false);
              break;
            case SubmenuAction::Report:
              throw ApiError("Report is not yet implemented", 500);
            case SubmenuAction::MuteUser: {
              auto txn = self->controller->open_read_txn();
              const auto comment = txn.get_comment(id);
              if (!comment) throw ApiError("Comment does not exist", 404);
              self->controller->hide_user(user, comment->get().author(), true);
              break;
            }
            case SubmenuAction::UnmuteUser: {
              auto txn = self->controller->open_read_txn();
              const auto comment = txn.get_comment(id);
              if (!comment) throw ApiError("Comment does not exist", 404);
              self->controller->hide_user(user, comment->get().author(), false);
              break;
            }
            case SubmenuAction::MuteBoard: {
              auto txn = self->controller->open_read_txn();
              const auto comment = txn.get_comment(id);
              if (!comment) throw ApiError("Comment does not exist", 404);
              const auto thread = txn.get_thread(comment->get().thread());
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_board(user, thread->get().board(), true);
              break;
            }
            case SubmenuAction::UnmuteBoard:{
              auto txn = self->controller->open_read_txn();
              const auto comment = txn.get_comment(id);
              if (!comment) throw ApiError("Comment does not exist", 404);
              const auto thread = txn.get_thread(comment->get().thread());
              if (!thread) throw ApiError("Thread does not exist", 404);
              self->controller->hide_board(user, thread->get().board(), false);
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
          if (m->is_htmx) {
            const auto show_user = body.optional_bool("show_user"),
              show_board = body.optional_bool("show_board");
            auto txn = self->controller->open_read_txn();
            const auto login = LocalUserDetail::get(txn, user);
            const auto comment = CommentDetail::get(txn, id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->writer(rsp)
              .write_controls_submenu(comment, login, show_user, show_board)
              .finish();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      })
      .post_form("/thread/:id/vote", [self](auto* rsp, auto* req, auto m) {
        const auto user = m->require_login();
        const auto post_id = hex_id_param(req, 0);
        const auto referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), user, post_id, referer](auto body) {
          const auto vote = body.required_vote("vote");
          self->controller->vote(user, post_id, vote);
          if (m->is_htmx) {
            auto txn = self->controller->open_read_txn();
            const auto login = LocalUserDetail::get(txn, user);
            const auto thread = ThreadDetail::get(txn, post_id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            self->writer(rsp).write_vote_buttons(thread, login).finish();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      })
      .post_form("/comment/:id/vote", [self](auto* rsp, auto* req, auto m) {
        const auto user = m->require_login();
        const auto post_id = hex_id_param(req, 0);
        const auto referer = string(req->getHeader("referer"));
        return [self, rsp, m = std::move(m), user, post_id, referer](auto body) {
          const auto vote = body.required_vote("vote");
          self->controller->vote(user, post_id, vote);
          if (m->is_htmx) {
            auto txn = self->controller->open_read_txn();
            const auto login = LocalUserDetail::get(txn, user);
            const auto comment = CommentDetail::get(txn, post_id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            self->writer(rsp).write_vote_buttons(comment, login).finish();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      })
      .post_form("/b/:name/subscribe", [self](auto* rsp, auto* req, auto m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto name = string(req->getParameter(0));
        const auto user = m->require_login();
        const auto referer = string(req->getHeader("referer"));
        return [self, rsp, board_id, name, m = std::move(m), user, referer](QueryString body) {
          self->controller->subscribe(user, board_id, !body.optional_bool("unsubscribe"));
          if (m->is_htmx) {
            rsp->writeHeader("Content-Type", TYPE_HTML);
            self->writer(rsp).write_subscribe_button(name, !body.optional_bool("unsubscribe")).finish();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      });
    }
  };

  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<RichTextParser> rt,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void {
    auto router = std::make_shared<Webapp<SSL>>(controller, rt, rl);
    router->register_routes(app);
  }

  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<RichTextParser> rt,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<InstanceController> controller,
    shared_ptr<RichTextParser> rt,
    shared_ptr<KeyedRateLimiter> rl
  ) -> void;
}
