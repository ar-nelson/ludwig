#include "user_controller.h++"

using std::generator, std::optional, std::regex_match, std::string,
  std::string_view, flatbuffers::FlatBufferBuilder;

namespace Ludwig {

// PBKDF2-HMAC-SHA256 iteration count, as suggested by
// https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html#pbkdf2
static constexpr uint32_t PASSWORD_HASH_ITERATIONS = 600'000;

auto UserController::hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void {
  if (!PKCS5_PBKDF2_HMAC(
    password.data.data(), password.data.length(),
    salt, 16,
    PASSWORD_HASH_ITERATIONS, EVP_sha256(),
    32, hash
  )) {
    throw std::bad_alloc();
  }
}

auto UserController::user_detail(ReadTxn& txn, uint64_t id, Login login) -> UserDetail {
  const auto detail = UserDetail::get(txn, id, login);
  if (!detail.can_view(login)) throw ApiError("Cannot view this user", 403);
  return detail;
}

auto UserController::local_user_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail {
  const auto detail = LocalUserDetail::get(txn, id, {});
  if (!detail.can_view(login)) throw ApiError("Cannot view this user", 403);
  return detail;
}

auto UserController::list_users(
  ReadTxn& txn,
  PageCursor& cursor,
  UserSortType sort,
  bool local_only,
  Login login
) -> generator<const UserDetail&> {
  DBIter iter = [&]{
    using enum UserSortType;
    switch (sort) {
      case New: return txn.list_users_new(cursor.next_cursor_desc());
      // TODO: Figure out what changed here in the refactor that removed `limit`.
      // `Old` used to require `next_cursor_asc`, now it needs `next_cursor_desc`.
      // Not sure what the difference is, this works, but why?
      case Old: return txn.list_users_old(cursor.next_cursor_desc());
      case NewPosts: return txn.list_users_new_posts(cursor.next_cursor_desc());
      case MostPosts: return txn.list_users_most_posts(cursor.next_cursor_desc());
    }
  }();
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_0(), *it);
    try {
      const auto d = UserDetail::get(txn, id, login);
      if (local_only && d.user().instance()) continue;
      if (!d.should_show(login)) continue;
      co_yield d;
    } catch (const ApiError& e) {
      spdlog::warn("User {:x} error: {}", id, e.what());
    }
  }
  cursor.reset();
}

auto UserController::create_local_user(
  WriteTxn& txn,
  string_view username,
  optional<string_view> email,
  SecretString&& password,
  bool is_bot,
  optional<uint64_t> invite,
  IsApproved is_approved,
  IsAdmin is_admin
) -> uint64_t {
  if (!regex_match(username.begin(), username.end(), username_regex)) {
    throw ApiError("Invalid username (only letters, numbers, and underscores allowed; max 64 characters)", 400);
  }
  if (email && !regex_match(email->begin(), email->end(), email_regex)) {
    throw ApiError("Invalid email address", 400);
  }
  if (password.data.length() < 8) {
    throw ApiError("Password must be at least 8 characters", 400);
  }
  if (txn.get_user_id_by_name(username)) {
    throw ApiError("A user with this name already exists on this instance", 409);
  }
  if (email && txn.get_user_id_by_email(*email)) {
    throw ApiError("A user with this email address already exists on this instance", 409);
  }
  uint8_t salt[16], hash[32];
  if (!RAND_bytes(salt, 16)) {
    throw ApiError("Internal server error", 500, "Not enough randomness to generate secure password salt");
  }
  hash_password(std::move(password), salt, hash);
  FlatBufferBuilder fbb;
  {
    union { uint8_t bytes[4]; uint32_t n; } salt;
    RAND_pseudo_bytes(salt.bytes, 4);
    const auto name_s = fbb.CreateString(username);
    UserBuilder b(fbb);
    b.add_created_at(now_s());
    b.add_name(name_s);
    b.add_bot(is_bot);
    b.add_salt(salt.n);
    if (is_approved == IsApproved::Yes) {
      b.add_mod_state(ModState::Approved);
    } else {
      const auto* site = site_controller->site_detail();
      if (site->registration_application_required) b.add_mod_state(ModState::Unapproved);
    }
    fbb.Finish(b.Finish());
  }
  const auto user_id = txn.create_user(fbb.GetBufferSpan());
  fbb.Clear();
  {
    const auto email_s = email.transform([&](auto s) { return fbb.CreateString(s); });
    Hash hash_struct(hash);
    Salt salt_struct(salt);
    LocalUserBuilder b(fbb);
    if (email_s) b.add_email(*email_s);
    b.add_password_hash(&hash_struct);
    b.add_password_salt(&salt_struct);
    b.add_admin(is_admin == IsAdmin::Yes);
    if (invite) b.add_invite(*invite);
    fbb.Finish(b.Finish());
  }
  txn.set_local_user(user_id, fbb.GetBufferSpan());
  txn.queue_event(event_bus, Event::UserUpdate, user_id);
  return user_id;
}

auto UserController::update_local_user(
  WriteTxn& txn,
  uint64_t id,
  optional<uint64_t> as_user,
  const LocalUserUpdate& update
) -> void {
  const auto login = LocalUserDetail::get_login(txn, as_user);
  const auto detail = LocalUserDetail::get(txn, id, login);
  if (login && !detail.can_change_settings(login)) {
    throw ApiError("User does not have permission to modify this user", 403);
  }
  if (update.email && !regex_match(update.email->cbegin(), update.email->cend(), email_regex)) {
    throw ApiError("Invalid email address", 400);
  }
  if (update.email && txn.get_user_id_by_email(string(*update.email))) {
    throw ApiError("A user with this email address already exists on this instance", 409);
  }
  if (update.display_name && *update.display_name && (*update.display_name)->length() > 1024) {
    throw ApiError("Display name cannot be longer than 1024 bytes", 400);
  }
  if (update.email || update.admin || update.open_links_in_new_tab || update.show_avatars ||
      update.show_bot_accounts || update.hide_cw_posts ||
      update.expand_cw_posts || update.expand_cw_images ||
      update.show_karma || update.javascript_enabled ||
      update.infinite_scroll_enabled || update.default_sort_type ||
      update.default_comment_sort_type) {
    FlatBufferBuilder fbb;
    fbb.Finish(patch_local_user(fbb, detail.local_user(), {
      .email = update.email,
      .admin = update.admin.transform([](auto x){return x == IsAdmin::Yes;}),
      .open_links_in_new_tab = update.open_links_in_new_tab,
      .show_avatars = update.show_avatars,
      .show_bot_accounts = update.show_bot_accounts,
      .hide_cw_posts = update.hide_cw_posts,
      .expand_cw_posts = update.expand_cw_posts,
      .expand_cw_images = update.expand_cw_images,
      .show_karma = update.show_karma,
      .javascript_enabled = update.javascript_enabled,
      .infinite_scroll_enabled = update.infinite_scroll_enabled,
      .default_sort_type = update.default_sort_type,
      .default_comment_sort_type = update.default_comment_sort_type
    }));
    txn.set_local_user(id, fbb.GetBufferSpan());
  }
  if (update.display_name || update.bio || update.avatar_url || update.banner_url || update.bot) {
    FlatBufferBuilder fbb;
    fbb.Finish(patch_user(fbb, detail.user(), {
      .display_name = update.display_name,
      .bio = update.bio,
      .avatar_url = update.avatar_url,
      .banner_url = update.banner_url,
      .updated_at = now_s(),
      .bot = update.bot
    }));
    txn.set_user(id, fbb.GetBufferSpan());
    txn.queue_event(event_bus, Event::UserUpdate, id);
  }
}

auto UserController::save_post(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t post_id,
  bool saved
) -> void {
  if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
  if (!txn.get_post_stats(post_id)) throw ApiError("Post does not exist", 410);
  txn.set_save(user_id, post_id, saved);
}

auto UserController::hide_post(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t post_id,
  bool hidden
) -> void {
  if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
  if (!txn.get_post_stats(post_id)) throw ApiError("Post does not exist", 410);
  txn.set_hide_post(user_id, post_id, hidden);
}

auto UserController::hide_user(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t hidden_user_id,
  bool hidden
) -> void {
  if (!txn.get_local_user(user_id) || !txn.get_user(hidden_user_id)) {
    throw ApiError("User does not exist", 410);
  }
  txn.set_hide_user(user_id, hidden_user_id, hidden);
}

auto UserController::hide_board(
  WriteTxn& txn,
  uint64_t user_id,
  uint64_t board_id,
  bool hidden
) -> void {
  if (!txn.get_local_user(user_id)) throw ApiError("User does not exist", 410);
  if (!txn.get_post_stats(board_id)) throw ApiError("Board does not exist", 410);
  txn.set_hide_board(user_id, board_id, hidden);
}

}