#pragma once
#include "db/db.h++"
#include "db/page_cursor.h++"
#include "services/event_bus.h++"
#include "models/thread.h++"
#include "models/comment.h++"
#include "site_controller.h++"
#include <parallel_hashmap/phmap.h>
#include <parallel_hashmap/btree.h>

namespace Ludwig {

struct CommentTree {
  phmap::flat_hash_map<uint64_t, PageCursor> continued;
  phmap::btree_multimap<uint64_t, CommentDetail> comments;

  auto size() const -> size_t {
    return comments.size();
  }
  auto emplace(uint64_t parent, CommentDetail e) -> void {
    comments.emplace(parent, e);
  }
  auto mark_continued(uint64_t parent, PageCursor from = {}) {
    continued.emplace(parent, from);
  }
};

struct ThreadUpdate {
  std::optional<std::string_view> title;
  std::optional<std::optional<std::string_view>> text_content, content_warning;
};

struct CommentUpdate {
  std::optional<std::string_view> text_content;
  std::optional<std::optional<std::string_view>> content_warning;
};

class PostController {
private:
  std::shared_ptr<SiteController> site_controller;
  std::shared_ptr<EventBus> event_bus;

  auto fetch_card(const ThreadDetail& thread) -> void {
    if (thread.should_fetch_card()) event_bus->dispatch(Event::ThreadFetchLinkCard, thread.id);
  };

public:
  static constexpr uint64_t FEED_ALL = 0, FEED_LOCAL = 1, FEED_HOME = 2;

  PostController(
    std::shared_ptr<SiteController> site,
    std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>()
  ) : site_controller(site), event_bus(event_bus) {
    assert(site != nullptr);
    assert(event_bus != nullptr);
  }

  auto thread_detail(
    ReadTxn& txn,
    CommentTree& tree_out,
    uint64_t id,
    CommentSortType sort = CommentSortType::Hot,
    Login login = {},
    PageCursor from = {},
    uint16_t limit = 20
  ) -> ThreadDetail;
  auto comment_detail(
    ReadTxn& txn,
    CommentTree& tree_out,
    uint64_t id,
    CommentSortType sort = CommentSortType::Hot,
    Login login = {},
    PageCursor from = {},
    uint16_t limit = 20
  ) -> CommentDetail;

  auto list_board_threads(
    ReadTxn& txn,
    PageCursor& cursor,
    uint64_t board_id,
    SortType sort = SortType::Active,
    Login login = {}
  ) -> std::generator<const ThreadDetail&>;
  auto list_board_comments(
    ReadTxn& txn,
    PageCursor& cursor,
    uint64_t board_id,
    SortType sort = SortType::Active,
    Login login = {}
  ) -> std::generator<const CommentDetail&>;
  auto list_feed_threads(
    ReadTxn& txn,
    PageCursor& cursor,
    uint64_t feed_id,
    SortType sort = SortType::Active,
    Login login = {}
  ) -> std::generator<const ThreadDetail&>;
  auto list_feed_comments(
    ReadTxn& txn,
    PageCursor& from,
    uint64_t feed_id,
    SortType sort = SortType::Active,
    Login login = {}
  ) -> std::generator<const CommentDetail&>;
  auto list_user_threads(
    ReadTxn& txn,
    PageCursor& cursor,
    uint64_t user_id,
    UserPostSortType sort = UserPostSortType::New,
    Login login = {}
  ) -> std::generator<const ThreadDetail&>;
  auto list_user_comments(
    ReadTxn& txn,
    PageCursor& from,
    uint64_t user_id,
    UserPostSortType sort = UserPostSortType::New,
    Login login = {}
  ) -> std::generator<const CommentDetail&>;

  auto create_thread(
    WriteTxn& txn,
    uint64_t author,
    uint64_t board,
    std::optional<std::string_view> remote_post_url,
    std::optional<std::string_view> remote_activity_url,
    Timestamp created_at,
    std::optional<Timestamp> updated_at,
    std::string_view title,
    std::optional<std::string_view> submission_url,
    std::optional<std::string_view> text_content_markdown,
    std::optional<std::string_view> content_warning = {}
  ) -> uint64_t;
  auto create_local_thread(
    WriteTxn& txn,
    uint64_t author,
    uint64_t board,
    std::string_view title,
    std::optional<std::string_view> submission_url,
    std::optional<std::string_view> text_content_markdown,
    std::optional<std::string_view> content_warning = {}
  ) -> uint64_t;
  auto update_thread(
    WriteTxn& txn,
    uint64_t id,
    std::optional<uint64_t> as_user,
    const ThreadUpdate& update
  ) -> void;
  auto create_comment(
    WriteTxn& txn,
    uint64_t author,
    uint64_t parent,
    std::optional<std::string_view> remote_post_url,
    std::optional<std::string_view> remote_activity_url,
    Timestamp created_at,
    std::optional<Timestamp> updated_at,
    std::string_view text_content_markdown,
    std::optional<std::string_view> content_warning = {},
    Login login = {}
  ) -> uint64_t;
  auto create_local_comment(
    WriteTxn& txn,
    uint64_t author,
    uint64_t parent,
    std::string_view text_content_markdown,
    std::optional<std::string_view> content_warning = {}
  ) -> uint64_t;
  auto update_comment(
    WriteTxn& txn,
    uint64_t id,
    std::optional<uint64_t> as_user,
    const CommentUpdate& update
  ) -> void;
  auto vote(WriteTxn& txn, uint64_t user_id, uint64_t post_id, Vote vote) -> void;
};

}