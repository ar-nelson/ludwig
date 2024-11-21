#pragma once
#include "db/db.h++"
#include "services/event_bus.h++"
#include "services/http_client.h++"
#include "services/thumbnail_cache.h++"
#include "util/asio_common.h++"
#include "util/rich_text.h++"

namespace Ludwig {

class RemoteMediaController {
private:
  std::shared_ptr<asio::io_context> io;
  std::shared_ptr<DB> db;
  std::shared_ptr<HttpClient> http_client;
  std::shared_ptr<LibXmlContext> xml_ctx;
  std::shared_ptr<EventBus> event_bus;
  EventBus::Subscription sub_fetch;
  ThumbnailCache small_cache, banner_cache;
public:
  RemoteMediaController(
    std::shared_ptr<asio::io_context> io,
    std::shared_ptr<DB> db,
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<LibXmlContext> xml_ctx,
    std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>(),
    ThumbnailCache::Dispatcher dispatcher = [](auto f) { f(); }
  );

  auto user_avatar(std::string_view user_name) -> std::shared_ptr<CompletableOnce<ImageRef>>;
  auto user_banner(std::string_view user_name) -> std::shared_ptr<CompletableOnce<ImageRef>>;
  auto board_icon(std::string_view board_name) -> std::shared_ptr<CompletableOnce<ImageRef>>;
  auto board_banner(std::string_view board_name) -> std::shared_ptr<CompletableOnce<ImageRef>>;
  auto thread_link_card_image(uint64_t thread_id) -> std::shared_ptr<CompletableOnce<ImageRef>>;
  auto fetch_link_card_for_thread(uint64_t thread_id) noexcept -> Async<void>;
};

}
