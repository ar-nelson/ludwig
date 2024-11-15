#pragma once
#include "db/db.h++"
#include "db/page_cursor.h++"
#include "models/local_user.h++"
#include "services/event_bus.h++"
#include "controllers/site_controller.h++"

namespace Ludwig {

enum class IsApproved : bool { No, Yes };
enum class IsAdmin : bool { No, Yes };

struct LocalUserUpdate {
  std::optional<std::string_view> email;
  std::optional<std::optional<std::string_view>> display_name, bio,
      avatar_url, banner_url;
  std::optional<bool> bot, open_links_in_new_tab, show_avatars,
      show_bot_accounts, show_karma, hide_cw_posts, expand_cw_images,
      expand_cw_posts, javascript_enabled, infinite_scroll_enabled;
  std::optional<IsAdmin> admin;
  std::optional<SortType> default_sort_type;
  std::optional<CommentSortType> default_comment_sort_type;
};

class UserController {
private:
  std::shared_ptr<SiteController> site_controller;
  std::shared_ptr<EventBus> event_bus;

public:
  UserController(
    std::shared_ptr<SiteController> site,
    std::shared_ptr<EventBus> event_bus = std::make_shared<DummyEventBus>()
  ) : site_controller(site), event_bus(event_bus) {}

  static auto hash_password(SecretString&& password, const uint8_t salt[16], uint8_t hash[32]) -> void;

  auto user_detail(ReadTxn& txn, uint64_t id, Login login) -> UserDetail;
  auto local_user_detail(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail;
  auto list_users(
    ReadTxn& txn,
    PageCursor& cursor,
    UserSortType sort,
    bool local_only,
    Login login = {}
  ) -> std::generator<const UserDetail&>;
  auto create_local_user(
    WriteTxn& txn,
    std::string_view username,
    std::optional<std::string_view> email,
    SecretString&& password,
    bool is_bot,
    std::optional<uint64_t> invite = {},
    IsApproved is_approved = IsApproved::No,
    IsAdmin is_admin = IsAdmin::No
  ) -> uint64_t;
  auto update_local_user(
    WriteTxn& txn,
    uint64_t id,
    std::optional<uint64_t> as_user,
    const LocalUserUpdate& update
  ) -> void;
  auto hide_user(
    WriteTxn& txn,
    uint64_t user_id,
    uint64_t hidden_user_id,
    bool hidden = true
  ) -> void;
  auto hide_board(
    WriteTxn& txn,
    uint64_t user_id,
    uint64_t board_id,
    bool hidden = true
  ) -> void;
  auto save_post(WriteTxn& txn, uint64_t user_id, uint64_t post_id, bool saved = true) -> void;
  auto hide_post(WriteTxn& txn, uint64_t user_id, uint64_t post_id, bool hidden = true) -> void;
};

}