#pragma once
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/search_controller.h++"
#include "html/html_search_results.h++"
#include "html/html_sidebar.h++"

namespace Ludwig {

template <bool SSL>
void define_search_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<SearchController> search
) {
  using Coro = RouterCoroutine<Context<SSL>>;

  r.get_async("/search", [search](auto* rsp, auto _c) -> Coro {
    auto query = co_await _c.with_request([](auto* req) {
      return SearchQuery{
        .query = req->getQuery("search"),
        /// TODO: Other parameters
        .include_threads = true,
        .include_comments = true
      };
    });
    auto& c = co_await _c;
    {
      auto txn = c.app->db->open_read_txn();
      c.populate(txn);
    }
    auto results = co_await search->search(c, query, c.login);
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/search",
      .banner_title = "Search",
    });
    c.write("<div>");
    html_sidebar(c, c.login, c.site);
    c.write("<main>");
    html_search_result_list(c, results, true);
    c.write("</main></div>");
    html_site_footer(c);
    c.finish_write();
  });
}

}