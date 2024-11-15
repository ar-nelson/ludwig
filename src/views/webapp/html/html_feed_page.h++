#pragma once
#include "html_common.h++"
#include "views/webapp/webapp_common.h++"
#include "html_list_widgets.h++"
#include "html_thread_entry.h++"
#include "html_comment_entry.h++"
#include "db/page_cursor.h++"

namespace Ludwig {

template <class Detail>
void html_feed_entry(
  ResponseWriter& r,
  const Detail& detail,
  const SiteDetail* site,
  Login login,
  PostContext context = PostContext::Feed,
  bool show_images = true
) noexcept;

template <> inline void html_feed_entry<ThreadDetail>(
  ResponseWriter& r,
  const ThreadDetail& detail,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept {
  html_thread_entry(r, detail, site, login, context, show_images);
}

template <> inline void html_feed_entry<CommentDetail>(
  ResponseWriter& r,
  const CommentDetail& detail,
  const SiteDetail* site,
  Login login,
  PostContext context,
  bool show_images
) noexcept {
  html_comment_entry(r, detail, site, login, context, show_images);
}

template <class Detail> constexpr bool is_show_threads() { return false; }
template <> constexpr bool is_show_threads<ThreadDetail>() { return true; }

template <class Detail, class Sort>
void html_feed_page(
  GenericContext& r,
  PageCursor& cursor,
  std::generator<const Detail&> entries,
  std::string_view base_url,
  Sort sort,
  PostContext context = PostContext::Feed,
  bool show_images = true,
  bool show_votes = true
) {
  using fmt::operator""_cf;
  bool is_first_page = !cursor.exists;
  if (!r.is_htmx) {
    r.write(R"(<section><h2 class="a11y">Sort and filter</h2>)");
    html_sort_options(r, base_url, sort, is_show_threads<Detail>(), show_images);
    r.write_fmt(R"(</section><main><ol class="{}-list{}" id="top-level-list">)"_cf,
      is_show_threads<Detail>() ? "thread" : "comment",
      show_votes ? "" : " no-votes"
    );
  }
  uint8_t count = 0;
  for (const auto& e : entries) {
    html_feed_entry<Detail>(r, e, r.site, r.login, context, show_images);
    if (++count >= 20) break;
  }
  if (!r.is_htmx) {
    if (count == 0) r.write(R"(<li class="no-entries">There's nothing here.)");
    r.write("</ol>");
  }
  html_pagination(r, base_url, is_first_page, cursor);
  if (!r.is_htmx) r.write("</main>");
}

}