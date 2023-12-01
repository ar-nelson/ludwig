#pragma once
#include "util/common.h++"
#include "models/db.h++"
#include <regex>
#include <sstream>
#include <variant>
#include <uWebSockets/App.h>
#include <flatbuffers/string.h>
#include <simdjson.h>

namespace Ludwig {
  static constexpr std::string_view ESCAPED = "<>'\"&",
    TYPE_HTML = "text/html; charset=utf-8",
    TYPE_CSS = "text/css; charset=utf-8",
    TYPE_JS = "text/javascript; charset=utf-8",
    TYPE_SVG = "image/svg+xml; charset=utf-8",
    TYPE_WEBP = "image/webp",
    TYPE_FORM = "application/x-www-form-urlencoded";

  static constexpr auto http_status(uint16_t code) -> std::string_view {
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
      default: return "500 Internal Server Error";
    }
  }

  class ApiError : public std::runtime_error {
  public:
    uint16_t http_status;
    std::string message, internal_message;
    ApiError(std::string message, uint16_t http_status = 500, std::string internal_message = { nullptr, 0 })
      : std::runtime_error(internal_message.data() ? fmt::format("{} - {}", message, internal_message) : std::string(message)),
        http_status(http_status), message(message), internal_message(internal_message) {}
  };

  struct QueryString {
    std::string_view query;
    inline auto required_hex_id(std::string_view key) -> uint64_t {
      try {
        return std::stoull(std::string(uWS::getDecodedQueryValue(key, query)), nullptr, 16);
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    inline auto required_int(std::string_view key) -> int {
      try {
        return std::stoi(std::string(uWS::getDecodedQueryValue(key, query)));
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    inline auto required_string(std::string_view key) -> std::string_view {
      auto s = uWS::getDecodedQueryValue(key, query);
      if (s.empty()) throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      return s;
    }
    inline auto required_vote(std::string_view key) -> Vote {
      const auto vote_str = uWS::getDecodedQueryValue(key, query);
      if (vote_str == "1") return Vote::Upvote;
      else if (vote_str == "-1") return Vote::Downvote;
      else if (vote_str == "0") return Vote::NoVote;
      else throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
    }
    inline auto optional_string(std::string_view key) -> std::optional<std::string_view> {
      auto s = uWS::getDecodedQueryValue(key, query);
      if (s.empty()) return {};
      return s;
    }
    inline auto optional_bool(std::string_view key) -> bool {
      return uWS::getDecodedQueryValue(key, query) == "1";
    }
  };

  static inline auto hex_id_param(uWS::HttpRequest* req, uint16_t param) {
    const auto str = req->getParameter(param);
    uint64_t id;
    const auto res = std::from_chars(str.begin(), str.end(), id, 16);
    if (res.ec != std::errc{} || res.ptr != str.data() + str.length()) {
      throw ApiError(fmt::format("Invalid hexadecimal ID: ", str), 404);
    }
    return id;
  }

  template <bool SSL, typename M = std::monostate, typename E = std::monostate> class Router {
  public:
    using Middleware = std::function<M (uWS::HttpResponse<SSL>*, uWS::HttpRequest*)>;
    using ErrorMiddleware = std::function<E (const uWS::HttpResponse<SSL>*, uWS::HttpRequest*)>;
    using ErrorHandler = std::function<void (uWS::HttpResponse<SSL>*, const ApiError&, const E&)>;
  private:
    uWS::TemplatedApp<SSL>& app;

    static auto default_error_handler(uWS::HttpResponse<SSL>* rsp, const ApiError& err, const E&) -> void {
      rsp->writeStatus(http_status(err.http_status))
        ->writeHeader("Content-Type", "text/plain; charset=utf-8")
        ->end(fmt::format("Error {:d} - {}", err.http_status, err.message));
    }

    struct Impl {
      Middleware middleware;
      ErrorMiddleware error_middleware;
      ErrorHandler error_handler;

      auto handle_error(
        const ApiError& e,
        uWS::HttpResponse<SSL>* rsp,
        const E& meta,
        std::string_view method,
        std::string_view url
      ) noexcept -> void {
        if (e.http_status >= 500) {
          spdlog::error("[{} {}] - {:d} {}", method, url, e.http_status, e.internal_message.empty() ? e.message : e.internal_message);
        } else {
          spdlog::info("[{} {}] - {:d} {}", method, url, e.http_status, e.internal_message.empty() ? e.message : e.internal_message);
        }
        if (rsp->getWriteOffset()) {
          spdlog::critical("Route {} threw exception after starting to respond; response has been truncated. This is a bug.", url);
          rsp->end();
          return;
        }
        try {
          error_handler(rsp, e, meta);
        } catch (...) {
          spdlog::critical("Route {} threw exception in error page callback; response has been truncated. This is a bug.", url);
          rsp->end();
        }
      }

      auto handle_error(
        std::exception_ptr eptr,
        uWS::HttpResponse<SSL>* rsp,
        const E& meta,
        std::string_view method,
        std::string_view url
      ) noexcept -> void {
        try {
          rethrow_exception(eptr);
        } catch (const ApiError& e) {
          handle_error(e, rsp, meta, method, url);
        } catch (const std::exception& e) {
          handle_error(ApiError("Unhandled internal exception", 500, e.what()), rsp, meta, method, url);
        } catch (...) {
          handle_error(
            ApiError("Unhandled internal exception", 500, "Unhandled internal exception, no information available"),
            rsp, meta, method, url
          );
        }
      }
    };

    std::shared_ptr<Impl> impl;
  public:
    Router(
      uWS::TemplatedApp<SSL>& app,
      Middleware middleware,
      ErrorMiddleware error_middleware,
      ErrorHandler error_handler = default_error_handler
    ) : app(app), impl(std::make_shared<Impl>(middleware, error_middleware, error_handler)) {}

    Router(
      uWS::TemplatedApp<SSL>& app,
      Middleware middleware,
      ErrorHandler error_handler = default_error_handler
    ) requires std::same_as<E, std::monostate>
      : Router(app, middleware, [](auto*, auto*){return std::monostate();}, error_handler) {}

    Router(
      uWS::TemplatedApp<SSL>& app,
      ErrorHandler error_handler = default_error_handler
    ) requires std::same_as<M, std::monostate> && std::same_as<E, std::monostate>
      : Router(app, [](auto*, auto*){return std::monostate();}, [](auto*, auto*){return std::monostate();}, error_handler) {}

    Router &&get(std::string pattern, uWS::MoveOnlyFunction<void (uWS::HttpResponse<SSL>*, uWS::HttpRequest*, M&)> &&handler) {
      app.get(pattern, [impl = impl, handler = std::move(handler)](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        const auto url = req->getUrl();
        try {
          auto meta = impl->middleware(rsp, req);
          handler(rsp, req, meta);
          spdlog::debug("[GET {}] - {} {}", url, rsp->getRemoteAddressAsText(), req->getHeader("user-agent"));
        } catch (...) {
          impl->handle_error(std::current_exception(), rsp, impl->error_middleware(rsp, req), "GET", url);
        }
      });
      return std::move(*this);
    }

    Router &&get_async(
      std::string pattern,
      uWS::MoveOnlyFunction<void (
        uWS::HttpResponse<SSL>*,
        uWS::HttpRequest*,
        std::unique_ptr<M>,
        uWS::MoveOnlyFunction<void (std::function<void ()>)>
      )> &&handler
    ) {
      app.get(pattern, [impl = impl, handler = std::move(handler)](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        auto abort_flag = std::make_shared<bool>(false);
        const auto url = std::string(req->getUrl());
        rsp->onAborted([abort_flag, url]{
          *abort_flag = true;
          spdlog::debug("[GET {}] - HTTP session aborted", url);
        });
        auto error_meta = std::make_shared<E>(impl->error_middleware(rsp, req));
        try {
          auto meta = std::make_unique<M>(impl->middleware(rsp, req));
          handler(rsp, req, std::move(meta), [impl = impl, rsp, url, error_meta, abort_flag](std::function<void()> body) mutable {
            if (*abort_flag) return;
            rsp->cork([&]{
              try { body(); }
              catch (...) { impl->handle_error(std::current_exception(), rsp, *error_meta, "GET", url); }
            });
          });
          spdlog::debug("[GET {}] - {} {}", url, rsp->getRemoteAddressAsText(), req->getHeader("user-agent"));
        } catch (...) {
          impl->handle_error(std::current_exception(), rsp, *error_meta, "GET", url);
        }
      });
      return std::move(*this);
    }

    Router &&post_form(
      std::string pattern,
      uWS::MoveOnlyFunction<uWS::MoveOnlyFunction<void (QueryString)> (
        uWS::HttpResponse<SSL>*,
        uWS::HttpRequest*,
        std::unique_ptr<M>
      )> &&handler,
      size_t max_size = 10 * 1024 * 1024 // 10MiB
    ) {
      app.post(pattern, [impl = impl, max_size, handler = std::move(handler)](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        const auto url = std::string(req->getUrl());
        auto error_meta = std::make_shared<E>(impl->error_middleware(rsp, req));
        rsp->onAborted([url]{
          spdlog::debug("[POST {}] - HTTP session aborted", url);
        });
        const auto user_agent = std::string(req->getHeader("user-agent"));
        std::string buffer = "?";
        try {
          const auto content_type = req->getHeader("content-type");
          if (content_type.data() && !content_type.starts_with(TYPE_FORM)) {
            throw ApiError("Wrong POST request Content-Type (expected application/x-www-form-urlencoded)", 415);
          }
          auto meta = std::make_unique<M>(impl->middleware(rsp, req));
          auto body_handler = handler(rsp, req, std::move(meta));
          rsp->onData([
            impl = impl, rsp, max_size, url, user_agent,
            error_meta = error_meta,
            body_handler = std::move(body_handler),
            buffer = std::move(buffer)
          ](std::string_view data, bool last) mutable {
            buffer.append(data);
            try {
              if (buffer.length() > max_size) throw ApiError("POST body is too large", 413);
              if (!last) return;
              if (!simdjson::validate_utf8(buffer)) throw ApiError("POST body is not valid UTF-8", 415);
              rsp->cork([&]{
                try {
                  body_handler(QueryString{buffer});
                  spdlog::debug("[POST {}] - {} {}", url, rsp->getRemoteAddressAsText(), user_agent);
                } catch (...) {
                  impl->handle_error(std::current_exception(), rsp, *error_meta, "POST", url);
                }
              });
            } catch (...) {
              rsp->cork([&]{ impl->handle_error(std::current_exception(), rsp, *error_meta, "POST", url); });
            }
          });
        } catch (...) {
          impl->handle_error(std::current_exception(), rsp, *error_meta, "POST", url);
          return;
        }
      });
      return std::move(*this);
    }
  };

  struct Escape {
    std::string_view str;
    Escape(std::string_view str) : str(str) {}
    Escape(const flatbuffers::String* fbs) : str(fbs ? fbs->string_view() : "") {}
  };
}

namespace fmt {
  template <> struct formatter<Ludwig::Escape> {
    constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator {
      auto it = ctx.begin();
      if (it != ctx.end()) detail::throw_format_error("invalid format");
      return it;
    }

    auto format(Ludwig::Escape e, format_context& ctx) const {
      size_t start = 0;
      for (
        size_t i = e.str.find_first_of(Ludwig::ESCAPED);
        i != std::string_view::npos;
        start = i + 1, i = e.str.find_first_of(Ludwig::ESCAPED, start)
      ) {
        if (i > start) std::copy(e.str.begin() + start, e.str.begin() + i, ctx.out());
        switch (e.str[i]) {
          case '<':
            format_to(ctx.out(), "&lt;");
            break;
          case '>':
            format_to(ctx.out(), "&gt;");
            break;
          case '\'':
            format_to(ctx.out(), "&apos;");
            break;
          case '"':
            format_to(ctx.out(), "&quot;");
            break;
          case '&':
            format_to(ctx.out(), "&amp;");
            break;
        }
      }
      return std::copy(e.str.begin() + start, e.str.end(), ctx.out());
    }
  };
}

namespace Ludwig {
  static inline auto escape_html(std::string_view str) -> std::string {
    return fmt::format("{}", Escape(str));
  }
}
