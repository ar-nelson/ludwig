#pragma once
#include "services/db.h++"
#include "controllers/instance.h++"
#include "models/lemmy_api.h++"
#include "models/enums.h++"

namespace Ludwig::Lemmy {
  struct GetPost {
    uint64_t id, comment_id;
  };

  struct GetPosts {
    std::optional<ListingType> type;
    std::string_view sort, community_name;
    uint64_t community_id;
    uint16_t limit, page;
    std::string_view page_cursor;
    bool saved_only, liked_only, disliked_only;
  };

  struct GetComment {
    uint64_t id;
  };

  struct GetComments {
    std::optional<ListingType> type;
    std::string_view sort, community_name;
    uint64_t post_id, parent_id;
    uint16_t limit, max_depth, page;
    std::string_view page_cursor;
    bool saved_only, liked_only, disliked_only;
  };

  struct GetPersonMentions {
    UserPostSortType sort;
    uint16_t limit, page;
    bool unread_only;
  };

  struct GetPersonDetails {
    std::string_view username;
    uint64_t community_id, person_id;
    uint16_t limit, page;
    UserPostSortType sort;
    bool saved_only;
  };

  struct GetCommunity {
    uint64_t id;
    std::string_view name;
  };

  struct ListCommunities {
    std::optional<ListingType> type;
    BoardSortType sort;
    uint16_t limit, page;
    bool show_nsfw;
  };

  struct GetReplies {
    UserPostSortType sort;
    uint16_t limit, page;
    bool unread_only;
  };

  // TODO: GetBannedPersons
  // TODO: GetCaptcha
  // TODO: GetFederatedInstances
  // TODO: GetModlog
  // TODO: ListCommentReports

  class ApiController {
  private:
    std::shared_ptr<InstanceController> instance;
    std::shared_ptr<RichTextParser> rich_text;
    auto validate_jwt(ReadTxnBase& txn, SecretString&& jwt) -> uint64_t;
    auto validate_jwt(SecretString&& jwt) -> uint64_t {
      auto txn = instance->open_read_txn();
      return validate_jwt(txn, std::move(jwt));
    }
    template <typename T> auto optional_auth(T& form, std::optional<SecretString>&& auth) -> std::optional<uint64_t> {
      if (auth) return validate_jwt(std::move(*auth));
      if (form.auth) return validate_jwt(std::move(*form.auth));
      return {};
    }
    template <typename T> auto require_auth(T& form, std::optional<SecretString>&& auth, bool must_be_admin = false) -> uint64_t {
      uint64_t id;
      auto txn = instance->open_read_txn();
      if (auth) id = validate_jwt(txn, std::move(*auth));
      else if (form.auth) id = validate_jwt(txn, std::move(*form.auth));
      else throw ApiError("Auth required", 401);
      if (must_be_admin && !LocalUserDetail::get(txn, id).local_user().admin()) {
        throw ApiError("Admin privileges required", 403);
      }
      return id;
    }
    template <typename T> auto require_auth_and_keep_jwt(T& form, std::optional<SecretString>&& auth) -> std::pair<uint64_t, SecretString> {
      if (auth) {
        auto tmp = auth->data;
        return { validate_jwt(std::move(*auth)), SecretString(tmp) };
      }
      if (form.auth) {
        auto tmp = form.auth->data;
        return { validate_jwt(std::move(*form.auth)), SecretString(tmp) };
      }
      throw ApiError("Auth required", 401);
    }
    auto login_and_get_jwt(
      std::string_view username_or_email,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent
    ) -> SecretString;
    auto to_comment(uint64_t id, const Ludwig::Comment& comment, Ludwig::Login login) -> Comment;
    auto to_community(uint64_t id, const Board& board, bool hidden, Ludwig::Login login) -> Community;
    auto to_person(uint64_t id, const User& user, OptRef<const Ludwig::LocalUser> local_user, Ludwig::Login login) -> Person;
    auto to_post(uint64_t id, const Thread& thread, OptRef<const LinkCard> link_card, Ludwig::Login login) -> Post;
    auto to_comment_aggregates(const CommentDetail& detail) -> CommentAggregates;
    auto to_community_aggregates(const BoardDetail& detail) -> CommunityAggregates;
    auto to_person_aggregates(const UserDetail& detail) -> PersonAggregates;
    auto to_post_aggregates(const ThreadDetail& detail) -> PostAggregates;
    auto get_site_object() -> Site;
    auto get_site_view(ReadTxnBase& txn) -> SiteView;
    auto to_comment_view(ReadTxnBase& txn, const CommentDetail& detail, Ludwig::Login login) -> CommentView;
    auto get_comment_view(ReadTxnBase& txn, uint64_t id, std::optional<uint64_t> login_id) -> CommentView {
      const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      const auto detail = CommentDetail::get(txn, id, login);
      return to_comment_view(txn, detail, login);
    }
    auto to_community_view(const BoardDetail& detail, Ludwig::Login login) -> CommunityView;
    auto get_community_view(ReadTxnBase& txn, uint64_t id, std::optional<uint64_t> login_id) -> CommunityView {
      const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      const auto detail = BoardDetail::get(txn, id, login);
      return to_community_view(detail, login);
    }
    auto to_person_view(const UserDetail& detail, Ludwig::Login login) -> PersonView {
      return {
        .counts = to_person_aggregates(detail),
        .person = to_person(detail.id, detail.user(), detail.maybe_local_user(), login)
      };
    }
    auto get_person_view(ReadTxnBase& txn, uint64_t id, std::optional<uint64_t> login_id) -> PersonView {
      const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      const auto detail = UserDetail::get(txn, id, login);
      return to_person_view(detail, login);
    }
    auto to_post_view(ReadTxnBase& txn, const ThreadDetail& detail, Ludwig::Login login) -> PostView;
    auto get_post_view(ReadTxnBase& txn, uint64_t id, std::optional<uint64_t> login_id) -> PostView {
      const auto login = login_id.transform([&txn](auto id){return LocalUserDetail::get(txn, id);});
      const auto detail = ThreadDetail::get(txn, id, login);
      return to_post_view(txn, detail, login);
    }
  public:
    ApiController(std::shared_ptr<InstanceController> instance, std::shared_ptr<RichTextParser> rich_text)
      : instance(instance), rich_text(rich_text) {}

    /* addAdmin */
    /* addModToCommunity */
    /* approveRegistrationApplication */
    /* banFromCommunity */
    /* banPerson */
    /* blockCommunity */
    /* blockPerson */

    auto change_password(ChangePassword& form, std::optional<SecretString>&& auth) -> LoginResponse;

    auto create_comment(CreateComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    /* createCommentReport */

    auto create_community(CreateCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* createCustomEmoji */

    auto create_post(CreatePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* createPostReport */
    /* createPrivateMessage */
    /* createPrivateMessageReport */

    auto create_site(CreateSite& form, std::optional<SecretString>&& auth) -> SiteResponse;

    auto delete_account(DeleteAccount& form, std::optional<SecretString>&& auth) -> void;

    auto delete_comment(DeleteComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto delete_community(DeleteCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* deleteCustomEmoji */

    auto delete_post(DeletePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* deletePrivateMessage */
    /* distinguishComment */

    auto edit_comment(EditComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto edit_community(EditCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* editCustomEmoji */

    auto edit_post(EditPost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* editPrivateMessage */

    auto edit_site(EditSite& form, std::optional<SecretString>&& auth) -> SiteResponse;

    /* featurePost */

    auto follow_community(FollowCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* getBannedPersons */
    /* getCaptcha */

    auto get_comment(const GetComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto get_comments(const GetComments& form, std::optional<SecretString>&& auth) -> GetCommentsResponse;

    auto get_community(const GetCommunity& form, std::optional<SecretString>&& auth) -> GetCommunityResponse;

    /* getFederatedInstances */
    /* getModlog */

    auto get_person_details(const GetPersonDetails& form, std::optional<SecretString>&& auth) -> GetPersonDetailsResponse;

    auto get_post(const GetPost& form, std::optional<SecretString>&& auth) -> GetPostResponse;

    auto get_person_mentions(const GetPersonMentions& form, SecretString&& auth) -> GetPersonMentionsResponse;

    auto get_posts(const GetPosts& form, std::optional<SecretString>&& auth) -> GetPostsResponse;

    /* getPrivateMessages */

    auto get_replies(const GetReplies& form, SecretString&& auth) -> GetRepliesResponse;

    /* getReportCount */

    auto get_site(std::optional<SecretString>&& auth) -> GetSiteResponse;

    /* getSiteMetadata */
    /* getUnreadCount */
    /* getUnreadRegistrationApplicationCount */
    /* leaveAdmin */

    auto like_comment(CreateCommentLike& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto like_post(CreatePostLike& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* listCommentReports */

    auto list_communities(const ListCommunities& form, std::optional<SecretString>&& auth) -> ListCommunitiesResponse;

    /* listPostReports */
    /* listPrivateMessageReports */
    /* listRegistrationApplications */

    /* lockPost */

    auto login(Login& form, std::string_view ip, std::string_view user_agent) -> LoginResponse;

    auto logout(SecretString&& auth) -> void;

    auto mark_all_as_read(MarkAllAsRead& form, std::optional<SecretString>&& auth) -> GetRepliesResponse;

    auto mark_comment_reply_as_read(MarkCommentReplyAsRead& form, std::optional<SecretString>&& auth) -> CommentReplyResponse;

    auto mark_person_mentions_as_read(MarkPersonMentionAsRead& form, std::optional<SecretString>&& auth) -> PersonMentionResponse;

    auto mark_post_as_read(MarkPostAsRead& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* markPrivateMessageAsRead */

    auto password_change_after_reset(PasswordChangeAfterReset& form) -> void;

    auto password_reset(PasswordReset& form) -> void;

    /* purgeComment */
    /* purgeCommunity */
    /* purgePerson */
    /* purgePost */

    auto register_account(Register& form, std::string_view ip, std::string_view user_agent) -> LoginResponse;

    /* removeComment */
    /* removeCommunity */
    /* removePost */
    /* resolveCommentReport */
    /* resolveObject */
    /* resolvePostReport */
    /* resolvePrivateMessageReport */

    auto save_comment(SaveComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto save_post(SavePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    auto save_user_settings(SaveUserSettings& form, std::optional<SecretString>&& auth) -> LoginResponse;

    auto search(Search& form, std::optional<SecretString>&& auth, uWS::MoveOnlyFunction<void (SearchResponse)> cb) -> void;

    /* transferCommunity */

    //auto upload_image(const UploadImage& named_parameters) -> UploadImageResponse;

    auto validate_auth(std::optional<SecretString>&& auth) -> void;

    auto verify_email(VerifyEmail& form) -> void;
  };
}
