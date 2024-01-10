#pragma once
#include "models/detail.h++"
#include "util/web.h++"

namespace Ludwig {
  constexpr auto to_string(SortType sort) -> std::string_view {
    switch (sort) {
      case SortType::Hot: return "Hot";
      case SortType::Active: return "Active";
      case SortType::New: return "New";
      case SortType::Old: return "Old";
      case SortType::MostComments: return "MostComments";
      case SortType::NewComments: return "NewComments";
      case SortType::TopAll: return "TopAll";
      case SortType::TopYear: return "TopYear";
      case SortType::TopSixMonths: return "TopSixMonths";
      case SortType::TopThreeMonths: return "TopThreeMonths";
      case SortType::TopMonth: return "TopMonth";
      case SortType::TopWeek: return "TopWeek";
      case SortType::TopDay: return "TopDay";
      case SortType::TopTwelveHour: return "TopTwelveHour";
      case SortType::TopSixHour: return "TopSixHour";
      case SortType::TopHour: return "TopHour";
    }
  }

  static inline auto parse_sort_type(std::string_view str, Login login = {}) -> SortType {
    if (str.empty()) return login ? login->local_user().default_sort_type() : SortType::Active;
    if (str == "Hot") return SortType::Hot;
    if (str == "Active") return SortType::Active;
    if (str == "New") return SortType::New;
    if (str == "Old") return SortType::Old;
    if (str == "MostComments") return SortType::MostComments;
    if (str == "NewComments") return SortType::NewComments;
    if (str == "Top" || str == "TopAll") return SortType::TopAll;
    if (str == "TopYear") return SortType::TopYear;
    if (str == "TopSixMonths") return SortType::TopSixMonths;
    if (str == "TopThreeMonths") return SortType::TopThreeMonths;
    if (str == "TopMonth") return SortType::TopMonth;
    if (str == "TopWeek") return SortType::TopWeek;
    if (str == "TopDay") return SortType::TopDay;
    if (str == "TopTwelveHour") return SortType::TopTwelveHour;
    if (str == "TopSixHour") return SortType::TopSixHour;
    if (str == "TopHour") return SortType::TopHour;
    throw ApiError("Bad sort type", 400);
  }

  constexpr auto to_string(CommentSortType sort) -> std::string_view {
    switch (sort) {
      case CommentSortType::Hot: return "Hot";
      case CommentSortType::New: return "New";
      case CommentSortType::Old: return "Old";
      case CommentSortType::Top: return "Top";
    }
  }

  static inline auto parse_comment_sort_type(std::string_view str, Login login = {}) -> CommentSortType {
    if (str.empty()) return login ? login->local_user().default_comment_sort_type() : CommentSortType::Hot;
    if (str == "Hot") return CommentSortType::Hot;
    if (str == "New") return CommentSortType::New;
    if (str == "Old") return CommentSortType::Old;
    if (str == "Top") return CommentSortType::Top;
    throw ApiError("Bad comment sort type", 400);
  }

  constexpr auto to_string(UserPostSortType sort) -> std::string_view {
    switch (sort) {
      case UserPostSortType::New: return "New";
      case UserPostSortType::Old: return "Old";
      case UserPostSortType::Top: return "Top";
    }
  }

  static inline auto parse_user_post_sort_type(std::string_view str) -> UserPostSortType {
    if (str.empty() || str == "New") return UserPostSortType::New;
    if (str == "Old") return UserPostSortType::Old;
    if (str == "Top") return UserPostSortType::Top;
    throw ApiError("Bad post sort type", 400);
  }

  constexpr auto to_string(UserSortType sort) -> std::string_view {
    switch (sort) {
      case UserSortType::NewPosts: return "NewPosts";
      case UserSortType::MostPosts: return "MostPosts";
      case UserSortType::New: return "New";
      case UserSortType::Old: return "Old";
    }
  }

  static inline auto parse_user_sort_type(std::string_view str) -> UserSortType {
    if (str.empty() || str == "NewPosts") return UserSortType::NewPosts;
    if (str == "MostPosts") return UserSortType::MostPosts;
    if (str == "New") return UserSortType::New;
    if (str == "Old") return UserSortType::Old;
    throw ApiError("Bad user sort type", 400);
  }

  constexpr auto to_string(BoardSortType sort) -> std::string_view {
    switch (sort) {
      case BoardSortType::NewPosts: return "NewPosts";
      case BoardSortType::MostPosts: return "MostPosts";
      case BoardSortType::MostSubscribers: return "MostSubscribers";
      case BoardSortType::New: return "New";
      case BoardSortType::Old: return "Old";
    }
  }

  static inline auto parse_board_sort_type(std::string_view str) -> BoardSortType {
    if (str.empty() || str == "MostSubscribers") return BoardSortType::MostSubscribers;
    if (str == "NewPosts") return BoardSortType::NewPosts;
    if (str == "MostPosts") return BoardSortType::MostPosts;
    if (str == "MostSubscribers") return BoardSortType::MostSubscribers;
    if (str == "New") return BoardSortType::New;
    if (str == "Old") return BoardSortType::Old;
    throw ApiError("Bad board sort type", 400);
  }

  constexpr auto to_string(HomePageType type) -> std::string_view {
    switch (type) {
      case HomePageType::All: return "All";
      case HomePageType::Local: return "Local";
      case HomePageType::Subscribed: return "Subscribed";
      case HomePageType::BoardList: return "BoardList";
      case HomePageType::SingleBoard: return "SingleBoard";
    }
  }

  static inline auto parse_home_page_type(std::string_view str) -> HomePageType {
    if (str == "All") return HomePageType::All;
    if (str == "Local") return HomePageType::Local;
    if (str == "Subscribed") return HomePageType::Subscribed;
    if (str == "BoardList") return HomePageType::BoardList;
    if (str == "SingleBoard") return HomePageType::SingleBoard;
    throw ApiError("Bad home page type", 400);
  }

  enum class SubscribedType : uint8_t {
    NotSubscribed,
    Subscribed,
    Pending
  };

  constexpr auto to_string(SubscribedType type) -> std::string_view {
    switch (type) {
      case SubscribedType::NotSubscribed: return "NotSubscribed";
      case SubscribedType::Subscribed: return "Subscribed";
      case SubscribedType::Pending: return "Pending";
    }
  }

  static inline auto parse_subscribed_type(std::string_view str) -> SubscribedType {
    if (str == "NotSubscribed") return SubscribedType::NotSubscribed;
    if (str == "Subscribed") return SubscribedType::Subscribed;
    if (str == "Pending") return SubscribedType::Pending;
    throw ApiError("Bad SubscribedType", 400);
  }

  namespace Lemmy {
    enum class ListingType : uint8_t {
      All,
      Local,
      Subscribed,
      ModeratorView
    };

    constexpr auto to_string(ListingType type) -> std::string_view {
      switch (type) {
        case ListingType::All: return "All";
        case ListingType::Local: return "Local";
        case ListingType::Subscribed: return "Subscribed";
        case ListingType::ModeratorView: return "ModeratorView";
      }
    }

    static inline auto parse_listing_type(std::string_view str) -> ListingType {
      if (str == "All") return ListingType::All;
      if (str == "Local") return ListingType::Local;
      if (str == "Subscribed") return ListingType::Subscribed;
      if (str == "ModeratorView") return ListingType::ModeratorView;
      throw ApiError("Bad ListingType", 400);
    }

    enum class RegistrationMode : uint8_t {
      Closed,
      RequireApplication,
      Open
    };

    constexpr auto to_string(RegistrationMode type) -> std::string_view {
      switch (type) {
        case RegistrationMode::Closed: return "Closed";
        case RegistrationMode::RequireApplication: return "RequireApplication";
        case RegistrationMode::Open: return "Open";
      }
    }

    static inline auto parse_registration_mode(std::string_view str) -> RegistrationMode {
      if (str == "Closed") return RegistrationMode::Closed;
      if (str == "RequireApplication") return RegistrationMode::RequireApplication;
      if (str == "Open") return RegistrationMode::Open;
      throw ApiError("Bad RegistrationMode", 400);
    }

    enum class SearchType : uint8_t {
      All,
      Comments,
      Posts,
      Communities,
      Users,
      Url
    };

    constexpr auto to_string(SearchType type) -> std::string_view {
      switch (type) {
        case SearchType::All: return "All";
        case SearchType::Comments: return "Comments";
        case SearchType::Posts: return "Posts";
        case SearchType::Communities: return "Communities";
        case SearchType::Users: return "Users";
        case SearchType::Url: return "Url";
      }
    }

    static inline auto parse_search_type(std::string_view str) -> SearchType {
      if (str == "All") return SearchType::All;
      if (str == "Comments") return SearchType::Comments;
      if (str == "Posts") return SearchType::Posts;
      if (str == "Communities") return SearchType::Communities;
      if (str == "Users") return SearchType::Users;
      if (str == "Url") return SearchType::Url;
      throw ApiError("Bad SearchType", 400);
    }
  }
}
