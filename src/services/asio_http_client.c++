#include "asio_http_client.h++"
#include "asio/ip/address_v4.hpp"
#include "asio/ip/basic_resolver_results.hpp"
#include "asio/ssl/host_name_verification.hpp"
#include "spdlog/spdlog.h"
#include "util/web.h++"
#include <filesystem>
#include <stdexcept>
#include <uWebSockets/HttpParser.h>
#include <asio/experimental/parallel_group.hpp>
#include <openssl/rand.h>
#include <chrono>

using namespace std::literals;
using asio::deferred, asio::error_code, asio::io_context, asio::ip::tcp,
    asio::redirect_error, std::exception_ptr, std::make_unique,
    asio::ip::address, asio::ip::address_v4,
    std::match_results, std::nullopt, std::optional, std::regex,
    std::regex_match, std::rethrow_exception, std::runtime_error,
    std::shared_ptr, std::string, std::string_view, std::unique_ptr;
namespace ssl = asio::ssl;

namespace Ludwig {
  static const string uws_http_errors[] = {
      "",
      "Invalid HTTP response: possibly wrong Content-Length",
      "Response header fields too large",
      "Invalid HTTP response",
      "This error should never happen (404, but when parsing response?)"
    },
    cert_file_name = "ca-certificates.crt";

  AsioHttpClient::AsioHttpClient(
    shared_ptr<io_context> io,
    uint32_t req_per_5min,
    UnsafeHttps unsafe_https,
    UnsafeLocalRequests unsafe_local_requests
  ) : io(io),
      ssl(ssl::context::sslv23),
      rate_limiter((double)req_per_5min / 300.0, req_per_5min),
      safe_https(unsafe_https == SAFE_HTTPS),
      safe_local_requests(unsafe_local_requests == SAFE_LOCAL_REQUESTS) {
    if (safe_https) {
      ssl.set_verify_mode(ssl::verify_peer | ssl::context::verify_fail_if_no_peer_cert);
      ssl.set_default_verify_paths();
      const char* dirname = getenv(X509_get_default_cert_dir_env());
      if (!dirname) dirname = X509_get_default_cert_dir();
      auto cert_complete_path = std::filesystem::absolute(dirname) / cert_file_name;
      if (!std::filesystem::exists(cert_complete_path)) {
        spdlog::critical("Cannot find system SSL root certificates at {}", cert_complete_path.string());
        spdlog::critical("This is a fatal error. If you want to run Ludwig anyway, without verifying HTTPS certificates, use --unsafe-https.");
        exit(1);
      }
      ssl.load_verify_file(cert_complete_path);
    }
  }

  using TcpSocket = asio::basic_stream_socket<tcp, io_context::executor_type>;
  using SslSocket = ssl::stream<TcpSocket>;

  class AsioHttpClientResponse : public HttpClientResponse {
    static inline constexpr size_t MAX_RESPONSE_BYTES = 64 * MiB;
    static inline auto random_uint64() -> uint64_t {
      uint64_t n;
      RAND_pseudo_bytes((uint8_t*)&n, sizeof(uint64_t));
      return n;
    }
    // This is random to ensure that servers cannot break this client by spoofing this header
    static const inline string response_header_key = fmt::format("x-{:016x}", random_uint64());
    uint16_t _status = 0;
    string fake_request_buf = fmt::format("POST / HTTP/1.1\r\nHost: \r\n{}: ", response_header_key);
    string _body;
    optional<uWS::HttpRequest> fake_request;
  public:
    auto append(const void* data, size_t size) -> void {
      if (fake_request_buf.length() + size > MAX_RESPONSE_BYTES) {
        throw runtime_error(fmt::format("Response is larger than max of {} bytes", MAX_RESPONSE_BYTES));
      }
      fake_request_buf.append((const char*)data, size);
    }

    auto parse() -> void {
      // uWS parser expects buffer to be padded
      fake_request_buf.reserve(fake_request_buf.length() + 2);

      uWS::HttpParser parser;
      const auto [n, ret] = parser.consumePostPadded(fake_request_buf.data(), static_cast<unsigned>(fake_request_buf.length()), this, nullptr,
        [this](void* u, auto* req) -> void* {
          fake_request = { *req };
          return u;
        },
        [this](void* u, string_view data, bool) -> void* {
          _body += data;
          return u;
        }
      );
      // FULLPTR = error return, according to gnarly internal details of uWS
      if (ret == uWS::FULLPTR) throw runtime_error(uws_http_errors[n]);
      if (!fake_request) throw runtime_error("Incomplete HTTP response");
      const auto response_line = fake_request->getHeader(response_header_key);
      static const regex http_regex(R"((HTTP/[\d.]+)\s+(\d+)(?:\s+(\w[^\r\n]*))?)");
      match_results<string_view::const_iterator> match;
      if (!regex_match(response_line.cbegin(), response_line.cend(), match, http_regex)) {
        throw runtime_error(fmt::format("Invalid HTTP response: {}", response_line.substr(0, 128)));
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

  inline auto check_ec(const error_code& ec, string_view step) -> void {
    if (ec) throw runtime_error(fmt::format("{} (while {})", ec.message(), step));
  }

  template <typename Socket> struct AsioFetchCtx {
    tcp::resolver resolver;
    Socket socket;

    template <class... Args> AsioFetchCtx(io_context& io, Args&& ...args) : resolver(io), socket(std::forward<Args>(args)...) {}

    ~AsioFetchCtx() {
      resolver.cancel();
      close_socket(socket);
    }

    auto send_and_recv(HttpClientRequest& req, unique_ptr<AsioHttpClientResponse>& response) -> Async<void> {
      asio::streambuf rsp_buf;
      error_code ec;
      size_t bytes = co_await asio::async_write(socket, asio::buffer(req.request.data(), req.request.size()), redirect_error(deferred, ec));
      check_ec(ec, "writing HTTP request");
      while (
        (bytes = co_await asio::async_read(socket, rsp_buf, redirect_error(deferred, ec))),
        ec != asio::error::eof && ec != ssl::error::stream_truncated
      ) {
        check_ec(ec, "reading HTTP response");
        response->append(rsp_buf.data().data(), bytes);
        rsp_buf.consume(bytes);
      }
      response->append(rsp_buf.data().data(), bytes);
    }
  };

  auto AsioHttpClient::check_for_unsafe_local_requests(const asio::ip::basic_resolver_results<tcp>& endpoint_iterator, const Url& url) -> void {
    if (!safe_local_requests) return;
    for (const auto& endpoint : endpoint_iterator) {
      if (!is_safe_address(endpoint.endpoint().address())) {
        throw runtime_error("Host " + url.host + " resolves to an unsafe local network address (use --unsafe-local-requests to allow this)");
      }
    }
  }

  auto AsioHttpClient::https_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*> {
    error_code ec;
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<SslSocket> c(*io, *io, ssl);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req.url.host),
      req.url.port.empty() ? "https" : req.url.port,
      redirect_error(deferred, ec)
    );
    check_ec(ec, "resolving address");
    check_for_unsafe_local_requests(endpoint_iterator, req.url);
    c.socket.set_verify_mode(ssl::verify_peer);
    c.socket.set_verify_callback(asio::ssl::host_name_verification(req.url.host));
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, redirect_error(deferred, ec));
    check_ec(ec, "connecting");

    // SNI - This is barely documented, but it's necessary to connect to some HTTPS sites without a handshake error!
    // Based on https://stackoverflow.com/a/59225060/548027
    if (!SSL_set_tlsext_host_name(c.socket.native_handle(), req.url.host.c_str())) {
      check_ec({static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()}, "setting TLS host name");
    }

    co_await c.socket.async_handshake(ssl::stream_base::client, redirect_error(deferred, ec));
    check_ec(ec, "performing TLS handshake");
    co_await c.send_and_recv(req, response);
    co_return response.release();
  }

  auto AsioHttpClient::http_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*> {
    error_code ec;
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<TcpSocket> c(*io, *io);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req.url.host),
      req.url.port.empty() ? "http" : req.url.port,
      redirect_error(deferred, ec)
    );
    check_ec(ec, "resolving address");
    check_for_unsafe_local_requests(endpoint_iterator, req.url);
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, redirect_error(deferred, ec));
    check_ec(ec, "connecting");
    co_await c.send_and_recv(req, response);
    co_return response.release();
  }

  auto AsioHttpClient::fetch(HttpClientRequest&& req) -> Async<unique_ptr<const HttpClientResponse>> {
    spdlog::debug("CLIENT HTTP {} {}", req.method, req.url.to_string());
    for (uint8_t redirects = 0; redirects < 10; redirects++) {
      if (!co_await rate_limiter.try_acquire_or_asio_await(req.url.host, 30s)) {
        throw runtime_error("HTTP client rate limited (too many requests to the same host)");
      }
      asio::steady_timer timeout(*io, 30s);
      auto [order, ec, ex, rsp_ptr] = co_await asio::experimental::make_parallel_group(
        timeout.async_wait(deferred),
        asio::co_spawn(*io, req.url.scheme == "https" ? https_fetch(req) : http_fetch(req), deferred)
      ).async_wait(
        asio::experimental::wait_for_one(),
        deferred
      );
      if (order[0] == 0) {
        if (ec) throw asio::system_error(ec);
        throw runtime_error("Request timed out");
      }
      unique_ptr<AsioHttpClientResponse> response(rsp_ptr);
      if (ex) rethrow_exception(ex);
      response->parse();
      const auto status = response->status();
      switch (status) {
      case 301:
      case 302:
      case 303:
        // Don't follow basic redirects for non-GET requests
        if (req.method != "GET") co_return response;
        // fall through
      case 307:
      case 308:
        if (response->header("location").empty()) throw runtime_error("Got redirect with no Location header");
        else req.redirect(string(response->header("location")));
        break;
      default:
        co_return response;
      }
    }
    throw runtime_error("Too many redirects");
  }

  auto AsioHttpClient::fetch(HttpClientRequest&& from_req, HttpResponseCallback&& callback) -> void {
    asio::co_spawn(
      io->get_executor(),
      [this, req = std::move(from_req)] mutable { return fetch(std::move(req)); },
      [callback = std::move(callback)](exception_ptr ep, auto rsp) mutable {
        if (ep) {
          try { rethrow_exception(ep); }
          catch (const runtime_error& e) { callback(make_unique<ErrorHttpClientResponse>(e.what())); }
          catch (...) { callback(make_unique<ErrorHttpClientResponse>("Unknown error")); }
        } else {
          callback(std::move(rsp));
        }
      }
    );
  }

  auto is_safe_address(const address& addr) -> bool {
    if (addr.is_loopback() || addr.is_multicast() || addr.is_multicast()) {
      return false;
    }
    if (addr.is_v4()) {
      static const auto
        private_class_a = address_v4::from_string("10.0.0.0").to_ulong() >> 24,
        private_class_b = address_v4::from_string("172.16.0.0").to_ulong() >> 20,
        private_class_c = address_v4::from_string("192.168.0.0").to_ulong() >> 16,
        private_apipa = address_v4::from_string("169.254.0.0").to_ulong() >> 16,
        private_cgnat = address_v4::from_string("100.64.0.0").to_ulong() >> 22;
      const auto addr4 = addr.to_v4().to_ulong();
      return
        (addr4 >> 24) != private_class_a &&
        (addr4 >> 20) != private_class_b &&
        (addr4 >> 16) != private_class_c &&
        (addr4 >> 16) != private_apipa &&
        (addr4 >> 22) != private_cgnat;
    } else {
      const auto& addr6 = addr.to_v6();
      if (addr6.is_link_local()) return false;
      if (addr6.is_v4_mapped()) return is_safe_address(addr6.to_v4());
      return (addr6.to_bytes()[0] & 0xfc) != 0xfc;
    }
  }
}
