#pragma once
#include "db/db.h++"
#include "views/router_common.h++"
#include "views/webapp/paths.h++"
#include "webapp_common.h++"
#include "controllers/user_controller.h++"
#include "controllers/post_controller.h++"
#include "html/html_user_list.h++"
#include "html/html_notification_list.h++"
#include "html/html_login_forms.h++"
#include "html/html_user_settings_forms.h++"

namespace Ludwig {

template <bool SSL>
void define_user_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<UserController> users,
  std::shared_ptr<PostController> posts
) {
  using std::nullopt, std::optional, std::pair, std::string;
  using fmt::operator""_cf;
  using Coro = RouterCoroutine<Context<SSL>>;

  // USERS LIST
  //////////////////////////////////////////////////////////

  r.get(PATH_USER_list, [users](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto local = req->getQuery("local") == "1";
    const auto sort = parse_user_sort_type(req->getQuery("sort"));
    const auto base_url = format(PATH_USER_list "?local={}&"_cf, local ? "1" : "0");
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = PATH_USER_list,
      .banner_link = PATH_USER_list,
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

  r.get(PATH_login, [](auto* rsp, auto*, auto& c) {
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
      .canonical_path = PATH_login,
      .banner_title = "Login",
    });
    html_login_form(c,
      c.site->setup_done ? nullopt : optional(
        txn.get_admin_list().empty()
          ? "This server is not yet set up. A username and random password should be"
            " displayed in the server's console log. Log in as this user to continue."
          : "This server is not yet set up. Log in as an admin user to continue."));
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form(PATH_login, [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (c.logged_in_user_id) die(403, "Already logged in");
    auto referer = co_await _c.with_request(λx(string(x->getHeader("referer"))));
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
      auto txn = co_await c.app->db->open_write_txn(WritePriority::Low);
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
        ->writeHeader("Location", (referer.empty() || referer == PATH_login || !c.site->setup_done) ? "/" : referer)
        ->end();
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status))
        ->writeHeader("Content-Type", TYPE_HTML);
      html_site_header(c, {
        .canonical_path = PATH_login,
        .banner_title = "Login",
      });
      html_login_form(c, {e.message});
      html_site_footer(c);
      c.finish_write();
    }
  });

  r.get_async(PATH_logout, [](auto* rsp, auto _c) -> Coro {
    auto& c = co_await _c;
    const auto referer = co_await _c.with_request(λx(string(x->getHeader("referer"))));
    if (c.session_id) {
      auto txn = co_await c.app->db->open_write_txn();
      c.app->session_controller->delete_session(txn, *c.session_id);
      txn.commit();
    }
    rsp->writeStatus(http_status(303))
      ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
    if (referer.empty()) rsp->writeHeader("Location", "/");
    else rsp->writeHeader("Location", referer);
    rsp->end();
  });

  // REGISTER
  //////////////////////////////////////////////////////////

  r.get(PATH_register, [](auto* rsp, auto*, auto& c) {
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
      .canonical_path = PATH_register,
      .banner_title = "Register",
    });
    html_register_form(c, c.site);
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form(PATH_register, [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (!c.site->registration_enabled) die(403, "Registration is not enabled on this site");
    if (c.logged_in_user_id) die(403, "Already logged in");
    auto referer = co_await _c.with_request(λx(string(x->getHeader("referer"))));
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
      auto txn = co_await c.app->db->open_write_txn(WritePriority::Low);
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
        .canonical_path = PATH_register,
        .banner_title = "Register",
      });
      html_register_form(c, c.site, {e.message});
      html_site_footer(c);
      c.finish_write();
      co_return;
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = PATH_register,
      .banner_title = "Register",
    });
    c.write(R"(<main><div class="form form-page"><h2>Registration complete!</h2>)"
      R"(<p>Log in to your new account:</p><p><a class="big-button" href=")" PATH_login R"(">Login</a></p>)"
      "</div></main>");
    html_site_footer(c);
    c.finish_write();
  });

  // NOTIFICATIONS
  //////////////////////////////////////////////////////////

  r.get(PATH_notifications, [](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto& login = c.require_login(txn);
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = PATH_notifications,
      .banner_link = PATH_notifications,
      .banner_title = "Notifications",
    });
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    html_notification_list(c, cursor, c.app->session_controller->list_notifications(txn, cursor, login));
    html_site_footer(c);
    c.finish_write();
  });

  r.post(PATH_notifications_read(":id"), [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    const auto [id, referer] = co_await _c.with_request(λx(pair(hex_id_param(x, 0), string(x->getHeader("referer")))));
    const auto user = c.require_login();
    auto txn = co_await c.app->db->open_write_txn();
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

  r.post(PATH_notifications_all_read, [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    const auto referer = co_await _c.with_request(λx(string(x->getHeader("referer"))));
    const auto user = c.require_login();
    auto txn = co_await c.app->db->open_write_txn();
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

# define SETTINGS_PAGE(PATH, TAB, CONTENT) \
    rsp->writeHeader("Content-Type", TYPE_HTML); \
    html_site_header(c, { .canonical_path = PATH, .banner_title = "User Settings" }); \
    c.write("<main>"); \
    html_user_settings_tabs(c, c.site, UserSettingsTab::TAB); \
    CONTENT; \
    c.write("</main>"); \
    html_site_footer(c); \
    c.finish_write();
# define SETTINGS_ROUTE(PATH, TAB, CONTENT) \
    r.get(PATH, [](auto* rsp, auto*, auto& c) { \
      auto txn = c.app->db->open_read_txn(); \
      const auto login = c.require_login(txn); \
      SETTINGS_PAGE(PATH, TAB, CONTENT) \
    });
  SETTINGS_ROUTE(PATH_USER_settings, Settings, html_user_settings_form(c, c.site, login))
  SETTINGS_ROUTE(PATH_USER_profile, Profile, html_user_settings_profile_form(c, c.site, login))
  SETTINGS_ROUTE(PATH_USER_account, Account, html_user_settings_account_form(c, c.site, login))
  SETTINGS_ROUTE(PATH_USER_invites, Invites, html_invites_list(c, *c.app->session_controller, txn, login, ""))
# undef SETTINGS_ROUTE

  r.post_form(PATH_USER_settings, [users](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto txn = co_await c.app->db->open_write_txn();
    const auto login = c.require_login(txn);
    try {
      users->update_local_user(txn, login.id, login.id, user_settings_form_to_update(co_await body));
      txn.commit();
      write_redirect_back(rsp, PATH_USER_settings);
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status));
      SETTINGS_PAGE(PATH_USER_settings, Settings, html_user_settings_form(c, c.site, login, e.message))
    }
  });

  r.post_form(PATH_USER_profile, [users](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto txn = co_await c.app->db->open_write_txn();
    const auto login = c.require_login(txn);
    try {
      users->update_local_user(txn, login.id, login.id, user_settings_profile_form_to_update(co_await body));
      txn.commit();
      write_redirect_back(rsp, PATH_USER_profile);
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status));
      SETTINGS_PAGE(PATH_USER_profile, Profile, html_user_settings_profile_form(c, c.site, login, e.message))
    }
  });

  r.post_form(PATH_USER_change_password, [users](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto txn = co_await c.app->db->open_write_txn();
    const auto login = c.require_login(txn);
    try {
      auto passwords = user_settings_validate_change_password_form(co_await body);
      c.app->session_controller->change_password(txn, login.id, std::move(passwords.first), std::move(passwords.second));
      txn.commit();
      write_redirect_back(rsp, PATH_USER_account);
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status));
      SETTINGS_PAGE(PATH_USER_account, Account, html_user_settings_account_form(c, c.site, login, e.message))
    }
  });

  r.post_form(PATH_USER_delete_account, [users, posts](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto txn = co_await c.app->db->open_write_txn();
    const auto login = c.require_login(txn);
    try {
      auto password_and_delete_posts = user_settings_validate_delete_account_form(co_await body);
      c.app->session_controller->delete_account(txn, login.id, std::move(password_and_delete_posts.first));
      if (password_and_delete_posts.second) {
        std::vector<uint64_t> ids;
        for (const auto id : txn.list_threads_of_user_old(login.id)) ids.push_back(id);
        for (const auto id : ids) posts->delete_thread(txn, id, login.id);
        ids.clear();
        for (const auto id : txn.list_comments_of_user_old(login.id)) ids.push_back(id);
        for (const auto id : ids) posts->delete_comment(txn, id, login.id);
      }
      if (c.session_id) c.app->session_controller->delete_session(txn, *c.session_id);
      txn.commit();
      rsp->writeStatus(http_status(303))
        ->writeHeader("Set-Cookie", COOKIE_NAME "=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT")
        ->writeHeader("Location", "/")
        ->end();
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status));
      SETTINGS_PAGE(PATH_USER_account, Account, html_user_settings_account_form(c, c.site, login, e.message))
    }
  });

  r.post(PATH_USER_new_invite, [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (!c.site->registration_invite_required || c.site->invite_admin_only) {
      die(403, "Users cannot generate invite codes on this server");
    }
    auto txn = co_await c.app->db->open_write_txn();
    const auto login = c.require_login(txn);
    try {
      if (login.mod_state().state >= ModState::Locked) {
        die(403, "User does not have permission to create an invite code");
      }
      c.app->session_controller->create_site_invite(txn, login.id);
      txn.commit();
      write_redirect_back(rsp, PATH_USER_invites);
    } catch (ApiError e) {
      rsp->writeStatus(http_status(e.http_status));
      SETTINGS_PAGE(PATH_USER_invites, Invites, html_invites_list(c, *c.app->session_controller, txn, login, "", e.message))
    }
  });

# undef SETTINGS_PAGE
}

}