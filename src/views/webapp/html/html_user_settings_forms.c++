#include "html_user_settings_forms.h++"
#include "html_form_widgets.h++"
#include "html_post_widgets.h++"
#include "util/rich_text.h++"
#include "views/webapp/paths.h++"

using std::optional, std::pair, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

// Settings
#define FIELD_default_sort_type           "default_sort_type"
#define FIELD_default_comment_sort_type   "default_comment_sort_type"
#define FIELD_show_avatars                "show_avatars"
#define FIELD_show_karma                  "show_karma"
#define FIELD_show_images_threads         "show_images_threads"
#define FIELD_show_images_comments        "show_images_comments"
#define FIELD_show_bot_accounts           "show_bot_accounts"
#define FIELD_show_new_post_notifs        "show_new_post_notifs"
#define FIELD_show_read_posts             "show_read_posts"
#define FIELD_content_warnings            "content_warnings"
#define FIELD_open_links_in_new_tab       "open_links_in_new_tab"
#define FIELD_send_notifications_to_email "send_notifications_to_email"
#define FIELD_javascript_enabled          "javascript_enabled"
#define FIELD_infinite_scroll_enabled     "infinite_scroll_enabled"

// Profile
#define FIELD_display_name "display_name"
#define FIELD_email        "email"
#define FIELD_bio          "bio"
#define FIELD_avatar_url   "avatar_url"
#define FIELD_banner_url   "banner_url"

// Account
#define FIELD_old_password     "old_password"
#define FIELD_password         "password"
#define FIELD_confirm_password "confirm_password"
#define FIELD_delete_password  "delete_password"
#define FIELD_delete_confirm   "delete_confirm"
#define FIELD_delete_posts     "delete_posts"

void html_user_settings_tabs(ResponseWriter& r, const SiteDetail* site, UserSettingsTab selected) noexcept {
  using enum UserSettingsTab; 
  r.write(R"(<ul class="tabs">)");
  html_tab(r, Settings, selected, "Settings",PATH_USER_settings);
  html_tab(r, Profile, selected, "Profile",PATH_USER_profile);
  html_tab(r, Account, selected, "Account",PATH_USER_account);
  if (site->registration_invite_required && !site->invite_admin_only) {
    html_tab(r, Invites, selected, "Invites",PATH_USER_invites);
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
    R"(<form data-component="Form" class="form form-page" method="post" action=")" PATH_USER_settings R"("><h2>User settings</h2>{})"
    R"(<fieldset><legend>Sorting</legend>)"
    R"(<label for=")" FIELD_default_sort_type R"("><span>Default sort</span>)"_cf,
    error_banner(error)
  );
  html_sort_select(r,FIELD_default_sort_type, u.default_sort_type());
  r.write(R"(</label><label for=")" FIELD_default_comment_sort_type R"("><span>Default comment sort</span>)");
  html_sort_select(r,FIELD_default_comment_sort_type, u.default_comment_sort_type());
  r.write_fmt(
    R"(</label></fieldset><fieldset><legend>Show/Hide</legend>)"
    HTML_CHECKBOX(FIELD_show_avatars, "Show avatars", "{}")
    ""_cf,
    check(u.show_avatars())
  );
  if (site->votes_enabled) {
    r.write_fmt(HTML_CHECKBOX(FIELD_show_karma, "Show karma (score)", "{}") ""_cf, check(u.show_karma()));
  }
  r.write_fmt(
    HTML_CHECKBOX(FIELD_show_images_threads, "Show images on threads by default", "{}")
    HTML_CHECKBOX(FIELD_show_images_comments, "Show inline images in comments by default", "{}")
    HTML_CHECKBOX(FIELD_show_bot_accounts, "Show bot accounts", "{}")
    HTML_CHECKBOX(FIELD_show_new_post_notifs, "Show new post notifications", "{}")
    HTML_CHECKBOX(FIELD_show_read_posts, "Show read posts", "{}")
    ""_cf,
    check(u.show_images_threads()),
    check(u.show_images_comments()),
    check(u.show_bot_accounts()),
    check(u.show_new_post_notifs()),
    check(u.show_read_posts())
  );
  if (site->cws_enabled) {
    r.write_fmt(
      R"(<label><span>Content warnings</span><select name=")" FIELD_content_warnings R"(" autocomplete="off">)"
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
    HTML_CHECKBOX(FIELD_open_links_in_new_tab, "Open links in new tab", "{}")
    HTML_CHECKBOX(FIELD_send_notifications_to_email, "Send notifications to email", "{}")
    ""_cf,
    check(u.open_links_in_new_tab()),
    check(u.send_notifications_to_email())
  );
  if (site->javascript_enabled) {
    r.write_fmt(HTML_CHECKBOX(FIELD_javascript_enabled, "JavaScript enabled", "{}") ""_cf, check(u.javascript_enabled()));
  }
  if (site->infinite_scroll_enabled) {
    r.write_fmt(HTML_CHECKBOX(FIELD_infinite_scroll_enabled, "Infinite scroll enabled", "{}") ""_cf, check(u.infinite_scroll_enabled()));
  }
  r.write(R"(</fieldset><input type="submit" value="Submit"></form>)");
}

auto user_settings_form_to_update(QueryString<string_view> form) -> LocalUserUpdate {
  const auto cw = form.required_int(FIELD_content_warnings);
  return LocalUserUpdate {
    .open_links_in_new_tab = form.optional_bool(FIELD_open_links_in_new_tab),
    .show_avatars = form.optional_bool(FIELD_show_avatars),
    .show_bot_accounts = form.optional_bool(FIELD_show_bot_accounts),
    .show_karma = form.optional_bool(FIELD_show_karma),
    .show_images_threads = form.optional_bool(FIELD_show_images_threads),
    .show_images_comments = form.optional_bool(FIELD_show_images_comments),
    .hide_cw_posts = cw < 1,
    .expand_cw_images = cw > 2,
    .expand_cw_posts = cw > 1,
    .show_new_post_notifs = form.optional_bool(FIELD_show_new_post_notifs),
    .show_read_posts = form.optional_bool(FIELD_show_read_posts),
    .send_notifications_to_email = form.optional_bool(FIELD_send_notifications_to_email),
    .javascript_enabled = form.optional_bool(FIELD_javascript_enabled),
    .infinite_scroll_enabled = form.optional_bool(FIELD_infinite_scroll_enabled),
    .default_sort_type = parse_sort_type(form.required_string(FIELD_default_sort_type)),
    .default_comment_sort_type = parse_comment_sort_type(form.required_string(FIELD_default_comment_sort_type)),
  };
}

void html_user_settings_profile_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action=")" PATH_USER_profile R"("><h2>Profile</h2>{})"
    R"(<label for="name"><span>Username</span><output name="name" id="name">{}</output></label>)"
    HTML_FIELD(FIELD_display_name, "Display name", "text", R"( value="{}")")
    HTML_FIELD(FIELD_email, "Email address", "email", R"( required value="{}")")
    HTML_TEXTAREA(FIELD_bio, "Bio", "", "{}")
    HTML_FIELD(FIELD_avatar_url, "Avatar URL", "text", R"( value="{}")")
    HTML_FIELD(FIELD_banner_url, "Banner URL", "text", R"( value="{}")")
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

auto user_settings_profile_form_to_update(QueryString<string_view> form) -> LocalUserUpdate {
  return LocalUserUpdate {
    .email = form.required_string(FIELD_email),
    .display_name = { form.optional_string(FIELD_display_name) },
    .bio = { form.optional_string(FIELD_bio) },
    .avatar_url = { form.optional_string(FIELD_avatar_url) },
    .banner_url = { form.optional_string(FIELD_banner_url) }
  };
}

void html_user_settings_account_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action=")" PATH_USER_change_password R"("><h2>Change password</h2>{})"
    HTML_FIELD(FIELD_old_password, "Old password", "password", R"( required autocomplete="off")")
    HTML_FIELD(FIELD_password, "New password", "password", R"( required autocomplete="off")")
    HTML_FIELD(FIELD_confirm_password, "Confirm new password", "password", R"( required autocomplete="off")")
    R"(<input type="submit" value="Submit"></form><br>)"
    R"(<form data-component="Form" class="form form-page" method="post" action=")" PATH_USER_delete_account R"("><h2>Delete account</h2>)"
    R"(<p>⚠️ <strong>Warning: This cannot be undone!</strong> ⚠️</p>)"
    HTML_FIELD(FIELD_delete_password, "Type your password here", "password", R"( required autocomplete="off")")
    HTML_FIELD(FIELD_delete_confirm, R"(Type "delete" here to confirm)", "text", R"( required autocomplete="off")")
    HTML_CHECKBOX(FIELD_delete_posts, "Also delete all of my posts", R"( autocomplete="off")")
    R"(<input type="submit" value="Delete Account"></form>)"_cf,
    error_banner(error)
  );
}

auto user_settings_validate_change_password_form(QueryString<string_view> form) -> pair<SecretString, SecretString> {
  SecretString
    old_password(form.required_string(FIELD_old_password)),
    confirm_password(form.required_string(FIELD_confirm_password)),
    new_password(form.required_string(FIELD_password));
  if (old_password.data != confirm_password.data) throw ApiError("Passwords do not match", 400);
  return { std::move(old_password), std::move(new_password) };
}

auto user_settings_validate_delete_account_form(QueryString<string_view> form) -> pair<SecretString, bool> {
  if (!form.optional_bool(FIELD_delete_confirm)) throw ApiError("Did not confirm", 400);
  return { SecretString(form.required_string(FIELD_delete_password)), form.optional_bool(FIELD_delete_posts) };
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
    R"(<form action=")" PATH_USER_new_invite R"(" method="post"><input type="submit" value="Generate New Invite Code"></form><table>)"
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