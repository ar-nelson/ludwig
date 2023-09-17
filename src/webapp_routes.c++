#include "webapp_routes.h++"
#include <regex>
#include <spdlog/fmt/fmt.h>
#include "xxhash.h"
#include "generated/default-theme.css.h"
#include "generated/htmx.min.js.h"
#include "generated/feather-sprite.svg.h"

using std::optional, std::string, std::string_view;

#define COOKIE_NAME "ludwig_auth"

namespace Ludwig {
  static constexpr std::string_view
    ESCAPED = "<>'\"&",
    HTML_FOOTER = R"(<div class="spacer"></div> <footer>Powered by Ludwig</body></html>)",
    LOGIN_FORM = R"(<main class="full-width"><form class="form-page">)"
      R"(<label for="username"><span>Username or email</span><input type="text" name="username" id="username"></label>)"
      R"(<label for="password"><span>Password</span><input type="password" name="password" id="password"></label>)"
      R"(<input type="submit" value="Login">)"
      R"(</form></main>)";
  static const std::regex cookie_regex(
    R"((^|;)\s*)" COOKIE_NAME R"(\s*=\s*([^;]+))",
    std::regex_constants::ECMAScript
  );

  static inline auto hexstring(uint64_t n, bool padded = false) -> std::string {
    std::string s;
    if (padded) fmt::format_to(std::back_inserter(s), "{:016x}", n);
    else fmt::format_to(std::back_inserter(s), "{:x}", n);
    return s;
  }

  static inline auto http_status(uint16_t code) -> std::string {
    switch (code) {
      case 200: return "200 OK";
      case 201: return "201 Created";
      case 202: return "202 Accepted";
      case 204: return "204 No Content";
      case 301: return "301 Moved Permanently";
      case 302: return "302 Found";
      case 303: return "303 See Other";
      case 304: return "304 Not Modified";
      case 307: return "307 Temporary Redirect";
      case 308: return "308 Permanent Redirect";
      case 400: return "400 Bad Request";
      case 401: return "401 Unauthorized";
      case 403: return "403 Forbidden";
      case 404: return "404 Not Found";
      case 405: return "405 Method Not Allowed";
      case 406: return "406 Not Acceptable";
      case 408: return "408 Request Timeout";
      case 409: return "409 Conflict";
      case 410: return "410 Gone";
      case 413: return "413 Payload Too Large";
      case 415: return "413 Unsupported Media Type";
      case 418: return "418 I'm a teapot";
      case 422: return "422 Unprocessable Entity";
      case 429: return "429 Too Many Requests";
      case 451: return "451 Unavailable For Legal Reasons";
      case 500: return "500 Internal Server Error";
      case 501: return "501 Not Implemented";
      case 503: return "503 Service Unavailable";
      default: return std::to_string(code);
    }
  }

  enum class SortFormType {
    Board,
    Comments,
    User
  };

  struct Escape {
    string_view str;
  };

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, const std::string_view rhs) -> uWS::HttpResponse<SSL>& {
    lhs.write(rhs);
    return lhs;
  }

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, int64_t rhs) -> uWS::HttpResponse<SSL>& {
    string s;
    fmt::format_to(std::back_inserter(s), "{}", rhs);
    lhs.write(s);
    return lhs;
  }

  template <bool SSL> static inline auto operator<<(uWS::HttpResponse<SSL>& lhs, uint64_t rhs) -> uWS::HttpResponse<SSL>& {
    string s;
    fmt::format_to(std::back_inserter(s), "{}", rhs);
    lhs.write(s);
    return lhs;
  }

  template <typename T> static inline auto operator<<(T& lhs, Escape rhs) -> T& {
    size_t start = 0;
    for (
      size_t i = rhs.str.find_first_of(ESCAPED);
      i != std::string_view::npos;
      start = i + 1, i = rhs.str.find_first_of(ESCAPED, start)
    ) {
      if (i > start) lhs << rhs.str.substr(start, i - start);
      switch (rhs.str[i]) {
        case '<':
          lhs << "&lt;";
          break;
        case '>':
          lhs << "&gt;";
          break;
        case '\'':
          lhs << "&apos;";
          break;
        case '"':
          lhs << "&quot;";
          break;
        case '&':
          lhs << "&amp;";
          break;
      }
    }
    if (start < rhs.str.length()) lhs << rhs.str.substr(start);
    return lhs;
  }

  template <bool SSL> struct Webapp : public std::enable_shared_from_this<Webapp<SSL>> {
    std::shared_ptr<Controller> controller;

    Webapp(std::shared_ptr<Controller> controller) : controller(controller) {}

    using Self = std::shared_ptr<Webapp<SSL>>;
    using App = uWS::TemplatedApp<SSL>;
    using Response = uWS::HttpResponse<SSL>;
    using Request = uWS::HttpRequest;

    auto error_page(Response& rsp, ControllerError e) noexcept {
      try {
        rsp.writeStatus(http_status(e.http_error()));
        rsp.writeHeader("Content-Type", "text/html; charset=utf-8");
        rsp << "Error " << (uint64_t)e.http_error() << ": " << Escape{e.what()};
      } catch (...) {
        spdlog::error("Error when displaying HTTP error ({} {})", e.http_error(), e.what());
      }
      rsp.end();
    }

    struct SafePage {
    private:
      Self self;
      Request& req;
      Response& rsp;
    public:
      SafePage(Self self, Request& req, Response& rsp) : self(self), req(req), rsp(rsp) {}

      inline auto operator()(
        std::function<void (Request&)> before,
        std::function<void (Response&)> after
      ) -> void {
        try {
          before(req);
        } catch (ControllerError e) {
          self->error_page(rsp, e);
          return;
        } catch (std::exception e) {
          spdlog::error("Unhandled exception in webapp route: {}", e.what());
          self->error_page(rsp, ControllerError("Unhandled internal exception", 500));
          return;
        } catch (...) {
          spdlog::error("Unhandled exception in webapp route, no information available");
          self->error_page(rsp, ControllerError("Unhandled internal exception", 500));
          return;
        }
        after(rsp);
      }
    };

    auto safe_page(std::function<void (Self, SafePage)> fn) {
      return [self = this->shared_from_this(), fn](Response* rsp, Request* req) {
        fn(self, SafePage(self, *req, *rsp));
      };
    }

    auto get_logged_in_user(ReadTxn& txn, Request& req) -> optional<LocalUserDetailResponse> {
      auto cookies = req.getHeader("cookie");
      std::match_results<string_view::const_iterator> match;
      if (!std::regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) return {};
      try {
        auto id = controller->get_auth_user({ match[1] });
        return optional(controller->local_user_detail(txn, id));
      } catch (...) {
        //spdlog::debug("Auth cookie is invalid; requesting deletion");
        //rsp->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
        return {};
      }
    }

    static auto write_html_header(
      Response& rsp,
      const SiteDetail* site,
      const optional<LocalUserDetailResponse>& logged_in_user,
      optional<string_view> canonical_path = {},
      optional<string_view> banner_title = {},
      optional<string_view> page_title = {},
      optional<string_view> card_image = {}
    ) noexcept -> void {
      rsp.writeHeader("Content-Type", "text/html; charset=utf-8");
      rsp << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,shrink-to-fit=no"><title>)"
        << Escape{site->name};
      if (page_title) rsp << " - " << Escape{*page_title};
      else if (banner_title) rsp << " - " << Escape{*banner_title};
      rsp << R"(</title><link rel="stylesheet" href="/static/default-theme.css">)";
      if (canonical_path) {
        rsp << R"(<link rel="canonical" href=")" << Escape{site->domain} << Escape{*canonical_path} <<
          R"("><meta property="og:url" content=")" << Escape{site->domain} << Escape{*canonical_path} <<
          R"("><meta property="twitter:url" content=")" << Escape{site->domain} << Escape{*canonical_path} <<
          R"(">)";
      }
      if (page_title) {
        rsp << R"(<meta property="title" href=")" << Escape{site->name} << " - " << Escape{*page_title} <<
          R"("><meta property="og:title" content=")" << Escape{site->name} << " - " << Escape{*page_title} <<
          R"("><meta property="twitter:title" content=")" << Escape{site->name} << " - " << Escape{*page_title} <<
          R"("><meta property="og:type" content="website">)";
      }
      if (card_image) {
        rsp << R"("<meta property="og:image" content=")" << Escape{*card_image} <<
          R"("><meta property="twitter:image" content=")" << Escape{*card_image} <<
          R"("><meta property="twitter:card" content="summary_large_image">)";
      }
      rsp << R"(</head><body><nav class="topbar"><div class="site-name">🎹 )"
        << Escape{site->name} // TODO: Site icon
        << R"(</div><ul class="quick-boards"><li><a href="/">Home</a><li><a href="/feed/local">Local</a><li><a href="/feed/federated">All</a></ul><ul>)";
      if (logged_in_user) {
        // TODO: Subscribed boards
        rsp << R"(<li id="topbar-user"><a href="/u/)" << Escape{logged_in_user->user->name()->string_view()}
          << R"(">)" << Escape{logged_in_user->user->display_name()->string_view()}
          << R"(</a> ()" << (logged_in_user->stats->page_karma() + logged_in_user->stats->note_karma())
          << R"()<li><a href="/settings">Settings</a><li><a href="/logout">Logout</a></ul></nav>)";
      } else {
        // TODO: Top local boards
        rsp << R"(<li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)";
      }
      if (banner_title) {
        rsp << R"(<header id="page-header"><h1>)" << Escape{*banner_title} << "</h1></header>";
      }
    }

    static inline auto nsfw_allowed(const SiteDetail* site, const optional<LocalUserDetailResponse>& logged_in_user) -> bool {
      if (!site->nsfw_allowed) return false;
      if (!logged_in_user) return true;
      return logged_in_user->local_user->show_nsfw();
    }

    static auto write_sidebar(
      Response& rsp,
      const SiteDetail* site,
      const optional<LocalUserDetailResponse>& logged_in_user,
      const optional<BoardDetailResponse>& board = {}
    ) noexcept -> void {
      rsp << R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
        R"(<form action="/search" id="search-form">)"
        R"(<label for="search"><span class="a11y">Search</span><input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)";
      const auto nsfw = nsfw_allowed(site, logged_in_user);
      const auto board_name = Escape{board ? board->board->display_name() ? board->board->display_name()->string_view() : board->board->name()->string_view() : ""};
      if (board) rsp << R"(<input type="hidden" name="board" value=")" << board->id << R"(">)";
      if (nsfw || board) {
        rsp << R"(<details id="search-options"><summary>Search Options</summary><fieldset>)";
        if (board) {
          rsp << R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to )"
            << board_name
            << "</label>";
        }
        if (nsfw) {
          rsp << R"(<label for="include_nsfw"><input type="checkbox" name="include_nsfw" id="include_nsfw" checked> Include NSFW results</label>)";
        }
        rsp << "</fieldset></details>";
      }
      rsp << "</form></section>";
      if (!logged_in_user) {
        rsp << R"(<section id="login-section"><h2>Login</h2>)"
          R"(<form method="post" action="/do/login" id="login-form">)"
          R"(<label for="username"><span class="a11y">Username or email</span><input type="text" name="username" id="username" placeholder="Username or email"></label>)"
          R"(<label for="password"><span class="a11y">Password</span><input type="password" name="password" id="password" placeholder="Password"></label>)"
          R"(<input type="submit" value="Login" class="big-button"></form>)"
          R"(<a href="/register" class="big-button">Register</a>)"
          "</section>";
      } else if (board) {
        rsp << R"(<section id="actions-section"><h2>Actions</h2><a class="big-button" href="/b/)"
          << Escape{board->board->name()->string_view()} << R"(/submit">Submit a new link</a><a class="big-button" href="/b/)"
          << Escape{board->board->name()->string_view()} << R"(/submit?text=true">Submit a new text post</a></section>)";
      }
      if (board) {
        rsp << R"(<section id="board-sidebar"><h2>)" << board_name << "</h2>";
        // TODO: Banner image
        // TODO: Allow safe HTML in description
        if (board->board->description_safe()) {
          rsp << "<p>" << board->board->description_safe()->string_view() << "</p>";
        }
        rsp << "</section>";
        // TODO: Board stats
        // TODO: Modlog link
      } else {
        rsp << R"(<section id="site-sidebar"><h2>)" << Escape{site->name} << "</h2>";
        if (site->banner_url) {
          rsp << R"(<div class="sidebar-banner"><img src=")" << Escape{*site->banner_url}
            << R"(" alt=")" << Escape{site->name} << R"( banner"></div>)";
        }
        // TODO: Allow safe HTML in description
        rsp << "<p>" << Escape{site->description} << "</p></section>";
        // TODO: Site stats
        // TODO: Modlog link
      }
      rsp << "</aside>";
    }

    static auto relative_time(uint64_t timestamp) -> std::string {
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
      if (diff < HOUR) return std::to_string(diff / MINUTE) + " minutes ago";
      if (diff < HOUR * 2) return "1 hour ago";
      if (diff < DAY) return std::to_string(diff / HOUR) + " hours ago";
      if (diff < DAY * 2) return "1 day ago";
      if (diff < WEEK) return std::to_string(diff / DAY) + " days ago";
      if (diff < WEEK * 2) return "1 week ago";
      if (diff < MONTH) return std::to_string(diff / WEEK) + " weeks ago";
      if (diff < MONTH * 2) return "1 month ago";
      if (diff < YEAR) return std::to_string(diff / MONTH) + " months ago";
      if (diff < YEAR * 2) return "1 year ago";
      return std::to_string(diff / YEAR) + " years ago";
    }

    static auto write_user_link(
      Response& rsp,
      const User* user
    ) noexcept -> void {
      rsp << R"(<a class="user-link" href="/u/)" << Escape{user->name()->string_view()} << R"(">)";
      if (user->avatar_url()) {
        rsp << R"(<img aria-hidden="true" class="avatar" loading="lazy" src=")" << Escape{user->avatar_url()->string_view()} << R"(">)";
      } else {
        rsp << R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#user"></svg>)";
      }
      auto name = user->name()->str();
      if (user->display_name() == nullptr) {
        rsp << Escape{name.substr(0, name.find('@'))};
      } else {
        rsp << Escape{user->display_name()->string_view()};
      }
      if (user->instance()) {
        const auto suffix_ix = name.find('@');
        if (suffix_ix != std::string::npos) {
          rsp << R"(<span class="at-domain">@)" << Escape{name.substr(suffix_ix + 1)} << "</span>";
        }
      }
      rsp << "</a>";
    }

    static auto write_board_link(
      Response& rsp,
      const Board* board
    ) noexcept -> void {
      rsp << R"(<a class="board-link" href="/b/)" << Escape{board->name()->string_view()} << R"(">)";
      if (board->icon_url()) {
        rsp << R"(<img aria-hidden="true" class="avatar" loading="lazy" src=")" << Escape{board->icon_url()->string_view()} << R"(">)";
      } else {
        rsp << R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#book"></svg>)";
      }
      auto name = board->name()->str();
      if (board->display_name() == nullptr) {
        rsp << Escape{name.substr(0, name.find('@'))};
      } else {
        rsp << Escape{board->display_name()->string_view()};
      }
      if (board->instance()) {
        const auto suffix_ix = name.find('@');
        if (suffix_ix != std::string::npos) {
          rsp << R"(<span class="at-domain">@)" << Escape{name.substr(suffix_ix + 1)} << "</span>";
        }
      }
      rsp << "</a>";
    }

    static auto write_board_list(
      Response& rsp,
      const ListBoardsResponse& list
    ) noexcept -> void {
      // TODO: Pagination
      rsp << R"(<ol class="board-list">)";
      for (size_t i = 0; i < list.size; i++) {
        rsp << R"(<li class="board-list-entry"><h2 class="board-title">)";
        write_board_link(rsp, list.page[i].board);
        rsp << "</h2></li>";
      }
      rsp << "</ol>";
    }

    static auto write_sort_options(
      Response& rsp,
      std::string_view sort_name,
      SortFormType type,
      bool show_posts = true,
      bool show_images = true,
      bool show_nsfw = true
    ) noexcept -> void {
      rsp << R"(<details class="sort-options"><summary>Sort and Filter ()" << sort_name
        << R"()</summary><form class="sort-form" method="get">)";
      if (type != SortFormType::Comments) {
        rsp << R"(<label for="type"><span>Show</span><select name="type"><option value="posts")"
          << (show_posts ? " selected" : "") << R"(>Posts</option><option value="comments")"
          << (show_posts ? "" : " selected") << R"(>Comments</option></select></label>)";
      }
      rsp << R"(<label for="sort"><span>Sort</span><select name="sort">)";
      if (type == SortFormType::Board) {
        rsp << R"(<option value="Active")" << (sort_name == "Active" ? " selected" : "") << ">Active</option>";
      }
      if (type != SortFormType::User) {
        rsp << R"(<option value="Hot")" << (sort_name == "Hot" ? " selected" : "") << ">Hot</option>";
      }
      rsp << R"(<option value="New")" << (sort_name == "New" ? " selected" : "") << ">New</option>"
        R"(<option value="Old")" << (sort_name == "Old" ? " selected" : "") << ">Old</option>";
      if (type == SortFormType::Board) {
        rsp << R"(<option value="MostComments")" << (sort_name == "MostComments" ? " selected" : "") << ">Most Comments</option>"
          R"(<option value="NewComments")" << (sort_name == "NewComments" ? " selected" : "") << ">New Comments</option>"
          R"(<option value="TopAll")" << (sort_name == "TopAll" ? " selected" : "") << ">Top All</option>"
          R"(<option value="TopYear")" << (sort_name == "TopYear" ? " selected" : "") << ">Top Year</option>"
          R"(<option value="TopSixMonths")" << (sort_name == "TopSixMonths" ? " selected" : "") << ">Top Six Months</option>"
          R"(<option value="TopThreeMonths")" << (sort_name == "TopThreeMonths" ? " selected" : "") << ">Top Three Months</option>"
          R"(<option value="TopMonth")" << (sort_name == "TopMonth" ? " selected" : "") << ">Top Month</option>"
          R"(<option value="TopWeek")" << (sort_name == "TopWeek" ? " selected" : "") << ">Top Week</option>"
          R"(<option value="TopDay")" << (sort_name == "TopDay" ? " selected" : "") << ">Top Day</option>"
          R"(<option value="TopTwelveHour")" << (sort_name == "TopTwelveHour" ? " selected" : "") << ">Top Twelve Hour</option>"
          R"(<option value="TopSixHour")" << (sort_name == "TopSixHour" ? " selected" : "") << ">Top Six Hour</option>"
          R"(<option value="TopHour")" << (sort_name == "TopHour" ? " selected" : "") << ">Top Hour</option>";
      } else {
        rsp << R"(<option value="Top")" << (sort_name == "Top" ? " selected" : "") << ">Top</option>";
      }
      rsp << R"(</select></label><label for="si"><input name="si" type="checkbox" value="1")"
        << (show_images ? " checked" : "") << R"(> Show Images</label>)";
      if (type != SortFormType::Comments) {
        rsp << R"(<label for="sn"><input name="sn" type="checkbox" value="1")"
          << (show_nsfw ? " checked" : "") << R"(> Show NSFW</label>)";
      }
      rsp << R"(<input type="submit" value="Apply"></form></details>)";
    }

    static auto write_vote_buttons(
      Response& rsp,
      uint64_t post_id,
      int64_t karma,
      Vote your_vote
    ) noexcept -> void {
      const auto id = hexstring(post_id);
      rsp << R"(<form class="vote-buttons" id="votes-)" << id
        << R"(" method="post" action="/do/vote"><input type="hidden" name="post" value=")" << id
        << R"("><output class="karma" id="karma-)" << id << R"(">)" << karma
        << R"(</output><label class="upvote"></span><button type="submit" name="vote")"
        << (your_vote == Upvote ? R"( class="voted")" : "")
        << R"( value="1"><span class="a11y">Upvote</span></button></label><label class="downvote"><button type="submit" name="vote")"
        << (your_vote == Downvote ? R"( class="voted")" : "") << R"( value="-1"><span class="a11y">Downvote</span></button></label></form>)";
    }

    static auto write_page_list(
      Response& rsp,
      const ListPagesResponse& list,
      bool show_images = true
    ) noexcept -> void {
      rsp << R"(<ol class="page-list">)";
      for (size_t i = 0; i < list.size; i++) {
        const auto page = &list.page[i];
        const auto id = hexstring(page->id);
        rsp << R"(<li><article class="page" id="page-)" << id <<
          R"("><h2 class="page-title"><a class="page-title-link" href=")";
        if (page->page->content_url()) {
          rsp << Escape{page->page->content_url()->string_view()};
        } else {
          rsp << "/post/" << id;
        }
        rsp << R"(">)" << Escape{page->page->title()->string_view()} << "</a></h2>";
        // TODO: page-source (link URL)
        // TODO: thumbnail
        rsp << R"(<div class="thumbnail"><svg class="icon"><use href="/static/feather-sprite.svg#)"
          << (page->page->nsfw() ? "alert-octagon" : (page->page->content_url() ? "link" : "file-text"))
          << R"("></svg></div><div class="page-info">)";
        if (page->page->nsfw()) {
          rsp << R"(<abbr title="Not Safe For Work" class="nsfw-tag">NSFW</abbr> )";
        }
        rsp << R"(submitted )" << Escape{relative_time(page->page->created_at())} << " by ";
        write_user_link(rsp, page->author);
        rsp << " to ";
        write_board_link(rsp, page->board);
        rsp << "</div>";
        write_vote_buttons(rsp, page->id, page->stats->karma(), page->your_vote);
        rsp << R"(<div class="controls"><a id="comment-link-)" << id
          << R"(" href="/post/)" << id << R"(#comments">)" << page->stats->descendant_count()
          << (page->stats->descendant_count() == 1 ? " comment" : " comments")
          << R"(</a><div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
            R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
          << R"("><button type="submit" formaction="/do/save">Save</button>)"
            R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
          << R"(">Report</a></form></div></div></article>)";
      }
      rsp << "</ol>";
    }

    static auto write_note_list(
      Response& rsp,
      const ListNotesResponse& list
    ) noexcept -> void {
      rsp << R"(<ol class="note-list">)";
      for (size_t i = 0; i < list.size; i++) {
        const auto note = &list.page[i];
        const auto id = hexstring(note->id);
        rsp << R"(<li><article class="note" id="note-)" << id
          << R"("><h2 class="note-info"><a href="/comment/)" << id << R"(">)";
        write_user_link(rsp, note->author);
        rsp << " commented " << Escape{relative_time(note->note->created_at())}
          << R"( on <a href="/post/)" << hexstring(note->note->page())
          << R"(">)" << Escape{note->page->title()->string_view()}
          << R"(</a></h2><div class="note-content">)" << note->note->content_safe()->string_view() << "</div>";
        write_vote_buttons(rsp, note->id, note->stats->karma(), note->your_vote);
        rsp << R"(<div class="controls"><a id="comment-link-)" << id
          << R"(" href="/comment/)" << id << R"(#replies">)" << note->stats->child_count()
          << (note->stats->child_count() == 1 ? " reply" : " replies")
          << R"(</a><div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
            R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
          << R"("><button type="submit" formaction="/do/save">Save</button>)"
            R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
          << R"(">Report</a></form></div></div></article>)";
      }
      rsp << "</ol>";
    }

    auto serve_static(
      App& app,
      string filename,
      string mimetype,
      const unsigned char* src,
      size_t len
    ) noexcept -> void {
      const auto hash = hexstring(XXH3_64bits(src, len), true);
      app.get("/static/" + filename, [src, len, mimetype, hash](auto* res, auto* req) {
        if (req->getHeader("if-none-match") == hash) {
          res->writeStatus(http_status(304))->end();
        } else {
          res->writeHeader("Content-Type", mimetype)
            ->writeHeader("Etag", hash)
            ->end(string_view(reinterpret_cast<const char*>(src), len));
        }
      });
    }

    auto register_routes(App& app) -> void {
      app.get("/", safe_page([](auto self, auto page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        optional<LocalUserDetailResponse> login;
        ListBoardsResponse boards;
        page([&](auto& req) {
          site = self->controller->site_detail();
          login = self->get_logged_in_user(txn, req);
          boards = self->controller->list_local_boards(txn);
        }, [&](auto& rsp) {
          write_html_header(rsp, site, login, {"/"}, {site->name});
          rsp << "<div>";
          write_sidebar(rsp, site, login);
          rsp << "<main>";
          write_board_list(rsp, boards);
          rsp << "</main></div>";
          rsp.end(HTML_FOOTER);
        });
      }));
      app.get("/b/:name", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        optional<LocalUserDetailResponse> login;
        BoardDetailResponse board;
        ListPagesResponse pages;
        ListNotesResponse notes;
        std::string_view sort_str;
        bool show_posts, show_images, show_nsfw;
        page([&](Request& req) {
          const auto name = req.getParameter(0);
          const auto board_id = txn.get_board_id(name);
          if (!board_id) throw ControllerError("Board name does not exist", 404);
          // TODO: Get sort and filter settings from user
          show_posts = req.getQuery("type") != "comments";
          sort_str = req.getQuery("sort");
          const auto sort = Controller::parse_sort_type(sort_str);
          const auto from = Controller::parse_hex_id(std::string(req.getQuery("from")));
          show_images = req.getQuery("si") == "1" || sort_str.empty();
          show_nsfw = req.getQuery("sn") == "1" || sort_str.empty();
          site = self->controller->site_detail();
          login = self->get_logged_in_user(txn, req);
          board = self->controller->board_detail(txn, *board_id);
          if (show_posts) {
            pages = self->controller->list_board_pages(txn, *board_id, sort, login ? (*login).id : 0, !show_nsfw, from);
          } else {
            notes = self->controller->list_board_notes(txn, *board_id, sort, login ? (*login).id : 0, !show_nsfw, from);
          }
        }, [&](auto& rsp) {
          write_html_header(rsp, site, login, {"/b/" + board.board->name()->str()}, {board.board->name()->string_view()});
          rsp << "<div>";
          write_sidebar(rsp, site, login, {board});
          rsp << "<main>";
          write_sort_options(rsp, sort_str.empty() ? "Hot" : sort_str, SortFormType::Board, show_posts, show_images, show_nsfw);
          if (show_posts) write_page_list(rsp, pages, show_images);
          else write_note_list(rsp, notes);
          rsp << "</main></div>";
          rsp.end(HTML_FOOTER);
        });
      }));
      app.get("/u/:name", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        optional<LocalUserDetailResponse> login;
        UserDetailResponse user;
        ListPagesResponse pages;
        ListNotesResponse notes;
        std::string_view sort_str;
        bool show_posts, show_images, show_nsfw;
        page([&](Request& req) {
          const auto name = req.getParameter(0);
          const auto user_id = txn.get_user_id(name);
          if (!user_id) throw ControllerError("User does not exist", 404);
          // TODO: Get sort and filter settings from user
          show_posts = req.getQuery("type") != "comments";
          sort_str = req.getQuery("sort");
          const auto sort = Controller::parse_user_post_sort_type(sort_str);
          const auto from = Controller::parse_hex_id(std::string(req.getQuery("from")));
          show_images = req.getQuery("si") == "1" || sort_str.empty();
          show_nsfw = req.getQuery("sn") == "1" || sort_str.empty();
          site = self->controller->site_detail();
          login = self->get_logged_in_user(txn, req);
          user = self->controller->user_detail(txn, *user_id);
          if (show_posts) {
            pages = self->controller->list_user_pages(txn, *user_id, sort, login ? (*login).id : 0, !show_nsfw, from);
          } else {
            notes = self->controller->list_user_notes(txn, *user_id, sort, login ? (*login).id : 0, !show_nsfw, from);
          }
        }, [&](auto& rsp) {
          write_html_header(rsp, site, login, {"/u/" + user.user->name()->str()}, {user.user->name()->string_view()});
          rsp << "<div>";
          write_sidebar(rsp, site, login);
          rsp << "<main>";
          write_sort_options(rsp, sort_str.empty() ? "New" : sort_str, SortFormType::User, show_posts, show_images, show_nsfw);
          if (show_posts) write_page_list(rsp, pages, show_images);
          else write_note_list(rsp, notes);
          rsp << "</main></div>";
          rsp.end(HTML_FOOTER);
        });
      }));
      app.get("/login", safe_page([](auto self, auto page) {
        const SiteDetail* site;
        bool logged_in;
        page([&](auto& req) {
          auto txn = self->controller->open_read_txn();
          site = self->controller->site_detail();
          logged_in = !!self->get_logged_in_user(txn, req);
        }, [&](auto& rsp) {
          if (logged_in) {
            rsp.writeStatus(http_status(302));
            rsp.writeHeader("Location", "/");
          } else {
            write_html_header(rsp, site, {}, {"/login"}, {"Login"});
            rsp << LOGIN_FORM;
            rsp.end(HTML_FOOTER);
          }
        });
      }));
      serve_static(app, "default-theme.css", "text/css; charset=utf-8", default_theme_css, default_theme_css_len);
      serve_static(app, "htmx.min.js", "text/javascript; charset=utf-8", htmx_min_js, htmx_min_js_len);
      serve_static(app, "feather-sprite.svg", "image/svg+xml; charset=utf-8", feather_sprite_svg, feather_sprite_svg_len);
    }
  };

  template <bool SSL> auto webapp_routes(
    uWS::TemplatedApp<SSL>& app,
    std::shared_ptr<Controller> controller
  ) -> void {
    auto router = std::make_shared<Webapp<SSL>>(controller);
    router->register_routes(app);
  }

  template auto webapp_routes<true>(
    uWS::TemplatedApp<true>& app,
    std::shared_ptr<Controller> controller
  ) -> void;

  template auto webapp_routes<false>(
    uWS::TemplatedApp<false>& app,
    std::shared_ptr<Controller> controller
  ) -> void;
}
