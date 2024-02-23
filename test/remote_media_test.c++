#include "test_common.h++"
#include "services/db.h++"
#include "controllers/remote_media.h++"

using flatbuffers::FlatBufferBuilder;

static const auto xml_ctx = make_shared<LibXmlContext>();
static const string WIKI_URL = "https://wikipedia.test/Red_Panda";

TEST_CASE("fetch Wikipedia link card", "[remote_media]") {
  TempFile db_file;
  auto db = make_shared<DB>(db_file.name);
  uint64_t thread_id;
  {
    auto txn = db->open_write_txn();
    FlatBufferBuilder fbb;
    {
      const auto user_name = fbb.CreateString("foo");
      UserBuilder u(fbb);
      u.add_name(user_name);
      u.add_created_at(now_s());
      fbb.Finish(u.Finish());
    }
    const auto user_id = txn.create_user(fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto board_name = fbb.CreateString("bar");
      BoardBuilder b(fbb);
      b.add_name(board_name);
      b.add_created_at(now_s());
      fbb.Finish(b.Finish());
    }
    const auto board_id = txn.create_board(fbb.GetBufferSpan());
    fbb.Clear();
    {
      const auto thread_url = fbb.CreateString(WIKI_URL);
      const auto [title_type, title] = plain_text_to_rich_text(fbb, "Red panda");
      ThreadBuilder t(fbb);
      t.add_author(user_id);
      t.add_board(board_id);
      t.add_content_url(thread_url);
      t.add_title_type(title_type);
      t.add_title(title);
      t.add_created_at(now_s());
      fbb.Finish(t.Finish());
    }
    thread_id = txn.create_thread(fbb.GetBufferSpan());
    txn.commit();
  }

  auto http_client = make_shared<MockHttpClient>();
  http_client->on_get(WIKI_URL, 200, TYPE_HTML, load_file(test_root() / "fixtures" / "wikipedia_red_panda.html"));
  RemoteMediaController remote_media(db, http_client, xml_ctx);

  remote_media.fetch_link_card_for_thread(thread_id);

  auto txn = db->open_read_txn();
  const auto link_card_opt = txn.get_link_card(WIKI_URL);
  REQUIRE(!!link_card_opt);
  const auto& card = link_card_opt->get();
  REQUIRE(card.fetch_complete() == true);
  REQUIRE(card.fetch_tries() == 1);
  REQUIRE(card.last_fetch_at() > 0);
  REQUIRE(card.title() != nullptr);
  REQUIRE(card.title()->string_view() == "Red panda - Wikipedia");
  REQUIRE(card.image_url() != nullptr);
  REQUIRE(card.image_url()->string_view() == "https://upload.wikimedia.org/wikipedia/commons/thumb/e/e6/Red_Panda_%2824986761703%29.jpg/1200px-Red_Panda_%2824986761703%29.jpg");
}
