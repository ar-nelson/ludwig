#pragma once
#include "db/db.h++"
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/user_controller.h++"
#include "html/html_user_list.h++"
#include "html/html_notification_list.h++"
#include "html/html_login_forms.h++"
#include "html/html_user_settings_forms.h++"

namespace Ludwig {

template <bool SSL>
void define_user_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<UserController> users
) {
  using fmt::operator""_cf;
  using Coro = RouterCoroutine<Context<SSL>>;

  // USERS LIST
  //////////////////////////////////////////////////////////

  r.get("/users", [users](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto local = req->getQuery("local") == "1";
    const auto sort = parse_user_sort_type(req->getQuery("sort"));
    const auto base_url = format("/users?local={}&"_cf, local ? "1" : "0");
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/users",
      .banner_link = "/users",
      .banner_title = "Users",
    });
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    html_user_list(c, cursor, users->list_users(txn, cursor, sort, local, c.login), base_url, sort);
    html_site_footer(c);
    c.finish_write();
  });

  // LOGIN/LOGOUT
  //////////////////////////////////////////////////////////

  r.get("/login", [](auto* rsp, auto*, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    if (c.login) {
      rsp->writeStatus(http_status(303))
        ->writeHeader("Location", "/")
        ->end();
      return;
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/login",
      .banner_title = "Login",
    });
    html_login_form(c,
      c.site->setup_done ? std::nullopt : std::optional(
        txn.get_admin_list().empty()
          ? "This server is not yet set up. A username and random password should be"
            " displayed in the server's console log. Log in as this user to continue."
          : "This server is not yet set up. Log in as an admin user to continue."));
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form("/login", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (c.logged_in_user_id) die(403, "Already logged in");
    auto referer = co_await _c.with_request([](auto* req){ return std::string(req->getHeader("referer")); });
    auto form = co_await body;
    if (form.optional_string("username") /* actually a honeypot */) {
      spdlog::warn("Caught a bot with honeypot field on login");
      rsp->writeStatus(http_status(418))->end();
      co_return;
    }
    bool remember = form.optional_bool("remember");
    try {
      // Logins have low priority because anyone can initiate them.
      // This prevents login spam from DOS'ing other actions.
      auto txn = co_await open_write_txn<Context<SSL>>(c.app->db, WritePriority::Low);
      auto login = c.app->session_controller->login(
        txn,
        form.required_string("actual_username"),
        form.required_string("password"),
        c.ip,
        c.user_agent,
        remember
      );
      txn.commit();
      rsp->writeStatus(http_status(303))
        ->writeHeader("Set-Cookie",
          format(COOKIE_NAME "={:x}; path=/; expires={:%a, %d %b %Y %T %Z}"_cf,
            login.session_id, fmt::gmtime(login.expiration)))
        ->writeHeader("Location", (referer.empty() || referer == "/login" || !c.site->setup_done) ? "/" : referer)
        ->end();
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status))
        ->writeHeader("Content-Type", TYPE_HTML);
      html_site_header(c, {
        .canonical_path = "/login",
        .banner_title = "Login",
      });
      html_login_form(c, {e.message});
      html_site_footer(c);
      c.finish_write();
    }
  });

  r.get("/logout", [](auto* rsp, auto* req, auto&) {
    rsp->writeStatus(http_status(303))
      ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
    if (req->getHeader("referer").empty()) rsp->writeHeader("Location", "/");
    else rsp->writeHeader("Location", req->getHeader("referer"));
    rsp->end();
  });

  // REGISTER
  //////////////////////////////////////////////////////////

  r.get("/register", [](auto* rsp, auto*, auto& c) {
    if (!c.site->registration_enabled) die(403, "Registration is not enabled on this site");
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    if (c.login) {
      rsp->writeStatus(http_status(303))
        ->writeHeader("Location", "/")
        ->end();
      return;
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/register",
      .banner_title = "Register",
    });
    html_register_form(c, c.site);
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form("/register", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (!c.site->registration_enabled) die(403, "Registration is not enabled on this site");
    if (c.logged_in_user_id) die(403, "Already logged in");
    auto referer = co_await _c.with_request([](auto* req){ return std::string(req->getHeader("referer")); });
    auto form = co_await body;
    if (form.optional_string("username") /* actually a honeypot */) {
      spdlog::warn("Caught a bot with honeypot field on register");
      rsp->writeStatus(http_status(418))->end();
      co_return;
    }
    try {
      SecretString password = form.required_string("password"),
        confirm_password = form.required_string("confirm_password");
      if (password.data != confirm_password.data) {
        die(400, "Passwords do not match");
      }
      // Registrations have low priority because anyone can initiate them.
      // This prevents registration spam from DOS'ing other actions.
      auto txn = co_await open_write_txn<Context<SSL>>(c.app->db, WritePriority::Low);
      c.app->session_controller->register_local_user(
        txn,
        form.required_string("actual_username"),
        form.required_string("email"),
        std::move(password),
        rsp->getRemoteAddressAsText(),
        c.user_agent,
        form.optional_string("invite_code").and_then(invite_code_to_id),
        form.optional_string("application_reason")
      );
      txn.commit();
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status))
        ->writeHeader("Content-Type", TYPE_HTML);
      html_site_header(c, {
        .canonical_path = "/register",
        .banner_title = "Register",
      });
      html_register_form(c, c.site, {e.message});
      html_site_footer(c);
      c.finish_write();
      co_return;
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/register",
      .banner_title = "Register",
    });
    c.write(R"(<main><div class="form form-page"><h2>Registration complete!</h2>)"
      R"(<p>Log in to your new account:</p><p><a class="big-button" href="/login">Login</a></p>)"
      "</div></main>");
    html_site_footer(c);
    c.finish_write();
  });

  // NOTIFICATIONS
  //////////////////////////////////////////////////////////

  r.get("/notifications", [](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto& login = c.require_login(txn);
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/notifications",
      .banner_link = "/notifications",
      .banner_title = "Notifications",
    });
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    html_notification_list(c, cursor, c.app->session_controller->list_notifications(txn, cursor, login));
    html_site_footer(c);
    c.finish_write();
  });

  r.post("/notifications/:id/read", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    const auto [id, referer] = co_await _c.with_request([](auto* req) {
      return std::pair(hex_id_param(req, 0), std::string(req->getHeader("referer")));
    });
    const auto user = c.require_login();
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    c.app->session_controller->mark_notification_read(txn, user, id);
    if (c.is_htmx) {
      c.populate(txn);
      rsp->writeHeader("Content-Type", TYPE_HTML);
      PageCursor cur;
      html_notification(c, NotificationDetail::get(txn, id, *c.login), *c.login);
      c.finish_write();
    } else {
      write_redirect_back(rsp, referer);
    }
    txn.commit();
  });

  r.post("/notifications/all_read", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    const auto referer = co_await _c.with_request([](auto* req) { return std::string(req->getHeader("referer")); });
    const auto user = c.require_login();
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    c.app->session_controller->mark_all_notifications_read(txn, user);
    if (c.is_htmx) {
      c.populate(txn);
      rsp->writeHeader("Content-Type", TYPE_HTML);
      PageCursor cursor;
      html_notification_list(c, cursor, c.app->session_controller->list_notifications(txn, cursor, *c.login));
      c.finish_write();
    } else {
      write_redirect_back(rsp, referer);
    }
    txn.commit();
  });

  // USER SETTINGS
  //////////////////////////////////////////////////////////

# define SETTINGS_ROUTE(PATH, TAB, CONTENT) \
    r.get(PATH, [](auto* rsp, auto*, auto& c) { \
      auto txn = c.app->db->open_read_txn(); \
      const auto login = c.require_login(txn); \
      rsp->writeHeader("Content-Type", TYPE_HTML); \
      html_site_header(c, { \
        .canonical_path = PATH, \
        .banner_title = "User Settings", \
      }); \
      c.write("<main>"); \
      html_user_settings_tabs(c, c.site, UserSettingsTab::TAB); \
      CONTENT; \
      c.write("</main>"); \
      html_site_footer(c); \
      c.finish_write(); \
    });
  SETTINGS_ROUTE("/settings", Settings, html_user_settings_form(c, c.site, login))
  SETTINGS_ROUTE("/settings/profile", Profile, html_user_settings_profile_form(c, c.site, login))
  SETTINGS_ROUTE("/settings/account", Account, html_user_settings_account_form(c, c.site, login))
  SETTINGS_ROUTE("/settings/invites", Invites, html_invites_list(c, *c.app->session_controller, txn, login, ""))
# undef SETTINGS_ROUTE

  // TODO: POST /settings
  // TODO: POST /settings/profile
  // TODO: POST /settings/account/change_password
  // TODO: POST /settings/account/delete

  r.post("/settings/invites/new", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (!c.site->registration_invite_required || c.site->invite_admin_only) {
      die(403, "Users cannot generate invite codes on this server");
    }
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    const auto login = c.require_login(txn);
    if (login.mod_state().state >= ModState::Locked) {
      die(403, "User does not have permission to create an invite code");
    }
    c.app->session_controller->create_site_invite(txn, login.id);
    txn.commit();
    write_redirect_back(rsp, "/settings/invites");
  });
}

}