#include "lemmy_api.h++"
#include <stack>
#include <spdlog/fmt/chrono.h>
#include <static_vector.hpp>
#include "models/detail.h++"
#include "util/jwt.h++"
#include "util/rich_text.h++"
#include "util/lambda_macros.h++"

using flatbuffers::FlatBufferBuilder, flatbuffers::Offset, flatbuffers::String,
    std::nullopt, std::optional, std::string, std::string_view, std::vector;
namespace chrono = std::chrono;

namespace Ludwig::Lemmy {
  static inline auto listing_type_to_feed(ListingType lt) -> uint64_t {
    switch (lt) {
      case ListingType::All: return InstanceController::FEED_ALL;
      case ListingType::Local: return InstanceController::FEED_LOCAL;
      case ListingType::Subscribed: return InstanceController::FEED_HOME;
      case ListingType::ModeratorView: throw ApiError("ModeratorView is not yet implemented", 500);
    }
  }

  static inline auto listing_type_to_home_page_type(ListingType lt) -> HomePageType {
    switch (lt) {
      case ListingType::All: return HomePageType::All;
      case ListingType::Local: return HomePageType::Local;
      case ListingType::Subscribed: return HomePageType::Subscribed;
      case ListingType::ModeratorView: throw ApiError("default_post_listing_type cannot be ModeratorView", 400);
    }
  }

  static inline auto home_page_type_to_listing_type(HomePageType ht) -> ListingType {
    switch (ht) {
      case HomePageType::All: return ListingType::All;
      case HomePageType::Subscribed: return ListingType::Subscribed;
      default: return ListingType::Local;
    }
  }

  auto ApiController::validate_jwt(ReadTxnBase& txn, SecretString&& jwt) -> uint64_t {
    if (auto parsed_jwt = parse_jwt(jwt.data, txn.get_jwt_secret())) {
      if (auto user_id = instance->validate_session(txn, parsed_jwt->sub)) return *user_id;
      throw ApiError("Invalid or expired session associated with auth token", 401);
    }
    throw ApiError("Invalid or expired auth token", 401);
  }

  auto ApiController::login_and_get_jwt(
    string_view username_or_email,
    string_view ip,
    string_view user_agent,
    SecretString&& password,
    FlatBufferBuilder& fbb
  ) -> Offset<flatbuffers::String> {
    const auto session = instance->login(username_or_email, std::move(password), ip, user_agent, true);
    auto txn = instance->open_read_txn();
    SecretString jwt(make_jwt(session.session_id, session.expiration, txn.get_jwt_secret()));
    return fbb.CreateString(jwt.data);
  }

  inline auto write_timestamp(uint64_t timestamp, FlatBufferBuilder& fbb) -> Offset<String> {
    return fbb.CreateString(fmt::format("{:%FT%TZ}",
      fmt::gmtime(chrono::system_clock::time_point(chrono::seconds(timestamp)))
    ));
  }

  inline auto write_subscribed_type(bool subscribed, FlatBufferBuilder& fbb) -> Offset<String> {
    // TODO: Pending state for subscriptions
    if (subscribed) return fbb.CreateString("Subscribed");
    else return fbb.CreateString("NotSubscribed");
  }

  auto ApiController::to_comment_aggregates(const CommentDetail& detail, FlatBufferBuilder& fbb) -> Offset<CommentAggregates> {
    const auto published = write_timestamp(detail.comment().created_at(), fbb);
    return CreateCommentAggregates(fbb,
      detail.id,
      detail.id,
      detail.stats().child_count(),
      detail.stats().upvotes(),
      detail.stats().downvotes(),
      detail.stats().karma(),
      detail.rank,
      published
    );
  }
  auto ApiController::to_community_aggregates(const BoardDetail& detail, FlatBufferBuilder& fbb) -> Offset<CommunityAggregates> {
    const auto published = write_timestamp(detail.board().created_at(), fbb);
    return CreateCommunityAggregates(fbb,
      detail.id,
      detail.id,
      detail.stats().comment_count(),
      detail.stats().thread_count(),
      detail.stats().subscriber_count(),
      // TODO: User counts
      0, 0, 0, 0, 0,
      published
    );
  }
  auto ApiController::to_person_aggregates(const UserDetail& detail, FlatBufferBuilder& fbb) -> Offset<PersonAggregates> {
    return CreatePersonAggregates(fbb,
      detail.id,
      detail.id,
      detail.stats().comment_count(),
      detail.stats().thread_count(),
      detail.stats().comment_karma(),
      detail.stats().thread_karma()
    );
  }
  auto ApiController::to_post_aggregates(const ThreadDetail& detail, FlatBufferBuilder& fbb) -> Offset<PostAggregates> {
    return CreatePostAggregates(fbb,
      detail.id,
      detail.id,
      detail.stats().descendant_count(),
      detail.stats().upvotes(),
      detail.stats().downvotes(),
      detail.stats().karma(),
      detail.rank, // TODO: distinguish hot_rank and hot_rank_active
      detail.rank,
      write_timestamp(detail.thread().created_at(), fbb),
      write_timestamp(detail.stats().latest_comment(), fbb),
      write_timestamp(detail.stats().latest_comment_necro(), fbb),
      false, // TODO: Featured posts
      false
    );
  }

  auto ApiController::to_comment(
    uint64_t id,
    const Ludwig::Comment& comment,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<Comment> {
    const auto* site = instance->site_detail();
    const Offset<String>
      ap_id = fbb.CreateString(comment.activity_url()
          ? comment.activity_url()->str()
          : fmt::format("{}/ap/activity/{:x}", site->base_url, id)),
      content = fbb.CreateString(
        rich_text->blocks_to_html(comment.content_type(), comment.content(), {
          .open_links_in_new_tab = login ? login->local_user().open_links_in_new_tab() : false
        })),
      path = fbb.CreateString(""), // TODO: What is Comment.path?
      published = write_timestamp(comment.created_at(), fbb),
      updated = comment.updated_at() ? write_timestamp(*comment.updated_at(), fbb) : 0;
    Lemmy::CommentBuilder b(fbb);
    b.add_id(id);
    b.add_creator_id(comment.author());
    b.add_language_id(0); // TODO: Languages
    b.add_post_id(comment.thread());
    b.add_ap_id(ap_id);
    b.add_content(content);
    b.add_path(path);
    b.add_published(published);
    b.add_updated(updated);
    b.add_deleted(!!comment.deleted_at());
    b.add_distinguished(false);
    b.add_local(!comment.instance());
    b.add_removed(comment.mod_state() >= ModState::Removed);
    return b.Finish();
  }

  auto ApiController::to_community(
    uint64_t id,
    const Board& board,
    bool hidden,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<Community> {
    const auto* site = instance->site_detail();
    const Offset<String>
      name = fbb.CreateString(board.name()),
      actor_id = fbb.CreateString(board.actor_id()
          ? board.actor_id()->str()
          : fmt::format("{}/ap/actor/{:x}", site->base_url, id)),
      inbox_url = fbb.CreateString(board.inbox_url()
        ? board.inbox_url()->str()
        : fmt::format("{}/ap/actor/{:x}/inbox", site->base_url, id)),
      followers_url = fbb.CreateString(board.followers_url()
        ? board.followers_url()->str()
        : fmt::format("{}/ap/actor/{:x}/followers", site->base_url, id)),
      icon = board.icon_url()
        ? fbb.CreateString(fmt::format("{}/media/board/{}/icon.webp", site->base_url, board.name()->string_view()))
        : 0,
      banner = board.banner_url()
        ? fbb.CreateString(fmt::format("{}/media/board/{}/banner.webp", site->base_url, board.name()->string_view()))
        : 0,
      description = fbb.CreateString(
        rich_text->blocks_to_html(board.description_type(), board.description(), {
          .open_links_in_new_tab = login ? login->local_user().open_links_in_new_tab() : false
        })),
      display_name = board.display_name()->size()
          ? fbb.CreateString(RichTextParser::plain_text_with_emojis_to_text_content(board.display_name_type(), board.display_name()))
          : 0,
      published = write_timestamp(board.created_at(), fbb),
      updated = board.updated_at() ? write_timestamp(*board.updated_at(), fbb) : 0;
    CommunityBuilder b(fbb);
    b.add_id(id);
    b.add_instance_id(board.instance());
    b.add_name(name);
    b.add_title(name);
    b.add_actor_id(actor_id);
    b.add_inbox_url(inbox_url);
    b.add_followers_url(followers_url);
    b.add_published(published);
    b.add_updated(updated);
    b.add_icon(icon);
    b.add_banner(banner);
    b.add_description(description);
    b.add_display_name(display_name);
    b.add_deleted(!!board.deleted_at());
    b.add_hidden(hidden);
    b.add_nsfw(!!board.content_warning());
    b.add_local(!board.instance());
    b.add_posting_restricted_to_mods(board.restricted_posting());
    b.add_removed(board.mod_state() >= ModState::Removed);
    return b.Finish();
  }

  auto ApiController::to_post(
    uint64_t id,
    const Thread& thread,
    OptRef<const LinkCard> link_card,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<Post> {
    const auto* site = instance->site_detail();
    const Offset<String>
      name = fbb.CreateString(RichTextParser::plain_text_with_emojis_to_text_content(thread.title_type(), thread.title())),
      ap_id = fbb.CreateString(thread.activity_url()
          ? thread.activity_url()->str()
          : fmt::format("{}/ap/activity/{:x}", site->base_url, id)),
      published = write_timestamp(thread.created_at(), fbb),
      updated = thread.updated_at() ? write_timestamp(*thread.updated_at(), fbb) : 0,
      body = thread.content_text()->size() ? fbb.CreateString(
        rich_text->blocks_to_html(thread.content_text_type(), thread.content_text(), {
          .open_links_in_new_tab = login ? login->local_user().open_links_in_new_tab() : false
        })) : 0,
      embed_description = link_card && link_card->get().description()
        ? fbb.CreateString(link_card->get().description())
        : 0,
      embed_title = link_card && link_card->get().title()
        ? fbb.CreateString(link_card->get().title())
        : 0,
      embed_video_url = 0, // TODO: Embed videos
      thumbnail_url = link_card && link_card->get().image_url()
        ? fbb.CreateString(fmt::format("{}/media/thread/{:x}/thumbnail.webp", site->base_url, id))
        : 0,
      url = thread.content_url() ? fbb.CreateString(thread.content_url()) : 0;
    PostBuilder b(fbb);
    b.add_id(id);
    b.add_community_id(thread.board());
    b.add_creator_id(thread.author());
    b.add_language_id(0); // TODO: Languages
    b.add_name(name);
    b.add_ap_id(ap_id);
    b.add_published(published);
    b.add_updated(updated);
    b.add_body(body);
    b.add_embed_description(embed_description);
    b.add_embed_title(embed_title);
    b.add_embed_video_url(embed_video_url);
    b.add_thumbnail_url(thumbnail_url);
    b.add_url(url);
    b.add_deleted(!!thread.deleted_at());
    b.add_featured_community(thread.featured());
    b.add_featured_local(false);
    b.add_local(!thread.instance());
    b.add_locked(thread.mod_state() >= ModState::Locked);
    b.add_nsfw(!!thread.content_warning());
    b.add_removed(thread.mod_state() >= ModState::Removed);
    return b.Finish();
  }

  auto ApiController::to_person(
    uint64_t id,
    const User& user,
    OptRef<const Ludwig::LocalUser> local_user,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<Person> {
    const auto* site = instance->site_detail();
    const Offset<String>
      name = fbb.CreateString(user.name()),
      actor_id = fbb.CreateString(user.actor_id()
        ? user.actor_id()->str()
        : fmt::format("{}/ap/actor/{:x}", site->base_url, id)),
      inbox_url = fbb.CreateString(user.inbox_url()
        ? user.inbox_url()->str()
        : fmt::format("{}/ap/actor/{:x}/inbox", site->base_url, id)),
      avatar = user.avatar_url()
        ? fbb.CreateString(fmt::format("{}/media/user/{}/avatar.webp", site->base_url, user.name()->string_view()))
        : 0,
      banner = user.banner_url()
        ? fbb.CreateString(fmt::format("{}/media/user/{}/banner.webp", site->base_url, user.name()->string_view()))
        : 0,
      bio = user.bio()->size() ? fbb.CreateString(
        rich_text->blocks_to_html(user.bio_type(), user.bio(), {
          .open_links_in_new_tab = login ? login->local_user().open_links_in_new_tab() : false
        })) : 0,
      display_name = user.display_name()->size()
          ? fbb.CreateString(RichTextParser::plain_text_with_emojis_to_text_content(user.display_name_type(), user.display_name()))
          : 0,
      matrix_user_id = fbb.CreateString(user.matrix_user_id()),
      published = write_timestamp(user.created_at(), fbb),
      updated = user.updated_at() ? write_timestamp(*user.updated_at(), fbb) : 0;
    PersonBuilder b(fbb);
    b.add_id(id);
    b.add_instance_id(user.instance());
    b.add_name(name);
    b.add_actor_id(actor_id);
    b.add_inbox_url(inbox_url);
    b.add_published(published);
    b.add_updated(updated);
    // TODO: ban_expires
    b.add_avatar(avatar);
    b.add_banner(banner);
    b.add_bio(bio);
    b.add_display_name(display_name);
    b.add_matrix_user_id(matrix_user_id);
    b.add_admin(local_user.transform(λx(x.get().admin())).value_or(false));
    b.add_banned(user.mod_state() >= ModState::Removed);
    b.add_bot_account(user.bot());
    b.add_deleted(!!user.deleted_at());
    b.add_local(!user.instance());
    return b.Finish();
  }

  auto ApiController::get_site_view(ReadTxnBase& txn, flatbuffers::FlatBufferBuilder& fbb) -> flatbuffers::Offset<SiteView> {
    const auto site = instance->site_detail();
    const auto
      name = fbb.CreateString(site->name),
      description = fbb.CreateString(site->description),
      icon = site->icon_url.transform([&](auto s){return fbb.CreateString(s);}).value_or(0),
      banner = site->banner_url.transform([&](auto s){return fbb.CreateString(s);}).value_or(0),
      actor_id = fbb.CreateString(fmt::format("{}/ap/actor/_site", site->base_url)),
      inbox_url = fbb.CreateString(fmt::format("{}/ap/actor/_site/inbox", site->base_url)),
      public_key = fbb.CreateString(site->public_key_pem),
      application_question = site->registration_application_required ?
        site->application_question.transform([&](auto s){return fbb.CreateString(s);}).value_or(0) :
        (site->registration_invite_required ? fbb.CreateString("Invite code") : 0),
      default_post_listing_type =
        fbb.CreateString(to_string(home_page_type_to_listing_type(site->home_page_type))),
      registration_mode = fbb.CreateString(to_string(site->registration_enabled ?
        (site->registration_application_required || site->registration_invite_required ?
          RegistrationMode::RequireApplication :
          RegistrationMode::Open) :
        RegistrationMode::Closed)),
      published = write_timestamp(site->created_at, fbb),
      updated = write_timestamp(site->updated_at, fbb),
      placeholder = fbb.CreateString("[not yet implemented]");
    auto sb = SiteBuilder(fbb);
    sb.add_name(name);
    sb.add_id(0);
    sb.add_instance_id(0);
    sb.add_sidebar(description); // TODO: distinguish sidebar and description
    sb.add_description(description);
    sb.add_actor_id(actor_id);
    sb.add_inbox_url(inbox_url);
    sb.add_icon(icon);
    sb.add_banner(banner);
    sb.add_published(published);
    sb.add_updated(updated);
    sb.add_last_refreshed_at(updated);
    sb.add_public_key(public_key);
    const auto site_offset = sb.Finish();
    auto lb = LocalSiteBuilder(fbb);
    lb.add_id(0);
    lb.add_site_id(0);
    lb.add_site_setup(site->setup_done);
    lb.add_enable_downvotes(site->votes_enabled && site->downvotes_enabled);
    lb.add_enable_nsfw(site->cws_enabled);
    lb.add_community_creation_admin_only(site->board_creation_admin_only);
    lb.add_require_email_verification(false); // TODO: email validation
    lb.add_application_question(application_question);
    lb.add_private_instance(site->require_login_to_view);
    lb.add_default_theme(placeholder);
    lb.add_default_post_listing_type(default_post_listing_type);
    lb.add_hide_modlog_mod_names(false);
    lb.add_application_email_admins(false);
    lb.add_actor_name_max_length(256);
    lb.add_federation_enabled(false); // TODO: federation
    lb.add_captcha_enabled(false); // TODO: captcha
    lb.add_captcha_difficulty(placeholder);
    lb.add_published(published);
    lb.add_updated(updated);
    lb.add_registration_mode(registration_mode);
    lb.add_reports_email_admins(false);
    lb.add_federation_signed_fetch(false);
    const auto local_site_offset = lb.Finish();
    const auto& stats = txn.get_site_stats();
    return CreateSiteView(fbb,
      site_offset,
      local_site_offset,
      CreateLocalSiteRateLimit(fbb,
        0, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999,
        published, updated, 9999, 9999
      ),
      CreateSiteAggregates(fbb,
        0,
        stats.user_count(),
        stats.thread_count(),
        stats.comment_count(),
        stats.board_count(),
        0,
        0,
        0,
        0
      )
    );
  }

  auto ApiController::to_community_view(
    const BoardDetail& detail,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<CommunityView> {
    return CreateCommunityView(fbb,
      to_community(detail.id, detail.board(), detail.hidden, login, fbb),
      to_community_aggregates(detail, fbb),
      detail.hidden,
      write_subscribed_type(detail.subscribed, fbb)
    );
  }

  auto ApiController::to_comment_view(
    ReadTxnBase& txn,
    const CommentDetail& detail,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<CommentView> {
    return CreateCommentView(fbb,
      to_comment(detail.id, detail.comment(), login, fbb),
      to_community(detail.thread().board(), detail.board(), detail.board_hidden, login, fbb),
      to_comment_aggregates(detail, fbb),
      to_person(detail.author_id(), detail.author(), txn.get_local_user(detail.author_id()), login, fbb),
      to_post(detail.comment().thread(), detail.thread(), {}, login, fbb),
      fbb.CreateString("NotSubscribed") // TODO: Get board subscription status here
    );
  }

  auto ApiController::to_post_view(
    ReadTxnBase& txn,
    const ThreadDetail& detail,
    Ludwig::Login login,
    FlatBufferBuilder& fbb
  ) -> Offset<PostView> {
    return CreatePostView(fbb,
      to_community(detail.thread().board(), detail.board(), detail.board_hidden, login, fbb),
      to_post_aggregates(detail, fbb),
      to_person(detail.author_id(), detail.author(), txn.get_local_user(detail.author_id()), login, fbb),
      to_post(detail.id, detail.thread(), detail.link_card(), login, fbb),
      0, // TODO: track read/unread
      false, // TODO: creator_banned_from_community
      detail.user_hidden,
      false, // TODO: track read/unread
      detail.saved,
      write_subscribed_type(false, fbb), // TODO: Support subscribing to posts
      detail.your_vote == Vote::NoVote ? nullopt : optional((int8_t)detail.your_vote)
    );
  }

  /* addAdmin */
  /* addModToCommunity */
  /* approveRegistrationApplication */
  /* banFromCommunity */
  /* banPerson */
  /* blockCommunity */
  /* blockPerson */

  auto ApiController::change_password(ChangePassword& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<LoginResponse> {
    const auto [user_id, jwt] = require_auth_and_keep_jwt(form, std::move(auth));
    SecretString new_password(form.new_password()->string_view()),
      new_password_verify(form.new_password_verify()->string_view());
    if (new_password.data != new_password_verify.data) throw ApiError("Passwords do not match", 400);
    instance->change_password(user_id, form.old_password()->string_view(), std::move(new_password));
    string username;
    {
      auto txn = instance->open_read_txn();
      const auto user = txn.get_user(user_id);
      username = user.value().get().name()->str();
    }
    return CreateLoginResponseDirect(fbb, jwt.data.c_str());
  }

  auto ApiController::create_comment(DoCreateComment& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    const auto id = instance->create_local_comment(
      user_id,
      form.parent_id().value_or(form.post_id()),
      form.content()->string_view()
    );
    auto txn = instance->open_read_txn();
    return CreateCommentResponse(fbb,
      get_comment_view(txn, id, user_id, fbb)
      // TODO: what are form_id and reference_ids?
    );
  }

  /* createCommentReport */

  auto ApiController::create_community(DoCreateCommunity& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommunityResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: use discussion_languages
    const auto id = instance->create_local_board(
      user_id,
      form.name()->string_view(),
      form.title() ? optional(form.title()->string_view()) : nullopt,
      form.nsfw() ? optional("NSFW") : nullopt,
      false,
      form.posting_restricted_to_mods(),
      false
    );
    if (form.icon() || form.banner() || form.description()) {
      instance->update_local_board(id, user_id, {
        .description = form.description() ? optional(form.description()->c_str()) : nullopt,
        .icon_url = form.icon() ? optional(form.icon()->c_str()) : nullopt,
        .banner_url = form.banner() ? optional(form.banner()->c_str()) : nullopt
      });
    }
    auto txn = instance->open_read_txn();
    return CreateCommunityResponse(fbb,
      get_community_view(txn, id, user_id, fbb),
      fbb.CreateVector(vector<uint64_t>{1})
    );
  }

  /* createCustomEmoji */

  auto ApiController::create_post(DoCreatePost& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PostResponse> {
    if (form.honeypot()) throw ApiError("bots begone", 418);
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use language_id
    const auto id = instance->create_local_thread(
      user_id,
      form.community_id(),
      form.name()->string_view(),
      form.url() ? optional(form.url()->string_view()) : nullopt,
      form.body() ? optional(form.body()->string_view()) : nullopt,
      form.nsfw() ? optional("NSFW") : nullopt
    );
    auto txn = instance->open_read_txn();
    return CreatePostResponse(fbb, get_post_view(txn, id, user_id, fbb));
  }

  /* createPostReport */
  /* createPrivateMessage */
  /* createPrivateMessageReport */

  auto ApiController::create_site(DoCreateSite& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<SiteResponse> {
    require_auth(form, std::move(auth), true);
    const auto home_page_type = opt_sv(form.default_post_listing_type())
      .transform(parse_listing_type)
      .transform(listing_type_to_home_page_type);
    const auto registration_mode = opt_sv(form.registration_mode()).transform(parse_registration_mode);
    // TODO: distinguish sidebar and description
    // TODO: legal_information
    // TODO: languages
    // TODO: custom rate limits
    // TODO: captcha
    // TODO: federation
    // TODO: taglines
    instance->first_run_setup({{
      .name = form.name()->string_view(),
      .description = opt_sv(form.sidebar()),
      .icon_url = opt_sv(form.icon()),
      .banner_url = opt_sv(form.banner()),
      .application_question = opt_sv(form.application_question()),
      .home_page_type = home_page_type,
      .votes_enabled = true,
      .downvotes_enabled = form.enable_downvotes(),
      .cws_enabled = form.enable_nsfw(),
      .require_login_to_view = form.private_instance(),
      .board_creation_admin_only = form.community_creation_admin_only(),
      .registration_enabled = registration_mode.transform(λx(x != RegistrationMode::Closed)),
      .registration_application_required = registration_mode.transform(λx(x == RegistrationMode::RequireApplication)),
    }, nullopt, nullopt, nullopt, nullopt });
    auto txn = instance->open_read_txn();
    return CreateSiteResponse(fbb, get_site_view(txn, fbb));
  }

  auto ApiController::delete_account(DeleteAccount&, optional<SecretString>&&) -> void {
    throw ApiError("Not yet implemented", 500);
  }

  auto ApiController::delete_comment(DeleteComment&, optional<SecretString>&&, FlatBufferBuilder&) -> Offset<CommentResponse> {
    throw ApiError("Not yet implemented", 500);
  }

  auto ApiController::delete_community(DeleteCommunity&, optional<SecretString>&&, FlatBufferBuilder&) -> Offset<CommunityResponse> {
    throw ApiError("Not yet implemented", 500);
  }

  /* deleteCustomEmoji */

  auto ApiController::delete_post(DeletePost&, optional<SecretString>&&, FlatBufferBuilder&) -> Offset<PostResponse> {
    throw ApiError("Not yet implemented", 500);
  }

  /* deletePrivateMessage */
  /* distinguishComment */

  auto ApiController::edit_comment(EditComment& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use language_id
    instance->update_local_comment(form.comment_id(), user_id, {
      .text_content = form.content() ? optional(form.content()->c_str()) : nullopt
    });
    auto txn = instance->open_read_txn();
    return CreateCommentResponse(fbb,
      get_comment_view(txn, form.comment_id(), user_id, fbb),
      fbb.CreateString(form.form_id())
      // TODO: what are reference_ids?
    );
  }

  auto ApiController::edit_community(EditCommunity& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommunityResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use discussion_languages
    instance->update_local_board(form.community_id(), user_id, {
      .display_name = form.title() ? optional(form.title()->c_str()) : nullopt,
      .description = form.description() ? optional(form.description()->c_str()) : nullopt,
      .icon_url = form.icon() ? optional(form.icon()->c_str()) : nullopt,
      .banner_url = form.banner() ? optional(form.banner()->c_str()) : nullopt,
      .content_warning = form.nsfw() ? optional("NSFW") : nullopt,
      .restricted_posting = form.posting_restricted_to_mods()
    });
    auto txn = instance->open_read_txn();
    return CreateCommunityResponse(fbb,
      get_community_view(txn, form.community_id(), user_id, fbb),
      fbb.CreateVector(vector<uint64_t>{1})
    );
  }

  /* editCustomEmoji */

  auto ApiController::edit_post(EditPost& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PostResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.url()) throw ApiError("Updating thread URLs is not yet implemented", 500);
    // TODO: Use language_id
    // TODO: Update url
    instance->update_local_thread(form.post_id(), user_id, {
      .title = form.name() ? optional(form.name()->c_str()) : nullopt,
      .text_content = form.body() ? optional(form.body()->c_str()) : nullopt,
      .content_warning = form.nsfw() ? optional("NSFW") : nullopt
    });
    auto txn = instance->open_read_txn();
    return CreatePostResponse(fbb, get_post_view(txn, form.post_id(), user_id, fbb));
  }

  /* editPrivateMessage */

  auto ApiController::edit_site(EditSite& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<SiteResponse> {
    const auto user_id = require_auth(form, std::move(auth), true);
    const auto home_page_type = opt_sv(form.default_post_listing_type())
      .transform(parse_listing_type)
      .transform(listing_type_to_home_page_type);
    const auto registration_mode = opt_sv(form.registration_mode()).transform(parse_registration_mode);
    // TODO: distinguish sidebar and description
    // TODO: legal_information
    // TODO: languages
    // TODO: custom rate limits
    // TODO: captcha
    // TODO: federation
    // TODO: taglines
    instance->update_site({
      .name = opt_sv(form.name()),
      .description = opt_sv(form.sidebar()),
      .icon_url = opt_sv(form.icon()),
      .banner_url = opt_sv(form.banner()),
      .application_question = opt_sv(form.application_question()),
      .home_page_type = home_page_type,
      .downvotes_enabled = form.enable_downvotes(),
      .cws_enabled = form.enable_nsfw(),
      .require_login_to_view = form.private_instance(),
      .board_creation_admin_only = form.community_creation_admin_only(),
      .registration_enabled = registration_mode.transform(λx(x != RegistrationMode::Closed)),
      .registration_application_required = registration_mode.transform(λx(x == RegistrationMode::RequireApplication)),
    }, user_id);
    auto txn = instance->open_read_txn();
    return CreateSiteResponse(fbb, get_site_view(txn, fbb));
  }

  /* featurePost */

  auto ApiController::follow_community(FollowCommunity& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommunityResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    instance->subscribe(user_id, form.community_id(), form.follow());
    auto txn = instance->open_read_txn();
    return CreateCommunityResponse(fbb,
      get_community_view(txn, form.community_id(), user_id, fbb),
      fbb.CreateVector(vector<uint64_t>{1})
    );
  }

  /* getBannedPersons */
  /* getCaptcha */

  auto ApiController::get_comment(const GetComment& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentResponse> {
    auto txn = instance->open_read_txn();
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    return CreateCommentResponse(fbb, get_comment_view(txn, form.id, login_id, fbb));
  }

  auto ApiController::get_comments(const GetComments& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetCommentsResponse> {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_comments requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
    if (!form.type + !form.parent_id + form.community_name.empty() != 2) {
      throw ApiError(R"(get_comments requires exactly one of "type", "parent_id", or "community_name")", 400);
    }
    PageCursor next;
    stlpb::static_vector<Offset<CommentView>, 256> entries;
    if (form.parent_id) {
      const bool is_thread = !!txn.get_thread(form.parent_id);
      const auto sort = parse_comment_sort_type(form.sort, login);
      auto tree = is_thread ?
        instance->thread_detail(txn, form.parent_id, sort, login, {}, (uint16_t)final_total).second :
        instance->comment_detail(txn, form.parent_id, sort, login, {}, (uint16_t)final_total).second;
      using Iter = std::multimap<uint64_t, CommentDetail>::iterator;
      stlpb::static_vector<std::pair<Iter, Iter>, 256> stack_vec;
      std::stack stack(stack_vec);
      stack.push(tree.comments.equal_range(form.parent_id));
      for (uint16_t i = 0; i < final_total && !stack.empty(); i++) {
        auto iters = stack.top();
        if (iters.first == iters.second) {
          stack.pop();
          continue;
        }
        const auto& detail = iters.first->second;
        if (i >= offset) entries.push_back(to_comment_view(txn, detail, login, fbb));
        if (tree.comments.contains(detail.id)) stack.push(tree.comments.equal_range(detail.id));
        iters.first++;
      }
    } else {
      uint16_t i = 0;
      const auto add_entry = [&](auto& e) {
        if (i++ >= offset) entries.push_back(to_comment_view(txn, e, login, fbb));
      };
      const auto sort = parse_sort_type(form.sort, login);
      if (form.type) {
        instance->list_feed_comments(add_entry, txn, listing_type_to_feed(*form.type), sort, login, {}, (uint16_t)final_total);
      } else if (auto board_id = txn.get_board_id_by_name(form.community_name)) {
        instance->list_board_comments(add_entry, txn, *board_id, sort, login, {}, (uint16_t)final_total);
      } else {
        throw ApiError(fmt::format("No community named \"{}\" exists", form.community_name), 404);
      }
    }
    return CreateGetCommentsResponse(fbb, fbb.CreateVector(entries.data(), entries.size()));
  }

  auto ApiController::get_community(const GetCommunity& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetCommunityResponse> {
    auto txn = instance->open_read_txn();
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    if (!form.id == form.name.empty()) {
      throw ApiError("get_community requires exactly one of \"id\" or \"name\"", 400);
    }
    uint64_t id;
    if (form.id) {
      id = form.id;
    } else if (auto name_id = txn.get_board_id_by_name(form.name)) {
      id = *name_id;
    } else {
      throw ApiError(fmt::format("No community named \"{}\" exists", form.name), 404);
    }
    return CreateGetCommunityResponse(fbb,
      get_community_view(txn, id, login_id, fbb),
      fbb.CreateVector(vector<uint64_t>{1})
      // TODO: moderators
    );
  }

  /* getFederatedInstances */
  /* getModlog */

  auto ApiController::get_person_details(const GetPersonDetails& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetPersonDetailsResponse> {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_person_details requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
    if ((form.person_id == 0) == !form.username.empty()) {
      throw ApiError("get_person_details requires exactly one of \"person_id\" or \"username\"", 400);
    }
    uint64_t id;
    if (form.person_id) {
      id = form.person_id;
    } else if (auto name_id = txn.get_user_id_by_name(form.username)) {
      id = *name_id;
    } else {
      throw ApiError(fmt::format("No user named \"{}\" exists", form.username), 404);
    }
    stlpb::static_vector<Offset<PostView>, 256> posts;
    stlpb::static_vector<Offset<CommentView>, 256> comments;
    uint16_t i = 0;
    instance->list_user_threads([&](auto& e) {
      if (i++ >= offset) posts.push_back(to_post_view(txn, e, login, fbb));
    }, txn, id, form.sort, login, {}, (uint16_t)final_total);
    i = 0;
    instance->list_user_comments([&](auto& e) {
      if (i++ >= offset) comments.push_back(to_comment_view(txn, e, login, fbb));
    }, txn, id, form.sort, login, {}, (uint16_t)final_total);
    return CreateGetPersonDetailsResponse(fbb,
      get_person_view(txn, id, login_id, fbb),
      fbb.CreateVector(comments.data(), comments.size()),
      0, // TODO: moderators
      fbb.CreateVector(posts.data(), posts.size())
    );
  }

  auto ApiController::get_person_mentions(const GetPersonMentions&, SecretString&& auth, FlatBufferBuilder& fbb) -> Offset<GetPersonMentionsResponse> {
    validate_jwt(std::move(auth));
    // TODO: mentions
    return CreateGetPersonMentionsResponse(fbb);
  }

  auto ApiController::get_post(const GetPost& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetPostResponse> {
    auto txn = instance->open_read_txn();
    const auto user_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    if (!form.id == !form.comment_id) {
      throw ApiError("get_post requires exactly one of \"id\" or \"comment_id\"", 400);
    }
    uint64_t id;
    if (form.id) {
      id = form.id;
    } else {
      const auto login = user_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      id = CommentDetail::get(txn, form.comment_id, login).comment().thread();
    }
    const auto post_view = get_post_view(txn, id, user_id, fbb);
    return CreateGetPostResponse(fbb,
      get_community_view(txn, flatbuffers::GetTemporaryPointer(fbb, post_view)->community()->id(), user_id, fbb),
      post_view
      // TODO: cross-posts
      // TODO: moderators
    );
  }

  auto ApiController::get_posts(const GetPosts& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetPostsResponse> {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_posts requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
    const int missing = (int)!form.type + (int)!form.community_id + (int)form.community_name.empty();
    if (missing != 2) {
      throw ApiError(R"(get_posts requires exactly one of "type", "community_id", or "community_name" (missing = )" + std::to_string(missing), 400);
    }
    const auto sort = parse_sort_type(form.sort, login);
    stlpb::static_vector<Offset<PostView>, 256> entries;
    uint16_t i = 0;
    const auto add_entry = [&](auto& e) {
      if (i++ >= offset) entries.push_back(to_post_view(txn, e, login, fbb));
    };
    if (form.type) {
      instance->list_feed_threads(add_entry, txn, listing_type_to_feed(*form.type), sort, login, {}, (uint16_t)final_total);
    } else {
      uint64_t board_id = 0;
      if (form.community_id) {
        board_id = form.community_id;
      } else if (auto name_id = txn.get_board_id_by_name(form.community_name)) {
        board_id = *name_id;
      } else {
        throw ApiError(fmt::format("No community named \"{}\" exists", form.community_name), 404);
      }
      instance->list_board_threads(add_entry, txn, board_id, sort, login, {}, (uint16_t)final_total);
    }
    return CreateGetPostsResponse(fbb, fbb.CreateVector(entries.data(), entries.size()));
  }

  /* getPrivateMessages */

  auto ApiController::get_replies(const GetReplies&, SecretString&& auth, FlatBufferBuilder& fbb) -> Offset<GetRepliesResponse> {
    validate_jwt(std::move(auth));
    // TODO: Support replies (this does nothing)
    return CreateGetRepliesResponse(fbb);
  }

  /* getReportCount */

  auto ApiController::get_site(optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetSiteResponse> {
    auto txn = instance->open_read_txn();
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
    const auto en = fbb.CreateString("en"); // TODO: languages
    const auto admins = txn.get_admin_list();
    return CreateGetSiteResponse(fbb,
      get_site_view(txn, fbb),
      fbb.CreateVector<Offset<PersonView>>(admins.size(), [&](size_t i) {
        return get_person_view(txn, admins[i], login_id, fbb);
      }),
      fbb.CreateString("0.19"), // Lemmy version compatibility
      login.transform([&](auto& l){
        return CreateMyUserInfo(fbb,
          CreateLocalUserView(fbb,
            CreateLocalUser(fbb,
              l.id, l.id,
              en,
              fbb.CreateString(l.local_user().lemmy_theme()),
              write_timestamp(now_s(), fbb),
              fbb.CreateString(l.local_user().email()),
              0,
              l.local_user().accepted_application(),
              l.local_user().email_verified(),
              l.local_user().open_links_in_new_tab(),
              l.local_user().send_notifications_to_email(),
              l.local_user().show_avatars(),
              l.local_user().show_bot_accounts(),
              l.local_user().show_new_post_notifs(),
              !l.local_user().hide_cw_posts(),
              true,
              l.local_user().show_karma(),
              fbb.CreateString("Subscribed"),
              fbb.CreateString("Active")
            ),
            to_person(l.id, l.user(), l.maybe_local_user(), l, fbb),
            to_person_aggregates(l, fbb)
          ),
          0,
          fbb.CreateVector(vector<uint64_t>{1})
          // TODO: List subscriptions
          // TODO: List blocks
        );
      }).value_or(0),
      fbb.CreateVector(vector{CreateLanguage(fbb, 1, en, fbb.CreateString("English"))}),
      fbb.CreateVector(vector<uint64_t>{1})
    );
  }

  /* getSiteMetadata */
  /* getUnreadCount */
  /* getUnreadRegistrationApplicationCount */
  /* leaveAdmin */

  auto ApiController::like_comment(DoCreateCommentLike& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.score() > 1 || form.score() < -1) throw ApiError("Invalid vote score (must be -1, 0, or 1)", 400);
    instance->vote(user_id, form.comment_id(), (Vote)form.score());
    auto txn = instance->open_read_txn();
    return CreateCommentResponse(fbb, get_comment_view(txn, form.comment_id(), user_id, fbb));
  }

  auto ApiController::like_post(DoCreatePostLike& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PostResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.score() > 1 || form.score() < -1) throw ApiError("Invalid vote score (must be -1, 0, or 1)", 400);
    instance->vote(user_id, form.post_id(), (Vote)form.score());
    auto txn = instance->open_read_txn();
    return CreatePostResponse(fbb, get_post_view(txn, form.post_id(), user_id, fbb));
  }

  /* listCommentReports */

  auto ApiController::list_communities(const ListCommunities& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<ListCommunitiesResponse> {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("list_communities requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
    stlpb::static_vector<Offset<CommunityView>, 256> entries;
    uint16_t i = 0;
    // TODO: hide nsfw
    instance->list_boards(
      [&](auto& e) {
        if (i++ >= offset) entries.push_back(to_community_view(e, login, fbb));
      },
      txn, form.sort,
      form.type == optional(ListingType::Local),
      form.type == optional(ListingType::Subscribed),
      login, {},
      (uint16_t)final_total
    );
    return CreateListCommunitiesResponse(fbb, fbb.CreateVector(entries.data(), entries.size()));
  }

  /* listPostReports */
  /* listPrivateMessageReports */
  /* listRegistrationApplications */
  /* lockPost */

  auto ApiController::login(Login& form, string_view ip, string_view user_agent, FlatBufferBuilder& fbb) -> Offset<LoginResponse> {
    // TODO: Specific error messages from API
    if (form.totp_2fa_token()) throw ApiError("TOTP 2FA is not supported", 400);
    return CreateLoginResponse(fbb,
      login_and_get_jwt(form.username_or_email()->string_view(), SecretString(form.password()->string_view()), ip, user_agent, fbb)
    );
  }

  auto ApiController::logout(SecretString&& auth) -> void {
    uint64_t session_id = 0;
    {
      auto txn = instance->open_read_txn();
      if (const auto jwt = parse_jwt(auth.data, txn.get_jwt_secret())) {
        session_id = jwt->sub;
      }
    }
    if (session_id) instance->delete_session(session_id);
  }

  auto ApiController::mark_all_as_read(MarkAllAsRead& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<GetRepliesResponse> {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return CreateGetRepliesResponse(fbb);
  }

  auto ApiController::mark_comment_reply_as_read(MarkCommentReplyAsRead& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentReplyResponse> {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return CreateCommentReplyResponse(fbb);
  }

  auto ApiController::mark_person_mentions_as_read(MarkPersonMentionAsRead& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PersonMentionResponse> {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return CreatePersonMentionResponse(fbb);
  }

  auto ApiController::mark_post_as_read(MarkPostAsRead& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PostResponse> {
    auto txn = instance->open_read_txn();
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return CreatePostResponse(fbb, get_post_view(txn, form.post_id(), user_id, fbb));
  }

  /* markPrivateMessageAsRead */

  auto ApiController::password_change_after_reset(PasswordChangeAfterReset& form) -> void {
    SecretString password(form.password()->string_view()), password_verify(form.password_verify()->string_view());
    if (password.data != password_verify.data) throw ApiError("Passwords do not match", 400);
    instance->change_password(form.token()->string_view(), std::move(password));
  }

  auto ApiController::password_reset(PasswordReset&) -> void {
    throw ApiError("Not yet supported (no email support)", 500);
  }

  /* purgeComment */
  /* purgeCommunity */
  /* purgePerson */
  /* purgePost */

  auto ApiController::register_account(Register& form, string_view ip, string_view user_agent, FlatBufferBuilder& fbb) -> Offset<LoginResponse> {
    if (form.honeypot()) throw ApiError("bots begone", 418);
    SecretString password(form.password()->string_view()), password_verify(form.password_verify()->string_view());
    if (password.data != password_verify.data) throw ApiError("Passwords do not match", 400);
    // TODO: Use captcha
    // TODO: Use show_nsfw
    auto [id, approved] = instance->register_local_user(
      form.username()->string_view(),
      form.email()->string_view(),
      std::move(password),
      ip,
      user_agent,
      {},
      form.answer() ? optional(form.answer()->string_view()) : nullopt
    );
    return CreateLoginResponse(fbb,
      login_and_get_jwt(form.username()->string_view(), std::move(password_verify), ip, user_agent, fbb),
      approved,
      false // TODO: Email verification
    );
  }

  /* removeComment */
  /* removeCommunity */
  /* removePost */
  /* resolveCommentReport */
  /* resolveObject */
  /* resolvePostReport */
  /* resolvePrivateMessageReport */

  auto ApiController::save_comment(SaveComment& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<CommentResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    instance->save_post(user_id, form.comment_id(), form.save_());
    auto txn = instance->open_read_txn();
    return CreateCommentResponse(fbb, get_comment_view(txn, form.comment_id(), user_id, fbb));
  }

  auto ApiController::save_post(SavePost& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<PostResponse> {
    const auto user_id = require_auth(form, std::move(auth));
    instance->save_post(user_id, form.post_id(), form.save_());
    auto txn = instance->open_read_txn();
    return CreatePostResponse(fbb, get_post_view(txn, form.post_id(), user_id, fbb));
  }

  auto ApiController::save_user_settings(SaveUserSettings& form, optional<SecretString>&& auth, FlatBufferBuilder& fbb) -> Offset<LoginResponse> {
    const auto [user_id, jwt] = require_auth_and_keep_jwt(form, std::move(auth));
    // TODO: show_new_post_notifs
    // TODO: show_read_posts
    // TODO: discussion_languages
    // TODO: default_listing_type
    // TODO: default_sort_type
    // TODO: matrix_user_id
    // TODO: generate_totp_2fa
    // TODO: send_notifications_to_email
    // TODO: theme
    instance->update_local_user(user_id, user_id, {
      .email = opt_c_str(form.email()),
      .display_name = opt_sv(form.display_name()),
      .bio = opt_sv(form.bio()),
      .avatar_url = opt_sv(form.avatar()),
      .banner_url = opt_sv(form.banner()),
      .open_links_in_new_tab = form.open_links_in_new_tab(),
      .show_avatars = form.show_avatars(),
      .show_bot_accounts = form.show_bot_accounts(),
      .show_karma = form.show_scores(),
      .hide_cw_posts = form.show_nsfw().transform(λx(!x))
    });
    return CreateLoginResponseDirect(fbb, jwt.data.c_str());
  }

  auto ApiController::search(Search& form, optional<SecretString>&& auth, uWS::MoveOnlyFunction<void (FlatBufferBuilder&&, Offset<SearchResponse>)> cb) -> void {
    // TODO: Most fields (this uses very few fields)
    const auto user_id = optional_auth(form, std::move(auth));
    uint16_t limit = form.limit().value_or(ITEMS_PER_PAGE);
    if (limit > 256) throw ApiError("search requires 0 < limit <= 256", 400);
    instance->search_step_1({
      .query = form.q()->string_view(),
      .board_id = form.community_id().value_or(0),
      .offset = (size_t)(form.page().value_or(0) * limit),
      .limit = limit
    }, [&, limit, user_id](auto&& results){
      auto txn = instance->open_read_txn();
      const auto login = user_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      FlatBufferBuilder fbb;
      stlpb::static_vector<Offset<CommentView>, 256> comments;
      stlpb::static_vector<Offset<CommunityView>, 256> communities;
      stlpb::static_vector<Offset<PostView>, 256> posts;
      stlpb::static_vector<Offset<PersonView>, 256> users;
      for (const auto detail : instance->search_step_2(txn, results, limit, login)) {
        std::visit(overload{
          [&](const CommentDetail& comment) { comments.push_back(to_comment_view(txn, comment, login, fbb)); },
          [&](const BoardDetail& board) { communities.push_back(to_community_view(board, login, fbb)); },
          [&](const ThreadDetail& thread) { posts.push_back(to_post_view(txn, thread, login, fbb)); },
          [&](const UserDetail& user) { users.push_back(to_person_view(user, login, fbb)); }
        }, detail);
      }
      const auto offset = CreateSearchResponse(fbb,
        fbb.CreateVector(comments.data(), comments.size()),
        fbb.CreateVector(communities.data(), communities.size()),
        fbb.CreateVector(posts.data(), posts.size()),
        fbb.CreateVector(users.data(), users.size())
      );
      cb(std::move(fbb), offset);
    });
  }

  /* transferCommunity */

  //auto ApiController::upload_image(const UploadImage& named_parameters, FlatBufferBuilder& fbb) -> Offset<UploadImageResponse>;

  auto ApiController::validate_auth(std::optional<SecretString>&& auth) -> void {
    if (auth) validate_jwt(std::move(*auth));
  }

  auto ApiController::verify_email(VerifyEmail&) -> void {
    throw ApiError("Not yet supported (no email support)", 500);
  }
}
