#pragma once
#include "services/db.h++"
#include "services/event_bus.h++"
#include "services/http_client.h++"
#include "services/search_engine.h++"
#include "services/thumbnail_cache.h++"
#include "util/rich_text.h++"

namespace Ludwig {
  class RemoteMediaController : std::enable_shared_from_this<RemoteMediaController> {
  private:
    std::shared_ptr<DB> db;
    std::shared_ptr<asio::io_context> io;
    std::shared_ptr<HttpClient> http_client;
    std::shared_ptr<LibXmlContext> xml_ctx;
    std::shared_ptr<EventBus> event_bus;
    std::optional<std::shared_ptr<SearchEngine>> search_engine;
    EventBus::Subscription sub_fetch;
    ThumbnailCache small_cache, banner_cache;
  public:
    RemoteMediaController(
      std::shared_ptr<DB> db,
      std::shared_ptr<asio::io_context> io,
      std::shared_ptr<HttpClient> http_client,
      std::shared_ptr<LibXmlContext> xml_ctx,
      std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>(),
      std::optional<std::shared_ptr<SearchEngine>> search_engine = {}
    );

    auto user_avatar(std::string_view user_name) -> Async<ThumbnailCache::Image>;
    auto user_banner(std::string_view user_name) -> Async<ThumbnailCache::Image>;
    auto board_icon(std::string_view board_name) -> Async<ThumbnailCache::Image>;
    auto board_banner(std::string_view board_name) -> Async<ThumbnailCache::Image>;
    auto thread_link_card_image(uint64_t thread_id) -> Async<ThumbnailCache::Image>;
    auto fetch_link_card_for_thread(uint64_t thread_id) -> Async<void>;
  };
}
