#include "lemmy_api.h++"
#include "util/web.h++"
#include <flatbuffers/minireflect.h>

using flatbuffers::FlatBufferBuilder, flatbuffers::ToStringVisitor,
    flatbuffers::IterateFlatBuffer, flatbuffers::Offset, flatbuffers::Parser,
    std::make_shared, std::nullopt, std::optional, std::shared_ptr, std::string,
    std::string_view;

namespace Ludwig::Lemmy {
  struct Meta {
    FlatBufferBuilder fbb;
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

  template <bool SSL>
  static inline auto write_json(
    uWS::HttpResponse<SSL>* rsp,
    const flatbuffers::TypeTable* type,
    FlatBufferBuilder& fbb
  ) -> void {
    ToStringVisitor visitor("", true, "", true);
    IterateFlatBuffer(fbb.GetBufferPointer(), type, &visitor);
    rsp->writeHeader("Content-Type", "application/json; charset=utf-8")
      ->writeHeader("Access-Control-Allow-Origin", "*")
      ->end(visitor.s);
  }

  template <class In, class Out> using PostRetHandler = uWS::MoveOnlyFunction<Out (In, Meta&)>;

  template <bool SSL, class In, class Out>
  struct JsonFlatBufferRequestBuilder {
    Router<SSL, Meta>& router;
    string pattern;
    shared_ptr<Parser> parser;
    const char* in_table_name;
    const flatbuffers::TypeTable* out_type_table;
    size_t max_size = 10 * 1024 * 1024;

    auto post(PostRetHandler<In&, Offset<Out>>&& handler) -> void {
      router.template post_json_fb<In>(pattern, parser, in_table_name, [out_type_table=out_type_table, handler=std::move(handler)](auto*, auto m) mutable {
        return [out_type_table=out_type_table, handler = std::move(handler), m = std::move(m)](auto fb, auto&& write) mutable {
          m->fbb.Finish(handler(*fb, *m));
          write([out_type_table=out_type_table, m = std::move(m)](auto* rsp) { write_json<SSL>(rsp, out_type_table, m->fbb); });
        };
      }, max_size);
    }

    auto put(PostRetHandler<In&, Offset<Out>>&& handler) -> void {
      router.template put_json_fb<In>(pattern, parser, in_table_name, [out_type_table=out_type_table, handler=std::move(handler)](auto*, auto m) mutable {
        return [out_type_table=out_type_table, handler = std::move(handler), m = std::move(m)](auto fb, auto&& write) mutable {
          m->fbb.Finish(handler(*fb, *m));
          write([out_type_table=out_type_table, m = std::move(m)](auto* rsp) { write_json<SSL>(rsp, out_type_table, m->fbb); });
        };
      }, max_size);
    }
  };

# define JSON_FB(Pattern,In,Out) JsonFlatBufferRequestBuilder<SSL,In,Out>{router,Pattern,parser,#In,Out##TypeTable()}

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
        FlatBufferBuilder fbb;
        fbb.Finish(CreateErrorDirect(fbb, err.message.c_str(), err.http_status));
        ToStringVisitor visitor("", true, "", true);
        IterateFlatBuffer(fbb.GetBufferPointer(), ErrorTypeTable(), &visitor);
        rsp->writeStatus(http_status(err.http_status))
          ->writeHeader("Content-Type", "application/json; charset=utf-8")
          ->writeHeader("Access-Control-Allow-Origin", "*")
          ->end(visitor.s);
      }
    );

    // Site
    ///////////////////////////////////////////////////////

    router.get("/api/v3/site", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      m.fbb.Finish(controller->get_site(header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, GetSiteResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/site", DoCreateSite, SiteResponse).post([controller](auto& form, auto& m) {
      return controller->create_site(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/site", EditSite, SiteResponse).put([controller](auto& form, auto& m) {
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
      QueryString q(req);
      m.fbb.Finish(controller->get_community(
        {.id=q.optional_id("id"),.name=q.optional_string("name").value_or("")},
        header_or_query_auth(q, m), m.fbb)
      );
      write_json<SSL>(rsp, CommunityResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/community", DoCreateCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->create_community(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/community", EditCommunity, CommunityResponse).put([controller](auto& form, auto& m) {
      return controller->edit_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/hide
    router.get("/api/v3/community/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      m.fbb.Finish(controller->list_communities({
        .sort = parse_board_sort_type(q.string("sort")),
        .limit = (uint16_t)q.optional_uint("limit"),
        .page = (uint16_t)q.optional_uint("page"),
        .show_nsfw = q.optional_bool("show_nsfw")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, ListCommunitiesResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/community/follow", FollowCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->follow_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/block
    JSON_FB("/api/v3/community/delete", DeleteCommunity, CommunityResponse).post([controller](auto& form, auto& m) {
      return controller->delete_community(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/community/remove
    // TODO: /api/v3/community/transfer
    // TODO: /api/v3/community/ban_user
    // TODO: /api/v3/community/mod

    // Post
    ///////////////////////////////////////////////////////

    router.get("/api/v3/post", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      m.fbb.Finish(controller->get_post({
        .id = q.optional_id("id"),
        .comment_id = q.optional_id("comment_id")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, PostResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/post", DoCreatePost, PostResponse).post([controller](auto& form, auto& m) {
      return controller->create_post(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/post", EditPost, PostResponse).put([controller](auto& form, auto& m) {
      return controller->edit_post(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/post/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      spdlog::debug("{}", req->getQuery());
      QueryString q(req);
      spdlog::debug("{}", q.optional_string("type").value_or("<none>"));
      m.fbb.Finish(controller->get_posts({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .community_id = q.optional_id("community_id"),
        .limit = (uint16_t)q.optional_uint("limit"),
        .page = (uint16_t)q.optional_uint("page"),
        .page_cursor = q.string("page_cursor"),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, GetPostsResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/post/delete", DeletePost, PostResponse).post([controller](auto& form, auto& m) {
      return controller->delete_post(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/remove
    JSON_FB("/api/v3/post/mark_as_read", MarkPostAsRead, PostResponse).post([controller](auto& form, auto& m) {
      return controller->mark_post_as_read(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/lock
    // TODO: /api/v3/post/feature
    JSON_FB("/api/v3/post/like", DoCreatePostLike, PostResponse).post([controller](auto& form, auto& m) {
      return controller->like_post(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/post/save", SavePost, PostResponse).put([controller](auto& form, auto& m) {
      return controller->save_post(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/post/report
    // TODO: /api/v3/post/report/resolve
    // TODO: /api/v3/post/report/list
    // TODO: /api/v3/post/site_metadata

    // Comment
    ///////////////////////////////////////////////////////

    router.get("/api/v3/comment", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      m.fbb.Finish(controller->get_comment({.id=q.required_hex_id("id")}, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, CommentResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/comment", DoCreateComment, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->create_comment(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/comment", EditComment, CommentResponse).put([controller](auto& form, auto& m) {
      return controller->edit_comment(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/comment/list", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      m.fbb.Finish(controller->get_comments({
        .type = q.optional_string("type").transform(parse_listing_type),
        .sort = q.optional_string("sort").value_or(""),
        .community_name = q.optional_string("community_name").value_or(""),
        .post_id = q.optional_id("post_id"),
        .parent_id = q.optional_id("parent_id"),
        .limit = (uint16_t)q.optional_uint("limit"),
        .max_depth = (uint16_t)q.optional_uint("max_depth"),
        .page = (uint16_t)q.optional_uint("page"),
        .page_cursor = q.optional_string("page_cursor").value_or(""),
        .saved_only = q.optional_bool("saved_only"),
        .liked_only = q.optional_bool("liked_only"),
        .disliked_only = q.optional_bool("disliked_only"),
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, GetCommentsResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/comment/delete", DeleteComment, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->delete_comment(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/comment/remove
    JSON_FB("/api/v3/comment/mark_as_read", MarkCommentReplyAsRead, CommentReplyResponse).post([controller](auto& form, auto& m) {
      return controller->mark_comment_reply_as_read(form, std::move(m.auth), m.fbb);
    });
    // TODO: /api/v3/comment/distinguish
    JSON_FB("/api/v3/comment/like", DoCreateCommentLike, CommentResponse).post([controller](auto& form, auto& m) {
      return controller->like_comment(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/comment/save", SaveComment, CommentResponse).put([controller](auto& form, auto& m) {
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
      QueryString q(req);
      m.fbb.Finish(controller->get_person_details({
        .username = q.optional_string("username").value_or(""),
        .community_id = q.optional_id("community_id"),
        .person_id = q.optional_id("person_id"),
        .limit = (uint16_t)q.optional_uint("limit"),
        .page = (uint16_t)q.optional_uint("page"),
        .sort = parse_user_post_sort_type(q.string("sort")),
        .saved_only = q.optional_bool("saved_only")
      }, header_or_query_auth(q, m), m.fbb));
      write_json<SSL>(rsp, GetPersonDetailsResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/user/register", Register, LoginResponse).post([controller](auto& form, auto& m) {
      return controller->register_account(form, m.ip, m.user_agent, m.fbb);
    });
    // TODO: /api/v3/user/get_captcha
    router.get("/api/v3/user/mentions", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      m.fbb.Finish(controller->get_person_mentions({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit"),
        .page = (uint16_t)q.optional_uint("page"),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth), m.fbb));
      write_json<SSL>(rsp, GetPersonMentionsResponseTypeTable(), m.fbb);
    });
    JSON_FB("/api/v3/user/mention/mark_as_read", MarkPersonMentionAsRead, PersonMentionResponse).post([controller](auto& form, auto& m) {
      return controller->mark_person_mentions_as_read(form, std::move(m.auth), m.fbb);
    });
    router.get("/api/v3/user/replies", [controller, parser](auto* rsp, auto* req, auto& m) {
      QueryString q(req);
      auto auth = header_or_query_auth(q, m);
      if (!auth) throw ApiError("Auth required", 401);
      m.fbb.Finish(controller->get_replies({
        .sort = parse_user_post_sort_type(q.optional_string("sort").value_or("")),
        .limit = (uint16_t)q.optional_uint("limit"),
        .page = (uint16_t)q.optional_uint("page"),
        .unread_only = q.optional_bool("unread_only")
      }, std::move(*auth), m.fbb));
      write_json<SSL>(rsp, GetRepliesResponseTypeTable(), m.fbb);
    });
    // TODO: /api/v3/user/ban
    // TODO: /api/v3/user/banned
    // TODO: /api/v3/user/block
    JSON_FB("/api/v3/user/login", Login, LoginResponse).post([controller](auto& form, auto& m) {
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
    JSON_FB("/api/v3/user/mention/mark_all_as_read", MarkAllAsRead, GetRepliesResponse).post([controller](auto& form, auto& m) {
      return controller->mark_all_as_read(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/user/save_user_settings", SaveUserSettings, LoginResponse).put([controller](auto& form, auto& m) {
      return controller->save_user_settings(form, std::move(m.auth), m.fbb);
    });
    JSON_FB("/api/v3/user/change_password", ChangePassword, LoginResponse).put([controller](auto& form, auto& m) {
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
