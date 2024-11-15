#pragma once
#include "services/http_client.h++"
#include <atomic>
#include <concurrent_lru_cache.h>
#include <variant>
#include <vips/vips8>
#include <xxhash.h>

namespace Ludwig {

class ImageRef {
private:
  VipsArea* area;
  uint64_t _hash;
public: 
  ImageRef() : area(nullptr), _hash(0) {}
  ImageRef(VipsBlob* blob) :
    area(blob ? &blob->area : nullptr),
    _hash(area ? XXH3_64bits(area->data, area->length) : 0) {}
  ~ImageRef() {
    if (area) vips_area_unref(area);
  }
  ImageRef(const ImageRef& from) : area(from.area), _hash(from._hash) {
    if (area) from.area->count++;
  }
  ImageRef(ImageRef&& from) : area(from.area), _hash(from._hash) {
    from.area = nullptr;
    from._hash = 0;
  }
  void operator=(const ImageRef& from) {
    if (from.area) from.area->count++;
    if (area) vips_area_unref(area);
    area = from.area;
    _hash = from._hash;
  }
  void operator=(ImageRef&& from) {
    if (area) vips_area_unref(area);
    area = from.area;
    _hash = from._hash;
    from.area = nullptr;
    from._hash = 0;
  }
  uint64_t hash() const {
    return _hash;
  }
  const void* data() const {
    return area ? area->data : nullptr;
  }
  size_t length() const {
    return area ? area->length : 0;
  }
  operator bool() const {
    return !!area;
  }
  operator std::string_view() const {
    return {(const char*)data(), length() };
  }
};

class ThumbnailCache {
public:
  using Callback = uWS::MoveOnlyFunction<void (ImageRef)>;
  using Dispatcher = std::function<void (uWS::MoveOnlyFunction<void()>)>;
private:
  struct CancelableCallback : public Cancelable {
    Callback callback;
    std::atomic<bool> canceled = false;
    CancelableCallback(Callback&& cb) : callback(std::move(cb)) {}
    void operator()(ImageRef img) {
      if (!canceled.load(std::memory_order_acquire)) callback(img);
    }
    void cancel() noexcept override {
      canceled.store(true, std::memory_order_release);
    }
  };
  using Promise = std::list<std::shared_ptr<CancelableCallback>>;
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
  auto thumbnail(std::string url, Callback&& callback) -> std::shared_ptr<Cancelable>;
  auto set_thumbnail(std::string url, std::string_view mimetype, std::string_view data) -> bool;
  static auto generate_thumbnail(
    std::optional<std::string_view> mimetype,
    std::string_view data,
    uint16_t width,
    uint16_t height = 0
  ) -> ImageRef;
};

}