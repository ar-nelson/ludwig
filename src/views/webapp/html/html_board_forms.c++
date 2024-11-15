#include "html_board_forms.h++"
#include "html_form_widgets.h++"
#include "util/rich_text.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_create_board_form(
  ResponseWriter& r,
  const SiteDetail* site,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<main><form data-component="Form" class="form form-page" method="post" action="/create_board"><h2>Create Board</h2>{})"_cf,
    error_banner(error)
  );
  r.write(
    HTML_FIELD("name", "Name", "text", R"( autocomplete="off" placeholder="my_cool_board" pattern=")" USERNAME_REGEX_SRC R"(" required)")
    HTML_FIELD("display_name", "Display name", "text", R"( autocomplete="off" placeholder="My Cool Board")")
    HTML_FIELD("content_warning", "Content warning (optional)", "text", R"( autocomplete="off")")
    HTML_CHECKBOX("private", "Private (only visible to members)", "")
    HTML_CHECKBOX("restricted_posting", "Restrict posting to moderators", "")
    HTML_CHECKBOX("approve_subscribe", "Approval required to join", "")
    //HTML_CHECKBOX("invite_required", "Invite code required to join", "")
    //HTML_CHECKBOX("invite_mod_only", "Only moderators can invite new members", "")
  );
  html_voting_select(
    r,
    site->votes_enabled,
    site->downvotes_enabled,
    site->votes_enabled,
    site->downvotes_enabled
  );
  r.write(R"(<input type="submit" value="Submit"></form></main>)");
}

void html_board_settings_form(
  ResponseWriter& r,
  const SiteDetail* site,
  const LocalBoardDetail& board,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<form data-component="Form" class="form form-page" method="post" action="/b/{}/settings"><h2>Board settings</h2>{})"
    HTML_FIELD("display_name", "Display name", "text", R"( autocomplete="off" value="{}")")
    HTML_TEXTAREA("description", "Sidebar description", "", "{}")
    HTML_FIELD("content_warning", "Content warning (optional)", "text", R"( autocomplete="off" value="{}")")
    HTML_FIELD("icon_url", "Icon URL", "text", R"( autocomplete="off" value="{}")")
    HTML_FIELD("banner_url", "Banner URL", "text", R"( autocomplete="off" value="{}")")
    HTML_CHECKBOX("private", "Private (only visible to members)", "{}")
    HTML_CHECKBOX("restricted_posting", "Restrict posting to moderators", "{}")
    HTML_CHECKBOX("approve_subscribe", "Approval required to join", "{}")
    //HTML_CHECKBOX("invite_required", "Invite code required to join", "{}")
    //HTML_CHECKBOX("invite_mod_only", "Only moderators can invite new members", "{}")
    ""_cf,
    Escape(board.board().name()), error_banner(error),
    Escape(rich_text_to_plain_text(board.board().display_name_type(), board.board().display_name())),
    Escape(board.board().description_raw()),
    Escape(board.board().content_warning()),
    Escape(board.board().icon_url()),
    Escape(board.board().banner_url()),
    check(board.local_board().private_()),
    check(board.board().restricted_posting()),
    check(board.board().approve_subscribe())
  );
  html_voting_select(
    r,
    board.board().can_upvote(),
    board.board().can_downvote(),
    site->votes_enabled,
    site->downvotes_enabled
  );
  r.write(R"(<input type="submit" value="Submit"></form>)");
}

}