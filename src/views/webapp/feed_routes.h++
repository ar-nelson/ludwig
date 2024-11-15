#pragma once
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/post_controller.h++"
#include "controllers/board_controller.h++"
#include "controllers/user_controller.h++"
#include "controllers/first_run_controller.h++"
#include "html/html_sidebar.h++"
#include "html/html_feed_page.h++"
#include "html/html_first_run_setup_form.h++"

namespace Ludwig {

template <bool SSL>
void define_feed_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<PostController> posts,
  std::shared_ptr<BoardController> boards,
  std::shared_ptr<UserController> users,
  std::shared_ptr<FirstRunController> first_run
) {
  using fmt::operator""_cf;

  auto feed_route = [posts](uint64_t feed_id, uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req, Context<SSL>& c) -> void {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto sort = parse_sort_type(req->getQuery("sort"), c.login);
    const auto show_threads = req->getQuery("type") != "comments",
      show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
    if (
      feed_id == PostController::FEED_HOME &&
      (!c.logged_in_user_id || txn.list_subscribed_boards(*c.logged_in_user_id).is_done())
    ) {
      feed_id = PostController::FEED_LOCAL;
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    std::string title = [&]{
      using std::operator""s;
      switch (feed_id) {
        case PostController::FEED_ALL: return "All"s;
        case PostController::FEED_LOCAL: return c.site->name;
        case PostController::FEED_HOME: return "Subscribed"s;
        default: return "Unknown Feed"s;
      }
    }();
    const auto base_url = format("{}?type={}&sort={}&images={}"_cf,
      c.url,
      show_threads ? "threads" : "comments",
      EnumNameSortType(sort),
      show_images ? 1 : 0
    );
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    if (!c.is_htmx) {
      html_site_header(c, {
        .canonical_path = c.url,
        .banner_link = c.url,
        .page_title = feed_id == PostController::FEED_LOCAL ? "Local" : title,
        .banner_title = title
      });
      c.write("<div>");
      html_sidebar(c, c.login, c.site);
    }
    if (show_threads) {
      html_feed_page(
        c,
        cursor,
        posts->list_feed_threads(txn, cursor, feed_id, sort, c.login),
        base_url,
        sort,
        PostContext::Feed,
        show_images,
        c.site->votes_enabled
      );
    } else {
      html_feed_page(
        c,
        cursor,
        posts->list_feed_comments(txn, cursor, feed_id, sort, c.login),
        base_url,
        sort,
        PostContext::Feed,
        show_images,
        c.site->votes_enabled
      );
    }
    if (!c.is_htmx) {
      c.write("</div>");
      html_site_footer(c);
    }
    c.finish_write();
  };

  r.get("/", [feed_route, first_run](auto* rsp, auto* req, auto& c) {
    if (c.site->setup_done) {
      feed_route(c.logged_in_user_id ? PostController::FEED_HOME : PostController::FEED_LOCAL, rsp, req, c);
    } else {
      auto txn = c.app->db->open_read_txn();
      if (!c.require_login(txn).local_user().admin()) {
        die(403, "Only an admin user can perform first-run setup.");
      }
      rsp->writeHeader("Content-Type", TYPE_HTML);
      html_site_header(c, {
        .canonical_path = "/",
        .banner_title = "First-Run Setup",
      });
      html_first_run_setup_form(c, first_run->first_run_setup_options(txn));
      html_site_footer(c);
      c.finish_write();
    }
  });

  r.get("/all", [feed_route](auto* rsp, auto* req, auto& c) {
    feed_route(PostController::FEED_ALL, rsp, req, c);
  });

  r.get("/local", [feed_route](auto* rsp, auto* req, auto& c) {
    feed_route(PostController::FEED_LOCAL, rsp, req, c);
  });

  r.get("/c/:name", [](auto* rsp, auto* req, auto& c) {
    // Compatibility alias for Lemmy community URLs
    // Needed because some Lemmy apps expect URLs in exactly this format
    write_redirect_to(rsp, c, format("/b/{}"_cf, req->getParameter(0)));
  });

  r.get("/b/:name", [boards, posts](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto board_id = board_name_param(txn, req, 0);
    const auto board = boards->board_detail(txn, board_id, c.login);
    const auto sort = parse_sort_type(req->getQuery("sort"), c.login);
    const auto show_threads = req->getQuery("type") != "comments",
      show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
    const auto base_url = format("{}?type={}&sort={}&images={}"_cf,
      c.url,
      show_threads ? "threads" : "comments",
      EnumNameSortType(sort),
      show_images ? 1 : 0
    );
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, board_header_options(req, board.board()));
    if (!c.is_htmx) {
      c.write("<div>");
      html_sidebar(c, c.login, c.site, board);
    }
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    if (show_threads) {
      html_feed_page(
        c,
        cursor,
        posts->list_board_threads(txn, cursor, board_id, sort, c.login),
        base_url,
        sort,
        PostContext::Board,
        show_images,
        board.should_show_votes(c.login, c.site)
      );
    } else {
      html_feed_page(
        c,
        cursor,
        posts->list_board_comments(txn, cursor, board_id, sort, c.login),
        base_url,
        sort,
        PostContext::Board,
        show_images,
        board.should_show_votes(c.login, c.site)
      );
    }
    if (!c.is_htmx) c.write("</div>");
    html_site_footer(c);
    c.finish_write();
  });

  r.get("/u/:name", [users, posts](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto user_id = user_name_param(txn, req, 0);
    const auto user = users->user_detail(txn, user_id, c.login);
    const auto sort = parse_user_post_sort_type(req->getQuery("sort"));
    const auto show_threads = req->getQuery("type") != "comments",
      show_images = req->getQuery("images") == "1" || (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_threads() : false);
    const auto base_url = format("{}?type={}&sort={}&images={}"_cf,
      c.url,
      show_threads ? "threads" : "comments",
      EnumNameUserPostSortType(sort),
      show_images ? 1 : 0
    );
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = c.url,
      .banner_link = c.url,
      .banner_title = display_name_as_text(user.user()),
      .banner_image = user.user().banner_url()
        ? std::optional(format("/media/user/{}/banner.webp"_cf, user.user().name()->string_view()))
        : std::nullopt,
      .card_image = user.user().avatar_url()
        ? std::optional(format("/media/user/{}/avatar.webp"_cf, user.user().name()->string_view()))
        : std::nullopt
    });
    if (!c.is_htmx) {
      c.write("<div>");
      html_sidebar(c, c.login, c.site, user);
    }
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    if (show_threads) {
      html_feed_page(
        c,
        cursor,
        posts->list_user_threads(txn, cursor, user_id, sort, c.login),
        base_url,
        sort,
        PostContext::Board,
        show_images,
        c.site->votes_enabled
      );
    } else {
      html_feed_page(
        c,
        cursor,
        posts->list_user_comments(txn, cursor, user_id, sort, c.login),
        base_url,
        sort,
        PostContext::Board,
        show_images,
        c.site->votes_enabled
      );
    }
    if (!c.is_htmx) c.write("</div>");
    html_site_footer(c);
    c.finish_write();
  });
}

}