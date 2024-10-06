#pragma once
#include "models/detail.h++"
#include "services/db.h++"
#include "controllers/instance.h++"
#include "models/lemmy_api.h++"
#include "models/enums.h++"
#include "util/router.h++"

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
    auto validate_jwt(ReadTxn& txn, SecretString&& jwt) -> uint64_t;
    auto validate_jwt(SecretString&& jwt) -> uint64_t {
      auto txn = instance->open_read_txn();
      return validate_jwt(txn, std::move(jwt));
    }
    template <typename T>
    auto optional_auth(T& form, std::optional<SecretString>&& auth) -> std::optional<uint64_t> {
      if (auth) return validate_jwt(std::move(*auth));
      if (form.auth) return validate_jwt(std::move(*form.auth));
      return {};
    }
    template <typename T>
    auto require_auth(
      T& form,
      std::optional<SecretString>&& auth,
      ReadTxn& txn,
      bool must_be_admin = false
    ) -> uint64_t {
      uint64_t id;
      if (auth) id = validate_jwt(txn, std::move(*auth));
      else if (form.auth) id = validate_jwt(txn, std::move(*form.auth));
      else throw ApiError("Auth required", 401);
      if (must_be_admin && !LocalUserDetail::get_login(txn, id).local_user().admin()) {
        throw ApiError("Admin privileges required", 403);
      }
      return id;
    }
    template <typename T>
    auto require_auth_and_keep_jwt(
      T& form,
      std::optional<SecretString>&& auth,
      ReadTxn& txn
    ) -> std::pair<uint64_t, SecretString> {
      if (auth) {
        auto tmp = auth->data;
        return { validate_jwt(txn, std::move(*auth)), SecretString(tmp) };
      }
      if (form.auth) {
        auto tmp = form.auth->data;
        return { validate_jwt(txn, std::move(*form.auth)), SecretString(tmp) };
      }
      throw ApiError("Auth required", 401);
    }
    auto login_and_get_jwt(
      WriteTxn&& txn,
      std::string_view username_or_email,
      SecretString&& password,
      std::string_view ip,
      std::string_view user_agent
    ) -> SecretString;
    auto to_comment(uint64_t id, const Ludwig::Comment& comment, std::string path = "") -> Comment;
    auto to_community(uint64_t id, const Board& board, bool hidden) -> Community;
    auto to_person(uint64_t id, const User& user, OptRef<Ludwig::LocalUser> local_user) -> Person;
    auto to_post(uint64_t id, const Thread& thread, OptRef<LinkCard> link_card) -> Post;
    auto to_comment_aggregates(const CommentDetail& detail) -> CommentAggregates;
    auto to_community_aggregates(const BoardDetail& detail) -> CommunityAggregates;
    auto to_person_aggregates(const UserDetail& detail) -> PersonAggregates;
    auto to_post_aggregates(const ThreadDetail& detail) -> PostAggregates;
    auto get_site_object() -> Site;
    auto get_site_view(ReadTxn& txn) -> SiteView;
    auto to_comment_view(ReadTxn& txn, const CommentDetail& detail) -> CommentView;
    auto get_comment_view(ReadTxn& txn, uint64_t id, std::optional<uint64_t> login_id) -> CommentView {
      const auto login = LocalUserDetail::get_login(txn, login_id);
      const auto detail = CommentDetail::get(txn, id, login);
      return to_comment_view(txn, detail);
    }
    auto to_community_view(const BoardDetail& detail) -> CommunityView;
    auto get_community_view(ReadTxn& txn, uint64_t id, std::optional<uint64_t> login_id) -> CommunityView {
      const auto login = LocalUserDetail::get_login(txn, login_id);
      const auto detail = BoardDetail::get(txn, id, login);
      return to_community_view(detail);
    }
    auto to_person_view(const UserDetail& detail) -> PersonView {
      return {
        .counts = to_person_aggregates(detail),
        .person = to_person(detail.id, detail.user(), detail.maybe_local_user())
      };
    }
    auto get_person_view(ReadTxn& txn, uint64_t id, std::optional<uint64_t> login_id) -> PersonView {
      const auto login = LocalUserDetail::get_login(txn, login_id);
      const auto detail = UserDetail::get(txn, id, login);
      return to_person_view(detail);
    }
    auto to_post_view(ReadTxn& txn, const ThreadDetail& detail) -> PostView;
    auto get_post_view(ReadTxn& txn, uint64_t id, std::optional<uint64_t> login_id) -> PostView {
      const auto login = LocalUserDetail::get_login(txn, login_id);
      const auto detail = ThreadDetail::get(txn, id, login);
      return to_post_view(txn, detail);
    }
  public:
    ApiController(std::shared_ptr<InstanceController> instance) : instance(instance) {}

    template <IsRequestContext Ctx>
    auto open_write_txn(WritePriority priority = WritePriority::Medium) {
      return instance->template open_write_txn<Ctx>(priority);
    }

    /* addAdmin */
    /* addModToCommunity */
    /* approveRegistrationApplication */
    /* banFromCommunity */
    /* banPerson */
    /* blockCommunity */
    /* blockPerson */

    auto change_password(WriteTxn txn, ChangePassword& form, std::optional<SecretString>&& auth) -> LoginResponse;

    auto create_comment(WriteTxn txn, CreateComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    /* createCommentReport */

    auto create_community(WriteTxn txn, CreateCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* createCustomEmoji */

    auto create_post(WriteTxn txn, CreatePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* createPostReport */
    /* createPrivateMessage */
    /* createPrivateMessageReport */

    auto create_site(WriteTxn txn, CreateSite& form, std::optional<SecretString>&& auth) -> SiteResponse;

    auto delete_account(WriteTxn txn, DeleteAccount& form, std::optional<SecretString>&& auth) -> void;

    auto delete_comment(WriteTxn txn, DeleteComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto delete_community(WriteTxn txn, DeleteCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* deleteCustomEmoji */

    auto delete_post(WriteTxn txn, DeletePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* deletePrivateMessage */
    /* distinguishComment */

    auto edit_comment(WriteTxn txn, EditComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto edit_community(WriteTxn txn, EditCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

    /* editCustomEmoji */

    auto edit_post(WriteTxn txn, EditPost& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* editPrivateMessage */

    auto edit_site(WriteTxn txn, EditSite& form, std::optional<SecretString>&& auth) -> SiteResponse;

    /* featurePost */

    auto follow_community(WriteTxn txn, FollowCommunity& form, std::optional<SecretString>&& auth) -> CommunityResponse;

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

    auto like_comment(WriteTxn txn, CreateCommentLike& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto like_post(WriteTxn txn, CreatePostLike& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* listCommentReports */

    auto list_communities(const ListCommunities& form, std::optional<SecretString>&& auth) -> ListCommunitiesResponse;

    /* listPostReports */
    /* listPrivateMessageReports */
    /* listRegistrationApplications */

    /* lockPost */

    auto login(WriteTxn&& txn, Login& form, std::string_view ip, std::string_view user_agent) -> LoginResponse;

    auto logout(WriteTxn txn, SecretString&& auth) -> void;

    auto mark_all_as_read(WriteTxn txn, MarkAllAsRead& form, std::optional<SecretString>&& auth) -> GetRepliesResponse;

    auto mark_comment_reply_as_read(WriteTxn txn, MarkCommentReplyAsRead& form, std::optional<SecretString>&& auth) -> CommentReplyResponse;

    auto mark_person_mentions_as_read(WriteTxn txn, MarkPersonMentionAsRead& form, std::optional<SecretString>&& auth) -> PersonMentionResponse;

    auto mark_post_as_read(WriteTxn txn, MarkPostAsRead& form, std::optional<SecretString>&& auth) -> PostResponse;

    /* markPrivateMessageAsRead */

    auto password_change_after_reset(WriteTxn&& txn, PasswordChangeAfterReset& form) -> void;

    auto password_reset(WriteTxn txn, PasswordReset& form) -> void;

    /* purgeComment */
    /* purgeCommunity */
    /* purgePerson */
    /* purgePost */

    auto register_account(WriteTxn&& txn, Register& form, std::string_view ip, std::string_view user_agent) -> std::pair<uint64_t, bool>;

    /* removeComment */
    /* removeCommunity */
    /* removePost */
    /* resolveCommentReport */
    /* resolveObject */
    /* resolvePostReport */
    /* resolvePrivateMessageReport */

    auto save_comment(WriteTxn txn, SaveComment& form, std::optional<SecretString>&& auth) -> CommentResponse;

    auto save_post(WriteTxn txn, SavePost& form, std::optional<SecretString>&& auth) -> PostResponse;

    auto save_user_settings(WriteTxn txn, SaveUserSettings& form, std::optional<SecretString>&& auth) -> LoginResponse;

    template <IsRequestContext Ctx>
    auto search(const Ctx& ctx, Search& form, std::optional<SecretString>&& auth) -> RouterAwaiter<std::vector<SearchResultDetail>, Ctx>;

    auto search_results(const std::vector<SearchResultDetail>& results) -> SearchResponse;

    /* transferCommunity */

    //auto upload_image(WriteTxn&& txn, const UploadImage& named_parameters) -> UploadImageResponse;

    auto validate_auth(std::optional<SecretString>&& auth) -> void;

    auto verify_email(WriteTxn txn, VerifyEmail& form) -> void;
  };
}
