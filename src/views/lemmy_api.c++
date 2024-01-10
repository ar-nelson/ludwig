#include "lemmy_api.h++"
#include "util/web.h++"
#include <flatbuffers/minireflect.h>

using std::make_shared, std::nullopt, std::optional, std::shared_ptr, std::string,
    std::string_view;

namespace Ludwig::Lemmy {
  struct Meta {
    optional<SecretString> auth;
    string ip, user_agent;
  };

  static inline auto header_or_query_auth(QueryString<uWS::HttpRequest*>& q, Meta& m) -> optional<SecretString> {
    if (m.auth) return std::move(m.auth);
    return q.optional_string("auth").transform([](auto s){return SecretString(s);});
  }

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

  template <class In, class Out> using PostRetHandler = std::function<Out (In, Meta&)>;

  template <bool SSL, class In, class Out>
  struct JsonRequestBuilder {
    Router<SSL, Meta>& router;
    string pattern;
    shared_ptr<simdjson::ondemand::parser> parser;
    size_t max_size = 10 * MiB;

    auto post(PostRetHandler<In&, Out> handler) -> void {
      router.template post_json<In>(pattern, parser, [handler](auto*, auto m) {
        return [handler, m=std::move(m)](In in, auto&& write) mutable {
          write([out=handler(in, *m)](auto* rsp) mutable {
            write_json<SSL, Out>(rsp, std::move(out));
          });
        };
      }, max_size);
    }

    auto put(PostRetHandler<In&, Out> handler) -> void {
      router.template put_json<In>(pattern, parser, [handler](auto*, auto m) {
        return [handler, m=std::move(m)](In in, auto&& write) mutable {
          write([out=handler(in, *m)](auto* rsp) mutable {
            write_json<SSL, Out>(rsp, std::move(out));
          });
        };
      }, max_size);
    }
  };

# define JSON_ROUTE(Pattern,In,Out) JsonRequestBuilder<SSL,In,Out>{router,Pattern,parser}

  template <bool SSL> auto api_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void {
    auto parser = make_shared<simdjson::ondemand::parser>();
    Router<SSL, Meta> router(app,
      [rate_limiter](auto* rsp, auto* req) -> Meta {
        const string ip(get_ip(rsp, req));
        if (rate_limiter && !rate_limiter->try_acquire(ip, req->getMethod() == "GET" ? 1 : 10)) {
          throw ApiError("Rate limited, try again later", 429);
        }
        const auto auth = req->getHeader("authorization");
        return {
          .auth = auth.starts_with("Bearer ") ? optional(SecretString(auth.substr(7))) : nullopt,
          .ip = ip,
          .user_agent = string(req->getHeader("user-agent"))
        };
      },
      [parser](auto* rsp, const ApiError& err, auto&) -> void {
        string s;
        Error e { err.message, err.http_status };
        JsonSerialize<Error>::to_json(e, s);
        rsp->writeStatus(http_status(err.http_status))
          ->writeHeader("Content-Type", "application/json; charset=utf-8")
          ->writeHeader("Access-Control-Allow-Origin", "*")
          ->end(s);
      }
    );

    // Site
    ///////////////////////////////////////////////////////

    router.get("/api/v3/site", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_site(header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/site", CreateSite, SiteResponse).post([controller](auto& form, auto& m) {
      return controller->create_site(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/site", EditSite, SiteResponse).put([controller](auto& form, auto& m) {
      return controller->edit_site(form, std::move(m.auth));
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

    router.get("/api/v3/community", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_community(
        {.id=q.optional_uint("id").value_or(0),.name=q.optional_string("name").value_or("")},
        header_or_query_auth(q, m))
      );
    });
    JSON_ROUTE("/api/v3/community", CreateCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->create_community(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/community", EditCommunity, CommunityResponse).put([controller](auto& form, auto& m) {
      return controller->edit_community(form, std::move(m.auth));
    });
    // TODO: /api/v3/community/hide
    router.get("/api/v3/community/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->list_communities({
        .sort = parse_board_sort_type(q.string("sort")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .show_nsfw = q.optional_bool("show_nsfw")
      }, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/community/follow", FollowCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->follow_community(form, std::move(m.auth));
    });
    // TODO: /api/v3/community/block
    JSON_ROUTE("/api/v3/community/delete", DeleteCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->delete_community(form, std::move(m.auth));
    });
    // TODO: /api/v3/community/remove
    // TODO: /api/v3/community/transfer
    // TODO: /api/v3/community/ban_user
    // TODO: /api/v3/community/mod

    // Post
    ///////////////////////////////////////////////////////

    router.get("/api/v3/post", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_post({
        .id = q.optional_uint("id").value_or(0),
        .comment_id = q.optional_uint("comment_id").value_or(0)
      }, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/post", CreatePost, PostResponse).post([controller](auto& form, auto& m) {
      return controller->create_post(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/post", EditPost, PostResponse).put([controller](auto& form, auto& m) {
      return controller->edit_post(form, std::move(m.auth));
    });
    router.get("/api/v3/post/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_posts({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .community_id = q.optional_uint("community_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .page_cursor = q.string("page_cursor"),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/post/delete", DeletePost, PostResponse).post([controller](auto& form, auto& m) {
      return controller->delete_post(form, std::move(m.auth));
    });
    // TODO: /api/v3/post/remove
    JSON_ROUTE("/api/v3/post/mark_as_read", MarkPostAsRead, PostResponse).post([controller](auto& form, auto& m) {
      return controller->mark_post_as_read(form, std::move(m.auth));
    });
    // TODO: /api/v3/post/lock
    // TODO: /api/v3/post/feature
    JSON_ROUTE("/api/v3/post/like", CreatePostLike, PostResponse).post([controller](auto& form, auto& m) {
      return controller->like_post(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/post/save", SavePost, PostResponse).put([controller](auto& form, auto& m) {
      return controller->save_post(form, std::move(m.auth));
    });
    // TODO: /api/v3/post/report
    // TODO: /api/v3/post/report/resolve
    // TODO: /api/v3/post/report/list
    // TODO: /api/v3/post/site_metadata

    // Comment
    ///////////////////////////////////////////////////////

    router.get("/api/v3/comment", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_comment({.id=q.required_hex_id("id")}, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/comment", CreateComment, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->create_comment(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/comment", EditComment, CommentResponse).put([controller](auto& form, auto& m) {
      return controller->edit_comment(form, std::move(m.auth));
    });
    router.get("/api/v3/comment/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_comments({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .post_id = q.optional_uint("post_id").value_or(0),
        .parent_id = q.optional_uint("parent_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .max_depth = (uint16_t)q.optional_uint("max_depth").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .page_cursor = q.optional_string("page_cursor").value_or(""),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/comment/delete", DeleteComment, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->delete_comment(form, std::move(m.auth));
    });
    // TODO: /api/v3/comment/remove
    JSON_ROUTE("/api/v3/comment/mark_as_read", MarkCommentReplyAsRead, CommentReplyResponse).post([controller](auto& form, auto& m) {
      return controller->mark_comment_reply_as_read(form, std::move(m.auth));
    });
    // TODO: /api/v3/comment/distinguish
    JSON_ROUTE("/api/v3/comment/like", CreateCommentLike, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->like_comment(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/comment/save", SaveComment, CommentResponse).put([controller](auto& form, auto& m) {
      return controller->save_comment(form, std::move(m.auth));
    });
    // TODO: /api/v3/comment/report
    // TODO: /api/v3/comment/report/resolve
    // TODO: /api/v3/comment/report/list

    // PrivateMessage
    ///////////////////////////////////////////////////////

    // TODO: Private messages

    // User
    ///////////////////////////////////////////////////////

    router.get("/api/v3/user", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      write_json<SSL>(rsp, controller->get_person_details({
        .username = q.optional_string("username").value_or(""),
        .community_id = q.optional_uint("community_id").value_or(0),
        .person_id = q.optional_uint("person_id").value_or(0),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .sort = parse_user_post_sort_type(q.string("sort")),
        .saved_only = q.optional_bool("saved_only")
      }, header_or_query_auth(q, m)));
    });
    JSON_ROUTE("/api/v3/user/register", Register, LoginResponse).post([controller](auto& form, auto& m) {
      return controller->register_account(form, m.ip, m.user_agent);
    });
    // TODO: /api/v3/user/get_captcha
    router.get("/api/v3/user/mentions", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      write_json<SSL>(rsp,controller->get_person_mentions({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth)));
    });
    JSON_ROUTE("/api/v3/user/mention/mark_as_read", MarkPersonMentionAsRead, PersonMentionResponse).post([controller](auto& form, auto& m) {
      return controller->mark_person_mentions_as_read(form, std::move(m.auth));
    });
    router.get("/api/v3/user/replies", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      write_json<SSL>(rsp, controller->get_replies({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit").value_or(0),
        .page = (uint16_t)q.optional_uint("page").value_or(0),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth)));
    });
    // TODO: /api/v3/user/ban
    // TODO: /api/v3/user/banned
    // TODO: /api/v3/user/block
    JSON_ROUTE("/api/v3/user/login", Login, LoginResponse).post([controller](auto& form, auto& m) {
      return controller->login(form, m.ip, m.user_agent);
    });
    router.template post_json<DeleteAccount>("/api/v3/user/delete_account", parser, [controller](auto*, auto m) {
      return [controller, m = std::move(m)](auto form, auto&& write) {
        controller->delete_account(form, std::move(m->auth));
        write(write_no_content<SSL>);
      };
    }).template post_json<PasswordReset>("/api/v3/user/password_reset", parser, [controller](auto*, auto) {
      return [controller](auto form, auto&& write) {
        controller->password_reset(form);
        write(write_no_content<SSL>);
      };
    }).template post_json<PasswordChangeAfterReset>("/api/v3/user/password_change", parser, [controller](auto*, auto) {
      return [controller](auto form, auto&& write) {
        controller->password_change_after_reset(form);
        write(write_no_content<SSL>);
      };
    });
    JSON_ROUTE("/api/v3/user/mention/mark_all_as_read", MarkAllAsRead, GetRepliesResponse).post([controller](auto& form, auto& m) {
      return controller->mark_all_as_read(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/user/save_user_settings", SaveUserSettings, LoginResponse).put([controller](auto& form, auto& m) {
      return controller->save_user_settings(form, std::move(m.auth));
    });
    JSON_ROUTE("/api/v3/user/change_password", ChangePassword, LoginResponse).put([controller](auto& form, auto& m) {
      return controller->change_password(form, std::move(m.auth));
    });
    // TODO: /api/v3/user/report_count
    // TODO: /api/v3/user/unread_count
    router.template post_json<VerifyEmail>("/api/v3/user/verify_email", parser, [controller](auto*, auto m) {
      return [controller, m = std::move(m)](auto form, auto&& write) {
        controller->verify_email(form);
        write(write_no_content<SSL>);
      };
    });
    // TODO: /api/v3/user/leave_admin
    // TODO: /api/v3/user/totp/generate
    // TODO: /api/v3/user/totp/update
    // TODO: /api/v3/user/export_settings
    // TODO: /api/v3/user/import_settings
    // TODO: /api/v3/user/list_logins
    router.get("/api/v3/user/validate_auth", [controller](auto* rsp, auto*, auto& m) {
      controller->validate_auth(std::move(m.auth));
      write_no_content(rsp);
    }).post("/api/v3/user/logout", [controller](auto*, auto m) {
      if (m->auth) controller->logout(std::move(*m->auth));
      return [controller, m = std::move(m)](auto, auto&& write) {
        write(write_no_content<SSL>);
      };
    });

    // Admin
    ///////////////////////////////////////////////////////

    // TODO: Admin endpoints

    // CustomEmoji
    ///////////////////////////////////////////////////////

    // TODO: Custom emoji

    router.any("/api/*", [](auto*, auto*, auto&) {
      throw ApiError("Endpoint does not exist or is not yet implemented", 404);
    });
  }

  template auto api_routes<true>(
    uWS::TemplatedApp<true>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void;

  template auto api_routes<false>(
    uWS::TemplatedApp<false>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void;
}
