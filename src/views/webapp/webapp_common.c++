#include "webapp_common.h++"
#include "html/html_rich_text.h++"

using std::match_results, std::optional, std::pair, std::regex, std::string, std::string_view,
  fmt::operator""_cf; // NOLINT

namespace Ludwig {

const regex GenericContext::cookie_regex(
  R"((?:^|;)\s*)" COOKIE_NAME R"(\s*=\s*([^;]+))",
  regex::ECMAScript
);

auto GenericContext::get_auth_cookie(
  uWS::HttpRequest* req,
  const string& ip
) noexcept -> pair<optional<LoginResponse>, optional<string>> {
  const auto cookies = req->getHeader("cookie");
  match_results<string_view::const_iterator> match;
  if (!regex_search(cookies.begin(), cookies.end(), match, cookie_regex)) return {{}, {}};
  try {
    auto txn = app->db->open_read_txn();
    const auto old_session = stoull(match[1], nullptr, 16);
    auto new_session = app->session_controller->validate_or_regenerate_session(
      txn, old_session, ip, req->getHeader("user-agent")
    );
    if (!new_session) throw std::runtime_error("expired session");
    if (new_session->session_id != old_session) {
      spdlog::debug("Regenerated session {:x} as {:x}", old_session, new_session->session_id);
      return {
        new_session,
        format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}"_cf,
          new_session->session_id, fmt::gmtime(new_session->expiration))
      };
    }
    return {new_session, {}};
  } catch (...) {
    spdlog::debug("Auth cookie is invalid; requesting deletion");
    return {{}, COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT"};
  }
}

void html_site_header(GenericContext& c, HtmlHeaderOptions opt) noexcept {
  assert(c.site != nullptr);
  c.write_cookie();
  if (c.is_htmx) return;
  c.write_fmt(
    R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
    R"(<meta name="viewport" content="width=device-width,initial-scale=1">)"
    R"(<meta name="referrer" content="same-origin"><title>{}{}{}</title>)"
    R"(<style type="text/css">body{}--color-accent:{}!important;--color-accent-dim:{}!important;--color-accent-hover:{}!important;{}</style>)"
    R"(<link rel="stylesheet" href="/static/default-theme.css">)"_cf,
    Escape{c.site->name},
    (opt.page_title || opt.banner_title) ? " - " : "",
    Escape{
      opt.page_title ? *opt.page_title :
      opt.banner_title ? *opt.banner_title :
      ""
    },
    "{",
    c.site->color_accent,
    c.site->color_accent_dim,
    c.site->color_accent_hover,
    "}"
  );
  if (c.site->javascript_enabled) {
    c.write(
      R"(<script src="/static/htmx.min.js"></script>)"
      R"(<script src="/static/ludwig.js"></script>)"
    );
  }
  if (opt.canonical_path) {
    c.write_fmt(
      R"(<link rel="canonical" href="{0}{1}">)"
      R"(<meta property="og:url" content="{0}{1}">)"
      R"(<meta property="twitter:url" content="{0}{1}">)"_cf,
      Escape{c.site->base_url}, Escape{*opt.canonical_path}
    );
  }
  if (opt.page_title) {
    c.write_fmt(
      R"(<meta property="title" href="{0} - {1}">)"
      R"(<meta property="og:title" content="{0} - {1}">)"
      R"(<meta property="twitter:title" content="{0} - {1}">)"
      R"(<meta property="og:type" content="website">)"_cf,
      Escape{c.site->name}, Escape{*opt.page_title}
    );
  }
  if (opt.card_image) {
    c.write_fmt(
      R"(<meta property="og:image" content="{0}">)"
      R"(<meta property="twitter:image" content="{0}>)"
      R"(<meta property="twitter:card" content="summary_large_image">)"_cf,
      Escape{*opt.card_image}
    );
  }
  c.write_fmt(
    R"(</head><body><script>document.body.classList.add("has-js")</script>)"
    R"(<nav class="topbar"><div class="site-name">🎹 {}</div><ul class="quick-boards">)"
    R"(<li><a href="/">Home</a>)"
    R"(<li><a href="/local">Local</a>)"
    R"(<li><a href="/all">All</a>)"
    R"(<li><a href="/boards">Boards</a>)"
    R"(<li><a href="/users">Users</a>)"_cf,
    Escape{c.site->name}
  );
  if (c.login) {
    c.write_fmt(
      R"(</ul><ul>)"
      R"(<li id="topbar-user"><a href="/u/{}">{}</a> ({:d}))"
      R"(<li><a href="/notifications">Notifications ({:d})</a><li><a href="/settings">Settings</a>)"
      R"({}<li><a href="/logout">Logout</a></ul></nav>)"_cf,
      Escape(c.login->user().name()),
      display_name_as_html(c.login->user()),
      c.login->stats().thread_karma() + c.login->stats().comment_karma(),
      c.login->local_user_stats().unread_notification_count(),
      SiteController::can_change_site_settings(c.login) ? R"(<li><a href="/site_admin">Site admin</a>)" : ""
    );
  } else if (c.site->registration_enabled) {
    c.write(R"(</ul><ul><li><a href="/login">Login</a><li><a href="/register">Register</a></ul></nav>)");
  } else {
    c.write(R"(</ul><ul><li><a href="/login">Login</a></ul></nav>)");
  }
  if (c.login) {
    if (c.login->user().mod_state() >= ModState::Locked) {
      c.write(R"(<div id="banner-locked" class="banner">Your account is locked. You cannot post, vote, or subscribe to boards.</div>)");
    }
  }
  c.write(R"(<div id="toasts"></div>)");
  if (opt.banner_title) {
    c.write(R"(<header id="page-header")");
    if (opt.banner_image) {
      c.write_fmt(R"( class="banner-image" style="background-image:url('{}');")"_cf, Escape{*opt.banner_image});
    }
    if (opt.banner_link) {
      c.write_fmt(
        R"(><h1><a class="page-header-link" href="{}">{}</a></h1></header>)"_cf,
        Escape{*opt.banner_link}, Escape{*opt.banner_title}
      );
    } else {
      c.write_fmt("><h1>{}</h1></header>"_cf, Escape{*opt.banner_title});
    }
  }
}

void html_site_footer(GenericContext& c) noexcept {
  if (c.is_htmx) return;
  c.write_fmt(
    R"(<div class="spacer"></div><footer><small>Powered by <a href="https://github.com/ar-nelson/ludwig">Ludwig</a>)"
    R"( · v{})"
#   ifdef LUDWIG_DEBUG
    " (DEBUG BUILD)"
#   endif
    R"( · Generated in {:L}μs</small></footer></body></html>)"_cf,
    VERSION,
    c.time_elapsed()
  );
}

void html_toast(ResponseWriter& r, string_view content, string_view extra_classes) noexcept {
  r.write_fmt(
    R"(<div hx-swap-oob="afterbegin:#toasts">)"
    R"(<p class="toast{}" aria-live="polite" hx-get="data:text/html," hx-trigger="click, every 30s" hx-swap="delete">{}</p>)"
    "</div>"_cf,
    extra_classes, Escape{content}
  );
}

}