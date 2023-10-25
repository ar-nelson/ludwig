#include "asio_http_client.h++"
#include "util/web.h++"
#include <uWebSockets/HttpParser.h>
#include <spdlog/spdlog.h>
#include <chrono>

using namespace std::literals;
using std::bind, std::make_shared, std::nullopt, std::optional,
    std::placeholders::_1, std::placeholders::_2, std::regex, std::regex_match,
    std::runtime_error, std::shared_ptr, std::smatch, std::string,
    std::string_view, asio::ip::tcp;
namespace ssl = asio::ssl;

namespace Ludwig {
  AsioHttpClient::AsioHttpClient(
    shared_ptr<asio::io_context> io,
    shared_ptr<ssl::context> ssl
  ) : io(io), work(io->get_executor()), ssl(ssl), resolver(*io) {}

  // Based on https://github.com/alexandruc/SimpleHttpsClient/blob/712908677f1380a47ff8460ad05d3a8a78c9d599/https_client.cpp
  class AsioFetch : public HttpClientResponse, public std::enable_shared_from_this<AsioFetch> {
  private:
    static inline constexpr size_t MAX_RESPONSE_BYTES = 1024 * 1024 * 64; // 64MiB
    asio::io_context& io;
    ssl::context& ssl;
    asio::steady_timer timeout;
    HttpClientRequest request;
    HttpResponseCallback callback;
    tcp::resolver resolver;
    ssl::stream<tcp::socket> socket;
    asio::streambuf req_buf, rsp_buf;
    uWS::HttpParser parser;
    uint16_t _status;
    string response, _body;
    optional<uWS::HttpRequest> response_as_fake_request;

    auto die(string err) -> void {
      timeout.cancel();
      resolver.cancel();
      socket.shutdown();
      callback(make_shared<ErrorHttpClientResponse>(err));
    }

    auto complete() -> void {
      timeout.cancel();
      switch (_status) {
      case 301:
      case 302:
      case 303:
      case 307:
      case 308:
        if (response_as_fake_request->getHeader("location").empty()) {
          die("Got redirect with no Location header");
        } else try {
          string location(response_as_fake_request->getHeader("location"));
          auto fetch = make_shared<AsioFetch>(io, ssl, request.with_new_url(location), callback);
          socket.shutdown();
          fetch->run();
        } catch (const runtime_error& e) {
          die(e.what());
        }
        break;
      default:
        socket.shutdown();
        callback(shared_from_this());
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
      socket.set_verify_callback([self = weak_from_this()](bool preverified, ssl::verify_context& ctx) {
        if (auto ptr = self.lock()) {
          return ptr->on_verify_certificate(preverified, ctx);
        }
        return false;
      });

      //spdlog::info("Connecting to {}", endpoint_iterator->endpoint().address().to_string());

      asio::async_connect(socket.lowest_layer(), endpoint_iterator, bind(&AsioFetch::on_connect, shared_from_this(), _1, _2));
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
      }

      socket.async_handshake(ssl::stream_base::client, bind(&AsioFetch::on_handshake, shared_from_this(), _1));
    }

    auto on_handshake(const asio::error_code& ec) -> void {
      if (ec) {
        die(fmt::format("TCP handshake error connecting to {}: {}", request.url, ec.message()));
        return;
      }
      std::ostream(&req_buf) << request.request;
      asio::async_write(socket, req_buf, bind(&AsioFetch::on_write, shared_from_this(), _1, _2));
    }

    auto on_write(const asio::error_code& ec, size_t) -> void {
      if (ec) {
        die(fmt::format("Error sending HTTP request to {}: {}", request.url, ec.message()));
        return;
      }
      asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read, shared_from_this(), _1, _2));
    }

    inline auto parse(char* data, size_t length) -> int {
      const auto [n, ret] = parser.consumePostPadded(data, static_cast<unsigned>(length), this, nullptr,
        [&](void* u, auto* req) -> void* {
          response_as_fake_request = { *req };
          return u;
        },
        [&](void* u, string_view data, bool) -> void* {
          _body.append(data);
          return u;
        }
      );
      // FULLPTR = error return, according to gnarly internal details of uWS
      if (ret == uWS::FULLPTR) {
        // FIXME: This returns a meaningless HTTP error, need to parse these errors into something more meaningful
        die(fmt::format("HTTP parser error on response from {}: {}", request.url, uWS::httpErrorResponses[n]));
        return -1;
      }
      return 0;
    }

    auto on_read(const asio::error_code& ec, size_t bytes) -> void {
      // We have to account for 'stream truncated' errors because misbehaved
      // servers will just truncate streams instead of closing sometimes…
      if (ec && ec != asio::error::eof && ec != ssl::error::stream_truncated) {
        die(fmt::format("Error reading HTTP response from {}: {}", request.url, ec.message()));
        return;
      }
      if (response.length() + bytes > MAX_RESPONSE_BYTES) {
        die(fmt::format("HTTP response from {} is larger than max of {} bytes", request.url, MAX_RESPONSE_BYTES));
        return;
      }
      response.append(string_view((char*)rsp_buf.data().data(), bytes));
      if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
        const auto first_newline = response.find_first_of('\r');
        const auto first_line = response.substr(0, first_newline);
        static const regex http_regex(R"((HTTP/[\d.]+)\s+(\d+)(?:\s+(\w[^\r\n]*))?)");
        smatch match;
        if (!regex_match(first_line, match, http_regex)) {
          die(fmt::format("Invalid HTTP response from {}: {}", request.url, first_line.substr(0, 128)));
          return;
        }
        _status = static_cast<uint16_t>(std::stoi(match.str(2)));
        {
          // This looks unnecessary. It isn't.
          // Removing this temporary variable will lead to segfaults.
          auto fake_request = fmt::format("POST / {}\r\nHost: {}",
            match.str(1) == "HTTP/1.0" ? "HTTP/1.1" : match.str(1),
            response.substr(first_newline)
          );
          response = fake_request;
        }
        auto result = parse(response.data(), response.length());
        if (result < 0) return;
        complete();
      } else {
        rsp_buf.consume(bytes);
        asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read, shared_from_this(), _1, _2));
      }
    }

  public:
    AsioFetch(asio::io_context& io, ssl::context& ssl, HttpClientRequest&& request, HttpResponseCallback callback)
      : io(io), ssl(ssl), timeout(io, 1min), request(request), callback(callback), resolver(io), socket(io, ssl) {}

    auto status() const -> uint16_t { return _status; }
    auto header(string_view name) const -> string_view {
      if (response_as_fake_request) {
        auto* req = const_cast<uWS::HttpRequest*>(&*response_as_fake_request);
        return req->getHeader(name);
      }
      return string_view(nullptr, 0);
    }
    auto body() const -> string_view { return _body; }
    auto error() const -> optional<string_view> {
      return _status >= 400 ? optional(http_status(_status)) : nullopt;
    };

    inline auto run() -> void {
      timeout.async_wait([self = weak_from_this()](const asio::error_code& ec) {
        if (ec) return;
        if (auto ptr = self.lock()) ptr->die("Request timed out");
      });
      resolver.async_resolve(string_view(request.host), "https", bind(&AsioFetch::on_resolve, shared_from_this(), _1, _2));
    }
  };

  auto AsioHttpClient::fetch(HttpClientRequest&& req, HttpResponseCallback callback) -> void {
    spdlog::debug("CLIENT HTTP {} {}", req.method, req.url);
    auto s = make_shared<AsioFetch>(*io, *ssl, std::move(req), callback);
    s->run();
  }
}
