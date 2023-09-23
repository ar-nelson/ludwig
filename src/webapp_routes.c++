#include "webapp_routes.h++"
#include <chrono>
#include <iterator>
#include <regex>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/chrono.h>
#include "xxhash.h"
#include "generated/default-theme.css.h"
#include "generated/htmx.min.js.h"
#include "generated/feather-sprite.svg.h"
#include "webutil.h++"

using std::optional, std::nullopt, std::string, std::string_view;

#define COOKIE_NAME "ludwig_session"

namespace Ludwig {
  static const std::regex cookie_regex(
    R"((?:^|;)\s*)" COOKIE_NAME R"(\s*=\s*([^;]+))",
    std::regex_constants::ECMAScript
  );

  static inline auto hexstring(uint64_t n, bool padded = false) -> std::string {
    if (padded) return fmt::format("{:016x}", n);
    else return fmt::format("{:x}", n);
  }

  enum class SortFormType {
    Board,
    Comments,
    User
  };

  static inline auto display_name(const User* user) -> string_view {
    if (user->display_name()) return user->display_name()->string_view();
    const auto name = user->name()->string_view();
    return name.substr(0, name.find('@'));
  }

  static inline auto display_name(const Board* board) -> string_view {
    if (board->display_name()) return board->display_name()->string_view();
    const auto name = board->name()->string_view();
    return name.substr(0, name.find('@'));
  }

  struct QueryString {
    string_view query;
    inline auto required_hex_id(string_view key) -> uint64_t {
      try {
        return std::stoull(std::string(uWS::getDecodedQueryValue(key, query)), nullptr, 16);
      } catch (...) {
        throw ControllerError(fmt::format("Invalid or missing '{}' parameter", key).c_str(), 400);
      }
    }
    inline auto required_string(string_view key) -> string_view {
      auto s = uWS::getDecodedQueryValue(key, query);
      if (s.empty()) ControllerError(fmt::format("Invalid or missing '{}' parameter", key).c_str(), 400);
      return s;
    }
    inline auto optional_string(string_view key) -> optional<string_view> {
      auto s = uWS::getDecodedQueryValue(key, query);
      if (s.empty()) return {};
      return s;
    }
    inline auto optional_bool(string_view key) -> bool {
      return uWS::getDecodedQueryValue(key, query) == "1";
    }
  };

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
        rsp.writeHeader("Content-Type", TYPE_HTML);
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
      std::chrono::time_point<std::chrono::steady_clock> start;
    public:
      SafePage(Self self, Request& req, Response& rsp) : self(self), req(req), rsp(rsp), start(std::chrono::steady_clock::now()) {}

      inline auto operator()(
        ReadTxn& txn,
        std::function<void (Request&, Controller::Login)> before,
        std::function<void (Response&, Controller::Login)> after
      ) -> void {
        optional<uint64_t> old_session;
        optional<LoginResponse> new_session;
        optional<LocalUserDetailResponse> logged_in_user;
        try {
          auto cookies = req.getHeader("cookie");
          std::match_results<string_view::const_iterator> match;
          if (std::regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) {
            try {
              old_session = std::stoull(match[1], nullptr, 16);
              new_session = self->controller->validate_or_regenerate_session(
                txn, *old_session, rsp.getRemoteAddressAsText(), req.getHeader("user-agent")
              );
              if (new_session) {
                logged_in_user = self->controller->local_user_detail(txn, new_session->user_id);
              }
            } catch (...) {}
          }
          before(req, logged_in_user);
        } catch (ControllerError e) {
          self->error_page(rsp, e);
          return;
        } catch (...) {
          auto eptr = std::current_exception();
          try {
            std::rethrow_exception(eptr);
          } catch (std::exception& e) {
            spdlog::error("Unhandled exception in webapp route: {}", e.what());
          } catch (...) {
            spdlog::error("Unhandled exception in webapp route, no information available");
          }
          self->error_page(rsp, ControllerError("Unhandled internal exception", 500));
          return;
        }

        // FIXME: Sometimes this will screw up redirects because 200 OK will be written first.
        // It's unlikely, but can result in a blank page that doesn't redirect.
        if (old_session) {
          if (!new_session) {
            spdlog::debug("Auth cookie is invalid; requesting deletion");
            rsp.writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
          } else if (new_session->session_id != *old_session) {
            spdlog::debug("Regenerated session {:x} as {:x}", *old_session, new_session->session_id);
            rsp.writeHeader("Set-Cookie",
              fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
                new_session->session_id, fmt::gmtime((time_t)new_session->expiration)));
          }
        }

        rsp.cork([&]() {
          after(rsp, logged_in_user);
        });
      }

      inline auto time_elapsed() {
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      }
    };

    inline auto safe_page(std::function<void (Self, SafePage)> fn) {
      return [self = this->shared_from_this(), fn](Response* rsp, Request* req) {
        fn(self, SafePage(self, *req, *rsp));
      };
    }

    inline auto action_page(std::function<void (Self, Request&, Response&, QueryString, uint64_t)> fn, bool require_login = true) {
      return [self = this->shared_from_this(), fn, require_login](Response* rsp, Request* req) {
        std::string buffer = "?";
        rsp->onData([self, rsp, req, fn, require_login, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
          buffer.append(data.data(), data.length());
          if (!last) return;
          optional<ControllerError> err;
          try {
            optional<uint64_t> logged_in_user;
            if (require_login) {
              try {
                auto txn = self->controller->open_read_txn();
                auto cookies = req->getHeader("cookie");
                std::match_results<string_view::const_iterator> match;
                if (std::regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) {
                  logged_in_user = self->controller->validate_session(txn, std::stoull(match[1], nullptr, 16));
                }
              } catch (...) {}
            }
            if (logged_in_user || !require_login) {
              fn(self, *req, *rsp, {string_view(buffer)}, logged_in_user.value_or(0));
            } else if (req->getHeader("hx-target").empty()) {
              rsp->writeStatus(http_status(303));
              rsp->writeHeader("Location", "/login");
              rsp->endWithoutBody({}, true);
            } else {
              throw ControllerError("Login is required", 401);
            }
            return;
          } catch (ControllerError e) {
            err = e;
          } catch (...) {
            auto eptr = std::current_exception();
            try {
              std::rethrow_exception(eptr);
            } catch (std::exception& e) {
              spdlog::error("Unhandled exception in webapp route: {}", e.what());
            } catch (...) {
              spdlog::error("Unhandled exception in webapp route, no information available");
            }
            err = ControllerError("Unhandled internal exception", 500);
          }
          rsp->cork([rsp, self, &err]() {
            self->error_page(*rsp, *err);
          });
        });
        rsp->onAborted([rsp](){
          rsp->writeStatus(http_status(400));
          rsp->endWithoutBody({}, true);
        });
      };
    }

    inline auto write_qualified_display_name(Response& rsp, const User* user) -> void {
      const auto name = user->name()->string_view();
      if (user->display_name()) {
        rsp << user->display_name()->string_view();
        const auto at_index = name.find('@');
        if (at_index != string_view::npos) rsp << name.substr(at_index);
      } else {
        rsp << name;
      }
    }

    inline auto write_qualified_display_name(Response& rsp, const Board* board) -> void {
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

    static auto write_html_header(
      Response& rsp,
      const SiteDetail* site,
      const optional<LocalUserDetailResponse>& logged_in_user,
      HtmlHeaderOptions opt
    ) noexcept -> void {
      rsp.writeHeader("Content-Type", TYPE_HTML);
      rsp << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
        R"(<meta name="viewport" content="width=device-width,initial-scale=1,shrink-to-fit=no">)"
        R"(<meta name="referrer" content="same-origin"><title>)"
        << Escape{site->name};
      if (opt.page_title) rsp << " - " << Escape{*opt.page_title};
      else if (opt.banner_title) rsp << " - " << Escape{*opt.banner_title};
      rsp << R"(</title><link rel="stylesheet" href="/static/default-theme.css">)"
        R"(<script src="/static/htmx.min.js"></script>)";
      if (opt.canonical_path) {
        const auto canonical_path = *opt.canonical_path;
        rsp << R"(<link rel="canonical" href=")" << Escape{site->domain} << Escape{canonical_path} <<
          R"("><meta property="og:url" content=")" << Escape{site->domain} << Escape{canonical_path} <<
          R"("><meta property="twitter:url" content=")" << Escape{site->domain} << Escape{canonical_path} <<
          R"(">)";
      }
      if (opt.page_title) {
        const auto page_title = *opt.page_title;
        rsp << R"(<meta property="title" href=")" << Escape{site->name} << " - " << Escape{page_title} <<
          R"("><meta property="og:title" content=")" << Escape{site->name} << " - " << Escape{page_title} <<
          R"("><meta property="twitter:title" content=")" << Escape{site->name} << " - " << Escape{page_title} <<
          R"("><meta property="og:type" content="website">)";
      }
      if (opt.card_image) {
        const auto card_image = *opt.card_image;
        rsp << R"(<meta property="og:image" content=")" << Escape{card_image} <<
          R"("><meta property="twitter:image" content=")" << Escape{card_image} <<
          R"("><meta property="twitter:card" content="summary_large_image">)";
      }
      rsp << R"(</head><body><nav class="topbar"><div class="site-name">🎹 )"
        << Escape{site->name} // TODO: Site icon
        << R"(</div><ul class="quick-boards">)"
          R"(<li><a href="/">Home</a>)"
          R"(<li><a href="/feed/local">Local</a>)"
          R"(<li><a href="/feed/federated">All</a>)"
          R"(<li><a href="/boards">Boards</a>)";
      if (logged_in_user) {
        rsp << R"(<li><a href="/subscriptions">Subscriptions</a></ul><ul>)"
          R"(<li id="topbar-user"><a href="/u/)" << Escape{logged_in_user->user->name()->string_view()}
          << R"(">)" << Escape{logged_in_user->user->display_name()->string_view()}
          << R"(</a> ()" << (logged_in_user->stats->thread_karma() + logged_in_user->stats->comment_karma())
          << R"()<li><a href="/settings">Settings</a><li><a href="/logout">Logout</a></ul></nav>)";
      } else {
        rsp << R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)";
      }
      if (opt.banner_title) {
        rsp << R"(<header id="page-header")";
        if (opt.banner_image) {
          rsp << R"( class="banner-image" style="background-image:url(')" << Escape{*opt.banner_image} << R"(');")";
        }
        rsp << R"(><h1>)";
        if (opt.banner_link) rsp << R"(<a class="page-header-link" href=")" << Escape{*opt.banner_link} << R"(">)";
        rsp << Escape{*opt.banner_title};
        if (opt.banner_link) rsp << "</a>";
        rsp << "</h1></header>";
      }
    }

    static inline auto end_with_html_footer(Response& rsp, long time_elapsed) noexcept -> void {
      rsp.end(fmt::format(
        R"(<div class="spacer"></div><footer><small>Powered by Ludwig · Generated in {:L}μs</small></footer></body></html>)",
        time_elapsed
      ));
    }

    static inline auto hide_cw_posts(const optional<LocalUserDetailResponse>& logged_in_user) -> bool {
      if (!logged_in_user) return false;
      return logged_in_user->local_user->hide_cw_posts();
    }

    static auto write_subscribe_button(
      Response& rsp,
      uint64_t board_id,
      bool is_unsubscribe
    ) noexcept -> void {
      const string_view action = is_unsubscribe ? "/do/unsubscribe" : "/do/subscribe";
      rsp << R"(<form method="post" action=")" << action << R"(" hx-post=")" << action
        << R"(" hx-swap="outerHTML"><button type="submit" class="big-button">)"
        << (is_unsubscribe ? "Unsubscribe" : "Subscribe")
        << R"(</button><input type="hidden" name="board" value=")"
        << hexstring(board_id) << R"("></form>)";
    }

    static constexpr string_view LOGIN_FIELDS =
      R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)"
      R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)"
      R"(<label for="actual_username"><span>Username or email</span><input type="text" name="actual_username" id="actual_username" placeholder="Username or email"></label>)"
      R"(<label for="password"><span>Password</span><input type="password" name="password" id="password" placeholder="Password"></label>)"
      R"(<label for="remember"><span>Remember me</span><input type="checkbox" name="remember" id="remember"></label>)";

    static auto write_sidebar(
      Response& rsp,
      const SiteDetail* site,
      const optional<LocalUserDetailResponse>& logged_in_user,
      const optional<BoardDetailResponse>& board = {}
    ) noexcept -> void {
      rsp << R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
        R"(<form action="/search" id="search-form">)"
        R"(<label for="search"><span class="a11y">Search</span><input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)";
      const auto hide_cw = hide_cw_posts(logged_in_user);
      const auto board_name = Escape{board ? board->board->display_name() ? board->board->display_name()->string_view() : board->board->name()->string_view() : ""};
      if (board) rsp << R"(<input type="hidden" name="board" value=")" << board->id << R"(">)";
      if (!hide_cw || board) {
        rsp << R"(<details id="search-options"><summary>Search Options</summary><fieldset>)";
        if (board) {
          rsp << R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to )"
            << board_name
            << "</label>";
        }
        if (!hide_cw) {
          rsp << R"(<label for="include_cw"><input type="checkbox" name="include_cw" id="include_cw" checked> Include results with Content Warnings</label>)";
        }
        rsp << "</fieldset></details>";
      }
      rsp << "</form></section>";
      if (!logged_in_user) {
        rsp << R"(<section id="login-section"><h2>Login</h2><form method="post" action="/login" id="login-form">)"
          << LOGIN_FIELDS << R"(<input type="submit" value="Login" class="big-button"></form>)"
          R"(<a href="/register" class="big-button">Register</a></section>)";
      } else if (board) {
        rsp << R"(<section id="actions-section"><h2>Actions</h2>)";
        write_subscribe_button(rsp, board->id, board->subscribed);
        if (board && Controller::can_create_thread(*board, logged_in_user)) {
          rsp << R"(<a class="big-button" href="/b/)" << Escape{board->board->name()->string_view()}
            << R"(/create_thread">Submit a new link</a><a class="big-button" href="/b/)" << Escape{board->board->name()->string_view()}
            << R"(/create_thread?text=1">Submit a new text post</a>)";
        }
        rsp << "</section>";
      }
      if (board) {
        rsp << R"(<section id="board-sidebar"><h2>)" << board_name << "</h2>";
        // TODO: Banner image
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

    static auto write_datetime(Response& rsp, uint64_t timestamp) noexcept -> void {
      rsp << fmt::format(R"(<time datetime="{:%FT%TZ}" title="{:%D %r %Z}">{}</time>)",
        fmt::gmtime((time_t)timestamp), fmt::localtime((time_t)timestamp), relative_time(timestamp));
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
      if (board->content_warning()) {
        rsp << R"( <abbr class="content-warning-label" title="Content Warning: )" <<
          Escape{board->content_warning()->string_view()} << R"(">CW</abbr>)";
      }
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
      bool can_hide_cws,
      bool show_posts,
      bool show_images,
      bool show_cws
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
      rsp << R"(</select></label><label for="images"><input name="images" type="checkbox" value="1")"
        << (show_images ? " checked" : "") << R"(> Show images</label>)";
      if (can_hide_cws) {
        rsp << R"(<label for="cws"><input name="cws" type="checkbox" value="1")"
          << (show_cws ? " checked" : "") << R"(> Show posts with Content Warnings</label>)";
      }
      rsp << R"(<input type="submit" value="Apply"></form></details>)";
    }

    template <typename T> static auto write_vote_buttons(
      Response& rsp,
      const T* entry,
      Controller::Login login
    ) noexcept -> void {
      const auto id = hexstring(entry->id);
      const auto can_upvote = Controller::can_upvote(*entry, login),
        can_downvote = Controller::can_downvote(*entry, login);
      if (can_upvote || can_downvote) {
        rsp << R"(<form class="vote-buttons" id="votes-)" << id
          << R"(" method="post" action="/do/vote" hx-post="/do/vote" hx-swap="outerHTML"><input type="hidden" name="post" value=")" << id
          << R"("><output class="karma" id="karma-)" << id << R"(">)" << entry->stats->karma()
          << R"(</output><label class="upvote"><button type="submit" name="vote" )"
          << (can_upvote ? "" : "disabled ")
          << (entry->your_vote > 0 ? R"(class="voted" value="0")" : R"(value="1")")
          << R"(><span class="a11y">Upvote</span></button></label><label class="downvote"><button type="submit" name="vote" )"
          << (can_downvote ? "" : "disabled ")
          << (entry->your_vote < 0 ? R"(class="voted" value="0")" : R"(value="-1")") << R"(><span class="a11y">Downvote</span></button></label></form>)";
      } else {
        rsp << R"(<div class="vote-buttons" id="votes-)" << id
          << R"("><output class="karma" id="karma-)" << id << R"(">)" << entry->stats->karma()
          << R"(</output><div class="upvote"><button type="button" disabled><span class="a11y">Upvote</span></button></div>)"
          << R"(<div class="downvote"><button type="button" disabled><span class="a11y">Downvote</span></button></div></div>)";
      }
    }

    static inline auto write_pagination(
      Response& rsp,
      string_view base_url,
      bool is_first,
      optional<uint64_t> next
    ) noexcept -> void {
      const auto sep = base_url.find('?') == string_view::npos ? "?" : "&";
      rsp << R"(<div class="pagination" id="pagination" hx-swap-oob="true")";
      if (next) {
        rsp << R"( hx-trigger="revealed" hx-get=")" << Escape{base_url} << sep << "from=" << hexstring(*next)
          << R"(" hx-target="#infinite-scroll-list" hx-swap="beforeend")";
      }
      rsp << ">";
      if (!is_first) {
        rsp << R"(<a class="big-button" href=")" << Escape{base_url} << R"(">← First</a>)";
      }
      if (next) {
        rsp << R"(<a class="big-button" href=")" << Escape{base_url} << sep << "from=" << hexstring(*next) << R"(">Next →</a>)";
      }
      if (is_first && !next) {
        rsp << "<small>And that's it!</small>";
      }
      rsp << R"(<div class="spinner">Loading…</div></div>)";
    }

    static auto write_thread_list(
      Response& rsp,
      const ListThreadsResponse& list,
      string_view base_url,
      Controller::Login login,
      bool include_ol,
      bool show_user,
      bool show_board,
      bool show_images
    ) noexcept -> void {
      if (include_ol) rsp << R"(<ol class="thread-list" id="infinite-scroll-list">)";
      for (size_t i = 0; i < list.size; i++) {
        const auto thread = &list.page[i];
        const auto id = hexstring(thread->id);
        rsp << R"(<li><article class="thread" id="thread-)" << id <<
          R"("><h2 class="thread-title"><a class="thread-title-link" href=")";
        if (thread->thread->content_url()) {
          rsp << Escape{thread->thread->content_url()->string_view()};
        } else {
          rsp << "/thread/" << id;
        }
        rsp << R"(">)" << Escape{thread->thread->title()->string_view()} << "</a></h2>";
        // TODO: thread-source (link URL)
        // TODO: thumbnail
        rsp << R"(<div class="thumbnail"><svg class="icon"><use href="/static/feather-sprite.svg#)"
          << (thread->thread->content_warning() ? "alert-octagon" : (thread->thread->content_url() ? "link" : "file-text"))
          << R"("></svg></div><div class="thread-info">)";
        if (thread->thread->content_warning()) {
          rsp << R"(<p class="content-warning"><strong class="content-warning-label">Content Warning<span class="a11y">:</span></strong> )"
            << Escape{thread->thread->content_warning()->string_view()} << "</p>";
        }
        rsp << "submitted ";
        write_datetime(rsp, thread->thread->created_at());
        if (show_user) {
          rsp << " by ";
          write_user_link(rsp, thread->author);
        }
        if (show_board) {
          rsp << " to ";
          write_board_link(rsp, thread->board);
        }
        rsp << "</div>";
        write_vote_buttons(rsp, thread, login);
        rsp << R"(<div class="controls"><a id="comment-link-)" << id
          << R"(" href="/thread/)" << id << R"(#comments">)" << thread->stats->descendant_count()
          << (thread->stats->descendant_count() == 1 ? " comment" : " comments")
          << R"(</a><div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
            R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
          << R"("><button type="submit" formaction="/do/save">Save</button>)"
            R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
          << R"(">Report</a></form></div></div></article>)";
      }
      if (include_ol) rsp << "</ol>";
      write_pagination(rsp, base_url, list.is_first, list.next);
    }

    static auto write_comment_list(
      Response& rsp,
      const ListCommentsResponse& list,
      string_view base_url,
      Controller::Login login,
      bool show_user = true,
      bool show_thread = true
    ) noexcept -> void {
      rsp << R"(<ol class="comment-list">)";
      for (size_t i = 0; i < list.size; i++) {
        const auto comment = &list.page[i];
        const auto id = hexstring(comment->id);
        rsp << R"(<li><article class="comment" id="comment-)" << id
          << R"("><h2 class="comment-info">)";
        if (show_user) {
          write_user_link(rsp, comment->author);
          rsp << " ";
        }
        rsp << "commented ";
        write_datetime(rsp, comment->comment->created_at());
        if (show_thread) {
          rsp << R"( on <a href="/thread/)" << hexstring(comment->comment->thread()) << R"(">)"
            << Escape{comment->thread->title()->string_view()} << "</a>";
          if (comment->thread->content_warning()) {
            rsp << R"( <abbr class="content-warning-label" title="Content Warning: )" <<
              Escape{comment->thread->content_warning()->string_view()} << R"(">CW</abbr>)";
          }
        }
        rsp << R"(</h2><div class="comment-content">)" << comment->comment->content_safe()->string_view() << "</div>";
        write_vote_buttons(rsp, comment, login);
        rsp << R"(<div class="controls"><a id="comment-link-)" << id
          << R"(" href="/comment/)" << id << R"(#replies">)" << comment->stats->child_count()
          << (comment->stats->child_count() == 1 ? " reply" : " replies")
          << R"(</a><div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
            R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
          << R"("><button type="submit" formaction="/do/save">Save</button>)"
            R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
          << R"(">Report</a></form></div></div></article>)";
      }
      rsp << "</ol>";
      write_pagination(rsp, base_url, list.is_first, list.next);
    }

    static auto write_comment_tree(
      Response& rsp,
      const CommentTree& comments,
      uint64_t root,
      Controller::Login login,
      bool is_thread = true
    ) noexcept -> void {
      // TODO: Include existing query params
      auto range = comments.comments.equal_range(root);
      if (range.first == range.second) {
        if (is_thread) rsp << R"(<div class="no-comments">No comments</div>)";
        return;
      }
      rsp << R"(<ol class="comment-list" id="comments-)" << hexstring(root) << R"(">)";
      for (auto iter = range.first; iter != range.second; iter++) {
        const auto comment = &iter->second;
        const auto id = hexstring(comment->id);
        rsp << R"(<li><article class="comment-with-comments"><div class="comment" id=")" << id
          << R"("><h3 class="comment-info">)";
        write_user_link(rsp, comment->author);
        rsp << " commented ";
        write_datetime(rsp, comment->comment->created_at());
        rsp << R"(</h3><div class="comment-content">)" << comment->comment->content_safe()->string_view() << "</div>";
        write_vote_buttons(rsp, comment, login);
        rsp << R"(<div class="controls">)";
        if (Controller::can_reply_to(*comment, login)) {
          rsp << R"(<a href="/comment/)" << id << R"(#reply">Reply</a>)";
        }
        rsp << R"(<div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
            R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
          << R"("><button type="submit" formaction="/do/save">Save</button>)"
            R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
          << R"(">Report</a></form></div></div></div>)";
        const auto cont = comments.continued.find(comment->id);
        if (cont != comments.continued.end() && cont->second == 0) {
          rsp << R"(<div class="comments-continued" id="continue-)" << id << R"("><a href="/comment/)" << id
            << R"(">More comments…</a></div>)";
        } else if (comment->stats->child_count()) {
          rsp << R"(<section class="comments" aria-title="Replies">)";
          write_comment_tree(rsp, comments, comment->id, login, false);
          rsp << "</section>";
        }
        rsp << "</article>";
      }
      const auto cont = comments.continued.find(root);
      if (cont != comments.continued.end()) {
        rsp << R"(<li><div class="comments-continued" id="continue-)" << root << R"("><a href="/)"
          << (is_thread ? "thread" : "comment") << "/" << hexstring(root) << "?from=" << hexstring(cont->second)
          << R"(">More comments…</a></div>)";
      }
      rsp << "</ol>";
    }

    static auto write_reply_form(Response& rsp, uint64_t parent) noexcept -> void {
      rsp << R"(<form class="reply-form" method="post" action="/do/reply"><input type="hidden" name="parent" value=")"
        << hexstring(parent) << R"("><label for="text_content"><span>Reply</span>)"
        R"(<div><textarea name="text_content" placeholder="Write your reply here"></textarea>)"
        R"(<p><small><a href="https://www.markdownguide.org/cheat-sheet/" target="_blank">Markdown</a> formatting is supported.</small></p></div></label>)"
        R"(<label for="content_warning"><span>Content warning (optional)</span><input type="text" name="content_warning" id="content_warning"></label>)"
        R"(<input type="submit" value="Reply">)"
        R"(</form>)";
    }

    static auto write_thread_view(
      Response& rsp,
      const ThreadDetailResponse* thread,
      Controller::Login login,
      std::string_view sort_str = "",
      bool show_images = false,
      bool show_cws = false
    ) noexcept -> void {
      const auto id = hexstring(thread->id);
      rsp << R"(<article class="thread-with-comments"><div class="thread" id="thread-)" << id <<
        R"("><h2 class="thread-title">)";
      if (thread->thread->content_url()) {
        rsp << R"(<a class="thread-title-link" href=")" << Escape{thread->thread->content_url()->string_view()} << R"(">)";
      }
      rsp << Escape{thread->thread->title()->string_view()};
      if (thread->thread->content_url()) rsp << "</a>";
      rsp << "</h2>";
      // TODO: thread-source (link URL)
      // TODO: thumbnail
      rsp << R"(<div class="thumbnail"><svg class="icon"><use href="/static/feather-sprite.svg#)"
        << (thread->thread->content_warning() ? "alert-octagon" : (thread->thread->content_url() ? "link" : "file-text"))
        << R"("></svg></div><div class="thread-info">)";
      if (thread->thread->content_warning()) {
        rsp << R"(<p class="content-warning"><strong class="content-warning-label">Content Warning<span class="a11y">:</span></strong> )"
          << Escape{thread->thread->content_warning()->string_view()} << "</p>";
      }
      rsp << "submitted ";
      write_datetime(rsp, thread->thread->created_at());
      rsp << " by ";
      write_user_link(rsp, thread->author);
      rsp << " to ";
      write_board_link(rsp, thread->board);
      rsp << "</div>";
      write_vote_buttons(rsp, thread, login);
      rsp << R"(<div class="controls"><div class="controls-submenu-wrapper"><button type="button" class="controls-submenu-expand">More</button>)"
          R"(<form class="controls-submenu" method="post"><input type="hidden" name="post" value=")" << id
        << R"("><button type="submit" formaction="/do/save">Save</button>)"
          R"(<button type="submit" formaction="/do/hide">Hide</button><a target="_blank" href="/report_post/)" << id
        << R"(">Report</a></form></div></div></div>)";
      if (thread->thread->content_text_safe()) {
        rsp << R"(<div class="thread-content">)" << thread->thread->content_text_safe()->string_view()
          << "</div>";
      }
      rsp << R"(<section class="comments"><h2>)" << thread->stats->descendant_count() << R"( comments</h2>)";
      if (Controller::can_reply_to(*thread, login)) {
        write_reply_form(rsp, thread->id);
      }
      if (thread->stats->descendant_count()) {
        write_sort_options(
          rsp, sort_str.empty() ? "Hot" : sort_str, SortFormType::Comments,
          !thread->board->content_warning() && !thread->thread->content_warning(),
          false, show_images, show_cws
        );
        write_comment_tree(rsp, thread->comments, thread->id, login);
      }
      rsp << "</section></article>";
    }

    static auto write_login_form(Response& rsp, optional<string_view> error = {}) noexcept -> void {
      rsp << R"(<main><form class="form-page" method="post" action="/login">)";
      if (error) {
        rsp << R"(<p class="error-message">⚠️ )" << Escape{*error} << "</p>";
      }
      rsp << LOGIN_FIELDS << R"(<input type="submit" value="Login"></form></main>)";
    }

    static auto write_register_form(Response& rsp, optional<string_view> error = {}) noexcept -> void {
      rsp << R"(<main><form class="form-page" method="post" action="/do/register">)";
      if (error) {
        rsp << R"(<p class="error-message">⚠️ )" << Escape{*error} << "</p>";
      }
      rsp << R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)"
        R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)"
        R"(<label for="actual_username"><span>Username</span><input type="text" name="actual_username" id="actual_username"></label>)"
        R"(<label for="email"><span>Email address</span><input type="email" name="email" id="email"></label>)"
        R"(<label for="password"><span>Password</span><input type="password" name="password" id="password"></label>)"
        R"(<label for="confirm_password"><span>Confirm password</span><input type="password" name="confirm_password" id="confirm_password"></label>)"
        R"(<input type="submit" value="Register">)"
        R"(</form></main>)";
    }

    static auto write_create_board_form(
      Response& rsp,
      const LocalUserDetailResponse& login,
      optional<string_view> error = {}
    ) noexcept -> void {
      rsp << R"(<main><form class="form-page" method="post" action="/do/create_board"><h2>Create Board</h2>)";
      if (error) {
        rsp << R"(<p class="error-message">⚠️ )" << Escape{*error} << "</p>";
      }
      rsp << R"(<label for="name"><span>Name (URL)</span><div>/b/<input type="text" name="name" id="name" autocomplete="off" required></div></label>)"
        R"(<label for="display_name"><span>Display name</span><input type="text" name="display_name" id="display_name" autocomplete="off"></label>)"
        R"(<label for="private"><span>Private</span><input type="checkbox" name="private" id="private"></label>)"
        R"(<label for="restricted_posting"><span>Restrict posting to moderators</span><input type="checkbox" name="restricted_posting" id="restricted_posting"></label>)"
        R"(<label for="content_warning"><span>Content warning (optional)</span><input type="text" name="content_warning" id="content_warning" autocomplete="off"></label>)"
        R"(<input type="submit" value="Submit">)"
        R"(</form></main>)";
    }

    static auto write_create_thread_form(
      Response& rsp,
      bool show_url,
      const BoardDetailResponse& board,
      const LocalUserDetailResponse& login,
      optional<string_view> error = {}
    ) noexcept -> void {
      rsp << R"(<main><form class="form-page" method="post" action="/b/)" << Escape{board.board->name()->string_view()}
        << R"(/create_thread"><h2>Create Thread</h2>)";
      if (error) {
        rsp << R"(<p class="error-message">⚠️ )" << Escape{*error} << "</p>";
      }
      rsp << R"(<p class="thread-info">Posting as )";
      write_user_link(rsp, login.user);
      rsp << " to ";
      write_board_link(rsp, board.board);
      rsp << R"(</p><br><label for="title"><span>Title</span><input type="text" name="title" id="title" autocomplete="off" required></label>)";
      if (show_url) {
        rsp << R"(<label for="submission_url"><span>Submission URL</span><input type="text" name="submission_url" id="submission_url" autocomplete="off" required></label>)";
        rsp << R"(<label for="text_content"><span>Description (optional)</span><div><textarea name="text_content" id="text_content"></textarea>)";
      } else {
        rsp << R"(<label for="text_content"><span>Text content</span><div><textarea name="text_content" id="text_content" required></textarea>)";
      }
      rsp << R"(<small><a href="https://www.markdownguide.org/cheat-sheet/" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"
        R"(<label for="content_warning"><span>Content warning (optional)</span><input type="text" name="content_warning" id="content_warning" autocomplete="off"></label>)"
        R"(<input type="submit" value="Submit">)"
        R"(</form></main>)";
    }

    static auto write_edit_thread_form(
      Response& rsp,
      const ThreadListEntry& thread,
      const LocalUserDetailResponse& login,
      optional<string_view> error = {}
    ) noexcept -> void {
      rsp << R"(<main><form class="form-page" method="post" action="/thread/)" << hexstring(thread.id)
        << R"(/edit"><h2>Edit Thread</h2>)";
      if (error) {
        rsp << R"(<p class="error-message">⚠️ )" << Escape{*error} << "</p>";
      }
      rsp << R"(<p class="thread-info">Posted by )";
      write_user_link(rsp, login.user);
      rsp << " to ";
      write_board_link(rsp, thread.board);
      rsp << R"(</p><br><label for="title"><span>Title</span><input type="text" name="title" id="title" autocomplete="off" value=")"
        << Escape{thread.thread->title()->string_view()} << R"(" required></label>)"
        R"(<label for="text_content"><span>Text content</span><div><textarea name="text_content" id="text_content")"
        << (thread.thread->content_url() ? "" : " required") << ">" << Escape{thread.thread->content_text_raw()->string_view()}
        << R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"
        R"(<label for="content_warning"><span>Content warning (optional)</span><input type="text" name="content_warning" id="content_warning" autocomplete="off" value=")"
        << Escape{thread.thread->content_warning()->string_view()} << R"("></label>)"
        R"(<input type="submit" value="Submit">)"
        R"(</form></main>)";
    }

    static inline auto write_redirect_back(Request& req, Response& rsp) -> void {
      const auto referer = req.getHeader("referer");
      rsp.cork([&]() {
        if (referer.empty()) {
          rsp.writeStatus(http_status(202));
        } else {
          rsp.writeStatus(http_status(303));
          rsp.writeHeader("Location", referer);
        }
        rsp.endWithoutBody({}, true);
      });
    }

    auto serve_static(
      App& app,
      string_view filename,
      string_view mimetype,
      const unsigned char* src,
      size_t len
    ) noexcept -> void {
      const auto hash = hexstring(XXH3_64bits(src, len), true);
      app.get(fmt::format("/static/{}", filename), [src, len, mimetype, hash](auto* res, auto* req) {
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
      // -----------------------------------------------------------------------
      // STATIC FILES
      // -----------------------------------------------------------------------
      serve_static(app, "default-theme.css", TYPE_CSS,
        default_theme_css, default_theme_css_len);
      serve_static(app, "htmx.min.js", TYPE_JS,
        htmx_min_js, htmx_min_js_len);
      serve_static(app, "feather-sprite.svg", TYPE_SVG,
        feather_sprite_svg, feather_sprite_svg_len);

      // -----------------------------------------------------------------------
      // PAGES
      // -----------------------------------------------------------------------
      app.get("/", safe_page([](auto self, auto page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        ListBoardsResponse boards;
        page(txn, [&](auto&, auto&) {
          site = self->controller->site_detail();
          boards = self->controller->list_local_boards(txn);
        }, [&](auto& rsp, auto& login) {
          write_html_header(rsp, site, login, {
            .canonical_path = "/",
            .banner_title = site->name,
            .banner_link = "/",
          });
          rsp << "<div>";
          write_sidebar(rsp, site, login);
          rsp << "<main>";
          write_board_list(rsp, boards);
          rsp << "</main></div>";
          end_with_html_footer(rsp, page.time_elapsed());
        });
      }));
      app.get("/b/:name", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        BoardDetailResponse board;
        ListThreadsResponse threads;
        ListCommentsResponse comments;
        std::string_view sort_str;
        bool show_posts, show_images, show_cws, is_htmx;
        page(txn, [&](Request& req, auto& login) {
          const auto name = req.getParameter(0);
          const auto board_id = txn.get_board_id(name);
          if (!board_id) throw ControllerError("Board name does not exist", 404);
          // TODO: Get sort and filter settings from user
          show_posts = req.getQuery("type") != "comments";
          sort_str = req.getQuery("sort");
          const auto sort = Controller::parse_sort_type(sort_str);
          const auto from = Controller::parse_hex_id(std::string(req.getQuery("from")));
          show_images = req.getQuery("images") == "1" || sort_str.empty();
          show_cws = req.getQuery("cws") == "1" || sort_str.empty();
          is_htmx = req.getHeader("hx-target").data() != nullptr;
          site = self->controller->site_detail();
          board = self->controller->board_detail(txn, *board_id, login ? optional(login->id) : nullopt);
          if (show_posts) {
            threads = self->controller->list_board_threads(txn, *board_id, sort, login, !show_cws, from);
          } else {
            comments = self->controller->list_board_comments(txn, *board_id, sort, login, !show_cws, from);
          }
        }, [&](auto& rsp, auto& login) {
          const auto base_url = fmt::format("/b/{}?type={}&sort={}&images={}&cws={}",
            board.board->name()->string_view(),
            show_posts ? "posts" : "comments",
            sort_str,
            show_images ? 1 : 0,
            show_cws ? 1 : 0
          );
          if (is_htmx) {
            rsp.writeHeader("Content-Type", TYPE_HTML);
            if (show_posts) write_thread_list(rsp, threads, base_url, login, false, true, false, show_images);
            else write_comment_list(rsp, comments, base_url, login, true, true);
            rsp.end();
            return;
          }
          write_html_header(rsp, site, login, {
            .canonical_path = "/b/" + board.board->name()->str(),
            .banner_title = display_name(board.board),
            .banner_link = "/b/" + board.board->name()->str(),
            .banner_image = board.board->banner_url() ? optional(board.board->banner_url()->string_view()) : nullopt,
            .card_image = board.board->icon_url() ? optional(board.board->icon_url()->string_view()) : nullopt
          });
          rsp << "<div>";
          write_sidebar(rsp, site, login, {board});
          rsp << "<main>";
          write_sort_options(rsp, sort_str.empty() ? "Hot" : sort_str, SortFormType::Board, !board.board->content_warning(), show_posts, show_images, show_cws);
          if (show_posts) write_thread_list(rsp, threads, base_url, login, true, true, false, show_images);
          else write_comment_list(rsp, comments, base_url, login, true, true);
          rsp << "</main></div>";
          end_with_html_footer(rsp, page.time_elapsed());
        });
      }));
      app.get("/b/:name/create_thread", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        BoardDetailResponse board;
        bool show_url;
        page(txn, [&](Request& req, auto& login) {
          const auto name = req.getParameter(0);
          const auto board_id = txn.get_board_id(name);
          if (!board_id) throw ControllerError("Board name does not exist", 404);
          site = self->controller->site_detail();
          board = self->controller->board_detail(txn, *board_id, login ? optional(login->id) : nullopt);
          show_url = req.getQuery("text") != "1";
        }, [&](auto& rsp, auto& login) {
          write_html_header(rsp, site, login, {
            .canonical_path = fmt::format("/b/{}/create_thread", board.board->name()->string_view()),
            .banner_title = display_name(board.board),
            .banner_link = fmt::format("/b/{}", board.board->name()->string_view()),
            .banner_image = board.board->banner_url() ? optional(board.board->banner_url()->string_view()) : nullopt,
            .page_title = "Create Thread",
            .card_image = board.board->icon_url() ? optional(board.board->icon_url()->string_view()) : nullopt
          });
          write_create_thread_form(rsp, show_url, board, *login);
          end_with_html_footer(rsp, page.time_elapsed());
        });
      }));
      app.get("/u/:name", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        UserDetailResponse user;
        ListThreadsResponse threads;
        ListCommentsResponse comments;
        std::string_view sort_str;
        bool show_posts, show_images, show_cws;
        page(txn, [&](Request& req, auto& login) {
          const auto name = req.getParameter(0);
          const auto user_id = txn.get_user_id(name);
          if (!user_id) throw ControllerError("User does not exist", 404);
          // TODO: Get sort and filter settings from user
          show_posts = req.getQuery("type") != "comments";
          sort_str = req.getQuery("sort");
          const auto sort = Controller::parse_user_post_sort_type(sort_str);
          const auto from = Controller::parse_hex_id(std::string(req.getQuery("from")));
          show_images = req.getQuery("images") == "1" || sort_str.empty();
          show_cws = req.getQuery("cws") == "1" || sort_str.empty();
          site = self->controller->site_detail();
          user = self->controller->user_detail(txn, *user_id);
          if (show_posts) {
            threads = self->controller->list_user_threads(txn, *user_id, sort, login, !show_cws, from);
          } else {
            comments = self->controller->list_user_comments(txn, *user_id, sort, login, !show_cws, from);
          }
        }, [&](auto& rsp, auto& login) {
          write_html_header(rsp, site, login, {
            .canonical_path = "/u/" + user.user->name()->str(),
            .banner_title = display_name(user.user),
            .banner_link = "/u/" + user.user->name()->str(),
            .banner_image = user.user->banner_url() ? optional(user.user->banner_url()->string_view()) : nullopt,
            .card_image = user.user->avatar_url() ? optional(user.user->avatar_url()->string_view()) : nullopt
          });
          rsp << "<div>";
          write_sidebar(rsp, site, login);
          rsp << "<main>";
          write_sort_options(rsp, sort_str.empty() ? "New" : sort_str, SortFormType::User, true, show_posts, show_images, show_cws);
          const auto base_url = fmt::format("/u/{}?type={}&sort={}&images={}&cws={}",
            user.user->name()->string_view(),
            show_posts ? "posts" : "comments",
            sort_str,
            show_images ? 1 : 0,
            show_cws ? 1 : 0
          );
          if (show_posts) write_thread_list(rsp, threads, base_url, login, true, false, true, show_images);
          else write_comment_list(rsp, comments, base_url, login, false, true);
          rsp << "</main></div>";
          end_with_html_footer(rsp, page.time_elapsed());
        });
      }));
      app.get("/thread/:id", safe_page([](Self self, SafePage page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        BoardDetailResponse board;
        ThreadDetailResponse detail;
        std::string_view sort_str;
        bool show_images, show_cws;
        page(txn, [&](Request& req, auto& login) {
          const auto id = Controller::parse_hex_id(std::string(req.getParameter(0)));
          if (!id) throw ControllerError("Invalid hexadecimal post ID", 404);
          // TODO: Get sort and filter settings from user
          sort_str = req.getQuery("sort");
          const auto sort = Controller::parse_comment_sort_type(sort_str);
          const auto from = Controller::parse_hex_id(std::string(req.getQuery("from")));
          show_images = req.getQuery("images") == "1" || sort_str.empty();
          show_cws = req.getQuery("cws") == "1" || sort_str.empty();
          site = self->controller->site_detail();
          detail = self->controller->thread_detail(txn, *id, sort, login, !show_cws, from);
          board = self->controller->board_detail(txn, detail.thread->board(), login ? optional(login->id) : nullopt);
        }, [&](auto& rsp, auto& login) {
          write_html_header(rsp, site, login, {
            .canonical_path = fmt::format("/thread/{:x}", detail.id),
            .banner_title = display_name(board.board),
            .banner_link = "/b/" + board.board->name()->str(),
            .banner_image = board.board->banner_url() ? optional(board.board->banner_url()->string_view()) : nullopt,
            .card_image = board.board->icon_url() ? optional(board.board->icon_url()->string_view()) : nullopt
          });
          rsp << "<div>";
          write_sidebar(rsp, site, login, {board});
          rsp << "<main>";
          write_thread_view(rsp, &detail, login, sort_str, show_cws, show_images);
          rsp << "</main></div>";
          end_with_html_footer(rsp, page.time_elapsed());
        });
      }));
      app.get("/login", safe_page([](auto self, auto page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        page(txn, [&](auto&, auto&) {
          site = self->controller->site_detail();
        }, [&](auto& rsp, auto& login) {
          if (login) {
            rsp.writeStatus(http_status(303));
            rsp.writeHeader("Location", "/");
            rsp.endWithoutBody({}, true);
          } else {
            write_html_header(rsp, site, login, {
              .canonical_path = "/login",
              .banner_title = "Login",
            });
            write_login_form(rsp);
            end_with_html_footer(rsp, page.time_elapsed());
          }
        });
      }));
      app.get("/register", safe_page([](auto self, auto page) {
        auto txn = self->controller->open_read_txn();
        const SiteDetail* site;
        page(txn, [&](auto&, auto&) {
          site = self->controller->site_detail();
        }, [&](auto& rsp, auto& login) {
          if (login) {
            rsp.writeStatus(http_status(303));
            rsp.writeHeader("Location", "/");
            rsp.endWithoutBody({}, true);
          } else {
            write_html_header(rsp, site, login, {
              .canonical_path = "/register",
              .banner_title = "Register",
            });
            write_register_form(rsp);
            end_with_html_footer(rsp, page.time_elapsed());
          }
        });
      }));

      // -----------------------------------------------------------------------
      // API ACTIONS
      // -----------------------------------------------------------------------
      app.get("/logout", [](Response* rsp, Request* req) {
        rsp->writeStatus(http_status(303));
        rsp->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
        if (req->getHeader("referer").empty()) {
          rsp->writeHeader("Location", "/");
        } else {
          rsp->writeHeader("Location", req->getHeader("referer"));
        }
        rsp->endWithoutBody({}, true);
      });
      app.post("/login", action_page([](Self self, auto& req, auto& rsp, auto body, auto) {
        if (body.optional_string("username") /* actually a honeypot */) {
          spdlog::warn("Caught a bot with honeypot field on login");
          // just leave the connecting hanging, let the bots time out
          rsp.writeStatus(http_status(418));
          return;
        }
        LoginResponse login;
        bool remember = body.optional_bool("remember");
        try {
          login = self->controller->login(
            body.required_string("actual_username"),
            body.required_string("password"),
            rsp.getRemoteAddressAsText(),
            req.getHeader("user-agent"),
            remember
          );
        } catch (ControllerError e) {
          rsp.cork([&]() {
            rsp.writeStatus(http_status(e.http_error()));
            write_html_header(rsp, self->controller->site_detail(), {}, {
              .canonical_path = "/login",
              .banner_title = "Login",
            });
            write_login_form(rsp, {e.what()});
            end_with_html_footer(rsp, 0);
          });
          return;
        }
        const auto referer = req.getHeader("referer");
        rsp.cork([&]() {
          rsp.writeStatus(http_status(303));
          rsp.writeHeader("Set-Cookie",
            fmt::format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}",
              login.session_id, fmt::gmtime((time_t)login.expiration)));
          rsp.writeHeader("Location", referer.empty() || referer == "/login" ? "/" : referer);
          rsp.endWithoutBody({}, true);
        });
      }, false));
      app.post("/register", action_page([](Self self, auto&, auto& rsp, auto body, auto) {
        if (body.optional_string("username") /* actually a honeypot */) {
          spdlog::warn("Caught a bot with honeypot field on register");
          // just leave the connecting hanging, let the bots time out
          rsp.writeStatus(http_status(418));
          return;
        }
        try {
          SecretString password = body.required_string("password"),
            confirm_password = body.required_string("confirm_password");
          if (password.str != confirm_password.str) {
            throw ControllerError("Passwords do not match", 400);
          }
          self->controller->create_local_user(
            body.required_string("actual_username"),
            body.required_string("email"),
            std::move(password)
          );
        } catch (ControllerError e) {
          rsp.cork([&]() {
            rsp.writeStatus(http_status(e.http_error()));
            write_html_header(rsp, self->controller->site_detail(), {}, {
              .canonical_path = "/register",
              .banner_title = "Register",
            });
            write_register_form(rsp, {e.what()});
            end_with_html_footer(rsp, 0);
          });
          return;
        }
        const SiteDetail* site = self->controller->site_detail();
        rsp.cork([&]() {
          write_html_header(rsp, site, {}, { .banner_title = "Register" });
          rsp << R"(<main><div class="form-page"><h2>Registration complete!</h2>)"
            R"(<p>Log in to your new account:</p><p><a class="big-button" href="/login">Login</a></p>)"
            "</div></main>";
          end_with_html_footer(rsp, 0);
        });
      }, false));
      app.post("/create_board", action_page([](Self self, auto&, auto& rsp, auto body, auto logged_in_user) {
        const auto name = body.required_string("name");
        self->controller->create_local_board(
          logged_in_user,
          name,
          body.optional_string("display_name"),
          body.optional_string("content_warning"),
          body.optional_bool("private"),
          body.optional_bool("restricted_posting"),
          body.optional_bool("local_only")
        );
        rsp.cork([&]() {
          rsp.writeStatus(http_status(303));
          rsp.writeHeader("Location", fmt::format("/b/{}", name));
          rsp.endWithoutBody({}, true);
        });
      }));
      app.post("/b/:name/create_thread", action_page([](Self self, auto& req, auto& rsp, auto body, auto logged_in_user) {
        const auto name = req.getParameter(0);
        const auto board_id = self->controller->open_read_txn().get_board_id(name);
        if (!board_id) throw ControllerError("Board name does not exist", 404);
        const auto id = self->controller->create_local_thread(
          logged_in_user,
          *board_id,
          body.required_string("title"),
          body.optional_string("submission_url"),
          body.optional_string("text_content"),
          body.optional_string("content_warning")
        );
        rsp.cork([&]() {
          rsp.writeStatus(http_status(303));
          rsp.writeHeader("Location", fmt::format("/thread/{:x}", id));
          rsp.endWithoutBody({}, true);
        });
      }));
      app.post("/do/reply", action_page([](Self self, auto& req, auto& rsp, auto body, auto logged_in_user) {
        self->controller->create_local_comment(
          logged_in_user,
          body.required_hex_id("parent"),
          body.required_string("text_content"),
          body.optional_string("content_warning")
        );
        write_redirect_back(req, rsp);
      }));
      app.post("/do/vote", action_page([](Self self, auto& req, auto& rsp, auto body, auto logged_in_user) {
        const auto vote_str = uWS::getDecodedQueryValue("vote", body.query);
        Vote vote;
        if (vote_str == "1") vote = Upvote;
        else if (vote_str == "-1") vote = Downvote;
        else if (vote_str == "0") vote = NoVote;
        else throw ControllerError("Invalid or missing 'vote' parameter", 400);
        const auto post_id = body.required_hex_id("post");
        self->controller->vote(logged_in_user, post_id, vote);
        if (req.getHeader("hx-target").data() == nullptr) {
          write_redirect_back(req, rsp);
        } else {
          auto txn = self->controller->open_read_txn();
          const auto login = self->controller->local_user_detail(txn, logged_in_user);
          try {
            const auto thread = Controller::get_thread_entry(txn, post_id, login);
            rsp.cork([&]() {
              rsp.writeHeader("Content-Type", TYPE_HTML);
              write_vote_buttons(rsp, &thread, login);
              rsp.end();
            });
          } catch (ControllerError) {
            const auto comment = Controller::get_comment_entry(txn, post_id, login);
            rsp.cork([&]() {
              rsp.writeHeader("Content-Type", TYPE_HTML);
              write_vote_buttons(rsp, &comment, login);
              rsp.end();
            });
          }
        }
      }));
      app.post("/do/subscribe", action_page([](Self self, auto& req, auto& rsp, auto body, auto logged_in_user) {
        const auto board_id = body.required_hex_id("board");
        self->controller->subscribe(logged_in_user, board_id, true);
        if (req.getHeader("hx-target").data() == nullptr) {
          write_redirect_back(req, rsp);
        } else {
          rsp.cork([&]() {
            rsp.writeHeader("Content-Type", TYPE_HTML);
            write_subscribe_button(rsp, board_id, true);
            rsp.end();
          });
        }
      }));
      app.post("/do/unsubscribe", action_page([](Self self, auto& req, auto& rsp, auto body, auto logged_in_user) {
        const auto board_id = body.required_hex_id("board");
        self->controller->subscribe(logged_in_user, board_id, false);
        if (req.getHeader("hx-target").data() == nullptr) {
          write_redirect_back(req, rsp);
        } else {
          rsp.cork([&]() {
            rsp.writeHeader("Content-Type", TYPE_HTML);
            write_subscribe_button(rsp, board_id, false);
            rsp.end();
          });
        }
      }));

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
