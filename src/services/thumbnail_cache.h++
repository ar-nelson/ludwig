#pragma once
#include "services/http_client.h++"
#include "util/thumbnailer.h++"
#include <concurrent_lru_cache.h>
#include <asio.hpp>
#include <variant>

namespace Ludwig {
  class ThumbnailCache {
  public:
    using Image = std::shared_ptr<std::pair<std::string, uint64_t>>;
  private:
    using Entry = std::optional<AsyncCell<std::variant<Image, std::exception_ptr>>>;
    auto fetch_thumbnail(std::string url) -> asio::awaitable<Image>;
    tbb::concurrent_lru_cache<std::string, Entry, std::function<Entry (std::string)>> cache;
    std::shared_ptr<HttpClient> http_client;
    uint16_t w, h;
  public:
    ThumbnailCache(
      std::shared_ptr<HttpClient> http_client,
      size_t cache_size,
      uint16_t thumbnail_width,
      uint16_t thumbnail_height = 0
    );
    auto thumbnail(std::string url) -> asio::awaitable<Image>;
    auto set_thumbnail(std::string url, std::string_view mimetype, std::string_view data) -> bool;
  };
}
