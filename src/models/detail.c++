#include "detail.h++"
#include "util/web.h++"
#include <flatbuffers/flatbuffers.h>

using flatbuffers::FlatBufferBuilder, std::nullopt, std::optional, std::string,
    std::string_view;

namespace Ludwig {

  static constexpr uint8_t FETCH_MAX_TRIES = 6;
  static constexpr uint64_t FETCH_BACKOFF_DELAYS_S[FETCH_MAX_TRIES] = {
    0, 60, 60 * 5, 3600, 86400, 86400 * 7
  };

  static inline auto make_null_board() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    auto name_s = fbb.CreateString(""), display_name_s = fbb.CreateString("[deleted]");
    BoardBuilder board(fbb);
    board.add_name(name_s);
    board.add_display_name(display_name_s);
    board.add_can_upvote(false);
    board.add_can_downvote(false);
    fbb.Finish(board.Finish());
    return fbb;
  }
  static inline auto make_null_user() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    auto name_s = fbb.CreateString(""), display_name_s = fbb.CreateString("[deleted]");
    UserBuilder user(fbb);
    user.add_name(name_s);
    user.add_display_name(display_name_s);
    fbb.Finish(user.Finish());
    return fbb;
  }
  static inline auto make_null_thread() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    auto content_s = fbb.CreateString("[deleted]");
    ThreadBuilder thread(fbb);
    thread.add_content_text_raw(content_s);
    thread.add_content_text_safe(content_s);
    thread.add_title(content_s);
    fbb.Finish(thread.Finish());
    return fbb;
  }
  static inline auto make_null_link_card() -> FlatBufferBuilder {
    FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    LinkCardBuilder card(fbb);
    fbb.Finish(card.Finish());
    return fbb;
  }

  const FlatBufferBuilder ThreadDetail::null_board = make_null_board();
  const FlatBufferBuilder ThreadDetail::null_user = make_null_user();
  const FlatBufferBuilder ThreadDetail::null_link_card = make_null_link_card();
  const FlatBufferBuilder CommentDetail::null_board = make_null_board();
  const FlatBufferBuilder CommentDetail::null_user = make_null_user();
  const FlatBufferBuilder CommentDetail::null_thread = make_null_thread();

  auto ThreadDetail::can_view(Login login) const noexcept -> bool {
    if (
      thread().mod_state() >= ModState::Removed ||
      board().mod_state() >= ModState::Removed ||
      author().mod_state() >= ModState::Removed
    ) {
      if (!login || (login->id != thread().author() && !login->local_user().admin())) return false;
    }
    return true;
  }
  auto CommentDetail::can_view(Login login) const noexcept -> bool {
    if (
      comment().mod_state() >= ModState::Removed ||
      thread().mod_state() >= ModState::Removed ||
      board().mod_state() >= ModState::Removed ||
      author().mod_state() >= ModState::Removed
    ) {
      if (!login || (login->id != comment().author() && !login->local_user().admin())) return false;
    }
    // TODO: Check parent comments
    return true;
  }
  auto UserDetail::can_view(Login login) const noexcept -> bool {
    if (login && login->id == id) return true;
    if (user().mod_state() >= ModState::Removed || (maybe_local_user() && !maybe_local_user()->get().approved())) {
      if (!login || !login->local_user().admin()) return false;
    }
    return true;
  }
  auto BoardDetail::can_view(Login login) const noexcept -> bool {
    if (board().mod_state() >= ModState::Removed) {
      if (!login || !login->local_user().admin()) return false;
    }
    // TODO: Handle private boards
    return true;
  }
  auto ThreadDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || user_hidden || board_hidden || !can_view(login)) return false;
    if (login) {
      if (thread().content_warning() || board().content_warning()) {
        if (login->local_user().hide_cw_posts()) return false;
      }
      if (author().bot() && !login->local_user().show_bot_accounts()) {
        return false;
      }
      // TODO: Hide read posts
    }
    return true;
  }
  auto CommentDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || user_hidden || thread_hidden || board_hidden || !can_view(login)) return false;
    if (login) {
      if (comment().content_warning() || thread().content_warning() || board().content_warning()) {
        if (login->local_user().hide_cw_posts()) return false;
      }
      if (author().bot() && !login->local_user().show_bot_accounts()) {
        return false;
      }
      // TODO: Hide read posts
    }
    // TODO: Check parent comments
    return true;
  }
  auto UserDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || (login && user().bot() && !login->local_user().show_bot_accounts()) || !can_view(login)) return false;
    return true;
  }
  auto BoardDetail::should_show(Login login) const noexcept -> bool {
    if (hidden || !can_view(login)) return false;
    if (login) {
      if (board().content_warning() && login->local_user().hide_cw_posts()) return false;
    }
    return true;
  }
  auto BoardDetail::can_create_thread(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked) return false;
    return !board().restricted_posting() || login->local_user().admin();
  }
  auto ThreadDetail::can_reply_to(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked) return false;
    if (login->local_user().admin()) return true;
    return thread().mod_state() < ModState::Locked;
  }
  auto CommentDetail::can_reply_to(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked) return false;
    if (login->local_user().admin()) return true;
    return comment().mod_state() < ModState::Locked &&
      thread().mod_state() < ModState::Locked;
  }
  auto ThreadDetail::can_edit(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked || thread().instance()) return false;
    return login->id == thread().author() || login->local_user().admin();
  }
  auto CommentDetail::can_edit(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked || comment().instance()) return false;
    return login->id == comment().author() || login->local_user().admin();
  }
  auto ThreadDetail::can_delete(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked || thread().instance()) return false;
    return login->id == thread().author() || login->local_user().admin();
  }
  auto CommentDetail::can_delete(Login login) const noexcept -> bool {
    if (!login || login->user().mod_state() >= ModState::Locked || comment().instance()) return false;
    return login->id == comment().author() || login->local_user().admin();
  }
  auto ThreadDetail::can_upvote(Login login) const noexcept -> bool {
    return login && can_view(login) && thread().mod_state() < ModState::Locked &&
     login->user().mod_state() < ModState::Locked && board().can_upvote();
  }
  auto CommentDetail::can_upvote(Login login) const noexcept -> bool {
    return login && can_view(login) && comment().mod_state() < ModState::Locked &&
      thread().mod_state() < ModState::Locked && login->user().mod_state() < ModState::Locked &&
      board().can_upvote();
  }
  auto ThreadDetail::can_downvote(Login login) const noexcept -> bool {
    return login && can_view(login) && thread().mod_state() < ModState::Locked &&
      login->user().mod_state() < ModState::Locked && board().can_downvote();
  }
  auto CommentDetail::can_downvote(Login login) const noexcept -> bool {
    return login && can_view(login) && comment().mod_state() < ModState::Locked &&
      thread().mod_state() < ModState::Locked && login->user().mod_state() < ModState::Locked &&
      board().can_downvote();
  }
  auto BoardDetail::can_change_settings(Login login) const noexcept -> bool {
    return maybe_local_board() && login && (login->local_user().admin() || login->id == maybe_local_board()->get().owner());
  }
  auto ThreadDetail::should_fetch_card() const noexcept -> bool {
    if (!thread().content_url()) return false;
    const auto url = Url::parse(thread().content_url()->str());
    if (!url || !url->is_http_s()) return false;
    const auto& card = link_card();
    return !card.fetch_complete() &&
      card.fetch_tries() < FETCH_MAX_TRIES &&
      now_s() > card.last_fetch_at().value_or(0) + FETCH_BACKOFF_DELAYS_S[card.fetch_tries()];
  }

  static inline auto opt_str(string_view s) -> optional<string> {
    if (s.empty()) return {};
    return string(s);
  }

  auto SiteDetail::get(ReadTxnBase& txn) -> SiteDetail {
    return {
      .name = string(txn.get_setting_str(SettingsKey::name)),
      .domain = string(txn.get_setting_str(SettingsKey::domain)),
      .description = string(txn.get_setting_str(SettingsKey::description)),
      .icon_url = opt_str(txn.get_setting_str(SettingsKey::icon_url)),
      .banner_url = opt_str(txn.get_setting_str(SettingsKey::banner_url)),
      .javascript_enabled = !!txn.get_setting_int(SettingsKey::javascript_enabled),
      .infinite_scroll_enabled = !!txn.get_setting_int(SettingsKey::infinite_scroll_enabled),
      .board_creation_admin_only = !!txn.get_setting_int(SettingsKey::board_creation_admin_only),
      .registration_enabled = !!txn.get_setting_int(SettingsKey::registration_enabled),
      .registration_application_required = !!txn.get_setting_int(SettingsKey::registration_application_required),
      .registration_invite_required = !!txn.get_setting_int(SettingsKey::registration_invite_required),
      .invite_admin_only = !!txn.get_setting_int(SettingsKey::invite_admin_only)
    };
  }

  auto UserDetail::get(ReadTxnBase& txn, uint64_t id, Login login) -> UserDetail {
    const auto user = txn.get_user(id);
    const auto user_stats = txn.get_user_stats(id);
    if (!user || !user_stats) throw ApiError("User not found", 404);
    const auto local_user = txn.get_local_user(id);
    const auto hidden = login && txn.has_user_hidden_user(login->id, id);
    return { id, *user, local_user, *user_stats, hidden };
  }

  auto LocalUserDetail::get(ReadTxnBase& txn, uint64_t id, Login login) -> LocalUserDetail {
    const auto detail = UserDetail::get(txn, id, login);
    if (!detail.maybe_local_user()) throw ApiError("Local user not found", 404);
    return { std::move(detail) };
  }

  auto BoardDetail::get(ReadTxnBase& txn, uint64_t id, Login login) -> BoardDetail {
    const auto board = txn.get_board(id);
    const auto board_stats = txn.get_board_stats(id);
    if (!board || !board_stats) throw ApiError("Board not found", 404);
    const auto local_board = txn.get_local_board(id);
    const auto hidden = login && txn.has_user_hidden_board(login->id, id);
    const auto subscribed = login && txn.is_user_subscribed_to_board(login->id, id);
    return { id, *board, local_board, *board_stats, hidden, subscribed };
  }

  auto LocalBoardDetail::get(ReadTxnBase& txn, uint64_t id, Login login) -> LocalBoardDetail {
    const auto detail = BoardDetail::get(txn, id, login);
    if (!detail.maybe_local_board()) throw ApiError("Local user not found", 404);
    return { std::move(detail) };
  }

  auto ThreadDetail::get(
    ReadTxnBase& txn,
    uint64_t thread_id,
    Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> ThreadDetail {
    const auto thread = txn.get_thread(thread_id);
    const auto stats = txn.get_post_stats(thread_id);
    if (!thread || !stats) {
      spdlog::error("Entry references nonexistent thread {:x} (database is inconsistent!)", thread_id);
      throw ApiError("Database error", 500);
    }
    if (!author) {
      const auto id = thread->get().author();
      author = txn.get_user(id);
      is_author_hidden = login && (
        txn.has_user_hidden_user(login->id, id) ||
        (!login->local_user().show_bot_accounts() && author && author->get().bot())
      );
    }
    if (!board) {
      const auto id = thread->get().board();
      board = txn.get_board(id);
      const auto local_board = txn.get_local_board(id);
      is_board_hidden = (login && txn.has_user_hidden_board(login->id, id)) ||
        (local_board && local_board->get().private_() && (!login || !txn.is_user_subscribed_to_board(login->id, id)));
    }
    const auto card = thread->get().content_url() ? txn.get_link_card(thread->get().content_url()->string_view()) : nullopt;
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, thread_id) : Vote::NoVote;
    return {
      .id = thread_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, thread_id),
      .hidden = login && txn.has_user_hidden_post(login->id, thread_id),
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      ._thread = *thread,
      ._stats = *stats,
      ._link_card = card,
      ._author = author,
      ._board = board
    };
  }

  auto CommentDetail::get(
    ReadTxnBase& txn,
    uint64_t comment_id,
    Login login,
    OptRef<User> author,
    bool is_author_hidden,
    OptRef<Thread> thread,
    bool is_thread_hidden,
    OptRef<Board> board,
    bool is_board_hidden
  ) -> CommentDetail {
    const auto comment = txn.get_comment(comment_id);
    const auto stats = txn.get_post_stats(comment_id);
    if (!comment || !stats) {
      spdlog::error("Entry references nonexistent comment {:x} (database is inconsistent!)", comment_id);
      throw ApiError("Database error", 500);
    }
    if (!author) {
      const auto id = comment->get().author();
      author = txn.get_user(id);
      is_author_hidden = login && (
        txn.has_user_hidden_user(login->id, id) ||
        (!login->local_user().show_bot_accounts() && author && author->get().bot())
      );
    }
    if (!thread) {
      const auto id = comment->get().thread();
      thread = txn.get_thread(id);
      is_thread_hidden = login && txn.has_user_hidden_post(login->id, id);
    }
    if (!board) {
      const auto id = thread->get().board();
      board = txn.get_board(id);
      const auto local_board = txn.get_local_board(id);
      is_board_hidden = (login && txn.has_user_hidden_board(login->id, id)) ||
        (local_board && local_board->get().private_() && (!login || !txn.is_user_subscribed_to_board(login->id, id)));
    }
    const Vote vote = login ? txn.get_vote_of_user_for_post(login->id, comment_id) : Vote::NoVote;
    return {
      .id = comment_id,
      .your_vote = vote,
      .saved = login && txn.has_user_saved_post(login->id, comment_id),
      .hidden = login && txn.has_user_hidden_post(login->id, comment_id),
      .thread_hidden = is_thread_hidden,
      .user_hidden = is_author_hidden,
      .board_hidden = is_board_hidden,
      ._comment = *comment,
      ._stats = *stats,
      ._author = author,
      ._thread = thread,
      ._board = board
    };
  }
}
