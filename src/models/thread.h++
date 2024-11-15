#pragma once
#include "util/common.h++"
#include "db/db.h++"
#include "fbs/records.h++"
#include "models/user.h++"
#include "models/enums.h++"
#include "models/site.h++"
#include "models/null_placeholders.h++"

namespace Ludwig {

struct ThreadDetail {
  static const LinkCard* const null_link_card;
  static const User* const null_user;
  static const Board* const null_board;

  uint64_t id;
  double rank;
  Vote your_vote;
  bool saved, hidden, user_hidden, board_hidden, board_subscribed, user_is_admin;
  std::reference_wrapper<const Thread> _thread;
  std::reference_wrapper<const PostStats> _stats;
  OptRef<LinkCard> _link_card;
  OptRef<User> _author;
  OptRef<Board> _board;

  auto thread() const noexcept -> const Thread& { return _thread; }
  auto stats() const noexcept -> const PostStats& { return _stats; }
  auto link_card() const noexcept -> const LinkCard& {
    return _link_card ? _link_card->get() : *placeholders.null_link_card;
  }
  auto author() const noexcept -> const User& {
    return _author ? _author->get() : *placeholders.null_user;
  }
  auto board() const noexcept -> const Board& {
    return _board ? _board->get() : *placeholders.null_board;
  }
  auto mod_state(PostContext context = PostContext::View) const noexcept -> ModStateDetail;
  auto content_warning(PostContext context = PostContext::View) const noexcept -> std::optional<ContentWarningDetail>;
  auto created_at() const noexcept -> Timestamp {
    return uint_to_timestamp(thread().created_at());
  }
  auto author_id() const noexcept -> uint64_t { return thread().author(); }
  auto has_text_content() const noexcept -> bool {
    return thread().content_text() && thread().content_text()->size() &&
            !(thread().content_text()->size() == 1 &&
              thread().content_text_type()->GetEnum<RichText>(0) == RichText::Text &&
              !thread().content_text()->GetAsString(0)->size());
  }

  auto can_view(Login login) const noexcept -> bool;
  auto should_show(Login login) const noexcept -> bool;
  auto can_reply_to(Login login) const noexcept -> bool;
  auto can_edit(Login login) const noexcept -> bool;
  auto can_delete(Login login) const noexcept -> bool;
  auto can_upvote(Login login, const SiteDetail* site) const noexcept -> bool;
  auto can_downvote(Login login, const SiteDetail* site) const noexcept -> bool;
  auto should_show_votes(Login login, const SiteDetail* site) const noexcept -> bool;
  auto should_fetch_card() const noexcept -> bool;

  static constexpr std::string_view noun = "thread";
  static auto get(
    ReadTxn& txn,
    uint64_t thread_id,
    Login login,
    OptRef<User> author = {},
    bool is_author_hidden = false,
    OptRef<Board> board = {},
    bool is_board_hidden = false
  ) -> ThreadDetail;
  static auto get_created_at(
    ReadTxn& txn,
    uint64_t id
  ) -> Timestamp {
    const auto thread = txn.get_thread(id);
    if (!thread) return Timestamp::min();
    return uint_to_timestamp(thread->get().created_at());
  }
};

struct ThreadPatch {
  // TODO: Allow moving between boards?
  std::optional<std::string_view> title;
  std::optional<std::optional<std::string_view>>
    content_url, content_text, content_warning, mod_reason, board_mod_reason;
  std::optional<uint64_t> updated_at, fetched_at, deleted_at;
  std::optional<bool> featured;
  std::optional<ModState> mod_state, board_mod_state;
};

auto patch_thread(
  flatbuffers::FlatBufferBuilder& fbb,
  const Thread& old,
  const ThreadPatch& patch
) -> flatbuffers::Offset<Thread>;

}