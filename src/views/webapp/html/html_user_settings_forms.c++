#include "html_user_settings_forms.h++"
#include "html_form_widgets.h++"
#include "html_post_widgets.h++"
#include "util/rich_text.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_user_settings_tabs(ResponseWriter& r, const SiteDetail* site, UserSettingsTab selected) noexcept {
  using enum UserSettingsTab; 
  r.write(R"(<ul class="tabs">)");
  html_tab(r, Settings, selected, "Settings", "/settings");
  html_tab(r, Profile, selected, "Profile", "/settings/profile");
  html_tab(r, Account, selected, "Account", "/settings/account");
  if (site->registration_invite_required && !site->invite_admin_only) {
    html_tab(r, Invites, selected, "Invites", "/settings/invites");
  }
  r.write("</ul>");
}

void html_user_settings_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  int cw_mode = 1;
  const auto& u = login.local_user();
  if (u.hide_cw_posts()) cw_mode = 0;
  else if (u.expand_cw_images()) cw_mode = 3;
  else if (u.expand_cw_posts()) cw_mode = 2;
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action="/settings"><h2>User settings</h2>{})"
    R"(<fieldset><legend>Sorting</legend>)"
    R"(<label for="default_sort_type"><span>Default sort</span>)"_cf,
    error_banner(error)
  );
  html_sort_select(r, "default_sort_type", u.default_sort_type());
  r.write(R"(</label><label for="default_comment_sort_type"><span>Default comment sort</span>)");
  html_sort_select(r, "default_comment_sort_type", u.default_comment_sort_type());
  r.write_fmt(
    R"(</label></fieldset><fieldset><legend>Show/Hide</legend>)"
    HTML_CHECKBOX("show_avatars", "Show avatars", "{}")
    ""_cf,
    check(u.show_avatars())
  );
  if (site->votes_enabled) {
    r.write_fmt(HTML_CHECKBOX("show_karma", "Show karma (score)", "{}") ""_cf, check(u.show_karma()));
  }
  r.write_fmt(
    HTML_CHECKBOX("show_images_threads", "Show images on threads by default", "{}")
    HTML_CHECKBOX("show_images_comments", "Show inline images in comments by default", "{}")
    HTML_CHECKBOX("show_bot_accounts", "Show bot accounts", "{}")
    HTML_CHECKBOX("show_new_post_notifs", "Show new post notifications", "{}")
    HTML_CHECKBOX("show_read_posts", "Show read posts", "{}")
    ""_cf,
    check(u.show_images_threads()),
    check(u.show_images_comments()),
    check(u.show_bot_accounts()),
    check(u.show_new_post_notifs()),
    check(u.show_read_posts())
  );
  if (site->cws_enabled) {
    r.write_fmt(
      R"(<label><span>Content warnings</span><select name="content_warnings" autocomplete="off">)"
        R"(<option value="0"{}> Hide posts with content warnings completely)"
        R"(<option value="1"{}> Collapse posts with content warnings (default))"
        R"(<option value="2"{}> Expand text content of posts with content warnings but hide images)"
        R"(<option value="3"{}> Always expand text and images with content warnings)"
      R"(</select></label>)"_cf,
      select(cw_mode, 0), select(cw_mode, 1), select(cw_mode, 2), select(cw_mode, 3)
    );
  }
  r.write_fmt(
    R"(</fieldset><fieldset><legend>Misc</legend>)"
    HTML_CHECKBOX("open_links_in_new_tab", "Open links in new tab", "{}")
    HTML_CHECKBOX("send_notifications_to_email", "Send notifications to email", "{}")
    ""_cf,
    check(u.open_links_in_new_tab()),
    check(u.send_notifications_to_email())
  );
  if (site->javascript_enabled) {
    r.write_fmt(HTML_CHECKBOX("javascript_enabled", "JavaScript enabled", "{}") ""_cf, check(u.javascript_enabled()));
  }
  if (site->infinite_scroll_enabled) {
    r.write_fmt(HTML_CHECKBOX("infinite_scroll_enabled", "Infinite scroll enabled", "{}") ""_cf, check(u.infinite_scroll_enabled()));
  }
  r.write(R"(</fieldset><input type="submit" value="Submit"></form>)");
}

void html_user_settings_profile_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action="profile"><h2>Profile</h2>{})"
    R"(<label for="name"><span>Username</span><output name="name" id="name">{}</output></label>)"
    HTML_FIELD("display_name", "Display name", "text", R"( value="{}")")
    HTML_FIELD("email", "Email address", "email", R"( required value="{}")")
    HTML_TEXTAREA("bio", "Bio", "", "{}")
    HTML_FIELD("avatar_url", "Avatar URL", "text", R"( value="{}")")
    HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}")")
    R"(<input type="submit" value="Submit"></form>)"_cf,
    error_banner(error),
    Escape(login.user().name()),
    Escape(rich_text_to_plain_text(login.user().display_name_type(), login.user().display_name())),
    Escape(login.local_user().email()),
    Escape(login.user().bio_raw()),
    Escape(login.user().avatar_url()),
    Escape(login.user().banner_url())
  );
}

void html_user_settings_account_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action="account/change_password"><h2>Change password</h2>{})"
    HTML_FIELD("old_password", "Old password", "password", R"( required autocomplete="off")")
    HTML_FIELD("password", "New password", "password", R"( required autocomplete="off")")
    HTML_FIELD("confirm_password", "Confirm new password", "password", R"( required autocomplete="off")")
    R"(<input type="submit" value="Submit"></form><br>)"
    R"(<form data-component="Form" class="form form-page" method="post" action="account/delete"><h2>Delete account</h2>)"
    R"(<p>⚠️ <strong>Warning: This cannot be undone!</strong> ⚠️</p>)"
    HTML_FIELD("delete_password", "Type your password here", "password", R"( required autocomplete="off")")
    HTML_FIELD("delete_confirm", R"(Type "delete" here to confirm)", "text", R"( required autocomplete="off")")
    HTML_CHECKBOX("delete_posts", "Also delete all of my posts", R"( autocomplete="off")")
    R"(<input type="submit" value="Delete Account"></form>)"_cf,
    error_banner(error)
  );
}

void html_invites_list(
  ResponseWriter& r,
  SessionController& sessions,
  ReadTxn& txn,
  const LocalUserDetail& login,
  string_view cursor_str,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<div class="table-page"><h2>Invite Codes</h2>{})"
    R"(<form action="invites/new" method="post"><input type="submit" value="Generate New Invite Code"></form><table>)"
    R"(<thead><th>Code<th>Created<th>Expires<th>Accepted<th>Acceptor</thead>)"
    R"(<tbody id="invite-table">)"_cf,
    error_banner(error)
  );
  PageCursor cursor(cursor_str);
  bool any_entries = false;
  for (auto [id, invite] : sessions.list_invites_from_user(txn, cursor, login.id)) {
    any_entries = true;
    r.write_fmt(
      R"(<tr><td>{}<td>{:%D}<td>)"_cf,
      invite_id_to_code(id),
      uint_to_timestamp(invite.created_at())
    );
    if (auto to = invite.to()) {
      r.write_fmt(
        R"(N/A<td>{:%D}<td>)"_cf,
        uint_to_timestamp(*invite.accepted_at())
      );
      try {
        auto u = LocalUserDetail::get(txn, *to, login);
        html_user_link(r, u.user(), u.local_user().admin(), login);
        r.write("</tr>");
      } catch (...) {
        r.write("[error]</tr>");
      }
    } else {
      r.write_fmt(
        R"({:%D}<td>N/A<td>N/A</tr>)"_cf,
        uint_to_timestamp(invite.expires_at())
      );
    }
  }
  if (!any_entries) r.write(R"(<tr><td colspan="5">There's nothing here.</tr>)");
  // TODO: Pagination
  r.write("</tbody></table></div>");
}

}