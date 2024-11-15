#include "html_site_admin_forms.h++"
#include "html_form_widgets.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_site_admin_tabs(ResponseWriter& r, const SiteDetail* site, SiteAdminTab selected) noexcept {
  r.write(R"(<ul class="tabs">)");
  html_tab(r, SiteAdminTab::Settings, selected, "Settings", "/site_admin");
  html_tab(r, SiteAdminTab::ImportExport, selected, "Import/Export", "/site_admin/import_export");
  if (site->registration_application_required) {
    html_tab(r, SiteAdminTab::Applications, selected, "Applications", "/site_admin/applications");
  }
  if (site->registration_invite_required) {
    html_tab(r, SiteAdminTab::Invites, selected, "Invites", "/site_admin/invites");
  }
  r.write("</ul>");
}

void html_site_admin_form(
  ResponseWriter& r,
  const SiteDetail* site,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action="/site_admin"><h2>Site settings</h2>{})"
    HTML_FIELD("name", "Site name", "text", R"( value="{}" autocomplete="off" required)")
    HTML_TEXTAREA("description", "Sidebar description", "", "{}")
    HTML_FIELD("icon_url", "Icon URL", "text", R"( value="{}" autocomplete="off")")
    HTML_FIELD("banner_url", "Banner URL", "text", R"( value="{}" autocomplete="off")")
    HTML_FIELD("color_accent", "Accent Color", "color", R"( value="{}" autocomplete="off")")
    HTML_FIELD("color_accent_dim", "Accent Color (Dim)", "color", R"( value="{}" autocomplete="off")")
    HTML_FIELD("color_accent_hover", "Accent Color (Hover)", "color", R"( value="{}" autocomplete="off")")
    ""_cf,
    error_banner(error),
    Escape{site->name}, Escape{site->description},
    Escape{site->icon_url.value_or("")}, Escape{site->banner_url.value_or("")},
    site->color_accent,
    site->color_accent_dim,
    site->color_accent_hover
  );
  html_home_page_type_select(r, site->home_page_type);
  html_voting_select(r, site->votes_enabled, site->downvotes_enabled);
  r.write_fmt(
    HTML_CHECKBOX("cws_enabled", "Allow posts with content warnings (also known as NSFW posts)?", R"( {} autocomplete="off")")
    HTML_CHECKBOX("not_board_creation_admin_only", "Allow non-admin users to create boards?", R"( {} autocomplete="off")")
    HTML_CHECKBOX("registation_enabled", "Allow new users to register?", R"( {} autocomplete="off")")
    HTML_CHECKBOX("registation_application_required", "Require admin approval for registration?", R"( {} autocomplete="off")")
    HTML_TEXTAREA("application_question", "Application question", "", "{}")
    HTML_CHECKBOX("registation_invite_required", "Require invite codes for registration?", R"( {} autocomplete="off")")
    HTML_CHECKBOX("not_invite_admin_only", "Allow non-admin users to generate invite codes?", R"( {} autocomplete="off")")
    R"(<details><summary>Advanced</summary><fieldset><legend class="a11y">Advanced</legend>)"
      HTML_FIELD("post_max_length", "Max post length (bytes)", "number", R"( min="512" value="{:d}" autocomplete="off")")
      HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( {} autocomplete="off")")
      HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( {} autocomplete="off")")
    R"(</fieldset></details><input type="submit" value="Submit"></form>)"_cf,
    site->cws_enabled ? "checked" : "", site->board_creation_admin_only ? "" : "checked",
    site->registration_enabled ? "checked" : "", site->registration_application_required ? "checked" : "",
    Escape{site->application_question.value_or("")},
    site->registration_invite_required ? "checked" : "", site->invite_admin_only ? "" : "checked",
    site->post_max_length, site->javascript_enabled ? "checked" : "", site->infinite_scroll_enabled ? "checked" : ""
  );
}

void html_site_admin_import_export_form(ResponseWriter& r) noexcept {
  r.write(
    R"(<form class="form form-page" method="post" action="/site_admin/export"><h2>Export Database</h2>)"
    R"(<input type="hidden" name="for_reals" value="yes">)"
    R"(<p>This will export the <strong>entire database</strong> as a <code>.dbdump.zst</code> file.</p>)"
    R"(<p>The exported file can later be imported using the <code>--import</code> command-line option.</p>)"
    R"(<p>⚠️ <strong>Warning: This is a huge file, and it can take a long time to download!</strong> ⚠️</p>)"
    R"(<input type="submit" value="Download All The Things"></form>)"
  );
}

void html_site_admin_applications_list(
  ResponseWriter& r,
  SessionController& sessions,
  ReadTxn& txn,
  Login login,
  optional<uint64_t> cursor,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<div class="table-page"><h2>Registration Applications</h2>{}<table>)"
    R"(<thead><th>Name<th>Email<th>Date<th>IP Addr<th>User Agent<th class="table-reason">Reason<th>Approved</thead>)"
    R"(<tbody id="application-table">)"_cf,
    error_banner(error)
  );
  bool any_entries = false;
  for (auto [application, detail] : sessions.list_applications(txn, cursor, login)) {
    any_entries = true;
    r.write_fmt(
      R"(<tr><td>{}<td>{}<td>{:%D}<td>{}<td>{}<td class="table-reason"><div class="reason">{}</div><td class="table-approve">)"_cf,
      Escape{detail.user().name()},
      Escape{detail.local_user().email()},
      detail.created_at(),
      Escape{application.ip()},
      Escape{application.user_agent()},
      Escape{application.text()}
    );
    if (detail.local_user().accepted_application()) {
      r.write(R"(<span class="a11y">Approved</span>)" ICON("check") "</tr>");
    } else {
      r.write_fmt(
        R"(<form method="post"><button type="submit" formaction="/site_admin/applications/approve/{0:x}">)"
        R"(<span class="a11y">Approve</span>)" ICON("check") "</button>"
        R"(&nbsp;<button type="submit" formaction="/site_admin/applications/reject/{0:x}">)"
        R"(<span class="a11y">Reject</span>)" ICON("x") "</button></form></tr>"_cf,
        detail.id
      );
    }
  }
  if (!any_entries) r.write(R"(<tr><td colspan="7">There's nothing here.</tr>)");
  // TODO: Pagination
  r.write("</tbody></table></div>");
}

auto form_to_site_update(QueryString<string_view> body) -> SiteUpdate {
  const auto voting = body.optional_uint("voting");
  return {
    .name = body.optional_string("name"),
    .description = body.optional_string("description"),
    .icon_url = body.optional_string("icon_url"),
    .banner_url = body.optional_string("banner_url"),
    .application_question = body.optional_string("application_question"),
    .post_max_length = body.optional_uint("post_max_length"),
    .remote_post_max_length = body.optional_uint("remote_post_max_length"),
    .home_page_type = body.optional_string("home_page_type").transform(parse_home_page_type),
    .javascript_enabled = body.optional_bool("javascript_enabled"),
    .infinite_scroll_enabled = body.optional_bool("infinite_scroll_enabled"),
    .votes_enabled = voting.transform(λx(x > 0)),
    .downvotes_enabled = voting.transform(λx(x > 1)),
    .cws_enabled = body.optional_bool("cws_enabled"),
    .require_login_to_view = body.optional_bool("require_login_to_view"),
    .board_creation_admin_only = !body.optional_bool("not_board_creation_admin_only"),
    .registration_enabled = body.optional_bool("registation_enabled"),
    .registration_application_required = body.optional_bool("registation_application_required"),
    .registration_invite_required = body.optional_bool("registation_invite_required"),
    .invite_admin_only = !body.optional_bool("not_invite_admin_only")
  };
}

}