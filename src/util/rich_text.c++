#include "rich_text.h++"
#include "util/web.h++"
#include "models/emoji_table.h++"
#include "static/emoji_table.fb.h++"
#include <stdio.h>
#include <static_block.hpp>
#include <static_vector.hpp>
#include <md4c.h>

using std::back_inserter, std::min, std::regex, std::regex_iterator,
    std::runtime_error, std::shared_ptr, std::string, std::string_view,
    std::vector, flatbuffers::FlatBufferBuilder, flatbuffers::Offset,
    flatbuffers::Vector;

namespace Ludwig {
  static const regex
    emoji_regex(R"(:([\w\-+.]+):)"),
    rich_text_shortcodes_regex(
      R"(:([\w\-+.]+):|)" // Emoji
      R"((^|[\s()\[\]{}])(/[bcu]/|[@!])([a-z][a-z0-9_]{0,63}(?:[@][a-z0-9-]+(?:[.][a-z0-9-]+)+)?))", // Reference
      regex::icase
    ),
    html_regex(R"(<[^>]*>|[&](\w+);)"); // beware of zalgo


  static std::unordered_map<std::string_view, std::string_view> shortcode_to_emoji;

  static_block {
    const auto emoji_table = flatbuffers::GetRoot<EmojiTable>(emoji_table_fb);
    for (const auto entry : *emoji_table->entries()) {
      for (const auto shortcode : *entry->shortcodes()) {
        shortcode_to_emoji.emplace(shortcode->string_view(), entry->emoji()->string_view());
      }
    }
  }

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

  auto html_to_rich_text(FlatBufferBuilder&, string_view, LibXmlContext&) -> RichTextVectors {
    throw runtime_error("not yet implemented");
  }

  static inline auto attr_to_text(const MD_ATTRIBUTE& a) noexcept -> string {
    string s;
    for (size_t i = 0; a.substr_offsets[i] < a.size; i++) {
      switch (a.substr_types[i]) {
        case MD_TEXT_ENTITY: // TODO: Entities in attributes
        case MD_TEXT_NULLCHAR: s += "�"; break;
        case MD_TEXT_SOFTBR: s += "\n"; break;
        default: s += string_view{a.text+a.substr_offsets[i], a.substr_offsets[i+1]-a.substr_offsets[i]};
      }
    }
    return s;
  }

  static inline auto html_open_block(MD_BLOCKTYPE type, void* detail) noexcept -> string {
    switch (type) {
      case MD_BLOCK_P: return "<p>";
      case MD_BLOCK_QUOTE: return "<blockquote>";
      case MD_BLOCK_H:
        return fmt::format("<h{:d}>", ((MD_BLOCK_H_DETAIL*)detail)->level);
      case MD_BLOCK_UL: return "<ul>";
      case MD_BLOCK_OL: {
        const auto* ol_detail = (MD_BLOCK_OL_DETAIL*)detail;
        if (ol_detail->start != 1) {
          return fmt::format(R"(<ol start="{:d}">)", ol_detail->start);
        }
        return "<ol>";
      }
      case MD_BLOCK_LI: return "<li>";
      case MD_BLOCK_CODE: {
        const auto* code_detail = (MD_BLOCK_CODE_DETAIL*)detail;
        if (code_detail->lang.size) {
          return fmt::format(R"(<pre data-language="{}"><code>)", Escape{attr_to_text(code_detail->lang)});
        }
        return "<pre><code>";
      }
      case MD_BLOCK_HR: return "<hr>";
      case MD_BLOCK_TABLE: return "<table>";
      case MD_BLOCK_THEAD: return "<thead>";
      case MD_BLOCK_TBODY: return "<tbody>";
      case MD_BLOCK_TR: return "<tr>";
      case MD_BLOCK_TH: return "<th>";
      case MD_BLOCK_TD:
        switch (((MD_BLOCK_TD_DETAIL*)detail)->align) {
          case MD_ALIGN_DEFAULT: return "<td>";
          case MD_ALIGN_LEFT: return R"(<td align="left">)";
          case MD_ALIGN_CENTER: return R"(<td align="center">)";
          case MD_ALIGN_RIGHT: return R"(<td align="right">)";
        }
      case MD_BLOCK_DOC:
      case MD_BLOCK_HTML: return "";
    }
  }

  static inline auto html_close_block(MD_BLOCKTYPE type, void* detail) noexcept -> string {
    switch (type) {
      case MD_BLOCK_P: return "</p>";
      case MD_BLOCK_QUOTE: return "</blockquote>";
      case MD_BLOCK_H:
        return fmt::format("</h{:d}>", ((MD_BLOCK_H_DETAIL*)detail)->level);
      case MD_BLOCK_UL: return "</ul>";
      case MD_BLOCK_OL: return "</ol>";
      case MD_BLOCK_LI: return "</li>";
      case MD_BLOCK_CODE: return "</code></pre>";
      case MD_BLOCK_TABLE: return "</table>";
      case MD_BLOCK_THEAD: return "</thead>";
      case MD_BLOCK_TBODY: return "</tbody>";
      case MD_BLOCK_TR: return "</tr>";
      case MD_BLOCK_TH: return "</th>";
      case MD_BLOCK_TD: return "</td>";
      case MD_BLOCK_HR:
      case MD_BLOCK_DOC:
      case MD_BLOCK_HTML: return "";
    }
  }

  struct ParseState {
    FlatBufferBuilder& fbb;
    vector<RichText> types;
    vector<Offset<void>> chunks;
    string text_buf, img_src;
    bool just_opened_block = true;
    int alt_depth = 0;

    ParseState(FlatBufferBuilder& fbb) : fbb(fbb) {}

    auto push_text_chunk() noexcept {
      if (!text_buf.empty()) {
        types.push_back(RichText::Text);
        chunks.push_back(fbb.CreateString(text_buf).Union());
        text_buf.clear();
      }
    }
  };

  static const MD_PARSER md_parser {
    .flags = MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_STRIKETHROUGH | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_NOHTML,
    .enter_block = [](MD_BLOCKTYPE type, void* detail, void* userdata) -> int {
      auto* st = (ParseState*)userdata;
      if (!st->just_opened_block) st->text_buf += "\n\n";
      st->text_buf += html_open_block(type, detail);
      st->just_opened_block = true;
      return 0;
    },
    .leave_block = [](MD_BLOCKTYPE type, void* detail, void* userdata) -> int {
      auto* st = (ParseState*)userdata;
      st->just_opened_block = false;
      st->text_buf += html_close_block(type, detail);
      return 0;
    },
    .enter_span = [](MD_SPANTYPE type, void* detail, void* userdata) -> int {
      auto* st = (ParseState*)userdata;
      if (st->alt_depth) {
        if (type == MD_SPAN_IMG) st->alt_depth++;
        return 0;
      }
      switch (type) {
        case MD_SPAN_EM: st->text_buf += "<em>"; break;
        case MD_SPAN_STRONG: st->text_buf += "<strong>"; break;
        case MD_SPAN_CODE: st->text_buf += "<code>"; break;
        case MD_SPAN_DEL: st->text_buf += "<del>"; break;
        case MD_SPAN_U: st->text_buf += "<u>"; break;
        case MD_SPAN_A: {
          const auto* a_detail = (MD_SPAN_A_DETAIL*)detail;
          st->push_text_chunk();
          st->types.push_back(RichText::Link);
          st->chunks.push_back(st->fbb.CreateString(attr_to_text(a_detail->href)).Union());
          break;
        }
        case MD_SPAN_IMG: {
          const auto* img_detail = (MD_SPAN_IMG_DETAIL*)detail;
          st->push_text_chunk();
          st->img_src = attr_to_text(img_detail->src);
          st->alt_depth = 1;
          break;
        }
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_WIKILINK:
          // do nothing
      }
      return 0;
    },
    .leave_span = [](MD_SPANTYPE type, void*, void* userdata) -> int {
      auto* st = (ParseState*)userdata;
      if (st->alt_depth) {
        if (type == MD_SPAN_IMG) st->alt_depth--;
        if (st->alt_depth) return 0;
      }
      switch (type) {
        case MD_SPAN_EM: st->text_buf += "</em>"; break;
        case MD_SPAN_STRONG: st->text_buf += "</strong>"; break;
        case MD_SPAN_CODE: st->text_buf += "</code>"; break;
        case MD_SPAN_DEL: st->text_buf += "</del>"; break;
        case MD_SPAN_U: st->text_buf += "</u>"; break;
        case MD_SPAN_A: st->text_buf += "</a>"; break;
        case MD_SPAN_IMG:
          st->types.push_back(RichText::Image);
          st->chunks.push_back(CreateRichTextImage(st->fbb,
            st->fbb.CreateString(st->img_src),
            st->text_buf.size() ? st->fbb.CreateString(st->text_buf) : 0
          ).Union());
          st->text_buf.clear();
          st->img_src.clear();
          st->alt_depth = 0;
          break;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_WIKILINK:
          // do nothing
      }
      return 0;
    },
    .text = [](MD_TEXTTYPE type, const char* text, MD_SIZE size, void* userdata) -> int {
      auto* st = (ParseState*)userdata;
      switch (type) {
        case MD_TEXT_NORMAL: {
          const string_view str{text, size};
          regex_iterator<string_view::iterator> inlines_begin(str.begin(), str.end(), rich_text_shortcodes_regex), inlines_end;
          size_t last_offset = 0;
          for (auto i = inlines_begin; i != inlines_end; ++i) {
            const auto match = *i;
            fmt::format_to(back_inserter(st->text_buf), "{}", Escape{str.substr(last_offset, (size_t)match.position(0) - last_offset)});
            last_offset = (size_t)(match.position(0) + match.length(0));
            const auto builtin_emoji = shortcode_to_emoji.find(match.str(1));
            if (builtin_emoji != shortcode_to_emoji.end()) {
              st->text_buf += builtin_emoji->second;
              continue;
            } else {
              st->text_buf += match.str(2);
            }
            st->push_text_chunk();
            if (match.length(1)) {
              st->types.push_back(RichText::Emoji);
              st->chunks.push_back(st->fbb.CreateString(match.str(1)).Union());
            }  else {
              st->types.push_back(match.str(3) == "/u/" || match.str(3) == "@" ? RichText::UserLink : RichText::BoardLink);
              st->chunks.push_back(st->fbb.CreateString(match.str(4)).Union());
              fmt::format_to(back_inserter(st->text_buf), "{}{}</a>", Escape{match.str(3)}, Escape{match.str(4)});
            }
          }
          fmt::format_to(back_inserter(st->text_buf), "{}", Escape{str.substr(last_offset)});
          break;
        }
        case MD_TEXT_CODE:
          fmt::format_to(back_inserter(st->text_buf), "{}", Escape{string_view{text, size}});
          break;
        case MD_TEXT_ENTITY: // TODO: HTML entity parsing
        case MD_TEXT_NULLCHAR: st->text_buf += "�"; break;
        case MD_TEXT_BR: if (!st->alt_depth) st->text_buf += "<br>"; break;
        case MD_TEXT_SOFTBR: st->text_buf += "\n"; break;
        case MD_TEXT_HTML:
        case MD_TEXT_LATEXMATH:
          // do nothing
      }
      return 0;
    },
    .debug_log = [](const char* msg, void*) {
      spdlog::debug("MD4C: {}", msg);
    }
  };

  auto markdown_to_rich_text(FlatBufferBuilder& fbb, string_view markdown) noexcept -> RichTextVectors {
    struct ParseState st(fbb);
    md_parse(markdown.data(), (unsigned)markdown.length(), &md_parser, &st);
    st.push_text_chunk();
    return { fbb.CreateVector(st.types), fbb.CreateVector(st.chunks) };
  }

  auto plain_text_with_emojis_to_rich_text(
    flatbuffers::FlatBufferBuilder& fbb,
    std::string_view text
  ) noexcept -> RichTextVectors {
    vector<RichText> types;
    vector<Offset<void>> chunks;
    string text_buf;
    regex_iterator<string_view::iterator> emoji_begin(text.begin(), text.end(), emoji_regex), emoji_end;
    size_t last_offset = 0;
    for (auto i = emoji_begin; i != emoji_end; ++i) {
      const auto match = *i;
      fmt::format_to(back_inserter(text_buf), "{}", Escape{text.substr(last_offset, (size_t)match.position(0) - last_offset)});
      last_offset = (size_t)(match.position(0) + match.length(0));
      const auto builtin_emoji = shortcode_to_emoji.find(match.str(1));
      if (builtin_emoji != shortcode_to_emoji.end()) {
        text_buf += builtin_emoji->second;
        continue;
      }
      if (!text_buf.empty()) {
        types.push_back(RichText::Text);
        chunks.push_back(fbb.CreateString(text_buf).Union());
        text_buf.clear();
      }
      types.push_back(RichText::Emoji);
      chunks.push_back(fbb.CreateString(match[1].str()).Union());
    }
    fmt::format_to(back_inserter(text_buf), "{}", Escape{text.substr(last_offset)});
    if (!text_buf.empty()) {
      types.push_back(RichText::Text);
      chunks.push_back(fbb.CreateString(text_buf).Union());
    }
    return { fbb.CreateVector(types), fbb.CreateVector(chunks) };
  }

  auto rich_text_to_html(
    const flatbuffers::Vector<RichText>* types,
    const flatbuffers::Vector<flatbuffers::Offset<void>>* values,
    const ToHtmlOptions& opts
  ) noexcept -> std::string {
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
            fmt::format_to(back_inserter(out), ":{}:", Escape{values->GetAsString(i)});
          }
          break;
        case RichText::Link:
          fmt::format_to(back_inserter(out), R"(<a href="{}" rel="noopener noreferrer{}"{}>)",
            Escape{values->GetAsString(i)},
            opts.links_nofollow ? " nofollow" : "",
            opts.open_links_in_new_tab ? R"( target="_blank")" : ""
          );
          break;
        case RichText::UserLink:
          fmt::format_to(back_inserter(out), R"(<a href="/u/{}"{}>)",
            Escape{values->GetAsString(i)},
            opts.open_links_in_new_tab ? R"( target="_blank")" : ""
          );
          break;
        case RichText::BoardLink:
          fmt::format_to(back_inserter(out), R"(<a href="/b/{}"{}>)",
            Escape{values->GetAsString(i)},
            opts.open_links_in_new_tab ? R"( target="_blank")" : ""
          );
          break;
        case RichText::Image: {
          const auto* img = values->GetAs<RichTextImage>(i);
          if (!opts.show_images) {
            fmt::format_to(back_inserter(out), R"(<details><summary>Image{}{}</summary>)",
              img->alt() ? ": " : "", Escape{img->alt()}
            );
          }
          fmt::format_to(back_inserter(out), R"(<img src="{}" loading="lazy")", Escape{img->src()});
          if (img->alt()) {
            fmt::format_to(back_inserter(out), R"( alt="{0}" title="{0}")", Escape{img->alt()});
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

  auto rich_text_to_plain_text(
    const Vector<RichText>* types,
    const Vector<Offset<void>>* values
  ) noexcept -> string {
    if (!types || !values) return "";
    string out;
    for (unsigned i = 0; i < min(types->size(), values->size()); i++) {
      switch (types->Get(i)) {
        case RichText::Text: {
          const auto text = values->GetAsString(i)->string_view();
          regex_iterator<string_view::iterator> html_begin(text.begin(), text.end(), html_regex), html_end;
          size_t last_offset = 0;
          for (auto i = html_begin; i != html_end; ++i) {
            const auto match = *i;
            out += text.substr(last_offset, (size_t)match.position(0) - last_offset);
            last_offset = (size_t)(match.position(0) + match.length(0));
            if (match[1].str() == "lt") out.push_back('<');
            else if (match[1].str() == "gt") out.push_back('>');
            else if (match[1].str() == "quot") out.push_back('"');
            else if (match[1].str() == "amp") out.push_back('&');
            else if (match[1].str() == "apos") out.push_back('\'');
          }
          out += text.substr(last_offset);
          break;
        }
        case RichText::Emoji:
          fmt::format_to(back_inserter(out), ":{}:", values->GetAsString(i)->string_view());
          break;
        default:
          // Do nothing
      }
    }
    return out;
  }
}
