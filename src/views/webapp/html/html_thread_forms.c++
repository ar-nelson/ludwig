#include "html_thread_forms.h++"
#include "html_post_widgets.h++"
#include "html_form_widgets.h++"
#include "util/rich_text.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_create_thread_form(
  ResponseWriter& r,
  bool show_url,
  const BoardDetail board,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<main><form data-component="Form" class="form form-page" method="post" action="/b/{}/create_thread"><h2>Create Thread</h2>{})"
    R"(<p class="thread-info"><span>Posting as )"_cf,
    Escape(board.board().name()), error_banner(error)
  );
  html_user_link(r, login.user(), login.local_user().admin(), login);
  r.write("</span><span>to ");
  html_board_link(r, board.board());
  r.write("</span></p><br>" HTML_FIELD("title", "Title", "text", R"( autocomplete="off" required)"));
  if (show_url) {
    r.write(
      HTML_FIELD("submission_url", "Submission URL", "text", R"( autocomplete="off" required)")
      HTML_TEXTAREA("text_content", "Description (optional)", "", "")
    );
  } else {
    r.write(HTML_TEXTAREA("text_content", "Text content", " required", ""));
  }
  r.write(R"(<input type="submit" value="Submit"></form></main>)");
}

void html_edit_thread_form(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const LocalUserDetail& login,
  optional<string_view> error
) noexcept {
  r.write_fmt(
    R"(<main><form data-component="Form" class="form form-page" method="post" action="/thread/{:x}/edit"><h2>Edit Thread</h2>{})"
    R"(<p class="thread-info"><span>Posting as )"_cf,
    thread.id, error_banner(error)
  );
  html_user_link(r, login.user(), login.local_user().admin(), login);
  r.write("</span><span>to ");
  html_board_link(r, thread.board());
  r.write_fmt(
    "</span></p><br>"
    HTML_FIELD("title", "Title", "text", R"( value="{}" autocomplete="off" required)")
    HTML_TEXTAREA("text_content", "Text content", "{}", "{}")
    ""_cf,
    Escape(display_name_as_text(thread.thread())),
    thread.thread().content_url() ? "" : " required",
    Escape(thread.thread().content_text_raw())
  );
  html_content_warning_field(r, thread.thread().content_warning() ? thread.thread().content_warning()->string_view() : "");
  r.write(R"(<input type="submit" value="Submit"></form></main>)");
}

}