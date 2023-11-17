#pragma once
#include "http_client.h++"
#include <asio.hpp>
#include <asio/ssl.hpp>

namespace Ludwig {
  class AsioHttpClientResponse;

  class AsioHttpClient : public HttpClient {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::executor_work_guard<decltype(io->get_executor())> work;
    std::shared_ptr<asio::ssl::context> ssl;

    auto https_fetch(std::unique_ptr<HttpClientRequest>& req) -> asio::awaitable<std::unique_ptr<AsioHttpClientResponse>>;
    auto http_fetch(std::unique_ptr<HttpClientRequest>& req) -> asio::awaitable<std::unique_ptr<AsioHttpClientResponse>>;
  protected:
    auto fetch(std::unique_ptr<HttpClientRequest> req) -> asio::awaitable<std::unique_ptr<const HttpClientResponse>>;
  public:
    AsioHttpClient(std::shared_ptr<asio::io_context> io, std::shared_ptr<asio::ssl::context> ssl);
  };
}
