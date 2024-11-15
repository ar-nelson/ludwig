#include "user.h++"
#include "local_user.h++"
#include "util/rich_text.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset, fmt::format,
  fmt::operator""_cf; // NOLINT

namespace Ludwig {

auto UserDetail::can_view(Login login) const noexcept -> bool {
  if (login && login->id == id) return true;
  if (mod_state().state >= ModState::Unapproved) {
    if (!login || !login->local_user().admin()) return false;
  }
  return true;
}
auto UserDetail::should_show(Login login) const noexcept -> bool {
  if (hidden || (login && user().bot() && !login->local_user().show_bot_accounts()) || !can_view(login)) return false;
  return true;
}
auto UserDetail::can_change_settings(Login login) const noexcept -> bool {
  return maybe_local_user() && login && (login->local_user().admin() || login->id == id);
}

auto UserDetail::get(ReadTxn& txn, uint64_t id, Login login) -> UserDetail {
  const auto user = txn.get_user(id);
  const auto user_stats = txn.get_user_stats(id);
  if (!user || !user_stats) throw ApiError(format("User {:x} does not exist"_cf, id), 410);
  const auto local_user = txn.get_local_user(id);
  const auto hidden = login && txn.has_user_hidden_user(login->id, id);
  return { id, *user, local_user, *user_stats, hidden };
}

auto patch_user(FlatBufferBuilder& fbb, const User& old, const UserPatch& patch) -> Offset<User> {
  const auto name = fbb.CreateString(old.name()),
    actor_id = fbb.CreateString(old.actor_id()),
    inbox_url = fbb.CreateString(old.inbox_url()),
    avatar_url = update_opt_str(fbb, patch.avatar_url, old.avatar_url()),
    banner_url = update_opt_str(fbb, patch.banner_url, old.banner_url()),
    matrix_user_id = update_opt_str(fbb, patch.matrix_user_id, old.matrix_user_id()),
    mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason());
  const auto [display_name_type, display_name] =
    update_rich_text_emojis_only(fbb, patch.display_name, old.display_name_type(), old.display_name());
  const auto [bio_raw, bio_type, bio] =
    update_rich_text(fbb, patch.bio, old.bio_raw());
  UserBuilder b(fbb);
  b.add_name(name);
  b.add_display_name_type(display_name_type);
  b.add_display_name(display_name);
  b.add_bio_raw(bio_raw);
  b.add_bio_type(bio_type);
  b.add_bio(bio);
  b.add_actor_id(actor_id);
  b.add_inbox_url(inbox_url);
  b.add_matrix_user_id(matrix_user_id);
  b.add_instance(old.instance());
  b.add_created_at(old.created_at());
  if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
  if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
  if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
  b.add_avatar_url(avatar_url);
  b.add_banner_url(banner_url);
  b.add_bot(patch.bot.value_or(old.bot()));
  b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
  b.add_mod_reason(mod_reason);
  return b.Finish();
}

}