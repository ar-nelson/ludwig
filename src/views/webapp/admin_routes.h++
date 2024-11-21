#pragma once
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/first_run_controller.h++"
#include "controllers/dump_controller.h++"
#include "html/html_site_admin_forms.h++"
#include "html/html_first_run_setup_form.h++"
#include "html/html_user_settings_forms.h++"
#include <memory>

namespace Ludwig {

static inline auto require_admin(GenericContext& c) {
  auto txn = c.app->db->open_read_txn();
  const auto login = c.require_login(txn);
  if (!SiteController::can_change_site_settings(login)) {
    die(403, "Admin login required to perform this action");
  }
}

template <bool SSL>
void define_admin_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<FirstRunController> first_run,
  std::shared_ptr<DumpController> dump
) {
  using fmt::operator""_cf;
  using Coro = RouterCoroutine<Context<SSL>>;

# define ADMIN_PAGE(PATH, TAB, CONTENT) \
  rsp->writeHeader("Content-Type", TYPE_HTML); \
  html_site_header(c, { \
    .canonical_path = PATH, \
    .banner_title = "Site Admin", \
  }); \
  c.write("<main>"); \
  html_site_admin_tabs(c, c.site, SiteAdminTab::TAB); \
  CONTENT; \
  c.write("</main>"); \
  html_site_footer(c); \
  c.finish_write();
# define ADMIN_ROUTE(PATH, TAB, CONTENT) r.get(PATH, [](auto* rsp, auto*, auto& c) { \
    auto txn = c.app->db->open_read_txn(); \
    const auto login = c.require_login(txn); \
    if (!SiteController::can_change_site_settings(login)) { \
      die(403, "Admin login required to view this page"); \
    } \
    ADMIN_PAGE(PATH, TAB, CONTENT) \
  });
  ADMIN_ROUTE("/site_admin", Settings, html_site_admin_form(c, c.site))
  ADMIN_ROUTE("/site_admin/import_export", ImportExport, html_site_admin_import_export_form(c))
  ADMIN_ROUTE("/site_admin/applications", Applications, html_site_admin_applications_list(c, *c.app->session_controller, txn, c.login))
  ADMIN_ROUTE("/site_admin/invites", Invites, html_invites_list(c, *c.app->session_controller, txn, login, ""))

  r.post_form("/site_admin", [](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    require_admin(c);
    auto form = co_await body;
    try {
      c.app->site_controller->update_site(
        co_await c.app->db->open_write_txn(),
        form_to_site_update(form),
        c.logged_in_user_id
      );
      write_redirect_back(rsp, "/site_admin");
    } catch (const ApiError& e) {
      rsp->writeStatus(http_status(e.http_status));
      ADMIN_PAGE("/site_admin", Settings, html_site_admin_form(c, c.site, {e.message}))
    }
  });

  r.post_form("/site_admin/first_run_setup", [first_run](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    if (c.site->setup_done) {
      die(403, "First-run setup is already complete");
    }
    require_admin(c);
    auto form = co_await body;
    try {
      first_run->first_run_setup(co_await c.app->db->open_write_txn(), {
        form_to_site_update(form),
        form.optional_string("base_url"),
        form.optional_string("default_board_name"),
        form.optional_string("admin_username"),
        form.optional_string("admin_password").transform(λx(SecretString(x)))
      }, *c.logged_in_user_id);
      write_redirect_back(rsp, "/");
    } catch (const ApiError& e) {
      auto txn = c.app->db->open_read_txn();
      rsp->writeStatus(http_status(e.http_status))
        ->writeHeader("Content-Type", TYPE_HTML);
      html_site_header(c, {
        .canonical_path = "/",
        .banner_title = "First-Run Setup",
      });
      html_first_run_setup_form(c, first_run->first_run_setup_options(txn), e.message);
      html_site_footer(c);
      c.finish_write();
    }
  });

  r.post("/site_admin/export", [dump](auto* rsp, auto _c, auto) -> Coro {
    auto& c = co_await _c;
    require_admin(c);
    rsp->writeHeader("Content-Type", "application/zstd")
      ->writeHeader(
        "Content-Disposition",
        format(R"(attachment; filename="ludwig-{:%F-%H%M%S}.dbdump.zst")"_cf, now_t())
      );
    auto done = std::make_shared<CompletableOnce<std::monostate>>();
    std::thread([&, done] {
      spdlog::info("Beginning database dump");
      std::binary_semaphore lock(0);
      try {
        auto txn = c.app->db->open_read_txn();
        for (auto chunk : dump->export_dump(txn)) {
          if (done->is_canceled()) return;
          c.on_response_thread([&](auto* rsp) {
            if (!done->is_canceled()) {
              rsp->write(std::string_view{(const char*)chunk.data(), chunk.size()});
            }
            lock.release();
          });
          lock.acquire();
        }
        spdlog::info("Database dump completed successfully");
        done->complete({});
      } catch (const std::exception& e) {
        spdlog::error("Database dump failed: {}", e.what());
        done->cancel();
      }
    }).detach();
    co_await done;
    rsp->end();
  });

  r.post("/site_admin/applications/:action/:id", [](auto* rsp, auto _c, auto) -> Coro {
    auto [is_approve, id] = co_await _c.with_request([](auto* req) {
      bool is_approve;
      if (req->getParameter(0) == "approve") is_approve = true;
      else if (req->getParameter(0) == "reject") is_approve = false;
      else die(404, "Page not found");
      return std::pair(is_approve, hex_id_param(req, 1));
    });
    auto& c = co_await _c;
    require_admin(c);
    auto txn = co_await c.app->db->open_write_txn();
    try {
      if (is_approve) {
        c.app->session_controller->approve_local_user_application(txn, id, c.logged_in_user_id);
      } else {
        c.app->session_controller->reject_local_user_application(txn, id, c.logged_in_user_id);
      }
      txn.commit();
      write_redirect_back(rsp, "/site_admin/applications");
    } catch (const ApiError& e) {
      rsp->writeStatus(http_status(e.http_status));
      ADMIN_PAGE("/site_admin/applications", Applications, html_site_admin_applications_list(c, *c.app->session_controller, txn, c.login, {}, e.message))
    }
  });

  r.post("/site_admin/invites/new", [](auto* rsp, auto _c, auto) -> Coro {
    auto& c = co_await _c;
    require_admin(c);
    auto txn = co_await c.app->db->open_write_txn();
    c.app->session_controller->create_site_invite(txn, c.logged_in_user_id);
    txn.commit();
    write_redirect_back(rsp, "/site_admin/invites");
  });
}

}