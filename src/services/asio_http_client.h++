#pragma once
#include "http_client.h++"
#include "util/asio_common.h++"
#include "util/rate_limiter.h++"

namespace Ludwig {
  class AsioHttpClientResponse;

  class AsioHttpClient : public HttpClient {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::ssl::context ssl;
    KeyedRateLimiter rate_limiter;

    auto https_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
    auto http_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
  protected:
    auto fetch(HttpClientRequest&& req) -> Async<std::unique_ptr<const HttpClientResponse>>;
    auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void;
  public:
    AsioHttpClient(
      std::shared_ptr<asio::io_context> io,
      uint32_t requests_per_host_per_5min = 1000
    );
  };
}
