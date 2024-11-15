#pragma once
#include "util/common.h++"

namespace Ludwig {

class ResponseWriter {
protected:
  std::string buf;
private:
  std::back_insert_iterator<std::string> inserter = std::back_inserter(buf);
public:
  auto write(std::string_view s) noexcept -> void { buf.append(s); }
  template <typename T, typename... Args>
  auto write_fmt(T fmt, Args&&... args) noexcept -> void {
    fmt::format_to(inserter, fmt, std::forward<Args>(args)...);
  }
  virtual auto finish_write() -> void = 0;
};

static constexpr std::string_view ESCAPED = "<>'\"&";

struct Escape {
  std::string_view str;
  Escape(std::string_view str) : str(str) {}
  Escape(const flatbuffers::String* fbs) : str(fbs ? fbs->string_view() : "") {}
};
struct Suffixed {
  int64_t n;
};
struct RelativeTime {
  Timestamp t;
};

}

namespace fmt {

template <> struct formatter<Ludwig::Escape> : public Ludwig::CustomFormatter {
  template <typename FormatContext>
  auto format(Ludwig::Escape e, FormatContext& ctx) const {
    size_t start = 0;
    for (
      size_t i = e.str.find_first_of(Ludwig::ESCAPED);
      i != std::string_view::npos;
      start = i + 1, i = e.str.find_first_of(Ludwig::ESCAPED, start)
    ) {
      if (i > start) std::copy(e.str.begin() + start, e.str.begin() + i, ctx.out());
      switch (e.str[i]) {
        case '<': format_to(ctx.out(), "&lt;"_cf); break;
        case '>': format_to(ctx.out(), "&gt;"_cf); break;
        case '\'': format_to(ctx.out(), "&apos;"_cf); break;
        case '"': format_to(ctx.out(), "&quot;"_cf); break;
        case '&': format_to(ctx.out(), "&amp;"_cf); break;
      }
    }
    return std::copy(e.str.begin() + start, e.str.end(), ctx.out());
  }
};

template <> struct formatter<Ludwig::Suffixed> : public Ludwig::CustomFormatter {
  // Adapted from https://programming.guide/java/formatting-byte-size-to-human-readable-format.html
  template <typename FormatContext>
  auto format(Ludwig::Suffixed x, FormatContext &ctx) const {
    static constexpr auto SUFFIXES = "KMBTqQ";
    auto n = x.n;
    if (-1000 < n && n < 1000)
      return format_to(ctx.out(), "{:d}"_cf, n);
    uint8_t i = 0;
    while (n <= -999'950 || n >= 999'950) {
      n /= 1000;
      i++;
    }
    return format_to(ctx.out(), "{:.3g}{:c}"_cf, (double)n / 1000.0, SUFFIXES[i]);
    // SUFFIXES[i] can never overflow, max 64-bit int is ~18 quintillion (Q)
  }
};

template <> struct formatter<Ludwig::RelativeTime> : public Ludwig::CustomFormatter {
  template <typename FormatContext>
  static auto write(FormatContext &ctx, const char s[]) {
    return std::copy(s, s + std::char_traits<char>::length(s), ctx.out());
  }

  template <typename FormatContext>
  auto format(Ludwig::RelativeTime x, FormatContext &ctx) const {
    using namespace std::chrono;
    const auto now = Ludwig::now_t();
    if (x.t > now)
      return write(ctx, "in the future");
    const auto diff = now - x.t;
    if (diff < 1min)
      return write(ctx, "just now");
    if (diff < 2min)
      return write(ctx, "1 minute ago");
    if (diff < 1h)
      return format_to(ctx.out(), "{:d} minutes ago"_cf, duration_cast<minutes>(diff).count());
    if (diff < 2h)
      return write(ctx, "1 hour ago");
    if (diff < days{1})
      return format_to(ctx.out(), "{:d} hours ago"_cf, duration_cast<hours>(diff).count());
    if (diff < days{2})
      return write(ctx, "1 day ago");
    if (diff < weeks{1})
      return format_to(ctx.out(), "{:d} days ago"_cf, duration_cast<days>(diff).count());
    if (diff < weeks{2})
      return write(ctx, "1 week ago");
    if (diff < months{1})
      return format_to(ctx.out(), "{:d} weeks ago"_cf, duration_cast<weeks>(diff).count());
    if (diff < months{2})
      return write(ctx, "1 month ago");
    if (diff < years{1})
      return format_to(ctx.out(), "{:d} months ago"_cf, duration_cast<months>(diff).count());
    if (diff < years{2})
      return write(ctx, "1 year ago");
    return format_to(ctx.out(), "{:d} years ago"_cf, duration_cast<years>(diff).count());
  }
};

}

namespace Ludwig {

static inline auto escape_html(std::string_view str) -> std::string {
  using fmt::operator""_cf;
  return fmt::format("{}"_cf, Escape(str));
}

#define ICON(NAME) \
  R"(<svg aria-hidden="true" class="icon"><use href="/static/feather-sprite.svg#)" NAME R"("></svg>)"

}