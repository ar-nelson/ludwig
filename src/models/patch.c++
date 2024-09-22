#include "patch.h++"
#include <openssl/rand.h>
#include "controllers/instance.h++"
#include "util/lambda_macros.h++"
#include "util/rich_text.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset, flatbuffers::String,
    flatbuffers::Vector, std::nullopt, std::optional, std::pair, std::string,
    std::string_view, std::tuple;

namespace Ludwig {

  static inline auto update_opt_str(
    FlatBufferBuilder& fbb,
    optional<optional<string_view>> updated,
    const String* existing
  ) -> Offset<String> {
    if (!updated) return fbb.CreateString(existing);
    else if (!*updated) return 0;
    else return fbb.CreateString(**updated);
  }

  static inline auto update_rich_text_emojis_only(
    FlatBufferBuilder& fbb,
    optional<optional<string_view>> updated,
    const Vector<RichText>* types,
    const Vector<Offset<void>>* values
  ) {
    return updated
      .transform([](optional<string_view> sv) { return sv.transform(λx(string(x))); })
      .value_or(types && types->size()
        ? optional(rich_text_to_plain_text(types, values))
        : nullopt)
      .transform([&](string s) { return plain_text_with_emojis_to_rich_text(fbb, s); })
      .value_or(pair(0, 0));
  }

  static inline auto update_rich_text(
    FlatBufferBuilder& fbb,
    optional<optional<string_view>> updated,
    const String* existing_raw
  ) {
    return updated
      .value_or(existing_raw ? optional(existing_raw->string_view()) : nullopt)
      .transform([&](string_view s) {
        const auto [types, values] = markdown_to_rich_text(fbb, s);
        return tuple(fbb.CreateString(s), types, values);
      })
      .value_or(tuple(0, 0, 0));
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

  auto patch_local_user(FlatBufferBuilder& fbb, const LocalUser& old, LocalUserPatch&& patch) -> Offset<LocalUser> {
    const auto email = update_opt_str(fbb, patch.email, old.email()),
      lemmy_theme = update_opt_str(fbb, patch.lemmy_theme, old.lemmy_theme());
    LocalUserBuilder b(fbb);
    b.add_email(email);
    if (patch.password) {
      Salt salt;
      Hash hash;
      if (!RAND_bytes((uint8_t*)salt.bytes()->Data(), salt.bytes()->size())) {
        throw ApiError("Not enough randomness to generate secure password salt", 500);
      }
      InstanceController::hash_password(std::move(*patch.password), salt.bytes()->Data(), (uint8_t*)hash.bytes()->Data());
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

  auto patch_board(FlatBufferBuilder& fbb, const Board& old, const BoardPatch& patch) -> Offset<Board> {
    const auto name = fbb.CreateString(old.name()),
      actor_id = fbb.CreateString(old.actor_id()),
      inbox_url = fbb.CreateString(old.inbox_url()),
      followers_url = fbb.CreateString(old.followers_url()),
      icon_url = update_opt_str(fbb, patch.icon_url, old.icon_url()),
      banner_url = update_opt_str(fbb, patch.banner_url, old.banner_url()),
      content_warning = update_opt_str(fbb, patch.content_warning, old.content_warning()),
      mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason());
    const auto [display_name_type, display_name] =
      update_rich_text_emojis_only(fbb, patch.display_name, old.display_name_type(), old.display_name());
    const auto [description_raw, description_type, description] =
      update_rich_text(fbb, patch.description, old.description_raw());
    BoardBuilder b(fbb);
    b.add_name(name);
    b.add_display_name_type(display_name_type);
    b.add_display_name(display_name);
    b.add_actor_id(actor_id);
    b.add_inbox_url(inbox_url);
    b.add_followers_url(followers_url);
    b.add_instance(old.instance());
    b.add_created_at(old.created_at());
    if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
    if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
    if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
    b.add_description_raw(description_raw);
    b.add_description_type(description_type);
    b.add_description(description);
    b.add_icon_url(icon_url);
    b.add_banner_url(banner_url);
    b.add_content_warning(content_warning);
    b.add_restricted_posting(patch.restricted_posting.value_or(old.restricted_posting()));
    b.add_approve_subscribe(patch.approve_subscribe.value_or(old.approve_subscribe()));
    b.add_can_upvote(patch.can_upvote.value_or(old.can_upvote()));
    b.add_can_downvote(patch.can_downvote.value_or(old.can_downvote()));
    b.add_default_sort_type(patch.default_sort_type.value_or(old.default_sort_type()));
    b.add_default_comment_sort_type(patch.default_comment_sort_type.value_or(old.default_comment_sort_type()));
    b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
    b.add_mod_reason(mod_reason);
    return b.Finish();
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

  auto patch_thread(FlatBufferBuilder& fbb, const Thread& old, const ThreadPatch& patch) -> Offset<Thread> {
    const auto activity_url = fbb.CreateString(old.activity_url()),
      original_post_url = fbb.CreateString(old.original_post_url()),
      content_url = update_opt_str(fbb, patch.content_url, old.content_url()),
      content_warning = update_opt_str(fbb, patch.content_warning, old.content_warning()),
      mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason()),
      board_mod_reason = update_opt_str(fbb, patch.board_mod_reason, old.board_mod_reason());
    const auto [title_type, title] =
      update_rich_text_emojis_only(fbb, patch.title, old.title_type(), old.title());
    const auto [content_text_raw, content_text_type, content_text] =
      update_rich_text(fbb, patch.content_text, old.content_text_raw());
    ThreadBuilder b(fbb);
    b.add_author(old.author());
    b.add_board(old.board());
    b.add_title_type(title_type);
    b.add_title(title);
    b.add_created_at(old.created_at());
    if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
    if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
    if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
    b.add_instance(old.instance());
    b.add_activity_url(activity_url);
    b.add_original_post_url(original_post_url);
    b.add_content_url(content_url);
    b.add_content_text_raw(content_text_raw);
    b.add_content_text_type(content_text_type);
    b.add_content_text(content_text);
    b.add_content_warning(content_warning);
    b.add_featured(patch.featured.value_or(old.featured()));
    b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
    b.add_mod_reason(mod_reason);
    b.add_board_mod_state(patch.board_mod_state.value_or(old.board_mod_state()));
    b.add_board_mod_reason(board_mod_reason);
    return b.Finish();
  }

  auto patch_comment(FlatBufferBuilder& fbb, const Comment& old, const CommentPatch& patch) -> Offset<Comment> {
    const auto activity_url = fbb.CreateString(old.activity_url()),
      original_post_url = fbb.CreateString(old.original_post_url()),
      content_warning = update_opt_str(fbb, patch.content_warning, old.content_warning()),
      mod_reason = update_opt_str(fbb, patch.mod_reason, old.mod_reason()),
      board_mod_reason = update_opt_str(fbb, patch.board_mod_reason, old.board_mod_reason());
    const auto [content_raw, content_type, content] =
      update_rich_text(fbb, patch.content, old.content_raw());
    CommentBuilder b(fbb);
    b.add_author(old.author());
    b.add_parent(old.parent());
    b.add_thread(old.thread());
    b.add_created_at(old.created_at());
    if (auto t = patch.updated_at ? patch.updated_at : old.updated_at()) b.add_updated_at(*t);
    if (auto t = patch.fetched_at ? patch.fetched_at : old.fetched_at()) b.add_fetched_at(*t);
    if (auto t = patch.deleted_at ? patch.deleted_at : old.deleted_at()) b.add_deleted_at(*t);
    b.add_instance(old.instance());
    b.add_activity_url(activity_url);
    b.add_original_post_url(original_post_url);
    b.add_content_raw(content_raw);
    b.add_content_type(content_type);
    b.add_content(content);
    b.add_content_warning(content_warning);
    b.add_mod_state(patch.mod_state.value_or(old.mod_state()));
    b.add_mod_reason(mod_reason);
    b.add_board_mod_state(patch.board_mod_state.value_or(old.board_mod_state()));
    b.add_board_mod_reason(board_mod_reason);
    return b.Finish();
  }
}
