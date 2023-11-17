#pragma once
#include "util/common.h++"
#include "models/db.h++"
#include <regex>
#include <sstream>
#include <variant>
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <uWebSockets/App.h>
#include <uSockets/internal/eventing/asio.h>
#include <flatbuffers/string.h>
#include <simdjson.h>

using namespace asio::experimental::awaitable_operators;

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

  static bool behind_reverse_proxy = true;

  static inline auto get_ip(uWS::HttpResponse<false>* rsp, uWS::HttpRequest* req) -> std::string_view {
    // Hacky way to deal with x-forwarded-for:
    // If we're behind a reverse proxy, then every request will have x-forwarded-for.
    // If we EVER see a request without x-forwarded-for, ignore it from now on.
    if (behind_reverse_proxy) {
      auto forwarded_for = req->getHeader("x-forwarded-for");
      if (!forwarded_for.empty()) return forwarded_for.substr(0, forwarded_for.find(','));
      behind_reverse_proxy = false;
    }
    return rsp->getRemoteAddressAsText();
  };

  static inline auto get_ip(uWS::HttpResponse<true>* rsp, uWS::HttpRequest*) -> std::string_view {
    // Assume that SSL connections will never be behind a reverse proxy
    return rsp->getRemoteAddressAsText();
  };

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
#     ifdef LUDWIG_DEBUG
      std::optional<std::thread::id> original_thread;
#     endif

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

      // A Router should always live on a single thread.
      // If this is ever violated, one user's responses could be leaked to another user!
      inline auto check_thread() -> void {
#       ifdef LUDWIG_DEBUG
        if (original_thread) assert(std::this_thread::get_id() == *original_thread);
        else original_thread = std::this_thread::get_id();
#       endif
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
        impl->check_thread();
        const auto url = req->getUrl();
        try {
          auto meta = impl->middleware(rsp, req);
          handler(rsp, req, meta);
          spdlog::debug("[GET {}] - {} {}", url, get_ip(rsp, req), req->getHeader("user-agent"));
        } catch (...) {
          impl->handle_error(std::current_exception(), rsp, impl->error_middleware(rsp, req), "GET", url);
        }
      });
      return std::move(*this);
    }

    using GetAsyncHandler = uWS::MoveOnlyFunction<asio::awaitable<void> (
      uWS::HttpResponse<SSL>*,
      uWS::HttpRequest*,
      std::unique_ptr<M>
    )>;

    Router &&get_async(std::string pattern, GetAsyncHandler&& handler) {
      auto hsp = std::make_shared<GetAsyncHandler>(std::forward<GetAsyncHandler>(handler));
      app.get(pattern, [impl = impl, hsp = hsp](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        impl->check_thread();
        // We have to make sure this runs on the router's loop thread.
        //
        // This *should* have been doable with uWS::Loop::defer, but that
        // causes a buffer overflow for some reason.
        //
        // So instead we access the interals of a uSockets loop directly
        // and extract the asio io_context to schedule something on it.
        const auto* loop = us_socket_context_loop(SSL, us_socket_context(SSL, (us_socket_t*)rsp));
        auto* io = (asio::io_context*)loop->io;
        struct Coroutine {
          std::shared_ptr<Impl> impl;
          std::shared_ptr<GetAsyncHandler> handler;
          uWS::HttpResponse<SSL>* rsp;
          uWS::HttpRequest* req;
          std::string url;
          E error_meta;

          auto run() -> asio::awaitable<void> {
            auto cs = co_await asio::this_coro::cancellation_state;
            try {
              std::string msg = spdlog::get_level() == spdlog::level::debug
                ? fmt::format("[GET {}] - {} {}", url, get_ip(rsp, req), req->getHeader("user-agent"))
                : "";
              impl->check_thread();
              if (cs.cancelled() == asio::cancellation_type::none) {
                co_await (*handler)(rsp, req, std::make_unique<M>(impl->middleware(rsp, req)));
                impl->check_thread();
                spdlog::debug("{}", msg);
              }
            } catch (...) {
              if (cs.cancelled() == asio::cancellation_type::none) {
                impl->handle_error(std::current_exception(), rsp, error_meta, "GET", url);
              }
            }
            delete this;
          }
        };
        auto coroutine = new Coroutine{impl, hsp, rsp, req, std::string(req->getUrl()), impl->error_middleware(rsp, req)};
        AsyncCell cancel_signal;
        rsp->onAborted([cancel_signal, coroutine] mutable {
          cancel_signal.set({});
          spdlog::debug("[GET {}] - HTTP session aborted", coroutine->url);
        });
        // Run until the first co_await so req stays on the stack
        asio::co_spawn(*io, coroutine->run() || cancel_signal.async_get(asio::use_awaitable), asio::detached);
      });
      return std::move(*this);
    }

    using PostFormHandler = uWS::MoveOnlyFunction<asio::awaitable<void> (
      uWS::HttpResponse<SSL>*,
      uWS::HttpRequest*,
      std::unique_ptr<M>,
      std::function<asio::awaitable<QueryString>()>
    )>;

    // TODO: Continue here, make it match get_async
    Router &&post_form(
      std::string pattern,
      PostFormHandler &&handler,
      size_t max_size = 10 * 1024 * 1024 // 10MiB
    ) {
      auto hsp = std::make_shared<PostFormHandler>(std::forward<PostFormHandler>(handler));
      app.post(pattern, [impl = impl, hsp = hsp, max_size](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        impl->check_thread();
        const auto url = std::string(req->getUrl());
        auto error_meta = std::make_shared<E>(impl->error_middleware(rsp, req));
        bool canceled = false;
        rsp->onAborted([url, canceled] mutable {
          canceled = true;
          spdlog::debug("[POST {}] - HTTP session aborted", url);
        });
        const auto ip = get_ip(rsp, req);
        const auto user_agent = std::string(req->getHeader("user-agent"));
        std::string buffer = "?";

        const auto* loop = us_socket_context_loop(SSL, us_socket_context(SSL, (us_socket_t*)rsp));
        auto* io = (asio::io_context*)loop->io;
        AsyncCell<std::variant<QueryString, std::exception_ptr>> body;
        rsp->onData([
          impl = impl, rsp, max_size, url, ip, user_agent,
          error_meta = error_meta,
          body,
          buffer = std::move(buffer)
        ](std::string_view data, bool last) mutable {
          impl->check_thread();
          buffer.append(data);
          try {
            if (buffer.length() > max_size) throw ApiError("POST body is too large", 413);
            if (!last) return;
            if (!simdjson::validate_utf8(buffer)) throw ApiError("POST body is not valid UTF-8", 415);
            body.set(QueryString{buffer});
          } catch (...) {
            body.set(std::current_exception());
          }
        });
        struct Coroutine {
          std::shared_ptr<Impl> _impl;
          std::shared_ptr<PostFormHandler> _handler;
          AsyncCell<std::variant<QueryString, std::exception_ptr>> _body;
          uWS::HttpResponse<SSL>* _rsp;
          uWS::HttpRequest* _req;
          std::string _url;
          E error_meta;

          auto run() -> asio::awaitable<void> {
            try {
              const auto content_type = _req->getHeader("content-type");
              if (content_type.data() && !content_type.starts_with(TYPE_FORM)) {
                throw ApiError("Wrong POST request Content-Type (expected application/x-www-form-urlencoded)", 415);
              }
              co_await (*_handler)(_rsp, _req, std::make_unique<M>(_impl->middleware(_rsp, _req)), [body = _body] -> asio::awaitable<QueryString> {
                auto v = co_await body.async_get(asio::use_awaitable);
                if (auto* b = std::get_if<QueryString>(&v)) co_return *b;
                std::rethrow_exception(std::get<1>(v));
              });
            } catch (const asio::system_error& e) {
              // This error can be thrown after the request completes.
              // Just ignore it.
              if (e.code() != asio::error::operation_aborted) {
                spdlog::error("Ignoring asio error from {}: {}", _url, e.what());
              }
            } catch (...) {
              _impl->handle_error(std::current_exception(), _rsp, error_meta, "POST", _url);
            }
            delete this;
          }
        };
        auto coroutine = new Coroutine{impl, hsp, body, rsp, req, url, impl->error_middleware(rsp, req)};
        asio::co_spawn(*io, coroutine->run(), asio::detached);
        spdlog::debug("[POST {}] - {} {}", url, get_ip(rsp, req), req->getHeader("user-agent"));
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
