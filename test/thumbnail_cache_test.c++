#include "test_common.h++"
#include "services/thumbnail_cache.h++"

using namespace Ludwig;
using namespace std::chrono_literals;

static const string URL = "http://foo.test/img.webp";

struct ThumbnailCacheFixture {
  shared_ptr<asio::io_context> io;
  shared_ptr<MockHttpClient> http_client;
  shared_ptr<ThumbnailCache> cache;
  string image_data;

  ThumbnailCacheFixture()
    : io(make_shared<asio::io_context>()),
      http_client(make_shared<MockHttpClient>()),
      cache(make_shared<ThumbnailCache>(io, http_client, 8, 64)),
      image_data(load_file(test_root() / "images" / "test.webp"))
  {
    http_client->on_get(URL, 200, "image/webp", image_data);
  }
};

TEST_CASE_METHOD(ThumbnailCacheFixture, "fetch thumbnail", "[thumbnail_cache]") {
  auto f = asio::co_spawn(*io, [&] -> Async<size_t> {
    auto img = co_await cache->thumbnail(URL);
    co_return img->first.length();
  }, asio::use_future);
  REQUIRE_NOTHROW(io->run());
  REQUIRE_NOTHROW(f.get());
  CHECK(http_client->total_requests() == 1);
}

TEST_CASE_METHOD(ThumbnailCacheFixture, "multiple waiters on the same thumbnail", "[thumbnail_cache]") {
  http_client->set_delay(500ms);
  {
    asio::executor_work_guard<decltype(io->get_executor())> work(io->get_executor());
    auto th = std::thread([&] { io->run(); });
    auto fn = [&] -> Async<uint64_t> {
      auto img = co_await cache->thumbnail(URL);
      co_return img->second;
    };
    auto f1 = asio::co_spawn(*io, fn, asio::use_future),
      f2 = asio::co_spawn(*io, fn, asio::use_future),
      f3 = asio::co_spawn(*io, fn, asio::use_future);
    uint64_t h1, h2, h3;
    REQUIRE_NOTHROW(h1 = f1.get());
    REQUIRE_NOTHROW(h2 = f2.get());
    REQUIRE_NOTHROW(h3 = f3.get());
    CHECK(h1 == h2);
    CHECK(h1 == h3);
    th.detach();
  }
  CHECK(http_client->total_requests() == 1);
}

TEST_CASE_METHOD(ThumbnailCacheFixture, "set and fetch thumbnail", "[thumbnail_cache]") {
  cache->set_thumbnail(URL, "image/webp", image_data);
  auto f = asio::co_spawn(*io, [&] -> Async<size_t> {
    auto img = co_await cache->thumbnail(URL);
    co_return img->first.length();
  }, asio::use_future);
  REQUIRE_NOTHROW(io->run());
  REQUIRE_NOTHROW(f.get());
  CHECK(http_client->total_requests() == 0);
}
