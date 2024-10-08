#include "lemmy_api.h++"
#include "util/rate_limiter.h++"
#include "util/web.h++"
#include "util/router.h++"
#include <flatbuffers/minireflect.h>

using std::make_shared, std::nullopt, std::optional, std::shared_ptr, std::string,
    std::string_view;

namespace Ludwig::Lemmy {
  template <bool SSL>
  struct Context : public RequestContext<SSL, std::shared_ptr<KeyedRateLimiter>> {
    optional<SecretString> auth;
    string ip;

    void pre_request(uWS::HttpResponse<SSL>* rsp, uWS::HttpRequest* req, std::shared_ptr<KeyedRateLimiter> rate_limiter) override {
      ip = get_ip(rsp, req);
      if (rate_limiter && !rate_limiter->try_acquire(ip, this->method == "get" ? 1 : 10)) {
        throw ApiError("Rate limited, try again later", 429);
      }
      const auto auth_header = req->getHeader("authorization");
      auth = auth_header.starts_with("Bearer ") ? optional(SecretString(auth_header.substr(7))) : nullopt;
    }

    void error_response(const ApiError& err, uWS::HttpResponse<SSL>* rsp) noexcept override {
      string s;
      Error e { err.message, err.http_status };
      JsonSerialize<Error>::to_json(e, s);
      rsp->writeStatus(http_status(err.http_status))
        ->writeHeader("Content-Type", "application/json; charset=utf-8")
        ->writeHeader("Access-Control-Allow-Origin", "*")
        ->end(s);
    }

    auto header_or_query_auth(QueryString<uWS::HttpRequest*>& q) -> optional<SecretString> {
      if (auth) return std::move(auth);
      return q.optional_string("auth").transform([](auto s){return SecretString(s);});
    }
  };

  template <bool SSL>
  static inline auto write_no_content(uWS::HttpResponse<SSL>* rsp) {
    rsp->writeStatus(http_status(204))
      ->writeHeader("Access-Control-Allow-Origin", "*")
      ->end();
  }

  template <bool SSL, typename T>
  static inline auto write_json(uWS::HttpResponse<SSL>* rsp, T&& t) -> void {
    string s;
    JsonSerialize<T>::to_json(t, s);
    rsp->writeHeader("Content-Type", "application/json; charset=utf-8")
      ->writeHeader("Access-Control-Allow-Origin", "*")
      ->end(s);
  }

  template <class Fn, bool SSL, class In, class Out>
  concept PostRetHandler = requires (Fn&& fn, In in, Context<SSL>& ctx, WriteTxn&& txn) {
    std::invocable<Fn, In, Context<SSL>&, WriteTxn&&>;
    { fn(in, ctx, std::move(txn)) } -> std::same_as<Out>;
  };

  template <bool SSL, class In, class Out>
  struct JsonRequestBuilder {
    Router<SSL, Context<SSL>, std::shared_ptr<KeyedRateLimiter>>& router;
    string pattern;
    shared_ptr<simdjson::ondemand::parser> parser;
    ApiController& controller;
    size_t max_size = 10 * MiB;

    template <PostRetHandler<SSL, In&, Out> Fn>
    auto post(Fn handler) -> void {
      router.template post_json<In>(pattern, parser, [&controller = controller, handler = std::move(handler)](auto* rsp, auto c, auto body) -> RouterCoroutine<Context<SSL>> {
        auto& ctx = co_await c;
        auto form = co_await body;
        write_json<SSL, Out>(rsp, handler(form, ctx, co_await controller.template open_write_txn<Context<SSL>>()));
      }, max_size);
    }

    template <PostRetHandler<SSL, In&, Out> Fn>
    auto put(Fn handler) -> void {
      router.template put_json<In>(pattern, parser, [&controller = controller, handler = std::move(handler)](auto* rsp, auto c, auto body) -> RouterCoroutine<Context<SSL>> {
        auto& ctx = co_await c;
        auto form = co_await body;
        write_json<SSL, Out>(rsp, handler(form, ctx, co_await controller.template open_write_txn<Context<SSL>>()));
      }, max_size);
    }
  };

# define JSON_ROUTE(Pattern,In,Out) JsonRequestBuilder<SSL,In,Out>{router,Pattern,parser,*controller}

  template <bool SSL>
  auto api_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void {
    using Coro = RouterCoroutine<Context<SSL>>;
    auto parser = make_shared<simdjson::ondemand::parser>();
    Router<SSL, Context<SSL>, std::shared_ptr<KeyedRateLimiter>> router(app, rate_limiter);
    router.access_control_allow_origin("*");

    // Site
    ///////////////////////////////////////////////////////

    router.get("/api/v3/site", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_site(ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/site", CreateSite, SiteResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->create_site(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/site", EditSite, SiteResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->edit_site(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/site/block

    // Miscellaneous
    ///////////////////////////////////////////////////////

    // TODO: /api/v3/modlog
    // TODO: /api/v3/search
    // TODO: /api/v3/resolve_object
    // TODO: /api/v3/federated_instances

    // Community
    ///////////////////////////////////////////////////////

    router.get("/api/v3/community", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_community(
        {.id=q.optional_uint("id").value_or(0),.name=q.optional_string("name").value_or("")},
        ctx.header_or_query_auth(q))
      );
    });
    JSON_ROUTE("/api/v3/community", CreateCommunity, CommunityResponse).post([controller](auto&& form, auto& ctx, auto&& txn) {
      return controller->create_community(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/community", EditCommunity, CommunityResponse).put([controller](auto&& form, auto& ctx, auto&& txn) {
      return controller->edit_community(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/community/hide
    router.get("/api/v3/community/list", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->list_communities({
        .sort = parse_board_sort_type(q.string("sort")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .show_nsfw = q.optional_bool("show_nsfw")
      }, ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/community/follow", FollowCommunity, CommunityResponse).post([controller](auto&& form, auto& ctx, auto&& txn) {
      return controller->follow_community(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/community/block
    JSON_ROUTE("/api/v3/community/delete", DeleteCommunity, CommunityResponse).post([controller](auto&& form, auto& ctx, auto&& txn) {
      return controller->delete_community(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/community/remove
    // TODO: /api/v3/community/transfer
    // TODO: /api/v3/community/ban_user
    // TODO: /api/v3/community/mod

    // Post
    ///////////////////////////////////////////////////////

    router.get("/api/v3/post", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_post({
        .id = q.optional_uint("id").value_or(0),
        .comment_id = q.optional_uint("comment_id").value_or(0)
      }, ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/post", CreatePost, PostResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->create_post(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/post", EditPost, PostResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->edit_post(std::move(txn), form, std::move(ctx.auth));
    });
    router.get("/api/v3/post/list", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_posts({
        .type = q.optional_string("type").or_else([&](){return q.optional_string("type_");}).transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .community_id = q.optional_uint("community_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .page_cursor = q.string("page_cursor"),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/post/delete", DeletePost, PostResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->delete_post(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/post/remove
    JSON_ROUTE("/api/v3/post/mark_as_read", MarkPostAsRead, PostResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->mark_post_as_read(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/post/lock
    // TODO: /api/v3/post/feature
    JSON_ROUTE("/api/v3/post/like", CreatePostLike, PostResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->like_post(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/post/save", SavePost, PostResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->save_post(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/post/report
    // TODO: /api/v3/post/report/resolve
    // TODO: /api/v3/post/report/list
    // TODO: /api/v3/post/site_metadata

    // Comment
    ///////////////////////////////////////////////////////

    router.get("/api/v3/comment", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_comment({.id=q.required_hex_id("id")}, ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/comment", CreateComment, CommentResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->create_comment(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/comment", EditComment, CommentResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->edit_comment(std::move(txn), form, std::move(ctx.auth));
    });
    router.get("/api/v3/comment/list", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_comments({
        .type = q.optional_string("type").or_else([&](){return q.optional_string("type_");}).transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .post_id = q.optional_uint("post_id").value_or(0),
        .parent_id = q.optional_uint("parent_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .max_depth = (uint16_t)q.optional_uint("max_depth").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .page_cursor = q.optional_string("page_cursor").value_or(""),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, ctx.header_or_query_auth(q)));
    });
    JSON_ROUTE("/api/v3/comment/delete", DeleteComment, CommentResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->delete_comment(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/comment/remove
    JSON_ROUTE("/api/v3/comment/mark_as_read", MarkCommentReplyAsRead, CommentReplyResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->mark_comment_reply_as_read(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/comment/distinguish
    JSON_ROUTE("/api/v3/comment/like", CreateCommentLike, CommentResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->like_comment(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/comment/save", SaveComment, CommentResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->save_comment(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/comment/report
    // TODO: /api/v3/comment/report/resolve
    // TODO: /api/v3/comment/report/list

    // PrivateMessage
    ///////////////////////////////////////////////////////

    // TODO: Private messages

    // User
    ///////////////////////////////////////////////////////

    router.get("/api/v3/user", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_person_details({
        .username = q.optional_string("username").value_or(""),
        .community_id = q.optional_uint("community_id").value_or(0),
        .person_id = q.optional_uint("person_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .sort = parse_user_post_sort_type(q.string("sort")),
        .saved_only = q.optional_bool("saved_only")
      }, ctx.header_or_query_auth(q)));
    });
    router.template post_json<Register>("/api/v3/user/register", parser, [controller](auto* rsp, auto c, auto body) -> RouterCoroutine<Context<SSL>> {
      auto& ctx = co_await c;
      auto form = co_await body;
      Login login {
        .username_or_email = form.username,
        .password = SecretString(form.password.data)
      };
      controller->register_account(
        co_await controller->template open_write_txn<Context<SSL>>(),
        form,
        ctx.ip,
        ctx.user_agent
      );
      write_json<SSL, LoginResponse>(rsp, controller->login(
        co_await controller->template open_write_txn<Context<SSL>>(),
        login,
        ctx.ip,
        ctx.user_agent
      ));
    });
    // TODO: /api/v3/user/get_captcha
    router.get("/api/v3/user/mentions", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      auto auth = ctx.header_or_query_auth(q);
      if (!auth) throw ApiError("Auth required", 401);
      write_json<SSL>(rsp,controller->get_person_mentions({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth)));
    });
    JSON_ROUTE("/api/v3/user/mention/mark_as_read", MarkPersonMentionAsRead, PersonMentionResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->mark_person_mentions_as_read(std::move(txn), form, std::move(ctx.auth));
    });
    router.get("/api/v3/user/replies", [controller, parser](auto* rsp, auto* req, auto& ctx) {
      QueryString q(req);
      auto auth = ctx.header_or_query_auth(q);
      if (!auth) throw ApiError("Auth required", 401);
      write_json<SSL>(rsp, controller->get_replies({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(1),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth)));
    });
    // TODO: /api/v3/user/ban
    // TODO: /api/v3/user/banned
    // TODO: /api/v3/user/block
    JSON_ROUTE("/api/v3/user/login", Login, LoginResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->login(std::move(txn), form, ctx.ip, ctx.user_agent);
    });
    router.template post_json<DeleteAccount>("/api/v3/user/delete_account", parser, [controller](auto* rsp, auto ctx, auto body) -> Coro {
      auto form = co_await body;
      controller->delete_account(co_await controller->template open_write_txn<Context<SSL>>(), form, std::move((co_await ctx).auth));
      write_no_content(rsp);
    }).template post_json<PasswordReset>("/api/v3/user/password_reset", parser, [controller](auto* rsp, auto, auto body) -> Coro {
      auto form = co_await body;
      controller->password_reset(co_await controller->template open_write_txn<Context<SSL>>(), form);
      write_no_content(rsp);
    }).template post_json<PasswordChangeAfterReset>("/api/v3/user/password_change", parser, [controller](auto* rsp, auto, auto body) -> Coro {
      auto form = co_await body;
      controller->password_change_after_reset(co_await controller->template open_write_txn<Context<SSL>>(), form);
      write_no_content(rsp);
    });
    JSON_ROUTE("/api/v3/user/mention/mark_all_as_read", MarkAllAsRead, GetRepliesResponse).post([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->mark_all_as_read(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/user/save_user_settings", SaveUserSettings, LoginResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->save_user_settings(std::move(txn), form, std::move(ctx.auth));
    });
    JSON_ROUTE("/api/v3/user/change_password", ChangePassword, LoginResponse).put([controller](auto& form, auto& ctx, auto&& txn) {
      return controller->change_password(std::move(txn), form, std::move(ctx.auth));
    });
    // TODO: /api/v3/user/report_count
    // TODO: /api/v3/user/unread_count
    // Placeholder implementation because Lemmy frontend calls this a lot
    router.get("/api/v3/user/unread_count", [](auto* rsp, auto*, auto&) {
      rsp->writeHeader("Content-Type", "application/json; charset=utf-8")
        ->writeHeader("Access-Control-Allow-Origin", "*")
        ->end(R"({"replies":0,"mentions":0,"private_messages":0})");
    });
    router.template post_json<VerifyEmail>("/api/v3/user/verify_email", parser, [controller](auto* rsp, auto, auto body) -> Coro {
      auto form = co_await body;
      controller->verify_email(co_await controller->template open_write_txn<Context<SSL>>(), form);
      write_no_content(rsp);
    });
    // TODO: /api/v3/user/leave_admin
    // TODO: /api/v3/user/totp/generate
    // TODO: /api/v3/user/totp/update
    // TODO: /api/v3/user/export_settings
    // TODO: /api/v3/user/import_settings
    // TODO: /api/v3/user/list_logins
    router.get("/api/v3/user/validate_auth", [controller](auto* rsp, auto*, auto& ctx) {
      controller->validate_auth(std::move(ctx.auth));
      write_no_content(rsp);
    }).post("/api/v3/user/logout", [controller](auto* rsp, auto _ctx, auto body) -> Coro {
      auto& ctx = co_await _ctx;
      co_await body;
      if (ctx.auth) controller->logout(co_await controller->template open_write_txn<Context<SSL>>(), std::move(*ctx.auth));
      write_no_content(rsp);
    });

    // Admin
    ///////////////////////////////////////////////////////

    // TODO: Admin endpoints

    // CustomEmoji
    ///////////////////////////////////////////////////////

    // TODO: Custom emoji
    // Placeholder implementation because Lemmy frontend calls this a lot
    router.get("/api/v3/custom_emoji/list", [](auto* rsp, auto*, auto&) {
      rsp->writeHeader("Content-Type", "application/json; charset=utf-8")
        ->writeHeader("Access-Control-Allow-Origin", "*")
        ->end(R"({"custom_emojis":[]})");
    });

    router.any("/api/*", [](auto*, auto*, auto&) {
      throw ApiError("Endpoint does not exist or is not yet implemented", 404);
    });
  }

#ifndef LUDWIG_DEBUG
  template auto api_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void;
#endif

  template auto api_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void;
}
