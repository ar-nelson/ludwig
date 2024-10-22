#pragma once
#include "http_client.h++"
#include "util/asio_common.h++"
#include "util/rate_limiter.h++"

namespace Ludwig {

  enum class UnsafeHttps : bool {
    SAFE = false,
    UNSAFE = true
  };
  
  enum class UnsafeLocalRequests : bool {
    SAFE = false,
    UNSAFE = true
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
    Async<std::unique_ptr<const HttpClientResponse>> fetch(HttpClientRequest&& req) override;
    void fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) override;
  public:
    AsioHttpClient(
      std::shared_ptr<asio::io_context> io,
      uint32_t requests_per_host_per_5min = 1000,
      UnsafeHttps unsafe_https = UnsafeHttps::SAFE,
      UnsafeLocalRequests unsafe_local_requests = UnsafeLocalRequests::SAFE
    );
  };

  auto is_safe_address(const asio::ip::address& addr) -> bool;
}
