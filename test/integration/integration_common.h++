#pragma once
#include "../test_common.h++"
#include "util/rich_text.h++"
#include "services/asio_http_client.h++"
#include "services/asio_event_bus.h++"
#include "services/db.h++"
#include "controllers/instance.h++"
#include "controllers/remote_media.h++"
#include "controllers/lemmy_api.h++"
#include "views/webapp.h++"
#include "views/media.h++"
#include "views/lemmy_api.h++"

using std::promise, std::unique_ptr;

namespace Ludwig {

class IntegrationTest {
  TempFile dbfile;
  AsioThreadPool pool;
  us_listen_socket_t* app_socket = nullptr;
  shared_ptr<LibXmlContext> xml;

public:
  static constexpr inline string_view first_run_admin_password = "first-run";
  string base_url;
  AsioHttpClient http;
  shared_ptr<MockHttpClient> outer_http;
  shared_ptr<InstanceController> instance;
  IntegrationTest() :
    pool(1),
    xml(make_shared<LibXmlContext>()),
    http(pool.io, 100000, UNSAFE_HTTPS, UNSAFE_LOCAL_REQUESTS),
    outer_http(make_shared<MockHttpClient>())
  {
    pair<Hash, Salt> first_run_hash;
    RAND_pseudo_bytes(const_cast<uint8_t*>(first_run_hash.second.bytes()->Data()), first_run_hash.second.bytes()->size());
    InstanceController::hash_password(
      string(first_run_admin_password),
      first_run_hash.second.bytes()->Data(),
      const_cast<uint8_t*>(first_run_hash.first.bytes()->Data())
    );
    auto db = make_shared<DB>(dbfile.name, 100);
    auto rate_limiter = make_shared<KeyedRateLimiter>(10, 3000);
    auto event_bus = make_shared<AsioEventBus>(pool.io);
    instance = make_shared<InstanceController>(db, outer_http, event_bus, nullopt, first_run_hash);
    auto api_c = make_shared<Lemmy::ApiController>(instance);
    auto remote_media_c = make_shared<RemoteMediaController>(
      db, outer_http, xml, event_bus,
      [&](auto f) { pool.post(std::move(f)); }
    );
    promise<uint16_t> port_promise;
    auto port_future = port_promise.get_future();
    std::thread server_thread([&]{
      uWS::App app;
      media_routes(app, remote_media_c);
      webapp_routes(app, instance, rate_limiter);
      Lemmy::api_routes(app, api_c, rate_limiter);
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
