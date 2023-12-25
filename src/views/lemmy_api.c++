#include "lemmy_api.h++"
#include "util/web.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::GenerateText,
    flatbuffers::GetRoot, flatbuffers::Offset, flatbuffers::Parser,
    std::make_shared, std::nullopt, std::optional, std::shared_ptr, std::string,
    std::string_view;

namespace Ludwig::Lemmy {
  struct Meta {
    FlatBufferBuilder fbb;
    optional<SecretString> auth;
    string ip, user_agent;
  };

  static inline auto header_or_query_auth(QueryString& q, Meta& m) -> optional<SecretString> {
    if (m.auth) return std::move(m.auth);
    return q.optional_string("auth").transform([](auto s){return SecretString(s);});
  }

  template <bool SSL>
  static inline auto write_no_content(uWS::HttpResponse<SSL>* rsp) {
    rsp->writeStatus(http_status(204))
      ->writeHeader("Access-Control-Allow-Origin", "*")
      ->end();
  }

  template <bool SSL, class Out>
  static inline auto write_json(
    uWS::HttpResponse<SSL>* rsp,
    flatbuffers::Parser& parser,
    FlatBufferBuilder& fbb
  ) -> void {
    string s;
    if (!GenerateText(parser, GetRoot<Out>(fbb.GetBufferPointer()), &s)) {
      throw ApiError("Serialization failed", 500);
    }
    rsp->writeHeader("Content-Type", "application/json; charset=utf-8")
      ->writeHeader("Access-Control-Allow-Origin", "*")
      ->end(s);
  }

  template <class In, class Out> using PostRetHandler = uWS::MoveOnlyFunction<Out (In, Meta&)>;

  template <bool SSL, class In, class Out>
  static inline auto post_json_fb(
    Router<SSL, Meta>& router,
    string pattern,
    shared_ptr<Parser> parser,
    const char table_name[],
    PostRetHandler<In&, Offset<Out>>&& handler,
    size_t max_size = 10 * 1024 * 1024
  ) -> void {
    router.template post_json_fb<In>(pattern, parser, table_name, [parser, handler = std::move(handler)](auto*, auto m) mutable {
      return [parser, handler = std::move(handler), m = std::move(m)](auto fb, auto&& write) mutable {
        m->fbb.Finish(handler(*fb, *m));
        write([parser, m = std::move(m)](auto* rsp) { write_json<SSL, Out>(rsp, *parser, m->fbb); });
      };
    }, max_size);
  }

  template <bool SSL, class In, class Out>
  static inline auto put_json_fb(
    Router<SSL, Meta>& router,
    string pattern,
    shared_ptr<Parser> parser,
    const char table_name[],
    PostRetHandler<In&, Offset<Out>>&& handler,
    size_t max_size = 10 * 1024 * 1024
  ) -> void {
    router.template put_json_fb<In>(pattern, parser, table_name, [parser, handler = std::move(handler)](auto*, auto m) mutable {
      return [parser, handler = std::move(handler), m = std::move(m)](auto fb, auto&& write) mutable {
        m->fbb.Finish(handler(*fb, *m));
        write([parser, m = std::move(m)](auto* rsp) { write_json<SSL, Out>(rsp, *parser, m->fbb); });
      };
    }, max_size);
  }

  template <bool SSL> auto api_routes(
    uWS::TemplatedApp<SSL>& app,
    shared_ptr<ApiController> controller,
    shared_ptr<KeyedRateLimiter> rate_limiter
  ) -> void {
    auto parser = make_shared<Parser>();
    Router<SSL, Meta> router(app,
      [rate_limiter](auto* rsp, auto* req) -> Meta {
        const string ip(get_ip(rsp, req));
        if (rate_limiter && !rate_limiter->try_acquire(ip, req->getMethod() == "GET" ? 1 : 10)) {
          throw ApiError("Rate limited, try again later", 429);
        }
        const auto auth = req->getHeader("authorization");
        return {
          .fbb = FlatBufferBuilder(),
          .auth = auth.starts_with("Bearer ") ? optional(SecretString(auth.substr(7))) : nullopt,
          .ip = ip,
          .user_agent = string(req->getHeader("user-agent"))
        };
      },
      [parser](auto* rsp, const ApiError& err, auto&) -> void {
        string s;
        FlatBufferBuilder fbb;
        fbb.Finish(CreateErrorDirect(fbb, err.message.c_str(), err.http_status));
        if (!GenerateText(*parser, GetRoot<Error>(fbb.GetBufferPointer()), &s)) {
          spdlog::warn("Error when writing error message! Original error: {}", err.what());
          s = R"({"error":"error when writing error message","status":500})";
        }
        rsp->writeStatus(http_status(err.http_status))
          ->writeHeader("Content-Type", "application/json; charset=utf-8")
          ->writeHeader("Access-Control-Allow-Origin", "*")
          ->end(s);
      }
    );

    // Site
    ///////////////////////////////////////////////////////

    router.get("/api/v3/site", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_site(header_or_query_auth(q, m), m.fbb));
      write_json<SSL, GetSiteResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DoCreateSite, SiteResponse>(router, "/api/v3/site", parser, "DoCreateSite", [controller](auto& form, auto& m) {
      return controller->create_site(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, EditSite, SiteResponse>(router, "/api/v3/site", parser, "EditSite", [controller](auto& form, auto& m) {
      return controller->edit_site(form, std::move(m.auth), m.fbb);
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
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_community(
        {.id=q.id("id"),.name=q.string("name")},
        header_or_query_auth(q, m), m.fbb)
      );
      write_json<SSL, CommunityResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DoCreateCommunity, CommunityResponse>(router, "/api/v3/community", parser, "DoCreateCommunity", [controller](auto& form, auto& m) {
      return controller->create_community(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, EditCommunity, CommunityResponse>(router, "/api/v3/community", parser, "EditCommunity", [controller](auto& form, auto& m) {
      return controller->edit_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/hide
    router.get("/api/v3/community/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->list_communities({
        .sort = parse_board_sort_type(q.string("sort")),
        .limit = (uint16_t)q.id("limit"),
        .page = (uint16_t)q.id("page"),
        .show_nsfw = q.optional_bool("show_nsfw")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, ListCommunitiesResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, FollowCommunity, CommunityResponse>(router, "/api/v3/community/follow", parser, "FollowCommunity", [controller](auto& form, auto& m) {
      return controller->follow_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/block
    post_json_fb<SSL, DeleteCommunity, CommunityResponse>(router, "/api/v3/community/delete", parser, "DeleteCommunity", [controller](auto& form, auto& m) {
      return controller->delete_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/remove
    // TODO: /api/v3/community/transfer
    // TODO: /api/v3/community/ban_user
    // TODO: /api/v3/community/mod

    // Post
    ///////////////////////////////////////////////////////

    router.get("/api/v3/post", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_post({
        .id = q.id("id"),
        .comment_id = q.id("comment_id")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, PostResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DoCreatePost, PostResponse>(router, "/api/v3/post", parser, "DoCreatePost", [controller](auto& form, auto& m) {
      return controller->create_post(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, EditPost, PostResponse>(router, "/api/v3/post", parser, "EditPost", [controller](auto& form, auto& m) {
      return controller->edit_post(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/post/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_posts({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.string("sort"),
        .community_name = q.string("community_name"),
        .community_id = q.id("community_id"),
        .limit = (uint16_t)q.id("limit"),
        .page = (uint16_t)q.id("page"),
        .page_cursor = q.string("page_cursor"),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, GetPostsResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DeletePost, PostResponse>(router, "/api/v3/post/delete", parser, "DeletePost", [controller](auto& form, auto& m) {
      return controller->delete_post(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/remove
    post_json_fb<SSL, MarkPostAsRead, PostResponse>(router, "/api/v3/post/mark_as_read", parser, "MarkPostAsRead", [controller](auto& form, auto& m) {
      return controller->mark_post_as_read(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/lock
    // TODO: /api/v3/post/feature
    post_json_fb<SSL, DoCreatePostLike, PostResponse>(router, "/api/v3/post/like", parser, "DoCreatePostLike", [controller](auto& form, auto& m) {
      return controller->like_post(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, SavePost, PostResponse>(router, "/api/v3/post/save", parser, "SavePost", [controller](auto& form, auto& m) {
      return controller->save_post(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/report
    // TODO: /api/v3/post/report/resolve
    // TODO: /api/v3/post/report/list
    // TODO: /api/v3/post/site_metadata

    // Comment
    ///////////////////////////////////////////////////////

    router.get("/api/v3/comment", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_comment({.id=q.id("id")}, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, CommentResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DoCreateComment, CommentResponse>(router, "/api/v3/comment", parser, "DoCreateComment", [controller](auto& form, auto& m) {
      return controller->create_comment(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, EditComment, CommentResponse>(router, "/api/v3/comment", parser, "EditComment", [controller](auto& form, auto& m) {
      return controller->edit_comment(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/comment/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_comments({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.string("sort"),
        .community_name = q.string("community_name"),
        .post_id = q.id("post_id"),
        .parent_id = q.id("parent_id"),
        .limit = (uint16_t)q.id("limit"),
        .max_depth = (uint16_t)q.id("max_depth"),
        .page = (uint16_t)q.id("page"),
        .page_cursor = q.string("page_cursor"),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, GetCommentsResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, DeleteComment, CommentResponse>(router, "/api/v3/comment/delete", parser, "DeleteComment", [controller](auto& form, auto& m) {
      return controller->delete_comment(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/comment/remove
    post_json_fb<SSL, MarkCommentReplyAsRead, CommentReplyResponse>(router, "/api/v3/comment/mark_as_read", parser, "MarkCommentReplyAsRead", [controller](auto& form, auto& m) {
      return controller->mark_comment_reply_as_read(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/comment/distinguish
    post_json_fb<SSL, DoCreateCommentLike, CommentResponse>(router, "/api/v3/comment/like", parser, "DoCreateCommentLike", [controller](auto& form, auto& m) {
      return controller->like_comment(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, SaveComment, CommentResponse>(router, "/api/v3/comment/save", parser, "SaveComment", [controller](auto& form, auto& m) {
      return controller->save_comment(form, std::move(m.auth), m.fbb);
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
      QueryString q{req->getQuery()};
      m.fbb.Finish(controller->get_person_details({
        .username = q.string("username"),
        .community_id = q.id("community_id"),
        .person_id = q.id("person_id"),
        .limit = (uint16_t)q.id("limit"),
        .page = (uint16_t)q.id("page"),
        .sort = parse_user_post_sort_type(q.string("sort")),
        .saved_only = q.optional_bool("saved_only")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL, GetPersonDetailsResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, Register, LoginResponse>(router, "/api/v3/user/register", parser, "Register", [controller](auto& form, auto& m) {
      return controller->register_account(form, m.ip, m.user_agent, m.fbb);
    });
    // TODO: /api/v3/user/get_captcha
    router.get("/api/v3/user/mentions", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      m.fbb.Finish(controller->get_person_mentions({
        .sort = parse_user_post_sort_type(q.string("sort")),
        .limit = (uint16_t)q.id("limit"),
        .page = (uint16_t)q.id("page"),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth), m.fbb));
      write_json<SSL, GetPersonMentionsResponse>(rsp, *parser, m.fbb);
    });
    post_json_fb<SSL, MarkPersonMentionAsRead, PersonMentionResponse>(router, "/api/v3/user/mention/mark_as_read", parser, "MarkPersonMentionAsRead", [controller](auto& form, auto& m) {
      return controller->mark_person_mentions_as_read(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/user/replies", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q{req->getQuery()};
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      m.fbb.Finish(controller->get_replies({
        .sort = parse_user_post_sort_type(q.string("sort")),
        .limit = (uint16_t)q.id("limit"),
        .page = (uint16_t)q.id("page"),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth), m.fbb));
      write_json<SSL, GetRepliesResponse>(rsp, *parser, m.fbb);
    });
    // TODO: /api/v3/user/ban
    // TODO: /api/v3/user/banned
    // TODO: /api/v3/user/block
    post_json_fb<SSL, Login, LoginResponse>(router, "/api/v3/user/login", parser, "Login", [controller](auto& form, auto& m) {
      return controller->login(form, m.ip, m.user_agent, m.fbb);
    });
    router.template post_json_fb<DeleteAccount>("/api/v3/user/delete_account", parser, "DeleteAccount", [controller](auto*, auto m) {
      return [controller, m = std::move(m)](auto form, auto&& write) {
        controller->delete_account(*form, std::move(m->auth));
        write(write_no_content<SSL>);
      };
    }).template post_json_fb<PasswordReset>("/api/v3/user/password_reset", parser, "PasswordReset", [controller](auto*, auto) {
      return [controller](auto form, auto&& write) {
        controller->password_reset(*form);
        write(write_no_content<SSL>);
      };
    }).template post_json_fb<PasswordChangeAfterReset>("/api/v3/user/password_change", parser, "PasswordChangeAfterReset", [controller](auto*, auto) {
      return [controller](auto form, auto&& write) {
        controller->password_change_after_reset(*form);
        write(write_no_content<SSL>);
      };
    });
    post_json_fb<SSL, MarkAllAsRead, GetRepliesResponse>(router, "/api/v3/user/mention/mark_all_as_read", parser, "MarkAllAsRead", [controller](auto& form, auto& m) {
      return controller->mark_all_as_read(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, SaveUserSettings, LoginResponse>(router, "/api/v3/user/save_user_settings", parser, "SaveUserSettings", [controller](auto& form, auto& m) {
      return controller->save_user_settings(form, std::move(m.auth), m.fbb);
    });
    put_json_fb<SSL, ChangePassword, LoginResponse>(router, "/api/v3/user/change_password", parser, "ChangePassword", [controller](auto& form, auto& m) {
      return controller->change_password(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/user/report_count
    // TODO: /api/v3/user/unread_count
    router.template post_json_fb<VerifyEmail>("/api/v3/user/verify_email", parser, "VerifyEmail", [controller](auto*, auto m) {
      return [controller, m = std::move(m)](auto form, auto&& write) {
        controller->verify_email(*form);
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
