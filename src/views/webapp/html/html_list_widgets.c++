#include "html_list_widgets.h++"

using std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

void html_show_threads_toggle(ResponseWriter& r, bool show_threads) noexcept {
  r.write_fmt(
    R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
    R"(<input class="a11y" name="type" type="radio" value="threads" id="type-threads"{}><label for="type-threads" class="toggle-button">Threads</label>)"
    R"(<input class="a11y" name="type" type="radio" value="comments" id="type-comments"{}><label for="type-comments" class="toggle-button">Comments</label></fieldset>)"_cf,
    check(show_threads), check(!show_threads)
  );
}

void html_local_toggle(ResponseWriter& r, bool local_only) noexcept {
  r.write_fmt(
    R"(<fieldset class="toggle-buttons"><legend class="a11y">Show</legend>)"
    R"(<input class="a11y" name="local" type="radio" value="1" id="local-1"{}><label for="local-1" class="toggle-button">Local</label>)"
    R"(<input class="a11y" name="local" type="radio" value="0" id="local-0"{}><label for="local-0" class="toggle-button">All</label></fieldset>)"_cf,
    check(local_only), check(!local_only)
  );
}

void html_show_images_toggle(ResponseWriter& r, bool show_images) noexcept {
  r.write_fmt(
    R"(</label><label for="images"><input class="a11y" name="images" id="images" type="checkbox" value="1"{}><div class="toggle-switch"></div> Images</label>)"
    R"(<input class="no-js" type="submit" value="Apply"></form>)"_cf,
    check(show_images)
  );
}

void html_subscribed_toggle(ResponseWriter& r, bool show_images) noexcept {
  r.write_fmt(
    R"(</label><label for="sub"><input class="a11y" name="sub" id="sub" type="checkbox" value="1"{}><div class="toggle-switch"></div> Subscribed Only</label>)"
    R"(<input class="no-js" type="submit" value="Apply"></form>)"_cf,
    check(show_images)
  );
}

void html_pagination(
  ResponseWriter& r,
  string_view base_url,
  bool is_first,
  PageCursor next,
  bool infinite_scroll_enabled
) noexcept {
  const auto sep = base_url.find('?') == string_view::npos ? "?" : "&amp;";
  r.write(R"(<div class="pagination" id="pagination" hx-swap-oob="true")");
  if (next && infinite_scroll_enabled) {
    r.write_fmt(
      R"( hx-get="{}{}from={}" hx-target="#top-level-list" hx-swap="beforeend" hx-trigger="revealed">)"_cf,
      Escape{base_url}, sep, next.to_string()
    );
  } else r.write(">");
  if (!is_first) {
    r.write_fmt(R"(<a class="big-button no-js" href="{}">← First</a>)"_cf, Escape{base_url});
  }
  if (next) {
    r.write_fmt(
      R"(<a class="big-button no-js" href="{0}{1}from={2}">Next →</a>)"
      R"(<a class="more-link js" href="{0}{1}from={2}" hx-get="{0}{1}from={2}" hx-target="#top-level-list" hx-swap="beforeend">Load more…</a>)"_cf,
      Escape{base_url}, sep, next.to_string()
    );
  }
  r.write(R"(<div class="spinner">Loading…</div></div>)");
}

}