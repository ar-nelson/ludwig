#include "local_user.h++"
#include "controllers/user_controller.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset, fmt::format,
  fmt::operator""_cf; // NOLINT

namespace Ludwig {

auto LocalUserDetail::get(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail {
  const auto detail = UserDetail::get(txn, id, login);
  const auto stats = txn.get_local_user_stats(id);
  if (!detail.maybe_local_user() || !stats) throw ApiError(format("Local user {:x} does not exist", id), 410);
  return { std::move(detail), *stats };
}

auto LocalUserDetail::get_login(ReadTxn& txn, uint64_t id) -> LocalUserDetail {
  if (!id) throw ApiError("Not logged in", 401);
  try { return get(txn, id, {}); }
  catch (const ApiError& e) {
    if (e.http_status == 410) throw ApiError(format("Logged in user {:x} does not exist"_cf, id), 401);
    else throw e;
  }
}

auto patch_local_user(FlatBufferBuilder& fbb, const LocalUser& old, LocalUserPatch&& patch) -> Offset<LocalUser> {
  const auto email = update_opt_str(fbb, patch.email, old.email()),
    lemmy_theme = update_opt_str(fbb, patch.lemmy_theme, old.lemmy_theme());
  LocalUserBuilder b(fbb);
  b.add_email(email);
  if (patch.password) {
    Salt salt;
    Hash hash;
    if (!RAND_bytes((uint8_t*)salt.bytes()->Data(), salt.bytes()->size())) {
      throw ApiError("Internal server error", 500, "Not enough randomness to generate secure password salt");
    }
    UserController::hash_password(std::move(*patch.password), salt.bytes()->Data(), (uint8_t*)hash.bytes()->Data());
    b.add_password_hash(&hash);
    b.add_password_salt(&salt);
  } else {
    b.add_password_hash(old.password_hash());
    b.add_password_salt(old.password_salt());
  }
  b.add_admin(patch.admin.value_or(old.admin()));
  b.add_accepted_application(patch.accepted_application.value_or(old.accepted_application()));
  b.add_email_verified(patch.email_verified.value_or(old.email_verified()));
  b.add_invite(patch.invite.value_or(old.invite()));
  b.add_open_links_in_new_tab(patch.open_links_in_new_tab.value_or(old.open_links_in_new_tab()));
  b.add_send_notifications_to_email(patch.send_notifications_to_email.value_or(old.send_notifications_to_email()));
  b.add_show_avatars(patch.show_avatars.value_or(old.show_avatars()));
  b.add_show_images_threads(patch.show_images_threads.value_or(old.show_images_threads()));
  b.add_show_images_comments(patch.show_images_comments.value_or(old.show_images_comments()));
  b.add_show_bot_accounts(patch.show_bot_accounts.value_or(old.show_bot_accounts()));
  b.add_show_new_post_notifs(patch.show_new_post_notifs.value_or(old.show_new_post_notifs()));
  b.add_hide_cw_posts(patch.hide_cw_posts.value_or(old.hide_cw_posts()));
  b.add_expand_cw_posts(patch.expand_cw_posts.value_or(old.expand_cw_posts()));
  b.add_expand_cw_images(patch.expand_cw_images.value_or(old.expand_cw_images()));
  b.add_show_read_posts(patch.show_read_posts.value_or(old.show_read_posts()));
  b.add_show_karma(patch.show_karma.value_or(old.show_karma()));
  b.add_javascript_enabled(patch.javascript_enabled.value_or(old.javascript_enabled()));
  b.add_infinite_scroll_enabled(patch.infinite_scroll_enabled.value_or(old.infinite_scroll_enabled()));
  b.add_theme(patch.theme.value_or(old.theme()));
  b.add_lemmy_theme(lemmy_theme);
  b.add_default_sort_type(patch.default_sort_type.value_or(old.default_sort_type()));
  b.add_default_comment_sort_type(patch.default_comment_sort_type.value_or(old.default_comment_sort_type()));
  return b.Finish();
}

}