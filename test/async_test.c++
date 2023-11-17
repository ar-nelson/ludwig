#include "test_common.h++"

TEST_CASE("wait on AsyncCell once", "[async]") {
  asio::io_context io;
  AsyncCell<int> cell;
  auto f = asio::co_spawn(io, [cell] mutable -> asio::awaitable<int> {
    int x = co_await cell.async_get(asio::use_awaitable);
    co_return x;
  }, asio::use_future);
  asio::post(io, [cell] mutable { cell.set(42); });
  io.run();
  REQUIRE(f.get() == 42);
}
