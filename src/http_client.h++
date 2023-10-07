#pragma once
#include "common.h++"
#include <regex>

namespace Ludwig {
  class HttpClient;

  class HttpClientResponse {
  public:
    virtual inline ~HttpClientResponse() {};
    virtual auto status() -> uint16_t = 0;
    virtual auto error() -> std::optional<std::string_view> = 0;
    virtual auto header(std::string_view name) -> std::string_view = 0;
    virtual auto body() -> std::string_view = 0;
  };

  class ErrorHttpClientResponse : public HttpClientResponse {
  private:
    std::string msg;
  public:
    ErrorHttpClientResponse(std::string msg) : msg(msg) {}
    auto status() -> uint16_t { return 0; };
    auto error() -> std::optional<std::string_view> { return { msg }; };
    auto header(std::string_view) -> std::string_view { return { nullptr, 0 }; };
    auto body() -> std::string_view { return { nullptr, 0 }; };
  };

  using HttpResponseCallback = std::function<void (std::unique_ptr<HttpClientResponse>)>;

  class HttpClientRequest {
  public:
    HttpClient& client;
    std::string_view url, method;
    std::string host;
    bool https = false, has_body = false;
    std::string request;

    HttpClientRequest(HttpClient& client, std::string_view url, std::string_view method)
      : client(client), url(url), method(method) {
      static const std::regex url_regex(
        R"((https?)://([\w\-.]+)(?::\d+)?(/[^#]*)?(?:#.*)?)",
        std::regex_constants::ECMAScript
      );
      std::match_results<std::string_view::const_iterator> match;
      if (!std::regex_match(url.begin(), url.end(), match, url_regex)) {
        throw std::runtime_error(fmt::format("Invalid HTTP URL: {}", url));
      }
      https = match.str(1) == "https";
      host = match.str(2);
      fmt::format_to(
        std::back_inserter(request), "{} {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nUser-Agent: ludwig",
        method, match.str(3).empty() ? "/" : match.str(3), host
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

    inline auto with_new_url(std::string_view new_url) -> HttpClientRequest {
      auto new_req = HttpClientRequest(client, new_url, method);
      new_req.has_body = has_body || request.ends_with("\r\n\r\n");
      new_req.request.append(request.substr(request.find("User-Agent: ludwig\r\n") + 18));
      return new_req;
    }

    auto dispatch(HttpResponseCallback callback) && -> void;
  };

  class HttpClient {
  protected:
    virtual auto fetch(HttpClientRequest&& req, HttpResponseCallback callback) -> void = 0;
  public:
    inline auto get(std::string_view url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "GET");
    }
    inline auto post(std::string_view url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "POST");
    }
    inline auto put(std::string_view url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "PUT");
    }
    inline auto delete_(std::string_view url) -> HttpClientRequest {
      return HttpClientRequest(*this, url, "DELETE");
    }

    friend class HttpClientRequest;
  };

  inline auto HttpClientRequest::dispatch(HttpResponseCallback callback) && -> void {
    if (!has_body) request.append("\r\n\r\n");
    client.fetch(std::move(*this), callback);
  }
}
