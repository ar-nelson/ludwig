#include "thread.h++"
#include "local_user.h++"
#include "util/rich_text.h++"

using std::nullopt, std::optional, flatbuffers::FlatBufferBuilder, flatbuffers::Offset;
namespace chrono = std::chrono;
using namespace std::literals;

namespace Ludwig {

static constexpr uint8_t FETCH_MAX_TRIES = 6;
static constexpr chrono::seconds FETCH_BACKOFF_DELAYS[FETCH_MAX_TRIES] = { 0s, 1min, 5min, 1h, 24h, 24h * 7 };

auto ThreadDetail::can_view(Login login) const noexcept -> bool {
  if (mod_state().state >= ModState::Unapproved) {
    if (!login || (login->id != thread().author() && !login->local_user().admin())) return false;
  }
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

auto ThreadDetail::can_reply_to(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked) return false;
  if (login->local_user().admin()) return true;
  return thread().mod_state() < ModState::Locked;
}

auto ThreadDetail::can_edit(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked || thread().instance()) return false;
  return login->id == thread().author() || login->local_user().admin();
}

auto ThreadDetail::can_delete(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked || thread().instance()) return false;
  return login->id == thread().author() || login->local_user().admin();
}

auto ThreadDetail::can_upvote(Login login, const SiteDetail* site) const noexcept -> bool {
  return login && can_view(login) &&
          mod_state().state < ModState::Locked &&
          login->mod_state(thread().board()).state < ModState::Locked &&
          board().can_upvote() && (board().instance() || site->votes_enabled);
}

auto ThreadDetail::can_downvote(Login login, const SiteDetail* site) const noexcept -> bool {
  return login && can_view(login) &&
          mod_state().state < ModState::Locked &&
          login->mod_state(thread().board()).state < ModState::Locked &&
          board().can_downvote() &&
          (board().instance() || site->downvotes_enabled);
}

auto ThreadDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
  return site->votes_enabled && board().can_upvote();
}

auto ThreadDetail::should_fetch_card() const noexcept -> bool {
  if (!thread().content_url()) return false;
  if (!is_https(ada::parse(thread().content_url()->str()))) return false;
  const auto& card = link_card();
  return !card.fetch_complete() &&
    card.fetch_tries() < FETCH_MAX_TRIES &&
    now_t() > uint_to_timestamp(card.last_fetch_at().value_or(0)) + FETCH_BACKOFF_DELAYS[card.fetch_tries()];
}

auto ThreadDetail::get(
  ReadTxn& txn,
  uint64_t thread_id,
  Login login,
  OptRef<User> author,
  bool is_author_hidden,
  OptRef<Board> board,
  bool is_board_hidden
) -> ThreadDetail {
  const auto thread = txn.get_thread(thread_id);
  const auto stats = txn.get_post_stats(thread_id);
  if (!thread || !stats) throw ApiError("Database error", 500,
    fmt::format("Entry references nonexistent thread {:x} (database is inconsistent!)", thread_id)
  );
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
    .board_subscribed = login && txn.is_user_subscribed_to_board(login->id, thread->get().board()),
    .user_is_admin = txn.get_local_user(thread->get().author()).transform([](auto u){return u.get().admin();}).value_or(false),
    ._thread = *thread,
    ._stats = *stats,
    ._link_card = card,
    ._author = author,
    ._board = board
  };
}

auto ThreadDetail::mod_state(PostContext context) const noexcept -> ModStateDetail {
  using enum ModState;
  using enum ModStateSubject;
  // TODO: Board-specific user mod state
  ModStateDetail d;
  if (context != PostContext::Board && board().mod_state() > Normal && board().mod_state() >= d.state) {
    d = { Board, board().mod_state(), opt_sv(board().mod_reason()) };
  }
  if (context != PostContext::User && author().mod_state() > Normal && author().mod_state() >= d.state) {
    d = { User, author().mod_state(), opt_sv(author().mod_reason()) };
  }
  if (thread().board_mod_state() > Normal && thread().board_mod_state() >= d.state) {
    d = { ThreadInBoard, thread().board_mod_state(), opt_sv(thread().board_mod_reason()) };
  }
  if (thread().mod_state() > Normal && thread().mod_state() >= d.state) {
    d = { Thread, thread().mod_state(), opt_sv(thread().mod_reason()) };
  }
  return d;
}

auto ThreadDetail::content_warning(PostContext context) const noexcept -> optional<ContentWarningDetail> {
  using enum ContentWarningSubject;
  if (thread().content_warning()) {
    return {{ Thread, thread().content_warning()->string_view() }};
  } else if (context != PostContext::Board && context != PostContext::View && board().content_warning()) {
    return {{ Board, board().content_warning()->string_view() }};
  }
  return {};
}

auto patch_thread(FlatBufferBuilder& fbb, const Thread& old, const ThreadPatch& patch) -> Offset<Thread> {
  const auto activity_url = fbb.CreateString(old.activity_url()),
    original_post_url = fbb.CreateString(old.original_post_url()),
    content_url = update_opt_str(fbb, patch.content_url, old.content_url()),
    content_warning = update_opt_str(fbb, patch.content_warning, old.content_warning()),
    mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason()),
    board_mod_reason = update_opt_str(fbb, patch.board_mod_reason, old.board_mod_reason());
  const auto [title_type, title] =
    update_rich_text_emojis_only(fbb, patch.title, old.title_type(), old.title());
  const auto [content_text_raw, content_text_type, content_text] =
    update_rich_text(fbb, patch.content_text, old.content_text_raw());
  ThreadBuilder b(fbb);
  b.add_author(old.author());
  b.add_board(old.board());
  b.add_title_type(title_type);
  b.add_title(title);
  b.add_created_at(old.created_at());
  if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
  if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
  if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
  b.add_instance(old.instance());
  b.add_activity_url(activity_url);
  b.add_original_post_url(original_post_url);
  b.add_content_url(content_url);
  b.add_content_text_raw(content_text_raw);
  b.add_content_text_type(content_text_type);
  b.add_content_text(content_text);
  b.add_content_warning(content_warning);
  b.add_featured(patch.featured.value_or(old.featured()));
  b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
  b.add_mod_reason(mod_reason);
  b.add_board_mod_state(patch.board_mod_state.value_or(old.board_mod_state()));
  b.add_board_mod_reason(board_mod_reason);
  return b.Finish();
}

}