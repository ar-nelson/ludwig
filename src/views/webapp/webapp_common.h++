#pragma once
#include "fmt/compile.h"
#include "uWebSockets/HttpParser.h"
#include "views/router_common.h++"
#include "views/webapp/html/html_common.h++"
#include "util/rate_limiter.h++"
#include "controllers/session_controller.h++"

namespace Ludwig {

#define COOKIE_NAME "ludwig_session"

static inline void die(uint16_t status, const char* message) {
  throw ApiError(message, status);
}

struct WebappState {
  std::shared_ptr<DB> db;
  std::shared_ptr<SessionController> session_controller;
  std::shared_ptr<SiteController> site_controller;
  std::shared_ptr<KeyedRateLimiter> rate_limiter; // may be null!
};

struct GenericContext : public ResponseWriter {
  static const std::regex cookie_regex;

  std::chrono::steady_clock::time_point start;
  std::optional<uint64_t> logged_in_user_id;
  std::optional<std::string> session_cookie;
  std::string ip;
  bool is_htmx;
  const SiteDetail* site = nullptr;
  WebappState* app = nullptr;
  std::optional<LocalUserDetail> login;

  auto populate(ReadTxn& txn) {
    if (logged_in_user_id) {
      if (*logged_in_user_id) login.emplace(LocalUserDetail::get_login(txn, *logged_in_user_id));
      else if (!site->setup_done) {
        spdlog::warn("Using temporary admin user");
        login.emplace(LocalUserDetail::temp_admin());
      }
    }
  }

  auto require_login() {
    if (!logged_in_user_id) die(401, "Login is required");
    const auto id = *logged_in_user_id;
    if (!id && site->setup_done) die(401, "Site is set up, temporary login is no longer valid");
    return id;
  }

  auto require_login(ReadTxn& txn) -> const LocalUserDetail& {
    if (!logged_in_user_id) die(401, "Login is required");
    if (!login) populate(txn);
    if (!login) die(401, "Site is set up, temporary login is no longer valid");
    return *login;
  }

  auto time_elapsed() const noexcept {
    using namespace std::chrono;
    const auto end = steady_clock::now();
    return duration_cast<microseconds>(end - start).count();
  }

  auto get_auth_cookie(uWS::HttpRequest* req, const std::string& ip) noexcept ->
    std::pair<std::optional<LoginResponse>, std::optional<std::string>>;

  virtual auto write_cookie() const noexcept -> void = 0;
};

template <bool SSL>
struct Context : public RequestContext<SSL, std::shared_ptr<WebappState>>, public GenericContext {
  using Request = uWS::HttpRequest*;
  using Response = uWS::HttpResponse<SSL>*;

  Response rsp;

  void pre_try(const uWS::HttpResponse<SSL>* rsp, Request req) noexcept override {
    start = std::chrono::steady_clock::now();
    is_htmx = !req->getHeader("hx-request").empty() && req->getHeader("hx-boosted").empty();
  }

  void pre_request(Response rsp, Request req, std::shared_ptr<WebappState> app) override {
    using namespace std::chrono;
    spdlog::info("url={} is_htmx={:d}", this->url, is_htmx);
    this->rsp = rsp;
    this->app = app.get();
    ip = get_ip(rsp, req);

    if (app->rate_limiter && !app->rate_limiter->try_acquire(ip, this->method == "GET" ? 1 : 10)) {
      die(429, "Rate limited, try again later");
    }

    const auto [new_session, cookie] = get_auth_cookie(req, ip);
    session_cookie = cookie;
    site = app->site_controller->site_detail();
    if (!new_session) {
      if (site->require_login_to_view && this->url != "/login") {
        die(401, "Login is required to view this page");
      }
      if (!site->setup_done && this->url != "/login") {
        die(401, "First-run setup is not complete. Log in as an admin user to complete site setup. If no admin user exists, check console output for a randomly-generated password.");
      }
    } else if (!site->setup_done) {
      if (this->url != "/" && this->url != "/login" && this->url != "/logout" && this->url != "/site_admin/first_run_setup") {
        die(403, "First-run setup is not complete. This page is not yet accessible.");
      }
    }

    logged_in_user_id = new_session.transform(λx(x.user_id));
  }

  void error_response(const ApiError& e, Response rsp) noexcept override;

  auto write_cookie() const noexcept -> void override {
    if (session_cookie) rsp->writeHeader("Set-Cookie", *session_cookie);
  }

  auto finish_write() -> void override {
    rsp->end(this->buf);
  }
};

struct HtmlHeaderOptions {
  std::optional<std::string_view> canonical_path, banner_link, page_title;
  std::optional<std::string> banner_title, banner_image, card_image;
};

void html_site_header(GenericContext& c, HtmlHeaderOptions opt) noexcept;

void html_site_footer(GenericContext& c) noexcept;

template <bool SSL>
void Context<SSL>::error_response(const ApiError& e, Response rsp) noexcept {
  using fmt::operator""_cf;
  if (!is_htmx) {
    if (this->method == "get" && e.http_status == 401) {
      rsp->writeStatus(http_status(303))
        ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT")
        ->writeHeader("Location", "/login")
        ->end();
      return;
    } else if (app) {
      try {
        auto txn = app->db->open_read_txn();
        populate(txn);
        rsp->writeStatus(http_status(e.http_status));
        html_site_header(*this, {});
        this->write_fmt(R"(<main><div class="error-page"><h2>Error {}</h2><p>{}</p></div></main>)"_cf, http_status(e.http_status), e.message);
        html_site_footer(*this);
        this->finish_write();
        return;
      } catch (...) {
        spdlog::warn("Error when rendering error page");
      }
    }
  }
  rsp->writeStatus(http_status(e.http_status))
    ->writeHeader("Content-Type", TYPE_HTML)
    ->end(format("Error {:d}: {}"_cf, e.http_status, Escape(e.message)));
}

void html_toast(ResponseWriter& r, std::string_view content, std::string_view extra_classes = "") noexcept;

template <bool SSL>
static inline auto write_redirect_to(uWS::HttpResponse<SSL>* rsp, const Context<SSL>& c, std::string_view location) noexcept -> void {
  if (c.is_htmx) {
    rsp->writeStatus(http_status(204))
      ->writeHeader("HX-Redirect", location);
  } else {
    rsp->writeStatus(http_status(303))
      ->writeHeader("Location", location);
  }
  rsp->end();
}

template <bool SSL>
static inline auto write_redirect_back(uWS::HttpResponse<SSL>* rsp, std::string_view referer) noexcept -> void {
  if (referer.empty()) {
    rsp->writeStatus(http_status(202));
  } else {
    rsp->writeStatus(http_status(303))
      ->writeHeader("Location", referer);
  }
  rsp->end();
}

}