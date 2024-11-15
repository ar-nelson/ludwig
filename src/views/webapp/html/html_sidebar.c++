#include "html_sidebar.h++"
#include "html_login_forms.h++"
#include "html_rich_text.h++"
#include "models/local_user.h++"
#include "controllers/site_controller.h++"

using std::monostate, std::nullopt, std::optional, std::string_view, std::variant,
  fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_subscribe_button(ResponseWriter& r, string_view name, bool is_unsubscribe) noexcept {
  r.write_fmt(
    R"(<form method="post" action="/b/{0}/subscribe" hx-post="/b/{0}/subscribe" hx-swap="outerHTML">{1})"
    R"(<button type="submit" class="big-button">{2}</button>)"
    "</form>"_cf,
    Escape{name},
    is_unsubscribe ? R"(<input type="hidden" name="unsubscribe" value="1">)" : "",
    is_unsubscribe ? "Unsubscribe" : "Subscribe"
  );
}

void html_sidebar(
  ResponseWriter& r,
  Login login,
  const SiteDetail* site,
  variant<monostate, const BoardDetail, const UserDetail> detail
) noexcept {
  r.write(
    R"(<label id="sidebar-toggle-label" for="sidebar-toggle">)" ICON("menu") R"( Menu</label>)"
    R"(<input type="checkbox" name="sidebar-toggle" id="sidebar-toggle" class="a11y">)"
    R"(<aside id="sidebar"><section id="search-section"><h2>Search</h2>)"
    R"(<form action="/search" id="search-form">)"
    R"(<label for="search"><span class="a11y">Search</span>)"
    R"(<input type="search" name="search" id="search" placeholder="Search"><input type="submit" value="Search"></label>)"
  );
  const auto hide_cw = login && login->local_user().hide_cw_posts();
  const optional<BoardDetail> board =
    std::holds_alternative<const BoardDetail>(detail) ? optional(std::get<const BoardDetail>(detail)) : nullopt;
  if (board) r.write_fmt(R"(<input type="hidden" name="board" value="{:x}">)"_cf, board->id);
  if (!hide_cw || board) {
    r.write(R"(<details id="search-options"><summary>Search Options</summary><fieldset>)");
    if (board) {
      r.write_fmt(
        R"(<label for="only_board"><input type="checkbox" name="only_board" id="only_board" checked> Limit my search to {}</label>)"_cf,
        display_name_as_html(board->board())
      );
    }
    if (!hide_cw) {
      r.write(R"(<label for="include_cw"><input type="checkbox" name="include_cw" id="include_cw" checked> Include results with Content Warnings</label>)");
    }
    r.write("</fieldset></details>");
  }
  r.write("</form></section>");
  if (!login) {
    r.write(R"(<section id="login-section"><h2>Login</h2>)");
    html_sidebar_login_form(r);
    if (site->registration_enabled) r.write(R"(<a href="/register" class="big-button">Register</a>)");
    r.write("</section>");
  } else {
    visit(overload{
      [&](monostate) {
        if (SiteController::can_create_board(login, *site)) {
          r.write(
            R"(<section id="actions-section"><h2>Actions</h2>)"
            R"(<a class="big-button" href="/create_board">Create a new board</a>)"
            R"(</section>)"
          );
        }
      },
      [&](const BoardDetail& board) {
        r.write(R"(<section id="actions-section"><h2>Actions</h2>)");
        html_subscribe_button(r, board.board().name()->string_view(), board.subscribed);
        if (board.can_create_thread(login)) {
          r.write_fmt(
            R"(<a class="big-button" href="/b/{0}/create_thread">Submit a new link</a>)"
            R"(<a class="big-button" href="/b/{0}/create_thread?text=1">Submit a new text post</a>)"_cf,
            Escape(board.board().name())
          );
        }
        if (board.can_change_settings(login)) {
          r.write_fmt(
            R"(<a class="big-button" href="/b/{0}/settings">Board settings</a>)"_cf,
            Escape(board.board().name())
          );
        }
        r.write("</section>");
      },
      [&](const UserDetail&) {}
    }, detail);
  }

  visit(overload{
    [&](monostate) {
      r.write_fmt(R"(<section id="site-sidebar"><h2>{}</h2>)"_cf, Escape{site->name});
      if (site->banner_url) {
        r.write_fmt(
          R"(<div class="sidebar-banner"><img src="{}" alt="{} banner"></div>)"_cf,
          Escape{*site->banner_url}, Escape{site->name}
        );
      }
      r.write_fmt("<p>{}</p>"_cf, Escape{site->description});
    },
    [&](const BoardDetail& board) {
      r.write_fmt(R"(<section id="board-sidebar"><h2>{}</h2>)"_cf, display_name_as_html(board.board()));
      // TODO: Banner image
      if (board.board().description_type() && board.board().description_type()->size()) {
        r.write_fmt(R"(<div class="markdown">{}</div>)"_cf, rich_text_to_html(
          board.board().description_type(),
          board.board().description(),
          { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
        ));
      }
    },
    [&](const UserDetail& user) {
      r.write_fmt(R"(<section id="user-sidebar"><h2>{}</h2>)"_cf, display_name_as_html(user.user()));
      if (user.user().bio_type() && user.user().bio_type()->size()) {
        r.write_fmt(R"(<div class="markdown">{}</div>)"_cf, rich_text_to_html(
          user.user().bio_type(),
          user.user().bio(),
          { .open_links_in_new_tab = login && login->local_user().open_links_in_new_tab() }
        ));
      }
    }
  }, detail);

  return r.write("</section></aside>");
}

}