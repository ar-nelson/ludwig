#pragma once
#include "http_client.h++"
#include "util/asio_common.h++"
#include "util/rate_limiter.h++"

namespace Ludwig {

  enum UnsafeHttps {
    SAFE_HTTPS,
    UNSAFE_HTTPS
  };
  
  enum UnsafeLocalRequests {
    SAFE_LOCAL_REQUESTS,
    UNSAFE_LOCAL_REQUESTS
  };

  class AsioHttpClientResponse;

  class AsioHttpClient : public HttpClient {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::ssl::context ssl;
    KeyedRateLimiter rate_limiter;
    bool safe_https, safe_local_requests;

    auto https_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
    auto http_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
    auto check_for_unsafe_local_requests(const asio::ip::basic_resolver_results<asio::ip::tcp>& endpoint_iterator, const Url& url) -> void;
  protected:
    auto fetch(HttpClientRequest&& req) -> Async<std::unique_ptr<const HttpClientResponse>>;
    auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void;
  public:
    AsioHttpClient(
      std::shared_ptr<asio::io_context> io,
      uint32_t requests_per_host_per_5min = 1000,
      UnsafeHttps unsafe_https = SAFE_HTTPS,
      UnsafeLocalRequests unsafe_local_requests = SAFE_LOCAL_REQUESTS
    );
  };

  auto is_safe_address(const asio::ip::address& addr) -> bool;
}
