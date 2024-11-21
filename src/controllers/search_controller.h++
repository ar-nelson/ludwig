#pragma once
#include "db/db.h++"
#include "services/event_bus.h++"
#include "services/search_engine.h++"
#include "models/user.h++"
#include "models/thread.h++"
#include "models/comment.h++"
#include "models/board.h++"
#include <atomic>

namespace Ludwig {

using SearchResultDetail = std::variant<UserDetail, BoardDetail, ThreadDetail, CommentDetail>;

auto search_result_detail(ReadTxn& txn, const SearchResult& result, Login login = {}) -> std::optional<SearchResultDetail>;

class CompletableSearch;

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

  auto search(SearchQuery query, Login login) -> std::shared_ptr<CompletableSearch>;

  friend class CompletableSearch;
};

class CompletableSearch : public CompletableOnce<std::vector<SearchResultDetail>>, public std::enable_shared_from_this<CompletableSearch> {
private:
  SearchController& controller;
  SearchQuery query;
  Login login;
  std::atomic<std::shared_ptr<CompletableOnce<std::vector<SearchResult>>>> last_page;
  std::vector<SearchResultDetail> results;

  void on_page(std::vector<SearchResult> page) {
    if (page.empty()) {
      spdlog::info("Got nothing!");
      complete(std::move(results));
      return;
    }
    spdlog::info("Got page of {:d} results", page.size());
    {
      auto txn = controller.db->open_read_txn();
      for (auto& r : page) {
        try {
          if (auto d = search_result_detail(txn, r, login)) {
            spdlog::info("+ Accepted result");
            results.push_back(*d);
          } else {
            spdlog::info("- Rejected result");
          }
        } catch (const std::exception& e) {
          spdlog::warn("Error in search result: {}", e.what());
        }
        if (results.size() >= query.limit) {
          spdlog::info("Done, got {:d} total results", results.size());
          complete(std::move(results));
          return;
        }
      }
    }
    if (is_canceled()) return;
    query.offset += query.limit;
    spdlog::info("Repeating with offset {:d}", query.offset);
    last_page.store(controller.search_engine->search(query), std::memory_order_release);
    last_page.load(std::memory_order_acquire)->on_complete([self = this->shared_from_this()](auto v){
      self->on_page(v);
    });
  }
public:
  CompletableSearch(SearchController& controller, SearchQuery query, Login login) :
    controller(controller), query(query), login(login), last_page(controller.search_engine->search(query)) {
    last_page.load(std::memory_order_acquire)->on_complete([self = this->shared_from_this()](auto v){
      self->on_page(v);
    });
  }

  void cancel() noexcept override {
    CompletableOnce<std::vector<SearchResultDetail>>::cancel();
    last_page.load(std::memory_order_acquire)->cancel();
  }
};

}