#pragma once
#include "util/common.h++"
#include "db/db.h++"
#include "fbs/records.h++"
#include "models/user.h++"
#include "models/site.h++"

namespace Ludwig {

struct BoardDetail {
  uint64_t id;
  std::reference_wrapper<const Board> _board;
  OptRef<const LocalBoard> _local_board;
  std::reference_wrapper<const BoardStats> _stats;
  bool hidden, subscribed;

  auto board() const noexcept -> const Board& { return _board; }
  auto maybe_local_board() const noexcept -> OptRef<const LocalBoard> { return _local_board; }
  auto stats() const noexcept -> const BoardStats& { return _stats; }
  auto mod_state() const noexcept -> ModStateDetail {
    if (board().mod_state() > ModState::Normal) {
      return { ModStateSubject::Board, board().mod_state(), opt_sv(board().mod_reason()) };
    }
    return {};
  }
  auto created_at() const noexcept -> Timestamp {
    return uint_to_timestamp(board().created_at());
  }

  auto can_view(Login login) const noexcept -> bool;
  auto should_show(Login login) const noexcept -> bool;
  auto can_create_thread(Login login) const noexcept -> bool;
  auto can_change_settings(Login login) const noexcept -> bool;
  auto should_show_votes(Login login, const SiteDetail* site) const noexcept -> bool;

  static constexpr std::string_view noun = "board";
  static auto get(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail;
};

struct BoardPatch {
  std::optional<std::optional<std::string_view>>
    display_name, description, icon_url, banner_url, content_warning, mod_reason;
  std::optional<uint64_t> updated_at, fetched_at, deleted_at;
  std::optional<bool>
    restricted_posting, approve_subscribe, can_upvote, can_downvote;
  std::optional<SortType> default_sort_type;
  std::optional<CommentSortType> default_comment_sort_type;
  std::optional<ModState> mod_state;
};

auto patch_board(
  flatbuffers::FlatBufferBuilder& fbb,
  const Board& old,
  const BoardPatch& patch
) -> flatbuffers::Offset<Board>;

}