#include "search_controller.h++"

using std::exception, std::nullopt, std::optional, std::shared_ptr;
using namespace std::placeholders;

namespace Ludwig {

#define SEARCH_EVENT_HANDLER std::bind(&SearchController::event_handler, this, _1, _2)

auto search_result_detail(ReadTxn& txn, const SearchResult& result, Login login) -> optional<SearchResultDetail> {
  using enum SearchResultType;
  const auto id = result.id;
  switch (result.type) {
    case User: {
      const auto entry = UserDetail::get(txn, id, login);
      if (entry.should_show(login)) return entry;
      break;
    }
    case Board: {
      const auto entry = BoardDetail::get(txn, id, login);
      if (entry.should_show(login)) return entry;
      break;
    }
    case Thread: {
      const auto entry = ThreadDetail::get(txn, id, login);
      if (entry.should_show(login)) return entry;
      break;
    }
    case Comment: {
      const auto entry = CommentDetail::get(txn, id, login);
      if (entry.should_show(login)) return entry;
      break;
    }
  }
  return {};
}

SearchController::SearchController(
  shared_ptr<DB> db,
  shared_ptr<SearchEngine> search_engine,
  shared_ptr<EventBus> event_bus
) : db(db),
    search_engine(search_engine),
    user_sub(event_bus->on_event(Event::UserUpdate, SEARCH_EVENT_HANDLER)),
    board_sub(event_bus->on_event(Event::BoardUpdate, SEARCH_EVENT_HANDLER)), 
    thread_sub(event_bus->on_event(Event::ThreadUpdate, SEARCH_EVENT_HANDLER)),
    comment_sub(event_bus->on_event(Event::CommentUpdate, SEARCH_EVENT_HANDLER)),
    user_del_sub(event_bus->on_event(Event::UserDelete, SEARCH_EVENT_HANDLER)),
    board_del_sub(event_bus->on_event(Event::BoardDelete, SEARCH_EVENT_HANDLER)), 
    thread_del_sub(event_bus->on_event(Event::ThreadDelete, SEARCH_EVENT_HANDLER)),
    comment_del_sub(event_bus->on_event(Event::CommentDelete, SEARCH_EVENT_HANDLER)) {
  assert(db != nullptr);
}

auto SearchController::event_handler(Event event, uint64_t subject_id) noexcept -> void {
  using enum Event;
  if (!search_engine) return;
  try {
    // TODO: Ignore entries with ModState Removed or ModState Unapproved
    // Probably need to send UserUpdate when a user is approved to make this work
    switch (event) {
      case UserUpdate: {
        auto txn = db->open_read_txn();
        search_engine->index(subject_id, txn.get_user(subject_id).value());
        spdlog::debug("Indexed user {:x} in search engine", subject_id);
        break;
      }
      case BoardUpdate: {
        auto txn = db->open_read_txn();
        search_engine->index(subject_id, txn.get_board(subject_id).value());
        spdlog::debug("Indexed board {:x} in search engine", subject_id);
        break;
      }
      case ThreadUpdate: {
        auto txn = db->open_read_txn();
        const auto thread = txn.get_thread(subject_id);
        if (!thread) return;
        const auto card = thread->get().content_url() ? txn.get_link_card(thread->get().content_url()->string_view()) : nullopt;
        search_engine->index(subject_id, *thread, card);
        spdlog::debug("Indexed thread {:x} in search engine", subject_id);
        break;
      }
      case CommentUpdate: {
        auto txn = db->open_read_txn();
        search_engine->index(subject_id, txn.get_comment(subject_id).value());
        spdlog::debug("Indexed comment {:x} in search engine", subject_id);
        break;
      }
      case UserDelete:
        search_engine->unindex(subject_id, SearchResultType::User);
        break;
      case BoardDelete:
        search_engine->unindex(subject_id, SearchResultType::Board);
        break;
      case ThreadDelete:
        search_engine->unindex(subject_id, SearchResultType::Thread);
        break;
      case CommentDelete:
        search_engine->unindex(subject_id, SearchResultType::Comment);
        break;
      default:
        assert(false);
    }
  } catch (const exception& e) {
    spdlog::warn("Error in search engine update for item {:x}: {}", subject_id, e.what());
  }
}

auto SearchController::index_all() -> void {
  auto txn = db->open_read_txn();
  for (const auto user : txn.list_users_old()) {
    try {
      search_engine->index(user, txn.get_user(user).value());
    } catch (const exception& e) {
      spdlog::warn("Error adding user {:x} to search index: {}", user, e.what());
    }
  }
  for (const auto board : txn.list_boards_old()) {
    try {
      search_engine->index(board, txn.get_board(board).value());
    } catch (const exception& e) {
      spdlog::warn("Error adding board {:x} to search index: {}", board, e.what());
    }
  }
  for (const auto thread : txn.list_threads_old()) {
    try {
      search_engine->index(thread, txn.get_thread(thread).value());
    } catch (const exception& e) {
      spdlog::warn("Error adding thread {:x} to search index: {}", thread, e.what());
    }
  }
  for (const auto comment : txn.list_comments_old()) {
    try {
      search_engine->index(comment, txn.get_comment(comment).value());
    } catch (const exception& e) {
      spdlog::warn("Error adding comment {:x} to search index: {}", comment, e.what());
    }
  }
}

}