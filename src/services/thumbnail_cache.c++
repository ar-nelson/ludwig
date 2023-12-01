#include "thumbnail_cache.h++"
#include "util/web.h++"
#include <xxhash.h>

using std::make_shared, std::nullopt, std::optional, std::pair,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::variant;

namespace Ludwig {
  ThumbnailCache::ThumbnailCache(
    shared_ptr<asio::io_context> io,
    shared_ptr<HttpClient> http_client,
    size_t cache_size,
    uint16_t thumbnail_width,
    uint16_t thumbnail_height
  ) : cache([](string)->Entry{return nullptr;}, cache_size), io(io), http_client(http_client),
      w(thumbnail_width), h(thumbnail_height ? thumbnail_height : thumbnail_width) {}

  auto ThumbnailCache::fetch_thumbnail(string url) -> Async<Image> {
    auto rsp = co_await http_client->get(url)
      .header("Accept", "image/*")
      .throw_on_error_status()
      .dispatch();
    const auto mimetype = rsp->header("content-type");
    Image img = make_shared<pair<string, uint64_t>>();
    const auto thumbnail = generate_thumbnail(
      mimetype.empty() ? nullopt : optional(mimetype), rsp->body(), w, h
    );
    const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
    co_return make_shared<pair<string, uint64_t>>(std::move(thumbnail), hash);
  }

  auto ThumbnailCache::thumbnail(string url) -> Async<Image> {
    shared_ptr<CacheChan<variant<Image, ApiError>>> chan;
    {
      auto handle = cache[url];
      if (auto existing = handle.value()) {
        auto v = co_await existing->get();
        if (auto* i = std::get_if<Image>(&v)) co_return *i;
        throw std::get<ApiError>(v);
      }
      handle.value() = chan = make_shared<CacheChan<variant<Image, ApiError>>>(*io);
    }
    optional<ApiError> ae;
    bool transient = false;
    try {
      auto img = co_await fetch_thumbnail(url);
      chan->set(img);
      co_return img;
    } catch (const ApiError& e) {
      ae = e;
    } catch (const HttpClientError& e) {
      spdlog::warn("Failed to fetch remote image {}: {}", url, e.what());
      ae = ApiError("Failed to fetch remote image", 404);
      transient = e.transient;
    } catch (const runtime_error& e) {
      spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
      ae = ApiError("Failed to generate thumbnail", 500);
    }
    if (!transient) {
      // Have we been canceled?
      co_await asio::this_coro::throw_if_cancelled(false);
      const auto cs = co_await asio::this_coro::cancellation_state;
      if (cs.cancelled() == asio::cancellation_type::none) {
        chan->set(*ae);
        goto done;
      }
    }
    {
      auto handle = cache[url];
      handle.value() = nullptr;
    }
  done:
    throw ae;
  }

  auto ThumbnailCache::set_thumbnail(string url, string_view mimetype, string_view data) -> bool {
    auto handle = cache[url];
    try {
      if (handle.value()) throw runtime_error("Thumbnail already exists");
      const auto thumbnail = generate_thumbnail(
        mimetype.empty() ? nullopt : optional(mimetype), data, w, h
      );
      const auto hash = XXH3_64bits(thumbnail.data(), thumbnail.length());
      auto chan = make_shared<CacheChan<variant<Image, ApiError>>>(*io);
      handle.value() = chan;
      chan->set(make_shared<pair<string, uint64_t>>(std::move(thumbnail), hash));
      return true;
    } catch (const runtime_error& e) {
      spdlog::warn("Failed to generate thumbnail for {}: {}", url, e.what());
      return false;
    }
  }
}
