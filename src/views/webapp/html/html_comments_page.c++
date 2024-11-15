#include "html_comments_page.h++"
#include "html_comment_entry.h++"
#include "html_thread_entry.h++"
#include "html_post_widgets.h++"
#include "html_list_widgets.h++"
#include "html_rich_text.h++"

using fmt::format, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_comment_tree(
  ResponseWriter& r,
  const CommentTree& comments,
  uint64_t root,
  CommentSortType sort,
  const SiteDetail* site,
  Login login,
  bool show_images,
  bool is_top_level,
  bool include_ol,
  bool is_alt
) noexcept {
  // TODO: Include existing query params
  if (include_ol) r.write_fmt(R"(<ol class="comment-list comment-tree" id="comments-{:x}">)"_cf, root);
  auto range = comments.comments.equal_range(root);
  if (range.first == range.second) {
    if (is_top_level) r.write(R"(<li class="no-comments">No comments</li>)");
  } else {
    const bool infinite_scroll_enabled =
      site->infinite_scroll_enabled && (!login || login->local_user().infinite_scroll_enabled());
    for (auto iter = range.first; iter != range.second; iter++) {
      const auto& comment = iter->second;
      r.write_fmt(
        R"(<li><article class="comment-with-comments{}{}">)"
        R"(<details open class="comment-collapse" id="comment-{:x}"><summary>)"_cf,
        comment.should_show_votes(login, site) ? "" : " no-votes",
        is_alt ? " odd-depth" : "",
        comment.id
      );
      html_comment_header(r, comment, login, PostContext::Reply);
      r.write_fmt(
        R"(<small class="comment-reply-count">({:d} repl{})</small>)"_cf,
        comment.stats().descendant_count(),
        comment.stats().descendant_count() == 1 ? "y" : "ies"
      );
      r.write("</summary>");
      html_comment_body(r, comment, site, login, PostContext::Reply, show_images);
      const auto cont = comments.continued.find(comment.id);
      if (cont != comments.continued.end() && !cont->second) {
        r.write_fmt(
          R"(<a class="more-link{0}" id="continue-{1:x}" href="/comment/{1:x}">More comments…</a>)"_cf,
          is_alt ? "" : " odd-depth", comment.id
        );
      } else if (comment.stats().child_count()) {
        r.write(R"(<section class="comments" aria-title="Replies">)");
        html_comment_tree(r, comments, comment.id, sort, site, login, show_images, false, true, !is_alt);
        r.write("</section>");
      }
      r.write("</details></article>");
    }
    const auto cont = comments.continued.find(root);
    if (cont != comments.continued.end()) {
      r.write_fmt(R"(<li id="comment-replace-{:x}")"_cf, root);
      if (infinite_scroll_enabled) {
        r.write_fmt(
          R"( hx-get="/{0}/{1:x}?sort={2}&from={3}" hx-swap="outerHTML" hx-trigger="revealed")"_cf,
          is_top_level ? "thread" : "comment", root,
          EnumNameCommentSortType(sort), cont->second.to_string()
        );
      }
      r.write_fmt(
        R"(><a class="more-link{0}" id="continue-{1:x}" href="/{2}/{1:x}?sort={3}&from={4}")"
        R"( hx-get="/{2}/{1:x}?sort={3}&from={4}" hx-target="#comment-replace-{1:x}" hx-swap="outerHTML">More comments…</a>)"_cf,
        is_alt ? " odd-depth" : "", root, is_top_level ? "thread" : "comment",
        EnumNameCommentSortType(sort), cont->second.to_string()
      );
    }
  }
  if (include_ol) r.write("</ol>");
}

void html_thread_view(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const CommentTree& comments,
  const SiteDetail* site,
  Login login,
  CommentSortType sort,
  bool show_images
) noexcept {
  r.write_fmt(R"(<article class="thread-with-comments{}">)"_cf,
    thread.should_show_votes(login, site) ? "" : " no-votes"
  );
  html_thread_entry(r, thread, site, login, PostContext::View, show_images);
  if (thread.has_text_content()) {
    const auto content = rich_text_to_html(
      thread.thread().content_text_type(),
      thread.thread().content_text(),
      { .show_images = show_images, .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
    );
    if (thread.thread().content_warning() || thread.board().content_warning() || thread.thread().mod_state() > ModState::Normal) {
      r.write(R"(<div class="thread-content markdown"><details class="content-warning-collapse"><summary>Content hidden (click to show))");
      html_content_warnings(r, thread, PostContext::View);
      r.write_fmt(R"(</summary><div>{}</div></details></div>)"_cf, content);
    } else {
      r.write_fmt(R"(<div class="thread-content markdown">{}</div>)"_cf, content);
    }
  }
  r.write_fmt(R"(<section class="comments" id="comments"><h2>{:d} comments</h2>)"_cf, thread.stats().descendant_count());
  html_sort_options(r, format("/thread/{:x}"_cf, thread.id), sort, false, show_images, format("#comments-{:x}", thread.id));
  if (thread.can_reply_to(login)) {
    html_reply_form(r, thread);
  }
  html_comment_tree(r, comments, thread.id, sort, site, login, show_images, true, true);
  r.write("</section></article>");
}

void html_comment_view(
  ResponseWriter& r,
  const CommentDetail& comment,
  const CommentTree& comments,
  const SiteDetail* site,
  Login login,
  CommentSortType sort,
  bool show_images
) noexcept {
  r.write_fmt(R"(<article class="comment-with-comments"><section class="comment{}" id="comment-{:x}">)"_cf,
    comment.should_show_votes(login, site) ? "" : " no-votes",
    comment.id
  );
  html_comment_header(r, comment, login, PostContext::View);
  html_comment_body(r, comment, site, login, PostContext::View, show_images);
  r.write_fmt(R"(</section><section class="comments" id="comments"><h2>{:d} replies</h2>)"_cf, comment.stats().descendant_count());
  html_sort_options(r, format("/comment/{:x}"_cf, comment.id), sort, false, show_images, format("#comments-{:x}"_cf, comment.id));
  if (comment.can_reply_to(login)) {
    html_reply_form(r, comment);
  }
  html_comment_tree(r, comments, comment.id, sort, site, login, show_images, false, true);
  r.write("</section></article>");
}

}