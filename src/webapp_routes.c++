#include "webapp_routes.h++"
#include <regex>
#include <spdlog/fmt/fmt.h>
#include "xxhash.h"
#include "generated/default-theme.css.h"
#include "generated/htmx.min.js.h"

using std::optional, std::string, std::string_view;

#define COOKIE_NAME "ludwig_auth"

namespace Ludwig {
  static constexpr std::string_view
    ESCAPED = "<>'\"&",
    HTML_FOOTER = R"(<footer>Powered by Ludwig</body></html>)";
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
        R"(<label for="search"><input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)";
      const auto nsfw = nsfw_allowed(site, logged_in_user);
      if (board) rsp << R"(<input type="hidden" name="board" value=")" << board->id << R"(">)";
      if (nsfw || board) {
        rsp << R"(<details id="search-options"><summary>Search Options</summary><fieldset>)";
        if (board) {
          rsp << R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to )"
            << Escape{board->board->display_name()->string_view()}
            << "</label>";
        }
        if (nsfw) {
          rsp << R"(<label for="include_nsfw"><input type="checkbox" name="include_nsfw" id="include_nsfw" checked> Include NSFW results</label>)";
        }
        rsp << "</details>";
      }
      rsp << "</form></section>";
      if (!logged_in_user) {
        rsp << R"(<section id="login-section"><h2>Login</h2>)"
          R"(<form method="post" action="/do/login" id="login-form">)"
          R"(<label for="username"><span>Username:</span> <input type="text" name="username" id="username"></label>)"
          R"(<label for="password"><span>Password:</span> <input type="password" name="password" id="password"></label>)"
          R"(<input type="submit" value="Login" class="big-button"></form>)"
          R"(<a href="/register" class="big-button">Register</a>)"
          "</section>";
      } else if (board) {
        rsp << R"(<section id="actions-section"><h2>Actions</h2><a class="big-button" href="/b/)"
          << Escape{board->board->name()->string_view()} << R"(/submit">Submit a new link</a><a class="big-button" href="/b/)"
          << Escape{board->board->name()->string_view()} << R"(/submit?text=true">Submit a new text post</a></section>)";
      }
      if (board) {
        Escape display_name{board->board->display_name()->string_view()};
        rsp << R"(<section id="board-sidebar"><h2>)" << display_name << "</h2>";
        // TODO: Banner image
        // TODO: Allow safe HTML in description
        rsp << "<p>" << Escape{board->board->description()->string_view()} << "</p></section>";
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
          res->writeStatus("304 Not Modified")->end();
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
        page([&](auto& req) {
          site = self->controller->site_detail();
          login = self->get_logged_in_user(txn, req);
        }, [&](auto& rsp) {
          write_html_header(rsp, site, login, {"/"}, {site->name});
          rsp << "<main>Hello, world!</main>";
          write_sidebar(rsp, site, login);
          rsp.end(HTML_FOOTER);
        });
      }));
      serve_static(app, "default-theme.css", "text/css; charset=utf-8", default_theme_css, default_theme_css_len);
      serve_static(app, "htmx.min.js", "text/javascript; charset=utf-8", htmx_min_js, htmx_min_js_len);
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
