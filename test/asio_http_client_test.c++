#include "util.h++"
#include "services/asio_http_client.h++"
#include <catch2/catch_test_macros.hpp>
#include <static_block.hpp>

using namespace std::literals;

//static_block {
//  spdlog::set_level(spdlog::level::debug);
//}

using namespace Ludwig;

TEST_CASE("send request to example.com", "[http_client]") {
  auto io = std::make_shared<asio::io_context>();
  auto ssl = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  ssl->set_default_verify_paths();
  AsioHttpClient client(io, ssl);
  bool success = false;
  std::ostringstream response_body;
  client.get("https://example.com")
    .header("Accept", "text/html")
    .dispatch([&](std::unique_ptr<HttpClientResponse> response) {
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
