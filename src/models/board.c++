#include "board.h++"
#include "local_user.h++"
#include "util/rich_text.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset;

namespace Ludwig {

auto BoardDetail::can_view(Login login) const noexcept -> bool {
  if (mod_state().state >= ModState::Unapproved) {
    if (!login || !login->local_user().admin()) return false;
  }
  // TODO: Handle private boards
  return true;
}

auto BoardDetail::should_show(Login login) const noexcept -> bool {
  if (hidden || !can_view(login)) return false;
  if (login) {
    if (board().content_warning() && login->local_user().hide_cw_posts()) return false;
  }
  return true;
}

auto BoardDetail::can_create_thread(Login login) const noexcept -> bool {
  if (!login || login->mod_state(id).state >= ModState::Locked) return false;
  return !board().restricted_posting() || login->local_user().admin();
}

auto BoardDetail::should_show_votes(Login, const SiteDetail* site) const noexcept -> bool {
  return site->votes_enabled && board().can_upvote();
}

auto BoardDetail::can_change_settings(Login login) const noexcept -> bool {
  return maybe_local_board() && login && (login->local_user().admin() || login->id == maybe_local_board()->get().owner());
}

auto BoardDetail::get(ReadTxn& txn, uint64_t id, Login login) -> BoardDetail {
  const auto board = txn.get_board(id);
  const auto board_stats = txn.get_board_stats(id);
  if (!board || !board_stats) throw ApiError("Board does not exist", 410);
  const auto local_board = txn.get_local_board(id);
  const auto hidden = login && txn.has_user_hidden_board(login->id, id);
  const auto subscribed = login && txn.is_user_subscribed_to_board(login->id, id);
  return { id, *board, local_board, *board_stats, hidden, subscribed };
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

}