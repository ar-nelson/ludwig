#include "test_common.h++"
#include "services/asio_http_client.h++"
#include "util/web.h++"

using namespace Ludwig;

struct WebFixture {
  shared_ptr<asio::io_context> client_io;
  shared_ptr<asio::ssl::context> ssl;
  shared_ptr<AsioHttpClient> http_client;
  uWS::App app;
  //Router<false> router;
  std::thread client_thread;
  int port;

  WebFixture()
    : client_io(make_shared<asio::io_context>()),
      ssl(make_shared<asio::ssl::context>(asio::ssl::context::sslv23)),
      http_client(make_shared<AsioHttpClient>(client_io, ssl)),
      //router(app),
      client_thread([&] { client_io->run(); }),
      port(-1) {}

  void with_app(std::function<Async<void>()> coroutine) {
    std::exception_ptr other_thread_exception;
    app.listen(0, [&](auto *listen_socket) {
      REQUIRE(listen_socket);
      port = us_socket_local_port(false, (us_socket_t*)listen_socket);
      REQUIRE(port > 0);
      spdlog::debug("Got port: {}", port);
      asio::co_spawn(*client_io, coroutine, [&](std::exception_ptr e) {
        other_thread_exception = e;
        app.close();
      });
    }).run();
    if (other_thread_exception) std::rethrow_exception(other_thread_exception);
  }

  ~WebFixture() {
    client_io->stop();
    client_thread.detach();
  }
};

/*
TEST_CASE_METHOD(WebFixture, "simple GET request", "[web]") {
  app.get("/hello", [](auto* rsp, auto* req) {
    spdlog::debug("GOT REQUEST!");
    rsp->end("Hello, test!");
  });
  with_app([&] -> Async<void> {
    auto url = fmt::format("http://localhost:{:d}/hello", port);
    spdlog::debug(url);
    auto rsp = co_await http_client->get(url).dispatch();
    CHECK(rsp->status() == 200);
    CHECK(rsp->body() == "Hello, test!");
  });
}
*/
