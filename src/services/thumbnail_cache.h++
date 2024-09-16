#pragma once
#include "services/http_client.h++"
#include "util/thumbnailer.h++"
#include <concurrent_lru_cache.h>
#include <variant>

namespace Ludwig {
  class ThumbnailCache {
  public:
    using Callback = uWS::MoveOnlyFunction<void (ImageRef)>;
    using Dispatcher = std::function<void (uWS::MoveOnlyFunction<void()>)>;
  private:
    using Promise = std::list<std::shared_ptr<Callback>>;
    using Entry = std::variant<Promise, ImageRef>;
    auto fetch_thumbnail(std::string url, Entry& entry_cell) -> Entry&;
    tbb::concurrent_lru_cache<std::string, Entry, Entry(*)(const std::string&)> cache;
    std::shared_ptr<HttpClient> http_client;
    Dispatcher dispatcher;
    uint16_t w, h;
  public:
    ThumbnailCache(
      std::shared_ptr<HttpClient> http_client,
      size_t cache_size,
      uint16_t thumbnail_width,
      uint16_t thumbnail_height,
      Dispatcher dispatcher = [](auto f) { f(); }
    );
    ThumbnailCache(
      std::shared_ptr<HttpClient> http_client,
      size_t cache_size,
      uint16_t thumbnail_size,
      Dispatcher dispatcher = [](auto f) { f(); }
    ) : ThumbnailCache(http_client, cache_size, thumbnail_size, thumbnail_size, dispatcher) {}
    auto thumbnail(std::string url, Callback&& callback) -> void;
    auto set_thumbnail(std::string url, std::string_view mimetype, std::string_view data) -> bool;
  };
}
