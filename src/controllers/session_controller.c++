#include "session_controller.h++"

using std::generator, std::optional, std::pair, std::runtime_error, std::string,
  std::string_view, flatbuffers::FlatBufferBuilder, fmt::format,
  fmt::operator""_cf; //NOLINT

namespace Ludwig {

auto SessionController::hash_and_salt(SecretString&& password) -> pair<Hash, Salt> {
  Hash hash;
  Salt salt;
  RAND_bytes((uint8_t*)salt.bytes()->Data(), 16);
  UserController::hash_password(std::move(password), salt.bytes()->Data(), (uint8_t*)hash.bytes()->Data());
  return { hash, salt };
}

auto SessionController::validate_or_regenerate_session(
  ReadTxn& txn,
  uint64_t session_id,
  string_view ip,
  string_view user_agent
) -> optional<LoginResponse> {
  using namespace std::chrono;
  const auto session_opt = txn.get_session(session_id);
  if (!session_opt) return {};
  const auto& session = session_opt->get();
  const auto user = session.user();

  // Don't allow logins as the temp admin user after setup is done
  if (!user && site_controller->site_detail()->setup_done) {
    auto txn = db->open_write_txn_sync();
    txn.delete_session(session_id);
    txn.commit();
    return {};
  }

  if (session.remember() && now_t() - uint_to_timestamp(session.created_at()) >= hours(24)) {
    // This is the *one place* that open_write_txn_sync is needed.
    // It's a hack, but there's no way to make this async, and it happens rarely anyway.
    auto txn = db->open_write_txn_sync();
    const auto [id, expiration] = txn.create_session(
      user,
      ip,
      user_agent,
      true,
      session.expires_at() - session.created_at()
    );
    txn.delete_session(session_id);
    txn.commit();
    return { { .user_id = user, .session_id = id, .expiration = uint_to_timestamp(expiration)} };
  }
  return { { .user_id = user, .session_id = session_id, .expiration = uint_to_timestamp(session.expires_at()) } };
}

auto SessionController::login(
  WriteTxn& txn,
  string_view username_or_email,
  SecretString&& password,
  string_view ip,
  string_view user_agent,
  bool remember
) -> LoginResponse {
  using namespace std::chrono;
  uint8_t hash[32];
  const Hash* target_hash;
  const Salt* salt;
  uint64_t user_id = 0;

  const bool is_first_run_admin =
    first_run_admin_password &&
    !site_controller->site_detail()->setup_done &&
    txn.get_admin_list().empty() &&
    username_or_email == FIRST_RUN_ADMIN_USERNAME;
  if (is_first_run_admin) {
    target_hash = &first_run_admin_password->first;
    salt = &first_run_admin_password->second;
  } else {
    const auto user_id_opt = username_or_email.find('@') == string_view::npos
      ? txn.get_user_id_by_name(username_or_email)
      : txn.get_user_id_by_email(username_or_email);
    if (!user_id_opt && !is_first_run_admin) {
      throw ApiError("Invalid username or password", 400,
        format("Tried to log in as nonexistent user {}"_cf, username_or_email)
      );
    }
    user_id = *user_id_opt;
    const auto local_user = txn.get_local_user(user_id);
    if (!local_user) {
      throw ApiError("Invalid username or password", 400,
        format("Tried to log in as non-local user {}"_cf, username_or_email)
      );
    }
    target_hash = local_user->get().password_hash();
    salt = local_user->get().password_salt();
  }

  UserController::hash_password(std::move(password), salt->bytes()->Data(), hash);

  // Note that this returns 0 on success, 1 on failure!
  if (CRYPTO_memcmp(hash, target_hash->bytes()->Data(), 32)) {
    // TODO: Lock users out after repeated failures
    throw ApiError("Invalid username or password", 400,
      format("Tried to login with wrong password for user {}"_cf, username_or_email)
    );
  }
  const auto [session_id, expiration] = txn.create_session(
    user_id,
    ip,
    user_agent,
    remember,
    remember ? duration_cast<seconds>(months{1}).count()
      : duration_cast<seconds>(days{1}).count()
  );
  return { .user_id = user_id, .session_id = session_id, .expiration = uint_to_timestamp(expiration) };
}

auto SessionController::list_applications(
  ReadTxn& txn,
  optional<uint64_t>& cursor,
  Login login
) -> generator<pair<const Application&, const LocalUserDetail&>> {
  if (login && !SiteController::can_change_site_settings(login)) co_return;
  auto iter = txn.list_applications(cursor.transform([](auto x){return Cursor(x);}));
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.emplace(*it);
    try {
      const auto local_user = LocalUserDetail::get(txn, id, login);
      co_yield {txn.get_application(id).value().get(), local_user};
    } catch (const runtime_error& e) {
      spdlog::warn("Application {:x} error: {}", id, e.what());
    }
  }
  cursor.reset();
}

auto SessionController::list_invites_from_user(
  ReadTxn& txn,
  PageCursor& cursor,
  uint64_t user_id
) -> generator<pair<uint64_t, const Invite&>> {
  auto iter = txn.list_invites_from_user(user_id, cursor.next_cursor_desc());
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_0(), *it);
    try {
      co_yield {id, txn.get_invite(id).value().get()};
    } catch (const ApiError& e) {
      spdlog::warn("Invite {:x} error: {}", id, e.what());
    }
  }
  cursor.reset();
}

auto SessionController::list_notifications(
  ReadTxn& txn,
  PageCursor& cursor,
  const LocalUserDetail& login
) -> generator<const NotificationDetail&> {
  auto iter = txn.list_notifications(login.id, cursor.next_cursor_desc(login.id));
  auto it = iter.begin();
  while (it != iter.end()) {
    const auto id = *it;
    if (++it == iter.end()) cursor.reset();
    else cursor.set(iter.get_cursor()->int_field_1(), *it);
    try {
      const auto entry = NotificationDetail::get(txn, id, login);
      co_yield entry;
    } catch (const ApiError& e) {
      spdlog::warn("Notification {:x} error: {}", id, e.what());
    }
  }
  cursor.reset();
}

auto SessionController::register_local_user(
  WriteTxn& txn,
  string_view username,
  string_view email,
  SecretString&& password,
  string_view ip,
  string_view user_agent,
  optional<uint64_t> invite_id,
  optional<string_view> application_text
) -> std::pair<uint64_t, bool> {
  const auto site = site_controller->site_detail();
  if (!site->registration_enabled) {
    throw ApiError("Registration is not allowed on this server", 403);
  }
  if (site->registration_application_required && !application_text) {
    throw ApiError("An application reason is required to register", 400);
  }
  if (site->registration_invite_required) {
    if (!invite_id) {
      throw ApiError("An invite code is required to register", 400);
    }
  }
  const auto user_id = user_controller->create_local_user(
    txn, username, email, std::move(password), false, invite_id, IsApproved::No, IsAdmin::No
  );
  if (invite_id) {
    const auto invite_opt = txn.get_invite(*invite_id);
    if (!invite_opt) {
      spdlog::warn("Invalid invite code: {:X}", *invite_id);
      throw ApiError("Invalid invite code", 400);
    }
    const auto& invite = invite_opt->get();
    if (invite.accepted_at()) {
      spdlog::warn("Attempt to use already-used invite code {} (for username {}, email {}, ip {}, user agent {})",
        invite_id_to_code(*invite_id), username, email, ip, user_agent
      );
      throw ApiError("Expired invite code", 400);
    }
    const auto now = now_s();
    if (invite.expires_at() <= now) throw ApiError("Expired invite code", 400);
    FlatBufferBuilder fbb;
    InviteBuilder b(fbb);
    b.add_from(invite.from());
    b.add_to(user_id);
    b.add_created_at(invite.created_at());
    b.add_accepted_at(now);
    b.add_expires_at(invite.expires_at());
    fbb.Finish(b.Finish());
    txn.set_invite(*invite_id, fbb.GetBufferSpan());
  }
  if (site->registration_application_required) {
    FlatBufferBuilder fbb;
    auto ip_s = fbb.CreateString(ip),
          user_agent_s = fbb.CreateString(user_agent),
          application_text_s = fbb.CreateString(*application_text);
    ApplicationBuilder b(fbb);
    b.add_ip(ip_s);
    b.add_user_agent(user_agent_s);
    b.add_text(application_text_s);
    fbb.Finish(b.Finish());
    txn.create_application(user_id, fbb.GetBufferSpan());
  }
  bool approved = txn.get_user(user_id).value().get().mod_state() < ModState::Unapproved;
  return { user_id, approved };
}

auto SessionController::approve_local_user_application(
  WriteTxn& txn,
  uint64_t user_id,
  optional<uint64_t> as_user
) -> void {
  FlatBufferBuilder fbb;
  if (as_user && !LocalUserDetail::get_login(txn, *as_user).local_user().admin()) {
    throw ApiError("Only admins can approve user applications", 403);
  }
  const auto old_opt = txn.get_local_user(user_id);
  if (!old_opt) throw ApiError(format("User {:x} does not exist"_cf, user_id), 400);
  const auto& old = old_opt->get();
  if (old.accepted_application()) throw ApiError("User's application has already been accepted", 409);
  if (!txn.get_application(user_id)) throw ApiError("User does not have an application to approve", 400);
  fbb.Finish(patch_local_user(fbb, old, { .accepted_application = true }));
  txn.set_local_user(user_id, fbb.GetBufferSpan());
  if (site_controller->site_detail()->registration_application_required) {
    const auto old_opt = txn.get_user(user_id);
    if (!old_opt) throw ApiError(format("User {:x} does not exist"_cf, user_id), 400);
    const auto& old = old_opt->get();
    fbb.Clear();
    // TODO: Check for other approval requirements, like email verification
    fbb.Finish(patch_user(fbb, old, { .mod_state = ModState::Approved }));
    txn.set_user(user_id, fbb.GetBufferSpan());
  }
}

auto SessionController::reject_local_user_application(
  WriteTxn& txn,
  uint64_t user_id,
  std::optional<uint64_t> as_user
) -> void {
  if (as_user && !LocalUserDetail::get_login(txn, *as_user).local_user().admin()) {
    throw ApiError("Only admins can reject user applications", 403);
  }
  const auto user_opt = txn.get_local_user(user_id);
  if (!user_opt) throw ApiError(format("User {:x} does not exist"_cf, user_id), 400);
  const auto& user = user_opt->get();
  if (user.accepted_application()) throw ApiError("User's application has already been accepted", 409);
  if (!txn.get_application(user_id)) throw ApiError("User does not have an application to reject", 400);
  txn.delete_user(user_id);
}

auto SessionController::reset_password(WriteTxn& txn, uint64_t user_id) -> string {
  // TODO: Reset password
  throw ApiError("Reset password is not yet supported", 500);
}

auto SessionController::change_password(
  WriteTxn& txn,
  uint64_t user_id,
  SecretString&& new_password
) -> void {
  const auto user = LocalUserDetail::get_login(txn, user_id);
  FlatBufferBuilder fbb;
  fbb.Finish(patch_local_user(fbb, user.local_user(), { .password = std::move(new_password) }));
  txn.set_local_user(user_id, fbb.GetBufferSpan());
}

auto SessionController::change_password(
  WriteTxn& txn,
  string_view reset_token,
  SecretString&& new_password
) -> string {
  // TODO: Reset password
  throw ApiError("Reset password is not yet supported", 500);
}

auto SessionController::change_password(
  WriteTxn& txn,
  uint64_t user_id,
  SecretString&& old_password,
  SecretString&& new_password
) -> void {
  const auto user = LocalUserDetail::get_login(txn, user_id);
  uint8_t hash[32];
  UserController::hash_password(std::move(old_password), user.local_user().password_salt()->bytes()->Data(), hash);
  // Note that this returns 0 on success, 1 on failure!
  if (CRYPTO_memcmp(hash, user.local_user().password_hash()->bytes()->Data(), 32)) {
    throw ApiError("Old password incorrect", 400);
  }
  FlatBufferBuilder fbb;
  fbb.Finish(patch_local_user(fbb, user.local_user(), { .password = std::move(new_password) }));
  txn.set_local_user(user_id, fbb.GetBufferSpan());
}

auto SessionController::create_site_invite(WriteTxn& txn, optional<uint64_t> as_user) -> uint64_t {
  using namespace std::chrono;
  if (const auto user = LocalUserDetail::get_login(txn, as_user)) {
    if (site_controller->site_detail()->invite_admin_only && !user->local_user().admin()) {
      throw ApiError("Only admins can create invite codes", 403);
    }
    if (user->mod_state().state >= ModState::Locked) {
      throw ApiError("User does not have permission to create invite codes", 403);
    }
  }
  return txn.create_invite(as_user.value_or(0), duration_cast<seconds>(weeks{1}).count());
}

auto SessionController::mark_notification_read(WriteTxn& txn, uint64_t user_id, uint64_t notification_id) -> void {
  if (!txn.get_local_user(user_id)) throw ApiError(format("User {:x} does not exist"_cf, user_id), 410);
  txn.mark_notification_read(user_id, notification_id);
}

auto SessionController::mark_all_notifications_read(WriteTxn& txn, uint64_t user_id) -> void {
  if (!txn.get_local_user(user_id)) throw ApiError(format("User {:x} does not exist"_cf, user_id), 410);
  std::vector<uint64_t> unread;
  for (uint64_t i : txn.list_unread_notifications(user_id)) unread.push_back(i);
  for (uint64_t i : unread) txn.mark_notification_read(user_id, i);
}

}