#pragma once
#include "fbs/records.h++"

namespace Ludwig {

struct ToHtmlOptions {
  std::function<std::optional<std::string> (std::string_view)> lookup_emoji = [](auto){return std::nullopt;};
  bool show_images = true;
  bool open_links_in_new_tab = false;
  bool links_nofollow = true;
};

auto rich_text_to_html(
  const flatbuffers::Vector<RichText>* types,
  const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
  const ToHtmlOptions& opts = {}
) noexcept -> std::string;

auto rich_text_to_html_emojis_only(
  const flatbuffers::Vector<RichText>* types,
  const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
  const ToHtmlOptions& opts = {}
) noexcept -> std::string;

auto display_name_as_html(const User& user) -> std::string;

auto display_name_as_html(const Board& board) -> std::string;

}