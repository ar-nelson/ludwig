#pragma once
#include "util/common.h++"
#include "db/db.h++"
#include "fbs/records.h++"
#include "models/user.h++"
#include "models/null_placeholders.h++"

namespace Ludwig {

struct LocalUserDetail : UserDetail {

  static auto temp_admin() -> LocalUserDetail {
    return {
      { 0, *placeholders.temp_admin_user, std::reference_wrapper(*placeholders.temp_admin_local_user), *placeholders.temp_admin_stats, false },
      *placeholders.temp_admin_local_stats
    };
  }

  std::reference_wrapper<const LocalUserStats> _local_user_stats;

  auto local_user() const -> const LocalUser& { return _local_user->get(); }
  auto local_user_stats() const noexcept -> const LocalUserStats& { return _local_user_stats.get(); }

  static auto get(ReadTxn& txn, uint64_t id, Login login) -> LocalUserDetail;
  static auto get_login(ReadTxn& txn, uint64_t id) -> LocalUserDetail;
  static auto get_login(ReadTxn& txn, std::optional<uint64_t> id) -> std::optional<LocalUserDetail> {
    return id.transform([&](auto id){return get_login(txn, id);});
  }
};

struct LocalUserPatch {
  std::optional<std::optional<std::string_view>> email, lemmy_theme;
  std::optional<SecretString> password;
  std::optional<bool> admin, accepted_application, email_verified,
    open_links_in_new_tab, send_notifications_to_email, show_avatars,
    show_images_threads, show_images_comments, show_bot_accounts,
    show_new_post_notifs, hide_cw_posts, expand_cw_posts, expand_cw_images,
    show_read_posts, show_karma, javascript_enabled,
    infinite_scroll_enabled;
  std::optional<uint64_t> invite, theme;
  std::optional<SortType> default_sort_type;
  std::optional<CommentSortType> default_comment_sort_type;
};

auto patch_local_user(
  flatbuffers::FlatBufferBuilder& fbb,
  const LocalUser& old,
  LocalUserPatch&& patch
) -> flatbuffers::Offset<LocalUser>;

}