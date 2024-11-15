#pragma once
#include "db/db.h++"
#include "db/page_cursor.h++"
#include "services/event_bus.h++"
#include "models/local_user.h++"
#include "models/board.h++"
#include "models/local_board.h++"
#include "site_controller.h++"

namespace Ludwig {

struct LocalBoardUpdate {
  std::optional<std::optional<std::string_view>> display_name, description,
      icon_url, banner_url, content_warning;
  std::optional<bool> is_private, restricted_posting, approve_subscribe,
      invite_required, invite_mod_only, can_upvote, can_downvote;
};

class BoardController {
private:
  std::shared_ptr<SiteController> site_controller;
  std::shared_ptr<EventBus> event_bus;

public:
  BoardController(
    std::shared_ptr<SiteController> site,
    std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>()
  ) : site_controller(site),
      event_bus(event_bus) {
    assert(site != nullptr);
    assert(event_bus != nullptr);
  }

  auto can_create_board(Login login) -> bool;

  auto board_detail(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail;
  auto local_board_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail;

  auto list_boards(
    ReadTxn& txn,
    PageCursor& cursor,
    BoardSortType sort,
    bool local_only,
    bool subscribed_only,
    Login login = {}
  ) -> std::generator<const BoardDetail&>;
  auto create_local_board(
    WriteTxn& txn,
    uint64_t owner,
    std::string_view name,
    std::optional<std::string_view> display_name,
    std::optional<std::string_view> content_warning = {},
    bool is_private = false,
    bool is_restricted_posting = false,
    bool is_local_only = false
  ) -> uint64_t;
  auto update_local_board(
    WriteTxn& txn,
    uint64_t id,
    std::optional<uint64_t> as_user,
    const LocalBoardUpdate& update
  ) -> void;
  auto subscribe(
    WriteTxn& txn,
    uint64_t user_id,
    uint64_t board_id,
    bool subscribed = true
  ) -> void;
};

}