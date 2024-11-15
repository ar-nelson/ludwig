#include "html_first_run_setup_form.h++"
#include "html_form_widgets.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_first_run_setup_form(
  ResponseWriter& r,
  const FirstRunSetupOptions& options,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<main><form data-component="Form" class="form form-page" method="post" action="/site_admin/first_run_setup">{})"
    HTML_FIELD("name", "What is this server's name?", "text", R"( required value="Ludwig" autocomplete="off")")
    "{}"_cf,
    error_banner(error),
    options.base_url_set ? "" : HTML_FIELD("base_url",
      "What domain will this server be accessed at?<br><strong>Important: This cannot be changed later!</strong>",
      "text",
      R"( required placeholder="https://ludwig.example" pattern="https?://[a-zA-Z0-9_\-]+([.][a-zA-Z0-9_\-]+)*(:\d{1,5})?" autocomplete="off")"
    )
  );
  if (!options.home_page_type_set) html_home_page_type_select(r);
  html_voting_select(r);
  r.write(
    HTML_CHECKBOX("cws_enabled", "Allow posts with content warnings (also known as NSFW posts)?", R"( checked autocomplete="off")")
    HTML_CHECKBOX("not_board_creation_admin_only", "Allow non-admin users to create boards?", R"( checked autocomplete="off")")
    HTML_CHECKBOX("registation_enabled", "Allow new users to register?", R"( checked autocomplete="off")")
    HTML_CHECKBOX("registation_application_required", "Require admin approval for registration?", R"( checked autocomplete="off")")
    HTML_TEXTAREA("application_question", "Application question", "", "Why do you want to join?")
    HTML_CHECKBOX("registation_invite_required", "Require invite codes for registration?", R"( autocomplete="off")")
    HTML_CHECKBOX("not_invite_admin_only", "Allow non-admin users to generate invite codes?", R"( autocomplete="off")")
    R"(<details><summary>Advanced</summary><fieldset><legend class="a11y">Advanced</legend><blockquote>)"
      HTML_FIELD("post_max_length", "Max post length (bytes)", "number", R"( min="512" value="1048576" autocomplete="off")")
      HTML_CHECKBOX("javascript_enabled", "Enable JavaScript?", R"( checked autocomplete="off")")
      HTML_CHECKBOX("infinite_scroll_enabled", "Enable infinite scroll?", R"( checked autocomplete="off")")
    R"(</blockquote></fieldset></details>)"
  );
  if (!options.admin_exists) {
    r.write(
      "<fieldset><legend>Create Admin Account</legend>"
      HTML_FIELD("admin_username", "Admin Username", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(" placeholder="admin")")
      HTML_FIELD("admin_password", "Admin Password", "password", " required")
      "</fieldset>"
    );
  }
  if (!options.default_board_exists) {
    r.write(
      "<fieldset><legend>Create Default Board</legend>"
      HTML_FIELD("default_board_name", "Board Name", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(" placeholder="home")")
      "</fieldset>"
    );
  }
  r.write(R"(<input type="submit" value="Submit"></form></main>)");
}

}