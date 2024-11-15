#pragma once
#include "util/common.h++"
#include "fbs/records.h++"
#include "models/board.h++"

namespace Ludwig {

struct LocalBoardDetail : BoardDetail {
  inline auto local_board() const -> const LocalBoard& { return _local_board->get(); }

  static auto get(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail;
};

struct LocalBoardPatch {
  // TODO: Allow changing owner?
  std::optional<bool> federated, private_, invite_required, invite_mod_only;
};

auto patch_local_board(
  flatbuffers::FlatBufferBuilder& fbb,
  const LocalBoard& old,
  const LocalBoardPatch& patch
) -> flatbuffers::Offset<LocalBoard>;

}