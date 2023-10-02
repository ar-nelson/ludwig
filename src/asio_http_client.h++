#pragma once
#include "http_client.h++"
#include <asio.hpp>
#include <asio/ssl.hpp>

namespace Ludwig {
  class AsioHttpClient : public HttpClient {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::executor_work_guard<decltype(io->get_executor())> work;
    std::shared_ptr<asio::ssl::context> ssl;
    asio::ip::tcp::resolver resolver;

    auto handle_resolve(const asio::error_code& ec, asio::ip::tcp::resolver::iterator endpoint_iterator) -> void;
  protected:
    auto fetch(HttpClientRequest&& req, HttpResponseCallback callback) -> void;
  public:
    AsioHttpClient(std::shared_ptr<asio::io_context> io, std::shared_ptr<asio::ssl::context> ssl);
  };
}
