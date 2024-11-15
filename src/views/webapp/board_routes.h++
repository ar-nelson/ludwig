#pragma once
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/board_controller.h++"
#include "html/html_board_list.h++"
#include "html/html_board_forms.h++"
#include "util/rich_text.h++"

using std::nullopt, std::optional, fmt::operator""_cf; // NOLINT

namespace Ludwig {

static inline auto board_header_options(
  uWS::HttpRequest* req,
  const Board& board,
  std::optional<std::string_view> title = {}
) -> HtmlHeaderOptions {
  return {
    .canonical_path = req->getUrl(),
    .banner_link = req->getUrl(),
    .page_title = title,
    .banner_title = display_name_as_text(board),
    .banner_image = board.banner_url() ? optional(format("/media/board/{}/banner.webp"_cf, board.name()->string_view())) : nullopt,
    .card_image = board.icon_url() ? optional(format("/media/board/{}/icon.webp"_cf, board.name()->string_view())) : nullopt
  };
}

template <bool SSL>
void define_board_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<BoardController> boards
) {
  using fmt::operator""_cf;
  using Coro = RouterCoroutine<Context<SSL>>;

  // BOARD LIST
  //////////////////////////////////////////////////////////

  r.get("/boards", [boards](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto local = req->getQuery("local") == "1";
    const auto sort = parse_board_sort_type(req->getQuery("sort"));
    const auto sub = req->getQuery("sub") == "1";
    const auto base_url = format("/boards?local={}&sort={}&sub={}"_cf,
      local ? "1" : "0",
      EnumNameBoardSortType(sort),
      sub ? "1" : "0"
    );
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/boards",
      .banner_link = "/boards",
      .banner_title = "Boards",
    });
    const auto from = req->getQuery("from");
    PageCursor cursor(from);
    html_board_list(c, cursor, boards->list_boards(txn, cursor, sort, local, sub, c.login), base_url, sort);
    html_site_footer(c);
    c.finish_write();
  });

  // CREATE BOARD
  //////////////////////////////////////////////////////////

  r.get("/create_board", [boards](auto* rsp, auto*, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto& login = c.require_login(txn);
    if (!boards->can_create_board(login)) {
      die(403, "User cannot create boards");
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, {
      .canonical_path = "/create_board",
      .banner_title = "Create Board",
    });
    c.write("<main>");
    html_create_board_form(c, c.site);
    c.write("</main>");
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form("/create_board", [boards](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto user = c.require_login();
    auto form = co_await body;
    const auto name = form.required_string("name");
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    boards->create_local_board(
      txn,
      user,
      name,
      form.optional_string("display_name"),
      form.optional_string("content_warning"),
      form.optional_bool("private"),
      form.optional_bool("restricted_posting"),
      form.optional_bool("local_only")
    );
    txn.commit();
    rsp->writeStatus(http_status(303));
    c.write_cookie();
    rsp->writeHeader("Location", format("/b/{}"_cf, name))->end();
  });

  // BOARD SETTINGS
  //////////////////////////////////////////////////////////

  r.get("/b/:name/settings", [boards](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto board_id = board_name_param(txn, req, 0);
    const auto login = c.require_login(txn);
    const auto board = boards->local_board_detail(txn, board_id, c.login);
    if (!login.local_user().admin() && login.id != board.local_board().owner()) {
      die(403, "Must be admin or board owner to view this page");
    }
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, board_header_options(req, board.board(), "Board Settings"));
    c.write("<main>");
    html_board_settings_form(c, c.site, board);
    c.write("</main>");
    html_site_footer(c);
    c.finish_write();
  });

  // TODO: POST board/settings

  // BOARD ACTIONS
  //////////////////////////////////////////////////////////

  r.post_form("/b/:name/subscribe", [boards](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    const auto [name, board_id, referer] = co_await _c.with_request([&](auto* req) {
      auto txn = c.app->db->open_read_txn();
      return std::tuple(
        std::string(req->getParameter(0)),
        board_name_param(txn, req, 0),
        std::string(req->getHeader("referer"))
      );
    });
    auto user = c.require_login();
    auto form = co_await body;
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    boards->subscribe(txn, user, board_id, !form.optional_bool("unsubscribe"));
    txn.commit();
    if (c.is_htmx) {
      rsp->writeHeader("Content-Type", TYPE_HTML);
      html_subscribe_button(c, name, !form.optional_bool("unsubscribe"));
      c.finish_write();
    } else {
      write_redirect_back(rsp, referer);
    }
  });

  // TODO: Delete board

  // TODO: Set mod state of board

  // TODO: Add mod

  // TODO: Remove mod

  // TODO: Mod log

}

}