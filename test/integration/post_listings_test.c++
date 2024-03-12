#include "integration_common.h++"

using std::regex, std::regex_iterator;

regex title_regex(R"(This is thread [#](\d+))");

static auto get_next_page(const HtmlDoc& page) -> string {
  const auto links = page.xpath(R"(//a[contains(text(),"Load more")])");
  REQUIRE(links.size() == 1);
  const auto next_page_href = page.attr(links[0], "href");
  REQUIRE_FALSE(next_page_href.empty());
  REQUIRE(next_page_href[0] == '/');
  return next_page_href;
}

static void expect_thread_numbers(const unique_ptr<const HttpClientResponse>& rsp, const vector<string>& numbers) {
  regex_iterator<string_view::iterator>
    posts_begin(rsp->body().begin(), rsp->body().end(), title_regex),
    posts_end;
  vector<string> matches;
  for (auto i = posts_begin; i != posts_end; i++) matches.push_back((*i)[1].str());
  CHECK(matches == numbers);
}

SCENARIO_METHOD(IntegrationTest, "post listings", "[integration][post_listings]") {
  instance->first_run_setup({
    .default_board_name = "main",
    .admin_name = "admin",
    .admin_password = "password"
  });
  spdlog::set_level(spdlog::level::info);
  uint64_t user_id = instance->create_local_user("myuser", {}, "mypassword", false, {}, IsApproved::Yes),
    board_id;
  {
    auto txn = instance->open_read_txn();
    board_id = txn.get_board_id_by_name("main").value();
  }

  GIVEN("30 threads by 10 users in one board, each two hours apart") {
    uint64_t user_ids[10] = {user_id}, thread_ids[30];
    for (size_t i = 1; i < 10; i++) {
      user_ids[i] = instance->create_local_user(fmt::format("user{:d}", i), {}, "mypassword", false, {}, IsApproved::Yes);
    }
    const auto start_time = now_t() - 60h;
    for (size_t i = 0; i < 30; i++) {
      thread_ids[i] = instance->create_thread(
        user_ids[i % 10],
        board_id,
        {},
        {},
        start_time + 2h * i,
        {},
        fmt::format("This is thread #{:d}", i),
        {},
        "This is `some` _sample_ [text](http://link.test)."
      );
    }
    spdlog::set_level(spdlog::level::debug);

    WHEN("a user views the board with the New sort order") {
      auto rsp = http.get(base_url + "/b/main?sort=New").dispatch_and_wait();
      CHECK(rsp->error().value_or("") == "");

      THEN("the last 20 posts are displayed in order") {
        expect_thread_numbers(rsp, {
          "29", "28", "27", "26", "25", "24", "23", "22", "21", "20",
          "19", "18", "17", "16", "15", "14", "13", "12", "11", "10"
        });
      }

      AND_WHEN("the board displays a Load More link") {
        const auto next_page_href = get_next_page(html(rsp));

        AND_WHEN("a user views the next page via this link") {
          auto rsp = http.get(base_url + next_page_href).dispatch_and_wait();
          CHECK(rsp->error().value_or("") == "");

          THEN("the remaining 10 posts are displayed in order") {
            expect_thread_numbers(rsp, {
              "9", "8", "7", "6", "5", "4", "3", "2", "1", "0"
            });
          }
        }
      }
    }

    WHEN("a user views the board with the Old sort order") {
      auto rsp = http.get(base_url + "/b/main?sort=Old").dispatch_and_wait();
      CHECK(rsp->error().value_or("") == "");

      THEN("the first 20 posts are displayed in order") {
        expect_thread_numbers(rsp, {
          "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
          "10", "11", "12", "13", "14", "15", "16", "17", "18", "19"
        });
      }

      AND_WHEN("the board displays a Load More link") {
        const auto next_page_href = get_next_page(html(rsp));

        AND_WHEN("a user views the next page via this link") {
          auto rsp = http.get(base_url + next_page_href).dispatch_and_wait();
          CHECK(rsp->error().value_or("") == "");

          THEN("the remaining 10 posts are displayed in order") {
            expect_thread_numbers(rsp, {
              "20", "21", "22", "23", "24", "25", "26", "27", "28", "29"
            });
          }
        }
      }
    }

    AND_GIVEN("10 upvotes on the second-newest thread") {
      for (size_t i = 1; i < 10; i++) {
        instance->vote(user_ids[i], thread_ids[28], Vote::Upvote);
      }

      WHEN("a user views the board with the Active sort order") {
        auto rsp = http.get(base_url + "/b/main?sort=Active").dispatch_and_wait();
        CHECK(rsp->error().value_or("") == "");

        THEN("the last 20 posts are displayed in order, with the upvoted post first") {
          expect_thread_numbers(rsp, {
            "28", "29", "27", "26", "25", "24", "23", "22", "21", "20",
            "19", "18", "17", "16", "15", "14", "13", "12", "11", "10"
          });
        }

        AND_WHEN("the board displays a Load More link") {
          const auto next_page_href = get_next_page(html(rsp));

          AND_WHEN("a user views the next page via this link") {
            auto rsp = http.get(base_url + next_page_href).dispatch_and_wait();
            CHECK(rsp->error().value_or("") == "");

            THEN("the remaining 10 posts are displayed in order") {
              expect_thread_numbers(rsp, {
                "9", "8", "7", "6", "5", "4", "3", "2", "1", "0"
              });
            }
          }
        }
      }

      WHEN("a user views the board with the Hot sort order") {
        auto rsp = http.get(base_url + "/b/main?sort=Hot").dispatch_and_wait();
        CHECK(rsp->error().value_or("") == "");

        THEN("the last 20 posts are displayed in order, with the upvoted post first") {
          expect_thread_numbers(rsp, {
            "28", "29", "27", "26", "25", "24", "23", "22", "21", "20",
            "19", "18", "17", "16", "15", "14", "13", "12", "11", "10"
          });
        }

        AND_WHEN("the board displays a Load More link") {
          const auto next_page_href = get_next_page(html(rsp));

          AND_WHEN("a user views the next page via this link") {
            auto rsp = http.get(base_url + next_page_href).dispatch_and_wait();
            CHECK(rsp->error().value_or("") == "");

            THEN("the remaining 10 posts are displayed in order") {
              expect_thread_numbers(rsp, {
                "9", "8", "7", "6", "5", "4", "3", "2", "1", "0"
              });
            }
          }
        }
      }
    }
  }
}
