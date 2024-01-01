#pragma once
#include "util/rich_text.h++"
#include "models/db.h++"

namespace Ludwig {
  struct UserPatch {
    std::optional<std::optional<std::string_view>>
      display_name, bio, matrix_user_id, avatar_url, banner_url, mod_reason;
    std::optional<uint64_t> updated_at, fetched_at, deleted_at;
    std::optional<bool> bot;
    std::optional<ModState> mod_state;
  };

  struct LocalUserPatch {
    std::optional<std::optional<std::string_view>> email, lemmy_theme;
    std::optional<SecretString> password;
    std::optional<bool>
      admin, approved, accepted_application, email_verified,
      open_links_in_new_tab, send_notifications_to_email, show_avatars,
      show_images_threads, show_images_comments, show_bot_accounts,
      show_new_post_notifs, hide_cw_posts, expand_cw_posts, expand_cw_images,
      show_read_posts, show_karma, javascript_enabled, infinite_scroll_enabled;
    std::optional<uint64_t> invite, theme;
    std::optional<SortType> default_sort_type;
    std::optional<CommentSortType> default_comment_sort_type;
  };

  struct BoardPatch {
    std::optional<std::optional<std::string_view>>
      display_name, description, icon_url, banner_url, content_warning, mod_reason;
    std::optional<uint64_t> updated_at, fetched_at, deleted_at;
    std::optional<bool>
      restricted_posting, approve_subscribe, can_upvote, can_downvote;
    std::optional<SortType> default_sort_type;
    std::optional<CommentSortType> default_comment_sort_type;
    std::optional<ModState> mod_state;
  };

  struct LocalBoardPatch {
    // TODO: Allow changing owner?
    std::optional<bool> federated, private_, invite_required, invite_mod_only;
  };

  struct ThreadPatch {
    // TODO: Allow moving between boards?
    std::optional<std::string_view> title;
    std::optional<std::optional<std::string_view>>
      content_url, content_text, content_warning, mod_reason;
    std::optional<uint64_t> updated_at, fetched_at, deleted_at;
    std::optional<bool> featured;
    std::optional<ModState> mod_state;
  };

  struct CommentPatch {
    // TODO: Allow moving between threads?
    std::optional<std::string_view> content;
    std::optional<std::optional<std::string_view>> content_warning, mod_reason;
    std::optional<uint64_t> updated_at, fetched_at, deleted_at;
    std::optional<ModState> mod_state;
  };

  auto patch_user(
    flatbuffers::FlatBufferBuilder& fbb,
    RichTextParser& rt,
    const User& old,
    const UserPatch& patch
  ) -> flatbuffers::Offset<User>;

  auto patch_local_user(
    flatbuffers::FlatBufferBuilder& fbb,
    const LocalUser& old,
    LocalUserPatch&& patch
  ) -> flatbuffers::Offset<LocalUser>;

  auto patch_board(
    flatbuffers::FlatBufferBuilder& fbb,
    RichTextParser& rt,
    const Board& old,
    const BoardPatch& patch
  ) -> flatbuffers::Offset<Board>;

  auto patch_local_board(
    flatbuffers::FlatBufferBuilder& fbb,
    const LocalBoard& old,
    const LocalBoardPatch& patch
  ) -> flatbuffers::Offset<LocalBoard>;

  auto patch_thread(
    flatbuffers::FlatBufferBuilder& fbb,
    RichTextParser& rt,
    const Thread& old,
    const ThreadPatch& patch
  ) -> flatbuffers::Offset<Thread>;

  auto patch_comment(
    flatbuffers::FlatBufferBuilder& fbb,
    RichTextParser& rt,
    const Comment& old,
    const CommentPatch& patch
  ) -> flatbuffers::Offset<Comment>;
}
