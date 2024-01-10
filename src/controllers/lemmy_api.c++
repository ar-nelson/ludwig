#include "lemmy_api.h++"
#include <stack>
#include <static_vector.hpp>
#include "models/detail.h++"
#include "util/jwt.h++"
#include "util/rich_text.h++"
#include "util/lambda_macros.h++"

using std::nullopt, std::optional, std::string, std::string_view, std::vector;
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
    SecretString&& password,
    string_view ip,
    string_view user_agent
  ) -> SecretString {
    const auto session = instance->login(username_or_email, std::move(password), ip, user_agent, true);
    auto txn = instance->open_read_txn();
    return make_jwt(session.session_id, session.expiration, txn.get_jwt_secret());
  }

  inline auto write_subscribed_type(bool subscribed) -> string_view {
    // TODO: Pending state for subscriptions
    if (subscribed) return "Subscribed";
    else return "NotSubscribed";
  }

  auto ApiController::to_comment_aggregates(const CommentDetail& detail) -> CommentAggregates {
    return {
      .id = detail.id,
      .comment_id = detail.id,
      .child_count = detail.stats().child_count(),
      .upvotes = detail.stats().upvotes(),
      .downvotes = detail.stats().downvotes(),
      .score = detail.stats().karma(),
      .hot_rank = detail.rank,
      .published = detail.created_at()
    };
  }
  auto ApiController::to_community_aggregates(const BoardDetail& detail) -> CommunityAggregates {
    return {
      .id = detail.id,
      .community_id = detail.id,
      .comments = detail.stats().comment_count(),
      .posts = detail.stats().thread_count(),
      .subscribers = detail.stats().subscriber_count(),

      // TODO: User counts
      .users_active_half_year = 0,
      .users_active_month = 0,
      .users_active_week = 0,
      .users_active_day = 0,

      .hot_rank = 0,
      .published = detail.created_at()
    };
  }
  auto ApiController::to_person_aggregates(const UserDetail& detail) -> PersonAggregates {
    return {
      .id = detail.id,
      .person_id = detail.id,
      .comment_count = detail.stats().comment_count(),
      .post_count = detail.stats().thread_count(),
      .comment_score = detail.stats().comment_karma(),
      .post_score = detail.stats().thread_karma()
    };
  }
  auto ApiController::to_post_aggregates(const ThreadDetail& detail) -> PostAggregates {
    return {
      .id = detail.id,
      .post_id = detail.id,
      .comments = detail.stats().descendant_count(),
      .upvotes = detail.stats().upvotes(),
      .downvotes = detail.stats().downvotes(),
      .score = detail.stats().karma(),
      .hot_rank = detail.rank, // TODO: distinguish hot_rank and hot_rank_active
      .hot_rank_active = detail.rank,
      .published = detail.created_at(),
      .newest_comment_time = chrono::system_clock::time_point(chrono::seconds(detail.stats().latest_comment())),
      .newest_comment_time_necro = chrono::system_clock::time_point(chrono::seconds(detail.stats().latest_comment_necro())),
      .featured_community = detail.thread().featured(),
      .featured_local = false
    };
  }

  auto ApiController::to_comment(
    uint64_t id,
    const Ludwig::Comment& comment,
    string path
  ) -> Comment {
    const auto* site = instance->site_detail();
    return {
      .id = id,
      .creator_id = comment.author(),
      .language_id = 0,
      .post_id = comment.thread(),
      .ap_id = comment.activity_url()
        ? comment.activity_url()->str()
        : fmt::format("{}/ap/activity/{:x}", site->base_url, id),
      .content = comment.content_raw()->str(),
      .path = path.empty() ? fmt::format("0.{}", id) : path,
      .published = chrono::system_clock::time_point(chrono::seconds(comment.created_at())),
      .updated = comment.updated_at().transform(λx(chrono::system_clock::time_point(chrono::seconds(x)))),
      .deleted = !!comment.deleted_at(),
      .distinguished = false,
      .local = !comment.instance(),
      .removed = comment.mod_state() >= ModState::Removed
    };
  }

  auto ApiController::to_community(uint64_t id, const Board& board, bool hidden) -> Community {
    const auto* site = instance->site_detail();
    const auto full_name = board.name()->string_view();
    const string name(full_name.substr(0, full_name.find('@')));
    return {
      .id = id,
      .instance_id = board.instance(),
      .name = name,
      .title = name,
      // Some Lemmy apps (Sync) expect URLs with *exactly* the format
      // "https://domain.example/c/name", and will do weird things otherwise.
      .actor_id = board.actor_id()
        ? board.actor_id()->str()
        : fmt::format("{}/c/{}", site->base_url, name),
      .followers_url = board.followers_url()
        ? board.followers_url()->str()
        : fmt::format("{}/ap/actor/{:x}/followers", site->base_url, id),
      .inbox_url = board.inbox_url()
        ? board.inbox_url()->str()
        : fmt::format("{}/ap/actor/{:x}/inbox", site->base_url, id),
      .published = chrono::system_clock::time_point(chrono::seconds(board.created_at())),
      .updated = board.updated_at().transform(λx(chrono::system_clock::time_point(chrono::seconds(x)))),
      .icon = board.icon_url()
        ? optional(fmt::format("{}/media/board/{}/icon.webp", site->base_url, board.name()->string_view()))
        : nullopt,
      .banner = board.banner_url()
        ? optional(fmt::format("{}/media/board/{}/banner.webp", site->base_url, board.name()->string_view()))
        : nullopt,
      .description = opt_str(board.description_raw()),
      .display_name = board.display_name()->size()
        ? optional(RichTextParser::plain_text_with_emojis_to_text_content(board.display_name_type(), board.display_name()))
        : nullopt,
      .deleted = !!board.deleted_at(),
      .hidden = hidden,
      .nsfw = !!board.content_warning(),
      .local = !board.instance(),
      .posting_restricted_to_mods = board.restricted_posting(),
      .removed = board.mod_state() >= ModState::Removed
    };
  }

  auto ApiController::to_post(uint64_t id, const Thread& thread, OptRef<LinkCard> link_card) -> Post {
    const auto* site = instance->site_detail();
    return {
      .id = id,
      .community_id = thread.board(),
      .creator_id = thread.author(),
      .language_id = 1, // TODO: Languages
      .name = RichTextParser::plain_text_with_emojis_to_text_content(thread.title_type(), thread.title()),
      .ap_id = thread.activity_url()
        ? thread.activity_url()->str()
        : fmt::format("{}/ap/activity/{:x}", site->base_url, id),
      .published = chrono::system_clock::time_point(chrono::seconds(thread.created_at())),
      .updated = thread.updated_at().transform(λx(chrono::system_clock::time_point(chrono::seconds(x)))),
      .body = opt_str(thread.content_text_raw()),
      .embed_description = link_card.and_then(λx(opt_str(x.get().description()))),
      .embed_title = link_card.and_then(λx(opt_str(x.get().title()))),
      .embed_video_url = nullopt, // TODO: Embed videos
      .thumbnail_url = link_card
        .and_then(λx(opt_sv(x.get().image_url())))
        .transform([&](auto){return fmt::format("{}/media/thread/{:x}/thumbnail.webp", site->base_url, id);}),
      .url = opt_str(thread.content_url()),
      .deleted = !!thread.deleted_at(),
      .featured_community = thread.featured(),
      .featured_local = false,
      .local = !!thread.instance(),
      .locked = thread.mod_state() >= ModState::Locked,
      .nsfw = !!thread.content_warning(),
      .removed = thread.mod_state() >= ModState::Removed
    };
  }

  auto ApiController::to_person(
    uint64_t id,
    const User& user,
    OptRef<Ludwig::LocalUser> local_user
  ) -> Person {
    const auto* site = instance->site_detail();
    const auto full_name = user.name()->string_view();
    const string name(full_name.substr(0, full_name.find('@')));
    return {
      .id = id,
      .instance_id = user.instance(),
      .name = name,
      .actor_id = user.actor_id()
        ? user.actor_id()->str()
        : fmt::format("{}/u/{}", site->base_url, name),
      .inbox_url = user.inbox_url()
        ? user.inbox_url()->str()
        : fmt::format("{}/ap/actor/{:x}/inbox", site->base_url, id),
      .published = chrono::system_clock::time_point(chrono::seconds(user.created_at())),
      .updated = user.updated_at().transform(λx(chrono::system_clock::time_point(chrono::seconds(x)))),
      // TODO: ban_expires
      .avatar = user.avatar_url()
        ? optional(fmt::format("{}/media/user/{}/avatar.webp", site->base_url, user.name()->string_view()))
        : nullopt,
      .banner = user.banner_url()
        ? optional(fmt::format("{}/media/user/{}/banner.webp", site->base_url, user.name()->string_view()))
        : nullopt,
      .bio = opt_str(user.bio_raw()),
      .display_name = user.display_name()->size()
          ? optional(RichTextParser::plain_text_with_emojis_to_text_content(user.display_name_type(), user.display_name()))
          : nullopt,
      .matrix_user_id = opt_str(user.matrix_user_id()),
      .admin = local_user.transform(λx(x.get().admin())).value_or(false),
      .banned = user.mod_state() >= ModState::Removed,
      .bot_account = user.bot(),
      .deleted = !!user.deleted_at(),
      .local = !user.instance()
    };
  }

  auto ApiController::get_site_object() -> Site {
    const auto site = instance->site_detail();
    const chrono::system_clock::time_point
      published(chrono::seconds(site->created_at)),
      updated(chrono::seconds(site->updated_at));
    return {
      .id = 0,
      .name = site->name,
      .sidebar = site->description, // TODO: distinguish sidebar and description
      .description = site->description,
      .published = published,
      .updated = updated,
      .last_refreshed_at = updated,
      .icon = site->icon_url,
      .banner = site->banner_url,
      .actor_id = site->base_url,
      .inbox_url = fmt::format("{}/inbox", site->base_url),
      .public_key = site->public_key_pem,
      .instance_id = 0
    };
  }

  auto ApiController::get_site_view(ReadTxnBase& txn) -> SiteView {
    const auto site = instance->site_detail();
    const auto& stats = txn.get_site_stats();
    const chrono::system_clock::time_point
      published(chrono::seconds(site->created_at)),
      updated(chrono::seconds(site->updated_at));
    return {
      .site = get_site_object(),
      .local_site = {
        .id = 0,
        .site_id = 0,
        .site_setup = site->setup_done,
        .enable_downvotes = site->votes_enabled && site->downvotes_enabled,
        .enable_nsfw = site->cws_enabled,
        .community_creation_admin_only = site->board_creation_admin_only,
        .require_email_verification = false, // TODO: email validation
        .application_question = site->application_question,
        .private_instance = !site->setup_done || site->require_login_to_view,
        .default_theme = "browser",
        .default_post_listing_type = to_string(home_page_type_to_listing_type(site->home_page_type)),
        .hide_modlog_mod_names = false,
        .application_email_admins = false,
        .actor_name_max_length = 256,
        .federation_enabled = false, // TODO: federation
        .captcha_enabled = false, // TODO: captcha
        .captcha_difficulty = "medium",
        .published = published,
        .updated = updated,
        .registration_mode = to_string(site->registration_enabled ?
          (site->registration_application_required || site->registration_invite_required ?
            RegistrationMode::RequireApplication :
            RegistrationMode::Open) :
          RegistrationMode::Closed),
        .reports_email_admins = false,
        .federation_signed_fetch = false,
      },
      .local_site_rate_limit = {
        0, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999,
        published, updated, 9999, 9999
      },
      .counts = {
        .site_id = 0,
        .users = stats.user_count(),
        .posts = stats.thread_count(),
        .comments = stats.comment_count(),
        .communities = stats.board_count(),

        // TODO: User counts
        .users_active_half_year = 0,
        .users_active_month = 0,
        .users_active_week = 0,
        .users_active_day = 0,
      }
    };
  }

  auto ApiController::to_community_view(const BoardDetail& detail) -> CommunityView {
    return {
      .community = to_community(detail.id, detail.board(), detail.hidden),
      .counts = to_community_aggregates(detail),
      .blocked = detail.hidden,
      .subscribed = write_subscribed_type(detail.subscribed)
    };
  }

  auto ApiController::to_comment_view(ReadTxnBase& txn, const CommentDetail& detail) -> CommentView {
    string path = "0.";
    for (uint64_t n : detail.path) fmt::format_to(std::back_inserter(path), "{}.", n);
    path += std::to_string(detail.id);
    return {
      .comment = to_comment(detail.id, detail.comment(), path),
      .community = to_community(detail.thread().board(), detail.board(), detail.board_hidden),
      .counts = to_comment_aggregates(detail),
      .creator = to_person(detail.author_id(), detail.author(), txn.get_local_user(detail.author_id())),
      .post = to_post(detail.comment().thread(), detail.thread(), {}),
      .subscribed = write_subscribed_type(detail.board_subscribed),
      .creator_banned_from_community = false, // TODO: creator_banned_from_community
      .creator_blocked = detail.user_hidden,
      .saved = detail.saved,
      .my_vote = detail.your_vote == Vote::NoVote ? nullopt : optional((int8_t)detail.your_vote)
    };
  }

  auto ApiController::to_post_view(ReadTxnBase& txn, const ThreadDetail& detail) -> PostView {
    return {
      .community = to_community(detail.thread().board(), detail.board(), detail.board_hidden),
      .counts = to_post_aggregates(detail),
      .creator = to_person(detail.author_id(), detail.author(), txn.get_local_user(detail.author_id())),
      .post = to_post(detail.id, detail.thread(), detail.link_card()),
      .unread_comments = 0, // TODO: track read/unread
      .creator_banned_from_community = false, // TODO: creator_banned_from_community
      .creator_blocked = detail.user_hidden,
      .read = 0, // TODO: track read/unread
      .saved = detail.saved,
      .subscribed = write_subscribed_type(detail.board_subscribed),
      .my_vote = detail.your_vote == Vote::NoVote ? nullopt : optional((int8_t)detail.your_vote)
    };
  }

  /* addAdmin */
  /* addModToCommunity */
  /* approveRegistrationApplication */
  /* banFromCommunity */
  /* banPerson */
  /* blockCommunity */
  /* blockPerson */

  auto ApiController::change_password(ChangePassword& form, optional<SecretString>&& auth) -> LoginResponse {
    const auto [user_id, jwt] = require_auth_and_keep_jwt(form, std::move(auth));
    if (form.new_password.data != form.new_password_verify.data) throw ApiError("Passwords do not match", 400);
    instance->change_password(user_id, std::move(form.old_password), std::move(form.new_password));
    string username;
    {
      auto txn = instance->open_read_txn();
      const auto user = txn.get_user(user_id);
      username = user.value().get().name()->str();
    }
    return { .jwt = SecretString(jwt.data), .registration_created = false, .verify_email_sent = false };
  }

  auto ApiController::create_comment(CreateComment& form, optional<SecretString>&& auth) -> CommentResponse {
    const auto user_id = require_auth(form, std::move(auth));
    const auto id = instance->create_local_comment(
      user_id,
      form.parent_id.value_or(form.post_id),
      form.content
    );
    auto txn = instance->open_read_txn();
    return {
      .comment_view = get_comment_view(txn, id, user_id)
      // TODO: what are form_id and reference_ids?
    };
  }

  /* createCommentReport */

  auto ApiController::create_community(CreateCommunity& form, optional<SecretString>&& auth) -> CommunityResponse {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: use discussion_languages
    const auto id = instance->create_local_board(
      user_id,
      form.name,
      form.title,
      form.nsfw ? optional("NSFW") : nullopt,
      false,
      form.posting_restricted_to_mods,
      false
    );
    if (form.icon || form.banner || form.description) {
      instance->update_local_board(id, user_id, {
        .description = form.description.transform(λx(x.c_str())),
        .icon_url = form.icon.transform(λx(x.c_str())),
        .banner_url = form.banner.transform(λx(x.c_str()))
      });
    }
    auto txn = instance->open_read_txn();
    return {
      .community_view = get_community_view(txn, id, user_id),
      .discussion_languages = vector<uint64_t>{1}
    };
  }

  /* createCustomEmoji */

  auto ApiController::create_post(CreatePost& form, optional<SecretString>&& auth) -> PostResponse {
    if (form.honeypot) throw ApiError("bots begone", 418);
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use language_id
    const auto id = instance->create_local_thread(
      user_id,
      form.community_id,
      form.name,
      form.url,
      form.body,
      form.nsfw ? optional("NSFW") : nullopt
    );
    auto txn = instance->open_read_txn();
    return { get_post_view(txn, id, user_id) };
  }

  /* createPostReport */
  /* createPrivateMessage */
  /* createPrivateMessageReport */

  auto ApiController::create_site(CreateSite& form, optional<SecretString>&& auth) -> SiteResponse {
    require_auth(form, std::move(auth), true);
    const auto home_page_type = form.default_post_listing_type
      .transform(parse_listing_type)
      .transform(listing_type_to_home_page_type);
    const auto registration_mode = form.registration_mode.transform(parse_registration_mode);
    // TODO: distinguish sidebar and description
    // TODO: legal_information
    // TODO: languages
    // TODO: custom rate limits
    // TODO: captcha
    // TODO: federation
    // TODO: taglines
    instance->first_run_setup({{
      .name = form.name,
      .description = form.sidebar,
      .icon_url = form.icon,
      .banner_url = form.banner,
      .application_question = form.application_question,
      .home_page_type = home_page_type,
      .votes_enabled = true,
      .downvotes_enabled = form.enable_downvotes,
      .cws_enabled = form.enable_nsfw,
      .require_login_to_view = form.private_instance,
      .board_creation_admin_only = form.community_creation_admin_only,
      .registration_enabled = registration_mode.transform(λx(x != RegistrationMode::Closed)),
      .registration_application_required = registration_mode.transform(λx(x == RegistrationMode::RequireApplication)),
    }, nullopt, nullopt, nullopt, nullopt });
    auto txn = instance->open_read_txn();
    return { .site_view = get_site_view(txn), .taglines = {} };
  }

  auto ApiController::delete_account(DeleteAccount&, optional<SecretString>&&) -> void {
    throw ApiError("Not yet implemented", 500);
  }

  auto ApiController::delete_comment(DeleteComment&, optional<SecretString>&&) -> CommentResponse {
    throw ApiError("Not yet implemented", 500);
  }

  auto ApiController::delete_community(DeleteCommunity&, optional<SecretString>&&) -> CommunityResponse {
    throw ApiError("Not yet implemented", 500);
  }

  /* deleteCustomEmoji */

  auto ApiController::delete_post(DeletePost&, optional<SecretString>&&) -> PostResponse {
    throw ApiError("Not yet implemented", 500);
  }

  /* deletePrivateMessage */
  /* distinguishComment */

  auto ApiController::edit_comment(EditComment& form, optional<SecretString>&& auth) -> CommentResponse {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use language_id
    instance->update_local_comment(form.comment_id, user_id, {
      .text_content = form.content.transform(λx(x.c_str()))
    });
    auto txn = instance->open_read_txn();
    return {
      .comment_view = get_comment_view(txn, form.comment_id, user_id),
      .form_id = form.form_id
    };
  }

  auto ApiController::edit_community(EditCommunity& form, optional<SecretString>&& auth) -> CommunityResponse {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Use discussion_languages
    instance->update_local_board(form.community_id, user_id, {
      .display_name = form.title.transform(λx(x.c_str())),
      .description = form.description.transform(λx(x.c_str())),
      .icon_url = form.icon.transform(λx(x.c_str())),
      .banner_url = form.banner.transform(λx(x.c_str())),
      .content_warning = form.nsfw ? optional("NSFW") : nullopt,
      .restricted_posting = form.posting_restricted_to_mods
    });
    auto txn = instance->open_read_txn();
    return {
      .community_view = get_community_view(txn, form.community_id, user_id),
      .discussion_languages = vector<uint64_t>{1}
    };
  }

  /* editCustomEmoji */

  auto ApiController::edit_post(EditPost& form, optional<SecretString>&& auth) -> PostResponse {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.url) throw ApiError("Updating thread URLs is not yet implemented", 500);
    // TODO: Use language_id
    // TODO: Update url
    instance->update_local_thread(form.post_id, user_id, {
      .title = form.name.transform(λx(x.c_str())),
      .text_content = form.body.transform(λx(x.c_str())),
      .content_warning = form.nsfw ? optional("NSFW") : nullopt
    });
    auto txn = instance->open_read_txn();
    return { get_post_view(txn, form.post_id, user_id) };
  }

  /* editPrivateMessage */

  auto ApiController::edit_site(EditSite& form, optional<SecretString>&& auth) -> SiteResponse {
    const auto user_id = require_auth(form, std::move(auth), true);
    const auto home_page_type = form.default_post_listing_type
      .transform(parse_listing_type)
      .transform(listing_type_to_home_page_type);
    const auto registration_mode = form.registration_mode.transform(parse_registration_mode);
    // TODO: distinguish sidebar and description
    // TODO: legal_information
    // TODO: languages
    // TODO: custom rate limits
    // TODO: captcha
    // TODO: federation
    // TODO: taglines
    instance->update_site({
      .name = form.name,
      .description = form.sidebar,
      .icon_url = form.icon,
      .banner_url = form.banner,
      .application_question = form.application_question,
      .home_page_type = home_page_type,
      .downvotes_enabled = form.enable_downvotes,
      .cws_enabled = form.enable_nsfw,
      .require_login_to_view = form.private_instance,
      .board_creation_admin_only = form.community_creation_admin_only,
      .registration_enabled = registration_mode.transform(λx(x != RegistrationMode::Closed)),
      .registration_application_required = registration_mode.transform(λx(x == RegistrationMode::RequireApplication)),
    }, user_id);
    auto txn = instance->open_read_txn();
    return { .site_view = get_site_view(txn), .taglines = {} };
  }

  /* featurePost */

  auto ApiController::follow_community(FollowCommunity& form, optional<SecretString>&& auth) -> CommunityResponse {
    const auto user_id = require_auth(form, std::move(auth));
    instance->subscribe(user_id, form.community_id, form.follow.value_or(true));
    auto txn = instance->open_read_txn();
    return {
      .community_view = get_community_view(txn, form.community_id, user_id),
      .discussion_languages = vector<uint64_t>{1}
    };
  }

  /* getBannedPersons */
  /* getCaptcha */

  auto ApiController::get_comment(const GetComment& form, optional<SecretString>&& auth) -> CommentResponse {
    auto txn = instance->open_read_txn();
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    return { .comment_view = get_comment_view(txn, form.id, login_id), .form_id = {} };
  }

  auto ApiController::get_comments(const GetComments& form, optional<SecretString>&& auth) -> GetCommentsResponse {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_comments requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = LocalUserDetail::get_login(txn, login_id);
    if ((int)!form.type + (int)!form.parent_id + (int)!form.post_id + (int)form.community_name.empty() < 3) {
      throw ApiError(R"(get_comments requires at most one of "type", "parent_id", "post_id", or "community_name")", 400);
    }
    PageCursor next;
    vector<CommentView> entries;
    if (form.parent_id || form.post_id) {
      const uint64_t parent_id = form.parent_id ? form.parent_id : form.post_id;
      spdlog::debug("Listing comments on {:x}, offset {}, limit {}", parent_id, offset, limit);
      const bool is_thread = !!txn.get_thread(parent_id);
      const auto sort = parse_comment_sort_type(form.sort, login);
      auto tree = is_thread ?
        instance->thread_detail(txn, parent_id, sort, login, {}, (uint16_t)final_total).second :
        instance->comment_detail(txn, parent_id, sort, login, {}, (uint16_t)final_total).second;
      using Iter = std::multimap<uint64_t, CommentDetail>::iterator;
      stlpb::static_vector<std::pair<Iter, Iter>, 256> stack_vec;
      std::stack stack(stack_vec);
      stack.push(tree.comments.equal_range(parent_id));
      for (uint16_t i = 0; i < final_total && !stack.empty(); i++) {
        auto& iters = stack.top();
        if (iters.first == iters.second) {
          stack.pop();
          continue;
        }
        const auto& detail = iters.first->second;
        if (i >= offset) entries.push_back(to_comment_view(txn, detail));
        if (tree.comments.contains(detail.id)) stack.push(tree.comments.equal_range(detail.id));
        iters.first++;
      }
    } else {
      uint16_t i = 0;
      const auto add_entry = [&](auto& e) {
        if (i++ >= offset) entries.push_back(to_comment_view(txn, e));
      };
      const auto sort = parse_sort_type(form.sort, login);
      if (!form.community_name.empty()) {
        if (auto board_id = txn.get_board_id_by_name(form.community_name)) {
          instance->list_board_comments(add_entry, txn, *board_id, sort, login, {}, (uint16_t)final_total);
        } else {
          throw ApiError(fmt::format("No community named \"{}\" exists", form.community_name), 410);
        }
      } else {
        const auto feed = form.type ? listing_type_to_feed(*form.type) : InstanceController::FEED_ALL;
        instance->list_feed_comments(add_entry, txn, feed, sort, login, {}, (uint16_t)final_total);
      }
    }
    spdlog::debug("Entries: {}", entries.size());
    return { entries };
  }

  auto ApiController::get_community(const GetCommunity& form, optional<SecretString>&& auth) -> GetCommunityResponse {
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
      throw ApiError(fmt::format("No community named \"{}\" exists", form.name), 410);
    }
    return {
      .community_view = get_community_view(txn, id, login_id),
      .discussion_languages = vector<uint64_t>{1},
      .site = get_site_object()
      // TODO: moderators
    };
  }

  /* getFederatedInstances */
  /* getModlog */

  auto ApiController::get_person_details(const GetPersonDetails& form, optional<SecretString>&& auth) -> GetPersonDetailsResponse {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_person_details requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = LocalUserDetail::get_login(txn, login_id);
    if ((form.person_id == 0) == form.username.empty()) {
      throw ApiError("get_person_details requires exactly one of \"person_id\" or \"username\"", 400);
    }
    uint64_t id;
    if (form.person_id) {
      id = form.person_id;
    } else if (auto name_id = txn.get_user_id_by_name(form.username)) {
      id = *name_id;
    } else {
      throw ApiError(fmt::format("No user named \"{}\" exists", form.username), 410);
    }
    vector<PostView> posts;
    vector<CommentView> comments;
    uint16_t i = 0;
    instance->list_user_threads([&](auto& e) {
      if (i++ >= offset) posts.push_back(to_post_view(txn, e));
    }, txn, id, form.sort, login, {}, (uint16_t)final_total);
    i = 0;
    instance->list_user_comments([&](auto& e) {
      if (i++ >= offset) comments.push_back(to_comment_view(txn, e));
    }, txn, id, form.sort, login, {}, (uint16_t)final_total);
    return {
      .person_view = get_person_view(txn, id, login_id),
      .comments = comments,
      .posts = posts
      // TODO: moderators
    };
  }

  auto ApiController::get_person_mentions(const GetPersonMentions&, SecretString&& auth) -> GetPersonMentionsResponse {
    validate_jwt(std::move(auth));
    // TODO: mentions
    return {};
  }

  auto ApiController::get_post(const GetPost& form, optional<SecretString>&& auth) -> GetPostResponse {
    auto txn = instance->open_read_txn();
    const auto user_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    if (!form.id == !form.comment_id) {
      throw ApiError("get_post requires exactly one of \"id\" or \"comment_id\"", 400);
    }
    uint64_t id;
    if (form.id) {
      id = form.id;
    } else {
      const auto login = LocalUserDetail::get_login(txn, user_id);
      id = CommentDetail::get(txn, form.comment_id, login).comment().thread();
    }
    const auto post_view = get_post_view(txn, id, user_id);
    return {
      .community_view = get_community_view(txn, post_view.community.id, user_id),
      .post_view = post_view
      // TODO: cross-posts
      // TODO: moderators
    };
  }

  auto ApiController::get_posts(const GetPosts& form, optional<SecretString>&& auth) -> GetPostsResponse {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("get_posts requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = LocalUserDetail::get_login(txn, login_id);
    if (((int)!form.type + (int)!form.community_id + (int)form.community_name.empty()) < 2) {
      throw ApiError(R"(get_posts requires at most one of "type", "community_id", or "community_name")", 400);
    }
    const auto sort = parse_sort_type(form.sort, login);
    vector<PostView> entries;
    uint16_t i = 0;
    const auto add_entry = [&](auto& e) {
      if (i++ >= offset) entries.push_back(to_post_view(txn, e));
    };
    uint64_t board_id = 0;
    if (form.community_id) {
      board_id = form.community_id;
    } else if (!form.community_name.empty()) {
      if (auto name_id = txn.get_board_id_by_name(form.community_name)) {
        board_id = *name_id;
      } else {
        throw ApiError(fmt::format("No community named \"{}\" exists", form.community_name), 410);
      }
    }
    if (board_id) {
      instance->list_board_threads(add_entry, txn, board_id, sort, login, {}, (uint16_t)final_total);
    } else {
      auto feed = form.type ? listing_type_to_feed(*form.type) : InstanceController::FEED_ALL;
      instance->list_feed_threads(add_entry, txn, feed, sort, login, {}, (uint16_t)final_total);
    }
    return { entries };
  }

  /* getPrivateMessages */

  auto ApiController::get_replies(const GetReplies&, SecretString&& auth) -> GetRepliesResponse {
    validate_jwt(std::move(auth));
    // TODO: Support replies (this does nothing)
    return {};
  }

  /* getReportCount */

  auto ApiController::get_site(optional<SecretString>&& auth) -> GetSiteResponse {
    auto txn = instance->open_read_txn();
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = LocalUserDetail::get_login(txn, login_id);
    vector<PersonView> admins;
    for (auto id : txn.get_admin_list()) {
      admins.push_back(get_person_view(txn, id, login_id));
    }
    return {
      .site_view = get_site_view(txn),
      .admins = admins,
      .version = "0.19.1", // Lemmy version compatibility
      .my_user = login.transform([&](auto& l) -> MyUserInfo {
        return {
          .local_user_view = {
            .local_user = {
              .id = l.id,
              .person_id = l.id,
              .interface_language = "en",
              .theme = opt_str(l.local_user().lemmy_theme()).value_or("browser"),
              .validator_time = chrono::system_clock::now(),
              .email = opt_str(l.local_user().email()),
              .accepted_application = l.local_user().accepted_application(),
              .email_verified = l.local_user().email_verified(),
              .open_links_in_new_tab = l.local_user().open_links_in_new_tab(),
              .send_notifications_to_email = l.local_user().send_notifications_to_email(),
              .show_avatars = l.local_user().show_avatars(),
              .show_bot_accounts = l.local_user().show_bot_accounts(),
              .show_new_post_notifs = l.local_user().show_new_post_notifs(),
              .show_nsfw = !l.local_user().hide_cw_posts(),
              .show_read_posts = true,
              .show_scores = l.local_user().show_karma(),
              .default_listing_type = "Subscribed",
              .default_sort_type = "Active"
            },
            .person = to_person(l.id, l.user(), l.maybe_local_user()),
            .counts = to_person_aggregates(l)
          },
          .discussion_languages = vector<uint64_t>{1}
          // TODO: List subscriptions
          // TODO: List blocks
        };
      }),
      .all_languages = vector<Language>{{ 1, "en", "English" }},
      .discussion_languages = vector<uint64_t>{1}
    };
  }

  /* getSiteMetadata */
  /* getUnreadCount */
  /* getUnreadRegistrationApplicationCount */
  /* leaveAdmin */

  auto ApiController::like_comment(CreateCommentLike& form, optional<SecretString>&& auth) -> CommentResponse {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.score > 1 || form.score < -1) throw ApiError("Invalid vote score (must be -1, 0, or 1)", 400);
    instance->vote(user_id, form.comment_id, (Vote)form.score);
    auto txn = instance->open_read_txn();
    return { get_comment_view(txn, form.comment_id, user_id), {}, {} };
  }

  auto ApiController::like_post(CreatePostLike& form, optional<SecretString>&& auth) -> PostResponse {
    const auto user_id = require_auth(form, std::move(auth));
    if (form.score > 1 || form.score < -1) throw ApiError("Invalid vote score (must be -1, 0, or 1)", 400);
    instance->vote(user_id, form.post_id, (Vote)form.score);
    auto txn = instance->open_read_txn();
    return { get_post_view(txn, form.post_id, user_id) };
  }

  /* listCommentReports */

  auto ApiController::list_communities(const ListCommunities& form, optional<SecretString>&& auth) -> ListCommunitiesResponse {
    auto txn = instance->open_read_txn();
    const auto limit = form.limit ? form.limit : ITEMS_PER_PAGE;
    if (limit < 1 || limit > 256) throw ApiError("list_communities requires 0 < limit <= 256", 400);
    const uint64_t offset = limit * form.page, final_total = offset + limit;
    if (final_total > std::numeric_limits<uint16_t>::max()) throw ApiError("Reached maximum page depth", 400);
    const auto login_id = auth.transform([&](auto&& s){return validate_jwt(txn, std::move(s));});
    const auto login = LocalUserDetail::get_login(txn, login_id);
    vector<CommunityView> entries;
    uint16_t i = 0;
    // TODO: hide nsfw
    instance->list_boards(
      [&](auto& e) {
        if (i++ >= offset) entries.push_back(to_community_view(e));
      },
      txn, form.sort,
      form.type == optional(ListingType::Local),
      form.type == optional(ListingType::Subscribed),
      login, {},
      (uint16_t)final_total
    );
    return { entries };
  }

  /* listPostReports */
  /* listPrivateMessageReports */
  /* listRegistrationApplications */
  /* lockPost */

  auto ApiController::login(Login& form, string_view ip, string_view user_agent) -> LoginResponse {
    // TODO: Specific error messages from API
    if (!form.totp_2fa_token.value_or("").empty()) {
      throw ApiError("TOTP 2FA is not supported", 400);
    }
    return {
      login_and_get_jwt(form.username_or_email, std::move(form.password), ip, user_agent),
      false, false
    };
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

  auto ApiController::mark_all_as_read(MarkAllAsRead& form, optional<SecretString>&& auth) -> GetRepliesResponse {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return {};
  }

  auto ApiController::mark_comment_reply_as_read(MarkCommentReplyAsRead& form, optional<SecretString>&& auth) -> CommentReplyResponse {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return {};
  }

  auto ApiController::mark_person_mentions_as_read(MarkPersonMentionAsRead& form, optional<SecretString>&& auth) -> PersonMentionResponse {
    require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    return {};
  }

  auto ApiController::mark_post_as_read(MarkPostAsRead& form, optional<SecretString>&& auth) -> PostResponse {
    const auto user_id = require_auth(form, std::move(auth));
    // TODO: Support mark as read (this does nothing)
    auto txn = instance->open_read_txn();
    return { get_post_view(txn, form.post_id, user_id) };
  }

  /* markPrivateMessageAsRead */

  auto ApiController::password_change_after_reset(PasswordChangeAfterReset& form) -> void {
    if (form.password.data != form.password_verify.data) throw ApiError("Passwords do not match", 400);
    instance->change_password(form.token, std::move(form.password));
  }

  auto ApiController::password_reset(PasswordReset&) -> void {
    throw ApiError("Not yet supported (no email support)", 500);
  }

  /* purgeComment */
  /* purgeCommunity */
  /* purgePerson */
  /* purgePost */

  auto ApiController::register_account(Register& form, string_view ip, string_view user_agent) -> LoginResponse {
    if (form.honeypot) throw ApiError("bots begone", 418);
    if (form.password.data != form.password_verify.data) throw ApiError("Passwords do not match", 400);
    // TODO: Use captcha
    // TODO: Use show_nsfw
    auto [id, approved] = instance->register_local_user(
      form.username,
      form.email,
      std::move(form.password),
      ip,
      user_agent,
      {},
      form.answer
    );
    return {
      login_and_get_jwt(form.username, std::move(form.password_verify), ip, user_agent),
      approved,
      false // TODO: Email verification
    };
  }

  /* removeComment */
  /* removeCommunity */
  /* removePost */
  /* resolveCommentReport */
  /* resolveObject */
  /* resolvePostReport */
  /* resolvePrivateMessageReport */

  auto ApiController::save_comment(SaveComment& form, optional<SecretString>&& auth) -> CommentResponse {
    const auto user_id = require_auth(form, std::move(auth));
    instance->save_post(user_id, form.comment_id, form.save_.value_or(true));
    auto txn = instance->open_read_txn();
    return { get_comment_view(txn, form.comment_id, user_id), {}, {} };
  }

  auto ApiController::save_post(SavePost& form, optional<SecretString>&& auth) -> PostResponse {
    const auto user_id = require_auth(form, std::move(auth));
    instance->save_post(user_id, form.post_id, form.save_.value_or(true));
    auto txn = instance->open_read_txn();
    return { get_post_view(txn, form.post_id, user_id) };
  }

  auto ApiController::save_user_settings(SaveUserSettings& form, optional<SecretString>&& auth) -> LoginResponse {
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
      .email = form.email,
      .display_name = form.display_name,
      .bio = form.bio,
      .avatar_url = form.avatar,
      .banner_url = form.banner,
      .open_links_in_new_tab = form.open_links_in_new_tab,
      .show_avatars = form.show_avatars,
      .show_bot_accounts = form.show_bot_accounts,
      .show_karma = form.show_scores,
      .hide_cw_posts = form.show_nsfw.transform(λx(!x))
    });
    return { SecretString(jwt.data), false, false };
  }

  auto ApiController::search(Search& form, optional<SecretString>&& auth, uWS::MoveOnlyFunction<void (SearchResponse)> cb) -> void {
    // TODO: Most fields (this uses very few fields)
    const auto user_id = optional_auth(form, std::move(auth));
    uint16_t limit = form.limit.value_or(ITEMS_PER_PAGE);
    if (limit > 256) throw ApiError("search requires 0 < limit <= 256", 400);
    instance->search_step_1({
      .query = form.q,
      .board_id = form.community_id.value_or(0),
      .offset = (size_t)(form.page.value_or(0) * limit),
      .limit = limit
    }, [&, limit, user_id](auto&& results){
      auto txn = instance->open_read_txn();
      const auto login = LocalUserDetail::get_login(txn, user_id);
      SearchResponse response;
      for (const auto detail : instance->search_step_2(txn, results, limit, login)) {
        std::visit(overload{
          [&](const CommentDetail& comment) { response.comments.push_back(to_comment_view(txn, comment)); },
          [&](const BoardDetail& board) { response.communities.push_back(to_community_view(board)); },
          [&](const ThreadDetail& thread) { response.posts.push_back(to_post_view(txn, thread)); },
          [&](const UserDetail& user) { response.users.push_back(to_person_view(user)); }
        }, detail);
      }
      cb(response);
    });
  }

  /* transferCommunity */

  //auto ApiController::upload_image(const UploadImage& named_parameters) -> UploadImageResponse;

  auto ApiController::validate_auth(std::optional<SecretString>&& auth) -> void {
    if (auth) validate_jwt(std::move(*auth));
  }

  auto ApiController::verify_email(VerifyEmail&) -> void {
    throw ApiError("Not yet supported (no email support)", 500);
  }
}
