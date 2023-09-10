#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>
#include "util.h++"
#include "../src/db.h++"

using namespace Ludwig;
using namespace flatbuffers;

/*
struct test_init {
  test_init() {
    spdlog::set_level(spdlog::level::debug);
  }
} test_init_instance;
*/

TEST_CASE("create DB", "[db]") {
  TempFile file;
  DB db(file.name);
}

TEST_CASE("create and get user", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t id;
  db.open_write_txn([&](WriteTxn txn) {
    FlatBufferBuilder fbb;
    auto offset = CreateUserDirect(fbb,
      "testuser",
      "Test User",
      "",
      "",
      "",
      {},
      now_s()
    );
    id = txn.create_user(std::move(fbb), offset);
    txn.commit();
  });
  {
    auto txn = db.open_read_txn();
    auto user = txn.get_user(id);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "testuser"sv);
    REQUIRE((*user)->display_name()->string_view() == "Test User"sv);
  }
}

static inline auto create_users(DB& db, uint64_t ids[3]) {
  db.open_write_txn([&](WriteTxn txn) {
    {
      FlatBufferBuilder fbb;
      ids[0] = txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user1",
        "User 1",
        "",
        "",
        "",
        {},
        now_s()
      ));
    }
    {
      FlatBufferBuilder fbb;
      ids[1] = txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user2",
        "User 2",
        "",
        "",
        "",
        {},
        now_s()
      ));
    }
    {
      FlatBufferBuilder fbb;
      ids[2] = txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "user3",
        "User 3",
        "",
        "",
        "",
        {},
        now_s()
      ));
    }
    txn.commit();
  });
}

static inline auto create_boards(DB& db, uint64_t ids[3]) {
  db.open_write_txn([&](WriteTxn txn) {
    {
      FlatBufferBuilder fbb;
      ids[0] = txn.create_board(std::move(fbb), CreateBoardDirect(fbb,
        "lions",
        "Lions",
        "",
        "",
        {},
        now_s()
      ));
    }
    {
      FlatBufferBuilder fbb;
      ids[1] = txn.create_board(std::move(fbb), CreateBoardDirect(fbb,
        "tigers",
        "Tigers",
        "",
        "",
        {},
        now_s()
      ));
    }
    {
      FlatBufferBuilder fbb;
      ids[2] = txn.create_board(std::move(fbb), CreateBoardDirect(fbb,
        "bears",
        "Bears",
        "",
        "",
        {},
        now_s()
      ));
    }
    txn.commit();
  });
}

TEST_CASE("create and list users", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t user_ids[3];
  create_users(db, user_ids);
  {
    auto txn = db.open_read_txn();
    auto iter = txn.list_users();
    REQUIRE(!iter.is_done());
    auto user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user1"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 1"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user2"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 2"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE((*user)->name()->string_view() == "user3"sv);
    REQUIRE((*user)->display_name()->string_view() == "User 3"sv);
    ++iter;
    REQUIRE(iter.is_done());
  }
}

TEST_CASE("create users and boards, subscribe and unsubscribe", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t user_ids[3], board_ids[3];
  create_users(db, user_ids);
  create_boards(db, board_ids);
  db.open_write_txn([&](WriteTxn txn) {
    txn.set_subscription(user_ids[0], board_ids[0], true);
    txn.set_subscription(user_ids[1], board_ids[0], true);
    txn.set_subscription(user_ids[2], board_ids[0], true);
    txn.set_subscription(user_ids[0], board_ids[1], true);
    txn.set_subscription(user_ids[1], board_ids[1], true);
    txn.set_subscription(user_ids[0], board_ids[2], true);
    txn.commit();
  });
  {
    auto txn = db.open_read_txn();
    auto stats0 = *txn.get_board_stats(board_ids[0]),
         stats1 = *txn.get_board_stats(board_ids[1]),
         stats2 = *txn.get_board_stats(board_ids[2]);
    REQUIRE(stats0->subscriber_count() == 3);
    REQUIRE(stats1->subscriber_count() == 2);
    REQUIRE(stats2->subscriber_count() == 1);
    REQUIRE(txn.user_is_subscribed(user_ids[0], board_ids[0]));
    REQUIRE(txn.user_is_subscribed(user_ids[1], board_ids[0]));
    REQUIRE(txn.user_is_subscribed(user_ids[2], board_ids[0]));
    REQUIRE(txn.user_is_subscribed(user_ids[0], board_ids[1]));
    REQUIRE(txn.user_is_subscribed(user_ids[1], board_ids[1]));
    REQUIRE(!txn.user_is_subscribed(user_ids[2], board_ids[1]));
    REQUIRE(txn.user_is_subscribed(user_ids[0], board_ids[2]));
    REQUIRE(!txn.user_is_subscribed(user_ids[1], board_ids[2]));
    REQUIRE(!txn.user_is_subscribed(user_ids[2], board_ids[2]));
  }
  db.open_write_txn([&](WriteTxn txn) {
    txn.set_subscription(user_ids[0], board_ids[0], false);
    txn.set_subscription(user_ids[0], board_ids[1], false);
    txn.set_subscription(user_ids[0], board_ids[2], false);
    txn.commit();
  });
  {
    auto txn = db.open_read_txn();
    auto stats0 = *txn.get_board_stats(board_ids[0]),
         stats1 = *txn.get_board_stats(board_ids[1]),
         stats2 = *txn.get_board_stats(board_ids[2]);
    REQUIRE(stats0->subscriber_count() == 2);
    REQUIRE(stats1->subscriber_count() == 1);
    REQUIRE(stats2->subscriber_count() == 0);
    REQUIRE(!txn.user_is_subscribed(user_ids[0], board_ids[0]));
    REQUIRE(txn.user_is_subscribed(user_ids[1], board_ids[0]));
    REQUIRE(txn.user_is_subscribed(user_ids[2], board_ids[0]));
    REQUIRE(!txn.user_is_subscribed(user_ids[0], board_ids[1]));
    REQUIRE(txn.user_is_subscribed(user_ids[1], board_ids[1]));
    REQUIRE(!txn.user_is_subscribed(user_ids[2], board_ids[1]));
    REQUIRE(!txn.user_is_subscribed(user_ids[0], board_ids[2]));
    REQUIRE(!txn.user_is_subscribed(user_ids[1], board_ids[2]));
    REQUIRE(!txn.user_is_subscribed(user_ids[2], board_ids[2]));
  }
}

static inline auto create_page(WriteTxn& txn, uint64_t user, uint64_t board, const char* title, const char* url) -> uint64_t {
  FlatBufferBuilder fbb;
  return txn.create_page(std::move(fbb), CreatePageDirect(fbb,
    user,
    board,
    title,
    now_s(),
    {},
    {},
    nullptr,
    nullptr,
    url
  ));
}

TEST_CASE("create and list posts", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t user_ids[3], board_ids[3], page_ids[12];
  create_users(db, user_ids);
  create_boards(db, board_ids);
  db.open_write_txn([&](WriteTxn txn) {
    page_ids[0] = create_page(txn, user_ids[0], board_ids[0], "post 1", "http://example.com");
    page_ids[1] = create_page(txn, user_ids[0], board_ids[0], "post 2", "http://example.com");
    page_ids[2] = create_page(txn, user_ids[0], board_ids[0], "post 3", "http://example.com");
    page_ids[3] = create_page(txn, user_ids[0], board_ids[0], "post 4", "http://example.com");
    page_ids[4] = create_page(txn, user_ids[0], board_ids[1], "post 5", "http://example.com");
    page_ids[5] = create_page(txn, user_ids[0], board_ids[1], "post 6", "http://example.com");
    page_ids[6] = create_page(txn, user_ids[1], board_ids[0], "post 7", "http://example.com");
    page_ids[7] = create_page(txn, user_ids[1], board_ids[0], "post 8", "http://example.com");
    page_ids[8] = create_page(txn, user_ids[1], board_ids[2], "post 9", "http://example.com");
    page_ids[9] = create_page(txn, user_ids[1], board_ids[2], "post 10", "http://example.com");
    page_ids[10] = create_page(txn, user_ids[2], board_ids[1], "post 11", "http://example.com");
    page_ids[11] = create_page(txn, user_ids[2], board_ids[2], "post 12", "http://example.com");
    txn.commit();
  });
  {
    auto txn = db.open_read_txn();
    REQUIRE((*txn.get_user_stats(user_ids[0]))->page_count() == 6);
    REQUIRE((*txn.get_user_stats(user_ids[1]))->page_count() == 4);
    REQUIRE((*txn.get_user_stats(user_ids[2]))->page_count() == 2);
    REQUIRE((*txn.get_board_stats(board_ids[0]))->page_count() == 6);
    REQUIRE((*txn.get_board_stats(board_ids[1]))->page_count() == 3);
    REQUIRE((*txn.get_board_stats(board_ids[2]))->page_count() == 3);
    size_t i = 0;
    for (uint64_t id : txn.list_pages_of_user_new(user_ids[0])) {
      REQUIRE(i < 6);
      REQUIRE(id == page_ids[5 - i]);
      i++;
    }
    REQUIRE(i == 6);
    i = 0;
    for (uint64_t id : txn.list_pages_of_user_new(user_ids[1])) {
      REQUIRE(i < 4);
      REQUIRE(id == page_ids[9 - i]);
      i++;
    }
    REQUIRE(i == 4);
    i = 0;
    for (uint64_t id : txn.list_pages_of_user_new(user_ids[2])) {
      REQUIRE(i < 2);
      REQUIRE(id == page_ids[11 - i]);
      i++;
    }

    static const size_t board_pages[] = {
      // board_ids[0]
      7, 6, 3, 2, 1, 0,
      // board_ids[1]
      10, 5, 4,
      // board_ids[2]
      11, 9, 8
    };

    i = 0;
    for (uint64_t id : txn.list_pages_of_board_new(board_ids[0])) {
      REQUIRE(i < 6);
      REQUIRE(id == page_ids[board_pages[i]]);
      i++;
    }
    REQUIRE(i == 6);
    for (uint64_t id : txn.list_pages_of_board_new(board_ids[1])) {
      REQUIRE(i < 9);
      REQUIRE(id == page_ids[board_pages[i]]);
      i++;
    }
    REQUIRE(i == 9);
    for (uint64_t id : txn.list_pages_of_board_new(board_ids[2])) {
      REQUIRE(i < 12);
      REQUIRE(id == page_ids[board_pages[i]]);
      i++;
    }
    REQUIRE(i == 12);
  }
}

static inline auto random_int(std::mt19937& gen, uint64_t n) -> uint64_t {
  std::uniform_int_distribution<uint64_t> dist(0, n - 1);
  return dist(gen);
}

TEST_CASE("generate and delete random posts and check stats", "[db]") {
  TempFile file;
  DB db(file.name);
  std::random_device rd;
  std::mt19937 gen(rd());
# define RND_SIZE 1000
  uint64_t boards[3];
  create_boards(db, boards);
  std::array<uint64_t, RND_SIZE / 10> users;
  std::array<uint64_t, RND_SIZE> pages, notes;
  auto now = now_s();
  db.open_write_txn([&](WriteTxn txn) {
    spdlog::info("Generating users");
    for (size_t i = 0; i < RND_SIZE / 10; i++) {
      FlatBufferBuilder fbb;
      users[i] = txn.create_user(std::move(fbb), CreateUserDirect(fbb,
        "testuser",
        "Test User",
        "",
        "",
        "",
        {},
        now - random_int(gen, 86400 * 30)
      ));
    }
    txn.commit();
  });
  db.open_write_txn([&](WriteTxn txn) {
    spdlog::info("Generating pages");
    for (size_t i = 0; i < RND_SIZE; i++) {
      auto author = users[random_int(gen, RND_SIZE / 10)];
      auto board = boards[random_int(gen, 3)];
      FlatBufferBuilder fbb;
      pages[i] = txn.create_page(std::move(fbb), CreatePageDirect(fbb,
        author,
        board,
        "Lorem ipsum dolor sit amet",
        now - random_int(gen, 86400 * 30),
        {},
        {},
        nullptr,
        nullptr,
        "https://example.com"
      ));
    }
    txn.commit();
  });
  db.open_write_txn([&](WriteTxn txn) {
    spdlog::info("Generating notes");
    for (size_t i = 0; i < RND_SIZE; i++) {
      auto author = users[random_int(gen, RND_SIZE / 10)],
        parent_ix = random_int(gen, RND_SIZE + i),
        parent = parent_ix >= RND_SIZE ? notes[parent_ix - RND_SIZE] : pages[parent_ix],
        page = parent_ix >= RND_SIZE ? (*txn.get_note(parent))->page() : parent;
      FlatBufferBuilder fbb;
      notes[i] = txn.create_note(std::move(fbb), CreateNoteDirect(fbb,
        author,
        parent,
        page,
        now - random_int(gen, 86400 * 30),
        {},
        {},
        nullptr,
        nullptr,
        "Lorem ipsum dolor sit amet"
      ));
    }
    txn.commit();
  });
  spdlog::info("Generating votes");
  for (size_t i = 0; i < RND_SIZE / 10; i++) {
    auto user = users[i];
    for (size_t ii = 0; ii < RND_SIZE; ii++) {
      switch (random_int(gen, 5)) {
        case 0:
          db.open_write_txn([&](WriteTxn txn) {
            txn.set_vote(user, pages[ii], Downvote);
            txn.commit();
          });
          break;
        case 3:
        case 4:
          db.open_write_txn([&](WriteTxn txn) {
            txn.set_vote(user, pages[ii], Upvote);
            txn.commit();
          });
          break;
        default:
          break;
      }
    }
    for (size_t ii = 0; ii < RND_SIZE; ii++) {
      switch (random_int(gen, 5)) {
        case 0:
          db.open_write_txn([&](WriteTxn txn) {
            txn.set_vote(user, notes[ii], Downvote);
            txn.commit();
          });
          break;
        case 3:
        case 4:
          db.open_write_txn([&](WriteTxn txn) {
            txn.set_vote(user, notes[ii], Upvote);
            txn.commit();
          });
          break;
        default:
          break;
      }
    }
  }
  {
    auto txn = db.open_read_txn();
    size_t total_pages = 0, total_notes = 0;
    for (size_t board_ix = 0; board_ix < 3; board_ix++) {
      size_t top_pages = 0, new_pages = 0, top_notes = 0, new_notes = 0;
      uint64_t board = boards[board_ix];
      int64_t last_karma = std::numeric_limits<int64_t>::max();
      for (auto page_id : txn.list_pages_of_board_top(board)) {
        auto stats = *txn.get_page_stats(page_id);
        REQUIRE(stats->karma() <= last_karma);
        REQUIRE(stats->karma() == (int64_t)stats->upvotes() - (int64_t)stats->downvotes());
        last_karma = stats->karma();
        top_pages++;
      }
      uint64_t last_timestamp = std::numeric_limits<uint64_t>::max();
      for (auto page_id : txn.list_pages_of_board_new(board)) {
        auto page = *txn.get_page(page_id);
        auto stats = *txn.get_page_stats(page_id);
        REQUIRE(page->created_at() == stats->created_at());
        REQUIRE(page->created_at() <= last_timestamp);
        last_timestamp = page->created_at();
        new_pages++;
      }
      last_karma = std::numeric_limits<int64_t>::max();
      for (auto note_id : txn.list_notes_of_board_top(board)) {
        auto stats = *txn.get_note_stats(note_id);
        REQUIRE(stats->karma() <= last_karma);
        REQUIRE(stats->karma() == (int64_t)stats->upvotes() - (int64_t)stats->downvotes());
        last_karma = stats->karma();
        top_notes++;
      }
      last_timestamp = std::numeric_limits<uint64_t>::max();
      for (auto note_id : txn.list_notes_of_board_new(board)) {
        auto note = *txn.get_note(note_id);
        auto stats = *txn.get_note_stats(note_id);
        REQUIRE(note->created_at() == stats->created_at());
        REQUIRE(note->created_at() <= last_timestamp);
        last_timestamp = note->created_at();
        new_notes++;
      }
      auto stats = *txn.get_board_stats(board);
      REQUIRE(stats->page_count() == top_pages);
      REQUIRE(stats->page_count() == new_pages);
      REQUIRE(stats->note_count() == top_notes);
      REQUIRE(stats->note_count() == new_notes);
      total_pages += new_pages;
      total_notes += new_notes;
    }
    REQUIRE(total_pages == RND_SIZE);
    REQUIRE(total_notes == RND_SIZE);
  }
  std::set<uint64_t> del_pages, del_notes;
  std::sample(pages.begin(), pages.end(), std::inserter(del_pages, del_pages.begin()), RND_SIZE / 20, gen);
  std::sample(notes.begin(), notes.end(), std::inserter(del_notes, del_notes.begin()), RND_SIZE / 20, gen);
  db.open_write_txn([&](WriteTxn txn) {
    for (auto page : del_pages) { REQUIRE(txn.delete_page(page) == true); }
    for (auto note : del_notes) txn.delete_note(note);
    txn.commit();
  });
  {
    auto txn = db.open_read_txn();
    size_t total_pages = 0;
    for (size_t board_ix = 0; board_ix < 3; board_ix++) {
      size_t top_pages = 0, new_pages = 0, top_notes = 0, new_notes = 0;
      uint64_t board = boards[board_ix];
      int64_t last_karma = std::numeric_limits<int64_t>::max();
      for (auto page_id : txn.list_pages_of_board_top(board)) {
        auto stats_opt = txn.get_page_stats(page_id);
        REQUIRE(!!stats_opt);
        auto stats = *stats_opt;
        REQUIRE(stats->karma() <= last_karma);
        REQUIRE(stats->karma() == (int64_t)stats->upvotes() - (int64_t)stats->downvotes());
        REQUIRE(!del_pages.contains(page_id));
        last_karma = stats->karma();
        top_pages++;
      }
      uint64_t last_timestamp = std::numeric_limits<uint64_t>::max();
      for (auto page_id : txn.list_pages_of_board_new(board)) {
        auto page_opt = txn.get_page(page_id);
        REQUIRE(!!page_opt);
        auto page = *page_opt;
        auto stats = *txn.get_page_stats(page_id);
        REQUIRE(page->created_at() == stats->created_at());
        REQUIRE(page->created_at() <= last_timestamp);
        REQUIRE(!del_pages.contains(page_id));
        last_timestamp = page->created_at();
        new_pages++;
      }
      last_karma = std::numeric_limits<int64_t>::max();
      for (auto note_id : txn.list_notes_of_board_top(board)) {
        auto stats_opt = txn.get_note_stats(note_id);
        REQUIRE(!!stats_opt);
        auto stats = *stats_opt;
        REQUIRE(stats->karma() <= last_karma);
        REQUIRE(stats->karma() == (int64_t)stats->upvotes() - (int64_t)stats->downvotes());
        REQUIRE(!del_notes.contains(note_id));
        last_karma = stats->karma();
        top_notes++;
      }
      last_timestamp = std::numeric_limits<uint64_t>::max();
      for (auto note_id : txn.list_notes_of_board_new(board)) {
        auto note_opt = txn.get_note(note_id);
        REQUIRE(!!note_opt);
        auto note = *note_opt;
        auto stats = *txn.get_note_stats(note_id);
        REQUIRE(note->created_at() == stats->created_at());
        REQUIRE(note->created_at() <= last_timestamp);
        REQUIRE(!del_notes.contains(note_id));
        REQUIRE(!del_notes.contains(note->parent()));
        REQUIRE(!del_pages.contains(note->page()));
        last_timestamp = note->created_at();
        new_notes++;
      }
      auto stats = *txn.get_board_stats(board);
      REQUIRE(stats->page_count() == top_pages);
      REQUIRE(stats->page_count() == new_pages);
      REQUIRE(stats->note_count() == top_notes);
      REQUIRE(stats->note_count() == new_notes);
      total_pages += new_pages;
    }
    REQUIRE(total_pages == RND_SIZE - (RND_SIZE / 20));
  }
}
