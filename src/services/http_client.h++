#pragma once
#include "util/common.h++"
#include <asio.hpp>

namespace Ludwig {
  class HttpClient;

  class HttpClientResponse {
  public:
    virtual inline ~HttpClientResponse() {};
    virtual auto status() const -> uint16_t = 0;
    virtual auto header(std::string_view name) const -> std::string_view = 0;
    virtual auto body() const -> std::string_view = 0;
  };

  class HttpClientError : public std::runtime_error {
  public:
    bool transient;
    HttpClientError(std::string url, std::string_view message, bool transient = false)
      : runtime_error(fmt::format("HTTP client request to {} failed: {}", url, message)), transient(transient) {}
    HttpClientError(const Url& url, std::string_view message, bool transient = false)
      : HttpClientError(url.to_string(), message, transient) {}
  };

  struct HttpClientRequest {
    Url url;
    std::string method, request;
    bool throw_on_error_status = false, has_body = false;

    HttpClientRequest(std::string url_str, std::string method)
      : url(*Url::parse(url_str).or_else([&url_str] -> std::optional<Url> {
          throw HttpClientError(url_str, "Invalid HTTP URL");
        })),
        method(method)
    {
      if (!url.is_http_s()) throw HttpClientError(url_str, "Not an HTTP(S) URL");
      fmt::format_to(
        std::back_inserter(request), "{} {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nUser-Agent: ludwig",
        method, url.path.empty() ? "/" : url.path, url.host
      );
    }

    inline auto redirect(std::string new_url) -> void {
      if (new_url.starts_with("/")) {
        new_url = fmt::format("{}://{}{}", url.scheme, url.host, new_url);
      }
      url = *Url::parse(new_url).or_else([&] -> std::optional<Url> {
        throw HttpClientError(url, fmt::format("Redirect to invalid HTTP URL: {}", new_url));
      });
      if (!url.is_http_s()) {
        throw HttpClientError(url, fmt::format("Redirect to non-HTTP(S) URL: {}", new_url));
      }
      const auto request_suffix = request.substr(request.find("User-Agent: ludwig\r\n") + 18);
      request.clear();
      fmt::format_to(
        std::back_inserter(request), "{} {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nUser-Agent: ludwig",
        method, url.path.empty() ? "/" : url.path, url.host
      );
      request.append(request_suffix);
    }
  };

  struct HttpClientRequestBuilder {
    HttpClient& client;
    std::unique_ptr<HttpClientRequest> req;

    HttpClientRequestBuilder(HttpClient& client, std::string url, std::string method)
      : client(client), req(std::make_unique<HttpClientRequest>(url, method)) {}

    inline auto header(std::string_view header, std::string_view value)&& -> HttpClientRequestBuilder&& {
      assert(!req->has_body);
      fmt::format_to(std::back_inserter(req->request), "\r\n{}: {}", header, value);
      return std::move(*this);
    }

    inline auto body(std::string_view content_type, std::string_view body)&& -> HttpClientRequestBuilder&&{
      assert(!req->has_body);
      fmt::format_to(
        std::back_inserter(req->request), "\r\nContent-Type: {}\r\nContent-Length: {:x}\r\n\r\n{}",
        content_type, body.length(), body
      );
      req->has_body = true;
      return std::move(*this);
    }

    inline auto throw_on_error_status()&& -> HttpClientRequestBuilder&&{
      req->throw_on_error_status = true;
      return std::move(*this);
    }

    auto dispatch()&& -> Async<std::unique_ptr<const HttpClientResponse>>;
  };

  class HttpClient {
  protected:
    virtual auto fetch(std::unique_ptr<HttpClientRequest> req) -> Async<std::unique_ptr<const HttpClientResponse>> = 0;
  public:
    virtual inline ~HttpClient() {};
    inline auto get(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "GET");
    }
    inline auto post(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "POST");
    }
    inline auto put(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "PUT");
    }
    inline auto delete_(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "DELETE");
    }

    friend struct HttpClientRequestBuilder;
  };

  inline auto HttpClientRequestBuilder::dispatch()&& -> Async<std::unique_ptr<const HttpClientResponse>> {
    if (!req->has_body) req->request.append("\r\n\r\n");
    return client.fetch(std::move(req));
  }
}
