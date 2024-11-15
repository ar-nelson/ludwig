#include "html_rich_text.h++"
#include "util/rich_text.h++"

using std::back_inserter, std::min, std::string, flatbuffers::Offset, flatbuffers::Vector, fmt::format_to,
  fmt::operator""_cf; // NOLINT

namespace Ludwig {

auto rich_text_to_html(
  const Vector<RichText>* types,
  const Vector<Offset<void>>* values,
  const ToHtmlOptions& opts
) noexcept -> string {
  if (!types || !values) return "";
  string out;
  for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
    switch (types->Get(i)) {
      case RichText::Text:
        out += values->GetAsString(i)->string_view();
        break;
      case RichText::Emoji:
        if (auto emoji = opts.lookup_emoji(values->GetAsString(i)->string_view())) {
          out += *emoji;
        } else {
          format_to(back_inserter(out), ":{}:"_cf, Escape{values->GetAsString(i)});
        }
        break;
      case RichText::Link:
        format_to(back_inserter(out), R"(<a href="{}" rel="noopener noreferrer{}"{}>)"_cf,
          Escape{values->GetAsString(i)},
          opts.links_nofollow ? " nofollow" : "",
          opts.open_links_in_new_tab ? R"( target="_blank")" : ""
        );
        break;
      case RichText::UserLink:
        format_to(back_inserter(out), R"(<a href="/u/{}"{}>)"_cf,
          Escape{values->GetAsString(i)},
          opts.open_links_in_new_tab ? R"( target="_blank")" : ""
        );
        break;
      case RichText::BoardLink:
        format_to(back_inserter(out), R"(<a href="/b/{}"{}>)"_cf,
          Escape{values->GetAsString(i)},
          opts.open_links_in_new_tab ? R"( target="_blank")" : ""
        );
        break;
      case RichText::Image: {
        const auto* img = values->GetAs<RichTextImage>(i);
        if (!opts.show_images) {
          format_to(back_inserter(out), R"(<details><summary>Image{}{}</summary>)"_cf,
            img->alt() ? ": " : "", Escape{img->alt()}
          );
        }
        format_to(back_inserter(out), R"(<img src="{}" loading="lazy")"_cf, Escape{img->src()});
        if (img->alt()) {
          format_to(back_inserter(out), R"( alt="{0}" title="{0}")"_cf, Escape{img->alt()});
        }
        out += opts.show_images ? ">" : "></details>";
        break;
      }
      case RichText::NONE:
        // Do nothing
    }
  }
  return out;
}

auto rich_text_to_html_emojis_only(
  const Vector<RichText>* types,
  const Vector<Offset<void>>* values,
  const ToHtmlOptions& opts
) noexcept -> string {
  if (!types || !values) return "";
  string out;
  for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
    switch (types->Get(i)) {
      case RichText::Text:
        out += values->GetAsString(i)->string_view();
        break;
      case RichText::Emoji:
        if (auto emoji = opts.lookup_emoji(values->GetAsString(i)->string_view())) {
          out += *emoji;
        } else {
          format_to(back_inserter(out), ":{}:"_cf, Escape{values->GetAsString(i)});
        }
        break;
      default:
        // Do nothing
    }
  }
  return out;
}

auto display_name_as_html(const User& user) -> string {
  using fmt::operator""_cf;
  if (user.display_name_type() && user.display_name_type()->size()) {
    return rich_text_to_html_emojis_only(user.display_name_type(), user.display_name(), {});
  }
  const auto name = user.name()->string_view();
  return format("{}"_cf, Escape(name.substr(0, name.find('@'))));
}

auto display_name_as_html(const Board& board) -> string {
  if (board.display_name_type() && board.display_name_type()->size()) {
    return rich_text_to_html_emojis_only(board.display_name_type(), board.display_name(), {});
  }
  const auto name = board.name()->string_view();
  return format("{}"_cf, Escape(name.substr(0, name.find('@'))));
}

}