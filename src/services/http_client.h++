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

  class HttpClientRequest {
  public:
    HttpClient& client;
    std::string url, method, host, request;
    bool https = false, has_body = false;

    HttpClientRequest(HttpClient& client, std::string url, std::string method)
      : client(client), url(url), method(method) {
      const auto parsed_url = Url::parse(url);
      if (!parsed_url || !parsed_url->is_http_s()) {
        throw std::runtime_error(fmt::format("Invalid HTTP URL: {}", url));
      }
      https = parsed_url->scheme == "https";
      host = parsed_url->host;
      fmt::format_to(
        std::back_inserter(request), "{} {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nUser-Agent: ludwig",
        method, parsed_url->path.empty() ? "/" : parsed_url->path, host
      );
    }

    inline auto header(std::string_view header, std::string_view value) && -> HttpClientRequest {
      fmt::format_to(std::back_inserter(request), "\r\n{}: {}", header, value);
      return *this;
    }

    inline auto body(std::string_view content_type, std::string_view body) && -> HttpClientRequest {
      fmt::format_to(
        std::back_inserter(request), "\r\nContent-Type: {}\r\nContent-Length: {:x}\r\n\r\n{}",
        content_type, body.length(), body
      );
      has_body = true;
      return *this;
    }

    inline auto with_new_url(std::string new_url) -> HttpClientRequest {
      if (new_url.starts_with("/")) {
        new_url = fmt::format("{}://{}{}", https ? "https" : "http", host, new_url);
      }
      auto new_req = HttpClientRequest(client, new_url, method);
      new_req.has_body = has_body || request.ends_with("\r\n\r\n");
      new_req.request.append(request.substr(request.find("User-Agent: ludwig\r\n") + 18));
      return new_req;
    }

    auto dispatch(HttpResponseCallback&& callback) && -> void;
  };

  class HttpClient {
  protected:
    virtual auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void = 0;
  public:
    virtual inline ~HttpClient() {};
    inline auto get(std::string url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "GET");
    }
    inline auto post(std::string url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "POST");
    }
    inline auto put(std::string url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "PUT");
    }
    inline auto delete_(std::string url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "DELETE");
    }

    friend class HttpClientRequest;
  };

  inline auto HttpClientRequest::dispatch(HttpResponseCallback&& callback) && -> void {
    if (!has_body) request.append("\r\n\r\n");
    client.fetch(std::move(*this), std::move(callback));
  }
}
