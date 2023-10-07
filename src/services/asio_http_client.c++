#include "asio_http_client.h++"
#include <uWebSockets/HttpParser.h>
#include <spdlog/spdlog.h>
#include <chrono>

using namespace std::literals;
using std::bind, std::placeholders::_1, std::placeholders::_2, std::optional, std::nullopt, std::string, std::string_view, asio::ip::tcp;
namespace ssl = asio::ssl;

namespace Ludwig {
  class UwsHttpClientResponse : public HttpClientResponse {
  private:
    uint16_t _status;
    uWS::HttpRequest _request;
    string _body;
    optional<string> _error;
  public:
    UwsHttpClientResponse(uint16_t status, uWS::HttpRequest&& request, string body, optional<string> error = {})
      : _status(status), _request(request), _body(body), _error(error) {}

    auto status() -> uint16_t { return _status; }
    auto header(string_view name) -> string_view { return _request.getHeader(name); }
    auto body() -> string_view { return _body; }
    auto error() -> optional<string_view> { return _error.transform([](auto s) { return s; }); };
  };

  AsioHttpClient::AsioHttpClient(
    std::shared_ptr<asio::io_context> io,
    std::shared_ptr<ssl::context> ssl
  ) : io(io), work(io->get_executor()), ssl(ssl), resolver(*io) {}

  // Based on https://github.com/alexandruc/SimpleHttpsClient/blob/712908677f1380a47ff8460ad05d3a8a78c9d599/https_client.cpp
  class AsioFetch : public std::enable_shared_from_this<AsioFetch> {
  private:
    asio::io_context& io;
    ssl::context& ssl;
    asio::steady_timer timeout;
    HttpClientRequest request;
    HttpResponseCallback callback;
    tcp::resolver resolver;
    ssl::stream<tcp::socket> socket;
    asio::streambuf req_buf, rsp_buf;
    uWS::HttpParser parser;
    uint16_t status;
    string status_message;
    optional<uWS::HttpRequest> response_as_fake_request;
    std::ostringstream body;

    auto die(string err) -> void {
      timeout.cancel();
      socket.async_shutdown([](const asio::error_code&){});
      callback(std::make_unique<ErrorHttpClientResponse>(err));
    }

    auto complete() -> void {
      timeout.cancel();
      switch (status) {
      case 301:
      case 302:
      case 303:
      case 307:
      case 308:
        if (response_as_fake_request->getHeader("location").empty()) {
          die("Got redirect with no Location header");
        } else {
          socket.async_shutdown([](const asio::error_code&){});
          std::make_shared<AsioFetch>(io, ssl, request.with_new_url(response_as_fake_request->getHeader("location")), callback)->run();
        }
        break;
      default:
        socket.async_shutdown([](const asio::error_code&){});
        callback(std::make_unique<UwsHttpClientResponse>(
          status,
          std::move(*response_as_fake_request),
          body.str(),
          status < 400 ? nullopt : optional(fmt::format("{:d} {}", status, status_message))
        ));
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
      spdlog::debug("Resolve OK");
      socket.set_verify_mode(ssl::verify_peer);
      socket.set_verify_callback(bind(&AsioFetch::on_verify_certificate, shared_from_this(), _1, _2));
      asio::async_connect(socket.lowest_layer(), endpoint_iterator, bind(&AsioFetch::on_connect, shared_from_this(), _1, _2));
    }

    auto on_verify_certificate(bool preverified, ssl::verify_context& ctx) -> bool {
      // TODO: Actually verify SSL certificates
      char subject_name[256];
      X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
      X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
      spdlog::debug("Verifying SSL cert: {} (preverified: {})", subject_name, preverified);
      return true;
    }

    auto on_connect(const asio::error_code& ec, tcp::resolver::iterator) -> void {
      if (ec) {
        die(fmt::format("Error connecting to {}: {}", request.url, ec.message()));
        return;
      }
      spdlog::debug("Connect OK");
      socket.async_handshake(ssl::stream_base::client, bind(&AsioFetch::on_handshake, shared_from_this(), _1));
    }

    auto on_handshake(const asio::error_code& ec) -> void {
      if (ec) {
        die(fmt::format("TCP handshake error connecting to {}: {}", request.url, ec.message()));
        return;
      }
      spdlog::debug("Handshake OK");
      spdlog::debug("Request: {}", request.request);
      std::ostream(&req_buf) << request.request;
      asio::async_write(socket, req_buf, bind(&AsioFetch::on_write, shared_from_this(), _1, _2));
    }

    auto on_write(const asio::error_code& ec, size_t bytes) -> void {
      if (ec) {
        die(fmt::format("Error sending HTTP request to {}: {}", request.url, ec.message()));
        return;
      }
      spdlog::debug("Write OK; sent {:d} bytes", bytes);
      asio::async_read_until(socket, rsp_buf, "\r\n\r\n", bind(&AsioFetch::on_read_headers, shared_from_this(), _1, _2));
    }

    auto parse(char* data, size_t length) -> int {
      size_t remaining = length;
      bool done;
      while (remaining) {
        const auto [n, ret] = parser.consumePostPadded(data, static_cast<unsigned>(length), nullptr, nullptr,
          [&](void* u, auto* req) -> void* {
            response_as_fake_request = { *req };
            return u;
          },
          [&](void* u, auto data, bool fin) -> void* {
            body << data;
            done = fin;
            return u;
          }
        );
        // FULLPTR = error return, according to gnarly internal details of uWS
        if (ret == uWS::FULLPTR) {
          // FIXME: This returns a meaningless HTTP error, need to parse these errors into something more meaningful
          die(fmt::format("HTTP parser error on response from {}: {}", request.url, uWS::httpErrorResponses[n]));
          return -1;
        }
        if (!n) break;
        remaining -= n;
      }
      return done ? 1 : 0;
    }

    auto on_read_headers(const asio::error_code& ec, size_t bytes) -> void {
      if (ec && ec != asio::error::eof) {
        die(fmt::format("Error reading HTTP response from {}: {}", request.url, ec.message()));
        return;
      }
      spdlog::debug("Read OK; got {:d} bytes", bytes);
      static const std::regex http_regex(R"((HTTP/[\d.]+)\s+(\d+)(?:\s+(\w[^\r\n]*))?((?:\r\n[^\r\n]+)*(?:\r\n\r\n)?))", std::regex_constants::multiline);
      string s((char*)rsp_buf.data().data(), bytes);
      std::smatch match;
      if (!std::regex_match(s, match, http_regex)) {
        die(fmt::format("Invalid HTTP response from {}: {}", request.url, s));
        return;
      }
      status = static_cast<uint16_t>(std::stoi(match.str(2)));
      status_message = match.str(3);
      // Pretend the response is a POST request so uWS can parse it
      const auto fake_request = fmt::format("POST / {}\r\nHost: {}", match.str(1) == "HTTP/1.0" ? "HTTP/1.1" : match.str(1), match.str(4));
      if (parse((char*)(fake_request.data()), fake_request.length()) < 0) return;
      if (ec == asio::error::eof) complete();
      else {
        rsp_buf.consume(bytes);
        asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read_content, shared_from_this(), _1, _2));
      }
    }

    auto on_read_content(const asio::error_code& ec, size_t bytes) -> void {
      // We have to account for 'stream truncated' errors because misbehaved
      // servers will just truncate streams instead of closing sometimes…
      if (ec && ec != asio::error::eof && ec != ssl::error::stream_truncated) {
        die(fmt::format("Error reading HTTP response from {}: {}", request.url, ec.message()));
        return;
      }
      spdlog::debug("Read OK; got {:d} bytes", bytes);
      int result = parse((char*)rsp_buf.data().data(), bytes);
      if (result < 0) return;
      else if (ec == asio::error::eof || ec == ssl::error::stream_truncated) complete();
      else {
        rsp_buf.consume(bytes);
        asio::async_read(socket, rsp_buf, bind(&AsioFetch::on_read_content, shared_from_this(), _1, _2));
      }
    }

  public:
    AsioFetch(asio::io_context& io, ssl::context& ssl, HttpClientRequest&& request, HttpResponseCallback callback)
      : io(io), ssl(ssl), timeout(io, 1min), request(request), callback(callback), resolver(io), socket(io, ssl) {}

    inline auto run() -> void {
      timeout.async_wait([&](const asio::error_code& ec) { if (!ec) die("Request timed out"); });
      resolver.async_resolve(string_view(request.host), "https", bind(&AsioFetch::on_resolve, shared_from_this(), _1, _2));
    }
  };

  auto AsioHttpClient::fetch(HttpClientRequest&& req, HttpResponseCallback callback) -> void {
    std::make_shared<AsioFetch>(*io, *ssl, std::move(req), callback)->run();
  }
}
