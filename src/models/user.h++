#pragma once
#include "util/common.h++"
#include "db/db.h++"
#include "fbs/records.h++"
#include "models/enums_pre.h++"

namespace Ludwig {

struct LocalUserDetail;
using Login = const std::optional<LocalUserDetail>&;

struct UserDetail {
  uint64_t id;
  std::reference_wrapper<const User> _user;
  OptRef<const LocalUser> _local_user;
  std::reference_wrapper<const UserStats> _stats;
  bool hidden;

  auto user() const noexcept -> const User& { return _user; }
  auto maybe_local_user() const noexcept -> OptRef<const LocalUser> { return _local_user; }
  auto stats() const noexcept -> const UserStats& { return _stats; }
  auto mod_state(uint64_t /* in_board_id */ = 0) const noexcept -> ModStateDetail {
    // TODO: Board-specific mod state
    if (user().mod_state() > ModState::Normal) {
      return { ModStateSubject::User, user().mod_state(), opt_sv(user().mod_reason()) };
    }
    return {};
  }
  auto created_at() const noexcept -> Timestamp {
    return uint_to_timestamp(user().created_at());
  }

  auto can_view(Login login) const noexcept -> bool;
  auto should_show(Login login) const noexcept -> bool;
  auto can_change_settings(Login login) const noexcept -> bool;

  static constexpr std::string_view noun = "user";
  static auto get(ReadTxn& txn, uint64_t id, Login login) -> UserDetail;
};

struct UserPatch {
  std::optional<std::optional<std::string_view>>
    display_name, bio, matrix_user_id, avatar_url, banner_url, mod_reason;
  std::optional<uint64_t> updated_at, fetched_at, deleted_at;
  std::optional<bool> bot;
  std::optional<ModState> mod_state;
};

auto patch_user(
  flatbuffers::FlatBufferBuilder& fbb,
  const User& old,
  const UserPatch& patch
) -> flatbuffers::Offset<User>;

}