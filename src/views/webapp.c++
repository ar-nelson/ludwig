#include "views/webapp.h++"
#include "util/web.h++"
#include "static/default-theme.css.h++"
#include "static/htmx.min.js.h++"
#include "static/feather-sprite.svg.h++"
#include <iterator>
#include <regex>
#include <spdlog/fmt/chrono.h>
#include "xxhash.h"

using std::bind, std::match_results, std::nullopt, std::optional, std::regex,
    std::regex_search, std::shared_ptr, std::stoull, std::string,
    std::string_view, std::to_string;

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

  static inline auto display_name(const User& user) -> string_view {
    if (user.display_name()) return user.display_name()->string_view();
    const auto name = user.name()->string_view();
    return name.substr(0, name.find('@'));
  }

  static inline auto display_name(const Board& board) -> string_view {
    if (board.display_name()) return board.display_name()->string_view();
    const auto name = board.name()->string_view();
    return name.substr(0, name.find('@'));
  }

  static inline auto mod_state(const ThreadListEntry& thread) -> ModState {
    return thread.thread().mod_state();
  }

  static inline auto mod_state(const CommentListEntry& comment) -> ModState {
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
  template <> constexpr auto post_word<ThreadDetailResponse>() -> string_view { return "thread"; }
  template <> constexpr auto post_word<CommentDetailResponse>() -> string_view { return "comment"; }
  template <> constexpr auto post_word<ThreadListEntry>() -> string_view { return "thread"; }
  template <> constexpr auto post_word<CommentListEntry>() -> string_view { return "comment"; }

  template <bool SSL> struct Webapp : public std::enable_shared_from_this<Webapp<SSL>> {
    shared_ptr<InstanceController> controller;
    string buf;

    Webapp(shared_ptr<InstanceController> controller) : controller(controller) {}

    using App = uWS::TemplatedApp<SSL>;
    using Response = uWS::HttpResponse<SSL>*;
    using Request = uWS::HttpRequest*;

    template <typename... Args> inline auto write_fmt(Response rsp, fmt::format_string<Args...> fmt, Args&&... args) noexcept -> void {
      buf.clear();
      fmt::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
      rsp->write(buf);
    }

    struct ErrorMeta {
      bool is_htmx;
    };

    auto error_middleware(const uWS::HttpResponse<SSL>*, Request req) noexcept -> ErrorMeta {
      return {
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty()
      };
    }

    auto error_page(Response rsp, const ApiError& e, const ErrorMeta& m) noexcept -> void {
      if (m.is_htmx) {
        rsp->writeStatus(http_status(200))
          ->writeHeader("Content-Type", TYPE_HTML)
          ->writeHeader("HX-Retarget", "#toasts")
          ->writeHeader("HX-Reswap", "afterbegin");
        write_toast(rsp, e.message, " toast-error");
      } else if (e.http_status == 401) {
        rsp->writeStatus(http_status(303))
          ->writeHeader("Location", "/login");
      } else {
        rsp->writeStatus(http_status(e.http_status))
          ->writeHeader("Content-Type", TYPE_HTML);
        write_fmt(rsp, "Error {:d}: {}", e.http_status, Escape(e.message));
      }
      rsp->end();
    }

    struct Meta {
      chrono::time_point<chrono::steady_clock> start;
      optional<uint64_t> logged_in_user_id;
      optional<string> session_cookie;
      bool is_htmx;
      const SiteDetail* site;
      optional<const LocalUserDetailResponse> login;

      inline auto populate(shared_ptr<Webapp<SSL>> self, ReadTxnBase& txn) {
        site = self->controller->site_detail();
        if (logged_in_user_id) {
          login.emplace(self->controller->local_user_detail(txn, *logged_in_user_id));
        }
      }

      inline auto require_login() {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        return *logged_in_user_id;
      }

      inline auto require_login(shared_ptr<Webapp<SSL>> self, ReadTxnBase& txn) {
        if (!logged_in_user_id) throw ApiError("Login is required", 401);
        if (site == nullptr) populate(self, txn);
        return *login;
      }

      inline auto write_cookie(Response rsp) const noexcept {
        if (session_cookie) rsp->writeHeader("Set-Cookie", *session_cookie);
      }

      inline auto time_elapsed() const noexcept {
        const auto end = chrono::steady_clock::now();
        return chrono::duration_cast<chrono::microseconds>(end - start).count();
      }
    };

    auto middleware(Response rsp, Request req) -> Meta {
      const auto start = chrono::steady_clock::now();
      optional<string> session_cookie;
      optional<LoginResponse> new_session;
      const auto cookies = req->getHeader("cookie");
      match_results<string_view::const_iterator> match;
      if (regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) {
        try {
          auto txn = controller->open_read_txn();
          const auto old_session = stoull(match[1], nullptr, 16);
          new_session = controller->validate_or_regenerate_session(
            txn, old_session, rsp->getRemoteAddressAsText(), req->getHeader("user-agent")
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
        .logged_in_user_id = new_session.transform([](auto r){return r.user_id;}),
        .session_cookie = session_cookie,
        .is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty(),
      };
    }

    inline auto write_toast(Response rsp, string_view content, string_view extra_classes = "") {
      write_fmt(rsp,
        R"(<div hx-swap-oob="afterbegin:#toasts">)"
        R"(<p class="toast{}" aria-live="polite" hx-get="data:text/html," hx-trigger="click, every 30s" hx-swap="delete">{}</p>)"
        "</div>",
        extra_classes, Escape{content}
      );
    }

    inline auto write_qualified_display_name(Response rsp, const User* user) -> void {
      const auto name = user->name()->string_view();
      if (user->display_name()) {
        rsp << user->display_name()->string_view();
        const auto at_index = name.find('@');
        if (at_index != string_view::npos) rsp << name.substr(at_index);
      } else {
        rsp << name;
      }
    }

    inline auto write_qualified_display_name(Response rsp, const Board* board) -> void {
      const auto name = board->name()->string_view();
      if (board->display_name()) {
        rsp << board->display_name()->string_view();
        const auto at_index = name.find('@');
        if (at_index != string_view::npos) rsp << name.substr(at_index);
      } else {
        rsp << name;
      }
    }

    struct HtmlHeaderOptions {
      optional<string_view> canonical_path, banner_title, banner_link, banner_image, page_title, card_image;
    };

    auto write_html_header(Response rsp, const Meta& m, HtmlHeaderOptions opt) noexcept -> void {
      assert(m.site != nullptr);
      rsp->writeHeader("Content-Type", TYPE_HTML);
      m.write_cookie(rsp);
      write_fmt(rsp,
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
        write_fmt(rsp,
          R"(<link rel="canonical" href="{0}{1}">)"
          R"(<meta property="og:url" content="{0}{1}">)"
          R"(<meta property="twitter:url" content="{0}{1}">)",
          Escape{m.site->domain}, Escape{*opt.canonical_path}
        );
      }
      if (opt.page_title) {
        write_fmt(rsp,
          R"(<meta property="title" href="{0} - {1}">)"
          R"(<meta property="og:title" content="{0} - {1}">)"
          R"(<meta property="twitter:title" content="{0} - {1}">)"
          R"(<meta property="og:type" content="website">)",
          Escape{m.site->domain}, Escape{*opt.page_title}
        );
      }
      if (opt.card_image) {
        write_fmt(rsp,
          R"(<meta property="og:image" content="{0}">)"
          R"(<meta property="twitter:image" content="{0}>)"
          R"(<meta property="twitter:card" content="summary_large_image">)",
          Escape{*opt.card_image}
        );
      }
      write_fmt(rsp,
        R"(</head><body><script>document.body.classList.add("has-js")</script>)"
        R"(<nav class="topbar"><div class="site-name">🎹 {}</div><ul class="quick-boards">)"
        R"(<li><a href="/">Home</a>)"
        R"(<li><a href="/feed/local">Local</a>)"
        R"(<li><a href="/feed/federated">All</a>)"
        R"(<li><a href="/boards">Boards</a>)",
        Escape{m.site->name}
      );
      if (m.login) {
        write_fmt(rsp,
          R"(<li><a href="/subscriptions">Subscriptions</a></ul><ul>)"
          R"(<li id="topbar-user"><a href="/u/{}">{}</a> ({:d}))"
          R"(<li><a href="/settings">Settings</a>{}<li><a href="/logout">Logout</a></ul></nav>)",
          Escape(m.login->user().name()),
          Escape{display_name(m.login->user())},
          m.login->stats().thread_karma() + m.login->stats().comment_karma(),
          InstanceController::can_change_site_settings(m.login) ? R"(<li><a href="/site_admin">Site admin</a>)" : ""
        );
      } else {
        rsp->write(R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)");
      }
      rsp->write(R"(<div id="toasts"></div>)");
      if (opt.banner_title) {
        rsp->write(R"(<header id="page-header")");
        if (opt.banner_image) {
          write_fmt(rsp, R"( class="banner-image" style="background-image:url('{}');")", Escape{*opt.banner_image});
        }
        if (opt.banner_link) {
          write_fmt(rsp,
            R"(><h1><a class="page-header-link" href="{}">{}</a></h1></header>)",
            Escape{*opt.banner_link}, Escape{*opt.banner_title}
          );
        } else {
          write_fmt(rsp, "><h1>{}</h1></header>", Escape{*opt.banner_title});
        }
      }
    }

    static inline auto end_with_html_footer(Response rsp, const Meta& m) noexcept -> void {
      rsp->end(fmt::format(
        R"(<div class="spacer"></div><footer><small>Powered by Ludwig · Generated in {:L}μs</small></footer></body></html>)",
        m.time_elapsed()
      ));
    }

#   define HTML_FIELD(ID, LABEL, TYPE, EXTRA) \
      "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"" TYPE "\" name=\"" ID "\" id=\"" ID "\"" EXTRA "></label>"
#   define HTML_CHECKBOX(ID, LABEL, EXTRA) \
      "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"checkbox\" class=\"a11y\" name=\"" ID "\" id=\"" ID "\"" EXTRA "><div class=\"toggle-switch\"></div></label>"
#   define HTML_TEXTAREA(ID, LABEL, EXTRA, CONTENT) \
      "<label for=\"" ID "\"><span>" LABEL "</span><div><textarea name=\"" ID "\" id=\"" ID "\"" EXTRA ">" CONTENT \
      R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"

    inline auto write_subscribe_button(
      Response rsp,
      std::string_view name,
      bool is_unsubscribe
    ) noexcept -> void {
      write_fmt(rsp,
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
      Response rsp,
      const Meta& m,
      const optional<BoardDetailResponse>& board = {}
    ) noexcept -> void {
      rsp->write(
        R"(<label id="sidebar-toggle-label" for="sidebar-toggle"><svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#menu"></svg> Menu</label>)"
        R"(<input type="checkbox" name="sidebar-toggle" id="sidebar-toggle" class="a11y">)"
        R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
        R"(<form action="/search" id="search-form">)"
        R"(<label for="search"><span class="a11y">Search</span>)"
        R"(<input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)"
      );
      const auto hide_cw = m.login && m.login->local_user().hide_cw_posts();
      const auto board_name = Escape{board ? display_name(board->board()) : ""};
      if (board) write_fmt(rsp, R"(<input type="hidden" name="board" value="{:x}">)", board->id);
      if (!hide_cw || board) {
        rsp->write(R"(<details id="search-options"><summary>Search Options</summary><fieldset>)");
        if (board) {
          write_fmt(rsp,
            R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to {}</label>)",
            board_name
          );
        }
        if (!hide_cw) {
          rsp->write(R"(<label for="include_cw"><input type="checkbox" name="include_cw" id="include_cw" checked> Include results with Content Warnings</label>)");
        }
        rsp->write("</fieldset></details>");
      }
      rsp->write("</form></section>");
      if (!m.login) {
        write_fmt(rsp,
          R"(<section id="login-section"><h2>Login</h2><form method="post" action="/login" id="login-form">{})"
          R"(<label for="actual_username"><span class="a11y">Username or email</span><input type="text" name="actual_username" id="actual_username" placeholder="Username or email"></label>)"
          R"(<label for="password"><span class="a11y">Password</span><input type="password" name="password" id="password" placeholder="Password"></label>)"
          R"(<label for="remember"><input type="checkbox" name="remember" id="remember"> Remember me</label>)"
          R"(<input type="submit" value="Login" class="big-button"></form>)"
          R"(<a href="/register" class="big-button">Register</a></section>)",
          HONEYPOT_FIELD
        );
      } else if (board) {
        rsp->write(R"(<section id="actions-section"><h2>Actions</h2>)");
        write_subscribe_button(rsp, board->board().name()->string_view(), board->subscribed);
        if (InstanceController::can_create_thread(*board, m.login)) {
          write_fmt(rsp,
            R"(<a class="big-button" href="/b/{0}/create_thread">Submit a new link</a>)"
            R"(<a class="big-button" href="/b/{0}/create_thread?text=1">Submit a new text post</a>)",
            Escape(board->board().name())
          );
        }
        // TODO: Board settings link
        rsp->write("</section>");
      }
      if (board) {
        write_fmt(rsp, R"(<section id="board-sidebar"><h2>{}</h2>)", board_name);
        // TODO: Banner image
        if (board->board().description_safe()) {
          write_fmt(rsp, "<p>{}</p>", board->board().description_safe()->string_view());
        }
        rsp->write("</section>");
        // TODO: Board stats
        // TODO: Modlog link
      } else {
        write_fmt(rsp, R"(<section id="site-sidebar"><h2>{}</h2>)", Escape{m.site->name});
        if (m.site->banner_url) {
          write_fmt(rsp,
            R"(<div class="sidebar-banner"><img src="{}" alt="{} banner"></div>)",
            Escape{*m.site->banner_url}, Escape{m.site->name}
          );
        }
        write_fmt(rsp, "<p>{}</p></section>", Escape{m.site->description});
        // TODO: Site stats
        // TODO: Modlog link
      }
      rsp->write("</aside>");
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

    auto write_datetime(Response rsp, uint64_t timestamp) noexcept -> void {
      write_fmt(rsp, R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)",
        fmt::gmtime((time_t)timestamp), fmt::localtime((time_t)timestamp), relative_time(timestamp));
    }

    auto write_user_link(Response rsp, OptRef<User> user_opt) noexcept -> void {
      if (!user_opt) {
        rsp->write("<em>[deleted]</em>");
        return;
      }
      const auto& user = user_opt->get();
      write_fmt(rsp, R"(<a class="user-link" href="/u/{}">)", Escape(user.name()));
      if (user.avatar_url()) {
        write_fmt(rsp, R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/user/{}/avatar.webp">)",
          Escape{user.name()}
        );
      } else {
        rsp->write(R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#user"></svg>)");
      }
      auto name = user.name()->str();
      write_fmt(rsp, "{}", Escape{
        user.display_name() == nullptr
          ? name.substr(0, name.find('@'))
          : user.display_name()->string_view()
        }
      );
      if (user.instance()) {
        const auto suffix_ix = name.find('@');
        if (suffix_ix != string::npos) {
          write_fmt(rsp, R"(<span class="at-domain">@{}</span>)", Escape{name.substr(suffix_ix + 1)});
        }
      }
      rsp->write("</a>");
    }

    auto write_board_link(Response rsp, OptRef<Board> board_opt) noexcept -> void {
      if (!board_opt) {
        rsp->write("<em>[deleted]</em>");
        return;
      }
      const auto& board = board_opt->get();
      write_fmt(rsp, R"(<a class="board-link" href="/b/{}">)", Escape(board.name()));
      if (board.icon_url()) {
        write_fmt(rsp, R"(<img aria-hidden="true" class="avatar" loading="lazy" src="/media/board/{}/icon.webp">)",
          Escape(board.name())
        );
      } else {
        rsp->write(R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#book"></svg>)");
      }
      auto name = board.name()->str();
      write_fmt(rsp, "{}", Escape{
        board.display_name() == nullptr
          ? name.substr(0, name.find('@'))
          : board.display_name()->string_view()
        }
      );
      if (board.instance()) {
        const auto suffix_ix = name.find('@');
        if (suffix_ix != string::npos) {
          write_fmt(rsp, R"(<span class="at-domain">@{}</span>)", Escape{name.substr(suffix_ix + 1)});
        }
      }
      rsp->write("</a>");
      if (board.content_warning()) {
        write_fmt(rsp, R"(<abbr class="content-warning-label" title="Content Warning: {}">CW</abbr>)",
          Escape(board.content_warning())
        );
      }
    }

    auto write_board_list(
      Response rsp,
      const PageOf<BoardListEntry>& list
    ) noexcept -> void {
      // TODO: Pagination
      rsp->write(R"(<ol class="board-list" id="top-level-list">)");
      for (auto& entry : list.entries) {
        rsp->write(R"(<li class="board-list-entry"><h2 class="board-title">)");
        write_board_link(rsp, entry._board);
        rsp->write("</h2></li>");
      }
      rsp->write("</ol>");
    }

    auto write_sort_options(
      Response rsp,
      SortFormType type,
      string_view base_url,
      string_view sort_name,
      bool show_threads,
      bool show_images,
      string_view hx_target = "#top-level-list"
    ) noexcept -> void {
      write_fmt(rsp,
        R"(<form class="sort-form" method="get" action="{0}" hx-get="{0}" hx-trigger="change" hx-target="{1}" hx-push-url="true">)",
        Escape{base_url}, Escape{hx_target}
      );
      if (type != SortFormType::Comments) {
        write_fmt(rsp,
          R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
          R"(<input class="a11y" name="type" type="radio" value="threads" id="type-threads"{}><label for="type-threads" class="toggle-button">Threads</label>)"
          R"(<input class="a11y" name="type" type="radio" value="comments" id="type-comments"{}><label for="type-comments" class="toggle-button">Comments</label></fieldset>)",
          show_threads ? " checked" : "", show_threads ? "" : " checked"
        );
      }
      rsp->write(R"(<label for="sort"><span class="a11y">Sort</span><select name="sort">)");
      if (type == SortFormType::Board) {
        write_fmt(rsp, R"(<option value="Active"{}>Active)", sort_name == "Active" ? " selected" : "");
      }
      if (type != SortFormType::User) {
        write_fmt(rsp, R"(<option value="Hot"{}>Hot)", sort_name == "Hot" ? " selected" : "");
      }
      write_fmt(rsp,
        R"(<option value="New"{}>New)"
        R"(<option value="Old"{}>Old)",
        sort_name == "New" ? " selected" : "",
        sort_name == "Old" ? " selected" : ""
      );
      if (type == SortFormType::Board) {
        write_fmt(rsp,
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
          R"(<option value="TopHour"{}>Top Hour)",
          sort_name == "MostComments" ? " selected" : "",
          sort_name == "NewComments" ? " selected" : "",
          sort_name == "TopAll" ? " selected" : "",
          sort_name == "TopYear" ? " selected" : "",
          sort_name == "TopSixMonths" ? " selected" : "",
          sort_name == "TopThreeMonths" ? " selected" : "",
          sort_name == "TopMonth" ? " selected" : "",
          sort_name == "TopWeek" ? " selected" : "",
          sort_name == "TopDay" ? " selected" : "",
          sort_name == "TopTwelveHour" ? " selected" : "",
          sort_name == "TopSixHour" ? " selected" : "",
          sort_name == "TopHour" ? " selected" : ""
        );
      } else {
        write_fmt(rsp, R"(<option value="Top"{}>Top)", sort_name == "Top" ? " selected" : "");
      }
      write_fmt(rsp,
        R"(</select></label><label for="images"><input class="a11y" name="images" id="images" type="checkbox" value="1"{}><div class="toggle-switch"></div> Images</label>)"
        R"(<input class="no-js" type="submit" value="Apply"></form>)",
        show_images ? " checked" : ""
      );
    }

    template <typename T> auto write_vote_buttons(
      Response rsp,
      const T& entry,
      InstanceController::Login login
    ) noexcept -> void {
      const auto can_upvote = InstanceController::can_upvote(entry, login),
        can_downvote = InstanceController::can_downvote(entry, login);
      if (can_upvote || can_downvote) {
        write_fmt(rsp,
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
        write_fmt(rsp,
          R"(<div class="vote-buttons" id="votes-{0:x}"><output class="karma" id="karma-{0:x}">{1}</output>)"
          R"(<div class="upvote"><button type="button" disabled><span class="a11y">Upvote</span></button></div>)"
          R"(<div class="downvote"><button type="button" disabled><span class="a11y">Downvote</span></button></div></div>)",
          entry.id, suffixed_short_number(entry.stats().karma())
        );
      }
    }

    auto write_pagination(
      Response rsp,
      string_view base_url,
      bool is_first,
      optional<uint64_t> next,
      bool infinite_scroll_enabled = true
    ) noexcept -> void {
      const auto sep = base_url.find('?') == string_view::npos ? "?" : "&amp;";
      rsp->write(R"(<div class="pagination" id="pagination" hx-swap-oob="true">)");
      if (!is_first) {
        write_fmt(rsp, R"(<a class="big-button no-js" href="{}">← First</a>)", Escape{base_url});
      }
      if (next) {
        write_fmt(rsp,
          R"(<a class="big-button no-js" href="{0}{1}from={2:x}">Next →</a>)"
          R"(<a class="more-link js" href="{0}{1}from={2:x}" hx-get="{0}{1}from={2:x}" hx-target="#top-level-list" hx-swap="beforeend"{3}>Load more…</a>)",
          Escape{base_url}, sep, *next,
          infinite_scroll_enabled ? R"( hx-trigger="revealed")" : ""
        );
      }
      rsp->write(R"(<div class="spinner">Loading…</div></div>)");
    }

    template <typename T> auto write_controls_submenu(
      Response rsp,
      const T& post,
      InstanceController::Login login,
      bool show_user,
      bool show_board
    ) noexcept -> void {
      if (!login) return;
      write_fmt(rsp,
        R"(<form class="controls-submenu" id="controls-submenu-{0:x}" method="post" action="/{1}/{0:x}/action">)"
        R"(<input type="hidden" name="show_user" value="{2:d}"><input type="hidden" name="show_board" value="{3:d}">)"
        R"(<label for="action"><span class="a11y">Action</span><svg class="icon"><use href="/static/feather-sprite.svg#chevron-down"></svg>)"
        R"(<select name="action" autocomplete="off" hx-post="/{1}/{0:x}/action" hx-trigger="change" hx-target="#controls-submenu-{0:x}">)"
        R"(<option selected hidden value="{4:d}">Actions)",
        post.id, post_word<T>(), show_user, show_board, SubmenuAction::None
      );
      if (InstanceController::can_reply_to(post, login)) {
        write_fmt(rsp, R"(<option value="{:d}">💬 Reply)", SubmenuAction::Reply);
      }
      if (InstanceController::can_edit(post, login)) {
        write_fmt(rsp, R"(<option value="{:d}">✏️ Edit)", SubmenuAction::Edit);
      }
      if (InstanceController::can_delete(post, login)) {
        write_fmt(rsp, R"(<option value="{:d}">🗑️ Delete)", SubmenuAction::Delete);
      }
      write_fmt(rsp,
        R"(<option value="{:d}">{})"
        R"(<option value="{:d}">{})",
        post.saved ? SubmenuAction::Unsave : SubmenuAction::Save, post.saved ? "🚫 Unsave" : "🔖 Save",
        post.hidden ? SubmenuAction::Unhide : SubmenuAction::Hide, post.hidden ? "🔈 Unhide" : "🔇 Hide"
      );
      if (show_user) {
        write_fmt(rsp, R"(<option value="{:d}">{})",
          post.user_hidden ? SubmenuAction::UnmuteUser : SubmenuAction::MuteUser,
          post.user_hidden ? "🔈 Unmute user" : "🔇 Mute user"
        );
      }
      if (show_board) {
        write_fmt(rsp, R"(<option value="{:d}">{})",
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
        write_fmt(rsp,
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
      rsp->write(R"(</select></label><button class="no-js" type="submit">Apply</button></form>)");
    }

    template <typename T> auto write_warnings(Response rsp, const T& thing, string_view prefix = "") noexcept -> void {
      if (thing.mod_state() > ModState::Visible) {
        if (thing.mod_reason()) {
          write_content_warning(rsp,
            fmt::format("{} by Moderator", describe_mod_state(thing.mod_state())),
            true,
            thing.mod_reason()->string_view(),
            prefix
          );
        } else {
          write_fmt(rsp,
            R"(<p class="content-warning"><span class="mod-warning-label">{}{} by Moderator</span></p>)",
            prefix, describe_mod_state(thing.mod_state())
          );
        }
      }
      if (thing.content_warning()) {
        write_content_warning(rsp, "Content Warning", false, thing.content_warning()->string_view(), prefix);
      }
    }

    template <typename T> auto write_short_warnings(Response rsp, const T& thing) noexcept -> void {
      if (thing.mod_state() > ModState::Visible) {
        write_fmt(rsp, R"( <abbr class="mod-warning-label" title="{0} by Moderator: {1}">{0}</abbr>)",
          describe_mod_state(thing.mod_state()), Escape(thing.mod_reason()));
      }
      if (thing.content_warning()) {
        write_fmt(rsp, R"( <abbr class="content-warning-label" title="Content Warning: {}">CW</abbr>)",
          Escape(thing.content_warning()));
      }
    }

    auto write_thread_entry(
      Response rsp,
      const ThreadListEntry& thread,
      InstanceController::Login login,
      bool is_list_item,
      bool show_user,
      bool show_board,
      bool show_images
    ) noexcept -> void {
      // TODO: thread-source (link URL)
      // TODO: thumbnail
      write_fmt(rsp,
        R"({} class="thread" id="thread-{:x}"><h2 class="thread-title">)",
        is_list_item ? "<li><article" : "<div",
        thread.id
      );
      if (is_list_item || thread.thread().content_url()) {
        write_fmt(rsp, R"(<a class="thread-title-link" href="{}">{}</a></h2>)",
          Escape{
            thread.thread().content_url()
              ? thread.thread().content_url()->string_view()
              : fmt::format("/thread/{:x}", thread.id)
          },
          Escape(thread.thread().title())
        );
      } else {
        write_fmt(rsp, "{}</h2>", Escape(thread.thread().title()));
      }
      write_fmt(rsp,
        R"(<div class="thumbnail"><svg class="icon"><use href="/static/feather-sprite.svg#{}"></svg></div>)",
        thread.thread().content_warning() ? "alert-octagon" : (thread.thread().content_url() ? "link" : "file-text")
      );
      if (thread.thread().content_warning() || thread.thread().mod_state() > ModState::Visible) {
        rsp->write(R"(<div class="thread-warnings">)");
        if (!is_list_item && thread.thread().content_text_safe()) write_short_warnings(rsp, thread.thread());
        else write_warnings(rsp, thread.thread());
        rsp->write(R"(</div>)");
      }
      rsp->write(R"(<div class="thread-info"><span>submitted )");
      write_datetime(rsp, thread.thread().created_at());
      if (show_user) {
        rsp->write("</span><span>by ");
        write_user_link(rsp, thread._author);
      }
      if (show_board) {
        rsp->write("</span><span>to ");
        write_board_link(rsp, thread._board);
      }
      rsp->write("</span></div>");
      write_vote_buttons(rsp, thread, login);
      if (is_list_item) {
        write_fmt(rsp, R"(<div class="controls"><a id="comment-link-{0:x}" href="/thread/{0:x}#comments">{1:d}{2}</a>)",
          thread.id,
          thread.stats().descendant_count(),
          thread.stats().descendant_count() == 1 ? " comment" : " comments"
        );
      } else {
        rsp->write(R"(<div class="controls"><span></span>)");
      }
      write_controls_submenu(rsp, thread, login, show_user, show_board);
      rsp->write(is_list_item ? "</div></article>" : "</div></div>");
    }

    auto write_thread_list(
      Response rsp,
      const PageOf<ThreadListEntry>& list,
      string_view base_url,
      InstanceController::Login login,
      bool include_ol,
      bool show_user,
      bool show_board,
      bool show_images
    ) noexcept -> void {
      if (include_ol) rsp->write(R"(<ol class="thread-list" id="top-level-list">)");
      for (const auto& thread : list.entries) {
        write_thread_entry(rsp, thread, login, true, show_user, show_board, show_images);
      }
      if (include_ol) rsp->write("</ol>");
      write_pagination(rsp, base_url, list.is_first, list.next);
    }

    auto write_comment_entry(
      Response rsp,
      const CommentListEntry& comment,
      InstanceController::Login login,
      bool is_list_item,
      bool is_tree_item,
      bool show_user,
      bool show_thread,
      bool show_images
    ) noexcept -> void {
      write_fmt(rsp, R"({} class="comment" id="comment-{:x}"><{} class="comment-info"><span>)",
        is_list_item ? "<li><article" : "<div",
        comment.id,
        is_tree_item ? "h3" : "h2"
      );
      if (show_user) {
        write_user_link(rsp, comment._author);
        rsp->write("</span><span>");
      }
      rsp->write("commented ");
      write_datetime(rsp, comment.comment().created_at());
      if (show_thread) {
        write_fmt(rsp, R"(</span><span>on <a href="/thread/{:x}">{}</a>)",
          comment.comment().thread(), Escape(comment.thread().title()));
        if (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible) {
          write_short_warnings(rsp, comment.thread());
        }
      }
      const bool has_warnings = comment.comment().content_warning() || comment.comment().mod_state() > ModState::Visible,
        thread_warnings = show_thread && (comment.thread().content_warning() || comment.thread().mod_state() > ModState::Visible);
      if (has_warnings || thread_warnings) {
        rsp->write(R"(</span></h2><div class="comment-content"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
        if (thread_warnings) write_warnings(rsp, comment.thread(), "Thread ");
        if (has_warnings) write_warnings(rsp, comment.comment());
        write_fmt(rsp, R"(</summary><div>{}</div></details></div>)", comment.comment().content_safe()->string_view());
      } else {
        write_fmt(rsp, R"(</span></{}><div class="comment-content">{}</div>)",
          is_tree_item ? "h3" : "h2",
          comment.comment().content_safe()->string_view()
        );
      }
      write_vote_buttons(rsp, comment, login);
      rsp->write(R"(<div class="controls">)");
      if (is_list_item) {
        write_fmt(rsp, R"(<a id="comment-link-{0:x}" href="/comment/{0:x}#replies">{1:d}{2}</a>)",
          comment.id,
          comment.stats().descendant_count(),
          comment.stats().descendant_count() == 1 ? " reply" : " replies"
        );
      } else if (is_tree_item) {
        write_fmt(rsp, R"(<a href="/comment/{:x}">Permalink</a>)", comment.id);
      } else {
        rsp->write("<span></span>");
      }
      write_controls_submenu(rsp, comment, login, show_user, show_thread);
      rsp->write(is_list_item ? "</div></article>" : "</div></div>");
    }

    auto write_comment_list(
      Response rsp,
      const PageOf<CommentListEntry>& list,
      string_view base_url,
      InstanceController::Login login,
      bool include_ol,
      bool show_user,
      bool show_thread,
      bool show_images
    ) noexcept -> void {
      if (include_ol) rsp->write(R"(<ol class="comment-list" id="top-level-list">)");
      for (const auto& comment : list.entries) {
        write_comment_entry(
          rsp, comment, login,
          true, false,
          show_user, show_thread, show_images
        );
      }
      if (include_ol) rsp->write("</ol>");
      write_pagination(rsp, base_url, list.is_first, list.next);
    }

    auto write_search_result_list(
      Response rsp,
      std::vector<InstanceController::SearchResultListEntry> list,
      InstanceController::Login login,
      bool include_ol
    ) noexcept -> void {
      if (include_ol) rsp->write(R"(<ol class="search-list" id="top-level-list">)");
      for (const auto& entry : list) {
        std::visit(overload{
          [&](const UserListEntry& user) {
            rsp->write("<li>");
            write_user_link(rsp, { user.user() });
          },
          [&](const BoardListEntry& board) {
            rsp->write("<li>");
            write_board_link(rsp, { board.board() });
          },
          [&](const ThreadListEntry& thread) {
            write_thread_entry(rsp, thread, login, true, true, true, true);
          },
          [&](const CommentListEntry& comment) {
            write_comment_entry(rsp, comment, login, true, false, true, true, true);
          },
        }, entry);
      }
      if (include_ol) rsp->write("</ol>");
    }

    auto write_comment_tree(
      Response rsp,
      const CommentTree& comments,
      uint64_t root,
      string_view sort_str,
      InstanceController::Login login,
      bool show_images,
      bool is_thread,
      bool include_ol,
      bool is_alt = false
    ) noexcept -> void {
      // TODO: Include existing query params
      auto range = comments.comments.equal_range(root);
      if (range.first == range.second) {
        if (is_thread) rsp->write(R"(<div class="no-comments">No comments</div>)");
        return;
      }
      if (include_ol) write_fmt(rsp, R"(<ol class="comment-list comment-tree" id="comments-{:x}">)", root);
      for (auto iter = range.first; iter != range.second; iter++) {
        const auto& comment = iter->second;
        write_fmt(rsp,
          R"(<li><article class="comment-with-comments {}">)",
          is_alt ? " odd-depth" : "", comment.id
        );
        write_comment_entry(rsp, comment, login, false, true, true, false, show_images);
        const auto cont = comments.continued.find(comment.id);
        if (cont != comments.continued.end() && cont->second == 0) {
          write_fmt(rsp,
            R"(<div class="comments-continued" id="continue-{0:x}"><a href="/comment/{0:x}">More comments…</a></div>)",
            comment.id
          );
        } else if (comment.stats().child_count()) {
          rsp->write(R"(<section class="comments" aria-title="Replies">)");
          write_comment_tree(rsp, comments, comment.id, sort_str, login, show_images, false, true, !is_alt);
          rsp->write("</section>");
        }
        rsp->write("</article>");
      }
      const auto cont = comments.continued.find(root);
      if (cont != comments.continued.end()) {
        write_fmt(rsp,
          R"(<li><div class="comments-continued" id="continue-{0:x}"><a href="/{1}/{0:x}?sort={2}&from={3:x}">More comments…</a></div>)",
          root, is_thread ? "thread" : "comment", sort_str, cont->second
        );
      }
      if (include_ol) rsp->write("</ol>");
    }

    auto write_content_warning_field(Response rsp, string_view existing_value = "") noexcept -> void {
      write_fmt(rsp,
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

    auto write_content_warning(Response rsp, string_view label, bool is_mod, string_view content, string_view prefix = "") noexcept -> void {
      write_fmt(rsp,
        R"(<p class="content-warning"><strong class="{}-warning-label">{}{}<span class="a11y">:</span></strong> {}</p>)",
        is_mod ? "mod" : "content", prefix, label, Escape{content}
      );
    }

    template <typename T> auto write_reply_form(Response rsp, const T& parent) noexcept -> void {
      write_fmt(rsp,
        R"(<form id="reply-{1:x}" class="form reply-form" method="post" action="/{0}/{1:x}/reply" )"
        R"html(hx-post="/{0}/{1:x}/reply" hx-target="#comments-{1:x}" hx-swap="afterbegin" hx-on::after-request="this.reset()">)html"
        R"(<a name="reply"></a>)"
        HTML_TEXTAREA("text_content", "Reply", R"( placeholder="Write your reply here")", ""),
        post_word<T>(), parent.id
      );
      write_content_warning_field(rsp);
      rsp->write(R"(<input type="submit" value="Reply"></form>)");
    }

    auto write_thread_view(
      Response rsp,
      const ThreadDetailResponse& thread,
      InstanceController::Login login,
      string_view sort_str = "Hot",
      bool show_images = false
    ) noexcept -> void {
      rsp->write(R"(<article class="thread-with-comments">)");
      write_thread_entry(rsp, thread, login, false, true, true, show_images);
      if (thread.thread().content_text_safe()) {
        if (thread.thread().content_warning() || thread.board().content_warning() || thread.thread().mod_state() > ModState::Visible) {
          rsp->write(R"(<div class="thread-content"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
          write_warnings(rsp, thread.thread());
          write_fmt(rsp, R"(</summary><div>{}</div></details></div>)", thread.thread().content_text_safe()->string_view());
        } else {
          write_fmt(rsp, R"(<div class="thread-content">{}</div>)", thread.thread().content_text_safe()->string_view());
        }
      }
      write_fmt(rsp, R"(<section class="comments" id="comments"><h2>{:d} comments</h2>)", thread.stats().descendant_count());
      write_sort_options(rsp, SortFormType::Comments, fmt::format("/thread/{:x}", thread.id), sort_str, false, show_images, fmt::format("#comments-{:x}", thread.id));
      if (InstanceController::can_reply_to(thread, login)) {
        write_reply_form(rsp, thread);
      }
      write_comment_tree(rsp, thread.comments, thread.id, sort_str, login, show_images, true, true);
      rsp->write("</section></article>");
    }

    auto write_comment_view(
      Response rsp,
      const CommentDetailResponse& comment,
      InstanceController::Login login,
      string_view sort_str = "Hot",
      bool show_images = false
    ) noexcept -> void {
      rsp->write(R"(<article class="comment-with-comments">)");
      write_comment_entry(
        rsp, comment, login,
        false, false,
        true, true, show_images
      );
      write_fmt(rsp, R"(<section class="comments" id="comments"><h2>{:d} replies</h2>)", comment.stats().descendant_count());
      write_sort_options(rsp, SortFormType::Comments, fmt::format("/comment/{:x}", comment.id), sort_str, false, show_images, fmt::format("#comments-{:x}", comment.id));
      if (InstanceController::can_reply_to(comment, login)) {
        write_reply_form(rsp, comment);
      }
      write_comment_tree(rsp, comment.comments, comment.id, sort_str, login, show_images, true, true);
      rsp->write("</section></article>");
    }

    inline auto error_banner(optional<string_view> error) noexcept -> string {
      if (!error) return "";
      return fmt::format(R"(<p class="error-message"><strong>Error:</strong> {}</p>)", Escape{*error});
    }

    inline auto write_login_form(Response rsp, optional<string_view> error = {}) noexcept -> void {
      write_fmt(rsp,
        R"(<main><form class="form form-page" method="post" action="/login">{}{})"
        HTML_FIELD("actual_username", "Username or email", "text", "")
        HTML_FIELD("password", "Password", "password", "")
        HTML_CHECKBOX("remember", "Remember me", "")
        R"(<input type="submit" value="Login"></form></main>)",
        error_banner(error), HONEYPOT_FIELD
      );
    }

    inline auto write_register_form(Response rsp, optional<string_view> error = {}) noexcept -> void {
      write_fmt(rsp,
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

    inline auto write_create_board_form(
      Response rsp,
      const LocalUserDetailResponse login,
      optional<string_view> error = {}
    ) noexcept -> void {
      write_fmt(rsp,
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
      Response rsp,
      bool show_url,
      const BoardDetailResponse board,
      const LocalUserDetailResponse login,
      optional<string_view> error = {}
    ) noexcept -> void {
      write_fmt(rsp,
        R"(<main><form class="form form-page" method="post" action="/b/{}/create_thread"><h2>Create Thread</h2>{})"
        R"(<p class="thread-info"><span>Posting as )",
        Escape(board.board().name()), error_banner(error)
      );
      write_user_link(rsp, login._user);
      rsp->write("</span><span>to ");
      write_board_link(rsp, board._board);
      rsp->write("</span></p><br>" HTML_FIELD("title", "Title", "text", R"( autocomplete="off" required)"));
      if (show_url) {
        rsp->write(
          HTML_FIELD("submission_url", "Submission URL", "text", R"( autocomplete="off" required)")
          HTML_TEXTAREA("text_content", "Description (optional)", "", "")
        );
      } else {
        rsp->write(HTML_TEXTAREA("text_content", "Text content", " required", ""));
      }
    }

    auto write_edit_thread_form(
      Response rsp,
      const ThreadListEntry& thread,
      const LocalUserDetailResponse login,
      optional<string_view> error = {}
    ) noexcept -> void {
      write_fmt(rsp,
        R"(<main><form class="form form-page" method="post" action="/thread/{:x}/edit"><h2>Edit Thread</h2>{})"
        R"(<p class="thread-info"><span>Posting as )",
        thread.id, error_banner(error)
      );
      write_user_link(rsp, login._user);
      rsp->write("</span><span>to ");
      write_board_link(rsp, thread._board);
      write_fmt(rsp,
        "</span></p><br>"
        HTML_FIELD("title", "Title", "text", R"( value="{}" autocomplete="off" required)")
        HTML_TEXTAREA("text_content", "Text content", "{}", "{}"),
        Escape(thread.thread().title()), thread.thread().content_url() ? "" : " required",
        Escape(thread.thread().content_text_raw())
      );
      write_content_warning_field(rsp, thread.thread().content_warning() ? thread.thread().content_warning()->string_view() : "");
      rsp->write(R"(<input type="submit" value="Submit"></form></main>)");
    }

    auto write_site_admin_form(
      Response rsp,
      const SiteDetail* site,
      optional<string_view> error = {}
    ) noexcept -> void {
      write_fmt(rsp,
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

    auto write_user_settings_form(
      Response rsp,
      const SiteDetail* site,
      const LocalUserDetailResponse login,
      optional<string_view> error = {}
    ) noexcept -> void {
      uint8_t cw_mode = 1;
      if (login.local_user().hide_cw_posts()) cw_mode = 0;
      else if (login.local_user().expand_cw_images()) cw_mode = 3;
      else if (login.local_user().expand_cw_posts()) cw_mode = 2;
      write_fmt(rsp,
        R"(<form class="form form-page" method="post" action="/settings"><h2>User settings</h2>{})"
        HTML_FIELD("display_name", "Display name", "text", R"( value="{}")")
        HTML_FIELD("email", "Email address", "email", R"( required value="{}")")
        HTML_TEXTAREA("bio", "Bio", "", "{}")
        HTML_FIELD("avatar_url", "Avatar URL", "text", R"( value="{}")")
        HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}")")
        HTML_CHECKBOX("open_links_in_new_tab", "Open links in new tab", "{}")
        HTML_CHECKBOX("show_avatars", "Show avatars", "{}")
        HTML_CHECKBOX("show_bot_accounts", "Show bot accounts", "{}")
        HTML_CHECKBOX("show_karma", "Show karma", "{}")
        R"(<fieldset><legend>Content warnings</legend>)"
          R"(<label for="cw_hide"><input type="radio" id="cw_hide" name="content_warnings" value="0"{}> Hide posts with content warnings completely</label>)"
          R"(<label for="cw_default"><input type="radio" id="cw_default" name="content_warnings" value="1"{}> Collapse posts with content warnings (default)</label>)"
          R"(<label for="cw_show"><input type="radio" id="cw_show" name="content_warnings" value="2"{}> Expand text content of posts with content warnings but hide images</label>)"
          R"(<label for="cw_show_images"><input type="radio" id="cw_show_images" name="content_warnings" value="3"{}> Always expand text and images with content warnings</label>)"
        R"(</fieldset>)",
        error_banner(error),
        Escape(login.user().display_name()),
        Escape(login.local_user().email()),
        Escape(login.user().bio_raw()),
        Escape(login.user().avatar_url()),
        Escape(login.user().banner_url()),
        login.local_user().open_links_in_new_tab() ? " checked" : "",
        login.local_user().show_avatars() ? " checked" : "",
        login.local_user().show_bot_accounts() ? " checked" : "",
        login.local_user().show_karma() ? " checked" : "",
        cw_mode == 0 ? " checked" : "", cw_mode == 1 ? " checked" : "", cw_mode == 2 ? " checked" : "", cw_mode == 3 ? " checked" : ""
      );
      if (site->javascript_enabled) {
        write_fmt(rsp,
          HTML_CHECKBOX("javascript_enabled", "JavaScript enabled", "{}"),
          login.local_user().javascript_enabled() ? " checked" : ""
        );
      }
      // TODO: Default sort, default comment sort
      // TODO: Change password
      rsp->write(R"(<input type="submit" value="Submit"></form>)");
    }

    auto write_board_settings_form(
      Response rsp,
      const LocalBoardDetailResponse board,
      optional<string_view> error = {}
    ) noexcept -> void {
      write_fmt(rsp,
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
        Escape(board.board().display_name()),
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
      const auto hash = fmt::format("{:016x}", XXH3_64bits(src.data(), src.length()));
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

    static inline auto hex_id_param(Request req, uint16_t param) {
      const auto str = req->getParameter(param);
      uint64_t id;
      const auto res = std::from_chars(str.begin(), str.end(), id, 16);
      if (res.ec != std::errc{} || res.ptr != str.data() + str.length()) {
        throw ApiError(fmt::format("Invalid hexadecimal ID: ", str), 404);
      }
      return id;
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

    auto register_routes(App& app) -> void {

      using PostList = std::variant<PageOf<ThreadListEntry>, PageOf<CommentListEntry>>;

      // -----------------------------------------------------------------------
      // STATIC FILES
      // -----------------------------------------------------------------------
      serve_static(app, "default-theme.css", TYPE_CSS, default_theme_css);
      serve_static(app, "htmx.min.js", TYPE_JS, htmx_min_js);
      serve_static(app, "feather-sprite.svg", TYPE_SVG, feather_sprite_svg);

      // -----------------------------------------------------------------------
      // PAGES
      // -----------------------------------------------------------------------
      auto self = this->shared_from_this();
      Router<SSL, Meta, ErrorMeta>(app,
        bind(&Webapp::middleware, self, _1, _2),
        bind(&Webapp::error_middleware, self, _1, _2),
        bind(&Webapp::error_page, self, _1, _2, _3)
      )
      .get("/", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto boards = self->controller->list_local_boards(txn);
        // ---
        self->write_html_header(rsp, m, {
          .canonical_path = "/",
          .banner_title = m.site->name,
          .banner_link = "/",
        });
        rsp->write("<div>");
        self->write_sidebar(rsp, m);
        rsp->write("<main>");
        self->write_board_list(rsp, boards);
        rsp->write("</main></div>");
        end_with_html_footer(rsp, m);
      })
      .get("/b/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto board_id = board_name_param(txn, req, 0);
        const auto board = self->controller->board_detail(txn, board_id, m.logged_in_user_id);
        const auto original_sort_str = req->getQuery("sort"),
          sort_str = original_sort_str.empty() ? "Hot" : original_sort_str;
        const auto sort = InstanceController::parse_sort_type(sort_str);
        const auto from = InstanceController::parse_hex_id(string(req->getQuery("from")));
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || original_sort_str.empty();
        const auto base_url = fmt::format("/b/{}?type={}&sort={}&images={}",
          board.board().name()->string_view(),
          show_threads ? "threads" : "comments",
          sort_str,
          show_images ? 1 : 0
        );
        const auto list = show_threads
          ? PostList(self->controller->list_board_threads(txn, board_id, sort, m.login, from))
          : PostList(self->controller->list_board_comments(txn, board_id, sort, m.login, from));
        // ---
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          self->write_html_header(rsp, m, {
            .canonical_path = req->getUrl(),
            .banner_title = display_name(board.board()),
            .banner_link = req->getUrl(),
            .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
            .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
          });
          rsp->write("<div>");
          self->write_sidebar(rsp, m, {board});
          rsp->write(R"(<section><h2 class="a11y">Sort and filter</h2>)");
          self->write_sort_options(rsp, SortFormType::Board, req->getUrl(), sort_str, show_threads, show_images);
          rsp->write(R"(</section><main>)");
        }
        std::visit(overload{
          [&](const PageOf<ThreadListEntry>& l){self->write_thread_list(rsp, l, base_url, m.login, !m.is_htmx, true, false, show_images);},
          [&](const PageOf<CommentListEntry>& l){self->write_comment_list(rsp, l, base_url, m.login, !m.is_htmx, true, true, show_images);}
        }, list);
        if (m.is_htmx) {
          rsp->end();
        } else {
          rsp->write("</main></div>");
          end_with_html_footer(rsp, m);
        }
      })
      .get("/b/:name/create_thread", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto show_url = req->getQuery("text") != "1";
        const auto login = m.require_login(self, txn);
        const auto board = self->controller->board_detail(txn, board_id, m.logged_in_user_id);
        // ---
        self->write_html_header(rsp, m, {
          .canonical_path = fmt::format("/b/{}/create_thread", board.board().name()->string_view()),
          .banner_title = display_name(board.board()),
          .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
          .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
          .page_title = "Create Thread",
          .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
        });
        self->write_create_thread_form(rsp, show_url, board, login);
        end_with_html_footer(rsp, m);
      })
      .get("/u/:name", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto user_id = user_name_param(txn, req, 0);
        const auto user = self->controller->user_detail(txn, user_id);
        const auto original_sort_str = req->getQuery("sort"),
          sort_str = original_sort_str.empty() ? "New" : original_sort_str;
        const auto sort = InstanceController::parse_user_post_sort_type(sort_str);
        const auto from = InstanceController::parse_hex_id(string(req->getQuery("from")));
        const auto show_threads = req->getQuery("type") != "comments",
          show_images = req->getQuery("images") == "1" || original_sort_str.empty();
        const auto base_url = fmt::format("/u/{}?type={}&sort={}&images={}",
          user.user().name()->string_view(),
          show_threads ? "threads" : "comments",
          sort_str,
          show_images ? 1 : 0
        );
        const auto list = show_threads
          ? PostList(self->controller->list_user_threads(txn, user_id, sort, m.login, from))
          : PostList(self->controller->list_user_comments(txn, user_id, sort, m.login, from));
        // ---
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
        } else {
          self->write_html_header(rsp, m, {
            .canonical_path = req->getUrl(),
            .banner_title = display_name(user.user()),
            .banner_link = req->getUrl(),
            .banner_image = user.user().banner_url() ? optional(fmt::format("/media/user/{}/banner.webp", user.user().name()->string_view())) : nullopt,
            .card_image = user.user().avatar_url() ? optional(fmt::format("/media/user/{}/avatar.webp", user.user().name()->string_view())) : nullopt
          });
          rsp->write("<div>");
          self->write_sidebar(rsp, m);
          rsp->write(R"(<section><h2 class="a11y">Sort and filter</h2>)");
          self->write_sort_options(rsp, SortFormType::User, req->getUrl(), sort_str, show_threads, show_images);
          rsp->write(R"(</section><main>)");
        }
        std::visit(overload{
          [&](const PageOf<ThreadListEntry>& l){self->write_thread_list(rsp, l, base_url, m.login, !m.is_htmx, false, false, show_images);},
          [&](const PageOf<CommentListEntry>& l){self->write_comment_list(rsp, l, base_url, m.login, !m.is_htmx, false, true, show_images);}
        }, list);
        if (m.is_htmx) {
          rsp->end();
        } else {
          rsp->write("</main></div>");
          end_with_html_footer(rsp, m);
        }
      })
      .get("/thread/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto id = hex_id_param(req, 0);
        // TODO: Get sort and filter settings from user
        const auto original_sort_str = req->getQuery("sort"),
          sort_str = original_sort_str.empty() ? "Hot" : original_sort_str;
        const auto sort = InstanceController::parse_comment_sort_type(sort_str);
        const auto from = InstanceController::parse_hex_id(string(req->getQuery("from")));
        const auto show_images = req->getQuery("images") == "1" || sort_str.empty();
        const auto detail = self->controller->thread_detail(txn, id, sort, m.login, from);
        const auto board = self->controller->board_detail(txn, detail.thread().board(), m.logged_in_user_id);
        // ---
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          self->write_comment_tree(rsp, detail.comments, detail.id, sort_str, m.login, show_images, true, false);
          rsp->end();
        } else {
          self->write_html_header(rsp, m, {
            .canonical_path = req->getUrl(),
            .banner_title = display_name(board.board()),
            .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
            .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
            .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
          });
          rsp->write("<div>");
          self->write_sidebar(rsp, m, board);
          rsp->write("<main>");
          self->write_thread_view(rsp, detail, m.login, sort_str, show_images);
          rsp->write("</main></div>");
          end_with_html_footer(rsp, m);
        }
      })
      .get("/thread/:id/edit", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto id = hex_id_param(req, 0);
        const auto login = m.require_login(self, txn);
        const auto thread = self->controller->get_thread_entry(txn, id, login);
        if (!InstanceController::can_edit(thread, login)) throw ApiError("Cannot edit this post", 403);
        // ---
        self->write_html_header(rsp, m, {
          .canonical_path = req->getUrl(),
          .banner_title = display_name(thread.board()),
          .banner_link = fmt::format("/b/{}", thread.board().name()->string_view()),
          .banner_image = thread.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", thread.board().name()->string_view())) : nullopt,
        });
        self->write_edit_thread_form(rsp, thread, login);
        end_with_html_footer(rsp, m);
      })
      .get("/comment/:id", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        const auto id = hex_id_param(req, 0);
        // TODO: Get sort and filter settings from user
        const auto original_sort_str = req->getQuery("sort"),
          sort_str = original_sort_str.empty() ? "Hot" : original_sort_str;
        const auto sort = InstanceController::parse_comment_sort_type(sort_str);
        const auto from = InstanceController::parse_hex_id(string(req->getQuery("from")));
        const auto show_images = req->getQuery("images") == "1" || sort_str.empty();
        const auto detail = self->controller->comment_detail(txn, id, sort, m.login, from);
        const auto board = self->controller->board_detail(txn, detail.thread().board(), m.logged_in_user_id);
        // ----
        if (m.is_htmx) {
          rsp->writeHeader("Content-Type", TYPE_HTML);
          m.write_cookie(rsp);
          self->write_comment_tree(rsp, detail.comments, detail.id, sort_str, m.login, show_images, false, false);
          rsp->end();
        } else {
          self->write_html_header(rsp, m, {
            .canonical_path = req->getUrl(),
            .banner_title = display_name(board.board()),
            .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
            .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
            .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
          });
          rsp->write("<div>");
          self->write_sidebar(rsp, m, board);
          rsp->write("<main>");
          self->write_comment_view(rsp, detail, m.login, sort_str, show_images);
          rsp->write("</main></div>");
          end_with_html_footer(rsp, m);
        }
      })
      .get_async("/search", [self](auto* rsp, auto* req, auto m, auto wrap) {
        SearchQuery query {
          .query = req->getQuery("search"),
          /// TODO: Other parameters
          .include_threads = true,
          .include_comments = true
        };
        self->controller->search(query, m->login, 0, [
          self, rsp, m = std::move(m), wrap = std::move(wrap)
        ](auto& txn, auto results) mutable {
          wrap([&]{
            m->populate(self, txn);
            self->write_html_header(rsp, *m, {
              .canonical_path = "/search",
              .banner_title = "Search",
            });
            rsp->write("<div>");
            self->write_sidebar(rsp, *m);
            rsp->write("<main>");
            self->write_search_result_list(rsp, results, m->login, true);
            rsp->write("</main></div>");
            end_with_html_footer(rsp, *m);
          });
        });
      })
      .get("/login", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        m.populate(self, txn);
        if (m.login) {
          rsp->writeStatus(http_status(303))
            ->writeHeader("Location", "/")
            ->end();
        } else {
          self->write_html_header(rsp, m, {
            .canonical_path = "/login",
            .banner_title = "Login",
          });
          self->write_login_form(rsp);
          end_with_html_footer(rsp, m);
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
          self->write_html_header(rsp, m, {
            .canonical_path = "/register",
            .banner_title = "Register",
          });
          self->write_register_form(rsp);
          end_with_html_footer(rsp, m);
        }
      })
      .get("/settings", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(self, txn);
        self->write_html_header(rsp, m, {
          .canonical_path = "/settings",
          .banner_title = "User Settings",
        });
        rsp->write("<main>");
        self->write_user_settings_form(rsp, m.site, login);
        rsp->write("</main>");
        end_with_html_footer(rsp, m);
      })
      .get("/b/:name/settings", [self](auto* rsp, auto* req, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto board_id = board_name_param(txn, req, 0);
        const auto login = m.require_login(self, txn);
        const auto board = self->controller->local_board_detail(txn, board_id, m.logged_in_user_id);
        if (!login.local_user().admin() && login.id != board.local_board().owner()) {
          throw ApiError("Must be admin or board owner to view this page", 403);
        }
        // --
        self->write_html_header(rsp, m, {
          .canonical_path = fmt::format("/b/{}/settings", board.board().name()->string_view()),
          .banner_title = display_name(board.board()),
          .banner_link = fmt::format("/b/{}", board.board().name()->string_view()),
          .banner_image = board.board().banner_url() ? optional(fmt::format("/media/board/{}/banner.webp", board.board().name()->string_view())) : nullopt,
          .page_title = "Board Settings",
          .card_image = board.board().icon_url() ? optional(fmt::format("/media/board/{}/icon.webp", board.board().name()->string_view())) : nullopt
        });
        rsp->write("<main>");
        self->write_board_settings_form(rsp, board);
        rsp->write("</main>");
        end_with_html_footer(rsp, m);
      })
      .get("/site_admin", [self](auto* rsp, auto*, Meta& m) {
        auto txn = self->controller->open_read_txn();
        const auto login = m.require_login(self, txn);
        if (!InstanceController::can_change_site_settings(login)) {
          throw ApiError("Admin login required to view this page", 403);
        }
        // --
        self->write_html_header(rsp, m, {
          .canonical_path = "/site_admin",
          .banner_title = "Site Admin",
        });
        rsp->write("<main>");
        self->write_site_admin_form(rsp, m.site);
        rsp->write("</main>");
        end_with_html_footer(rsp, m);
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
            self->write_html_header(rsp, *m, {
              .canonical_path = "/login",
              .banner_title = "Login",
            });
            self->write_login_form(rsp, {e.message});
            end_with_html_footer(rsp, *m);
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
            self->write_html_header(rsp, *m, {
              .canonical_path = "/register",
              .banner_title = "Register",
            });
            self->write_register_form(rsp, {e.message});
            end_with_html_footer(rsp, *m);
            return;
          }
          self->write_html_header(rsp, *m, { .banner_title = "Register" });
          rsp->write(R"(<main><div class="form form-page"><h2>Registration complete!</h2>)"
            R"(<p>Log in to your new account:</p><p><a class="big-button" href="/login">Login</a></p>)"
            "</div></main>");
          end_with_html_footer(rsp, *m);
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
            const auto comment = self->controller->get_comment_entry(txn, id, m->login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->write_comment_entry(rsp, comment, m->login, false, true, true, false, true);
            self->write_toast(rsp, "Reply submitted");
            rsp->end();
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
            const auto comment = self->controller->get_comment_entry(txn, id, m->login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->write_comment_entry(rsp, comment, m->login, false, true, true, false, true);
            self->write_toast(rsp, "Reply submitted");
            rsp->end();
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
            const auto login = self->controller->local_user_detail(txn, user);
            const auto thread = self->controller->get_thread_entry(txn, id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->write_controls_submenu(rsp, thread, login, show_user, show_board);
            rsp->end();
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
            const auto login = self->controller->local_user_detail(txn, user);
            const auto comment = self->controller->get_comment_entry(txn, id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            m->write_cookie(rsp);
            self->write_controls_submenu(rsp, comment, login, show_user, show_board);
            rsp->end();
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
            const auto login = self->controller->local_user_detail(txn, user);
            const auto thread = InstanceController::get_thread_entry(txn, post_id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            self->write_vote_buttons(rsp, thread, login);
            rsp->end();
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
            const auto login = self->controller->local_user_detail(txn, user);
            const auto comment = InstanceController::get_comment_entry(txn, post_id, login);
            rsp->writeHeader("Content-Type", TYPE_HTML);
            self->write_vote_buttons(rsp, comment, login);
            rsp->end();
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
            self->write_subscribe_button(rsp, name, true);
            rsp->end();
          } else {
            write_redirect_back(rsp, referer);
          }
        };
      });
    }
  };

  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<InstanceController> controller
  ) -> void {
    auto router = std::make_shared<Webapp<SSL>>(controller);
    router->register_routes(app);
  }

  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<InstanceController> controller
  ) -> void;

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<InstanceController> controller
  ) -> void;
}
