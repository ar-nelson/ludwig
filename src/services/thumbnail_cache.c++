#include "thumbnail_cache.h++"
#include <xxhash.h>

using std::make_shared, std::nullopt, std::optional, std::pair,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::visit, std::weak_ptr;

namespace Ludwig {
  ThumbnailCache::ThumbnailCache(
    shared_ptr<HttpClient> http_client,
    size_t cache_size,
    uint16_t thumbnail_width,
    uint16_t thumbnail_height
  ) : cache([](const string&)->Entry{return Promise{};}, cache_size), http_client(http_client),
      w(thumbnail_width), h(thumbnail_height ? thumbnail_height : thumbnail_width) {}

  auto ThumbnailCache::fetch_thumbnail(string url, Entry& entry_cell) -> Entry& {
    auto sync_flag = make_shared<std::monostate>();
    http_client->get(url)
      .header("Accept", "image/*")
      .dispatch([this, url, &entry_cell, maybe_sync_flag = weak_ptr(sync_flag)](auto&& rsp){
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
        if (maybe_sync_flag.lock()) {
          // If this callback was synchronous, the shared_ptr cell will still
          // exist; overwrite it so that the ::thumbnail callback will work.
          spdlog::debug("Got synchronous response for image {}", url);
          entry_cell.emplace<Image>(img);
        } else {
          Promise callbacks;
          {
            auto handle = cache[url];
            auto& value = handle.value();
            visit(overload{
              [&](Promise p) { callbacks = std::move(p); },
              [&](Image&) {
                spdlog::warn("Overwrote cached thumbnail for {}, this is probably a race condition and shouldn't happen!", url);
              }
            }, value);
            value.emplace<Image>(img);
          }
          spdlog::debug("Got thumbnail for {}, dispatching {:d} callbacks", url, callbacks.size());
          for (auto& cb : callbacks) (*cb)(img);
        }
      });
    return entry_cell;
  }

  auto ThumbnailCache::thumbnail(string url, Callback&& callback) -> void {
    auto handle = cache[url];
    auto& value = handle.value();
    visit(overload{
      [&](Promise& p) {
        if (p.empty()) {
          visit(overload{
            [&callback](Promise& p) {
              p.push_back(make_shared<Callback>(std::move(callback)));
            },
            [&callback](Image i) { callback(i); }
          }, fetch_thumbnail(url, value));
        } else {
          spdlog::debug("Adding callback to in-flight thumbnail request for {}", url);
          p.push_back(make_shared<Callback>(std::move(callback)));
        }
      },
      [&callback](Image i) { callback(i); }
    }, value);
  }

  auto ThumbnailCache::set_thumbnail(string url, string_view mimetype, string_view data) -> bool {
    auto handle = cache[url];
    try {
      const auto thumbnail = generate_thumbnail(
        mimetype.empty() ? nullopt : optional(mimetype), data, w, h
      );
      const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
      handle.value().emplace<Image>(make_shared<optional<pair<string, uint64_t>>>(pair(std::move(thumbnail), hash)));
      return true;
    } catch (const runtime_error& e) {
      spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
      return false;
    }
  }
}
