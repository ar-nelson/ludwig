#include "html_user_list.h++"
#include "html_list_widgets.h++"
#include "html_post_widgets.h++"
#include "html_rich_text.h++"

using std::generator, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_user_list_entry(ResponseWriter& r, const UserDetail& entry, Login login) noexcept {
  r.write(R"(<li class="user-list-entry"><div class="user-list-desc"><p class="user-list-name">)");
  html_user_link(r, entry.user(), entry.maybe_local_user().transform(λx(x.get().admin())).value_or(false), login);
  if (entry.user().display_name() && entry.user().display_name()->size()) {
    r.write_fmt(R"(</p><p class="account-name"><small>{}</small>)"_cf, Escape{entry.user().name()});
  }
  r.write_fmt(
    R"(</p><p>{}</p></div><div class="user-list-stats"><dl>)"
    R"(<dt>Threads</dt><dd>{:d}</dd>)"
    R"(<dt>Comments</dt><dd>{:d}</dd>)"
    R"(<dt>Last Activity</dt><dd>{}</dd></dl></div></li>)"_cf,
    rich_text_to_html(entry.user().bio_type(), entry.user().bio()),
    entry.stats().thread_count(),
    entry.stats().comment_count(),
    RelativeTime{uint_to_timestamp(entry.stats().latest_post_time())}
  );
}

void html_user_list(
  GenericContext& r,
  PageCursor& cursor,
  generator<const UserDetail&> entries,
  string_view base_url,
  UserSortType sort,
  bool local_only
) {
  bool is_first_page = !cursor.exists;
  if (!r.is_htmx) {
    r.write(R"(<section><h2 class="a11y">Sort and filter</h2>)");
    html_sort_options(r, base_url, sort, local_only, false);
    r.write(R"(</section><main><ol class="user-list" id="top-level-list">)");
  }
  bool any_entries = false;
  for (const auto& e : entries) {
    html_user_list_entry(r, e, r.login);
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