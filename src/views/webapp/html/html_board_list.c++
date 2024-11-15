#include "html_board_list.h++"
#include "html_list_widgets.h++"
#include "html_post_widgets.h++"
#include "html_rich_text.h++"

using std::generator, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_board_list_entry(ResponseWriter& r, const BoardDetail& entry) noexcept {
  r.write(R"(<li class="board-list-entry"><div class="board-list-desc"><p class="board-list-name">)");
  html_board_link(r, entry.board());
  if (entry.board().display_name() && entry.board().display_name()->size()) {
    r.write_fmt(R"(</p><p class="account-name"><small>{}</small>)"_cf, Escape{entry.board().name()});
  }
  r.write_fmt(
    R"(</p><p>{}</p></div><div class="board-list-stats"><dl>)"
    R"(<dt>Subscribers</dt><dd>{:d}</dd>)"
    R"(<dt>Threads</dt><dd>{:d}</dd>)"
    R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)"_cf,
    rich_text_to_html(entry.board().description_type(), entry.board().description()),
    entry.stats().subscriber_count(),
    entry.stats().thread_count(),
    RelativeTime{uint_to_timestamp(entry.stats().latest_post_time())}
  );
}

void html_board_list(
  GenericContext& r,
  PageCursor& cursor,
  generator<const BoardDetail&> entries,
  string_view base_url,
  BoardSortType sort,
  bool local_only
) {
  bool is_first_page = !cursor.exists;
  if (!r.is_htmx) {
    r.write(R"(<section><h2 class="a11y">Sort and filter</h2>)");
    html_sort_options(r, base_url, sort, local_only, false);
    r.write(R"(</section><main><ol class="board-list" id="top-level-list">)");
  }
  bool any_entries = false;
  for (const auto& e : entries) {
    html_board_list_entry(r, e);
    any_entries = true;
  }
  if (!r.is_htmx) {
    if (!any_entries) r.write(R"(<li class="no-entries">There's nothing here.)");
    r.write("</ol>");
  }
  html_pagination(r, fmt::format("{}sort={}&"_cf, base_url, to_string(sort)), is_first_page, cursor);
  if (!r.is_htmx) r.write("</main>");
}

}