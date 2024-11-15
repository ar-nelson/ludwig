#pragma once
#include "html_common.h++"
#include "models/enums.h++"

namespace Ludwig {

static inline auto error_banner(std::optional<std::string_view> error) noexcept -> std::string {
  using fmt::operator""_cf;
  if (!error) return "";
  return format(R"(<p class="error-message"><strong>Error:</strong> {}</p>)"_cf, Escape{*error});
}

static inline auto check(bool b) -> std::string_view {
  return b ? " checked" : "";
}

template <class T>
static inline auto select(T n, T v) -> std::string_view {
  return n == v ? " selected" : "";
}

template <class T>
void html_tab(ResponseWriter& r, T tab, T selected, std::string_view name, std::string_view url) {
  using fmt::operator""_cf;
  if (tab == selected) r.write_fmt(R"(<li><span class="selected">{}</span>)"_cf, name);
  else r.write_fmt(R"(<li><a href="{}">{}</a>)"_cf, url, name);
}

void html_home_page_type_select(ResponseWriter& r, HomePageType selected = HomePageType::Subscribed) noexcept;

void html_voting_select(
  ResponseWriter& r,
  bool voting_enabled = true,
  bool downvotes_enabled = true,
  bool sitewide_voting_enabled = true,
  bool sitewide_downvotes_enabled = true
) noexcept;

void html_content_warning_field(ResponseWriter& r, std::string_view existing_value = "") noexcept;

template <class T>
static void html_sort_select(ResponseWriter& r, std::string_view, T) noexcept;

#define DEF_SORT_SELECT(T, APPLY_TO_ENUM) \
  template <> inline void html_sort_select<T>(ResponseWriter& r, std::string_view name, T value) noexcept { \
    using fmt::operator""_cf; \
    using enum T; \
    r.write_fmt( \
      R"(<select name="{}" id="{}" autocomplete="off">)" APPLY_TO_ENUM(OPTION_HTML) "</select>"_cf, \
      name, name, APPLY_TO_ENUM(OPTION_SELECTED) "" \
    ); \
  }
#define OPTION_HTML(ENUM, NAME) R"(<option value=")" #ENUM R"("{}>)" NAME
#define OPTION_SELECTED(ENUM, NAME) select(value, ENUM),

DEF_SORT_SELECT(SortType, X_SORT_TYPE)
DEF_SORT_SELECT(CommentSortType, X_COMMENT_SORT_TYPE)
DEF_SORT_SELECT(UserPostSortType, X_USER_POST_SORT_TYPE)
DEF_SORT_SELECT(UserSortType, X_USER_SORT_TYPE)
DEF_SORT_SELECT(BoardSortType, X_BOARD_SORT_TYPE)

#undef OPTION_SELECTED
#undef OPTION_HTML
#undef DEF_SORT_SELECT

#define HTML_FIELD(ID, LABEL, TYPE, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"" TYPE "\" name=\"" ID "\" id=\"" ID "\"" EXTRA "></label>"
#define HTML_CHECKBOX(ID, LABEL, EXTRA) \
  "<label for=\"" ID "\"><span>" LABEL "</span><input type=\"checkbox\" class=\"a11y\" name=\"" ID "\" id=\"" ID "\"" EXTRA "><div class=\"toggle-switch\"></div></label>"
#define HTML_TEXTAREA(ID, LABEL, EXTRA, CONTENT) \
  "<label for=\"" ID "\"><span>" LABEL "</span><div><textarea name=\"" ID "\" id=\"" ID "\"" EXTRA ">" CONTENT \
  R"(</textarea><small><a href="https://www.markdownguide.org/cheat-sheet/" rel="nofollow" target="_blank">Markdown</a> formatting is supported.</small></div></label>)"

}