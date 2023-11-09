#include "test_common.h++"
#include "util/rich_text.h++"

using std::string_view, flatbuffers::FlatBufferBuilder, flatbuffers::Vector,
    flatbuffers::Offset, flatbuffers::GetTemporaryPointer;

auto xml_ctx = std::make_shared<LibXmlContext>();
RichTextParser rt(xml_ctx);

template <typename T> static inline auto vec(const Vector<T>* v) -> vector<T> {
  vector<T> out;
  std::copy(v->cbegin(), v->cend(), std::back_inserter(out));
  return out;
}

template <typename T> static inline auto expect_union(const Vector<Offset<void>>* offsets, unsigned i) -> const T* {
  const auto ptr = offsets->GetAs<T>(i);
  REQUIRE(ptr != nullptr);
  return ptr;
}
static inline auto expect_string(const Vector<Offset<void>>* offsets, unsigned i) -> string_view {
  const auto ptr = offsets->GetAsString(i);
  REQUIRE(ptr != nullptr);
  return ptr->string_view();
}

static inline auto parse_results(
  FlatBufferBuilder& fbb,
  pair<Offset<Vector<TextBlock>>, Offset<Vector<Offset<void>>>> res
) -> pair<vector<TextBlock>, const Vector<Offset<void>>*> {
  return { vec(GetTemporaryPointer(fbb, res.first)), GetTemporaryPointer(fbb, res.second) };
}

TEST_CASE("parse plain text as Markdown", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  rt.parse_markdown(fbb, "The rain in Spain stays mainly on the plain");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{TextBlock::P});
  const auto p = expect_union<TextSpans>(blocks, 0);
  REQUIRE(vec(p->spans_type()) == vector{TextSpan::Plain});
  const auto text_content = expect_string(p->spans(), 0);
  REQUIRE(text_content == "The rain in Spain stays mainly on the plain");
  REQUIRE(rt.blocks_to_html(GetTemporaryPointer(fbb, res.first), blocks, {}) == "<p>The rain in Spain stays mainly on the plain</p>");
  REQUIRE(rt.blocks_to_text_content(GetTemporaryPointer(fbb, res.first), blocks) == "The rain in Spain stays mainly on the plain");
}

TEST_CASE("parse Markdown spans", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  rt.parse_markdown(fbb, "plain text **bold text** _italic text_ ~~strikeout text~~ plain text again");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{TextBlock::P});
  const auto p = expect_union<TextSpans>(blocks, 0);
  REQUIRE(vec(p->spans_type()) == vector{
    TextSpan::Plain,
    TextSpan::Bold,
    TextSpan::Plain,
    TextSpan::Italic,
    TextSpan::Plain,
    TextSpan::Strikeout,
    TextSpan::Plain
  });
  REQUIRE(expect_string(p->spans(), 0) == "plain text ");
  REQUIRE(expect_string(expect_union<TextSpans>(p->spans(), 1)->spans(), 0) == "bold text");
  REQUIRE(expect_string(p->spans(), 2) == " ");
  REQUIRE(expect_string(expect_union<TextSpans>(p->spans(), 3)->spans(), 0) == "italic text");
  REQUIRE(expect_string(p->spans(), 4) == " ");
  REQUIRE(expect_string(expect_union<TextSpans>(p->spans(), 5)->spans(), 0) == "strikeout text");
  REQUIRE(expect_string(p->spans(), 6) == " plain text again");
  REQUIRE(rt.blocks_to_html(GetTemporaryPointer(fbb, res.first), blocks, {}) == "<p>plain text <strong>bold text</strong> <em>italic text</em> <s>strikeout text</s> plain text again</p>");
  REQUIRE(rt.blocks_to_text_content(GetTemporaryPointer(fbb, res.first), blocks) == "plain text bold text italic text strikeout text plain text again");
}

TEST_CASE("parse Markdown paragraphs", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  rt.parse_markdown(fbb, R"(
paragraph one

paragraph two, this one has a
newline in it
)");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{TextBlock::P, TextBlock::P});
  const auto p1 = expect_union<TextSpans>(blocks, 0);
  REQUIRE(vec(p1->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(p1->spans(), 0) == "paragraph one");
  const auto p2 = expect_union<TextSpans>(blocks, 1);
  REQUIRE(vec(p2->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(p2->spans(), 0) == "paragraph two, this one has a newline in it");
}

TEST_CASE("parse Markdown blocks", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  rt.parse_markdown(fbb, R"(
paragraph one

paragraph two, this one has a
newline in it

### heading 3

> blockquote 4

---

```
code block 5
```
)");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{TextBlock::P, TextBlock::P, TextBlock::H3, TextBlock::Blockquote, TextBlock::Hr, TextBlock::Code});
  const auto p1 = expect_union<TextSpans>(blocks, 0);
  REQUIRE(vec(p1->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(p1->spans(), 0) == "paragraph one");
  const auto p2 = expect_union<TextSpans>(blocks, 1);
  REQUIRE(vec(p2->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(p2->spans(), 0) == "paragraph two, this one has a newline in it");
  const auto h3 = expect_union<TextSpans>(blocks, 2);
  REQUIRE(vec(h3->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(h3->spans(), 0) == "heading 3");
  const auto q4 = expect_union<TextBlocks>(blocks, 3);
  REQUIRE(vec(q4->blocks_type()) == vector{TextBlock::P});
  const auto p4 = expect_union<TextSpans>(q4->blocks(), 0);
  REQUIRE(vec(p4->spans_type()) == vector{TextSpan::Plain});
  REQUIRE(expect_string(p4->spans(), 0) == "blockquote 4");
  const auto c5 = expect_union<TextCodeBlock>(blocks, 5);
  REQUIRE(c5->text()->string_view() == "code block 5\n");

  REQUIRE(rt.blocks_to_html(GetTemporaryPointer(fbb, res.first), blocks, {}) ==
    "<p>paragraph one</p>"
    "<p>paragraph two, this one has a newline in it</p>"
    "<h3>heading 3</h3>"
    "<blockquote><p>blockquote 4</p></blockquote>"
    "<hr>"
    "<pre><code>code block 5\n</code></pre>"
  );
  REQUIRE(rt.blocks_to_text_content(GetTemporaryPointer(fbb, res.first), blocks) ==
    "paragraph one paragraph two, this one has a newline in it heading 3 blockquote 4 --- code block 5\n");
}
