#pragma once
#include "webapp_common.h++"
#include "admin_routes.h++"
#include "board_routes.h++"
#include "feed_routes.h++"
#include "post_routes.h++"
#include "search_routes.h++"
#include "static_routes.h++"
#include "user_routes.h++"
#include "controllers/post_controller.h++"
#include "controllers/board_controller.h++"
#include "controllers/user_controller.h++"
#include "controllers/first_run_controller.h++"
#include "controllers/dump_controller.h++"

namespace Ludwig {

template <bool SSL>
void define_webapp_routes(
  uWS::TemplatedApp<SSL>& app,
  std::shared_ptr<DB> db,
  std::shared_ptr<SiteController> site,
  std::shared_ptr<SessionController> sessions,
  std::shared_ptr<PostController> posts,
  std::shared_ptr<BoardController> boards,
  std::shared_ptr<UserController> users,
  std::shared_ptr<SearchController> search,
  std::shared_ptr<FirstRunController> first_run,
  std::shared_ptr<DumpController> dump,
  std::shared_ptr<KeyedRateLimiter> rate_limiter = nullptr
) {
  define_static_routes(app);

  auto state = std::make_shared<WebappState>(
    db, sessions, site, rate_limiter
  );
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>> r(app, state);
  
  define_admin_routes(r, first_run, dump);
  define_board_routes(r, boards);
  define_feed_routes(r, posts, boards, users, first_run);
  define_post_routes(r, posts, boards, users);
  define_search_routes(r, search);
  define_user_routes(r, users);

  r.any("/*", [](auto*, auto*, auto&) {
    die(404, "Page not found");
  });
}

}