#pragma once
#include "html_common.h++"
#include "html_form_widgets.h++"
#include "db/page_cursor.h++"

namespace Ludwig {

void html_show_threads_toggle(ResponseWriter& r, bool show_threads) noexcept;
void html_local_toggle(ResponseWriter& r, bool local_only) noexcept;
void html_show_images_toggle(ResponseWriter& r, bool show_images) noexcept;
void html_subscribed_toggle(ResponseWriter& r, bool show_images) noexcept;

template <class T>
static inline void html_toggle_1(ResponseWriter& r, bool) noexcept {}
template <> inline void html_toggle_1<SortType>(ResponseWriter& r, bool t) noexcept { html_show_threads_toggle(r, t); }
template <> inline void html_toggle_1<UserPostSortType>(ResponseWriter& r, bool t) noexcept { html_show_threads_toggle(r, t); }
template <> inline void html_toggle_1<UserSortType>(ResponseWriter& r, bool t) noexcept { html_local_toggle(r, t); }
template <> inline void html_toggle_1<BoardSortType>(ResponseWriter& r, bool t) noexcept { html_local_toggle(r, t); }

template <class T>
static inline void html_toggle_2(ResponseWriter& r, bool) noexcept {
  r.write(R"(</label><input class="no-js" type="submit" value="Apply"></form>)");
};
template <> inline void html_toggle_2<SortType>(ResponseWriter& r, bool t) noexcept { html_show_images_toggle(r, t); }
template <> inline void html_toggle_2<CommentSortType>(ResponseWriter& r, bool t) noexcept { html_show_images_toggle(r, t); }
template <> inline void html_toggle_2<UserPostSortType>(ResponseWriter& r, bool t) noexcept { html_show_images_toggle(r, t); }
template <> inline void html_toggle_2<BoardSortType>(ResponseWriter& r, bool t) noexcept { html_subscribed_toggle(r, t); }

template <class T>
void html_sort_options(
  ResponseWriter& r,
  std::string_view base_url,
  T sort,
  bool toggle_1,
  bool toggle_2,
  std::string_view hx_target = "#top-level-list"
) noexcept {
  using fmt::operator""_cf;
  r.write_fmt(
    R"(<form class="sort-form" method="get" action="{0}" hx-get="{0}" hx-trigger="change" hx-target="{1}" hx-swap="outerHTML" hx-push-url="true">)"_cf,
    Escape{base_url}, Escape{hx_target}
  );
  html_toggle_1<T>(r, toggle_1);
  r.write(R"(<label for="sort"><span class="a11y">Sort</span>)");
  html_sort_select(r, "sort", sort);
  html_toggle_2<T>(r, toggle_2);
}

void html_pagination(
  ResponseWriter& r,
  std::string_view base_url,
  bool is_first,
  PageCursor next,
  bool infinite_scroll_enabled = true
) noexcept;

}