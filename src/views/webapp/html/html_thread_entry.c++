#include "html_thread_entry.h++"
#include "html_rich_text.h++"
#include "html_post_widgets.h++"
#include "html_action_menu.h++"

using fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_thread_entry(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept {
  // TODO: thread-source (link URL)
  r.write_fmt(
    R"({} class="thread" id="thread-{:x}"><h2 class="thread-title">)"_cf,
    context == PostContext::View ? "<div" : "<li><article",
    thread.id
  );
  const auto title = rich_text_to_html_emojis_only(thread.thread().title_type(), thread.thread().title(), {});
  if (context != PostContext::View || thread.thread().content_url()) {
    r.write_fmt(R"(<a class="thread-title-link" href="{}">{}</a></h2>)"_cf,
      Escape{
        thread.thread().content_url()
          ? thread.thread().content_url()->string_view()
          : format("/thread/{:x}"_cf, thread.id)
      },
      title
    );
  } else {
    r.write_fmt("{}</h2>"_cf, title);
  }
  const auto cw = thread.content_warning(context);
  // TODO: Selectively show CW'd images, maybe use blurhash
  if (show_images && !cw && thread.link_card().image_url()) {
    r.write_fmt(
      R"(<div class="thumbnail"><img src="/media/thread/{:x}/thumbnail.webp" aria-hidden="true"></div>)"_cf,
      thread.id
    );
  } else {
    r.write_fmt(
      R"(<div class="thumbnail">)" ICON("{}") "</div>"_cf,
      cw ? "alert-octagon" : (thread.thread().content_url() ? "link" : "file-text")
    );
  }
  if (
    (cw || thread.mod_state(context).state > ModState::Normal) &&
    (context != PostContext::View || !thread.has_text_content())
  ) {
    r.write(R"(<div class="thread-warnings">)");
    html_content_warnings(r, thread, context);
    r.write(R"(</div>)");
  }
  r.write(R"(<div class="thread-info"><span>submitted )");
  html_datetime(r, thread.created_at());
  if (context != PostContext::User) {
    r.write("</span><span>by ");
    html_user_link(r, thread.author(), thread.user_is_admin, login);
  }
  if (context != PostContext::Board) {
    r.write("</span><span>to ");
    html_board_link(r, thread.board());
  }
  r.write("</span></div>");
  html_vote_buttons(r, thread, site, login);
  if (context != PostContext::View) {
    r.write_fmt(R"(<div class="controls"><a id="comment-link-{0:x}" href="/thread/{0:x}#comments">{1:d}{2}</a>)"_cf,
      thread.id,
      thread.stats().descendant_count(),
      thread.stats().descendant_count() == 1 ? " comment" : " comments"
    );
  } else {
    r.write(R"(<div class="controls"><span></span>)");
  }
  html_action_menu(r, thread, login, context);
  r.write(context == PostContext::View ? "</div></div>" : "</div></article>");
}

}