#ifndef X
#  include <util/common.h++>
#  define XBEGIN(name) namespace Ludwig::Lemmy { struct name {
#  define X(field, type) type field;
#  define XEND }; }
#endif

#define String std::string
#define StringView std::string_view
#define Option std::optional
#define Vector std::vector

XBEGIN(AddAdmin)
X(auth, Option<SecretString>)
X(person_id, uint64_t)
X(added, bool)
XEND

XBEGIN(PersonAggregates)
X(id, uint64_t)
X(person_id, uint64_t)
X(comment_count, uint64_t)
X(post_count, uint64_t)
X(comment_score, int64_t)
X(post_score, int64_t)
XEND

XBEGIN(Person)
X(id, uint64_t)
X(instance_id, uint64_t)
X(name, String)
X(actor_id, String)
X(inbox_url, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(ban_expires, Option<Timestamp>)
X(avatar, Option<String>)
X(banner, Option<String>)
X(bio, Option<String>)
X(display_name, Option<String>)
X(matrix_user_id, Option<String>)
X(admin, bool)
X(banned, bool)
X(bot_account, bool)
X(deleted, bool)
X(local, Option<bool>)
XEND

XBEGIN(PersonView)
X(counts, Lemmy::PersonAggregates)
X(person, Lemmy::Person)
XEND

XBEGIN(AddAdminResponse)
X(admins, Vector<Lemmy::PersonView>)
XEND

XBEGIN(AddModToCommunity)
X(auth, Option<SecretString>)
X(person_id, uint64_t)
X(community_id, uint64_t)
X(added, bool)
XEND

XBEGIN(Community)
X(id, uint64_t)
X(instance_id, uint64_t)
X(name, String)
X(title, String)
X(actor_id, String)
X(followers_url, String)
X(inbox_url, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(icon, Option<String>)
X(banner, Option<String>)
X(description, Option<String>)
X(display_name, Option<String>)
X(deleted, bool)
X(hidden, bool)
X(nsfw, bool)
X(local, Option<bool>)
X(posting_restricted_to_mods, bool)
X(removed, bool)
XEND

XBEGIN(CommunityModeratorView)
X(community, Lemmy::Community)
X(moderator, Lemmy::Person)
XEND

XBEGIN(AddModToCommunityResponse)
X(moderators, Vector<Lemmy::CommunityModeratorView>)
XEND

// TODO: AdminPurgeComment
// TODO: AdminPurgeCommentView
// TODO: AdminPurgeCommunity
// TODO: AdminPurgeCommunityView
// TODO: AdminPurgePerson
// TODO: AdminPurgePersonView
// TODO: AdminPurgePost
// TODO: AdminPurgePersonView
// TODO: ApproveRegistrationApplication
// TODO: BanFromCommunity
// TODO: BanFromCommunityResponse
// TODO: BanPerson
// TODO: BanPersonResponse
// TODO: BannedPersonsResponse

XBEGIN(BlockInstance)
X(instance_id, uint64_t)
X(block, bool)
XEND

XBEGIN(BlockInstanceResponse)
X(blocked, bool)
XEND

// TODO: CaptchaResponse

XBEGIN(ChangePassword)
X(auth, Option<SecretString>)
X(new_password, SecretString)
X(new_password_verify, SecretString)
X(old_password, SecretString)
XEND

XBEGIN(Comment)
X(id, uint64_t)
X(creator_id, uint64_t)
X(language_id, uint64_t)
X(post_id, uint64_t)
X(ap_id, String)
X(content, String)
X(path, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(deleted, bool)
X(distinguished, bool)
X(local, Option<bool>)
X(removed, bool)
XEND

XBEGIN(CommentAggregates)
X(id, uint64_t)
X(comment_id, uint64_t)
X(child_count, uint64_t)
X(upvotes, uint64_t)
X(downvotes, uint64_t)
X(score, int64_t)
X(hot_rank, double)
X(published, Timestamp)
XEND

XBEGIN(CommentReply)
X(id, uint64_t)
X(comment_id, uint64_t)
X(recipient_id, uint64_t)
X(published, Timestamp)
X(read, bool)
XEND

XBEGIN(Post)
X(id, uint64_t)
X(community_id, uint64_t)
X(creator_id, uint64_t)
X(language_id, uint64_t)
X(name, String)
X(ap_id, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(body, Option<String>)
X(embed_description, Option<String>)
X(embed_title, Option<String>)
X(embed_video_url, Option<String>)
X(thumbnail_url, Option<String>)
X(url, Option<String>)
X(deleted, bool)
X(featured_community, bool)
X(featured_local, bool)
X(local, Option<bool>)
X(locked, bool)
X(nsfw, bool)
X(removed, bool)
XEND

XBEGIN(CommentReplyView)
X(comment, Lemmy::Comment)
X(comment_reply, Lemmy::CommentReply)
X(community, Lemmy::Community)
X(counts, Lemmy::CommentAggregates)
X(creator, Lemmy::Person)
X(recipient, Lemmy::Person)
X(post, Lemmy::Post)
X(subscribed, StringView) // enum SubscribedType
X(creator_banned_from_community, bool)
X(creator_blocked, bool)
X(saved, bool)
X(my_vote, Option<int8_t>)
XEND

XBEGIN(CommentReplyResponse)
X(comment_reply_view, Lemmy::CommentReplyView)
XEND

// TODO: CommentReport
// TODO: CommentReportResponse
// TODO: CommentReportView

XBEGIN(CommentView)
X(comment, Lemmy::Comment)
X(community, Lemmy::Community)
X(counts, Lemmy::CommentAggregates)
X(creator, Lemmy::Person)
X(post, Lemmy::Post)
X(subscribed, StringView) // enum SubscribedType
X(creator_banned_from_community, bool)
X(creator_blocked, bool)
X(saved, bool)
X(my_vote, Option<int8_t>)
XEND

XBEGIN(CommentResponse)
X(comment_view, Lemmy::CommentView)
X(form_id, Option<String>)
X(recipient_ids, Vector<uint64_t>)
XEND

XBEGIN(CommunityAggregates)
X(id, uint64_t)
X(community_id, uint64_t)
X(comments, uint64_t)
X(posts, uint64_t)
X(subscribers, uint64_t)
X(users_active_half_year, uint32_t)
X(users_active_month, uint32_t)
X(users_active_week, uint32_t)
X(users_active_day, uint32_t)
X(hot_rank, double)
X(published, Timestamp)
XEND

XBEGIN(CommunityBlockView)
X(community, Lemmy::Community)
X(person, Lemmy::Person)
XEND

XBEGIN(CommunityFollowerView)
X(community, Lemmy::Community)
X(follower, Lemmy::Person)
XEND

XBEGIN(CommunityJoin)
X(community_id, uint64_t)
XEND

XBEGIN(CommunityJoinResponse)
X(joined, bool)
XEND

XBEGIN(CommunityView)
X(community, Lemmy::Community)
X(counts, Lemmy::CommunityAggregates)
X(blocked, bool)
X(subscribed, StringView) // enum SubscribedType
XEND

XBEGIN(CommunityResponse)
X(community_view, Lemmy::CommunityView)
X(discussion_languages, Vector<uint64_t>)
XEND

XBEGIN(CreateComment)
X(id, Option<uint64_t>)
X(auth, Option<SecretString>)
X(content, String)
X(post_id, uint64_t)
X(form_id, Option<String>)
X(language_id, Option<uint64_t>)
X(parent_id, Option<uint64_t>)
XEND

XBEGIN(CreateCommentLike)
X(auth, Option<SecretString>)
X(comment_id, uint64_t)
X(score, int8_t)
XEND

// TODO: CreateCommentReport

XBEGIN(CreateCommunity)
X(auth, Option<SecretString>)
X(name, String)
X(title, String)
X(discussion_languages, Vector<uint64_t>)
X(banner, Option<String>)
X(description, Option<String>)
X(icon, Option<String>)
X(nsfw, bool)
X(posting_restricted_to_mods, bool)
XEND

// TODO: CreateCustomEmoji

XBEGIN(CreatePost)
X(auth, Option<SecretString>)
X(name, String)
X(community_id, uint64_t)
X(body, Option<String>)
X(honeypot, Option<String>)
X(url, Option<String>)
X(language_id, Option<uint64_t>)
X(nsfw, bool)
XEND

XBEGIN(CreatePostLike)
X(auth, Option<SecretString>)
X(post_id, uint64_t)
X(score, int8_t)
XEND

// TODO: CreatePostReport
// TODO: CreatePrivateMessage
// TODO: CreatePrivateMessageReport

XBEGIN(CreateSite)
X(auth, Option<SecretString>)
X(name, String)
X(sidebar, Option<String>)
X(description, Option<String>)
X(icon, Option<String>)
X(banner, Option<String>)
X(enable_downvotes, Option<bool>)
X(enable_nsfw, Option<bool>)
X(community_creation_admin_only, Option<bool>)
X(require_email_verification, Option<bool>)
X(application_question, Option<String>)
X(private_instance, Option<bool>)
X(default_theme, Option<String>)
X(default_post_listing_type, Option<String>) // enum ListingType
X(legal_information, Option<String>)
X(application_email_admins, Option<bool>)
X(hide_modlog_mod_names, Option<bool>)
X(discussion_languages, Vector<uint64_t>)
X(slur_filter_regex, Option<String>)
X(actor_name_max_length, Option<uint64_t>)
X(rate_limit_message, Option<uint64_t>)
X(rate_limit_message_per_second, Option<uint64_t>)
X(rate_limit_post, Option<uint64_t>)
X(rate_limit_post_per_second, Option<uint64_t>)
X(rate_limit_register, Option<uint64_t>)
X(rate_limit_register_per_second, Option<uint64_t>)
X(rate_limit_image, Option<uint64_t>)
X(rate_limit_image_per_second, Option<uint64_t>)
X(rate_limit_comment, Option<uint64_t>)
X(rate_limit_comment_per_second, Option<uint64_t>)
X(rate_limit_search, Option<uint64_t>)
X(rate_limit_search_per_second, Option<uint64_t>)
X(federation_enabled, Option<bool>)
X(federation_debug, Option<bool>)
X(captcha_enabled, Option<bool>)
X(captcha_difficulty, Option<String>)
X(allowed_instances, Vector<String>)
X(blocked_instances, Vector<String>)
X(taglines, Vector<String>)
X(registration_mode, Option<StringView>) // enum RegistrationMode
XEND

XBEGIN(CustomEmoji)
X(id, uint64_t)
X(local_site_id, uint64_t)
X(shortcode, String)
X(image_url, String)
X(alt_text, String)
X(category, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
XEND

XBEGIN(CustomEmojiKeyword)
X(custom_emoji_id, uint64_t)
X(keyword, String)
XEND

// TODO: CustomEmojiResponse

XBEGIN(CustomEmojiView)
X(custom_emoji, Lemmy::CustomEmoji)
X(keywords, Vector<Lemmy::CustomEmojiKeyword>)
XEND

XBEGIN(DeleteAccount)
X(auth, Option<SecretString>)
X(password, SecretString)
XEND

XBEGIN(DeleteComment)
X(auth, Option<SecretString>)
X(comment_id, uint64_t)
X(deleted, Option<bool>)
XEND

XBEGIN(DeleteCommunity)
X(auth, Option<SecretString>)
X(community_id, uint64_t)
X(deleted, Option<bool>)
XEND

// TODO: DeleteCustomEmoji
// TODO: DeleteCustomEmojiResponse

XBEGIN(DeletePost)
X(auth, Option<SecretString>)
X(post_id, uint64_t)
X(deleted, Option<bool>)
XEND

// TODO: DeletePrivateMessage
// TODO: DistinguishComment

XBEGIN(EditComment)
X(auth, Option<SecretString>)
X(comment_id, uint64_t)
X(content, Option<String>)
X(form_id, Option<String>)
X(language_id, Option<uint64_t>)
XEND

XBEGIN(EditCommunity)
X(auth, Option<SecretString>)
X(community_id, uint64_t)
X(banner, Option<String>)
X(description, Option<String>)
X(icon, Option<String>)
X(title, Option<String>)
X(discussion_languages, Vector<uint64_t>)
X(nsfw, bool)
X(posting_restricted_to_mods, bool)
XEND

// TODO: EditCustomEmoji

XBEGIN(EditPost)
X(auth, Option<SecretString>)
X(post_id, uint64_t)
X(body, Option<String>)
X(name, Option<String>)
X(url, Option<String>)
X(nsfw, bool)
X(language_id, Option<uint64_t>)
XEND

// TODO: EditPrivateMessage

XBEGIN(EditSite)
X(auth, Option<SecretString>)
X(name, Option<String>)
X(sidebar, Option<String>)
X(description, Option<String>)
X(icon, Option<String>)
X(banner, Option<String>)
X(enable_downvotes, Option<bool>)
X(enable_nsfw, Option<bool>)
X(community_creation_admin_only, Option<bool>)
X(require_email_verification, Option<bool>)
X(application_question, Option<String>)
X(private_instance, Option<bool>)
X(default_theme, Option<String>)
X(default_post_listing_type, Option<StringView>) // enum ListingType
X(legal_information, Option<String>)
X(application_email_admins, Option<bool>)
X(hide_modlog_mod_names, Option<bool>)
X(discussion_languages, Vector<uint64_t>)
X(slur_filter_regex, Option<String>)
X(actor_name_max_length, Option<uint64_t>)
X(rate_limit_message, Option<uint64_t>)
X(rate_limit_message_per_second, Option<uint64_t>)
X(rate_limit_post, Option<uint64_t>)
X(rate_limit_post_per_second, Option<uint64_t>)
X(rate_limit_register, Option<uint64_t>)
X(rate_limit_register_per_second, Option<uint64_t>)
X(rate_limit_image, Option<uint64_t>)
X(rate_limit_image_per_second, Option<uint64_t>)
X(rate_limit_comment, Option<uint64_t>)
X(rate_limit_comment_per_second, Option<uint64_t>)
X(rate_limit_search, Option<uint64_t>)
X(rate_limit_search_per_second, Option<uint64_t>)
X(federation_enabled, Option<bool>)
X(federation_debug, Option<bool>)
X(captcha_enabled, Option<bool>)
X(captcha_difficulty, Option<String>)
X(allowed_instances, Vector<String>)
X(blocked_instances, Vector<String>)
X(taglines, Vector<String>)
X(registration_mode, Option<StringView>) // enum RegistrationMode
X(reports_email_admins, Option<bool>)
XEND

// TODO: FeaturePost
// TODO: FederatedInstances

XBEGIN(FollowCommunity)
X(auth, Option<SecretString>)
X(community_id, uint64_t)
X(follow, Option<bool>)
XEND

// TODO: GetCaptchaResponse

XBEGIN(GetCommentsResponse)
X(comments, Vector<Lemmy::CommentView>)
XEND

XBEGIN(Site)
X(id, uint64_t)
X(name, String)
X(sidebar, Option<String>)
X(description, Option<String>)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(last_refreshed_at, Timestamp)
X(icon, Option<String>)
X(banner, Option<String>)
X(actor_id, String)
X(inbox_url, String)
X(private_key, Option<String>)
X(public_key, String)
X(instance_id, uint64_t)
XEND

XBEGIN(GetCommunityResponse)
X(community_view, Lemmy::CommunityView)
X(discussion_languages, Vector<uint64_t>)
X(moderators, Vector<Lemmy::CommunityModeratorView>)
X(site, Lemmy::Site)
XEND

// TODO: GetFederatedInstancesResponse
// TODO: GetModlogResponse

XBEGIN(PostAggregates)
X(id, uint64_t)
X(post_id, uint64_t)
X(comments, uint64_t)
X(upvotes, uint64_t)
X(downvotes, uint64_t)
X(score, int64_t)
X(hot_rank, double)
X(hot_rank_active, double)
X(published, Timestamp)
X(newest_comment_time, Timestamp)
X(newest_comment_time_necro, Timestamp)
X(featured_community, bool)
X(featured_local, bool)
XEND

XBEGIN(PostView)
X(community, Lemmy::Community)
X(counts, Lemmy::PostAggregates)
X(creator, Lemmy::Person)
X(post, Lemmy::Post)
X(unread_comments, uint64_t)
X(creator_banned_from_community, bool)
X(creator_blocked, bool)
X(read, bool)
X(saved, bool)
X(subscribed, StringView) // enum SubscribedType
X(my_vote, Option<int8_t>)
XEND

XBEGIN(GetPersonDetailsResponse)
X(person_view, Lemmy::PersonView)
X(comments, Vector<Lemmy::CommentView>)
X(moderates, Vector<Lemmy::CommunityModeratorView>)
X(posts, Vector<Lemmy::PostView>)
XEND

XBEGIN(PersonMention)
X(id, uint64_t)
X(comment_id, uint64_t)
X(recipient_id, uint64_t)
X(published, Timestamp)
X(read, bool)
XEND

XBEGIN(PersonMentionView)
X(comment, Lemmy::Comment)
X(community, Lemmy::Community)
X(counts, Lemmy::CommentAggregates)
X(creator, Lemmy::Person)
X(person_mention, Lemmy::PersonMention)
X(post, Lemmy::Post)
X(creator_blocked, bool)
X(creator_banned_from_community, bool)
X(saved, bool)
X(subscribed, StringView) // enum SubscribedType
X(my_vote, Option<int8_t>)
XEND

XBEGIN(GetPersonMentionsResponse)
X(mentions, Vector<Lemmy::PersonMentionView>)
XEND

XBEGIN(GetPostResponse)
X(community_view, Lemmy::CommunityView)
X(post_view, Lemmy::PostView)
X(cross_posts, Vector<Lemmy::PostView>)
X(moderators, Vector<Lemmy::CommunityModeratorView>)
XEND

XBEGIN(GetPostsResponse)
X(posts, Vector<Lemmy::PostView>)
XEND

// TODO: GetPrivateMessages

XBEGIN(GetRepliesResponse)
X(replies, Vector<Lemmy::CommentReplyView>)
XEND

// TODO: GetReportCountResponse
// TODO: GetSiteMetadataResponse

XBEGIN(LocalSite)
X(id, uint64_t)
X(site_id, uint64_t)
X(site_setup, bool)
X(enable_downvotes, bool)
X(enable_nsfw, bool)
X(community_creation_admin_only, bool)
X(require_email_verification, bool)
X(application_question, Option<String>)
X(private_instance, bool)
X(default_theme, String)
X(default_post_listing_type, StringView) // enum ListingType
X(legal_information, Option<String>)
X(hide_modlog_mod_names, bool)
X(application_email_admins, bool)
X(slur_filter_regex, Option<String>)
X(actor_name_max_length, uint64_t)
X(federation_enabled, bool)
X(captcha_enabled, bool)
X(captcha_difficulty, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(registration_mode, StringView) // enum RegistrationMode
X(reports_email_admins, bool)
X(federation_signed_fetch, bool)
XEND

XBEGIN(LocalSiteRateLimit)
X(local_site_id, uint64_t)
X(message, uint64_t)
X(message_per_second, uint64_t)
X(post, uint64_t)
X(post_per_second, uint64_t)
X(register_, uint64_t)
X(register_per_second, uint64_t)
X(image, uint64_t)
X(image_per_second, uint64_t)
X(comment, uint64_t)
X(comment_per_second, uint64_t)
X(search, uint64_t)
X(search_per_second, uint64_t)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(import_user_settings, uint64_t)
X(import_user_settings_per_second, uint64_t)
XEND

XBEGIN(SiteAggregates)
X(site_id, uint64_t)
X(users, uint64_t)
X(posts, uint64_t)
X(comments, uint64_t)
X(communities, uint64_t)
X(users_active_half_year, uint32_t)
X(users_active_month, uint32_t)
X(users_active_week, uint32_t)
X(users_active_day, uint32_t)
XEND

XBEGIN(SiteView)
X(site, Lemmy::Site)
X(local_site, Lemmy::LocalSite)
X(local_site_rate_limit, Lemmy::LocalSiteRateLimit)
X(counts, Lemmy::SiteAggregates)
XEND

XBEGIN(LocalUser)
X(id, uint64_t)
X(person_id, uint64_t)
X(interface_language, String)
X(theme, String)
X(validator_time, Timestamp)
X(email, Option<String>)
X(totp_2fa_url, Option<String>)
X(accepted_application, bool)
X(email_verified, bool)
X(open_links_in_new_tab, bool)
X(send_notifications_to_email, bool)
X(show_avatars, bool)
X(show_bot_accounts, bool)
X(show_new_post_notifs, bool)
X(show_nsfw, bool)
X(show_read_posts, bool)
X(show_scores, bool)
X(default_listing_type, StringView) // enum ListingType
X(default_sort_type, StringView) // enum SortType
XEND

XBEGIN(LocalUserView)
X(local_user, Lemmy::LocalUser)
X(person, Lemmy::Person)
X(counts, Lemmy::PersonAggregates)
XEND

XBEGIN(Language)
X(id, uint64_t)
X(code, String)
X(name, String)
XEND

XBEGIN(PersonBlockView)
X(person, Lemmy::Person)
X(target, Lemmy::Person)
XEND

XBEGIN(MyUserInfo)
X(local_user_view, Lemmy::LocalUserView)
X(community_blocks, Vector<Lemmy::CommunityBlockView>)
X(discussion_languages, Vector<uint64_t>)
X(follows, Vector<Lemmy::CommunityFollowerView>)
X(moderates, Vector<Lemmy::CommunityModeratorView>)
X(person_blocks, Vector<Lemmy::PersonBlockView>)
XEND

XBEGIN(Tagline)
X(id, uint64_t)
X(local_site_id, uint64_t)
X(content, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
XEND

XBEGIN(GetSiteResponse)
X(site_view, Lemmy::SiteView)
X(admins, Vector<Lemmy::PersonView>)
X(version, String)
X(my_user, Option<Lemmy::MyUserInfo>)
X(all_languages, Vector<Lemmy::Language>)
X(discussion_languages, Vector<uint64_t>)
X(taglines, Vector<Lemmy::Tagline>)
X(custom_emojis, Vector<Lemmy::CustomEmojiView>)
XEND

// TODO: GetUnreadCountResponse
// TODO: GetUnreadRegistrationApplicationCountResponse

XBEGIN(HideCommunity)
X(auth, Option<SecretString>)
X(community_id, uint64_t)
X(hidden, bool)
X(reason, Option<String>)
XEND

XBEGIN(ImageFile)
X(file, String)
X(delete_token, String)
XEND

XBEGIN(Instance)
X(id, uint64_t)
X(domain, String)
X(published, Timestamp)
X(updated, Option<Timestamp>)
X(software, Option<String>)
X(version, Option<String>)
XEND

// TODO: LeaveAdmin
// TODO: ListCommentReportsResponse

XBEGIN(ListCommunitiesResponse)
X(communities, Vector<Lemmy::CommunityView>)
XEND

// TODO: ListPostReports
// TODO: ListPostReportsResponse
// TODO: ListPrivateMessageReports
// TODO: ListPrivateMessageReportsResponse
// TODO: ListRegistrationApplications
// TODO: ListRegistrationApplicationsResponse

// TODO: LockPost

XBEGIN(Login)
X(username_or_email, String)
X(password, SecretString)
X(totp_2fa_token, Option<String>)
XEND

XBEGIN(LoginResponse)
X(jwt, SecretString)
X(registration_created, bool)
X(verify_email_sent, bool)
XEND


XBEGIN(MarkAllAsRead)
X(auth, Option<SecretString>)
XEND

XBEGIN(MarkCommentReplyAsRead)
X(auth, Option<SecretString>)
X(comment_reply_id, uint64_t)
X(read, bool)
XEND

XBEGIN(MarkPersonMentionAsRead)
X(auth, Option<SecretString>)
X(person_mention_id, uint64_t)
X(read, bool)
XEND

XBEGIN(MarkPostAsRead)
X(auth, Option<SecretString>)
X(post_id, uint64_t)
X(read, bool)
XEND

// TODO: MarkPrivateMessageAsRead
// TODO: ModAdd
// TODO: ModAddCommunity
// TODO: ModAddCommunityView
// TODO: ModAddView
// TODO: ModBan
// TODO: ModBanFromCommunity
// TODO: ModBanFromCommunityView
// TODO: ModBanView
// TODO: ModFeaturePost
// TODO: ModFeaturePostView
// TODO: ModHideCommunity
// TODO: ModHideCommunityView
// TODO: ModJoin
// TODO: ModJoinResponse
// TODO: ModLockPost
// TODO: ModLockPostView
// TODO: ModRemoveComment
// TODO: ModRemoveCommentView
// TODO: ModRemoveCommunity
// TODO: ModRemoveCommunityView
// TODO: ModRemovePost
// TODO: ModRemovePostView
// TODO: ModTransferCommunity
// TODO: ModTransferCommunityView
// TODO: ModlogListParams

XBEGIN(PasswordChangeAfterReset)
X(password, SecretString)
X(password_verify, SecretString)
X(token, String)
XEND

XBEGIN(PasswordReset)
X(email, String)
XEND

XBEGIN(PersonMentionResponse)
X(person_mention_view, Lemmy::PersonMentionView)
XEND

XBEGIN(PostJoin)
X(post_id, uint64_t)
XEND

XBEGIN(PostJoinResponse)
X(joined, Option<bool>)
XEND

// TODO: PostReport
// TODO: PostReportResponse
// TODO: PostReportView


XBEGIN(PostResponse)
X(post_view, Lemmy::PostView)
XEND

// TODO: PrivateMessage
// TODO: PrivateMessageReport
// TODO: PrivateMessageReportResponse
// TODO: PrivateMessageReportView
// TODO: PrivateMessageResponse
// TODO: PrivateMessageView
// TODO: PrivateMessagesResponse
// TODO: PurgeComment
// TODO: PurgeCommunity
// TODO: PurgeItemResponse
// TODO: PurgePerson
// TODO: PurgePost

XBEGIN(Register)
X(username, String)
X(password, SecretString)
X(password_verify, SecretString)
X(email, String)
X(answer, Option<String>)
X(captcha_answer, Option<String>)
X(captcha_uuid, Option<String>)
X(honeypot, Option<String>)
X(show_nsfw, bool)
XEND

// TODO: RegistrationApplication
// TODO: RegistrationApplicationResponse
// TODO: RegistrationApplicationView
// TODO: RemoveComment
// TODO: RemoveCommunity
// TODO: RemovePost
// TODO: ResolveCommentReport
// TODO: ResolveObject
// TODO: ResolveObjectResponse
// TODO: ResolvePostReport
// TODO: ResolvePrivateMessageReport

XBEGIN(SaveComment)
X(auth, Option<SecretString>)
X(comment_id, uint64_t)
X(save, Option<bool>)
XEND

XBEGIN(SavePost)
X(auth, Option<SecretString>)
X(post_id, uint64_t)
X(save, Option<bool>)
XEND

XBEGIN(SaveUserSettings)
X(auth, Option<SecretString>)
X(show_nsfw, Option<bool>)
X(blur_nsfw, Option<bool>)
X(auto_expand, Option<bool>)
X(show_scores, Option<bool>)
X(theme, Option<String>)
X(default_sort_type, Option<StringView>) // enum SortType
X(default_listing_type, Option<StringView>) // enum ListingType
X(interface_language, Option<String>)
X(avatar, Option<String>)
X(banner, Option<String>)
X(display_name, Option<String>)
X(email, Option<String>)
X(bio, Option<String>)
X(matrix_user_id, Option<String>)
X(show_avatars, Option<bool>)
X(send_notifications_to_email, Option<bool>)
X(bot_account, Option<bool>)
X(show_bot_accounts, Option<bool>)
X(show_read_posts, Option<bool>)
X(discussion_languages, Vector<uint64_t>)
X(open_links_in_new_tab, Option<bool>)
X(infinite_scroll_enabled, Option<bool>)
X(post_listing_mode, Option<String>) // List, Lemmy::Card, Lemmy::SmallCard
X(enable_keyboard_navigation, Option<bool>)
X(enable_animated_images, Option<bool>)
X(collapse_bot_comments, Option<bool>)
XEND

XBEGIN(Search)
X(q, String)
X(auth, Option<SecretString>)
X(community_name, Option<String>)
X(community_id, Option<uint64_t>)
X(creator_id, Option<uint64_t>)
X(limit, Option<uint16_t>)
X(page, Option<uint16_t>)
X(listing_type, Option<StringView>) // enum ListingType
X(sort, Option<StringView>) // enum SortType
X(type_, Option<StringView>) // enum SearchType
XEND

XBEGIN(SearchResponse)
X(comments, Vector<Lemmy::CommentView>)
X(communities, Vector<Lemmy::CommunityView>)
X(posts, Vector<Lemmy::PostView>)
X(users, Vector<Lemmy::PersonView>)
X(type_, StringView) // enum SearchType
XEND

// TODO: SiteMetadata

XBEGIN(SiteResponse)
X(site_view, Lemmy::SiteView)
X(taglines, Vector<Lemmy::Tagline>)
XEND

// TODO: TransferCommunity

XBEGIN(UploadImageResponse)
X(msg, String)
X(url, Option<String>)
X(delete_url, Option<String>)
X(files, Vector<Lemmy::ImageFile>)
XEND

XBEGIN(UserJoin)
X(auth, Option<SecretString>)
XEND

XBEGIN(UserJoinResponse)
X(joined, Option<bool>)
XEND

XBEGIN(VerifyEmail)
X(token, String)
XEND

XBEGIN(Error)
X(error, String)
X(status, uint16_t)
XEND

#undef String
#undef StringView
#undef Timestamp
#undef Option
#undef Vector

#undef XBEGIN
#undef X
#undef XEND
