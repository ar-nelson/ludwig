#include "remote_media.h++"

using std::make_shared, std::optional, std::pair, std::shared_ptr, std::string,
    std::string_view;

namespace Ludwig {
  RemoteMediaController::RemoteMediaController(shared_ptr<DB> db, shared_ptr<HttpClient> http_client)
    : db(db), small_cache(http_client, 16384, 256), banner_cache(http_client, 256, 960, 160) {}

  auto RemoteMediaController::user_avatar(string_view user_name, ThumbnailCache::Callback cb) -> void {
    auto txn = db->open_read_txn();
    const auto user =
      txn.get_user_id_by_name(user_name).and_then([&](auto id){return txn.get_user(id);});
    if (user && user->get().avatar_url()) {
      small_cache.thumbnail(user->get().avatar_url()->str(), cb);
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::user_banner(string_view user_name, ThumbnailCache::Callback cb) -> void {
    auto txn = db->open_read_txn();
    const auto user =
      txn.get_user_id_by_name(user_name).and_then([&](auto id){return txn.get_user(id);});
    if (user && user->get().banner_url()) {
      banner_cache.thumbnail(user->get().banner_url()->str(), cb);
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::board_icon(string_view board_name, ThumbnailCache::Callback cb) -> void {
    auto txn = db->open_read_txn();
    const auto board =
      txn.get_board_id_by_name(board_name).and_then([&](auto id){return txn.get_board(id);});
    if (board && board->get().icon_url()) {
      small_cache.thumbnail(board->get().icon_url()->str(), cb);
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::board_banner(string_view board_name, ThumbnailCache::Callback cb) -> void {
    auto txn = db->open_read_txn();
    const auto board =
      txn.get_board_id_by_name(board_name).and_then([&](auto id){return txn.get_board(id);});
    if (board && board->get().banner_url()) {
      banner_cache.thumbnail(board->get().banner_url()->str(), cb);
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }

  auto RemoteMediaController::thread_preview(uint64_t thread_id, ThumbnailCache::Callback cb) -> void {
    auto txn = db->open_read_txn();
    const auto thread = txn.get_thread(thread_id);
    if (thread && thread->get().card_image_url()) {
      small_cache.thumbnail(thread->get().card_image_url()->str(), cb);
    } else {
      cb(make_shared<optional<pair<string, uint64_t>>>());
    }
  }
}
