#include "thumbnail_cache.h++"
#include <xxhash.h>

using std::bind, std::make_shared, std::nullopt, std::optional, std::pair,
    std::runtime_error, std::placeholders::_1, std::shared_ptr,
    std::string, std::string_view, std::visit, std::weak_ptr;

namespace Ludwig {
  ThumbnailCache::ThumbnailCache(
    shared_ptr<HttpClient> http_client,
    size_t cache_size,
    uint16_t thumbnail_width,
    uint16_t thumbnail_height
  ) : cache(bind(&ThumbnailCache::fetch_thumbnail, this, _1), cache_size), http_client(http_client),
      w(thumbnail_width), h(thumbnail_height ? thumbnail_height : thumbnail_width) {}

  auto ThumbnailCache::fetch_thumbnail(string url) -> Entry {
    auto entry_cell = make_shared<Entry>(Promise{});
    http_client->get(url)
      .header("Accept", "image/*")
      .dispatch([this, url, maybe_entry_cell = weak_ptr(entry_cell)](auto rsp){
        const auto mimetype = rsp->header("content-type");
        Image img = make_shared<optional<pair<string, uint64_t>>>();
        if (rsp->error()) {
          spdlog::warn("Failed to fetch image at {}: {}", url, *rsp->error());
        } else {
          try {
            const auto thumbnail = generate_thumbnail(
              mimetype.empty() ? nullopt : optional(mimetype), rsp->body(), w, h
            );
            const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
            *img = { std::move(thumbnail), hash };
          } catch (const runtime_error& e) {
            spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
          }
        }
        if (auto entry_cell = maybe_entry_cell.lock()) {
          // If this callback was synchronous, the shared_ptr cell will still
          // exist; overwrite it so that the ::thumbnail callback will work.
          spdlog::debug("Got synchronous response for image {}", url);
          *entry_cell = img;
        } else {
          Promise callbacks;
          {
            auto handle = cache[url];
            visit(overload{
              [&](Promise p) { callbacks = std::move(p); },
              [&](Image&) {
                spdlog::warn("Overwrote cached thumbnail for {}, this is probably a race condition and shouldn't happen!", url);
              }
            }, handle.value());
            handle.value() = img;
          }
          spdlog::debug("Got thumbnail for {}, dispatching {:d} callbacks", url, callbacks.size());
          for (auto& cb : callbacks) (*cb)(img);
        }
      });
    return *entry_cell;
  }

  auto ThumbnailCache::thumbnail(string url, Callback callback) -> void {
    auto handle = cache[url];
    visit(overload{
      [&](Promise& p) {
        spdlog::debug("Adding callback to in-flight thumbnail request for {}", url);
        p.push_back(std::make_shared<Callback>(std::move(callback)));
      },
      [&](Image i) { callback(i); }
    }, handle.value());
  }
}
