#pragma once
#include "html_common.h++"
#include "html_form_widgets.h++"
#include "models/site.h++"
#include "models/thread.h++"
#include "models/comment.h++"
#include "controllers/post_controller.h++"

namespace Ludwig {

template <class T>
void html_reply_form(ResponseWriter& r, const T& parent) noexcept {
  using fmt::operator""_cf;
  r.write_fmt(
    R"(<form data-component="Form" id="reply-{1:x}" class="form reply-form" method="post" action="/{0}/{1:x}/reply" )"
    R"html(hx-post="/{0}/{1:x}/reply" hx-target="#comments-{1:x}" hx-swap="afterbegin" hx-on::after-request="this.reset()">)html"
    R"(<a name="reply"></a>)"
    HTML_TEXTAREA("text_content", "Reply", R"( required placeholder="Write your reply here")", "")
    ""_cf,
    T::noun, parent.id
  );
  html_content_warning_field(r);
  r.write(R"(<input type="submit" value="Reply"></form>)");
}

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
  bool is_alt = false
) noexcept;

void html_thread_view(
  ResponseWriter& r,
  const ThreadDetail& thread,
  const CommentTree& comments,
  const SiteDetail* site,
  Login login,
  CommentSortType sort,
  bool show_images = false
) noexcept;

void html_comment_view(
  ResponseWriter& r,
  const CommentDetail& comment,
  const CommentTree& comments,
  const SiteDetail* site,
  Login login,
  CommentSortType sort,
  bool show_images = false
) noexcept;

}