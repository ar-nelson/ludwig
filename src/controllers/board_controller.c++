#include "board_controller.h++"
#include "util/rich_text.h++"

using std::generator, std::optional, std::pair, std::string_view, flatbuffers::FlatBufferBuilder;

namespace Ludwig {

auto BoardController::can_create_board(Login login) -> bool {
  return login && ((!site_controller->site_detail()->board_creation_admin_only &&
                    login->mod_state().state < ModState::Locked) ||
                    login->local_user().admin());
}

auto BoardController::board_detail(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail {
  const auto detail = BoardDetail::get(txn, id, {});
  if (!detail.can_view(login)) throw ApiError("Cannot view this board", 403);
  return detail;
}

auto BoardController::local_board_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail {
  const auto detail = LocalBoardDetail::get(txn, id, {});
  if (!detail.can_view(login)) throw ApiError("Cannot view this board", 403);
  return detail;
}

auto BoardController::list_boards(
  ReadTxn& txn,
  PageCursor& cursor,
  BoardSortType sort,
  bool local_only,
  bool subscribed_only,
  Login login
) -> generator<const BoardDetail&> {
  DBIter iter = [&]{
    using enum BoardSortType;
    switch (sort) {
      case New: return txn.list_boards_new(cursor.next_cursor_desc());
      case Old: return txn.list_boards_old(cursor.next_cursor_asc());
      case NewPosts: return txn.list_boards_new_posts(cursor.next_cursor_desc());
      case MostPosts: return txn.list_boards_most_posts(cursor.next_cursor_desc());
      case MostSubscribers: return txn.list_boards_most_subscribers(cursor.next_cursor_desc());
    }
  }();
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (subscribed_only && !(login && txn.is_user_subscribed_to_board(login->id, id))) continue;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_0(), *it);
    try {
      const auto d = BoardDetail::get(txn, id, login);
      if (local_only && d.board().instance()) continue;
      if (!d.should_show(login)) continue;
      co_yield d;
    } catch (const ApiError& e) {
      spdlog::warn("Board {:x} error: {}", id, e.what());
    }
  }
  cursor.reset();
}

auto BoardController::create_local_board(
  WriteTxn& txn,
  uint64_t owner,
  string_view name,
  optional<string_view> display_name,
  optional<string_view> content_warning,
  bool is_private,
  bool is_restricted_posting,
  bool is_local_only
) -> uint64_t {
  if (!regex_match(name.begin(), name.end(), username_regex)) {
    throw ApiError("Invalid board name (only letters, numbers, and underscores allowed; max 64 characters)", 400);
  }
  if (display_name && display_name->length() > 1024) {
    throw ApiError("Display name cannot be longer than 1024 bytes", 400);
  }
  if (txn.get_board_id_by_name(name)) {
    throw ApiError("A board with this name already exists on this instance", 409);
  }
  if (!can_create_board(LocalUserDetail::get_login(txn, owner))) {
    throw ApiError("User does not have permission to create boards", 403);
  }
  FlatBufferBuilder fbb;
  {
    const auto [display_name_types, display_name_values] =
      display_name ? plain_text_with_emojis_to_rich_text(fbb, *display_name) : pair(0, 0);
    const auto content_warning_s = content_warning.transform([&](auto s) { return fbb.CreateString(s); });
    const auto name_s = fbb.CreateString(name);
    BoardBuilder b(fbb);
    b.add_created_at(now_s());
    b.add_name(name_s);
    if (display_name) {
      b.add_display_name_type(display_name_types);
      b.add_display_name(display_name_values);
    }
    if (content_warning) b.add_content_warning(*content_warning_s);
    b.add_restricted_posting(is_restricted_posting);
    fbb.Finish(b.Finish());
  }
  const auto board_id = txn.create_board(fbb.GetBufferSpan());
  fbb.Clear();
  {
    LocalBoardBuilder b(fbb);
    b.add_owner(owner);
    b.add_private_(is_private);
    b.add_federated(!is_local_only);
    fbb.Finish(b.Finish());
  }
  txn.set_local_board(board_id, fbb.GetBufferSpan());
  txn.queue_event(event_bus, Event::BoardUpdate, board_id);
  return board_id;
}

auto BoardController::update_local_board(
  WriteTxn& txn,
  uint64_t id,
  optional<uint64_t> as_user,
  const LocalBoardUpdate& update
) -> void {
  const auto login = LocalUserDetail::get_login(txn, as_user);
  const auto detail = LocalBoardDetail::get(txn, id, login);
  if (login && !detail.can_change_settings(login)) {
    throw ApiError("User does not have permission to modify this board", 403);
  }
  if (update.display_name && *update.display_name && (*update.display_name)->length() > 1024) {
    throw ApiError("Display name cannot be longer than 1024 bytes", 400);
  }
  if (update.is_private || update.invite_required || update.invite_mod_only) {
    FlatBufferBuilder fbb;
    fbb.Finish(patch_local_board(fbb, detail.local_board(), {
      .private_ = update.is_private,
      .invite_required = update.invite_required,
      .invite_mod_only = update.invite_mod_only
    }));
    txn.set_local_board(id, fbb.GetBufferSpan());
  }
  if (update.display_name || update.description || update.icon_url || update.banner_url) {
    FlatBufferBuilder fbb;
    fbb.Finish(patch_board(fbb, detail.board(), {
      .display_name = update.display_name,
      .description = update.description,
      .icon_url = update.icon_url,
      .banner_url = update.banner_url
    }));
    txn.set_board(id, fbb.GetBufferSpan());
  }
  txn.queue_event(event_bus, Event::BoardUpdate, id);
}

auto BoardController::subscribe(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t board_id,
  bool subscribed
) -> void {
  if (!txn.get_user(user_id)) throw ApiError("User does not exist", 410);
  if (!txn.get_board(board_id)) throw ApiError("Board does not exist", 410);
  txn.set_subscription(user_id, board_id, subscribed);

  txn.queue_event(event_bus, Event::UserStatsUpdate, user_id);
  txn.queue_event(event_bus, Event::BoardStatsUpdate, board_id);
}

}