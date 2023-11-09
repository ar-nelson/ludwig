#include "rich_text.h++"
#include "util/web.h++"
#include "models/emoji_table.h++"
#include "static/emoji_table.fb.h++"
#include <stdio.h>
#include <static_vector.hpp>
#include <mmd.h>

using std::back_inserter, std::get_if, std::min, std::pair, std::prev,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::swap, std::tuple, std::variant, std::vector,
    flatbuffers::FlatBufferBuilder, flatbuffers::Offset, flatbuffers::Vector;

namespace Ludwig {

  HtmlDoc::HtmlDoc(shared_ptr<LibXmlContext> xml_ctx, const char* data, size_t size, const char* url) : xml_ctx(xml_ctx) {
    doc = htmlReadMemory(
      data, (int)size, url, "utf-8",
      HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET
    );
    if (doc == nullptr) {
      throw runtime_error(fmt::format("Failed to parse HTML at {}", url));
    }
  }
  HtmlDoc::~HtmlDoc() {
    xmlFreeDoc(doc);
  }
  auto HtmlDoc::root() const -> xmlNodePtr {
    return xmlDocGetRootElement(doc);
  }
  static inline auto xml_str(const xmlChar* xstr) -> string {
    if (xstr == nullptr) return "";
    string str((const char*)xstr);
    xmlFree((void*)xstr);
    return str;
  }
  auto HtmlDoc::text_content(const xmlNode* node) const -> string {
    return xml_str(xmlNodeGetContent(node));
  }
  auto HtmlDoc::attr(const xmlNode* node, const char* name) const -> string {
    return xml_str(xmlGetProp(node, (const xmlChar*)name));
  }

  RichTextParser::RichTextParser(std::shared_ptr<LibXmlContext> xml_ctx) : xml_ctx(xml_ctx) {
    mmdSetOptions(MMD_OPTION_NONE);
    const auto emoji_table = flatbuffers::GetRoot<EmojiTable>(emoji_table_fb);
    for (const auto entry : *emoji_table->entries()) {
      for (const auto shortcode : *entry->shortcodes()) {
        shortcode_to_emoji.emplace(shortcode->string_view(), entry->emoji()->string_view());
      }
    }
    spdlog::debug("Loaded {} built-in emoji shortcodes", shortcode_to_emoji.size());
  }

  class BlockBuilder {
    FlatBufferBuilder& fbb;
    stlpb::static_vector<
      pair<
        TextBlock,
        variant<
          pair<vector<TextBlock>, vector<Offset<void>>>,
          vector<Offset<TextBlocks>>
        >
      >,
      32
    > parent_blocks;
    stlpb::static_vector<
      tuple<TextSpan, vector<TextSpan>, vector<Offset<void>>, string_view, string_view>,
      16
    > parent_spans;
    vector<TextBlock> block_types;
    vector<Offset<void>> block_values;
    TextBlock block_type = TextBlock::NONE, para_type = TextBlock::P;
    TextSpan span_type = TextSpan::Plain;
    string_view href, title;
    vector<TextSpan> span_types;
    vector<Offset<void>> span_values;
  public:
    BlockBuilder(FlatBufferBuilder& fbb) : fbb(fbb) {}

    auto close_span() -> void {
      if (parent_spans.empty()) return;
      auto last = prev(parent_spans.end());
      auto& [parent_span, parent_types, parent_values, parent_href, parent_title] = *last;
      if (!span_types.empty()) {
        parent_types.emplace_back(span_type);
        if (span_type == TextSpan::Link) {
          parent_values.emplace_back(CreateTextLink(fbb,
            fbb.CreateString(href),
            title.empty() ? 0 : fbb.CreateString(title),
            fbb.CreateVector(span_types),
            fbb.CreateVector(span_values)
          ).Union());
        } else {
          parent_values.emplace_back(CreateTextSpansDirect(fbb, &span_types, &span_values).Union());
        }
      }
      span_type = parent_span;
      swap(span_types, parent_types);
      swap(span_values, parent_values);
      href = parent_href;
      title = parent_title;
      parent_spans.erase(last);
    }

    auto close_paragaph() -> void {
      if (span_types.empty() && parent_spans.empty()) {
        para_type = TextBlock::P;
        return;
      }
      while (!parent_spans.empty()) close_span();
      block_types.emplace_back(para_type);
      block_values.emplace_back(CreateTextSpansDirect(fbb, &span_types, &span_values).Union());
      para_type = TextBlock::P;
      span_types.clear();
      span_values.clear();
    }

    auto close_block() -> void {
      if (parent_blocks.empty()) return;
      close_paragaph();
      auto last = prev(parent_blocks.end());
      auto& [parent_block, parent_variant] = *last;
      if (auto* items = get_if<vector<Offset<TextBlocks>>>(&parent_variant)) {
        items->push_back(CreateTextBlocksDirect(fbb, &block_types, &block_values));
        block_type = TextBlock::NONE;
        block_types.clear();
        block_values.clear();
      } else {
        auto& [parent_types, parent_values] = std::get<0>(parent_variant);
        if (!block_types.empty()) {
          parent_types.emplace_back(block_type);
          parent_values.emplace_back(CreateTextBlocksDirect(fbb, &block_types, &block_values).Union());
        }
        block_type = parent_block;
        swap(block_types, parent_types);
        swap(block_values, parent_values);
        parent_blocks.erase(last);
      }
    }

    auto close_list() -> void {
      if (block_type == TextBlock::NONE) close_paragaph();
      else while (block_type != TextBlock::NONE || (!parent_blocks.empty() && !block_types.empty())) close_block();
      if (parent_blocks.empty()) return;
      if (parent_blocks.size() < 2) throw runtime_error("RichTextParser: list item is missing parent list node");
      auto list_iter = prev(parent_blocks.end()), parent_iter = prev(list_iter);
      if (auto* list_items = get_if<vector<Offset<TextBlocks>>>(&list_iter->second)) {
        if (auto* parent_items = get_if<pair<vector<TextBlock>, vector<Offset<void>>>>(&parent_iter->second)) {
          auto& [parent_types, parent_values] = *parent_items;
          parent_types.emplace_back(list_iter->first);
          parent_values.emplace_back(CreateTextListDirect(fbb, list_items).Union());
          block_type = parent_iter->first;
          swap(block_types, parent_types);
          swap(block_values, parent_values);
          parent_blocks.erase(list_iter);
          parent_blocks.erase(parent_iter);
          return;
        }
      }
      throw runtime_error("RichTextParser: list item is missing parent list node");
    }

    inline auto add_plain_text(string_view text) -> void {
      span_types.emplace_back(TextSpan::Plain);
      span_values.emplace_back(fbb.CreateString(text).Union());
    }

    inline auto add_hard_break() -> void {
      span_types.emplace_back(TextSpan::HardBreak);
      span_values.emplace_back(CreateFbVoid(fbb).Union());
    }

    inline auto add_soft_break() -> void {
      span_types.emplace_back(TextSpan::SoftBreak);
      span_values.emplace_back(CreateFbVoid(fbb).Union());
    }

    inline auto add_hr() -> void {
      close_paragaph();
      block_types.emplace_back(TextBlock::Hr);
      block_values.emplace_back(CreateFbVoid(fbb).Union());
    }

    inline auto add_image(string_view src, string_view alt = "") -> void {
      close_paragaph();
      block_types.emplace_back(TextBlock::Image);
      block_values.emplace_back(CreateTextImage(fbb, fbb.CreateString(src), alt.empty() ? 0 : fbb.CreateString(alt)).Union());
    }

    inline auto add_emoji(string_view shortcode) -> void {
      // TODO: Handle shortcodes for Unicode emoji
      span_types.emplace_back(TextSpan::CustomEmoji);
      span_values.emplace_back(fbb.CreateString(shortcode).Union());
    }

    inline auto add_code_span(string_view text) -> void {
      span_types.emplace_back(TextSpan::Code);
      span_values.emplace_back(fbb.CreateString(text).Union());
    }

    inline auto add_code_block(string_view text, string_view language = "") -> void {
      close_paragaph();
      block_types.emplace_back(TextBlock::Code);
      block_values.emplace_back(CreateTextCodeBlock(fbb, fbb.CreateString(text), language.empty() ? 0 : fbb.CreateString(language)).Union());
    }

    inline auto add_math_span(string_view text) -> void {
      span_types.emplace_back(TextSpan::Math);
      span_values.emplace_back(fbb.CreateString(text).Union());
    }

    inline auto add_math_block(string_view text) -> void {
      close_paragaph();
      block_types.emplace_back(TextBlock::Math);
      block_values.emplace_back(fbb.CreateString(text).Union());
    }

    inline auto open_span(TextSpan new_span, string_view new_href = "", string_view new_title = "") -> void {
      auto& inserted = parent_spans.emplace_back(span_type, vector<TextSpan>{}, vector<Offset<void>>{}, href, title);
      span_type = new_span;
      swap(span_types, std::get<1>(inserted));
      swap(span_values, std::get<2>(inserted));
      href = new_href;
      title = new_title;
    }

    inline auto open_block(TextBlock new_block) -> void {
      close_paragaph();
      switch (new_block) {
        case TextBlock::P:
        case TextBlock::H1:
        case TextBlock::H2:
        case TextBlock::H3:
        case TextBlock::H4:
        case TextBlock::H5:
        case TextBlock::H6:
          para_type = new_block;
          break;
        default: {
          auto& inserted = std::get<0>(parent_blocks.emplace_back(block_type, pair(vector<TextBlock>{}, vector<Offset<void>>{})).second);
          block_type = new_block;
          swap(block_types, inserted.first);
          swap(block_values, inserted.second);
        }
      }
    }

    inline auto open_list_item() -> void {
      while (
        block_type != TextBlock::UnorderedList &&
        block_type != TextBlock::OrderedList &&
        block_type != TextBlock::NONE
      ) close_block();

      if (parent_blocks.empty()) {
        close_paragaph();
        parent_blocks.emplace_back(TextBlock::UnorderedList, vector<Offset<TextBlocks>>{});
      } else if (block_type == TextBlock::UnorderedList || block_type == TextBlock::OrderedList) {
        parent_blocks.emplace_back(block_type, vector<Offset<TextBlocks>>{});
        block_type = TextBlock::NONE;
      } else {
        close_block();
      }
    }

    inline auto finish() -> pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> {
      close_paragaph();
      while (!parent_blocks.empty()) {
        switch (block_type) {
          case TextBlock::UnorderedList:
          case TextBlock::OrderedList:
          case TextBlock::NONE:
            close_list();
            break;
          default:
            close_block();
        }
      }
      return { fbb.CreateVector(block_types), fbb.CreateVector(block_values) };
    }
  };

  static inline auto next_node(mmd_t* node, BlockBuilder& bb, string& text_buf) -> mmd_t* {
    auto type = mmdGetType(node);
    if (type != MMD_TYPE_CODE_BLOCK && type != MMD_TYPE_IMAGE) {
      if (auto next = mmdGetFirstChild(node)) return next;
    }
    do {
      auto type = mmdGetType(node);
      auto next = mmdGetNextSibling(node);
      const auto url = mmdGetURL(node);
      const bool type_changed = !mmdIsBlock(node) && (!next || type != mmdGetType(next)),
        url_changed = url && (!next || !strcmp(url, mmdGetURL(next))),
        changed = type_changed || url_changed,
        ws = next && mmdGetWhitespace(next);
      if (ws && (!changed || type == MMD_TYPE_NORMAL_TEXT)) text_buf.push_back(' ');
      if (changed) {
        if (!text_buf.empty()) {
          bb.add_plain_text(text_buf);
          text_buf.clear();
        }
        if (url_changed) bb.close_span();
      }
      switch (type) {
        case MMD_TYPE_PARAGRAPH:
        case MMD_TYPE_HEADING_1:
        case MMD_TYPE_HEADING_2:
        case MMD_TYPE_HEADING_3:
        case MMD_TYPE_HEADING_4:
        case MMD_TYPE_HEADING_5:
        case MMD_TYPE_HEADING_6:
          bb.close_paragaph();
          break;
        case MMD_TYPE_BLOCK_QUOTE:
        case MMD_TYPE_LIST_ITEM:
          bb.close_block();
          break;
        case MMD_TYPE_ORDERED_LIST:
        case MMD_TYPE_UNORDERED_LIST:
          bb.close_list();
          break;
        case MMD_TYPE_STRONG_TEXT:
        case MMD_TYPE_EMPHASIZED_TEXT:
        case MMD_TYPE_STRUCK_TEXT:
          if (type_changed) bb.close_span();
          break;
        default: ;
      }
      if (changed && ws && type != MMD_TYPE_NORMAL_TEXT) {
        if (mmdGetType(next) == MMD_TYPE_NORMAL_TEXT) text_buf.push_back(' ');
        else bb.add_plain_text(" ");
      }
      if (next) return next;
    } while ((node = mmdGetParent(node)));
    return nullptr;
  }

  auto RichTextParser::parse_markdown(
    FlatBufferBuilder& fbb,
    string_view markdown
  ) const -> pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> {
    auto file = fmemopen((void*)markdown.data(), markdown.size(), "r");
    auto doc = mmdLoadFile(file);
    if (doc == nullptr) {
      fclose(file);
      throw runtime_error("Out of memory when parsing Markdown");
    }
    BlockBuilder bb(fbb);
    mmd_type_t last_type = MMD_TYPE_NORMAL_TEXT;
    string text_buf;
    for (auto node = doc; node; node = next_node(node, bb, text_buf)) {
      const auto type = mmdGetType(node);

      if (mmdIsBlock(node)) {
        last_type = MMD_TYPE_NORMAL_TEXT;
      } else if (type != last_type) {
        switch (type) {
          case MMD_TYPE_STRONG_TEXT:
            bb.open_span(TextSpan::Bold);
            break;
          case MMD_TYPE_EMPHASIZED_TEXT:
            bb.open_span(TextSpan::Italic);
            break;
          case MMD_TYPE_STRUCK_TEXT:
            bb.open_span(TextSpan::Strikeout);
            break;
          default: ; // do nothing
        }
        last_type = type == MMD_TYPE_LINKED_TEXT ? MMD_TYPE_NORMAL_TEXT : type;
      }

      switch (type) {
        case MMD_TYPE_NORMAL_TEXT:
        case MMD_TYPE_STRONG_TEXT:
        case MMD_TYPE_EMPHASIZED_TEXT:
        case MMD_TYPE_STRUCK_TEXT:
          // TODO: Parse emoji shortcodes in text nodes
          text_buf.append(mmdGetText(node));
          break;
        case MMD_TYPE_CODE_TEXT:
          bb.add_code_span(mmdGetText(node));
          break;
        case MMD_TYPE_LINKED_TEXT: {
          const auto* url = mmdGetURL(node);
          if (url && !strcmp(url, "@")) {
            if (const auto* title = mmdGetExtra(node)) {
              bb.open_span(TextSpan::Link, url, title);
            } else {
              bb.open_span(TextSpan::Link, url);
            }
            break;
          }
          text_buf.append(mmdGetText(node));
          // Don't close the span.
          // Links can span multiple nodes which share a URL.
          break;
        }
        case MMD_TYPE_HARD_BREAK:
          bb.add_hard_break();
          break;
        case MMD_TYPE_SOFT_BREAK:
          bb.add_soft_break();
          break;
        case MMD_TYPE_THEMATIC_BREAK:
          bb.add_hr();
          break;
        case MMD_TYPE_PARAGRAPH:
          bb.open_block(TextBlock::P);
          break;
        case MMD_TYPE_HEADING_1:
          bb.open_block(TextBlock::H1);
          break;
        case MMD_TYPE_HEADING_2:
          bb.open_block(TextBlock::H2);
          break;
        case MMD_TYPE_HEADING_3:
          bb.open_block(TextBlock::H3);
          break;
        case MMD_TYPE_HEADING_4:
          bb.open_block(TextBlock::H4);
          break;
        case MMD_TYPE_HEADING_5:
          bb.open_block(TextBlock::H5);
          break;
        case MMD_TYPE_HEADING_6:
          bb.open_block(TextBlock::H6);
          break;
        case MMD_TYPE_BLOCK_QUOTE:
          bb.open_block(TextBlock::Blockquote);
          break;
        case MMD_TYPE_CODE_BLOCK: {
          string buf;
          for (auto* n = mmdGetFirstChild(node); n; n = mmdGetNextSibling(n)) {
            buf.append(mmdGetText(n));
          }
          if (const auto* language = mmdGetExtra(node)) {
            bb.add_code_block(buf, language);
          } else {
            bb.add_code_block(buf);
          }
          break;
        }
        case MMD_TYPE_IMAGE:
          if (auto* alt = mmdGetText(node)) {
            bb.add_image(mmdGetURL(node), alt);
          } else {
            bb.add_image(mmdGetURL(node));
          }
          break;
        case MMD_TYPE_ORDERED_LIST:
          bb.open_block(TextBlock::OrderedList);
          break;
        case MMD_TYPE_UNORDERED_LIST:
          bb.open_block(TextBlock::UnorderedList);
          break;
        case MMD_TYPE_LIST_ITEM:
          bb.open_list_item();
          break;
        case MMD_TYPE_DOCUMENT:
        case MMD_TYPE_TABLE:
        case MMD_TYPE_TABLE_BODY:
        case MMD_TYPE_TABLE_ROW:
        case MMD_TYPE_TABLE_HEADER:
        case MMD_TYPE_TABLE_HEADER_CELL:
        case MMD_TYPE_TABLE_BODY_CELL_LEFT:
        case MMD_TYPE_TABLE_BODY_CELL_RIGHT:
        case MMD_TYPE_TABLE_BODY_CELL_CENTER:
        case MMD_TYPE_CHECKBOX:
        case MMD_TYPE_METADATA:
        case MMD_TYPE_METADATA_TEXT:
        case MMD_TYPE_NONE:
          ; // Do nothing
      }
    }
    if (!text_buf.empty()) bb.add_plain_text(text_buf);
    mmdFree(doc);
    fclose(file);
    return bb.finish();
  }

  auto RichTextParser::plain_text_to_blocks(
    FlatBufferBuilder& fbb,
    string_view text
  ) -> pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> {
    return {
      fbb.CreateVector(vector{TextBlock::P}),
      fbb.CreateVector(vector{CreateTextSpans(fbb,
        fbb.CreateVector(vector{TextSpan::Plain}),
        fbb.CreateVector(vector{fbb.CreateSharedString(text).Union()})
      ).Union()})
    };
  }

  auto RichTextParser::parse_plain_text_with_emojis(
    FlatBufferBuilder& fbb,
    string_view text
  ) const -> pair<Offset<Vector<PlainTextWithEmojis>>, Offset<Vector<Offset<void>>>> {
    // TODO: Parse emoji shortcodes
    return plain_text_to_plain_text_with_emojis(fbb, text);
  }

  auto RichTextParser::plain_text_to_plain_text_with_emojis(
    FlatBufferBuilder& fbb,
    string_view text
  ) -> pair<Offset<Vector<PlainTextWithEmojis>>, Offset<Vector<Offset<void>>>> {
    return {
      fbb.CreateVector(vector{PlainTextWithEmojis::Plain}),
      fbb.CreateVector(vector{fbb.CreateSharedString(text).Union()})
    };
  }

  auto RichTextParser::plain_text_with_emojis_to_html(
    const Vector<PlainTextWithEmojis>* types,
    const Vector<Offset<void>>* values,
    const ToHtmlOptions& opts
  ) -> string {
    string out;
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      const auto ptr = values->GetAsString(i);
      if (ptr == nullptr) continue;
      switch (types->Get(i)) {
        case PlainTextWithEmojis::CustomEmoji:
          if (const auto url = opts.lookup_emoji(ptr->string_view())) {
            fmt::format_to(back_inserter(out),
              R"(<img class="custom-emoji" alt=":{0}:" title=":{0}:" src="{1}">)",
              Escape{ptr}, Escape{*url})
            ;
          } else {
            fmt::format_to(back_inserter(out), ":{}:", Escape{ptr});
          }
          break;
        case PlainTextWithEmojis::Plain:
        case PlainTextWithEmojis::NONE:
          fmt::format_to(back_inserter(out), "{}", Escape{ptr});
          break;
      }
    }
    return out;
  }

  static constexpr auto span_opening_tag(TextSpan span) -> string_view {
    switch (span) {
      case TextSpan::Bold: return "<strong>";
      case TextSpan::Italic: return "<em>";
      case TextSpan::Strikeout: return "<s>";
      case TextSpan::Code: return "<code>";
      case TextSpan::Link: return "<a>";
      case TextSpan::Spoiler: return R"(<span class="spoiler">)";
      case TextSpan::Math: return R"(<span class="math">$)";
      case TextSpan::HardBreak: return "<br>";
      case TextSpan::SoftBreak: return "<wbr>";
      case TextSpan::Plain:
      case TextSpan::CustomEmoji:
      case TextSpan::NONE:
        return "";
    }
  }

  static constexpr auto span_closing_tag(TextSpan span) -> string_view {
    switch (span) {
      case TextSpan::Bold: return "</strong>";
      case TextSpan::Italic: return "</em>";
      case TextSpan::Strikeout: return "</s>";
      case TextSpan::Code: return "</code>";
      case TextSpan::Link: return "</a>";
      case TextSpan::Spoiler: return "</span>";
      case TextSpan::Math: return "$</span>";
      case TextSpan::HardBreak:
      case TextSpan::SoftBreak:
      case TextSpan::Plain:
      case TextSpan::CustomEmoji:
      case TextSpan::NONE:
        return "";
    }
  }

  static auto write_span(
    string& out,
    const ToHtmlOptions& opts,
    unsigned i,
    const Vector<TextSpan>* types,
    const Vector<Offset<void>>* values
  ) -> void {
    const auto type = types->Get(i);
    switch (type) {
      case TextSpan::Plain:
      case TextSpan::Math:
      case TextSpan::Code:
        if (const auto ptr = values->GetAsString(i)) {
          fmt::format_to(back_inserter(out), "{}{}", span_opening_tag(type), Escape{ptr});
        }
        break;
      case TextSpan::Bold:
      case TextSpan::Italic:
      case TextSpan::Strikeout:
      case TextSpan::Spoiler:
        if (const auto ptr = values->GetAs<TextSpans>(i)) {
          if (ptr->spans()->size() == 0) return;
          out.append(span_opening_tag(type));
          for (unsigned ii = 0; ii < min(ptr->spans()->size(), ptr->spans_type()->size()); ii++) {
            write_span(out, opts, ii, ptr->spans_type(), ptr->spans());
          }
        }
        break;
      case TextSpan::Link:
        if (const auto ptr = values->GetAs<TextLink>(i)) {
          if (ptr->spans()->size() == 0) return;
          fmt::format_to(back_inserter(out), R"(<a href="{}" rel="{}noopener noreferrer"{} title="{}">)",
            Escape{ptr->href()},
            opts.links_nofollow ? "nofollow " : "",
            opts.open_links_in_new_tab ? R"( target="_blank")" : "",
            Escape{ptr->title() ? ptr->title() : ptr->href()}
          );
          for (unsigned ii = 0; ii < min(ptr->spans()->size(), ptr->spans_type()->size()); ii++) {
            write_span(out, opts, ii, ptr->spans_type(), ptr->spans());
          }
        }
        break;
      case TextSpan::CustomEmoji:
        if (const auto ptr = values->GetAsString(i)) {
          if (const auto url = opts.lookup_emoji(ptr->string_view())) {
            fmt::format_to(back_inserter(out),
              R"(<img class="custom-emoji" alt=":{0}:" title=":{0}:" src="{1}">)",
              Escape{ptr}, Escape{*url})
            ;
          } else {
            fmt::format_to(back_inserter(out), ":{}:", Escape{ptr});
          }
        }
        break;
      case TextSpan::HardBreak:
      case TextSpan::SoftBreak:
      case TextSpan::NONE:
        out.append(span_opening_tag(type));
    }
    out.append(span_closing_tag(type));
  }

  static constexpr auto block_opening_tag(TextBlock block) -> string_view {
    switch (block) {
      case TextBlock::H1: return "<h1>";
      case TextBlock::H2: return "<h2>";
      case TextBlock::H3: return "<h3>";
      case TextBlock::H4: return "<h4>";
      case TextBlock::H5: return "<h5>";
      case TextBlock::H6: return "<h6>";
      case TextBlock::Blockquote: return "<blockquote>";
      case TextBlock::Code: return "<pre><code>";
      case TextBlock::Math: return R"(<p class="math">$$)";
      case TextBlock::Hr: return "<hr>";
      case TextBlock::OrderedList: return "<ol>";
      case TextBlock::UnorderedList: return "<ul>";
      case TextBlock::P:
      case TextBlock::Image: return "<p>";
      case TextBlock::NONE: return "";
    }
  }

  static constexpr auto block_closing_tag(TextBlock block) -> string_view {
    switch (block) {
      case TextBlock::H1: return "</h1>";
      case TextBlock::H2: return "</h2>";
      case TextBlock::H3: return "</h3>";
      case TextBlock::H4: return "</h4>";
      case TextBlock::H5: return "</h5>";
      case TextBlock::H6: return "</h6>";
      case TextBlock::Blockquote: return "</blockquote>";
      case TextBlock::Code: return "</code></pre>";
      case TextBlock::Math: return R"($$</p>)";
      case TextBlock::OrderedList: return "</ol>";
      case TextBlock::UnorderedList: return "</ul>";
      case TextBlock::P:
      case TextBlock::Image: return "</p>";
      case TextBlock::Hr:
      case TextBlock::NONE: return "";
    }
  }

  static auto write_block(
    string& out,
    const ToHtmlOptions& opts,
    unsigned i,
    const Vector<TextBlock>* types,
    const Vector<Offset<void>>* values
  ) -> void {
    const auto type = types->Get(i);
    switch (type) {
      case TextBlock::P:
      case TextBlock::H1:
      case TextBlock::H2:
      case TextBlock::H3:
      case TextBlock::H4:
      case TextBlock::H5:
      case TextBlock::H6:
        if (const auto ptr = values->GetAs<TextSpans>(i)) {
          if (ptr->spans()->size() == 0) return;
          out.append(block_opening_tag(type));
          for (unsigned ii = 0; ii < min(ptr->spans()->size(), ptr->spans_type()->size()); ii++) {
            write_span(out, opts, ii, ptr->spans_type(), ptr->spans());
          }
        }
        break;
      case TextBlock::Blockquote:
        if (const auto ptr = values->GetAs<TextBlocks>(i)) {
          if (ptr->blocks()->size() == 0) return;
          out.append(block_opening_tag(type));
          for (unsigned ii = 0; ii < min(ptr->blocks()->size(), ptr->blocks_type()->size()); ii++) {
            write_block(out, opts, ii, ptr->blocks_type(), ptr->blocks());
          }
        }
        break;
      case TextBlock::Image:
        if (const auto ptr = values->GetAs<TextImage>(i)) {
          if (opts.show_images) {
            fmt::format_to(back_inserter(out),
              R"({}<img src="{}"{}">)",
              block_opening_tag(type),
              Escape{ptr->src()},
              ptr->alt() ? fmt::format(R"( alt="{}")", Escape{ptr->alt()}) : ""
            );
          } else {
            fmt::format_to(back_inserter(out),
              R"({}<details><summary>Image{}</summary><img src="{}"{}></details>)",
              block_opening_tag(type),
              ptr->alt() ? fmt::format(": {}", Escape{ptr->alt()}) : "",
              Escape{ptr->src()},
              ptr->alt() ? fmt::format(R"( alt="{}")", Escape{ptr->alt()}) : ""
            );
          }
        }
        break;
      case TextBlock::Code:
        if (const auto ptr = values->GetAs<TextCodeBlock>(i)) {
          if (ptr->language()) {
            fmt::format_to(back_inserter(out),
              R"(<pre><code class="language-{}">{})",
              Escape{ptr->language()}, Escape{ptr->text()}
            );
          } else {
            fmt::format_to(back_inserter(out), "{}{}", block_opening_tag(type), Escape{ptr->text()});
          }
        }
        break;
      case TextBlock::Math:
        if (const auto ptr = values->GetAsString(i)) {
          fmt::format_to(back_inserter(out), "{}{}", block_opening_tag(type), Escape{ptr});
        }
        break;
      case TextBlock::OrderedList:
      case TextBlock::UnorderedList:
        if (const auto ptr = values->GetAs<TextList>(i)) {
          if (ptr->items()->size() == 0) return;
          out.append(block_opening_tag(type));
          for (const auto item : *ptr->items()) {
            out.append("<li>");
            for (unsigned ii = 0; ii < min(item->blocks()->size(), item->blocks_type()->size()); ii++) {
              write_block(out, opts, ii, item->blocks_type(), item->blocks());
            }
            out.append("</li>");
          }
        }
        break;
      case TextBlock::Hr:
      case TextBlock::NONE:
        out.append(block_opening_tag(type));
    }
    out.append(block_closing_tag(type));
  }

  auto RichTextParser::blocks_to_html(
    const Vector<TextBlock>* types,
    const Vector<Offset<void>>* values,
    const ToHtmlOptions& opts
  ) -> string {
    string out;
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      write_block(out, opts, i, types, values);
    }
    return out;
  }

  static inline auto spans_to_text_content(
    const Vector<TextSpan>* types,
    const Vector<Offset<void>>* values,
    string& out
  ) -> void {
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      switch (types->Get(i)) {
        case TextSpan::Plain:
        case TextSpan::Code:
        case TextSpan::Math:
          if (const auto ptr = values->GetAsString(i)) {
            out.append(ptr->string_view());
          }
          break;
        case TextSpan::CustomEmoji:
          if (const auto ptr = values->GetAsString(i)) {
            fmt::format_to(back_inserter(out), ":{}:", ptr->string_view());
          }
          break;
        case TextSpan::Bold:
        case TextSpan::Italic:
        case TextSpan::Strikeout:
        case TextSpan::Spoiler:
          if (const auto ptr = values->GetAs<TextSpans>(i)) {
            spans_to_text_content(ptr->spans_type(), ptr->spans(), out);
          }
          break;
        case TextSpan::Link:
          if (const auto ptr = values->GetAs<TextLink>(i)) {
            spans_to_text_content(ptr->spans_type(), ptr->spans(), out);
            if (ptr->title()) {
              out.push_back(' ');
              out.append(ptr->title()->string_view());
            }
          }
          break;
        case TextSpan::SoftBreak:
        case TextSpan::HardBreak:
          out.push_back(' ');
          break;
        case TextSpan::NONE:
          ; // do nothing
      }
    }
  }

  auto RichTextParser::blocks_to_text_content(
    const Vector<TextBlock>* types,
    const Vector<Offset<void>>* values
  ) -> string {
    string out;
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      if (i) out.push_back(' ');
      switch (types->Get(i)) {
        case TextBlock::P:
        case TextBlock::H1:
        case TextBlock::H2:
        case TextBlock::H3:
        case TextBlock::H4:
        case TextBlock::H5:
        case TextBlock::H6:
          if (const auto ptr = values->GetAs<TextSpans>(i)) {
            spans_to_text_content(ptr->spans_type(), ptr->spans(), out);
          }
          break;
        case TextBlock::Blockquote:
          if (const auto ptr = values->GetAs<TextBlocks>(i)) {
            out.append(blocks_to_text_content(ptr->blocks_type(), ptr->blocks()));
          }
          break;
        case TextBlock::Math:
          if (const auto ptr = values->GetAsString(i)) {
            out.append(ptr->string_view());
          }
          break;
        case TextBlock::Code:
          if (const auto ptr = values->GetAs<TextCodeBlock>(i)) {
            out.append(ptr->text()->string_view());
          }
          break;
        case TextBlock::Image:
          if (const auto ptr = values->GetAs<TextImage>(i)) {
            if (ptr->alt()) out.append(ptr->alt()->string_view());
            else out.append(ptr->src()->string_view());
          }
          break;
        case TextBlock::OrderedList:
        case TextBlock::UnorderedList:
          if (const auto ptr = values->GetAs<TextList>(i)) {
            bool first = true;
            for (const auto item : *ptr->items()) {
              if (first) first = false;
              else out.push_back(' ');
              out.append(blocks_to_text_content(item->blocks_type(), item->blocks()));
            }
          }
          break;
        case TextBlock::Hr:
          out.append("---");
          break;
        case TextBlock::NONE:
          ; // do nothing
      }
    }
    return out;
  }

  auto RichTextParser::plain_text_with_emojis_to_text_content(
    const Vector<PlainTextWithEmojis>* types,
    const Vector<Offset<void>>* values
  ) -> string {
    string out;
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      switch (types->Get(i)) {
        case PlainTextWithEmojis::Plain:
          out.append(values->GetAsString(i)->string_view());
          break;
        case PlainTextWithEmojis::CustomEmoji:
          out.append(":");
          out.append(values->GetAsString(i)->string_view());
          out.append(":");
          break;
        case PlainTextWithEmojis::NONE: ;// do nothing
      }
    }
    return out;
  }
}
