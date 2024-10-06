#pragma once
#include "services/db.h++"
#include "util/json.h++"
#include "util/web.h++"
#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <uWebSockets/App.h>

namespace Ludwig {

  template <bool SSL, typename AppContext = std::monostate>
  class RequestContext;

  template <typename T>
  concept IsRequestContext = requires (T ctx, std::exception_ptr err) {
    { ctx.method } -> std::assignable_from<std::string_view>;
    { ctx.url } -> std::assignable_from<std::string_view>;
    { ctx.user_agent } -> std::assignable_from<std::string_view>;
    ctx.handle_error(err);
    ctx.log();
  };

  template <bool SSL, typename AppContext>
  class RequestContext {
  private:
    uWS::HttpResponse<SSL>* rsp = nullptr;
    uWS::HttpRequest* req = nullptr;
    uWS::Loop* loop = nullptr;
    std::atomic<bool> done = false;
    Cancelable* current_awaiter = nullptr;
    std::string method_s, url_s, user_agent_s;
  public:
    std::string_view method, url, user_agent;

    auto setup_sync(uWS::HttpResponse<SSL>* _rsp, uWS::HttpRequest* _req, AppContext ac) -> bool {
      rsp = _rsp;
      req = _req;
      pre_try(rsp, req);
      method = req->getMethod();
      url = req->getUrl();
      user_agent = req->getHeader("user-agent");
      try {
        pre_request(rsp, req, ac);
        return true;
      } catch (...) {
        handle_error(std::current_exception());
        return false;
      }
    }
    auto setup_async(uWS::HttpResponse<SSL>* _rsp, uWS::HttpRequest* _req, AppContext ac) -> bool {
      loop = uWS::Loop::get();
      rsp = _rsp;
      req = _req;
      pre_try(rsp, req);
      method = method_s = req->getMethod();
      url = url_s = req->getUrl();
      user_agent = user_agent_s = req->getHeader("user-agent");
      rsp->onAborted([this] {
        this->done.store(true, std::memory_order_release);
        if (this->current_awaiter) this->current_awaiter->cancel();
        spdlog::warn("[{} {}] - HTTP request aborted", this->method, this->url);
      });
      try {
        pre_request(rsp, req, ac);
        return true;
      } catch (...) {
        handle_error(std::current_exception());
        return false;
      }
    }

    virtual auto error_response(const ApiError& err, uWS::HttpResponse<SSL>* rsp) noexcept -> void {
      rsp->writeStatus(http_status(err.http_status))
        ->end(fmt::format("Error {}: {}", http_status(err.http_status), err.message));
    }
    virtual auto pre_try(const uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req) noexcept -> void {}
    virtual auto pre_request(uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req, AppContext ac) -> void {}
    virtual ~RequestContext() {}

    auto handle_error(const ApiError& e) noexcept -> bool {
      if (done.exchange(true, std::memory_order_acq_rel)) return false;
      if (e.http_status >= 500) {
        spdlog::error("[{} {}] - {:d} {}", method, url, e.http_status, e.internal_message.empty() ? e.message : e.internal_message);
      } else {
        spdlog::info("[{} {}] - {:d} {}", method, url, e.http_status, e.internal_message.empty() ? e.message : e.internal_message);
      }
      if (rsp->getWriteOffset()) {
        spdlog::critical("Route {} threw exception after starting to respond; response has been truncated. This is a bug.", url);
        rsp->end();
        return true;
      }
      try {
        error_response(e, rsp);
      } catch (...) {
        spdlog::critical("Route {} threw exception in error page callback; response has been truncated. This is a bug.", url);
        rsp->end();
      }
      return true;
    }

    auto handle_error(std::exception_ptr eptr) noexcept -> bool {
      try {
        rethrow_exception(eptr);
      } catch (const ApiError& e) {
        return handle_error(e);
      } catch (const std::exception& e) {
        return handle_error(ApiError("Unhandled internal exception", 500, e.what()));
      } catch (...) {
        return handle_error(
          ApiError("Unhandled internal exception", 500, "Unhandled internal exception, no information available")
        );
      }
    }

    auto log() -> void {
      spdlog::debug("[{} {}] - {} {}", method, url, rsp->getRemoteAddressAsText(), user_agent);
    }

    auto on_response_thread(uWS::MoveOnlyFunction<void (uWS::HttpResponse<SSL>*)>&& fn) {
      loop->defer([fn = std::move(fn), rsp = rsp] mutable {
        rsp->cork([&]{ fn(rsp); });
      });
    }

    template <IsRequestContext Ctx>
    friend class RouterPromise;

    template <typename T, IsRequestContext Ctx>
    friend class RouterAwaiter;

    template <typename T, IsRequestContext Ctx>
    friend class BodyAwaiter;

    template <IsRequestContext Ctx>
    friend struct ContextAwaiter;
  };

  template <IsRequestContext Ctx>
  struct RouterPromise;

  template <IsRequestContext Ctx>
  struct RouterCoroutine {
    using promise_type = RouterPromise<Ctx>;
    using handle_type = std::coroutine_handle<promise_type>;

    handle_type handle;

    RouterCoroutine(handle_type h) : handle(h) {}
  };

  template <IsRequestContext Ctx>
  struct RouterPromise {
    Ctx ctx;
    static inline uint64_t next_id = 0;
    uint64_t id = next_id++;

    RouterCoroutine<Ctx> get_return_object() {
      return RouterCoroutine(std::coroutine_handle<RouterPromise<Ctx>>::from_promise(*this));
    }

    auto unhandled_exception() noexcept {
      ctx.handle_error(std::current_exception());
    }
    void return_void() noexcept {
      if (!ctx.done.exchange(true, std::memory_order_acq_rel)) {
        ctx.log();
      } else {
        spdlog::debug("Reached end of coroutine on already completed request");
      }
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept {
      if (ctx.current_awaiter) ctx.current_awaiter->cancel();
      return {};
    }
  };

  template <typename T>
  struct SyncAwaiter {
    T value;
    SyncAwaiter(T value) : value(value) {}
    auto await_ready() const noexcept -> bool { return true; }
    auto await_resume() const noexcept -> T { return value; }
    auto await_suspend(std::coroutine_handle<>) noexcept -> void {}
  };

  template <typename T, IsRequestContext Ctx>
  class RouterAwaiter : public Cancelable {
  private:
    std::coroutine_handle<RouterPromise<Ctx>> handle;
    std::optional<T> value;
  protected:
    std::mutex mutex;
    std::shared_ptr<Cancelable> canceler;
    bool canceled;
  public:
    using result_type = T;
    template <typename Fn>
    RouterAwaiter(Fn fn) : canceler(fn(this)) {}
    void cancel() noexcept override {
      if (canceler) canceler->cancel();
      {
        std::lock_guard<std::mutex> lock(mutex);
        canceled = true;
      }
      if (handle.address() != nullptr) {
        handle.promise().ctx.current_awaiter = nullptr;
        handle.promise().ctx.loop->defer([h = handle]{ if (h) h.resume(); });
      }
    }
    bool await_ready() const noexcept { return canceled || value.has_value(); }
    T&& await_resume() {
      std::lock_guard<std::mutex> lock(mutex);
      if (canceled) throw ApiError("Request canceled", 400);
      return std::move(value.value());
    }
    bool await_suspend(std::coroutine_handle<RouterPromise<Ctx>> h) noexcept {
      std::lock_guard<std::mutex> lock(mutex);
      if (value.has_value()) return false;
      assert(h.address() != nullptr);
      handle = h;
      h.promise().ctx.current_awaiter = this;
      return true;
    }
    void set_value(T&& v) noexcept {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (handle.address() == nullptr) {
          value.emplace(std::move(v));
          return;
        } else if (canceled || handle.done() || handle.promise().ctx.done.load(std::memory_order_acquire)) {
          spdlog::warn("HTTP request canceled");
          return;
        } else {
          value.emplace(std::move(v));
        }
      }
      handle.promise().ctx.current_awaiter = nullptr;
      handle.promise().ctx.loop->defer([h = handle]{ if (h) h.resume(); });
    }
    void replace_canceler(std::shared_ptr<Cancelable> new_canceler) noexcept {
      canceler = new_canceler;
    }
  };

  template <IsRequestContext Ctx>
  class WriteTxnAwaiter : public RouterAwaiter<WriteTxn, Ctx> {
    std::optional<DB::WriteCancel> canceler;
  public:
    WriteTxnAwaiter(DB& db, WritePriority priority = WritePriority::Medium) : canceler(
    ) {}

    void cancel() noexcept override {
      if (canceler) canceler->cancel();
      RouterAwaiter<WriteTxn, Ctx>::cancel();
    }
  };

  template <typename T, IsRequestContext Ctx>
  class BodyAwaiter : public Cancelable {
    struct Impl {
      size_t max_size;
      std::string body;
      std::optional<T> value;
      Impl(size_t max_size, std::string body_prefix) : max_size(max_size), body(body_prefix) {}
    };
    std::coroutine_handle<RouterPromise<Ctx>> handle;
    std::shared_ptr<Impl> impl;
    bool canceled;
  public:
  using result_type = T;
    BodyAwaiter(size_t max_size = 10 * MiB, std::string body_prefix = "") :
      impl(std::make_shared<Impl>(max_size, body_prefix)) {}
    BodyAwaiter(const BodyAwaiter<T, Ctx>& from) : impl(from.impl) {}

    virtual T parse(std::string& body) = 0;

    void cancel() noexcept override {
      canceled = true;
      if (handle.address() != nullptr) {
        handle.promise().ctx.current_awaiter = nullptr;
        handle.promise().ctx.loop->defer([h = handle]{ if (h) h.resume(); });
      }
    }
    bool await_ready() const noexcept {
      return canceled || impl->value.has_value();
    }
    T&& await_resume() {
      if (canceled) throw ApiError("Request canceled", 400);
      return std::move(impl->value.value());
    }
    bool await_suspend(std::coroutine_handle<RouterPromise<Ctx>> h) noexcept {
      if (impl->value.has_value()) return false;
      handle = h;
      assert(h.address() != nullptr);
      h.promise().ctx.current_awaiter = this;
      auto* rsp = h.promise().ctx.rsp;
      rsp->onData([this, impl = impl, h, rsp](auto data, bool last) mutable {
        assert(h.address() != nullptr);
        if (canceled || h.done() || h.promise().ctx.done.load(std::memory_order_acquire)) {
          spdlog::warn("Received request body for canceled HTTP request");
          return;
        }
        impl->body.append(data);
        try {
          if (impl->body.length() > impl->max_size) {
            throw ApiError("Request body is too large", 413);
          }
          if (!last) return;
          impl->value.emplace(this->parse(impl->body));
          h.promise().ctx.current_awaiter = nullptr;
          h.promise().ctx.loop->defer([h]{ if (h) h.resume(); });
        } catch (...) {
          rsp->cork([&]{ h.promise().ctx.handle_error(std::current_exception()); });
          this->cancel();
        }
      });
      return true;
    }
  };

  template <IsRequestContext Ctx>
  struct StringBody : public BodyAwaiter<std::string_view, Ctx> {
    StringBody(size_t max_size = 10 * MiB) : BodyAwaiter<std::string_view, Ctx>(max_size) {}
    StringBody(const StringBody<Ctx>& from) : BodyAwaiter<std::string_view, Ctx>(from) {}
    virtual std::string_view parse(std::string& s) noexcept override { return s; }
  };

  template <IsRequestContext Ctx>
  struct FormBody : public BodyAwaiter<QueryString<std::string_view>, Ctx> {
    FormBody(size_t max_size = 10 * MiB) : BodyAwaiter<QueryString<std::string_view>, Ctx>(max_size, "&") {}
    FormBody(const FormBody<Ctx>& from) : BodyAwaiter<QueryString<std::string_view>, Ctx>(from) {}
    QueryString<std::string_view> parse(std::string& s) override {
      if (!simdjson::validate_utf8(s)) throw ApiError("Request body is not valid UTF-8", 415);
      return QueryString(std::string_view(s));
    }
  };

  template <typename T, IsRequestContext Ctx>
  class JsonBody : public BodyAwaiter<T, Ctx> {
    std::shared_ptr<simdjson::ondemand::parser> parser;
  public:
    JsonBody(size_t max_size, std::shared_ptr<simdjson::ondemand::parser> parser) :
      BodyAwaiter<T, Ctx>(max_size),
      parser(parser) {}
    JsonBody(const JsonBody<T, Ctx>& from) : BodyAwaiter<T, Ctx>(from), parser(from.parser) {}

    T parse(std::string& s) override {
      try {
        pad_json_string(s);
        return JsonSerialize<T>::from_json(parser->iterate(s).value());
      } catch (const simdjson::simdjson_error& e) {
        throw ApiError(fmt::format("JSON does not match type ({})", simdjson::error_message(e.error())), 422);
      }
    }
  };

  template <IsRequestContext Ctx>
  class HandleAwaiter {
  protected:
    std::coroutine_handle<RouterPromise<Ctx>> handle;
  public:
    auto await_ready() const noexcept -> bool { return handle.address() != nullptr; }
    auto await_suspend(std::coroutine_handle<RouterPromise<Ctx>> h) noexcept -> bool {
      handle = h;
      return false;
    }
  };

  template <IsRequestContext Ctx>
  struct ContextAwaiter : public HandleAwaiter<Ctx> {
    auto await_resume() const noexcept -> Ctx& { return this->handle.promise().ctx; }

    template <typename Fn>
    class RequestAwaiter : public HandleAwaiter<Ctx> {
      Fn fn;
    public:
      RequestAwaiter(Fn&& fn) : fn(std::move(fn)) {}
      auto await_resume() const noexcept { return fn(this->handle.promise().ctx.req); }
    };

    template<typename Fn> requires std::invocable<Fn, uWS::HttpRequest*>
    auto with_request(Fn&& fn) const noexcept { return RequestAwaiter<Fn>(std::move(fn)); }
  };

  template <typename Fn, bool SSL, typename Ctx>
  concept GetHandler = requires (Fn&& fn, uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req, Ctx& ctx) {
    std::is_base_of<Ctx, RequestContext<SSL>>::value;
    std::invocable<Fn, uWS::HttpResponse<SSL>*, uWS::HttpRequest*, Ctx&>;
    { fn(rsp, req, ctx) } -> std::same_as<void>;
  };

  template <typename Fn, bool SSL, typename Ctx>
  concept GetAsyncHandler = requires (Fn&& fn, uWS::HttpResponse<SSL>* rsp, ContextAwaiter<Ctx> ctx) {
    std::is_base_of<Ctx, RequestContext<SSL>>::value;
    std::invocable<Fn, uWS::HttpResponse<SSL>*, ContextAwaiter<Ctx>>;
    { fn(rsp, ctx) } -> std::same_as<RouterCoroutine<Ctx>>;
  };

  template <typename Fn, bool SSL, typename Ctx, typename Body>
  concept PostHandler = requires (Fn&& fn, uWS::HttpResponse<SSL>* rsp, ContextAwaiter<Ctx> ctx, Body body) {
    std::is_base_of<Body, BodyAwaiter<typename Body::result_type, Ctx>>::value;
    std::is_base_of<Ctx, RequestContext<SSL>>::value;
    std::invocable<Fn, uWS::HttpResponse<SSL>*, ContextAwaiter<Ctx>, Body>;
    { fn(rsp, ctx, body) } -> std::same_as<RouterCoroutine<Ctx>>;
  };

  template <bool SSL, IsRequestContext Ctx = RequestContext<SSL>, typename AppContext = std::monostate>
    requires std::is_base_of<RequestContext<SSL, AppContext>, Ctx>::value
  class Router {
  private:
    std::unordered_map<std::string, std::set<std::string_view>> options_allow_by_pattern;
    std::optional<std::string> _access_control_allow_origin;
    uWS::MoveOnlyFunction<Ctx ()> ctx_ctor;
    uWS::TemplatedApp<SSL>& app;
    AppContext ac;

    auto register_route(std::string pattern, std::string_view method) -> void {
      options_allow_by_pattern.try_emplace({ pattern, {} }).first->second.insert(method);
    }

  public:
    template <class T>
    using Rsp = uWS::HttpResponse<SSL>*;
    using Req = uWS::HttpRequest*;
    using Self = Router<SSL, Ctx, AppContext>&&;
    using Coro = RouterCoroutine<Ctx>;

    template <typename ...Args>
    Router(uWS::TemplatedApp<SSL>& app, AppContext ac) : app(app), ac(ac) {}

    ~Router() {
      // uWebSockets doesn't provide OPTIONS or CORS preflight handlers,
      // so we have to add those manually, after all routes have been defined.
      for (const auto [pattern, methods] : options_allow_by_pattern) {
        std::string allow = "OPTIONS";
        for (const auto method : methods) {
          fmt::format_to(std::back_inserter(allow), ", {}", method);
        }
        app.any(pattern, [=, origin = _access_control_allow_origin](auto* rsp, auto* req) {
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

    auto access_control_allow_origin(std::string origin) -> Self {
      _access_control_allow_origin = origin;
      return std::move(*this);
    }

    template <GetHandler<SSL, Ctx> Fn>
    auto get(std::string pattern, Fn&& handler) -> Self {
      app.get(pattern, [handler = std::move(handler), ac = ac](uWS::HttpResponse<SSL>* rsp, Req req) mutable {
        Ctx ctx;
        if (!ctx.setup_sync(rsp, req, ac)) return;
        try {
          handler(rsp, req, ctx);
          ctx.log();
        } catch (...) {
          ctx.handle_error(std::current_exception());
        }
      });
      register_route(pattern, "GET");
      return std::move(*this);
    }

    template <GetAsyncHandler<SSL, Ctx> Fn>
    auto get_async(std::string pattern, Fn&& handler) -> Self {
      app.get(pattern, [handler = std::move(handler), ac = ac](uWS::HttpResponse<SSL>* rsp, Req req) mutable {
        Coro coro = handler(rsp, ContextAwaiter<Ctx>());
        Ctx& ctx = coro.handle.promise().ctx;
        if (ctx.setup_async(rsp, req, ac)) coro.handle();
        else coro.handle.destroy();
      });
      register_route(pattern, "GET");
      return std::move(*this);
    }

    template <typename Body, PostHandler<SSL, Ctx, Body> Fn, typename... Args>
    auto post_handler(
      Fn handler,
      size_t max_size,
      std::optional<std::string_view> expected_content_type = {},
      Args... ctor_args
    ) {
      return [=, handler = std::move(handler), ac = ac](uWS::HttpResponse<SSL>* rsp, Req req) mutable {
        Coro coro = handler(rsp, ContextAwaiter<Ctx>(), Body(max_size, ctor_args...));
        Ctx& ctx = coro.handle.promise().ctx;
        if (expected_content_type) {
          const auto content_type = req->getHeader("content-type");
          if (content_type.data() && !content_type.starts_with(*expected_content_type)) {
            ctx.handle_error(ApiError(fmt::format("Wrong request Content-Type (expected {})", *expected_content_type), 415));
            coro.handle.destroy();
            return;
          }
        }
        if (ctx.setup_async(rsp, req, ac)) coro.handle();
        else coro.handle.destroy();
      };
    }

    template <PostHandler<SSL, Ctx, StringBody<Ctx>> Fn>
    auto post(std::string pattern, Fn handler, size_t max_size = 10 * MiB, std::optional<std::string_view> expected_content_type = {}) -> Self {
      app.post(pattern, post_handler<StringBody<Ctx>>(std::move(handler), max_size, expected_content_type));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    template <PostHandler<SSL, Ctx, FormBody<Ctx>> Fn>
    auto post_form(std::string pattern, Fn handler, size_t max_size = 10 * MiB) -> Self {
      app.post(pattern, post_handler<FormBody<Ctx>>(std::move(handler), max_size, TYPE_FORM));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    template <typename T, PostHandler<SSL, Ctx, JsonBody<T, Ctx>> Fn>
    auto post_json(
      std::string pattern,
      std::shared_ptr<simdjson::ondemand::parser> parser,
      Fn handler,
      size_t max_size = 10 * MiB,
      std::optional<std::string_view> expected_content_type = "application/json"
    ) -> Self {
      app.post(pattern, post_handler<JsonBody<T, Ctx>>(std::move(handler), max_size, expected_content_type, parser));
      register_route(pattern, "POST");
      return std::move(*this);
    }

    template <PostHandler<SSL, Ctx, StringBody<Ctx>> Fn>
    auto put(std::string pattern, Fn handler, size_t max_size = 10 * MiB, std::optional<std::string_view> expected_content_type = {}) -> Self {
      app.put(pattern, post_handler<StringBody<Ctx>>(std::move(handler), max_size, expected_content_type));
      register_route(pattern, "PUT");
      return std::move(*this);
    }

    template <PostHandler<SSL, Ctx, FormBody<Ctx>> Fn>
    auto put_form(std::string pattern, Fn handler, size_t max_size = 10 * MiB) -> Self {
      app.put(pattern, post_handler<FormBody<Ctx>>(std::move(handler), max_size, TYPE_FORM));
      register_route(pattern, "PUT");
      return std::move(*this);
    }

    template <typename T, PostHandler<SSL, Ctx, JsonBody<T, Ctx>> Fn>
    auto put_json(
      std::string pattern,
      std::shared_ptr<simdjson::ondemand::parser> parser,
      Fn handler,
      size_t max_size = 10 * MiB,
      std::optional<std::string_view> expected_content_type = "application/json"
    ) -> Self {
      app.put(pattern, post_handler<JsonBody<T, Ctx>>(std::move(handler), max_size, expected_content_type, parser));
      register_route(pattern, "PUT");
      return std::move(*this);
    }

    template <GetHandler<SSL, Ctx> Fn>
    auto any(std::string pattern, Fn&& handler) -> Self {
      app.any(pattern, [handler = std::move(handler), ac = ac](uWS::HttpResponse<SSL>* rsp, Req req) mutable {
        Ctx ctx;
        if (!ctx.setup_sync(rsp, req, ac)) return;
        try {
          handler(rsp, req, ctx);
          ctx.log();
        } catch (...) {
          ctx.handle_error(std::current_exception());
        }
      });
      return std::move(*this);
    }
  };
}