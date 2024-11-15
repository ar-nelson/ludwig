#pragma once
#include "html_common.h++"
#include "models/comment.h++"
#include "models/site.h++"

namespace Ludwig {

void html_comment_header(
  ResponseWriter& r,
  const CommentDetail& comment,
   Login login,
  PostContext context
) noexcept;

void html_comment_body(
  ResponseWriter& r,
  const CommentDetail& comment,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept;

void html_comment_entry(
  ResponseWriter& r,
  const CommentDetail& comment,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept;

}