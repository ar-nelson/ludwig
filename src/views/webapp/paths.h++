#pragma once

#define PATH_login         "/login"
#define PATH_logout        "/logout"
#define PATH_register      "/register"
#define PATH_search        "/search"
#define PATH_notifications "/notifications"
#define PATH_notifications_read(HEX_ID) PATH_notifications "/" HEX_ID "/read"
#define PATH_notifications_all_read PATH_notifications "/all_read"

#define PATH_FEED_all   "/all"
#define PATH_FEED_local "/local"

#define PATH_USER_user(NAME)      "/u/" NAME
#define PATH_USER_list            "/users"
#define PATH_USER_settings        "/settings"
#define PATH_USER_profile         PATH_USER_settings "/profile"
#define PATH_USER_account         PATH_USER_settings "/account"
#define PATH_USER_change_password PATH_USER_account  "/change_password"
#define PATH_USER_delete_account  PATH_USER_account  "/delete"
#define PATH_USER_invites         PATH_USER_settings "/invites"
#define PATH_USER_new_invite      PATH_USER_invites  "/new"

#define PATH_BOARD_board(NAME)     "/b/" NAME
#define PATH_BOARD_list            "/boards"
#define PATH_BOARD_create          "/create_board"
#define PATH_BOARD_subscribe(NAME) PATH_BOARD_board(NAME)    "/subscribe"
#define PATH_BOARD_settings(NAME)  PATH_BOARD_board(NAME)    "/settings"
#define PATH_BOARD_delete(NAME)    PATH_BOARD_settings(NAME) "/delete"

#define PATH_POST_post(NOUN, HEX_ID)   "/" NOUN "/" HEX_ID
#define PATH_POST_action(NOUN, HEX_ID) PATH_POST_post(NOUN, HEX_ID) "/action"
#define PATH_POST_vote(NOUN, HEX_ID)   PATH_POST_post(NOUN, HEX_ID) "/vote"
#define PATH_POST_edit(NOUN, HEX_ID)   PATH_POST_post(NOUN, HEX_ID) "/edit"
#define PATH_POST_reply(NOUN, HEX_ID)  PATH_POST_post(NOUN, HEX_ID) "/reply"
#define PATH_POST_delete(NOUN, HEX_ID) PATH_POST_post(NOUN, HEX_ID) "/delete"

#define PATH_THREAD_NOUN "thread"
#define PATH_THREAD_thread(HEX_ID)     PATH_POST_post(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_action(HEX_ID)     PATH_POST_action(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_vote(HEX_ID)       PATH_POST_vote(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_edit(HEX_ID)       PATH_POST_edit(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_reply(HEX_ID)      PATH_POST_reply(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_delete(HEX_ID)     PATH_POST_delete(PATH_THREAD_NOUN, HEX_ID)
#define PATH_THREAD_create(BOARD_NAME) PATH_BOARD_board(BOARD_NAME) "/create_thread"

#define PATH_COMMENT_NOUN "comment"
#define PATH_COMMENT_comment(HEX_ID) PATH_POST_post(PATH_COMMENT_NOUN, HEX_ID)
#define PATH_COMMENT_action(HEX_ID)  PATH_POST_action(PATH_COMMENT_NOUN, HEX_ID)
#define PATH_COMMENT_vote(HEX_ID)    PATH_POST_vote(PATH_COMMENT_NOUN, HEX_ID)
#define PATH_COMMENT_edit(HEX_ID)    PATH_POST_edit(PATH_COMMENT_NOUN, HEX_ID)
#define PATH_COMMENT_reply(HEX_ID)   PATH_POST_reply(PATH_COMMENT_NOUN, HEX_ID)
#define PATH_COMMENT_delete(HEX_ID)  PATH_POST_delete(PATH_COMMENT_NOUN, HEX_ID)

#define PATH_ADMIN_admin           "/site_admin"
#define PATH_ADMIN_import_export   PATH_ADMIN_admin "/import_export"
#define PATH_ADMIN_export          PATH_ADMIN_admin "/export"
#define PATH_ADMIN_applications    PATH_ADMIN_admin "/applications"
#define PATH_ADMIN_applications_action(HEX_ID, ACTION) PATH_ADMIN_applications "/" HEX_ID "/" ACTION
#define PATH_ADMIN_invites         PATH_ADMIN_admin "/invites"
#define PATH_ADMIN_new_invite      PATH_ADMIN_invites "/new"
#define PATH_ADMIN_first_run_setup PATH_ADMIN_admin "/first_run_setup"

#define PATH_STATIC_css         "/static/default-theme.css"
#define PATH_STATIC_js          "/static/ludwig.js"
#define PATH_STATIC_js_htmx     "/static/htmx.min.js"
#define PATH_STATIC_icon_sprite "/static/feather-sprite.svg"

#define PATH_MEDIA_user_avatar(USER_NAME)   "/media/user/" USER_NAME "/avatar.webp"
#define PATH_MEDIA_user_banner(USER_NAME)   "/media/user/" USER_NAME "/banner.webp"
#define PATH_MEDIA_board_icon(BOARD_NAME)   "/media/board/" BOARD_NAME "/icon.webp"
#define PATH_MEDIA_board_banner(BOARD_NAME) "/media/board/" BOARD_NAME "/banner.webp"
#define PATH_MEDIA_thread_thumbnail(HEX_ID) "/media/thread/" HEX_ID "/thumbnail.webp"