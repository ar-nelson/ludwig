#pragma once
#include "services/db.h++"
#include "services/http_client.h++"
#include "services/thumbnail_cache.h++"

namespace Ludwig {
  class RemoteMediaController : std::enable_shared_from_this<RemoteMediaController> {
  private:
    std::shared_ptr<DB> db;
    ThumbnailCache small_cache, banner_cache;
  public:
    RemoteMediaController(std::shared_ptr<DB> db, std::shared_ptr<HttpClient> http_client);

    auto user_avatar(std::string_view user_name, ThumbnailCache::Callback cb) -> void;
    auto user_banner(std::string_view user_name, ThumbnailCache::Callback cb) -> void;
    auto board_icon(std::string_view board_name, ThumbnailCache::Callback cb) -> void;
    auto board_banner(std::string_view board_name, ThumbnailCache::Callback cb) -> void;
    auto thread_preview(uint64_t thread_id, ThumbnailCache::Callback cb) -> void;
  };
}
