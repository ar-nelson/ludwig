#pragma once
#include "../test_common.h++"
#include "util/rich_text.h++"
#include "services/asio_http_client.h++"
#include "services/asio_event_bus.h++"
#include "db/db.h++"
#include "controllers/board_controller.h++"
#include "controllers/dump_controller.h++"
#include "controllers/first_run_controller.h++"
#include "controllers/lemmy_api_controller.h++"
#include "controllers/post_controller.h++"
#include "controllers/remote_media_controller.h++"
#include "controllers/search_controller.h++"
#include "controllers/session_controller.h++"
#include "controllers/site_controller.h++"
#include "controllers/user_controller.h++"
#include "views/webapp/routes.h++"
#include "views/media_routes.h++"
#include "views/lemmy_api_routes.h++"

using std::promise, std::unique_ptr;

namespace Ludwig {

class IntegrationTest {
  TempFile dbfile;
  AsioThreadPool pool;
  us_listen_socket_t* app_socket = nullptr;
  shared_ptr<LibXmlContext> xml;

public:
  static constexpr inline char first_run_admin_password[] = "first-run";
  string base_url;
  AsioHttpClient http;
  shared_ptr<MockHttpClient> outer_http;
  shared_ptr<DB> db;
  shared_ptr<SiteController> site;
  shared_ptr<UserController> users;
  shared_ptr<SessionController> sessions;
  shared_ptr<BoardController> boards;
  shared_ptr<PostController> posts;
  shared_ptr<SearchController> search;
  shared_ptr<FirstRunController> first_run;
  IntegrationTest() :
    pool(1),
    xml(make_shared<LibXmlContext>()),
    http(pool.io, 100000, UnsafeHttps::UNSAFE, UnsafeLocalRequests::UNSAFE),
    outer_http(make_shared<MockHttpClient>())
  {
    db = make_shared<DB>(dbfile.name, 100, true);
    auto rate_limiter = make_shared<KeyedRateLimiter>(10, 3000);
    auto event_bus = make_shared<AsioEventBus>(pool.io);
    auto xml_ctx = make_shared<LibXmlContext>();
    site = make_shared<SiteController>(db, event_bus);
    boards = make_shared<BoardController>(site, event_bus);
    users = make_shared<UserController>(site, event_bus);
    posts = make_shared<PostController>(site, event_bus);
    search = make_shared<SearchController>(db, nullptr, event_bus);
    sessions = make_shared<SessionController>(db, site, users, SecretString(first_run_admin_password));
    first_run = make_shared<FirstRunController>(users, boards, site);
    auto dump_c = make_shared<DumpController>();
    auto api_c = make_shared<Lemmy::ApiController>(site, users, sessions, boards, posts, search, first_run);
    auto remote_media_c = make_shared<RemoteMediaController>(
      pool.io, db, outer_http, xml_ctx, event_bus,
      [&](auto f) { pool.post(std::move(f)); }
    );
    promise<uint16_t> port_promise;
    auto port_future = port_promise.get_future();
    std::thread server_thread([&]{
      uWS::App app;
      define_media_routes(app, remote_media_c);
      define_webapp_routes(
        app,
        db,
        site,
        sessions,
        posts,
        boards,
        users,
        search,
        first_run,
        dump_c,
        rate_limiter
      );
      Lemmy::define_api_routes(app, db, api_c, rate_limiter);
      app.listen(0, [&](auto *listen_socket) {
        if (listen_socket) {
          int port = us_socket_local_port(false, (us_socket_t*)listen_socket);
          if (port > 0) {
            port_promise.set_value((uint16_t)port);
            return;
          }
        }
        try { throw runtime_error("Could not create test server"); }
        catch (...) { port_promise.set_exception(std::current_exception()); }
      }).run();
    });
    server_thread.detach();
    REQUIRE(port_future.wait_for(15s) == std::future_status::ready);
    base_url = fmt::format("http://127.0.0.1:{:d}", port_future.get());
  }
  ~IntegrationTest() {
    if (app_socket) us_listen_socket_close(false, app_socket);
    std::remove(fmt::format("{}-lock", dbfile.name).c_str());
  }

  HtmlDoc html(unique_ptr<const HttpClientResponse>& rsp, uint16_t expected_status = 200) {
    CHECK(rsp->header("content-type") == TYPE_HTML);
    return HtmlDoc(xml, rsp->body());
  }

  string get_login_cookie(unique_ptr<const HttpClientResponse>& rsp) {
    const auto set_cookie = string(rsp->header("set-cookie"));
    CHECK_FALSE(set_cookie == "");
    CHECK_FALSE(set_cookie.contains("deleted"));
    std::smatch cookie_match;
    REQUIRE(std::regex_match(set_cookie, cookie_match, std::regex(R"(^(\w+=\w+);.*)")));
    return cookie_match[1].str();
  }
};

}
