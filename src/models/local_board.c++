#include "local_board.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

namespace Ludwig {

auto LocalBoardDetail::get(ReadTxn& txn, uint64_t id, Login login) -> LocalBoardDetail {
  const auto detail = BoardDetail::get(txn, id, login);
  if (!detail.maybe_local_board()) throw ApiError("Local user does not exist", 410);
  return { std::move(detail) };
}

auto patch_local_board(
  FlatBufferBuilder& fbb,
  const LocalBoard& old,
  const LocalBoardPatch& patch
) -> Offset<LocalBoard> {
  LocalBoardBuilder b(fbb);
  b.add_owner(old.owner());
  b.add_federated(patch.federated.value_or(old.federated()));
  b.add_private_(patch.private_.value_or(old.private_()));
  b.add_invite_required(patch.invite_required.value_or(old.invite_required()));
  b.add_invite_mod_only(patch.invite_mod_only.value_or(old.invite_mod_only()));
  return b.Finish();
}

}