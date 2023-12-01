#include "test_common.h++"
#include "services/asio_http_client.h++"

TEST_CASE("send request to example.com", "[http_client]") {
  auto io = std::make_shared<asio::io_context>();
  AsioHttpClient client(io);
  bool success = false;
  std::ostringstream response_body;
  client.get("https://example.com")
    .header("Accept", "text/html")
    .dispatch([&](auto&& response) {
      REQUIRE(response->error().value_or("") == "");
      REQUIRE(response->status() == 200);
      REQUIRE(response->header("content-type").starts_with("text/html"));
      success = true;
      response_body << response->body();
      io->stop();
    });
  io->run();
  REQUIRE(success);
  REQUIRE(response_body.str().contains("<title>Example Domain</title>"));
}
