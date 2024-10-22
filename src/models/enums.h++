#pragma once
#include "db.h++"
#include "models/detail.h++"
#include "util/web.h++"

namespace Ludwig {

# define X_SORT_TYPE(X) \
    X(Active, "Active") X(Hot, "Hot") X(New, "New") X(Old, "Old") \
    X(MostComments, "Most Comments") X(NewComments, "New Comments") \
    X(TopAll, "Top All") X(TopYear, "Top Year") X(TopSixMonths, "Top Six Months") X(TopThreeMonths, "Top Three Months") \
    X(TopMonth, "Top Month") X(TopWeek, "Top Week") X(TopDay, "Top Day") \
    X(TopTwelveHour, "Top Twelve Hour") X(TopSixHour, "Top Six Hour") X(TopHour, "Top Hour")

# define X_COMMENT_SORT_TYPE(X) X(Hot, "Hot") X(New, "New") X(Old, "Old") X(Top, "Top")
# define X_USER_POST_SORT_TYPE(X) X(New, "New") X(Old, "Old") X(Top, "Top")
# define X_USER_SORT_TYPE(X) X(New, "New") X(Old, "Old") X(MostPosts, "Most Posts") X(NewPosts, "New Posts")
# define X_BOARD_SORT_TYPE(X) X(New, "New") X(Old, "Old") X(MostPosts, "Most Posts") X(NewPosts, "New Posts") X(MostSubscribers, "Most Subscribers")
# define X_HOME_PAGE_TYPE(X) \
    X(Subscribed, "Subscribed - Display the user's subscribed boards, or Local boards if not logged in") \
    X(Local, "Local - Display top content from all boards on this site") \
    X(All, "All - Display top content from all federated sites") \
    X(BoardList, "Board List - Display a curated list of boards, like a classic forum") \
    X(SingleBoard, "Single Board - The site has only one board, which is always the homepage")

enum class SubscribedType : uint8_t {
  NotSubscribed,
  Subscribed,
  Pending
};

# define X_SUBSCRIBED_TYPE(X) X(Subscribed, "Subscribed") X(NotSubscribed, "Not Subscribed") X(Pending, "Pending")

# define DEF_TO_STRING_AND_PARSE_WITH_LOGIN(T, LOWERCASE_NAME, APPLY_TO_ENUM, DEFAULT) \
  constexpr auto to_string(T sort) -> std::string_view { \
    using enum T; \
    switch (sort) { APPLY_TO_ENUM(TO_STRING_CASE) } \
  } \
  static inline auto parse_##LOWERCASE_NAME(std::string_view str, Login login = {}) -> T { \
    using enum T; \
    if (str.empty()) return login ? login->local_user().default_##LOWERCASE_NAME() : DEFAULT; \
    APPLY_TO_ENUM(PARSE_CASE) \
    throw ApiError("Bad " #T, 400); \
  }
# define DEF_TO_STRING_AND_PARSE(T, LOWERCASE_NAME, APPLY_TO_ENUM, ...) \
  constexpr auto to_string(T sort) -> std::string_view { \
    using enum T; \
    switch (sort) { APPLY_TO_ENUM(TO_STRING_CASE) } \
  } \
  static inline auto parse_##LOWERCASE_NAME(std::string_view str) -> T { \
    using enum T; \
    __VA_ARGS__; \
    APPLY_TO_ENUM(PARSE_CASE) \
    throw ApiError("Bad " #T, 400); \
  }
# define TO_STRING_CASE(NAME, UNUSED) case NAME: return #NAME;
# define PARSE_CASE(NAME, UNUSED) if (str == #NAME) return NAME;

  DEF_TO_STRING_AND_PARSE_WITH_LOGIN(SortType, sort_type, X_SORT_TYPE, Active)
  DEF_TO_STRING_AND_PARSE_WITH_LOGIN(CommentSortType, comment_sort_type, X_COMMENT_SORT_TYPE, Hot)
  DEF_TO_STRING_AND_PARSE(UserPostSortType, user_post_sort_type, X_USER_POST_SORT_TYPE, if (str.empty()) return New)
  DEF_TO_STRING_AND_PARSE(UserSortType, user_sort_type, X_USER_SORT_TYPE, if (str.empty()) return NewPosts)
  DEF_TO_STRING_AND_PARSE(BoardSortType, board_sort_type, X_BOARD_SORT_TYPE, if (str.empty()) return MostSubscribers)

  DEF_TO_STRING_AND_PARSE(HomePageType, home_page_type, X_HOME_PAGE_TYPE)
  DEF_TO_STRING_AND_PARSE(SubscribedType, subscribed_type, X_SUBSCRIBED_TYPE)

  namespace Lemmy {
    enum class ListingType : uint8_t {
      All,
      Local,
      Subscribed,
      ModeratorView
    };

#   define X_LEMMY_LISTING_TYPE(X) X(All, "All") X(Local, "Local") X(Subscribed, "Subscribed") X(ModeratorView, "Moderator View")
    DEF_TO_STRING_AND_PARSE(ListingType, listing_type, X_LEMMY_LISTING_TYPE)

    enum class RegistrationMode : uint8_t {
      Closed,
      RequireApplication,
      Open
    };

#   define X_LEMMY_REGISTRATION_MODE(X) X(Closed, "Closed") X(RequireApplication, "Application Required") X(Open, "Open")
    DEF_TO_STRING_AND_PARSE(RegistrationMode, registration_mode, X_LEMMY_REGISTRATION_MODE)

    enum class SearchType : uint8_t {
      All,
      Comments,
      Posts,
      Communities,
      Users,
      Url
    };

#   define X_LEMMY_SEARCH_TYPE(X) X(All, "All") X(Comments, "Comments") X(Posts, "Posts") X(Communities, "Boards") X(Users, "Users") X(Url, "URL")
    DEF_TO_STRING_AND_PARSE(SearchType, search_type, X_LEMMY_SEARCH_TYPE)
  }

# undef PARSE_CASE
# undef TO_STRING_CASE
# undef DEF_TO_STRING_AND_PARSE
# undef DEF_TO_STRING_AND_PARSE_WITH_LOGIN
}
