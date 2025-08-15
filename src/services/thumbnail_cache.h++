#pragma once
#include "services/http_client.h++"
#include <concurrent_lru_cache.h>
#include <variant>

namespace Ludwig {

class Thumbnail {
public: 
  uint8_t* data;
  size_t length;
  uint64_t hash;
  Thumbnail(uint8_t* data, size_t length);
  ~Thumbnail() { free(data); }
  Thumbnail(Thumbnail&& from) : data(from.data), length(from.length), hash(from.hash) { from.data = nullptr; }
  void operator=(Thumbnail&& from) { data = from.data; length = from.length; hash = from.hash; from.data = nullptr; }
  operator std::string_view() const { return std::string_view((const char*)data, length); }
};

using ThumbnailRef = std::shared_ptr<Thumbnail>;

class ThumbnailCache {
public:
  using Dispatcher = std::function<void (uWS::MoveOnlyFunction<void()>)>;
private:
  using Promise = std::list<std::shared_ptr<CompletableOnce<ThumbnailRef>>>;
  using Entry = std::variant<Promise, ThumbnailRef>;
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
  auto thumbnail(std::string url) -> std::shared_ptr<CompletableOnce<ThumbnailRef>>;
  auto set_thumbnail(std::string url, std::string_view mimetype, std::string_view data) -> bool;
  static auto generate_thumbnail(
    std::optional<std::string_view> mimetype,
    std::string_view data,
    uint16_t width,
    uint16_t height = 0
  ) noexcept -> ThumbnailRef;
};

}