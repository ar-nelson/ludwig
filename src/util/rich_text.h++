#pragma once
#include "fbs/records.h++"
#include "libxml/HTMLparser.h"
#include "libxml/xpath.h"
#include "views/webapp/html/html_common.h++"

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

using RichTextVectors = std::pair<
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


auto rich_text_to_plain_text(
  const flatbuffers::Vector<RichText>* types,
  const flatbuffers::Vector<flatbuffers::Offset<void>>* values
) noexcept -> std::string;

static inline auto update_rich_text_emojis_only(
  flatbuffers::FlatBufferBuilder& fbb,
  std::optional<std::optional<std::string_view>> updated,
  const flatbuffers::Vector<RichText>* types,
  const flatbuffers::Vector<flatbuffers::Offset<void>>* values
) {
  return updated
    .transform([](std::optional<std::string_view> sv) { return sv.transform(λx(std::string(x))); })
    .value_or(types && types->size()
      ? std::optional(rich_text_to_plain_text(types, values))
      : std::nullopt)
    .transform([&](std::string s) { return plain_text_with_emojis_to_rich_text(fbb, s); })
    .value_or(std::pair(0, 0));
}

static inline auto update_rich_text(
  flatbuffers::FlatBufferBuilder& fbb,
  std::optional<std::optional<std::string_view>> updated,
  const flatbuffers::String* existing_raw
) {
  return updated
    .value_or(existing_raw ? std::optional(existing_raw->string_view()) : std::nullopt)
    .transform([&](std::string_view s) {
      const auto [types, values] = markdown_to_rich_text(fbb, s);
      return std::tuple(fbb.CreateString(s), types, values);
    })
    .value_or(std::tuple(0, 0, 0));
}

static inline auto display_name_as_text(const User& user) -> std::string {
  if (user.display_name_type() && user.display_name_type()->size()) {
    return rich_text_to_plain_text(user.display_name_type(), user.display_name());
  }
  const auto name = user.name()->string_view();
  return std::string(name.substr(0, name.find('@')));
}

static inline auto display_name_as_text(const Board& board) -> std::string {
  if (board.display_name_type() && board.display_name_type()->size()) {
    return rich_text_to_plain_text(board.display_name_type(), board.display_name());
  }
  const auto name = board.name()->string_view();
  return std::string(name.substr(0, name.find('@')));
}

static inline auto display_name_as_text(const Thread& thread) -> std::string {
  return rich_text_to_plain_text(thread.title_type(), thread.title());
}

}