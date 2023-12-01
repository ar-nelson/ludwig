#pragma once
#include "util/common.h++"

namespace Ludwig {
  class HttpClient;

  class HttpClientResponse {
  public:
    virtual inline ~HttpClientResponse() {};
    virtual auto status() const -> uint16_t = 0;
    virtual auto error() const -> std::optional<std::string_view> = 0;
    virtual auto header(std::string_view name) const -> std::string_view = 0;
    virtual auto body() const -> std::string_view = 0;
  };

  class ErrorHttpClientResponse : public HttpClientResponse {
  private:
    std::string msg;
  public:
    ErrorHttpClientResponse(std::string msg) : msg(msg) {}
    auto status() const -> uint16_t { return 0; };
    auto error() const -> std::optional<std::string_view> { return { msg }; };
    auto header(std::string_view) const -> std::string_view { return { nullptr, 0 }; };
    auto body() const -> std::string_view { return { nullptr, 0 }; };
  };

  using HttpResponseCallback = uWS::MoveOnlyFunction<void (std::unique_ptr<const HttpClientResponse>&&)>;

  struct HttpClientRequest {
    Url url;
    std::string method, request;
    bool has_body = false;

    HttpClientRequest(std::string url_str, std::string method)
      : url(*Url::parse(url_str).or_else([&url_str] -> std::optional<Url> {
          throw std::runtime_error("Invalid HTTP URL: " + url_str);
        })),
        method(method)
    {
      if (!url.is_http_s()) throw std::runtime_error("Not an HTTP(S) URL: " + url_str);
      fmt::format_to(
        std::back_inserter(request), "{} {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nUser-Agent: ludwig",
        method, url.path.empty() ? "/" : url.path, url.host
      );
    }

    auto redirect(std::string new_url) -> void {
      if (new_url.starts_with("/")) {
        new_url = fmt::format("{}://{}{}", url.scheme, url.host, new_url);
      }
      url = *Url::parse(new_url).or_else([&] -> std::optional<Url> {
        throw std::runtime_error("Redirect to invalid HTTP URL: " + new_url);
      });
      if (!url.is_http_s()) {
        throw std::runtime_error("Redirect to non-HTTP(S) URL: " + new_url);
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
    HttpClientRequest req;

    HttpClientRequestBuilder(HttpClient& client, std::string url, std::string method)
      : client(client), req(url, method) {}

    auto header(std::string_view header, std::string_view value)&& -> HttpClientRequestBuilder&& {
      assert(!req.has_body);
      fmt::format_to(std::back_inserter(req.request), "\r\n{}: {}", header, value);
      return std::move(*this);
    }

    auto body(std::string_view content_type, std::string_view body)&& -> HttpClientRequestBuilder&&{
      assert(!req.has_body);
      fmt::format_to(
        std::back_inserter(req.request), "\r\nContent-Type: {}\r\nContent-Length: {:x}\r\n\r\n{}",
        content_type, body.length(), body
      );
      req.has_body = true;
      return std::move(*this);
    }

    auto dispatch(HttpResponseCallback&& callback) && -> void;
  };

  class HttpClient {
  protected:
    virtual auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void = 0;
  public:
    virtual ~HttpClient() {};
    auto get(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "GET");
    }
    auto post(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "POST");
    }
    auto put(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "PUT");
    }
    auto delete_(std::string url) -> HttpClientRequestBuilder {
      return HttpClientRequestBuilder(*this, url, "DELETE");
    }

    friend struct HttpClientRequestBuilder;
  };

  inline auto HttpClientRequestBuilder::dispatch(HttpResponseCallback&& callback) && -> void {
    if (!req.has_body) req.request.append("\r\n\r\n");
    client.fetch(std::move(req), std::move(callback));
  }
}
