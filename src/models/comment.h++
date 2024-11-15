#pragma once
#include "util/common.h++"
#include "db/db.h++"
#include "fbs/records.h++"
#include "models/user.h++"
#include "models/enums.h++"
#include "models/site.h++"
#include "models/null_placeholders.h++"

namespace Ludwig {

struct CommentDetail {
  uint64_t id;
  double rank;
  Vote your_vote;
  bool saved, hidden, thread_hidden, user_hidden, board_hidden, board_subscribed, user_is_admin;
  std::reference_wrapper<const Comment> _comment;
  std::reference_wrapper<const PostStats> _stats;
  OptRef<User> _author;
  OptRef<Thread> _thread;
  OptRef<Board> _board;
  std::vector<uint64_t> path;

  auto comment() const noexcept -> const Comment& { return _comment; }
  auto stats() const noexcept -> const PostStats& { return _stats; }
  auto author() const noexcept -> const User& {
    return _author ? _author->get() : *placeholders.null_user;
  }
  auto thread() const noexcept -> const Thread& {
    return _thread ? _thread->get() : *placeholders.null_thread;
  }
  auto board() const noexcept -> const Board& {
    return _board ? _board->get() : *placeholders.null_board;
  }
  auto mod_state(PostContext context = PostContext::View) const noexcept -> ModStateDetail;
  auto content_warning(PostContext context = PostContext::View) const noexcept -> std::optional<ContentWarningDetail>;
  auto created_at() const noexcept -> Timestamp {
    return uint_to_timestamp(comment().created_at());
  }
  auto author_id() const noexcept -> uint64_t { return comment().author(); }

  auto can_view(Login login) const noexcept -> bool;
  auto should_show(Login login) const noexcept -> bool;
  auto can_reply_to(Login login) const noexcept -> bool;
  auto can_edit(Login login) const noexcept -> bool;
  auto can_delete(Login login) const noexcept -> bool;
  auto can_upvote(Login login, const SiteDetail* site) const noexcept -> bool;
  auto can_downvote(Login login, const SiteDetail* site) const noexcept -> bool;
  auto should_show_votes(Login login, const SiteDetail* site) const noexcept -> bool;

  static constexpr std::string_view noun = "comment";
  static auto get(
    ReadTxn& txn,
    uint64_t comment_id,
    Login login,
    OptRef<User> author = {},
    bool is_author_hidden = false,
    OptRef<Thread> thread = {},
    bool is_thread_hidden = false,
    OptRef<Board> board = {},
    bool is_board_hidden = false
  ) -> CommentDetail;
  static auto get_created_at(
    ReadTxn& txn,
    uint64_t id
  ) -> Timestamp {
    const auto comment = txn.get_comment(id);
    if (!comment) return Timestamp::min();
    return uint_to_timestamp(comment->get().created_at());
  }
};

struct CommentPatch {
  // TODO: Allow moving between threads?
  std::optional<std::string_view> content;
  std::optional<std::optional<std::string_view>>
    content_warning, mod_reason, board_mod_reason;
  std::optional<uint64_t> updated_at, fetched_at, deleted_at;
  std::optional<ModState> mod_state, board_mod_state;
};

auto patch_comment(
  flatbuffers::FlatBufferBuilder& fbb,
  const Comment& old,
  const CommentPatch& patch
) -> flatbuffers::Offset<Comment>;

}