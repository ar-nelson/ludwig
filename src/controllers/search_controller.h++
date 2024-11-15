#pragma once
#include "db/db.h++"
#include "services/event_bus.h++"
#include "services/search_engine.h++"
#include "models/user.h++"
#include "models/thread.h++"
#include "models/comment.h++"
#include "models/board.h++"
#include "views/router_common.h++"

namespace Ludwig {

using SearchResultDetail = std::variant<UserDetail, BoardDetail, ThreadDetail, CommentDetail>;

auto search_result_detail(ReadTxn& txn, const SearchResult& result, Login login = {}) -> std::optional<SearchResultDetail>;

class SearchController {
private:
  std::shared_ptr<DB> db;
  std::shared_ptr<SearchEngine> search_engine;
  EventBus::Subscription user_sub, board_sub, thread_sub, comment_sub,
    user_del_sub, board_del_sub, thread_del_sub, comment_del_sub;

  auto event_handler(Event event, uint64_t user_id) noexcept -> void;

public:
  SearchController(
    std::shared_ptr<DB> db,
    std::shared_ptr<SearchEngine> search_engine,
    std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>()
  );
  
  auto index_all() -> void;

  template <IsRequestContext Ctx>
  auto search(const Ctx& ctx, SearchQuery query, Login login) -> RouterAwaiter<std::vector<SearchResultDetail>, Ctx>;

  friend class SearchHandler;
};

class SearchHandler : public std::enable_shared_from_this<SearchHandler> {
private:
  SearchController& controller;
  SearchQuery query;
  Login login;
  std::vector<SearchResultDetail> results;

  template <IsRequestContext Ctx>
  auto search_callback(RouterAwaiter<std::vector<SearchResultDetail>, Ctx>* awaiter) -> SearchEngine::Callback {
    return [awaiter, self = this->shared_from_this()](std::vector<SearchResult> page) {
      if (page.empty()) {
        spdlog::info("Got nothing!");
        awaiter->set_value(std::move(self->results));
        return;
      }
      spdlog::info("Got page of {:d} results", page.size());
      {
        auto txn = self->controller.db->open_read_txn();
        for (auto& r : page) {
          try {
            if (auto d = search_result_detail(txn, r, self->login)) {
              spdlog::info("+ Accepted result");
              self->results.push_back(*d);
            } else {
              spdlog::info("- Rejected result");
            }
          } catch (const std::exception& e) {
            spdlog::warn("Error in search result: {}", e.what());
          }
          if (self->results.size() >= self->query.limit) {
            spdlog::info("Done, got {:d} total results", self->results.size());
            awaiter->set_value(std::move(self->results));
            return;
          }
        }
      }
      self->query.offset += self->query.limit;
      spdlog::info("Repeating with offset {:d}", self->query.offset);
      auto se = self->controller.search_engine;
      awaiter->replace_canceler(se->search(self->query, self->search_callback(awaiter)));
    };
  }
public:
  SearchHandler(SearchController& controller, SearchQuery query, Login login) :
    controller(controller), query(query), login(login) {}

  template <IsRequestContext Ctx>
  auto awaiter(const Ctx&) {
    return RouterAwaiter<std::vector<SearchResultDetail>, Ctx>([&, se = controller.search_engine](auto* a) {
      return se->search(query, search_callback(a));
    });
  }
};

template <IsRequestContext Ctx>
auto SearchController::search(const Ctx& ctx, SearchQuery query, Login login) -> RouterAwaiter<std::vector<SearchResultDetail>, Ctx> {
  if (!search_engine) throw ApiError("Search is not enabled on this server", 403);
  auto handler = std::make_shared<SearchHandler>(*this, query, login);
  return handler->awaiter(ctx);
}

}