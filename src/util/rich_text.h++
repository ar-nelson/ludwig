#pragma once
#include "util/common.h++"
#include "models/db.h++"
#include "libxml/HTMLparser.h"
#include <map>

namespace Ludwig {
  struct LibXmlContext {
    LibXmlContext() { xmlInitParser(); }
    ~LibXmlContext() { xmlCleanupParser(); }
    LibXmlContext(const LibXmlContext&) = delete;
    auto operator=(const LibXmlContext&) = delete;
  };

  class HtmlDoc {
  private:
    std::shared_ptr<LibXmlContext> xml_ctx;
    htmlDocPtr doc;
  public:
    HtmlDoc(std::shared_ptr<LibXmlContext> xml_ctx, const char* data, size_t size, const char* url = "<no URL>");
    ~HtmlDoc();
    auto root() const -> xmlNodePtr;
    auto text_content(const xmlNode* node) const -> std::string;
    auto attr(const xmlNode* node, const char* name) const -> std::string;
  };

  struct ToHtmlOptions {
    std::function<std::optional<std::string> (std::string_view)> lookup_emoji = [](auto){return std::nullopt;};
    bool show_images = true;
    bool open_links_in_new_tab = false;
    bool links_nofollow = true;
  };

  class RichTextParser {
  private:
    std::unordered_map<std::string_view, std::string_view> shortcode_to_emoji;
    std::shared_ptr<LibXmlContext> xml_ctx;
  public:
    RichTextParser(std::shared_ptr<LibXmlContext> xml_ctx);

    auto parse_html(
      flatbuffers::FlatBufferBuilder& fbb,
      std::string_view html
    ) const -> std::pair<
      flatbuffers::Offset<flatbuffers::Vector<TextBlock>>,
      flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
    >;

    auto parse_markdown(
      flatbuffers::FlatBufferBuilder& fbb,
      std::string_view markdown
    ) const -> std::pair<
      flatbuffers::Offset<flatbuffers::Vector<TextBlock>>,
      flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
    >;

    static auto plain_text_to_blocks(
      flatbuffers::FlatBufferBuilder& fbb,
      std::string_view text
    ) -> std::pair<
      flatbuffers::Offset<flatbuffers::Vector<TextBlock>>,
      flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
    >;

    auto parse_plain_text_with_emojis(
      flatbuffers::FlatBufferBuilder& fbb,
      std::string_view text
    ) const -> std::pair<
      flatbuffers::Offset<flatbuffers::Vector<PlainTextWithEmojis>>,
      flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
    >;

    static auto plain_text_to_plain_text_with_emojis(
      flatbuffers::FlatBufferBuilder& fbb,
      std::string_view text
    ) -> std::pair<
      flatbuffers::Offset<flatbuffers::Vector<PlainTextWithEmojis>>,
      flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
    >;

    static auto blocks_to_html(
      const flatbuffers::Vector<TextBlock>* types,
      const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
      const ToHtmlOptions& opts
    )-> std::string;

    static auto blocks_to_text_content(
      const flatbuffers::Vector<TextBlock>* types,
      const flatbuffers::Vector<flatbuffers::Offset<void>>* values
    ) -> std::string;

    static auto plain_text_with_emojis_to_html(
      const flatbuffers::Vector<PlainTextWithEmojis>* types,
      const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
      const ToHtmlOptions& opts
    ) -> std::string;

    static auto plain_text_with_emojis_to_text_content(
      const flatbuffers::Vector<PlainTextWithEmojis>* types,
      const flatbuffers::Vector<flatbuffers::Offset<void>>* values
    ) -> std::string;
  };
}
