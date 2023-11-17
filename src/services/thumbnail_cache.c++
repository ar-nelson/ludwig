#include "thumbnail_cache.h++"
#include "util/web.h++"
#include <xxhash.h>

using std::make_shared, std::nullopt, std::optional, std::pair,
    std::runtime_error, std::shared_ptr, std::string, std::string_view;

namespace Ludwig {
  ThumbnailCache::ThumbnailCache(
    shared_ptr<HttpClient> http_client,
    size_t cache_size,
    uint16_t thumbnail_width,
    uint16_t thumbnail_height
  ) : cache([&](string) {return nullopt;}, cache_size), http_client(http_client),
      w(thumbnail_width), h(thumbnail_height ? thumbnail_height : thumbnail_width) {}

  auto ThumbnailCache::fetch_thumbnail(string url) -> asio::awaitable<Image> {
    auto rsp = co_await http_client->get(url).header("Accept", "image/*").dispatch();
    const auto mimetype = rsp->header("content-type");
    Image img = make_shared<pair<string, uint64_t>>();
    if (rsp->error()) {
      throw ApiError(fmt::format("Failed to fetch image at {}: {}", url, *rsp->error()), 404);
    }
    try {
      const auto thumbnail = generate_thumbnail(
        mimetype.empty() ? nullopt : optional(mimetype), rsp->body(), w, h
      );
      const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
      co_return make_shared<pair<string, uint64_t>>(std::move(thumbnail), hash);
    } catch (const runtime_error& e) {
      throw ApiError(fmt::format("Failed to generate thumbnail for {}: {}", url, e.what()), 500);
    }
  }

  auto ThumbnailCache::thumbnail(string url) -> asio::awaitable<Image> {
    auto handle = cache[url];
    if (auto& existing = handle.value()) {
      auto v = co_await existing->async_get(asio::use_awaitable);
      if (auto* i = std::get_if<Image>(&v)) co_return *i;
      std::rethrow_exception(std::get<1>(v));
    }
    AsyncCell<std::variant<Image, std::exception_ptr>> cell;
    handle.value() = cell;
    try {
      auto img = co_await fetch_thumbnail(url);
      cell.set(img);
      co_return img;
    } catch (...) {
      auto e = std::current_exception();
      cell.set(e);
      std::rethrow_exception(e);
    }
  }

  auto ThumbnailCache::set_thumbnail(string url, string_view mimetype, string_view data) -> bool {
    auto handle = cache[url];
    try {
      if (handle.value()) throw runtime_error("Thumbnail already exists");
      const auto thumbnail = generate_thumbnail(
        mimetype.empty() ? nullopt : optional(mimetype), data, w, h
      );
      const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
      AsyncCell<std::variant<Image, std::exception_ptr>> cell;
      handle.value() = cell;
      cell.set(make_shared<pair<string, uint64_t>>(std::move(thumbnail), hash));
      return true;
    } catch (const runtime_error& e) {
      spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
      return false;
    }
  }
}
