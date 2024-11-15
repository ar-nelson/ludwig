#include "comment.h++"
#include "local_user.h++"
#include "util/rich_text.h++"

using std::optional, std::vector, flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

namespace Ludwig {

auto CommentDetail::can_view(Login login) const noexcept -> bool {
  if (mod_state().state >= ModState::Unapproved) {
    if (!login || (login->id != comment().author() && !login->local_user().admin())) return false;
  }
  // TODO: Check parent comments
  return true;
}

auto CommentDetail::should_show(Login login) const noexcept -> bool {
  if (hidden || user_hidden || thread_hidden || board_hidden || !can_view(login)) return false;
  if (login) {
    if (login->local_user().hide_cw_posts()) {
      if (comment().content_warning() || thread().content_warning() || board().content_warning()) {
        return false;
      }
    }
    if (author().bot() && !login->local_user().show_bot_accounts()) {
      return false;
    }
    // TODO: Hide read posts
  }
  // TODO: Check parent comments
  return true;
}

auto CommentDetail::can_reply_to(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked) return false;
  if (login->local_user().admin()) return true;
  return comment().mod_state() < ModState::Locked &&
    thread().mod_state() < ModState::Locked;
}

auto CommentDetail::can_edit(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked || comment().instance()) return false;
  return login->id == comment().author() || login->local_user().admin();
}

auto CommentDetail::can_delete(Login login) const noexcept -> bool {
  if (!login || login->mod_state(thread().board()).state >= ModState::Locked || comment().instance()) return false;
  return login->id == comment().author() || login->local_user().admin();
}

auto CommentDetail::can_upvote(Login login, const SiteDetail* site) const noexcept -> bool {
  return login && can_view(login) &&
          mod_state().state < ModState::Locked &&
          login->mod_state(thread().board()).state < ModState::Locked &&
          board().can_upvote() && (board().instance() || site->votes_enabled);
}

auto CommentDetail::can_downvote(Login login, const SiteDetail* site) const noexcept -> bool {
  return login && can_view(login) &&
          mod_state().state < ModState::Locked &&
          login->mod_state(thread().board()).state < ModState::Locked &&
          board().can_downvote() &&
          (board().instance() || site->downvotes_enabled);
}

auto CommentDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
  return site->votes_enabled && board().can_upvote();
}

auto CommentDetail::get(
  ReadTxn& txn,
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
  if (!comment || !stats) throw ApiError("Database error", 500,
    fmt::format("Entry references nonexistent comment {:x} (database is inconsistent!)", comment_id)
  );
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
  vector<uint64_t> path;
  for (
    auto c = comment;
    c && c->get().parent() != c->get().thread();
    c = txn.get_comment(c->get().parent())
  ) {
    path.insert(path.begin(), c->get().parent());
  }
  return {
    .id = comment_id,
    .your_vote = vote,
    .saved = login && txn.has_user_saved_post(login->id, comment_id),
    .hidden = login && txn.has_user_hidden_post(login->id, comment_id),
    .thread_hidden = is_thread_hidden,
    .user_hidden = is_author_hidden,
    .board_hidden = is_board_hidden,
    .board_subscribed = login && txn.is_user_subscribed_to_board(login->id, thread->get().board()),
    .user_is_admin = txn.get_local_user(comment->get().author()).transform([](auto u){return u.get().admin();}).value_or(false),
    ._comment = *comment,
    ._stats = *stats,
    ._author = author,
    ._thread = thread,
    ._board = board,
    .path = path
  };
}

auto CommentDetail::mod_state(PostContext context) const noexcept -> ModStateDetail {
  using enum ModState;
  using enum ModStateSubject;
  // TODO: Board-specific user mod state
  ModStateDetail d;
  if (context != PostContext::Board && context != PostContext::Reply && board().mod_state() > Normal && board().mod_state() >= d.state) {
    d = { Board, board().mod_state(), opt_sv(board().mod_reason()) };
  }
  if (context != PostContext::User && author().mod_state() > Normal && author().mod_state() >= d.state) {
    d = { User, author().mod_state(), opt_sv(author().mod_reason()) };
  }
  if (context != PostContext::Reply && thread().board_mod_state() > Normal && thread().board_mod_state() >= d.state) {
    d = { ThreadInBoard, thread().board_mod_state(), opt_sv(thread().board_mod_reason()) };
  }
  if (context != PostContext::Reply && thread().mod_state() > Normal && thread().mod_state() >= d.state) {
    d = { Thread, thread().mod_state(), opt_sv(thread().mod_reason()) };
  }
  if (comment().board_mod_state() > Normal && comment().board_mod_state() >= d.state) {
    d = { CommentInBoard, comment().board_mod_state(), opt_sv(comment().board_mod_reason()) };
  }
  if (comment().mod_state() > Normal && comment().mod_state() >= d.state) {
    d = { Comment, comment().mod_state(), opt_sv(comment().mod_reason()) };
  }
  return d;
}

auto CommentDetail::content_warning(PostContext context) const noexcept -> optional<ContentWarningDetail> {
  using enum ContentWarningSubject;
  if (comment().content_warning()) {
    return {{ Comment, comment().content_warning()->string_view() }};
  } else if (context != PostContext::Reply && thread().content_warning()) {
    return {{ Thread, thread().content_warning()->string_view() }};
  } else if (context != PostContext::Board && context != PostContext::View && context != PostContext::Reply && board().content_warning()) {
    return {{ Board, board().content_warning()->string_view() }};
  }
  return {};
}

auto patch_comment(FlatBufferBuilder& fbb, const Comment& old, const CommentPatch& patch) -> Offset<Comment> {
  const auto activity_url = fbb.CreateString(old.activity_url()),
    original_post_url = fbb.CreateString(old.original_post_url()),
    content_warning = update_opt_str(fbb, patch.content_warning, old.content_warning()),
    mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason()),
    board_mod_reason = update_opt_str(fbb, patch.board_mod_reason, old.board_mod_reason());
  const auto [content_raw, content_type, content] =
    update_rich_text(fbb, patch.content, old.content_raw());
  CommentBuilder b(fbb);
  b.add_author(old.author());
  b.add_parent(old.parent());
  b.add_thread(old.thread());
  b.add_created_at(old.created_at());
  if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
  if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
  if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
  b.add_instance(old.instance());
  b.add_activity_url(activity_url);
  b.add_original_post_url(original_post_url);
  b.add_content_raw(content_raw);
  b.add_content_type(content_type);
  b.add_content(content);
  b.add_content_warning(content_warning);
  b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
  b.add_mod_reason(mod_reason);
  b.add_board_mod_state(patch.board_mod_state.value_or(old.board_mod_state()));
  b.add_board_mod_reason(board_mod_reason);
  return b.Finish();
}

}