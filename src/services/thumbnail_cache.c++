#include "thumbnail_cache.h++"
#include <vips/conversion.h>

using std::make_shared, std::nullopt, std::optional,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::visit, std::weak_ptr, vips::VImage;

namespace Ludwig {

ThumbnailCache::ThumbnailCache(
  shared_ptr<HttpClient> http_client,
  size_t cache_size,
  uint16_t thumbnail_width,
  uint16_t thumbnail_height,
  Dispatcher dispatcher
) : cache([](const string&)->Entry{return Promise{};}, cache_size), http_client(http_client), dispatcher(dispatcher),
    w(thumbnail_width), h(thumbnail_height ? thumbnail_height : thumbnail_width) {}

auto ThumbnailCache::fetch_thumbnail(string url, Entry& entry_cell) -> Entry& {
  auto sync_flag = make_shared<std::monostate>();
  http_client->get(url)
    .header("Accept", "image/*")
    .dispatch([this, url, &entry_cell, maybe_sync_flag = weak_ptr(sync_flag)](auto&& rsp){
      dispatcher([this, rsp = std::move(rsp), url, &entry_cell, maybe_sync_flag] mutable {
        const auto mimetype = rsp->header("content-type");
        ImageRef img = nullptr;
        if (rsp->error()) {
          spdlog::warn("Failed to fetch image at {}: {}", url, *rsp->error());
        } else {
          try {
            img = generate_thumbnail(
              mimetype.empty() ? nullopt : optional(mimetype), rsp->body(), w, h
            );
          } catch (const runtime_error& e) {
            spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
          }
        }
        if (maybe_sync_flag.lock()) {
          // If this callback was synchronous, the shared_ptr cell will still
          // exist; overwrite it so that the ::thumbnail callback will work.
          spdlog::debug("Got synchronous response for image {}", url);
          entry_cell.emplace<ImageRef>(img);
        } else {
          Promise callbacks;
          {
            auto handle = cache[url];
            auto& value = handle.value();
            visit(overload{
              [&](Promise p) { callbacks = std::move(p); },
              [&](ImageRef&) {
                spdlog::warn("Overwrote cached thumbnail for {}, this is probably a race condition and shouldn't happen!", url);
              }
            }, value);
            value.emplace<ImageRef>(img);
          }
          spdlog::debug("Got thumbnail for {}, dispatching {:d} callbacks", url, callbacks.size());
          for (auto& cb : callbacks) (*cb)(img);
        }
      });
    });
  return entry_cell;
}

auto ThumbnailCache::thumbnail(string url, Callback&& callback) -> shared_ptr<Cancelable> {
  auto handle = cache[url];
  auto& value = handle.value();
  return visit(overload{
    [&](Promise& p) {
      if (p.empty()) {
        return visit(overload{
          [&callback](Promise& p) {
            auto sp = make_shared<CancelableCallback>(std::move(callback));
            p.push_back(sp);
            return sp;
          },
          [&callback](ImageRef i) -> shared_ptr<CancelableCallback> {
            callback(i);
            return nullptr;
          }
        }, fetch_thumbnail(url, value));
      } else {
        spdlog::debug("Adding callback to in-flight thumbnail request for {}", url);
        auto sp = make_shared<CancelableCallback>(std::move(callback));
        p.push_back(sp);
        return sp;
      }
    },
    [&callback](ImageRef i) -> shared_ptr<CancelableCallback> {
      callback(i);
      return nullptr;
    }
  }, value);
}

auto ThumbnailCache::set_thumbnail(string url, string_view mimetype, string_view data) -> bool {
  auto handle = cache[url];
  try {
    handle.value().emplace<ImageRef>(generate_thumbnail(
      mimetype.empty() ? nullopt : optional(mimetype), data, w, h
    ));
    return true;
  } catch (const runtime_error& e) {
    spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
    return false;
  }
}


auto ThumbnailCache::generate_thumbnail(
  optional<string_view> mimetype,
  string_view data,
  uint16_t width,
  uint16_t height
) -> ImageRef {
  try {
    if (height <= 0) height = width;
    return ImageRef(
      VImage::thumbnail_buffer((void*)data.data(), data.length(), width,
        VImage::option()
        ->set("height", height)
        ->set("crop", VIPS_INTERESTING_CENTRE)
      ).webpsave_buffer(
        VImage::option()->set("target_size", width * (height ? height : width))
      )
    );
  } catch (const std::exception& e) {
    spdlog::warn("Thumbnail of image with type {} failed: {}", mimetype.value_or("(unknown)"), e.what());
    return {};
  }
}

}