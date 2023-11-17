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
  optional<std::thread> server_thread;
  optional<us_listen_socket_t*> server_socket;
  int port;

  WebFixture()
    : client_io(make_shared<asio::io_context>()),
      ssl(make_shared<asio::ssl::context>(asio::ssl::context::sslv23)),
      http_client(make_shared<AsioHttpClient>(client_io, ssl)),
      //router(app),
      client_thread([&] { client_io->run(); }),
      port(-1) {}

  void start_app() {
    std::mutex m;
    std::condition_variable cv;
    server_thread = std::thread([&] {
      app.listen(0, [&](auto *listen_socket) {
        REQUIRE(listen_socket);
        server_socket = listen_socket;
        port = us_socket_local_port(false, (us_socket_t*)listen_socket);
        spdlog::debug("Got port: {}", port);
        cv.notify_one();
      }).run();
    });
    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, 1s);
    REQUIRE(port > 0);
  }

  ~WebFixture() {
    client_io->stop();
    if (server_socket) us_listen_socket_close(false, *server_socket);
    if (server_thread) server_thread->detach();
    client_thread.detach();
  }
};

TEST_CASE_METHOD(WebFixture, "simple GET request", "[web]") {
  app.get("/hello", [](auto* rsp, auto* req) {
    spdlog::debug("GOT REQUEST!");
    rsp->end(fmt::format("Hello, test!"));
  });
  start_app();
  auto url = fmt::format("http://localhost:{:d}/hello", port);
  spdlog::debug(url);
  std::future<unique_ptr<const HttpClientResponse>> f;
  REQUIRE_NOTHROW(f = asio::co_spawn(*client_io, http_client->get(url).dispatch(), asio::use_future));
  spdlog::debug("A");
  auto rsp = f.get();
  spdlog::debug("B");
  CHECK(rsp->status() == 200);
  CHECK(rsp->error().value_or("") == "");
  CHECK(rsp->body() == "Hello, test!");
}
