#include "thumbnail_cache.h++"
#include "util/common.h++"
#include "wuffs-v0.4.c"
#include <webp/encode.h>
#include <webp/decode.h>

using std::make_shared, std::make_unique, std::nullopt, std::optional,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::unique_ptr, std::variant, std::visit, std::weak_ptr;

namespace Ludwig {

// Error messages for WebP encoding
static const string ENC_ERROR_MESSAGES[VP8_ENC_ERROR_LAST] = {
  "OK",
  "OUT_OF_MEMORY",
  "BITSTREAM_OUT_OF_MEMORY",
  "NULL_PARAMETER",
  "INVALID_CONFIGURATION",
  "BAD_DIMENSION",
  "PARTITION0_OVERFLOW",
  "PARTITION_OVERFLOW",
  "BAD_WRITE",
  "FILE_TOO_BIG",
  "USER_ABORT"
};

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
        ThumbnailRef img = nullptr;
        if (rsp->error()) {
          spdlog::warn("Failed to fetch image at {}: {}", url, *rsp->error());
        } else {
          img = generate_thumbnail(
            mimetype.empty() ? nullopt : optional(mimetype), rsp->body(), w, h
          );
        }
        if (maybe_sync_flag.lock()) {
          // If this callback was synchronous, the shared_ptr cell will still
          // exist; overwrite it so that the ::thumbnail callback will work.
          spdlog::debug("Got synchronous response for image {}", url);
          entry_cell.emplace<ThumbnailRef>(img);
          return;
        }
        Promise completables;
        {
          auto handle = cache[url];
          auto& value = handle.value();
          visit(overload{
            [&](Promise p) { std::swap(p, completables); },
            [&](ThumbnailRef) {
              spdlog::warn("Overwrote cached thumbnail for {}, this is probably a race condition and shouldn't happen!", url);
            }
          }, value);
          value.emplace<ThumbnailRef>(img);
        }
        spdlog::debug("Got thumbnail for {}, dispatching {:d} callbacks", url, completables.size());
        for (auto& c : completables) c->complete(img);
      });
    });
  return entry_cell;
}

auto ThumbnailCache::thumbnail(string url) -> shared_ptr<CompletableOnce<ThumbnailRef>> {
  auto handle = cache[url];
  auto& value = handle.value();
  auto c = make_shared<CompletableOnce<ThumbnailRef>>();
  visit(overload{
    [&](Promise& p) {
      if (p.empty()) {
        visit(overload{
          [&](Promise& p) { p.push_back(c); },
          [&](ThumbnailRef i) { c->complete(i); }
        }, fetch_thumbnail(url, value));
      } else {
        spdlog::debug("Adding callback to in-flight thumbnail request for {}", url);
        p.push_back(c);
      }
    },
    [&](ThumbnailRef i) { c->complete(i); }
  }, value);
  return c;
}

auto ThumbnailCache::set_thumbnail(string url, string_view mimetype, string_view data) -> bool {
  auto handle = cache[url];
  try {
    handle.value().emplace<ThumbnailRef>(generate_thumbnail(
      mimetype.empty() ? nullopt : optional(mimetype), data, w, h
    ));
    return true;
  } catch (const runtime_error& e) {
    spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
    return false;
  }
}

static auto webp_to_thumbnail(WebPPicture& picture, uint16_t width, uint16_t height) -> Thumbnail {
  double w_ratio = static_cast<double>(width) / picture.width;
  double h_ratio = static_cast<double>(height) / picture.height;

  if (w_ratio > h_ratio) {
    uint32_t h = w_ratio < 1 ? std::ceil(w_ratio * picture.height) : height;
    if (w_ratio < 1 && !WebPPictureRescale(&picture, width, h)) {
      throw runtime_error("failed to scale image");
    }
    assert(h >= height);
    uint32_t offset = (h - height) / 2;
    if (offset && !WebPPictureCrop(&picture, 0, offset, width, height)) {
      throw runtime_error("failed to crop image");
    }
  } else {
    uint32_t w = h_ratio < 1 ? std::ceil(h_ratio * picture.width) : width;
    if (h_ratio < 1 && !WebPPictureRescale(&picture, w, height)) {
      throw runtime_error("failed to scale image");
    }
    assert(w >= width);
    uint32_t offset = (w - width) / 2;
    if (offset && !WebPPictureCrop(&picture, offset, 0, width, height)) {
      throw runtime_error("failed to crop image");
    }
  }

  WebPMemoryWriter writer;
  WebPMemoryWriterInit(&writer);
  picture.writer = WebPMemoryWrite;
  picture.custom_ptr = &writer;
  WebPConfig config;
  if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, 70) || !WebPEncode(&config, &picture)) {
    throw runtime_error("failed to encode image as WebP");
  }
  return Thumbnail(writer.mem, writer.size);
}

class WuffsDecoder : public wuffs_aux::DecodeImageCallbacks {
private:
  unique_ptr<uint32_t[]> bgra_data = nullptr;
  uint32_t true_width = 0, true_height = 0;

  wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config& image_config) noexcept override {
    return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL);
  }

  AllocPixbufResult AllocPixbuf(const wuffs_base__image_config& image_config, bool allow_uninitialized_memory) noexcept override {
    uint32_t w = image_config.pixcfg.width();
    uint32_t h = image_config.pixcfg.height();
    if ((w > WEBP_MAX_DIMENSION) || (h > WEBP_MAX_DIMENSION)) {
      return AllocPixbufResult("Thumbnailer: image is too large");
    }
    true_width = w;
    true_height = h;
    bgra_data = make_unique<uint32_t[]>(w * h);

    wuffs_base__pixel_buffer pixbuf;
    wuffs_base__status status = pixbuf.set_interleaved(
        &image_config.pixcfg,
        wuffs_base__make_table_u8((uint8_t*)bgra_data.get(), w * 4, h, w * 4),
        wuffs_base__empty_slice_u8());

    if (!status.is_ok()) {
      return AllocPixbufResult(status.message());
    }

    return AllocPixbufResult(wuffs_aux::MemOwner(nullptr, &free), pixbuf);
  }

public:
  auto EncodeAsWebP(uint16_t width, uint16_t height) -> Thumbnail {
    assert(bgra_data && true_width > 0 && true_height > 0);
    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
      throw runtime_error("failed to initialize WebP");
    }
    picture.width = true_width;
    picture.height = true_height;
    if (!WebPPictureImportBGRA(&picture, (const uint8_t*)bgra_data.get(), true_width * 4)) {
      throw runtime_error("failed to load image into WebP context");
    }
    Defer free_picture([&](){ WebPPictureFree(&picture); });

    return webp_to_thumbnail(picture, width, height);
  }
};

class WuffsBufferInput : public wuffs_aux::sync_io::Input {
private:
  std::string_view data;
  size_t i = 0;

  std::string CopyIn(wuffs_aux::IOBuffer* dst) override {
    if (!dst) {
      return "Thumbnailer: NULL IOBuffer";
    } else if (dst->meta.closed) {
      return "Thumbnailer: end of file";
    }
    dst->compact();
    if (dst->writer_length() == 0) {
      return "Thumbnailer: full IOBuffer";
    }
    size_t len = std::min(dst->writer_length(), data.size() - i);
    std::memcpy(dst->writer_pointer(), data.data() + i, len);
    dst->meta.wi += len;
    i += len;
    return std::string();
  }
public:
  WuffsBufferInput(std::string_view data) : data(data) {}
};

auto ThumbnailCache::generate_thumbnail(
  optional<string_view> mimetype,
  string_view data,
  uint16_t width,
  uint16_t height
) noexcept -> ThumbnailRef {
  WuffsBufferInput input(data);
  WuffsDecoder decoder;
  if (height <= 0) height = width;
  auto result = wuffs_aux::DecodeImage(decoder, input);
  try {
    if (result.error_message == "webp: unsupported WebP file") {
      WebPPicture picture;
      if (!WebPPictureInit(&picture)) {
        throw runtime_error("failed to initialize WebP");
      }
      uint8_t* bgra_data = WebPDecodeBGRA((const uint8_t*)data.data(), data.size(), &picture.width, &picture.height);
      Defer free_bgra_data([&](){ free(bgra_data); });

      if (!WebPPictureImportBGRA(&picture, (const uint8_t*)bgra_data, picture.width * 4)) {
        throw runtime_error("failed to load image into WebP context");
      }
      Defer free_picture([&](){ WebPPictureFree(&picture); });

      return make_shared<Thumbnail>(webp_to_thumbnail(picture, width, height));
    } else if (!result.error_message.empty()) {
      throw runtime_error(result.error_message);
    }
    return make_shared<Thumbnail>(decoder.EncodeAsWebP(width, height));
  } catch (const std::exception& e) {
    spdlog::warn("Thumbnail of image with type {} failed: {}", mimetype.value_or("(unknown)"), e.what());
    return nullptr;
  }
}

Thumbnail::Thumbnail(uint8_t* data, size_t length) : data(data), length(length), hash(xxhash64(std::string_view((const char*)data, length))) {}

}