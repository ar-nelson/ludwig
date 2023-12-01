#include "test_common.h++"
#include "services/asio_http_client.h++"

TEST_CASE("send request to example.com", "[http_client]") {
  auto io = std::make_shared<asio::io_context>();
  auto ssl = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  ssl->set_default_verify_paths();
  AsioHttpClient client(io, ssl);
  bool success = false;
  std::ostringstream response_body;
  asio::co_spawn(*io, [&] -> Async<void> {
    auto response = co_await client.get("https://example.com")
      .header("Accept", "text/html")
      .dispatch();
    REQUIRE(response->status() == 200);
    REQUIRE(response->header("content-type").starts_with("text/html"));
    success = true;
    response_body << response->body();
    io->stop();
  }, asio::detached);
  io->run();
  REQUIRE(success);
  REQUIRE(response_body.str().contains("<title>Example Domain</title>"));
}
