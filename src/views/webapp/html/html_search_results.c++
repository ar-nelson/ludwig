#include "html_search_results.h++"
#include "html_list_widgets.h++"
#include "html_post_widgets.h++"
#include "html_rich_text.h++"
#include "html_thread_entry.h++"
#include "html_comment_entry.h++"

using std::vector, std::visit, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_search_result(
  ResponseWriter& r,
  const SearchResultDetail& entry,
  const SiteDetail* site,
  Login login,
  bool show_images
) noexcept {
  visit(overload{
    [&](const UserDetail& user) {
      r.write("<li>");
      html_user_link(r, user.user(), user.maybe_local_user().transform(λx(x.get().admin())).value_or(false), login);
    },
    [&](const BoardDetail& board) {
      r.write("<li>");
      html_board_link(r, board.board());
    },
    [&](const ThreadDetail& thread) {
      html_thread_entry(r, thread, site, login, PostContext::Feed, show_images);
    },
    [&](const CommentDetail& comment) {
      html_comment_entry(r, comment, site, login, PostContext::Feed, show_images);
    },
  }, entry);
}

void html_search_result_list(
  GenericContext& r,
  vector<SearchResultDetail> entries,
  bool show_images
) {
  if (!r.is_htmx) r.write(R"(<ol class="search-list" id="top-level-list">)");
  for (const auto& entry : entries) {
    html_search_result(r, entry, r.site, r.login, show_images);
  }
  if (!r.is_htmx) r.write("</ol>");
}

}