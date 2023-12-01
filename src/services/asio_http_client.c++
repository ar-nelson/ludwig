#include "asio_http_client.h++"
#include "util/web.h++"
#include <uWebSockets/HttpParser.h>
#include <asio/experimental/parallel_group.hpp>
#include <duthomhas/csprng.hpp>
#include <chrono>

using namespace std::literals;
using asio::deferred, asio::error_code, asio::io_context, asio::ip::tcp,
    asio::redirect_error, std::current_exception, std::exception_ptr,
    std::make_unique, std::match_results, std::nullopt, std::optional,
    std::regex, std::regex_match, std::rethrow_exception, std::runtime_error,
    std::shared_ptr, std::string, std::string_view, std::unique_ptr,
    std::variant;
namespace ssl = asio::ssl;

namespace Ludwig {
  AsioHttpClient::AsioHttpClient(
    shared_ptr<io_context> io,
    shared_ptr<ssl::context> ssl
  ) : io(io), work(io->get_executor()), ssl(ssl) {}

  class AsioHttpClientError : public runtime_error {
  public:
  };

  using TcpSocket = asio::basic_stream_socket<tcp, io_context::executor_type>;
  using SslSocket = ssl::stream<TcpSocket>;

  class AsioHttpClientResponse : public HttpClientResponse {
    static inline constexpr size_t MAX_RESPONSE_BYTES = 1024 * 1024 * 64; // 64MiB
    static inline auto random_uint64() -> uint64_t {
      uint64_t n;
      duthomhas::csprng rng;
      return rng(n);
    }
    // This is random to ensure that servers cannot break this client by spoofing this header
    static const inline string response_header_key = fmt::format("x-response-{:016x}", random_uint64());
    uint16_t _status = 0;
    string fake_request_buf;
    string _body;
    optional<uWS::HttpRequest> fake_request;
  public:
    AsioHttpClientResponse() : fake_request_buf(fmt::format("POST / HTTP/1.1\r\nHost: \r\n{}: ", response_header_key)) {}

    inline auto append(const void* data, size_t size, const Url& url) -> void {
      if (fake_request_buf.length() + size > MAX_RESPONSE_BYTES) {
        throw HttpClientError(url, fmt::format("Response is larger than max of {} bytes", MAX_RESPONSE_BYTES));
      }
      fake_request_buf.append((const char*)data, size);
    }

    inline auto parse(const Url& url) -> void {
      // uWS parser expects buffer to be padded
      fake_request_buf.reserve(fake_request_buf.length() + 2);

      uWS::HttpParser parser;
      const auto [n, ret] = parser.consumePostPadded(fake_request_buf.data(), static_cast<unsigned>(fake_request_buf.length()), this, nullptr,
        [this](void* u, auto* req) -> void* {
          fake_request = { *req };
          return u;
        },
        [this](void* u, string_view data, bool) -> void* {
          _body.append(data);
          return u;
        }
      );
      // FULLPTR = error return, according to gnarly internal details of uWS
      if (ret == uWS::FULLPTR) {
        // FIXME: This returns a meaningless HTTP error, need to parse these errors into something more meaningful
        throw HttpClientError(url, uWS::httpErrorResponses[n]);
      }
      if (!fake_request) throw HttpClientError(url, "Incomplete HTTP response");
      const auto response_line = fake_request->getHeader(response_header_key);
      static const regex http_regex(R"((HTTP/[\d.]+)\s+(\d+)(?:\s+(\w[^\r\n]*))?)");
      match_results<string_view::const_iterator> match;
      if (!regex_match(response_line.cbegin(), response_line.cend(), match, http_regex)) {
        throw HttpClientError(url, fmt::format("Invalid HTTP response: {}", response_line.substr(0, 128)));
      }
      _status = static_cast<uint16_t>(std::stoi(match.str(2)));
    }

    auto status() const -> uint16_t { return _status; }
    auto header(string_view name) const -> string_view {
      if (fake_request) {
        // This should be a const operation, but it's not…
        auto* req = const_cast<uWS::HttpRequest*>(&*fake_request);
        return req->getHeader(name);
      }
      return string_view(nullptr, 0);
    }
    auto body() const -> string_view { return _body; }
    auto error() const -> optional<string_view> {
      return _status >= 400 ? optional(http_status(_status)) : nullopt;
    };
  };

  template <typename Socket> static inline auto close_socket(Socket& s) -> void {
    s.close();
  }

  template<> inline auto close_socket<SslSocket>(SslSocket& s) -> void {
    try { s.shutdown(); } catch (...) { /* ignore "stream truncated", which may happen here */ }
  }

  inline auto check_ec(const error_code& ec, string_view step, const Url& url) -> void {
    // TODO: Mark certain errors (like connection refused) as non-transient
    // Currently this marks all connection errors as transient
    spdlog::debug(step);
    if (ec) throw HttpClientError(url, fmt::format("{} (while {})", ec.message(), step), true);
  }

  template <typename Socket> struct AsioFetchCtx {
    tcp::resolver resolver;
    Socket socket;

    template <class... Args> AsioFetchCtx(io_context& io, Args&& ...args) : resolver(io), socket(std::forward<Args>(args)...) {}

    ~AsioFetchCtx() {
      resolver.cancel();
      close_socket(socket);
    }

    inline auto send_and_recv(unique_ptr<HttpClientRequest>& req, unique_ptr<AsioHttpClientResponse>& response) -> Async<void> {
      asio::streambuf req_buf, rsp_buf;
      error_code ec;
      req_buf.sputn(req->request.data(), (std::streamsize)req->request.size());
      size_t bytes = co_await asio::async_write(socket, req_buf, redirect_error(deferred, ec));
      check_ec(ec, "writing HTTP request", req->url);
      while (
        (bytes = co_await asio::async_read(socket, rsp_buf, redirect_error(deferred, ec))),
        ec != asio::error::eof && ec != ssl::error::stream_truncated
      ) {
        check_ec(ec, "reading HTTP response", req->url);
        response->append(rsp_buf.data().data(), bytes, req->url);
        rsp_buf.consume(bytes);
      }
      response->append(rsp_buf.data().data(), bytes, req->url);
    }
  };

  auto AsioHttpClient::https_fetch(unique_ptr<HttpClientRequest>& req) -> Async<AsioHttpClientResponse*> {
    error_code ec;
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<SslSocket> c(*io, *io, *ssl);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req->url.host),
      req->url.port.empty() ? "https" : req->url.port,
      redirect_error(deferred, ec)
    );
    check_ec(ec, "resolving address", req->url);
    c.socket.set_verify_mode(ssl::verify_peer);
    c.socket.set_verify_callback([](bool /*preverified*/, ssl::verify_context& ctx) {
      // TODO: Actually verify SSL certificates
      char subject_name[256];
      X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
      X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
      //spdlog::debug("Verifying SSL cert: {} (preverified: {})", subject_name, preverified);
      return true;
    });
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, redirect_error(deferred, ec));
    check_ec(ec, "connecting", req->url);

    // SNI - This is barely documented, but it's necessary to connect to some HTTPS sites without a handshake error!
    // Based on https://stackoverflow.com/a/59225060/548027
    if (!SSL_set_tlsext_host_name(c.socket.native_handle(), req->url.host.c_str())) {
      check_ec({static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()}, "setting TLS host name", req->url);
    }

    co_await c.socket.async_handshake(ssl::stream_base::client, redirect_error(deferred, ec));
    check_ec(ec, "performing TLS handshake", req->url);
    co_await c.send_and_recv(req, response);
    co_return response.release();
  }

  auto AsioHttpClient::http_fetch(unique_ptr<HttpClientRequest>& req) -> Async<AsioHttpClientResponse*> {
    error_code ec;
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<asio::buffered_stream<TcpSocket>> c(*io, *io);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req->url.host),
      req->url.port.empty() ? "http" : req->url.port,
      redirect_error(deferred, ec)
    );
    check_ec(ec, "resolving address", req->url);
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, redirect_error(deferred, ec));
    check_ec(ec, "connecting", req->url);
    co_await c.send_and_recv(req, response);
    co_return response.release();
  }

  auto AsioHttpClient::fetch(unique_ptr<HttpClientRequest> req) -> Async<unique_ptr<const HttpClientResponse>> {
    auto run = [this, req = std::move(req)] mutable -> Async<unique_ptr<const HttpClientResponse>> {
      spdlog::debug("CLIENT HTTP {} {}", req->method, req->url.to_string());
      for (uint8_t redirects = 0; redirects < 10; redirects++) {
        asio::steady_timer timeout(*io, 30s);
        auto [order, ec, ex, rsp_ptr] = co_await asio::experimental::make_parallel_group(
          timeout.async_wait(deferred),
          asio::co_spawn(*io, req->url.scheme == "https" ? https_fetch(req) : http_fetch(req), deferred)
        ).async_wait(
          asio::experimental::wait_for_one(),
          deferred
        );
        if (order[0] == 0) {
          if (ec) throw asio::system_error(ec);
          throw HttpClientError(req->url, "Request timed out");
        }
        unique_ptr<AsioHttpClientResponse> response(rsp_ptr);
        if (ex) rethrow_exception(ex);
        response->parse(req->url);
        const auto status = response->status();
        switch (status) {
        case 301:
        case 302:
        case 303:
        case 307:
        case 308:
          if (response->header("location").empty()) throw HttpClientError(req->url, "Got redirect with no Location header");
          else req->redirect(string(response->header("location")));
          break;
        default:
          if (req->throw_on_error_status && status >= 400) {
            throw HttpClientError(req->url, http_status(status), status >= 500 || status == 429);
          }
          co_return response;
        }
      }
      throw HttpClientError(req->url, "Too many redirects");
    };
    if (io->get_executor().running_in_this_thread()) {
      co_return co_await run();
    }
    auto v = co_await asio::co_spawn(io->get_executor(),
      [run = std::move(run)] mutable -> Async<variant<unique_ptr<const HttpClientResponse>, exception_ptr>> {
        try { co_return co_await run(); }
        catch (...) { co_return current_exception(); }
      }, deferred);
    if (auto* p = std::get_if<unique_ptr<const HttpClientResponse>>(&v)) {
      co_return std::move(*p);
    }
    rethrow_exception(std::get<1>(std::move(v)));
  }
}
