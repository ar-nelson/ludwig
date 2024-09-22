#pragma once
#include "util/common.h++"
#include "util/json.h++"
#include "models/db.h++"
#include <variant>
#include <uWebSockets/App.h>

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

  static inline auto get_query_param(std::string_view query, std::string_view key) -> std::string_view {
    return uWS::getDecodedQueryValue(key, query);
  }

  static inline auto get_query_param(uWS::HttpRequest* req, std::string_view key) -> std::string_view {
    return req->getQuery(key);
  }

  template <typename T>
  struct QueryString {
    T query;
    QueryString(const T& query) : query(query) {}

    auto required_hex_id(std::string_view key) -> uint64_t {
      try {
        return std::stoull(std::string(get_query_param(query, key)), nullptr, 16);
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    auto required_int(std::string_view key) -> int {
      try {
        return std::stoi(std::string(get_query_param(query, key)));
      } catch (...) {
        throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      }
    }
    auto required_string(std::string_view key) -> std::string_view {
      auto s = get_query_param(query, key);
      if (s.empty()) throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
      return s;
    }
    auto required_vote(std::string_view key) -> Vote {
      const auto vote_str = get_query_param(query, key);
      if (vote_str == "1") return Vote::Upvote;
      else if (vote_str == "-1") return Vote::Downvote;
      else if (vote_str == "0") return Vote::NoVote;
      else throw ApiError(fmt::format("Invalid or missing '{}' parameter", key), 400);
    }
    auto optional_id(std::string_view key) -> uint64_t {
      auto s = get_query_param(query, key);
      if (s.empty()) return 0;
      try {
        return std::stoull(std::string(s), nullptr, 16);
      } catch (...) {
        throw ApiError(fmt::format("Invalid '{}' parameter", key), 400);
      }
    }
    auto string(std::string_view key) -> std::string_view {
      return get_query_param(query, key);
    }
    auto optional_string(std::string_view key) -> std::optional<std::string_view> {
      auto s = get_query_param(query, key);
      if (s.empty()) return {};
      return s;
    }
    auto optional_uint(std::string_view key) -> std::optional<uint64_t> {
      auto s = get_query_param(query, key);
      if (s.empty()) return {};
      try {
        return std::stoul(std::string(s));
      } catch (...) {
        throw ApiError(fmt::format("Invalid '{}' parameter", key), 400);
      }
    }
    auto optional_bool(std::string_view key) -> bool {
      const auto decoded = get_query_param(query, key);
      return !(decoded.empty() || decoded == "0" || decoded == "false");
    }
  };

  static inline auto hex_id_param(uWS::HttpRequest* req, uint16_t param) {
    const auto str = req->getParameter(param);
    uint64_t id;
    const auto res = std::from_chars(str.begin(), str.end(), id, 16);
    if (res.ec != std::errc{} || res.ptr != str.data() + str.length()) {
      throw ApiError(fmt::format("Invalid hexadecimal ID: ", str), 400);
    }
    return id;
  }

  template <bool SSL, typename M = std::monostate, typename E = std::monostate> class Router {
  public:
    using Middleware = std::function<M (uWS::HttpResponse<SSL>*, uWS::HttpRequest*)>;
    using ErrorMiddleware = std::function<E (const uWS::HttpResponse<SSL>*, uWS::HttpRequest*)>;
    using ErrorHandler = std::function<void (uWS::HttpResponse<SSL>*, const ApiError&, const E&)>;

    using GetAsyncHandler = uWS::MoveOnlyFunction<void (
      uWS::HttpResponse<SSL>*,
      uWS::HttpRequest*,
      std::unique_ptr<M>,
      uWS::MoveOnlyFunction<void (uWS::MoveOnlyFunction<void ()>&&)>
    )>;
    template <typename T> using PostBodyHandler = uWS::MoveOnlyFunction<void (T, uWS::MoveOnlyFunction<void (uWS::MoveOnlyFunction<void (uWS::HttpResponse<SSL>*)>)>&&)>;
    template <typename T> using PostHandler = uWS::MoveOnlyFunction<PostBodyHandler<T> (uWS::HttpRequest*, std::unique_ptr<M>)>;
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
      std::unordered_map<std::string, std::set<std::string_view>> options_allow_by_pattern;
      std::optional<std::string> access_control_allow_origin;

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

    auto register_route(const std::string& pattern, std::string_view method) -> void {
      impl->options_allow_by_pattern.try_emplace({ pattern, {} }).first->second.insert(method);
    }
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

    ~Router() {
      // uWebSockets doesn't provide OPTIONS or CORS preflight handlers,
      // so we have to add those manually, after all routes have been defined.
      for (const auto [pattern, methods] : impl->options_allow_by_pattern) {
        std::string allow = "OPTIONS";
        for (const auto method : methods) fmt::format_to(std::back_inserter(allow), ", {}", method);
        app.any(pattern, [=, origin = impl->access_control_allow_origin](auto* rsp, auto* req) {
          if (req->getMethod() == "options") {
            if (origin && !req->getHeader("origin").empty() && !req->getHeader("access-control-request-method").empty()) {
              rsp->writeHeader("Allow", allow)
                ->writeHeader("Access-Control-Allow-Origin", *origin)
                ->writeHeader("Access-Control-Allow-Methods", allow)
                ->writeHeader("Access-Control-Allow-Headers", "authorization,content-type")
                ->writeHeader("Access-Control-Max-Age", "86400")
                ->end();
            } else {
              rsp->writeStatus(http_status(204))
                ->writeHeader("Allow", allow)
                ->end();
            }
          } else {
            spdlog::info("[{} {}] - 405 Method Not Found", req->getMethod(), req->getUrl());
            rsp->writeStatus(http_status(405))->end();
          }
        });
      }
    }

    Router &&access_control_allow_origin(std::string origin) {
      impl->access_control_allow_origin = origin;
      return std::move(*this);
    }

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
      register_route(pattern, "GET");
      return std::move(*this);
    }

    Router &&any(std::string pattern, uWS::MoveOnlyFunction<void (uWS::HttpResponse<SSL>*, uWS::HttpRequest*, M&)> &&handler) {
      app.any(pattern, [impl = impl, handler = std::move(handler)](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        const auto url = req->getUrl();
        try {
          auto meta = impl->middleware(rsp, req);
          handler(rsp, req, meta);
          spdlog::debug("[{} {}] - {} {}", req->getMethod(), url, rsp->getRemoteAddressAsText(), req->getHeader("user-agent"));
        } catch (...) {
          impl->handle_error(std::current_exception(), rsp, impl->error_middleware(rsp, req), req->getMethod(), url);
        }
      });
      return std::move(*this);
    }

    Router &&get_async(std::string pattern, GetAsyncHandler&& handler) {
      using std::current_exception, std::make_shared, std::string;
      app.get(pattern, [impl = impl, handler = std::move(handler)](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        auto abort_flag = make_shared<std::atomic<bool>>(false);
        const string url(req->getUrl());
        rsp->onAborted([abort_flag, url]{
          *abort_flag = true;
          spdlog::debug("[GET {}] - HTTP session aborted", url);
        });
        auto error_meta = make_shared<E>(impl->error_middleware(rsp, req));
        try {
          auto meta = std::make_unique<M>(impl->middleware(rsp, req));
          handler(rsp, req, std::move(meta), [
            impl = impl, loop = uWS::Loop::get(), rsp, url, error_meta, abort_flag
          ](uWS::MoveOnlyFunction<void()>&& body) mutable {
            if (*abort_flag) return;
            loop->defer([impl = impl, body = std::move(body), rsp, url, error_meta, abort_flag] mutable {
              if (*abort_flag) return;
              rsp->cork([&]{
                try { body(); }
                catch (...) {
                  if (*abort_flag) return;
                  impl->handle_error(current_exception(), rsp, *error_meta, "GET", url);
                }
              });
            });
          });
          spdlog::debug("[GET {}] - {} {}", url, rsp->getRemoteAddressAsText(), req->getHeader("user-agent"));
        } catch (...) {
          impl->handle_error(current_exception(), rsp, *error_meta, "GET", url);
        }
      });
      register_route(pattern, "GET");
      return std::move(*this);
    }

    template <typename T> auto post_handler(
      PostHandler<T>&& handler,
      std::string_view method,
      size_t max_size,
      std::optional<std::string_view> expected_content_type,
      std::string body_prefix,
      uWS::MoveOnlyFunction<T (std::string&&)>&& parse_body
    ) {
      using std::current_exception, std::make_shared, std::string;
      return [
        impl = impl, max_size, expected_content_type, body_prefix, method,
        handler = std::move(handler),
        parse_body = std::move(parse_body)
      ](uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) mutable {
        auto abort_flag = make_shared<std::atomic<bool>>(false);
        const string url(req->getUrl());
        const auto error_meta = make_shared<E>(impl->error_middleware(rsp, req));
        rsp->onAborted([method, abort_flag, url]{
          *abort_flag = true;
          spdlog::debug("[{} {}] - HTTP session aborted", method, url);
        });
        const string user_agent(req->getHeader("user-agent"));
        try {
          if (expected_content_type) {
            const auto content_type = req->getHeader("content-type");
            if (content_type.data() && !content_type.starts_with(*expected_content_type)) {
              throw ApiError(fmt::format("Wrong request Content-Type (expected {})", *expected_content_type), 415);
            }
          }
          rsp->onData([=,
            loop = uWS::Loop::get(),
            &parse_body,
            body_handler = handler(req, std::make_unique<M>(impl->middleware(rsp, req))),
            in_buffer = body_prefix
          ](auto data, bool last) mutable {
            in_buffer.append(data);
            try {
              if (in_buffer.length() > max_size) throw ApiError("Request body is too large", 413);
              if (!last) return;
              rsp->cork([&]{
                try {
                  body_handler(
                    parse_body(std::move(in_buffer)),
                    [abort_flag, loop, rsp](uWS::MoveOnlyFunction<void (uWS::HttpResponse<SSL>*)>&& f) {
                      if (*abort_flag) throw std::runtime_error("HTTP session aborted");
                      loop->defer([abort_flag, rsp, f = std::move(f)] mutable {
                        if (*abort_flag) return;
                        rsp->cork([rsp, f = std::move(f)] mutable { f(rsp); });
                      });
                    }
                  );
                  spdlog::debug("[{} {}] - {} {}", method, url, rsp->getRemoteAddressAsText(), user_agent);
                } catch (...) {
                  *abort_flag = true;
                  impl->handle_error(current_exception(), rsp, *error_meta, method, url);
                }
              });
            } catch (...) {
              rsp->cork([&]{ impl->handle_error(current_exception(), rsp, *error_meta, method, url); });
            }
          });
        } catch (...) {
          impl->handle_error(current_exception(), rsp, *error_meta, method, url);
          return;
        }
      };
    }

    Router &&post(std::string pattern, PostHandler<std::string>&& handler, size_t max_size = 10 * MiB) {
      app.post(pattern, post_handler<std::string>(std::move(handler), "POST", max_size, {}, "", [](auto&& s){return s;}));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    Router &&put(std::string pattern, PostHandler<std::string>&& handler, size_t max_size = 10 * MiB) {
      app.put(pattern, post_handler<std::string>(std::move(handler), "PUT", max_size, {}, "", [](auto&& s){return s;}));
      register_route(pattern, "PUT");
      return std::move(*this);
    }

    Router &&post_form(std::string pattern, PostHandler<QueryString<std::string>>&& handler, size_t max_size = 10 * MiB) {
      app.post(pattern, post_handler<QueryString<std::string>>(std::move(handler), "POST", max_size, TYPE_FORM, "?", [](auto&& s){
        if (!simdjson::validate_utf8(s)) throw ApiError("POST body is not valid UTF-8", 415);
        return QueryString<std::string>(s);
      }));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    template <class T> Router &&post_json(
      std::string pattern,
      std::shared_ptr<simdjson::ondemand::parser> parser,
      PostHandler<T>&& handler,
      size_t max_size = 10 * MiB
    ) {
      app.post(pattern, post_handler<T>(std::move(handler), "POST", max_size, "application/json", "", [parser](auto&& s) -> T {
        try {
          pad_json_string(s);
          return JsonSerialize<T>::from_json(parser->iterate(s).value());
        } catch (const simdjson::simdjson_error& e) {
          throw ApiError(fmt::format("JSON does not match type ({})", simdjson::error_message(e.error())), 422);
        }
      }));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    template <class T> Router &&put_json(
      std::string pattern,
      std::shared_ptr<simdjson::ondemand::parser> parser,
      PostHandler<T>&& handler,
      size_t max_size = 10 * MiB
    ) {
      app.put(pattern, post_handler<T>(std::move(handler), "PUT", max_size, "application/json", "", [parser](auto&& s) -> T {
        try {
          pad_json_string(s);
          return JsonSerialize<T>::from_json(parser->iterate(s).value());
        } catch (const simdjson::simdjson_error& e) {
          throw ApiError(fmt::format("JSON does not match type ({})", simdjson::error_message(e.error())), 422);
        }
      }));
      register_route(pattern, "PUT");
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
  template <> struct formatter<Ludwig::Escape> : public Ludwig::CustomFormatter {
    template <typename FormatContext>
    auto format(Ludwig::Escape e, FormatContext& ctx) const {
      size_t start = 0;
      for (
        size_t i = e.str.find_first_of(Ludwig::ESCAPED);
        i != std::string_view::npos;
        start = i + 1, i = e.str.find_first_of(Ludwig::ESCAPED, start)
      ) {
        if (i > start) std::copy(e.str.begin() + start, e.str.begin() + i, ctx.out());
        switch (e.str[i]) {
          case '<': format_to(ctx.out(), "&lt;"_cf); break;
          case '>': format_to(ctx.out(), "&gt;"_cf); break;
          case '\'': format_to(ctx.out(), "&apos;"_cf); break;
          case '"': format_to(ctx.out(), "&quot;"_cf); break;
          case '&': format_to(ctx.out(), "&amp;"_cf); break;
        }
      }
      return std::copy(e.str.begin() + start, e.str.end(), ctx.out());
    }
  };
}

namespace Ludwig {
  static inline auto escape_html(std::string_view str) -> std::string {
    using fmt::operator""_cf;
    return fmt::format("{}"_cf, Escape(str));
  }
}

#define ICON(NAME) \
  R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#)" NAME R"("></svg>)"
#define HTML_FIELD(ID, LABEL, TYPE, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"" TYPE "\" name=\"" ID "\" id=\"" ID "\"" EXTRA "></label>"
#define HTML_CHECKBOX(ID, LABEL, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"checkbox\" class=\"a11y\" name=\"" ID "\" id=\"" ID "\"" EXTRA "><div class=\"toggle-switch\"></div></label>"
#define HTML_TEXTAREA(ID, LABEL, EXTRA, CONTENT) \
  "<label for=\"" ID "\"><span>" LABEL "</span><div><textarea name=\"" ID "\" id=\"" ID "\"" EXTRA ">" CONTENT \
  R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" rel="nofollow" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"