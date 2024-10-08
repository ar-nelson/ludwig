#include "test_common.h++"
#include "services/db.h++"
#include "util/rich_text.h++"
#include <algorithm>
#include <random>

using namespace flatbuffers;

static inline auto create_user(WriteTxn& txn, string_view name, string_view display_name, uint64_t now = now_s()) -> uint64_t {
  FlatBufferBuilder fbb;
  const auto name_s = fbb.CreateString(name);
  const auto [display_name_types, display_name_values] =
    plain_text_to_rich_text(fbb, display_name);
  UserBuilder user(fbb);
  user.add_created_at(now);
  user.add_name(name_s);
  user.add_display_name_type(display_name_types);
  user.add_display_name(display_name_values);
  user.add_salt(0);
  fbb.Finish(user.Finish());
  return txn.create_user(fbb.GetBufferSpan());
}

static inline auto create_board(WriteTxn& txn, string_view name, string_view display_name) -> uint64_t {
  FlatBufferBuilder fbb;
  const auto name_s = fbb.CreateString(name);
  const auto [display_name_types, display_name_values] =
    plain_text_to_rich_text(fbb, display_name);
  BoardBuilder board(fbb);
  board.add_created_at(now_s());
  board.add_name(name_s);
  board.add_display_name_type(display_name_types);
  board.add_display_name(display_name_values);
  fbb.Finish(board.Finish());
  return txn.create_board(fbb.GetBufferSpan());
}

TEST_CASE("create DB", "[db]") {
  TempFile file;
  DB db(file.name, 100, true);
}

TEST_CASE("create and get user", "[db]") {
  TempFile file;
  DB db(file.name, 100, true);
  uint64_t id;
  {
    auto txn = db.open_write_txn_sync();
    id = create_user(txn, "testuser", "Test User");
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto user = txn.get_user(id);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "testuser"sv);
    REQUIRE(user->get().display_name()->GetAsString(0)->string_view() == "Test User"sv);
  }
}

TEST_CASE("priority ordering of async write transactions", "[db][write_txn_async]") {
  TempFile file;
  DB db(file.name, 100, true);
  uint64_t id1;
  db.open_write_txn_async([&](auto txn, bool async) {
    CHECK(async == false);
    db.open_write_txn_async([&](auto txn, bool async) {
      CHECK(async == true);
      CHECK(create_user(txn, "user3", "User 3") == id1 + 2);
      txn.commit();
    }, WritePriority::Low);
    db.open_write_txn_async([&](auto txn, bool async) {
      CHECK(async == true);
      CHECK(create_user(txn, "user4", "User 4") == id1 + 3);
      txn.commit();
    }, WritePriority::Low);
    db.open_write_txn_async([&](auto txn, bool async) {
      CHECK(async == true);
      CHECK(create_user(txn, "user5", "User 5") == id1 + 4);
      txn.commit();
    }, WritePriority::Low);
    db.open_write_txn_async([&](auto txn, bool async) {
      CHECK(async == true);
      CHECK(create_user(txn, "user2", "User 2") == id1 + 1);
      txn.commit();
    }, WritePriority::High);

    id1 = create_user(txn, "user1", "User 1");
    txn.commit();
  });

  auto txn = db.open_read_txn();
  for (uint8_t i = 0; i < 5; i++) {
    auto user = txn.get_user(id1 + i);
    REQUIRE(!!user);
    CHECK(user->get().name()->str() == fmt::format("user{:d}", i + 1));
  }
}

static inline auto create_users(DB& db, uint64_t ids[3]) {
  auto txn = db.open_write_txn_sync();
  FlatBufferBuilder fbb;
  ids[0] = create_user(txn, "user1", "User 1");
  ids[1] = create_user(txn, "user2", "User 2");
  ids[2] = create_user(txn, "user3", "User 3");
  txn.commit();
}

static inline auto create_boards(DB& db, uint64_t ids[3]) {
  auto txn = db.open_write_txn_sync();
  ids[0] = create_board(txn, "lions", "Lions");
  ids[1] = create_board(txn, "tigers", "Tigers");
  ids[2] = create_board(txn, "bears", "Bears");
  txn.commit();
}

TEST_CASE("create and list users", "[db]") {
  TempFile file;
  DB db(file.name, 100, true);
  uint64_t user_ids[3];
  create_users(db, user_ids);
  {
    auto txn = db.open_read_txn();
    auto iter = txn.list_users_new();
    REQUIRE(!iter.is_done());
    auto user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "user3"sv);
    REQUIRE(user->get().display_name()->GetAsString(0)->string_view() == "User 3"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "user2"sv);
    REQUIRE(user->get().display_name()->GetAsString(0)->string_view() == "User 2"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "user1"sv);
    REQUIRE(user->get().display_name()->GetAsString(0)->string_view() == "User 1"sv);
    ++iter;
    REQUIRE(iter.is_done());
  }
}

TEST_CASE("create users and boards, subscribe and unsubscribe", "[db]") {
  TempFile file;
  DB db(file.name, 100, true);
  uint64_t user_ids[3], board_ids[3];
  create_users(db, user_ids);
  create_boards(db, board_ids);
  {
    auto txn = db.open_write_txn_sync();
    txn.set_subscription(user_ids[0], board_ids[0], true);
    txn.set_subscription(user_ids[1], board_ids[0], true);
    txn.set_subscription(user_ids[2], board_ids[0], true);
    txn.set_subscription(user_ids[0], board_ids[1], true);
    txn.set_subscription(user_ids[1], board_ids[1], true);
    txn.set_subscription(user_ids[0], board_ids[2], true);
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto &stats0 = txn.get_board_stats(board_ids[0])->get(),
         &stats1 = txn.get_board_stats(board_ids[1])->get(),
         &stats2 = txn.get_board_stats(board_ids[2])->get();
    REQUIRE(stats0.subscriber_count() == 3);
    REQUIRE(stats1.subscriber_count() == 2);
    REQUIRE(stats2.subscriber_count() == 1);
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[0], board_ids[0]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[1], board_ids[0]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[2], board_ids[0]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[0], board_ids[1]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[1], board_ids[1]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[2], board_ids[1]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[0], board_ids[2]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[1], board_ids[2]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[2], board_ids[2]));
  }
  {
    auto txn = db.open_write_txn_sync();
    txn.set_subscription(user_ids[0], board_ids[0], false);
    txn.set_subscription(user_ids[0], board_ids[1], false);
    txn.set_subscription(user_ids[0], board_ids[2], false);
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto &stats0 = txn.get_board_stats(board_ids[0])->get(),
         &stats1 = txn.get_board_stats(board_ids[1])->get(),
         &stats2 = txn.get_board_stats(board_ids[2])->get();
    REQUIRE(stats0.subscriber_count() == 2);
    REQUIRE(stats1.subscriber_count() == 1);
    REQUIRE(stats2.subscriber_count() == 0);
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[0], board_ids[0]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[1], board_ids[0]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[2], board_ids[0]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[0], board_ids[1]));
    REQUIRE(txn.is_user_subscribed_to_board(user_ids[1], board_ids[1]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[2], board_ids[1]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[0], board_ids[2]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[1], board_ids[2]));
    REQUIRE(!txn.is_user_subscribed_to_board(user_ids[2], board_ids[2]));
  }
}

static inline auto create_thread(WriteTxn& txn, uint64_t user, uint64_t board, const char* title, const char* url) -> uint64_t {
  FlatBufferBuilder fbb;
  const vector title_type{RichText::Text};
  const vector title_vec{fbb.CreateString(title).Union()};
  fbb.Finish(CreateThreadDirect(fbb,
    user,
    board,
    &title_type,
    &title_vec,
    now_s(),
    {},
    {},
    {},
    0,
    0,
    nullptr,
    nullptr,
    url
  ));
  return txn.create_thread(fbb.GetBufferSpan());
}

TEST_CASE("create and list posts", "[db]") {
  TempFile file;
  DB db(file.name, 100, true);
  uint64_t user_ids[3], board_ids[3], thread_ids[12];
  create_users(db, user_ids);
  create_boards(db, board_ids);
  {
    auto txn = db.open_write_txn_sync();
    thread_ids[0] = create_thread(txn, user_ids[0], board_ids[0], "post 1", "http://example.com");
    thread_ids[1] = create_thread(txn, user_ids[0], board_ids[0], "post 2", "http://example.com");
    thread_ids[2] = create_thread(txn, user_ids[0], board_ids[0], "post 3", "http://example.com");
    thread_ids[3] = create_thread(txn, user_ids[0], board_ids[0], "post 4", "http://example.com");
    thread_ids[4] = create_thread(txn, user_ids[0], board_ids[1], "post 5", "http://example.com");
    thread_ids[5] = create_thread(txn, user_ids[0], board_ids[1], "post 6", "http://example.com");
    thread_ids[6] = create_thread(txn, user_ids[1], board_ids[0], "post 7", "http://example.com");
    thread_ids[7] = create_thread(txn, user_ids[1], board_ids[0], "post 8", "http://example.com");
    thread_ids[8] = create_thread(txn, user_ids[1], board_ids[2], "post 9", "http://example.com");
    thread_ids[9] = create_thread(txn, user_ids[1], board_ids[2], "post 10", "http://example.com");
    thread_ids[10] = create_thread(txn, user_ids[2], board_ids[1], "post 11", "http://example.com");
    thread_ids[11] = create_thread(txn, user_ids[2], board_ids[2], "post 12", "http://example.com");
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    REQUIRE(txn.get_user_stats(user_ids[0])->get().thread_count() == 6);
    REQUIRE(txn.get_user_stats(user_ids[1])->get().thread_count() == 4);
    REQUIRE(txn.get_user_stats(user_ids[2])->get().thread_count() == 2);
    REQUIRE(txn.get_board_stats(board_ids[0])->get().thread_count() == 6);
    REQUIRE(txn.get_board_stats(board_ids[1])->get().thread_count() == 3);
    REQUIRE(txn.get_board_stats(board_ids[2])->get().thread_count() == 3);
    vector<uint64_t> xs;
    for (auto id : txn.list_threads_of_user_new(user_ids[0])) xs.push_back(id);
    REQUIRE(xs == vector{thread_ids[5], thread_ids[4], thread_ids[3], thread_ids[2], thread_ids[1], thread_ids[0]});
    xs.clear();
    for (auto id : txn.list_threads_of_user_new(user_ids[1])) xs.push_back(id);
    REQUIRE(xs == vector{thread_ids[9], thread_ids[8], thread_ids[7], thread_ids[6]});
    xs.clear();
    for (auto id : txn.list_threads_of_user_new(user_ids[2])) xs.push_back(id);
    REQUIRE(xs == vector{thread_ids[11], thread_ids[10]});

    static const size_t board_threads[] = {
      // board_ids[0]
      7, 6, 3, 2, 1, 0,
      // board_ids[1]
      10, 5, 4,
      // board_ids[2]
      11, 9, 8
    };

    xs.clear();
    for (auto id : txn.list_threads_of_board_new(board_ids[0])) xs.push_back(id);
    REQUIRE(xs == vector{
      thread_ids[board_threads[0]], thread_ids[board_threads[1]], thread_ids[board_threads[2]],
      thread_ids[board_threads[3]], thread_ids[board_threads[4]], thread_ids[board_threads[5]]
    });
    xs.clear();
    for (auto id : txn.list_threads_of_board_new(board_ids[1])) xs.push_back(id);
    REQUIRE(xs == vector{
      thread_ids[board_threads[6]], thread_ids[board_threads[7]], thread_ids[board_threads[8]],
    });
    xs.clear();
    for (auto id : txn.list_threads_of_board_new(board_ids[2])) xs.push_back(id);
    REQUIRE(xs == vector{
      thread_ids[board_threads[9]], thread_ids[board_threads[10]], thread_ids[board_threads[11]],
    });
  }
}

static inline auto random_int(std::mt19937& gen, uint64_t n) -> uint64_t {
  std::uniform_int_distribution<uint64_t> dist(0, n - 1);
  return dist(gen);
}

TEST_CASE("generate and delete random posts and check stats", "[db]") {
  spdlog::set_level(spdlog::level::info);
  TempFile file;
  DB db(file.name, 100, true);
  std::random_device rd;
  std::mt19937 gen(rd());
# define RND_SIZE 1000
  uint64_t boards[3];
  create_boards(db, boards);
  std::array<uint64_t, RND_SIZE / 10> users;
  std::array<uint64_t, RND_SIZE> threads, comments;
  auto now = now_s();
  {
    auto txn = db.open_write_txn_sync();
    for (size_t i = 0; i < RND_SIZE / 10; i++) {
      users[i] = create_user(txn, fmt::format("testuser{}", i), "Test User", now - random_int(gen, 86400 * 30));
    }
    txn.commit();
  }
  FlatBufferBuilder fbb;
  {
    auto txn = db.open_write_txn_sync();
    for (size_t i = 0; i < RND_SIZE; i++) {
      const auto author = users[random_int(gen, RND_SIZE / 10)];
      const auto board = boards[random_int(gen, 3)];
      fbb.Clear();
      const vector title_type{RichText::Text};
      const vector title{fbb.CreateString("Lorem ipsum dolor sit amet").Union()};
      fbb.Finish(CreateThreadDirect(fbb,
        author,
        board,
        &title_type,
        &title,
        now - random_int(gen, 86400 * 30),
        {},
        {},
        {},
        0,
        0,
        nullptr,
        nullptr,
        "https://example.com"
      ));
      threads[i] = txn.create_thread(fbb.GetBufferSpan());
    }
    txn.commit();
  }
  {
    auto txn = db.open_write_txn_sync();
    for (size_t i = 0; i < RND_SIZE; i++) {
      fbb.Clear();
      const auto author = users[random_int(gen, RND_SIZE / 10)],
        parent_ix = random_int(gen, RND_SIZE + i),
        parent = parent_ix >= RND_SIZE ? comments[parent_ix - RND_SIZE] : threads[parent_ix],
        thread = parent_ix >= RND_SIZE ? txn.get_comment(parent)->get().thread() : parent;
      const auto content_raw = fbb.CreateSharedString("Lorem ipsum dolor sit amet");
      const auto [content_type, content] = plain_text_to_rich_text(fbb, "Lorem ipsum dolor sit amet");
      CommentBuilder comment(fbb);
      comment.add_author(author);
      comment.add_parent(parent);
      comment.add_thread(thread);
      comment.add_created_at(now - random_int(gen, 86400 * 30));
      comment.add_content_raw(content_raw);
      comment.add_content_type(content_type);
      comment.add_content(content);
      comment.add_salt(0);
      fbb.Finish(comment.Finish());
      comments[i] = txn.create_comment(fbb.GetBufferSpan());
    }
    txn.commit();
  }
  {
    for (size_t i = 0; i < RND_SIZE / 10; i++) {
      auto user = users[i];
      for (size_t ii = 0; ii < RND_SIZE; ii++) {
        switch (random_int(gen, 5)) {
          case 0: {
            auto txn = db.open_write_txn_sync();
            txn.set_vote(user, threads[ii], Vote::Downvote);
            txn.commit();
            break;
          }
          case 3:
          case 4: {
            auto txn = db.open_write_txn_sync();
            txn.set_vote(user, threads[ii], Vote::Upvote);
            txn.commit();
            break;
          }
          default:
            break;
        }
      }
      for (size_t ii = 0; ii < RND_SIZE; ii++) {
        switch (random_int(gen, 5)) {
          case 0: {
            auto txn = db.open_write_txn_sync();
            txn.set_vote(user, comments[ii], Vote::Downvote);
            txn.commit();
            break;
          }
          case 3:
          case 4: {
            auto txn = db.open_write_txn_sync();
            txn.set_vote(user, comments[ii], Vote::Upvote);
            txn.commit();
            break;
          }
          default:
            break;
        }
      }
    }
  }
  {
    auto txn = db.open_read_txn();
    size_t total_threads = 0, total_comments = 0;
    for (size_t board_ix = 0; board_ix < 3; board_ix++) {
      size_t top_threads = 0, new_threads = 0, top_comments = 0, new_comments = 0;
      uint64_t board = boards[board_ix];
      int64_t last_karma = std::numeric_limits<int64_t>::max();
      for (auto thread_id : txn.list_threads_of_board_top(board)) {
        auto& stats = txn.get_post_stats(thread_id)->get();
        REQUIRE(stats.karma() <= last_karma);
        REQUIRE(stats.karma() == (int64_t)stats.upvotes() - (int64_t)stats.downvotes());
        last_karma = stats.karma();
        top_threads++;
      }
      uint64_t last_timestamp = ID_MAX;
      for (auto thread_id : txn.list_threads_of_board_new(board)) {
        auto& thread = txn.get_thread(thread_id)->get();
        REQUIRE(thread.created_at() <= last_timestamp);
        last_timestamp = thread.created_at();
        new_threads++;
      }
      last_karma = std::numeric_limits<int64_t>::max();
      for (auto comment_id : txn.list_comments_of_board_top(board)) {
        auto& stats = txn.get_post_stats(comment_id)->get();
        REQUIRE(stats.karma() <= last_karma);
        REQUIRE(stats.karma() == (int64_t)stats.upvotes() - (int64_t)stats.downvotes());
        last_karma = stats.karma();
        top_comments++;
      }
      last_timestamp = ID_MAX;
      for (auto comment_id : txn.list_comments_of_board_new(board)) {
        auto& comment = txn.get_comment(comment_id)->get();
        REQUIRE(comment.created_at() <= last_timestamp);
        last_timestamp = comment.created_at();
        new_comments++;
      }
      auto& stats = txn.get_board_stats(board)->get();
      REQUIRE(stats.thread_count() == top_threads);
      REQUIRE(stats.thread_count() == new_threads);
      REQUIRE(stats.comment_count() == top_comments);
      REQUIRE(stats.comment_count() == new_comments);
      total_threads += new_threads;
      total_comments += new_comments;
    }
    REQUIRE(total_threads == RND_SIZE);
    REQUIRE(total_comments == RND_SIZE);
  }
  phmap::flat_hash_set<uint64_t> del_threads, del_comments;
  std::sample(threads.begin(), threads.end(), std::inserter(del_threads, del_threads.begin()), RND_SIZE / 20, gen);
  std::sample(comments.begin(), comments.end(), std::inserter(del_comments, del_comments.begin()), RND_SIZE / 20, gen);
  {
    auto txn = db.open_write_txn_sync();
    for (auto thread : del_threads) { REQUIRE(txn.delete_thread(thread) == true); }
    for (auto comment : del_comments) txn.delete_comment(comment);
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    size_t total_threads = 0;
    for (size_t board_ix = 0; board_ix < 3; board_ix++) {
      size_t top_threads = 0, new_threads = 0, top_comments = 0, new_comments = 0;
      uint64_t board = boards[board_ix];
      int64_t last_karma = std::numeric_limits<int64_t>::max();
      for (auto thread_id : txn.list_threads_of_board_top(board)) {
        auto stats_opt = txn.get_post_stats(thread_id);
        REQUIRE(!!stats_opt);
        auto& stats = stats_opt->get();
        REQUIRE(stats.karma() <= last_karma);
        REQUIRE(stats.karma() == (int64_t)stats.upvotes() - (int64_t)stats.downvotes());
        REQUIRE(!del_threads.contains(thread_id));
        last_karma = stats.karma();
        top_threads++;
      }
      uint64_t last_timestamp = ID_MAX;
      for (auto thread_id : txn.list_threads_of_board_new(board)) {
        auto thread_opt = txn.get_thread(thread_id);
        REQUIRE(!!thread_opt);
        auto& thread = thread_opt->get();
        REQUIRE(thread.created_at() <= last_timestamp);
        REQUIRE(!del_threads.contains(thread_id));
        last_timestamp = thread.created_at();
        new_threads++;
      }
      last_karma = std::numeric_limits<int64_t>::max();
      for (auto comment_id : txn.list_comments_of_board_top(board)) {
        auto stats_opt = txn.get_post_stats(comment_id);
        REQUIRE(!!stats_opt);
        auto& stats = stats_opt->get();
        REQUIRE(stats.karma() <= last_karma);
        REQUIRE(stats.karma() == (int64_t)stats.upvotes() - (int64_t)stats.downvotes());
        REQUIRE(!del_comments.contains(comment_id));
        last_karma = stats.karma();
        top_comments++;
      }
      last_timestamp = ID_MAX;
      for (auto comment_id : txn.list_comments_of_board_new(board)) {
        auto comment_opt = txn.get_comment(comment_id);
        REQUIRE(!!comment_opt);
        auto& comment = comment_opt->get();
        REQUIRE(comment.created_at() <= last_timestamp);
        REQUIRE(!del_comments.contains(comment_id));
        REQUIRE(!del_comments.contains(comment.parent()));
        REQUIRE(!del_threads.contains(comment.thread()));
        last_timestamp = comment.created_at();
        new_comments++;
      }
      auto& stats = txn.get_board_stats(board)->get();
      REQUIRE(stats.thread_count() == top_threads);
      REQUIRE(stats.thread_count() == new_threads);
      REQUIRE(stats.comment_count() == top_comments);
      REQUIRE(stats.comment_count() == new_comments);
      total_threads += new_threads;
    }
    REQUIRE(total_threads == RND_SIZE- (RND_SIZE / 20));
  }
  spdlog::set_level(spdlog::level::debug);
}