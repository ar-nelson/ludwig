#pragma once
#include "db/db.h++"
#include "models/notification.h++"
#include "user_controller.h++"
#include <parallel_hashmap/phmap.h>

namespace Ludwig {

struct LoginResponse {
  uint64_t user_id, session_id;
  Timestamp expiration;
};


class SessionController {
private:
  std::shared_ptr<DB> db;
  std::shared_ptr<SiteController> site_controller;
  std::shared_ptr<UserController> user_controller;
  std::optional<std::pair<Hash, Salt>> first_run_admin_password;
  phmap::flat_hash_map<uint64_t, std::pair<uint64_t, Timestamp>> password_reset_tokens;
  std::mutex password_reset_tokens_mutex;

  static auto hash_and_salt(SecretString&& password) -> std::pair<Hash, Salt>;

public:
  SessionController(
    std::shared_ptr<DB> db,
    std::shared_ptr<SiteController> site,
    std::shared_ptr<UserController> user,
    std::optional<SecretString> first_run_admin_password = {}
  ) : db(db),
      site_controller(site),
      user_controller(user),
      first_run_admin_password(first_run_admin_password
        ? std::optional(hash_and_salt(std::move(*first_run_admin_password)))
        : std::nullopt) {
    assert(db != nullptr);
    assert(site != nullptr);
    assert(user != nullptr);
  }

  auto validate_session(ReadTxn& txn, uint64_t session_id) -> std::optional<uint64_t> {
    auto session = txn.get_session(session_id);
    if (session) return { session->get().user() };
    return {};
  }
  auto validate_or_regenerate_session(
    ReadTxn& txn,
    uint64_t session_id,
    std::string_view ip,
    std::string_view user_agent
  ) -> std::optional<LoginResponse>;
  auto delete_session(WriteTxn& txn, uint64_t session_id) -> void {
    txn.delete_session(session_id);
  }

  auto login(
    WriteTxn& txn,
    std::string_view username,
    SecretString&& password,
    std::string_view ip,
    std::string_view user_agent,
    bool remember = false
  ) -> LoginResponse;
  auto register_local_user(
    WriteTxn& txn,
    std::string_view username,
    std::string_view email,
    SecretString&& password,
    std::string_view ip,
    std::string_view user_agent,
    std::optional<uint64_t> invite = {},
    std::optional<std::string_view> application_text = {}
  ) -> std::pair<uint64_t, bool>;

  auto list_applications(
    ReadTxn& txn,
    std::optional<uint64_t>& cursor,
    Login login = {}
  ) -> std::generator<std::pair<const Application&, const LocalUserDetail&>>;
  auto approve_local_user_application(
    WriteTxn& txn,
    uint64_t user_id,
    std::optional<uint64_t> as_user
  ) -> void;
  auto reject_local_user_application(
    WriteTxn& txn,
    uint64_t user_id,
    std::optional<uint64_t> as_user
  ) -> void;

  auto list_invites_from_user(
    ReadTxn& txn,
    PageCursor& cursor,
    uint64_t user_id
  ) -> std::generator<std::pair<uint64_t, const Invite&>>;
  auto create_site_invite(WriteTxn& txn, std::optional<uint64_t> as_user) -> uint64_t;

  auto reset_password(WriteTxn& txn, uint64_t user_id) -> std::string;
  auto change_password(WriteTxn& txn, uint64_t user_id, SecretString&& new_password) -> void;
  auto change_password(
    WriteTxn& txn,
    std::string_view reset_token,
    SecretString&& new_password
  ) -> std::string; // returns username
  auto change_password(
    WriteTxn& txn,
    uint64_t user_id,
    SecretString&& old_password,
    SecretString&& new_password
  ) -> void;

  auto list_notifications(
    ReadTxn& txn,
    PageCursor& from,
    const LocalUserDetail& login
  ) -> std::generator<const NotificationDetail&>;
  auto mark_notification_read(WriteTxn& txn, uint64_t user_id, uint64_t notification_id) -> void;
  auto mark_all_notifications_read(WriteTxn& txn, uint64_t user_id) -> void;
};

}