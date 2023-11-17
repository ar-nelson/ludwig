#include "asio_http_client.h++"
#include "util/web.h++"
#include <uWebSockets/HttpParser.h>
#include <duthomhas/csprng.hpp>
#include <chrono>

using namespace std::literals;
using std::make_unique, std::match_results, std::nullopt, std::optional,
    std::regex, std::regex_match, std::runtime_error, std::shared_ptr,
    std::string, std::string_view, std::unique_ptr, asio::ip::tcp;
namespace ssl = asio::ssl;

namespace Ludwig {
  AsioHttpClient::AsioHttpClient(
    shared_ptr<asio::io_context> io,
    shared_ptr<ssl::context> ssl
  ) : io(io), work(io->get_executor()), ssl(ssl) {}

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

    inline auto append(const void* data, size_t size) -> void {
      if (fake_request_buf.length() + size > MAX_RESPONSE_BYTES) {
        throw runtime_error(fmt::format("Response is larger than max of {} bytes", MAX_RESPONSE_BYTES));
        return;
      }
      fake_request_buf.append((const char*)data, size);
    }

    inline auto parse() -> void {
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
        throw runtime_error(string(uWS::httpErrorResponses[n]));
      }
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

  template<> inline auto close_socket<ssl::stream<tcp::socket>>(ssl::stream<tcp::socket>& s) -> void {
    try { s.shutdown(); } catch (...) { /* ignore "stream truncated", which may happen here */ }
  }

  template <typename Socket> struct AsioFetchCtx {
    asio::steady_timer timeout;
    tcp::resolver resolver;
    Socket socket;

    template <class... Args> AsioFetchCtx(asio::io_context& io, Args&& ...args) : timeout(io, 30s), resolver(io), socket(std::forward<Args>(args)...) {
      timeout.async_wait([&](asio::error_code ec) {
        if (ec) return;
        resolver.cancel();
        close_socket(socket);
      });
    }

    ~AsioFetchCtx() {
      timeout.cancel();
      resolver.cancel();
      close_socket(socket);
    }

    inline auto send_and_recv(unique_ptr<HttpClientRequest>& req, unique_ptr<AsioHttpClientResponse>& response) -> asio::awaitable<void> {
      asio::streambuf req_buf, rsp_buf;
      asio::error_code ec;
      req_buf.sputn(req->request.data(), (std::streamsize)req->request.size());
      size_t bytes = co_await asio::async_write(socket, req_buf, asio::use_awaitable);
      while (
        (bytes = co_await asio::async_read(socket, rsp_buf, asio::redirect_error(asio::use_awaitable, ec))),
        ec != asio::error::eof && ec != ssl::error::stream_truncated
      ) {
        if (ec) throw runtime_error(fmt::format("Error reading HTTP response: {}", ec.message()));
        response->append(rsp_buf.data().data(), bytes);
        rsp_buf.consume(bytes);
      }
      response->append(rsp_buf.data().data(), bytes);
    }
  };

  auto AsioHttpClient::https_fetch(unique_ptr<HttpClientRequest>& req) -> asio::awaitable<unique_ptr<AsioHttpClientResponse>> {
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<ssl::stream<tcp::socket>> c(*io, *io, *ssl);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req->url.host),
      req->url.port.empty() ? "https" : req->url.port,
      asio::use_awaitable
    );
    c.socket.set_verify_mode(ssl::verify_peer);
    c.socket.set_verify_callback([](bool /*preverified*/, ssl::verify_context& ctx) {
      // TODO: Actually verify SSL certificates
      char subject_name[256];
      X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
      X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
      //spdlog::debug("Verifying SSL cert: {} (preverified: {})", subject_name, preverified);
      return true;
    });
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, asio::use_awaitable);

    // SNI - This is barely documented, but it's necessary to connect to some HTTPS sites without a handshake error!
    // Based on https://stackoverflow.com/a/59225060/548027
    if (!SSL_set_tlsext_host_name(c.socket.native_handle(), req->url.host.c_str())) {
      const asio::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
      throw runtime_error(fmt::format("Error setting TLS host name: {}", ec.message()));
    }

    co_await c.socket.async_handshake(ssl::stream_base::client, asio::use_awaitable);
    co_await c.send_and_recv(req, response);
    co_return response;
  }

  auto AsioHttpClient::http_fetch(unique_ptr<HttpClientRequest>& req) -> asio::awaitable<unique_ptr<AsioHttpClientResponse>> {
    auto response = make_unique<AsioHttpClientResponse>();
    AsioFetchCtx<asio::buffered_stream<tcp::socket>> c(*io, *io);
    auto endpoint_iterator = co_await c.resolver.async_resolve(
      string_view(req->url.host),
      req->url.port.empty() ? "http" : req->url.port,
      asio::use_awaitable
    );
    co_await asio::async_connect(c.socket.lowest_layer(), endpoint_iterator, asio::use_awaitable);
    co_await c.send_and_recv(req, response);
    co_return response;
  }

  auto AsioHttpClient::fetch(unique_ptr<HttpClientRequest> req)
    -> asio::awaitable<unique_ptr<const HttpClientResponse>> {
    spdlog::debug("CLIENT HTTP {} {}", req->method, req->url.to_string());
    try {
      for (uint8_t redirects = 0; redirects < 10; redirects++) {
        auto response = req->url.scheme == "https" ? co_await https_fetch(req) : co_await http_fetch(req);
        response->parse();
        switch (response->status()) {
        case 301:
        case 302:
        case 303:
        case 307:
        case 308:
          if (response->header("location").empty()) throw runtime_error("Got redirect with no Location header");
          else req->redirect(string(response->header("location")));
          break;
        default:
          co_return response;
        }
      }
      throw runtime_error("Too many redirects");
    } catch (const std::exception& e) {
      co_return make_unique<ErrorHttpClientResponse>(fmt::format("HTTP client request to {} failed: {}", req->url.to_string(), e.what()));
    }
  }
}
