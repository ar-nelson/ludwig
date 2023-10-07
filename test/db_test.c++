#include "util.h++"
#include "services/db.h++"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <random>

using namespace Ludwig;
using namespace flatbuffers;

TEST_CASE("create DB", "[db]") {
  TempFile file;
  DB db(file.name);
}

TEST_CASE("create and get user", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t id;
  {
    FlatBufferBuilder fbb;
    fbb.Finish(CreateUserDirect(fbb,
      "testuser",
      "Test User",
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      {},
      now_s()
    ));
    auto txn = db.open_write_txn();
    id = txn.create_user(fbb);
    txn.commit();
  }
  {
    auto txn = db.open_read_txn();
    auto user = txn.get_user(id);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "testuser"sv);
    REQUIRE(user->get().display_name()->string_view() == "Test User"sv);
  }
}

static inline auto create_users(DB& db, uint64_t ids[3]) {
  auto txn = db.open_write_txn();
  FlatBufferBuilder fbb;
  fbb.Finish(CreateUserDirect(fbb,
    "user1",
    "User 1",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[0] = txn.create_user(fbb);
  fbb.Clear();
  fbb.Finish(CreateUserDirect(fbb,
    "user2",
    "User 2",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[1] = txn.create_user(fbb);
  fbb.Clear();
  fbb.Finish(CreateUserDirect(fbb,
    "user3",
    "User 3",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[2] = txn.create_user(fbb);
  txn.commit();
}

static inline auto create_boards(DB& db, uint64_t ids[3]) {
  auto txn = db.open_write_txn();
  FlatBufferBuilder fbb;
  fbb.Finish(CreateBoardDirect(fbb,
    "lions",
    "Lions",
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[0] = txn.create_board(fbb);
  fbb.Clear();
  fbb.Finish(CreateBoardDirect(fbb,
    "tigers",
    "Tigers",
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[1] = txn.create_board(fbb);
  fbb.Clear();
  fbb.Finish(CreateBoardDirect(fbb,
    "bears",
    "Bears",
    nullptr,
    nullptr,
    {},
    now_s()
  ));
  ids[2] = txn.create_board(fbb);
  txn.commit();
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
    REQUIRE(user->get().name()->string_view() == "user1"sv);
    REQUIRE(user->get().display_name()->string_view() == "User 1"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "user2"sv);
    REQUIRE(user->get().display_name()->string_view() == "User 2"sv);
    ++iter;
    REQUIRE(!iter.is_done());
    user = txn.get_user(*iter);
    REQUIRE(!!user);
    REQUIRE(user->get().name()->string_view() == "user3"sv);
    REQUIRE(user->get().display_name()->string_view() == "User 3"sv);
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
  {
    auto txn = db.open_write_txn();
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
    auto txn = db.open_write_txn();
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
  fbb.Finish(CreateThreadDirect(fbb,
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
  return txn.create_thread(fbb);
}

TEST_CASE("create and list posts", "[db]") {
  TempFile file;
  DB db(file.name);
  uint64_t user_ids[3], board_ids[3], thread_ids[12];
  create_users(db, user_ids);
  create_boards(db, board_ids);
  {
    auto txn = db.open_write_txn();
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
    size_t i = 0;
    for (uint64_t id : txn.list_threads_of_user_new(user_ids[0])) {
      REQUIRE(i < 6);
      REQUIRE(id == thread_ids[5 - i]);
      i++;
    }
    REQUIRE(i == 6);
    i = 0;
    for (uint64_t id : txn.list_threads_of_user_new(user_ids[1])) {
      REQUIRE(i < 4);
      REQUIRE(id == thread_ids[9 - i]);
      i++;
    }
    REQUIRE(i == 4);
    i = 0;
    for (uint64_t id : txn.list_threads_of_user_new(user_ids[2])) {
      REQUIRE(i < 2);
      REQUIRE(id == thread_ids[11 - i]);
      i++;
    }

    static const size_t board_threads[] = {
      // board_ids[0]
      7, 6, 3, 2, 1, 0,
      // board_ids[1]
      10, 5, 4,
      // board_ids[2]
      11, 9, 8
    };

    i = 0;
    for (uint64_t id : txn.list_threads_of_board_new(board_ids[0])) {
      REQUIRE(i < 6);
      REQUIRE(id == thread_ids[board_threads[i]]);
      i++;
    }
    REQUIRE(i == 6);
    for (uint64_t id : txn.list_threads_of_board_new(board_ids[1])) {
      REQUIRE(i < 9);
      REQUIRE(id == thread_ids[board_threads[i]]);
      i++;
    }
    REQUIRE(i == 9);
    for (uint64_t id : txn.list_threads_of_board_new(board_ids[2])) {
      REQUIRE(i < 12);
      REQUIRE(id == thread_ids[board_threads[i]]);
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
  std::array<uint64_t, RND_SIZE> threads, comments;
  FlatBufferBuilder fbb;
  auto now = now_s();
  {
    auto txn = db.open_write_txn();
    for (size_t i = 0; i < RND_SIZE / 10; i++) {
      fbb.Clear();
      fbb.Finish(CreateUserDirect(fbb,
        "testuser",
        "Test User",
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {},
        now - random_int(gen, 86400 * 30)
      ));
      users[i] = txn.create_user(fbb);
    }
    txn.commit();
  }
  {
    auto txn = db.open_write_txn();
    for (size_t i = 0; i < RND_SIZE; i++) {
      auto author = users[random_int(gen, RND_SIZE / 10)];
      auto board = boards[random_int(gen, 3)];
      fbb.Clear();
      fbb.Finish(CreateThreadDirect(fbb,
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
      threads[i] = txn.create_thread(fbb);
    }
    txn.commit();
  }
  {
    auto txn = db.open_write_txn();
    for (size_t i = 0; i < RND_SIZE; i++) {
      auto author = users[random_int(gen, RND_SIZE / 10)],
        parent_ix = random_int(gen, RND_SIZE + i),
        parent = parent_ix >= RND_SIZE ? comments[parent_ix - RND_SIZE] : threads[parent_ix],
        thread = parent_ix >= RND_SIZE ? txn.get_comment(parent)->get().thread() : parent;
      FlatBufferBuilder fbb;
      fbb.Finish(CreateCommentDirect(fbb,
        author,
        parent,
        thread,
        now - random_int(gen, 86400 * 30),
        {},
        {},
        nullptr,
        nullptr,
        nullptr,
        "Lorem ipsum dolor sit amet"
      ));
      comments[i] = txn.create_comment(fbb);
    }
    txn.commit();
  }
  {
    for (size_t i = 0; i < RND_SIZE / 10; i++) {
      auto user = users[i];
      for (size_t ii = 0; ii < RND_SIZE; ii++) {
        switch (random_int(gen, 5)) {
          case 0: {
            auto txn = db.open_write_txn();
            txn.set_vote(user, threads[ii], Vote::Downvote);
            txn.commit();
            break;
          }
          case 3:
          case 4: {
            auto txn = db.open_write_txn();
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
            auto txn = db.open_write_txn();
            txn.set_vote(user, comments[ii], Vote::Downvote);
            txn.commit();
            break;
          }
          case 3:
          case 4: {
            auto txn = db.open_write_txn();
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
  std::set<uint64_t> del_threads, del_comments;
  std::sample(threads.begin(), threads.end(), std::inserter(del_threads, del_threads.begin()), RND_SIZE / 20, gen);
  std::sample(comments.begin(), comments.end(), std::inserter(del_comments, del_comments.begin()), RND_SIZE / 20, gen);
  {
    auto txn = db.open_write_txn();
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
}

