#pragma once
#include "http_client.h++"
#include "util/asio_common.h++"

namespace Ludwig {
  class AsioHttpClientResponse;

  class AsioHttpClient : public HttpClient {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::ssl::context ssl;

    auto https_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
    auto http_fetch(HttpClientRequest& req) -> Async<AsioHttpClientResponse*>;
  protected:
    auto fetch(HttpClientRequest&& req, HttpResponseCallback&& callback) -> void;
  public:
    AsioHttpClient(std::shared_ptr<asio::io_context> io);
  };
}
