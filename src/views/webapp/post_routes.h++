#pragma once
#include "views/router_common.h++"
#include "webapp_common.h++"
#include "controllers/post_controller.h++"
#include "controllers/board_controller.h++"
#include "controllers/user_controller.h++"
#include "html/html_sidebar.h++"
#include "html/html_action_menu.h++"
#include "html/html_post_widgets.h++"
#include "html/html_comments_page.h++"
#include "html/html_thread_forms.h++"

namespace Ludwig {

namespace Internal {

  // ACTIONS IMPL (reply, menu, vote)
  //////////////////////////////////////////////////////////

  template <bool SSL, class Detail>
  void define_post_action_routes(
    Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
    std::shared_ptr<PostController> posts,
    std::shared_ptr<UserController> users
  ) {
    using fmt::operator""_cf;
    using Coro = RouterCoroutine<Context<SSL>>;

    r.post_form(fmt::format("/{}/:id/reply", Detail::noun), [posts](auto* rsp, auto _c, auto body) -> Coro {
      auto& c = co_await _c;
      const auto post_id = co_await _c.with_request([](auto* req){ return hex_id_param(req, 0); });
      auto user = c.require_login();
      auto form = co_await body;
      auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
      const auto id = posts->create_local_comment(
        txn,
        user,
        post_id,
        form.required_string("text_content"),
        form.optional_string("content_warning")
      );
      if (c.is_htmx) {
        CommentTree tree;
        tree.emplace(post_id, CommentDetail::get(txn, id, c.login));
        rsp->writeHeader("Content-Type", TYPE_HTML);
        c.write_cookie();
        html_comment_tree(c, tree, post_id, CommentSortType::New, c.site, c.login, true, true, false);
        html_toast(c, "Reply submitted");
        c.finish_write();
      } else {
        rsp->writeStatus(http_status(303));
        c.write_cookie();
        rsp->writeHeader("Location", format("/{}s/{:x}"_cf, Detail::noun, post_id))->end();
      }
      txn.commit();
    });

    r.post_form(fmt::format("/{}/:id/action", Detail::noun), [users](auto* rsp, auto _c, auto body) -> Coro {
      auto& c = co_await _c;
      const auto [id, referer] = co_await _c.with_request([](auto* req) {
        return std::pair(hex_id_param(req, 0), std::string(req->getHeader("referer")));
      });
      auto user = c.require_login();
      auto form = co_await body;
      auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
      const auto action = static_cast<SubmenuAction>(form.required_int("action"));
      const auto redirect = action_menu_action<Detail>(txn, users, action, user, id);
      if (redirect) {
        write_redirect_to(rsp, c, *redirect);
      } else if (c.is_htmx) {
        const auto context = static_cast<PostContext>(form.required_int("context"));
        c.populate(txn);
        const auto detail = Detail::get(txn, id, c.login);
        rsp->writeHeader("Content-Type", TYPE_HTML);
        c.write_cookie();
        html_action_menu(c, detail, c.login, context);
        c.finish_write();
      } else {
        write_redirect_back(rsp, referer);
      }
      txn.commit();
    });

    r.post_form(fmt::format("/{}/:id/vote", Detail::noun), [posts](auto* rsp, auto _c, auto body) -> Coro {
      auto& c = co_await _c;
      const auto [post_id, referer] = co_await _c.with_request([](auto* req) {
        return std::pair(hex_id_param(req, 0), std::string(req->getHeader("referer")));
      });
      auto user = c.require_login();
      auto form = co_await body;
      auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
      const auto vote = form.required_vote("vote");
      posts->vote(txn, user, post_id, vote);
      if (c.is_htmx) {
        c.populate(txn);
        const auto detail = Detail::get(txn, post_id, c.login);
        rsp->writeHeader("Content-Type", TYPE_HTML);
        html_vote_buttons(c, detail, c.site, c.login);
        c.finish_write();
      } else {
        write_redirect_back(rsp, referer);
      }
      txn.commit();
    });
  };
}

template <bool SSL>
void define_post_routes(
  Router<SSL, Context<SSL>, std::shared_ptr<WebappState>>& r,
  std::shared_ptr<PostController> posts,
  std::shared_ptr<BoardController> boards,
  std::shared_ptr<UserController> users
) {
  using fmt::operator""_cf;
  using Coro = RouterCoroutine<Context<SSL>>;

  // VIEW COMMENTS
  //////////////////////////////////////////////////////////

  r.get("/thread/:id", [posts, boards](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto id = hex_id_param(req, 0);
    const auto sort = parse_comment_sort_type(req->getQuery("sort"), c.login);
    const auto show_images = req->getQuery("images") == "1" ||
      (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_comments() : false);
    CommentTree comments;
    const auto detail = posts->thread_detail(txn, comments, id, sort, c.login, req->getQuery("from"));
    rsp->writeHeader("Content-Type", TYPE_HTML);
    if (c.is_htmx) {
      c.write_cookie();
      html_comment_tree(c, comments, detail.id, sort, c.site, c.login, show_images, false, false);
    } else {
      html_site_header(c, board_header_options(req, detail.board(),
        format("{} - {}"_cf, display_name_as_text(detail.board()), display_name_as_text(detail.thread()))));
      c.write("<div>");
      html_sidebar(c, c.login, c.site, boards->board_detail(txn, detail.thread().board(), c.login));
      html_thread_view(c, detail, comments, c.site, c.login, sort, show_images);
      c.write("</div>");
      html_site_footer(c);
    }
    c.finish_write();
  });

  r.get("/comment/:id", [posts, boards](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    c.populate(txn);
    const auto id = hex_id_param(req, 0);
    const auto sort = parse_comment_sort_type(req->getQuery("sort"), c.login);
    const auto show_images = req->getQuery("images") == "1" ||
      (req->getQuery("sort").empty() ? !c.login || c.login->local_user().show_images_comments() : false);
    CommentTree comments;
    const auto detail = posts->comment_detail(txn, comments, id, sort, c.login, req->getQuery("from"));
    rsp->writeHeader("Content-Type", TYPE_HTML);
    if (c.is_htmx) {
      c.write_cookie();
      html_comment_tree(c, comments, detail.id, sort, c.site, c.login, show_images, false, false);
    } else {
      html_site_header(c, board_header_options(req, detail.board(),
          fmt::format("{} - {}'s comment on “{}”"_cf,
            display_name_as_text(detail.board()),
            display_name_as_text(detail.author()),
            display_name_as_text(detail.thread()))));
      c.write("<div>");
      html_sidebar(c, c.login, c.site, boards->board_detail(txn, detail.thread().board(), c.login));
      html_comment_view(c, detail, comments, c.site, c.login, sort, show_images);
      c.write("</div>");
      html_site_footer(c);
    }
    c.finish_write();
  });

  // CREATE THREAD
  //////////////////////////////////////////////////////////

  r.get("/b/:name/create_thread", [boards](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto board_id = board_name_param(txn, req, 0);
    const auto show_url = req->getQuery("text") != "1";
    const auto login = c.require_login(txn);
    const auto board = boards->board_detail(txn, board_id, c.login);
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, board_header_options(req, board.board(), "Create Thread"));
    html_create_thread_form(c, show_url, board, login);
    html_site_footer(c);
    c.finish_write();
  });

  r.post_form("/b/:name/create_thread", [posts](auto* rsp, auto _c, auto body) -> Coro {
    auto& c = co_await _c;
    auto user = c.require_login();
    const auto board_id = co_await _c.with_request([&](auto* req) {
      auto txn = c.app->db->open_read_txn();
      return board_name_param(txn, req, 0);
    });
    auto form = co_await body;
    auto txn = co_await open_write_txn<Context<SSL>>(c.app->db);
    const auto id = posts->create_local_thread(
      txn,
      user,
      board_id,
      form.required_string("title"),
      form.optional_string("submission_url"),
      form.optional_string("text_content"),
      form.optional_string("content_warning")
    );
    txn.commit();
    rsp->writeStatus(http_status(303));
    c.write_cookie();
    rsp->writeHeader("Location", format("/thread/{:x}"_cf, id))->end();
  });

  // EDIT THREAD
  //////////////////////////////////////////////////////////

  r.get("/thread/:id/edit", [](auto* rsp, auto* req, auto& c) {
    auto txn = c.app->db->open_read_txn();
    const auto id = hex_id_param(req, 0);
    const auto login = c.require_login(txn);
    const auto thread = ThreadDetail::get(txn, id, login);
    if (!thread.can_edit(login)) die(403, "Cannot edit this post");
    rsp->writeHeader("Content-Type", TYPE_HTML);
    html_site_header(c, board_header_options(req, thread.board(), "Edit Thread"));
    html_edit_thread_form(c, thread, login);
    html_site_footer(c);
    c.finish_write();
  });

  // TODO: POST edit thread

  // EDIT COMMENT
  //////////////////////////////////////////////////////////

  // TODO: GET edit comment
  // TODO: POST edit comment

  // REPORT POST
  //////////////////////////////////////////////////////////

  // TODO: GET report post
  // TODO: POST report post

  // ACTIONS (reply, menu, vote)
  //////////////////////////////////////////////////////////

  Internal::define_post_action_routes<SSL, ThreadDetail>(r, posts, users);
  Internal::define_post_action_routes<SSL, CommentDetail>(r, posts, users);
}

}