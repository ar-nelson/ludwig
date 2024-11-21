#include "integration_common.h++"

#define FIRST_RUN_SETUP_FORM \
  "name=MyServer" \
  "&base_url=http://myserver.test" \
  "&home_page_type=Subscribed" \
  "&voting=2" \
  "&cws_enabled=true" \
  "&not_board_creation_admin_only=true" \
  "&registation_enabled=true" \
  "&registation_application_required=true" \
  "&application_question=Who are you" \
  "&post_max_length=100000" \
  "&javascript_enabled=true" \
  "&infinite_scroll_enabled=true"

SCENARIO_METHOD(IntegrationTest, "first-run setup", "[integration][first_run]") {

  GIVEN("a fresh database with no users or configuration") {

    WHEN("user visits the home page") {
      auto rsp = http.get(base_url).dispatch_and_wait();

      THEN("user is redirected to the login page") {
        CHECK(rsp->error().value_or("") == "");
        auto login_page = html(rsp);
        CHECK_FALSE(login_page.xpath_exists(R"(//ol[@class="thread-list"])"));
        CHECK(login_page.xpath_exists(R"(//form[@action="/login"])"));
      }
    }

    AND_WHEN("a first-run setup form is submitted without logging in") {
      auto setup = http.post(base_url + "/site_admin/first_run_setup")
        .body(TYPE_FORM, FIRST_RUN_SETUP_FORM)
        .dispatch_and_wait();

      THEN("the request fails and first-run setup is still incomplete") {
        CHECK(setup->status() == 401);
        auto rsp = http.get(base_url).dispatch_and_wait();
        CHECK(rsp->error().value_or("") == "");
        auto login_page = html(rsp);
        CHECK_FALSE(login_page.xpath_exists(R"(//ol[@class="thread-list"])"));
        CHECK(login_page.xpath_exists(R"(//form[@action="/login"])"));
      }
    }

    WHEN("user logs in to the web interface with an incorrect password") {
      auto login = http.post(base_url + "/login").body(TYPE_FORM,
        fmt::format("actual_username={}&password=asdfasdf", FIRST_RUN_ADMIN_USERNAME)
      ).dispatch_and_wait();

      THEN("the login fails") { CHECK(login->status() == 400); }
    }

    WHEN("user logs in to the web interface with an incorrect username") {
      auto login = http.post(base_url + "/login").body(TYPE_FORM,
        fmt::format("actual_username=asdfasdf&password={}", first_run_admin_password)
      ).dispatch_and_wait();

      THEN("the login fails") { CHECK(login->status() == 400); }
    }

    WHEN("user logs in to the web interface as the temporary admin user") {
      auto login = http.post(base_url + "/login").body(TYPE_FORM,
        fmt::format("actual_username={}&password={}", FIRST_RUN_ADMIN_USERNAME, first_run_admin_password)
      ).dispatch_and_wait();
      CHECK(login->status() == 303);
      CHECK(login->error().value_or("") == "");
      CHECK(login->header("location") == "/");
      const auto cookie = get_login_cookie(login);

      AND_WHEN("user visits the home page") {
        auto rsp = http.get(base_url).header("cookie", cookie).dispatch_and_wait();

        THEN("the home page is the first-run setup form") {
          CHECK(rsp->error().value_or("") == "");
          auto page = html(rsp);
          CHECK(page.xpath_exists(R"(//form[@action="/site_admin/first_run_setup"])"));
          CHECK_FALSE(page.xpath_exists(R"(//form[@action="/login"])"));
        }
      }

      AND_WHEN("user visits a different settings page") {
        auto rsp = http.get(base_url + "/settings").header("cookie", cookie).dispatch_and_wait();

        THEN("the page is not accessible") { CHECK(rsp->status() == 403); }
      }

      AND_WHEN("a first-run setup form is submitted with an invalid base_url field") {
        auto setup = http.post(base_url + "/site_admin/first_run_setup")
          .header("cookie", cookie)
          .body(TYPE_FORM,
            "name=MyServer"
            "&base_url=asdfasdf"
            "&home_page_type=Subscribed"
            "&voting=2"
            "&cws_enabled=true"
            "&not_board_creation_admin_only=true"
            "&registation_enabled=true"
            "&registation_application_required=true"
            "&application_question=Who are you"
            "&post_max_length=100000"
            "&javascript_enabled=true"
            "&infinite_scroll_enabled=true"
          ).dispatch_and_wait();

        THEN("the request fails") { CHECK(setup->status() == 400); }
      }

      AND_WHEN("a valid first-run setup form is submitted") {
        auto setup = http.post(base_url + "/site_admin/first_run_setup")
          .header("cookie", cookie)
          .body(TYPE_FORM, FIRST_RUN_SETUP_FORM
            "&admin_username=myadmin"
            "&admin_password=mypassword"
            "&default_board_name=myboard"
          ).dispatch_and_wait();
        CHECK(setup->error().value_or("") == "");

        AND_WHEN("user visits the home page") {
          auto rsp = http.get(base_url).dispatch_and_wait();

          THEN("the home page is normal and shows the options from setup") {
            CHECK(rsp->error().value_or("") == "");
            auto page = html(rsp);
            CHECK(page.xpath_exists(R"(//ol[@class="thread-list"])"));
            CHECK(page.xpath_exists(R"(//head/title[contains(text(),"MyServer")])")); // name
            CHECK(page.xpath_exists(R"(//div[@class="site-name"][contains(text(),"MyServer")])")); // name
            CHECK(page.xpath_exists(R"(//head/link[@rel="canonical"][@href="http://myserver.test/"])")); // base_url
            CHECK(page.xpath_exists(R"(//nav//a[@href="/register"])")); // registration_enabled
            CHECK(page.xpath_exists(R"(//head/script)")); // javascript_enabled
          }
        }

        AND_WHEN("user tries to log in again as the temporary admin user") {
          auto login = http.post(base_url + "/login").body(TYPE_FORM,
            fmt::format("actual_username={}&password={}", FIRST_RUN_ADMIN_USERNAME, first_run_admin_password)
          ).dispatch_and_wait();

          THEN("the login fails") { CHECK(login->status() == 400); }
        }

        AND_WHEN("user logs in using the new admin username and password") {
          auto login = http.post(base_url + "/login").body(TYPE_FORM,
            fmt::format("actual_username=myadmin&password=mypassword")
          ).dispatch_and_wait();

          THEN("the login succeeds") {
            CHECK(login->status() == 303);
            CHECK(login->error().value_or("") == "");
            const auto set_cookie = login->header("set-cookie");
            CHECK_FALSE(set_cookie == "");
            CHECK(login->header("location") == "/");
          }
        }
      }
    }
  }

  GIVEN("an unconfigured database with some existing users") {
    auto txn = db->open_write_txn_sync();
    const auto admin_id =
      users->create_local_user(txn, "myadmin", "myadmin@myserver.test", "myadminpassword", false);
    users->create_local_user(txn, "myuser", "myuser@myserver.test", "myuserpassword", false);
    users->update_local_user(txn, admin_id, nullopt, { .admin = IsAdmin::Yes });
    txn.commit();

    WHEN("user visits the home page") {
      auto rsp = http.get(base_url).dispatch_and_wait();

      THEN("user is redirected to the login page") {
        CHECK(rsp->error().value_or("") == "");
        auto login_page = html(rsp);
        CHECK(login_page.xpath_exists(R"(//form[@action="/login"])"));
      }
    }

    WHEN("user logs in to the web interface as the temporary admin user") {
      auto login = http.post(base_url + "/login").body(TYPE_FORM,
        fmt::format("actual_username={}&password={}", FIRST_RUN_ADMIN_USERNAME, first_run_admin_password)
      ).dispatch_and_wait();

      THEN("the login fails") { CHECK(login->status() == 400); }
    }

    WHEN("user logs in to the web interface as an existing admin user") {
      auto login = http.post(base_url + "/login")
        .body(TYPE_FORM, "actual_username=myadmin&password=myadminpassword")
        .dispatch_and_wait();
      CHECK(login->status() == 303);
      CHECK(login->error().value_or("") == "");
      CHECK(login->header("location") == "/");
      const auto cookie = get_login_cookie(login);

      AND_WHEN("user visits the home page") {
        auto rsp = http.get(base_url).header("cookie", cookie).dispatch_and_wait();

        THEN("the home page is the first-run setup form") {
          CHECK(rsp->error().value_or("") == "");
          auto page = html(rsp);
          CHECK(page.xpath_exists(R"(//form[@action="/site_admin/first_run_setup"])"));
          CHECK_FALSE(page.xpath_exists(R"(//form[@action="/login"])"));
        }
      }

      AND_WHEN("user visits a different settings page") {
        auto rsp = http.get(base_url + "/settings").header("cookie", cookie).dispatch_and_wait();

        THEN("the page is not accessible") { CHECK(rsp->status() == 403); }
      }

      AND_WHEN("a valid first-run setup form is submitted") {
        auto setup = http.post(base_url + "/site_admin/first_run_setup")
          .header("cookie", cookie)
          .body(TYPE_FORM, FIRST_RUN_SETUP_FORM "&default_board_name=myboard")
          .dispatch_and_wait();
        CHECK(setup->error().value_or("") == "");

        AND_WHEN("user visits the home page") {
          auto rsp = http.get(base_url).dispatch_and_wait();

          THEN("the home page is normal and shows the options from setup") {
            spdlog::info("base_url: {}", site->site_detail()->base_url);
            CHECK(rsp->error().value_or("") == "");
            auto page = html(rsp);
            CHECK(page.xpath_exists(R"(//ol[@class="thread-list"])"));
            CHECK(page.xpath_exists(R"(//head/title[contains(text(),"MyServer")])")); // name
            CHECK(page.xpath_exists(R"(//div[@class="site-name"][contains(text(),"MyServer")])")); // name
            CHECK(page.xpath_exists(R"(//head/link[@rel="canonical"][@href="http://myserver.test/"])")); // base_url
            CHECK(page.xpath_exists(R"(//nav//a[@href="/register"])")); // registration_enabled
            CHECK(page.xpath_exists(R"(//head/script)")); // javascript_enabled
          }
        }

        AND_WHEN("another first-run setup form is submitted") {
          auto setup_again = http.post(base_url + "/site_admin/first_run_setup")
            .header("cookie", cookie)
            .body(TYPE_FORM, FIRST_RUN_SETUP_FORM).dispatch_and_wait();

          THEN("the second setup request fails") { CHECK(setup_again->status() == 403); }
        }
      }
    }

    WHEN("user logs in to the web interface as an existing non-admin user") {
      auto login = http.post(base_url + "/login").body(TYPE_FORM, "actual_username=myuser&password=myuserpassword").dispatch_and_wait();
      CHECK(login->status() == 303);
      CHECK(login->error().value_or("") == "");
      CHECK(login->header("location") == "/");
      const auto cookie = get_login_cookie(login);

      AND_WHEN("user visits the home page") {
        auto rsp = http.get(base_url).header("cookie", cookie).dispatch_and_wait();

        THEN("the page is not accessible") { CHECK(rsp->status() == 403); }
      }

      AND_WHEN("a first-run setup form is submitted") {
        auto setup = http.post(base_url + "/site_admin/first_run_setup")
          .header("cookie", cookie)
          .body(TYPE_FORM, FIRST_RUN_SETUP_FORM)
          .dispatch_and_wait();

        THEN("the request fails and first-run setup is still incomplete") {
          CHECK(setup->status() == 403);
          auto rsp = http.get(base_url).dispatch_and_wait();
          CHECK(rsp->error().value_or("") == "");
          auto login_page = html(rsp);
          CHECK_FALSE(login_page.xpath_exists(R"(//ol[@class="thread-list"])"));
          CHECK(login_page.xpath_exists(R"(//form[@action="/login"])"));
        }
      }
    }
  }
}
