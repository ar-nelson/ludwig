#include "asio_http_client.h++"
#include "util/web.h++"
#include <uWebSockets/HttpParser.h>
#include <duthomhas/csprng.hpp>
#include <chrono>

using namespace std::literals;
using std::bind, std::make_shared, std::make_unique, std::match_results,
      std::nullopt, std::optional, std::placeholders::_1, std::placeholders::_2,
      std::regex, std::regex_match, std::runtime_error, std::shared_ptr,
      std::string, std::string_view, std::unique_ptr, asio::ip::tcp;
namespace ssl = asio::ssl;

namespace Ludwig {
  AsioHttpClient::AsioHttpClient(
    shared_ptr<asio::io_context> io,
    shared_ptr<ssl::context> ssl
  ) : io(io), work(io->get_executor()), ssl(ssl), resolver(*io) {}

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

  // Based on https://github.com/alexandruc/SimpleHttpsClient/blob/712908677f1380a47ff8460ad05d3a8a78c9d599/https_client.cpp
  class AsioFetch {
  private:
    asio::io_context& io;
    ssl::context& ssl;
    asio::steady_timer timeout;
    HttpClientRequest request;
    HttpResponseCallback callback;
    tcp::resolver resolver;
    ssl::stream<tcp::socket> socket;
    asio::streambuf req_buf, rsp_buf;
    unique_ptr<AsioHttpClientResponse> response;

    auto die(string err) -> void {
      callback(make_unique<ErrorHttpClientResponse>(err));
      delete this;
    }

    auto complete() -> void {
      timeout.cancel();
      switch (response->status()) {
      case 301:
      case 302:
      case 303:
      case 307:
      case 308:
        if (response->header("location").empty()) {
          die("Got redirect with no Location header");
        } else try {
          string location(response->header("location"));
          new AsioFetch(io, ssl, request.with_new_url(location), std::move(callback));
          delete this;
        } catch (const runtime_error& e) {
          die(e.what());
        }
        break;
      default:
        callback(std::move(response));
        delete this;
        break;
      }
    }

    auto on_resolve(
      const asio::error_code& ec,
      tcp::resolver::iterator endpoint_iterator
    ) -> void {
      if (ec) {
        die(fmt::format("Error resolving {}: {}", request.url, ec.message()));
        return;
      }
      socket.set_verify_mode(ssl::verify_peer);
      socket.set_verify_callback(bind(&AsioFetch::on_verify_certificate, this, _1, _2));

      //spdlog::info("Connecting to {}", endpoint_iterator->endpoint().address().to_string());

      asio::async_connect(socket.lowest_layer(), endpoint_iterator, bind(&AsioFetch::on_connect, this, _1, _2));
    }

    auto on_verify_certificate(bool /*preverified*/, ssl::verify_context& ctx) -> bool {
      // TODO: Actually verify SSL certificates
      char subject_name[256];
      X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
      X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
      //spdlog::debug("Verifying SSL cert: {} (preverified: {})", subject_name, preverified);
      return true;
    }

    auto on_connect(const asio::error_code& ec, tcp::resolver::iterator) -> void {
      if (ec) {
        die(fmt::format("Error connecting to {}: {}", request.url, ec.message()));
        return;
      }

      // SNI - This is barely documented, but it's necessary to connect to some HTTPS sites without a handshake error!
      // Based on https://stackoverflow.com/a/59225060/548027
      if (!SSL_set_tlsext_host_name(socket.native_handle(), request.host.c_str())) {
        const asio::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
        die(fmt::format("Error setting TLS host name for {}: {}", request.url, ec.message()));
        return;
      }

      socket.async_handshake(ssl::stream_base::client, bind(&AsioFetch::on_handshake, this, _1));
    }

    auto on_handshake(const asio::error_code& ec) -> void {
      if (ec) {
        die(fmt::format("TCP handshake error connecting to {}: {}", request.url, ec.message()));
        return;
      }
      std::ostream(&req_buf) << request.request;
      asio::async_write(socket, req_buf, bind(&AsioFetch::on_write, this, _1, _2));
    }

    auto on_write(const asio::error_code& ec, size_t) -> void {
      if (ec) {
        die(fmt::format("Error sending HTTP request to {}: {}", request.url, ec.message()));
        return;
      }
      asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read, this, _1, _2));
    }

    auto on_read(const asio::error_code& ec, size_t bytes) -> void {
      // We have to account for 'stream truncated' errors because misbehaved
      // servers will just truncate streams instead of closing sometimes…
      if (ec && ec != asio::error::eof && ec != ssl::error::stream_truncated) {
        die(fmt::format("Error reading HTTP response from {}: {}", request.url, ec.message()));
        return;
      }
      try {
        response->append(rsp_buf.data().data(), bytes);
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
          response->parse();
          complete();
        } else {
          rsp_buf.consume(bytes);
          asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read, this, _1, _2));
        }
      } catch (const runtime_error& e) {
        die(fmt::format("Error reading HTTP response from {}: {}", request.url, e.what()));
      }
    }

  public:
    AsioFetch(asio::io_context& io, ssl::context& ssl, HttpClientRequest&& request, HttpResponseCallback&& callback)
      : io(io), ssl(ssl), timeout(io, 1min), request(request), callback(std::move(callback)), resolver(io), socket(io, ssl), response(make_unique<AsioHttpClientResponse>()) {
      timeout.async_wait([this](const asio::error_code& ec) {
        if (ec) return;
        die("Request timed out");
      });
      resolver.async_resolve(string_view(request.host), "https", bind(&AsioFetch::on_resolve, this, _1, _2));
    }
    ~AsioFetch() {
      timeout.cancel();
      resolver.cancel();
      socket.shutdown();
    }
  };

  auto AsioHttpClient::fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void {
    spdlog::debug("CLIENT HTTP {} {}", req.method, req.url);
    new AsioFetch(*io, *ssl, std::move(req), std::move(callback));
  }
}
