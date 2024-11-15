#include "html_comment_entry.h++"
#include "html_rich_text.h++"
#include "html_post_widgets.h++"
#include "html_action_menu.h++"

using std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_comment_header(
  ResponseWriter& r,
  const CommentDetail& comment,
  Login login,
  PostContext context
) noexcept {
  const string_view tag = context == PostContext::Reply ? "h3" : "h2";
  r.write_fmt(R"(<{} class="comment-info" id="comment-info-{:x}"><span>)"_cf, tag, comment.id);
  if (context != PostContext::User) {
    html_user_link(r, comment.author(), comment.user_is_admin, login);
    r.write("</span><span>");
  }
  r.write("commented ");
  html_datetime(r, comment.created_at());
  if (context != PostContext::Reply) {
    r.write_fmt(R"(</span><span>on <a href="/thread/{:x}">{}</a>)"_cf,
      comment.comment().thread(),
      rich_text_to_html_emojis_only(comment.thread().title_type(), comment.thread().title(), {})
    );
    // TODO: Use thread tags, not comment tags
    html_tags(r, comment, context);
    if (context != PostContext::Board) {
      r.write(R"(</span><span>in )");
      html_board_link(r, comment.board());
    }
  }
  r.write_fmt(R"(</span></{}>)"_cf, tag);
}

void html_comment_body(
  ResponseWriter& r,
  const CommentDetail& comment,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept {
  const bool has_warnings = comment.content_warning(context) || comment.mod_state(context).state > ModState::Normal;
  const auto content = rich_text_to_html(
    comment.comment().content_type(),
    comment.comment().content(),
    { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
  );
  r.write_fmt(
    R"(<div class="comment-body" id="comment-body-{:x}"><div class="comment-content markdown">)"_cf,
    comment.id
  );
  if (has_warnings) {
    r.write(R"(<details class="content-warning-collapse"><summary>Content hidden (click to show))");
    html_content_warnings(r, comment, context);
    r.write_fmt(R"(</summary><div>{}</div></details></div>)"_cf, content);
  } else {
    r.write_fmt(R"({}</div>)"_cf, content);
  }
  html_vote_buttons(r, comment, site, login);
  r.write(R"(<div class="controls">)");
  if (context != PostContext::Reply) {
    r.write_fmt(R"(<a id="comment-link-{0:x}" href="/comment/{0:x}#replies">{1:d}{2}</a>)"_cf,
      comment.id,
      comment.stats().descendant_count(),
      comment.stats().descendant_count() == 1 ? " reply" : " replies"
    );
  } else {
    r.write_fmt(R"(<a href="/comment/{:x}">Permalink</a>)"_cf, comment.id);
  }
  html_action_menu(r, comment, login, context);
  r.write("</div></div>");
}

void html_comment_entry(
  ResponseWriter& r,
  const CommentDetail& comment,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept {
  r.write_fmt(R"(<li><article class="comment{}" id="comment-{:x}">)"_cf,
    comment.should_show_votes(login, site) ? "" : " no-votes",
    comment.id
  );
  html_comment_header(r, comment, login, context);
  html_comment_body(r, comment, site, login, context, show_images);
  r.write("</article>");
}

}