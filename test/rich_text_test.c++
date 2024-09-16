#include "test_common.h++"
#include "util/common.h++"
#include "util/rich_text.h++"

using std::string_view, flatbuffers::FlatBufferBuilder, flatbuffers::Vector,
    flatbuffers::Offset;

auto xml_ctx = std::make_shared<LibXmlContext>();

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
  const RichTextVectors& res
) -> pair<vector<RichText>, const Vector<Offset<void>>*> {
  return { vec(get_temporary_pointer(fbb, res.first)), get_temporary_pointer(fbb, res.second) };
}

TEST_CASE("parse plain text as Markdown", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  markdown_to_rich_text(fbb, "The rain in Spain stays mainly on the plain");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  const auto text_content = expect_string(blocks, 0);
  REQUIRE(text_content == "<p>The rain in Spain stays mainly on the plain</p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == "<p>The rain in Spain stays mainly on the plain</p>");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "The rain in Spain stays mainly on the plain");
}

TEST_CASE("parse Markdown spans", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "plain text **bold text** _italic text_ ~~strikeout text~~ \"plain text\" again");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  const auto text_content = expect_string(blocks, 0);
  REQUIRE(text_content ==  "<p>plain text <strong>bold text</strong> <em>italic text</em> <del>strikeout text</del> &quot;plain text&quot; again</p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == text_content);
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "plain text bold text italic text strikeout text \"plain text\" again");
}

TEST_CASE("parse Markdown paragraphs", "[rich_text]") {
  {
    FlatBufferBuilder fbb;
    const auto res =  markdown_to_rich_text(fbb, R"(paragraph one

paragraph two)");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text});
    const auto text_content = expect_string(blocks, 0);
    REQUIRE(text_content == "<p>paragraph one</p>\n\n<p>paragraph two</p>");
    REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == text_content);
    REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "paragraph one\n\nparagraph two");
  }
  {
    FlatBufferBuilder fbb;
    const auto res =  markdown_to_rich_text(fbb, R"(
paragraph one

paragraph two, this one has a
newline in it
  )");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text});
    const auto text_content = expect_string(blocks, 0);
    REQUIRE(text_content == "<p>paragraph one</p>\n\n<p>paragraph two, this one has a\nnewline in it</p>");
    REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == text_content);
    REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "paragraph one\n\nparagraph two, this one has a\nnewline in it");
  }
}

TEST_CASE("parse Markdown blocks", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  markdown_to_rich_text(fbb, R"(
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
  REQUIRE(types == vector{RichText::Text});
  const auto text_content = expect_string(blocks, 0);
  REQUIRE(text_content ==
    "<p>paragraph one</p>\n\n"
    "<p>paragraph two, this one has a\nnewline in it</p>\n\n"
    "<h3>heading 3</h3>\n\n"
    "<blockquote><p>blockquote 4</p></blockquote>\n\n"
    "<hr>\n\n"
    "<pre><code>code block 5\n</code></pre>"
  );
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == text_content);
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) ==
    "paragraph one\n\nparagraph two, this one has a\nnewline in it\n\nheading 3\n\nblockquote 4\n\n\n\ncode block 5\n");
}

TEST_CASE("parse Markdown lists", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res =  markdown_to_rich_text(fbb, R"(
- foo
- bar
- 
  1. baz
  2. qux
  3. quux
)");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  const auto text_content = expect_string(blocks, 0);
  REQUIRE(text_content ==
    "<ul><li>foo</li>\n\n"
    "<li>bar</li>\n\n"
    "<li><ol><li>baz</li>\n\n"
    "<li>qux</li>\n\n"
    "<li>quux</li></ol></li></ul>"
  );
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == text_content);
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "foo\n\nbar\n\nbaz\n\nqux\n\nquux");
}

TEST_CASE("parse Markdown links", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "You're the 1,000,000th visitor! [Click here](http://example.com) to claim your prize!");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "<p>You&apos;re the 1,000,000th visitor! ");
  REQUIRE(expect_string(blocks, 1) == "http://example.com");
  REQUIRE(expect_string(blocks, 2) == "Click here</a> to claim your prize!</p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == R"(<p>You&apos;re the 1,000,000th visitor! <a href="http://example.com" rel="noopener noreferrer nofollow">Click here</a> to claim your prize!</p>)");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "You're the 1,000,000th visitor! Click here to claim your prize!");
}

TEST_CASE("parse Markdown complex links", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "[Link `one`](/1)[Link _two_**(!)**](/2)");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text, RichText::Link, RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "<p>");
  REQUIRE(expect_string(blocks, 1) == "/1");
  REQUIRE(expect_string(blocks, 2) == "Link <code>one</code></a>");
  REQUIRE(expect_string(blocks, 3) == "/2");
  REQUIRE(expect_string(blocks, 4) == "Link <em>two</em><strong>(!)</strong></a></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == R"(<p><a href="/1" rel="noopener noreferrer nofollow">Link <code>one</code></a><a href="/2" rel="noopener noreferrer nofollow">Link <em>two</em><strong>(!)</strong></a></p>)");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "Link oneLink two(!)");
}

TEST_CASE("parse Markdown builtin emoji", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "Nice :+1: **:fire::fire::fire:**");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "<p>Nice 👍 <strong>🔥🔥🔥</strong></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == "<p>Nice 👍 <strong>🔥🔥🔥</strong></p>");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "Nice 👍 🔥🔥🔥");
}

TEST_CASE("parse Markdown custom emoji", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "Nice :+2: **:water::water::water:**");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text, RichText::Emoji, RichText::Text, RichText::Emoji, RichText::Emoji, RichText::Emoji, RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "<p>Nice ");
  REQUIRE(expect_string(blocks, 1) == "+2");
  REQUIRE(expect_string(blocks, 2) == " <strong>");
  REQUIRE(expect_string(blocks, 3) == "water");
  REQUIRE(expect_string(blocks, 4) == "water");
  REQUIRE(expect_string(blocks, 5) == "water");
  REQUIRE(expect_string(blocks, 6) == "</strong></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == "<p>Nice :+2: <strong>:water::water::water:</strong></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {
    .lookup_emoji = [](auto emoji) {
      return fmt::format(R"(<img src="/{}.webp">)", Escape{emoji});
    }
  }) == R"(<p>Nice <img src="/+2.webp"> <strong><img src="/water.webp"><img src="/water.webp"><img src="/water.webp"></strong></p>)");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "Nice :+2: :water::water::water:");
}

TEST_CASE("escape Markdown emoji with code blocks", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "normal :+1: `escaped :+1:`");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "<p>normal 👍 <code>escaped :+1:</code></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == "<p>normal 👍 <code>escaped :+1:</code></p>");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "normal 👍 escaped :+1:");
}

TEST_CASE("parse plaintext builtin emoji", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = plain_text_with_emojis_to_rich_text(fbb, "Nice :+1: :fire::fire::fire:");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text});
  REQUIRE(expect_string(blocks, 0) == "Nice 👍 🔥🔥🔥");
  REQUIRE(rich_text_to_html_emojis_only(get_temporary_pointer(fbb, res.first), blocks, {}) == "Nice 👍 🔥🔥🔥");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "Nice 👍 🔥🔥🔥");
}

TEST_CASE("parse plaintext custom emoji", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = plain_text_with_emojis_to_rich_text(fbb, "Nice :+2: :water::water::water:");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{RichText::Text, RichText::Emoji, RichText::Text, RichText::Emoji, RichText::Emoji, RichText::Emoji});
  REQUIRE(expect_string(blocks, 0) == "Nice ");
  REQUIRE(expect_string(blocks, 1) == "+2");
  REQUIRE(expect_string(blocks, 2) == " ");
  REQUIRE(expect_string(blocks, 3) == "water");
  REQUIRE(expect_string(blocks, 4) == "water");
  REQUIRE(expect_string(blocks, 5) == "water");
  REQUIRE(rich_text_to_html_emojis_only(get_temporary_pointer(fbb, res.first), blocks, {}) == "Nice :+2: :water::water::water:");
  REQUIRE(rich_text_to_html_emojis_only(get_temporary_pointer(fbb, res.first), blocks, {
    .lookup_emoji = [](auto emoji) {
      return fmt::format(R"(<img src="/{}.webp">)", Escape{emoji});
    }
  }) == R"(Nice <img src="/+2.webp"> <img src="/water.webp"><img src="/water.webp"><img src="/water.webp">)");
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "Nice :+2: :water::water::water:");
}

TEST_CASE("parse Markdown user and board references", "[rich_text]") {
  FlatBufferBuilder fbb;
  const auto res = markdown_to_rich_text(fbb, "/u/foo @foo /b/foo /c/foo !foo /u/foo@bar.example @foo@bar.example /b/foo@bar.example /c/foo@bar.example !foo@bar.example");
  const auto [types, blocks] = parse_results(fbb, res);
  REQUIRE(types == vector{
    RichText::Text,
    RichText::UserLink,
    RichText::Text,
    RichText::UserLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
    RichText::UserLink,
    RichText::Text,
    RichText::UserLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
    RichText::BoardLink,
    RichText::Text,
  });
  REQUIRE(expect_string(blocks, 0) == "<p>");
  REQUIRE(expect_string(blocks, 1) == "foo");
  REQUIRE(expect_string(blocks, 2) == "/u/foo</a> ");
  REQUIRE(expect_string(blocks, 3) == "foo");
  REQUIRE(expect_string(blocks, 4) == "@foo</a> ");
  REQUIRE(expect_string(blocks, 5) == "foo");
  REQUIRE(expect_string(blocks, 6) == "/b/foo</a> ");
  REQUIRE(expect_string(blocks, 7) == "foo");
  REQUIRE(expect_string(blocks, 8) == "/c/foo</a> ");
  REQUIRE(expect_string(blocks, 9) == "foo");
  REQUIRE(expect_string(blocks, 10) == "!foo</a> ");
  REQUIRE(expect_string(blocks, 11) == "foo@bar.example");
  REQUIRE(expect_string(blocks, 12) == "/u/foo@bar.example</a> ");
  REQUIRE(expect_string(blocks, 13) == "foo@bar.example");
  REQUIRE(expect_string(blocks, 14) == "@foo@bar.example</a> ");
  REQUIRE(expect_string(blocks, 15) == "foo@bar.example");
  REQUIRE(expect_string(blocks, 16) == "/b/foo@bar.example</a> ");
  REQUIRE(expect_string(blocks, 17) == "foo@bar.example");
  REQUIRE(expect_string(blocks, 18) == "/c/foo@bar.example</a> ");
  REQUIRE(expect_string(blocks, 19) == "foo@bar.example");
  REQUIRE(expect_string(blocks, 20) == "!foo@bar.example</a></p>");
  REQUIRE(rich_text_to_html(get_temporary_pointer(fbb, res.first), blocks, {}) == "<p>"
    R"(<a href="/u/foo">/u/foo</a> )"
    R"(<a href="/u/foo">@foo</a> )"
    R"(<a href="/b/foo">/b/foo</a> )"
    R"(<a href="/b/foo">/c/foo</a> )"
    R"(<a href="/b/foo">!foo</a> )"
    R"(<a href="/u/foo@bar.example">/u/foo@bar.example</a> )"
    R"(<a href="/u/foo@bar.example">@foo@bar.example</a> )"
    R"(<a href="/b/foo@bar.example">/b/foo@bar.example</a> )"
    R"(<a href="/b/foo@bar.example">/c/foo@bar.example</a> )"
    R"(<a href="/b/foo@bar.example">!foo@bar.example</a></p>)"
  );
  REQUIRE(rich_text_to_plain_text(get_temporary_pointer(fbb, res.first), blocks) == "/u/foo @foo /b/foo /c/foo !foo /u/foo@bar.example @foo@bar.example /b/foo@bar.example /c/foo@bar.example !foo@bar.example");
}

TEST_CASE("parse Markdown auto links", "[rich_text]") {
  FlatBufferBuilder fbb;
  {
    const auto res = markdown_to_rich_text(fbb, "Go to https://example.com for more information");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text});
    REQUIRE(expect_string(blocks, 0) == "<p>Go to ");
    REQUIRE(expect_string(blocks, 1) == "https://example.com");
    REQUIRE(expect_string(blocks, 2) == "https://example.com</a> for more information</p>");
  }
  {
    const auto res = markdown_to_rich_text(fbb, "Go to https://example.com/ for more information");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text});
    REQUIRE(expect_string(blocks, 0) == "<p>Go to ");
    REQUIRE(expect_string(blocks, 1) == "https://example.com/");
    REQUIRE(expect_string(blocks, 2) == "https://example.com/</a> for more information</p>");
  }
  {
    const auto res = markdown_to_rich_text(fbb, "Go to https://example.com/foo?bar=(baz) for more information");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text});
    REQUIRE(expect_string(blocks, 0) == "<p>Go to ");
    REQUIRE(expect_string(blocks, 1) == "https://example.com/foo?bar=(baz)");
    REQUIRE(expect_string(blocks, 2) == "https://example.com/foo?bar=(baz)</a> for more information</p>");
  }
  {
    const auto res = markdown_to_rich_text(fbb, "Go to (https://example.com) for more information");
    const auto [types, blocks] = parse_results(fbb, res);
    REQUIRE(types == vector{RichText::Text, RichText::Link, RichText::Text});
    REQUIRE(expect_string(blocks, 0) == "<p>Go to (");
    REQUIRE(expect_string(blocks, 1) == "https://example.com");
    REQUIRE(expect_string(blocks, 2) == "https://example.com</a>) for more information</p>");
  }
}
