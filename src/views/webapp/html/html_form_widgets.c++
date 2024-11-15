#include "html_form_widgets.h++"

using fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_home_page_type_select(ResponseWriter& r, HomePageType selected) noexcept {
  r.write_fmt(R"(<label for="home_page_type"><span>Home page type{}</span>)"
      R"(<select name="home_page_type" id="home_page_type" autocomplete="off">)"
      R"(<option value="Subscribed"{}>Subscribed - Display the user's subscribed boards, or Local boards if not logged in)"
      R"(<option value="Local"{}>Local - Display top content from all boards on this site)"
      R"(<option value="All" disabled{}>All - Display top content from all federated sites (not yet supported))"
      R"(<option value="BoardList"{}>Board List - Display a curated list of boards, like a classic forum)"
      R"(<option value="SingleBoard"{}>Single Board - The site has only one board, which is always the homepage)"
    "</select></label>"_cf,
    selected == HomePageType::SingleBoard ? "<br><strong>Important: Once you select an option other than Single Board, you can never select Single Board again!</strong>" : "",
    select(selected, HomePageType::Subscribed),
    select(selected, HomePageType::Local),
    select(selected, HomePageType::All),
    select(selected, HomePageType::BoardList),
    select(selected, HomePageType::SingleBoard)
  );
}

void html_voting_select(
  ResponseWriter& r,
  bool voting_enabled,
  bool downvotes_enabled,
  bool sitewide_voting_enabled,
  bool sitewide_downvotes_enabled
) noexcept {
  if (!sitewide_voting_enabled) r.write(R"(<input type="hidden" name="voting" value="0">)");
  else r.write_fmt(R"(<label for="voting"><span>Voting</span><select name="voting" autocomplete="off">)"
      R"(<option value="2"{}{}>Rank posts using upvotes and downvotes)"
      R"(<option value="1"{}>Rank posts using only upvotes)"
      R"(<option value="0"{}>No voting, posts can only be ranked by age and comments)"
    R"(</select></label>)"_cf,
    sitewide_downvotes_enabled ? "" : " disabled",
    voting_enabled && downvotes_enabled ? " selected" : "",
    voting_enabled && !downvotes_enabled ? " selected" : "",
    voting_enabled ? "" : " selected"
  );
}

void html_content_warning_field(ResponseWriter& r, std::string_view existing_value) noexcept {
  r.write_fmt(
    R"(<label for="content_warning_toggle" class="js"><span>Content warning</span>)"
    R"(<input type="checkbox" id="content_warning_toggle" name="content_warning_toggle" class="a11y" autocomplete="off" )"
    R"html(onclick="document.querySelector('label[for=content_warning]').setAttribute('class', this.checked ? '' : 'no-js')"{}>)html"
    R"(<div class="toggle-switch"></div>)"
    R"(</label><label for="content_warning"{}>)"
    R"(<span class="no-js">Content warning (optional)</span>)"
    R"(<span class="js">Content warning text</span>)"
    R"(<input type="text" name="content_warning" id="content_warning" autocomplete="off" value="{}">)"
    R"(</label>)"_cf,
    check(!existing_value.empty()),
    existing_value.empty() ? R"( class="no-js")" : "",
    Escape{existing_value}
  );
}

}