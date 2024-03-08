#pragma once
#include "util/common.h++"
#include "util/web.h++"
#include "models/db.h++"
#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include <map>

namespace Ludwig {
  struct LibXmlContext;

  class XPath {
  private:
    xmlXPathCompExprPtr ptr;
    XPath(xmlXPathCompExprPtr ptr) : ptr(ptr) {}
    XPath(const char* src) : ptr(xmlXPathCompile((const xmlChar*)src)) {
      if (!ptr) throw std::runtime_error(fmt::format("XPath compilation failed: {}", src));
    }
  public:
    XPath(const XPath&) = delete;
    auto operator=(const XPath&) = delete;
    XPath(XPath&& from) noexcept : ptr(from) { from.ptr = nullptr; }
    auto operator=(XPath&& from) noexcept {
      if (ptr) xmlXPathFreeCompExpr(ptr);
      ptr = from.ptr;
      from.ptr = nullptr;
    }
    ~XPath() { if (ptr) xmlXPathFreeCompExpr(ptr); }
    operator xmlXPathCompExprPtr() const noexcept { return ptr; }
    friend struct LibXmlContext;
  };

  struct XPathResults {
    xmlXPathObjectPtr ptr;
    XPathResults(xmlXPathObjectPtr ptr) : ptr(ptr) {}
    XPathResults(const XPathResults&) = delete;
    auto operator=(const XPathResults&) = delete;
    XPathResults(XPathResults&& from) noexcept : ptr(from) { from.ptr = nullptr; }
    auto operator=(XPathResults&& from) noexcept {
      if (ptr) xmlXPathFreeObject(ptr);
      ptr = from.ptr;
      from.ptr = nullptr;
    }
    ~XPathResults() { if (ptr) xmlXPathFreeObject(ptr); }
    operator xmlXPathObjectPtr() const noexcept { return ptr; }
    auto empty() const noexcept { return xmlXPathNodeSetIsEmpty(ptr->nodesetval); }
    auto size() const noexcept { return xmlXPathNodeSetGetLength(ptr->nodesetval); }
    auto operator[](int i) const noexcept { return xmlXPathNodeSetItem(ptr->nodesetval, i); }
  };

  struct LibXmlContext {
    LibXmlContext() { xmlInitParser(); }
    ~LibXmlContext() { xmlCleanupParser(); }
    LibXmlContext(const LibXmlContext&) = delete;
    auto operator=(const LibXmlContext&) = delete;

    auto compile_xpath(const char* src) const { return XPath(src); }
  };

  class HtmlDoc {
  private:
    std::shared_ptr<LibXmlContext> xml_ctx;
    xmlXPathContextPtr xpath_ctx;
    htmlDocPtr doc;
  public:
    HtmlDoc(std::shared_ptr<LibXmlContext> xml_ctx, std::string_view src, const char* url = "<no URL>");
    ~HtmlDoc();
    auto root() const -> xmlNodePtr;
    auto text_content(const xmlNode* node) const -> std::string;
    auto attr(const xmlNode* node, const char* name) const -> std::string;
    auto xpath(const XPath& expr) const noexcept -> XPathResults;
    auto xpath(const char* expr) const noexcept -> XPathResults;
    auto xpath_exists(const XPath& expr) const noexcept -> bool;
    auto xpath_exists(const char* expr) const noexcept -> bool;
  };

  struct ToHtmlOptions {
    std::function<std::optional<std::string> (std::string_view)> lookup_emoji = [](auto){return std::nullopt;};
    bool show_images = true;
    bool open_links_in_new_tab = false;
    bool links_nofollow = true;
  };

  using RichTextVectors =  std::pair<
    flatbuffers::Offset<flatbuffers::Vector<RichText>>,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
  >;

  auto html_to_rich_text(
    flatbuffers::FlatBufferBuilder& fbb,
    std::string_view html,
    LibXmlContext& xml_ctx
  ) -> RichTextVectors;

  auto markdown_to_rich_text(
    flatbuffers::FlatBufferBuilder& fbb,
    std::string_view markdown
  ) noexcept -> RichTextVectors;

  static inline auto plain_text_to_rich_text(
    flatbuffers::FlatBufferBuilder& fbb,
    std::string_view text
  ) noexcept -> RichTextVectors {
    return {
      fbb.CreateVector(std::vector{RichText::Text}),
      fbb.CreateVector(std::vector{fbb.CreateString(fmt::format("{}", Escape{text})).Union()})
    };
  }

  auto plain_text_with_emojis_to_rich_text(
    flatbuffers::FlatBufferBuilder& fbb,
    std::string_view text
  ) noexcept -> RichTextVectors;

  auto rich_text_to_html(
    const flatbuffers::Vector<RichText>* types,
    const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
    const ToHtmlOptions& opts = {}
  ) noexcept -> std::string;

  static inline auto rich_text_to_html_emojis_only(
    const flatbuffers::Vector<RichText>* types,
    const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
    const ToHtmlOptions& opts = {}
  ) noexcept -> std::string {
    if (!types || !values) return "";
    std::string out;
    for (unsigned i = 0; i < std::min(types->size(), values->size()); i++) {
      switch (types->Get(i)) {
        case RichText::Text:
          out += values->GetAsString(i)->string_view();
          break;
        case RichText::Emoji:
          if (auto emoji = opts.lookup_emoji(values->GetAsString(i)->string_view())) {
            out += *emoji;
          } else {
            fmt::format_to(back_inserter(out), ":{}:", Escape{values->GetAsString(i)});
          }
          break;
        default:
          // Do nothing
      }
    }
    return out;
  }

  auto rich_text_to_plain_text(
    const flatbuffers::Vector<RichText>* types,
    const flatbuffers::Vector<flatbuffers::Offset<void>>* values
  ) noexcept -> std::string;
}
