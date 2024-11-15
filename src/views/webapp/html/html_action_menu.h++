#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "models/local_user.h++"
#include "models/enums.h++"
#include "controllers/user_controller.h++"

namespace Ludwig {

enum class SubmenuAction : uint8_t {
  None,
  Reply,
  Edit,
  Delete,
  Share,
  Save,
  Unsave,
  Hide,
  Unhide,
  Report,
  MuteUser,
  UnmuteUser,
  MuteBoard,
  UnmuteBoard,
  ModRestore,
  ModApprove,
  ModFlag,
  ModLock,
  ModRemove,
  ModRemoveUser,
  AdminRestore,
  AdminApprove,
  AdminFlag,
  AdminLock,
  AdminRemove,
  AdminRemoveUser,
  AdminPurge,
  AdminPurgeUser
};

static inline auto format_as(SubmenuAction a) { return fmt::underlying(a); };

static inline auto admin_submenu(ModState state) noexcept ->
  std::tuple<SubmenuAction, std::string_view, SubmenuAction, std::string_view, SubmenuAction, std::string_view> {
  using enum ModState;
  using enum SubmenuAction;
  switch (state) {
    case Normal: return {AdminFlag, "🚩 Flag", AdminLock, "🔒 Lock", AdminRemove, "✂️ Remove"};
    case Flagged: return {AdminRestore, "🏳️ Unflag", AdminLock, "🔒 Lock", AdminRemove, "✂️ Remove"};
    case Locked: return {AdminRestore, "🔓 Unlock", AdminFlag, "🚩 Unlock and Flag", AdminRemove, "✂️ Remove"};
    case Unapproved: return {AdminApprove, "✔️ Approve", AdminFlag, "🚩 Approve and Flag", AdminRemove, "❌ Reject"};
    default: return {AdminRestore, "♻️ Restore", AdminFlag, "🚩 Restore and Flag", AdminLock, "🔒 Restore and Lock"};
  }
}

template <class T>
void html_action_menu(ResponseWriter& r, const T& post, Login login, PostContext context) noexcept {
  using fmt::operator""_cf;
  using enum SubmenuAction;
  if (!login) return;
  r.write_fmt(
    R"(<form class="controls-submenu" id="controls-submenu-{0:x}" method="post" action="/{1}/{0:x}/action">)"
    R"(<input type="hidden" name="context" value="{2:d}">)"
    R"(<label for="action"><span class="a11y">Action</span>)" ICON("chevron-down")
    R"(<select name="action" autocomplete="off" hx-post="/{1}/{0:x}/action" hx-trigger="change" hx-target="#controls-submenu-{0:x}">)"
    R"(<option selected hidden value="{3:d}">Actions)"_cf,
    post.id, T::noun, static_cast<unsigned>(context), None
  );
  if (context != PostContext::View && post.can_reply_to(login)) {
    r.write_fmt(R"(<option value="{:d}">💬 Reply)"_cf, Reply);
  }
  if (post.can_edit(login)) {
    r.write_fmt(R"(<option value="{:d}">✏️ Edit)"_cf, Edit);
  }
  if (post.can_delete(login)) {
    r.write_fmt(R"(<option value="{:d}">🗑️ Delete)"_cf, Delete);
  }
  r.write_fmt(
    R"(<option value="{:d}">{})"
    R"(<option value="{:d}">{})"_cf,
    post.saved ? Unsave : Save, post.saved ? "🚫 Unsave" : "🔖 Save",
    post.hidden ? Unhide : Hide, post.hidden ? "🔈 Unhide" : "🔇 Hide"
  );
  if (context != PostContext::User) {
    r.write_fmt(R"(<option value="{:d}">{})"_cf,
      post.user_hidden ? UnmuteUser : MuteUser,
      post.user_hidden ? "🔈 Unmute user" : "🔇 Mute user"
    );
  }
  if (context != PostContext::Board) {
    r.write_fmt(R"(<option value="{:d}">{})"_cf,
      post.board_hidden ? UnmuteBoard : MuteBoard,
      post.board_hidden ? "🔈 Unhide board" : "🔇 Hide board"
    );
  }
  if (login->local_user().admin()) {
    // FIXME: This is not the right mod_state, will do weird things if
    // user or board has a mod_state > Normal
    const auto [a1, b1, a2, b2, a3, b3] = admin_submenu(post.mod_state().state);
    r.write_fmt(
      R"(<optgroup label="Admin">)"
      R"(<option value="{:d}">{})"
      R"(<option value="{:d}">{})"
      R"(<option value="{:d}">{})"
      R"(<option value="{:d}">🔨 Ban user)"
      R"(<option value="{:d}">☣️ Purge {})"
      R"(<option value="{:d}">☣️ Purge user)"
      "</optgroup>"_cf,
      a1, b1, a2, b2, a3, b3,
      AdminRemoveUser,
      AdminPurge, T::noun,
      AdminPurgeUser
    );
  }
  r.write(R"(</select></label><button class="no-js" type="submit">Apply</button></form>)");
}

template <class T>
auto action_menu_action(
  WriteTxn& txn,
  const std::shared_ptr<UserController>& users,
  SubmenuAction action,
  uint64_t user,
  uint64_t id
) -> std::optional<std::string> {
  using fmt::operator""_cf;
  using enum SubmenuAction;
  switch (action) {
    case Reply:
      return fmt::format("/{}/{:x}#reply"_cf, T::noun, id);
    case Edit:
      return fmt::format("/{}/{:x}/edit"_cf, T::noun, id);
    case Delete:
      // TODO: Delete
      die(500, "Delete is not yet implemented");
    case Share:
      die(500, "Share is not yet implemented");
    case Save:
      users->save_post(txn, user, id, true);
      return {};
    case Unsave:
      users->save_post(txn, user, id, false);
      return {};
    case Hide:
      users->hide_post(txn, user, id, true);
      return {};
    case Unhide:
      users->hide_post(txn, user, id, false);
      return {};
    case Report:
      // TODO: Report
      die(500, "Report is not yet implemented");
    case MuteUser: {
      auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
      users->hide_user(txn, user, e.author_id(), true);
      return {};
    }
    case UnmuteUser: {
      auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
      users->hide_user(txn, user, e.author_id(), false);
      return {};
    }
    case MuteBoard: {
      auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
      users->hide_board(txn, user, e.thread().board(), true);
      return {};
    }
    case UnmuteBoard:{
      auto e = T::get(txn, id, LocalUserDetail::get_login(txn, id));
      users->hide_board(txn, user, e.thread().board(), false);
      return {};
    }
    case ModRestore:
    case ModApprove:
    case ModFlag:
    case ModLock:
    case ModRemove:
    case ModRemoveUser:
      // TODO: Mod actions
      die(500, "Mod actions are not yet implemented");
    case AdminRestore:
    case AdminApprove:
    case AdminFlag:
    case AdminLock:
    case AdminRemove:
    case AdminRemoveUser:
    case AdminPurge:
    case AdminPurgeUser:
      // TODO: Admin actions
      die(500, "Admin actions are not yet implemented");
    case None:
      die(400, "No action selected");
  }
  throw ApiError("Invalid action", 400, fmt::format("Unrecognized SubmenuAction: {:d}"_cf, action));
}

}