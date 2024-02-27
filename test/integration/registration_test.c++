#include "integration_common.h++"

SCENARIO_METHOD(IntegrationTest, "registration", "[integration]") {
  FirstRunSetup setup {
    .default_board_name = "main",
    .admin_name = "admin",
    .admin_password = "password"
  };

  GIVEN("a server with no registration restrictions") {
    setup.registration_enabled = true;
    setup.registration_application_required = false;
    setup.registration_invite_required = false;
    instance->first_run_setup(std::move(setup));

    WHEN("a user visits the home page") {
      auto rsp = http.get(base_url).dispatch_and_wait();
      CHECK(rsp->error().value_or("") == "");
      auto page = html(rsp);

      THEN("there is a Register link") {
        CHECK(page.xpath_exists(R"(//a[@href="/register"])"));
      }
    }

    WHEN("a user visits the Register page") {
      auto rsp = http.get(base_url + "/register").dispatch_and_wait();
      auto page = html(rsp);

      THEN("the page displays a registation form") {
        CHECK(rsp->error().value_or("") == "");
        CHECK(page.xpath_exists(R"(//form[@action="/register"])"));
      }

      THEN("all registration form fields exist") {
        CHECK(page.xpath_exists(R"(//input[@name="username"])"));
        CHECK(page.xpath_exists(R"(//input[@name="actual_username"])"));
        CHECK(page.xpath_exists(R"(//input[@name="password"])"));
        CHECK(page.xpath_exists(R"(//input[@name="confirm_password"])"));
        CHECK(page.xpath_exists(R"(//input[@name="email"])"));
      }

      THEN("the application reason field is not present") {
        CHECK_FALSE(page.xpath_exists(R"(//textarea[@name="application_reason"])"));
      }

      THEN("the invite code field is not present") {
        CHECK_FALSE(page.xpath_exists(R"(//input[@name="invite_code"])"));
      }
    }

    WHEN("a user submits a registration form with an invalid username") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=look, spaces!&password=mypassword&confirm_password=mypassword&email=myuser@foo.test")
        .dispatch_and_wait();

      THEN("the registration fails") { CHECK(registration->status() == 400); }
    }

    WHEN("a user submits a registration form without an email address") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=mypassword")
        .dispatch_and_wait();

      THEN("the registration fails") { CHECK(registration->status() == 400); }
    }

    WHEN("a user submits a registration form with passwords that don't match") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=notmypassword&email=myuser@foo.test")
        .dispatch_and_wait();

      THEN("the registration fails") { CHECK(registration->status() == 400); }
    }

    WHEN("a user submits a valid registration form") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=mypassword&email=myuser@foo.test")
        .dispatch_and_wait();

      AND_WHEN("the registration succeeds") {
        auto page = html(registration);
        REQUIRE(registration->error().value_or("") == "");

        THEN("a success page with a login link is displayed") {
          auto page = html(registration);
          CHECK_FALSE(page.xpath_exists(R"(//form[@action="/register"])"));
          CHECK(page.xpath_exists(R"(//h2[contains(text(),"Registration complete")])"));
          CHECK(page.xpath_exists(R"(//a[@href="/login"])"));
        }

        AND_WHEN("the user logs in with the given username and password") {
          auto login = http.post(base_url + "/login")
            .body(TYPE_FORM, "actual_username=myuser&password=mypassword")
            .dispatch_and_wait();

          AND_WHEN("the login succeeds") {
            REQUIRE(login->error().value_or("") == "");
            const auto cookie = get_login_cookie(login);

            AND_WHEN("the user views the home page while logged in") {
              auto home = http.get(base_url).header("cookie", cookie).dispatch_and_wait();
              CHECK(home->error().value_or("") == "");
              auto page = html(home);

              THEN("the user's name is displayed in the nav bar") {
                CHECK(page.xpath_exists(R"(//nav//a[@href="/u/myuser"][contains(text(),"myuser")])"));
              }

              THEN("the 'account is not yet approved' banner is not present") {
                CHECK_FALSE(page.xpath_exists(R"(//div[@id="banner-not-approved"])"));
              }
            }
          }
        }
      }
    }
  }

  GIVEN("a server which requires approval for registration") {
    setup.registration_enabled = true;
    setup.registration_application_required = true;
    setup.application_question = "Who goes there?";
    setup.registration_invite_required = false;
    instance->first_run_setup(std::move(setup));

    WHEN("a user visits the Register page") {
      auto rsp = http.get(base_url + "/register").dispatch_and_wait();
      auto page = html(rsp);

      THEN("the application reason field is present") {
        CHECK(page.xpath_exists(R"(//textarea[@name="application_reason"])"));
        CHECK(rsp->body().contains("Who goes there?"));
      }

      THEN("the invite code field is not present") {
        CHECK_FALSE(page.xpath_exists(R"(//input[@name="invite_code"])"));
      }
    }

    WHEN("a user submits a registration form without an application reason") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=mypassword&email=myuser@foo.test")
        .dispatch_and_wait();

      THEN("the registration fails") { CHECK(registration->status() == 400); }
    }

    WHEN("a user submits a registration form with an application reason") {
      auto registration = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=mypassword&email=myuser@foo.test&application_reason=for the lulz")
        .dispatch_and_wait();

      AND_WHEN("the registration succeeds") {
        auto page = html(registration);
        REQUIRE(registration->error().value_or("") == "");

        THEN("a success page with a login link is displayed") {
          auto page = html(registration);
          CHECK_FALSE(page.xpath_exists(R"(//form[@action="/register"])"));
          CHECK(page.xpath_exists(R"(//h2[contains(text(),"Registration complete")])"));
          CHECK(page.xpath_exists(R"(//a[@href="/login"])"));
        }

        AND_WHEN("the user logs in with the given username and password") {
          auto login = http.post(base_url + "/login")
            .body(TYPE_FORM, "actual_username=myuser&password=mypassword")
            .dispatch_and_wait();

          AND_WHEN("the login succeeds") {
            REQUIRE(login->error().value_or("") == "");
            const auto cookie = get_login_cookie(login);

            AND_WHEN("the user views the home page while logged in") {
              auto home = http.get(base_url).header("cookie", cookie).dispatch_and_wait();
              CHECK(home->error().value_or("") == "");
              auto page = html(home);

              THEN("the user's name is displayed in the nav bar") {
                CHECK(page.xpath_exists(R"(//nav//a[@href="/u/myuser"][contains(text(),"myuser")])"));
              }

              THEN("the 'account is not yet approved' banner is present") {
                CHECK(page.xpath_exists(R"(//div[@id="banner-not-approved"])"));
              }
            }
          }
        }

        AND_WHEN("an admin logs in") {
          auto admin_login = http.post(base_url + "/login")
            .body(TYPE_FORM, "actual_username=admin&password=password")
            .dispatch_and_wait();
          REQUIRE(admin_login->error().value_or("") == "");
          const auto admin_cookie = get_login_cookie(admin_login);

          AND_WHEN("the admin views the Applications page") {
            auto rsp = http.get(base_url + "/site_admin/applications")
              .header("cookie", admin_cookie)
              .dispatch_and_wait();
            CHECK(rsp->error().value_or("") == "");

            THEN("the application is visible in the list") {
              auto page = html(rsp);
              CHECK(page.xpath_exists(R"(//tbody[@id="application-table"]/tr/td[text()="myuser"])"));
              CHECK(page.xpath_exists(R"(//tbody[@id="application-table"]/tr/td[text()="for the lulz"])"));
            }
          }

          AND_WHEN("the admin approves the application") {
            uint64_t id = 0;
            {
              auto txn = instance->open_read_txn();
              instance->list_applications([&](auto p) { id = p.second.id; }, txn, {}, {}, 1);
            }
            REQUIRE(id != 0);
            auto rsp = http.post(fmt::format("{}/site_admin/applications/approve/{:x}", base_url, id))
              .header("cookie", admin_cookie)
              .body(TYPE_FORM, "")
              .dispatch_and_wait();
            CHECK(rsp->error().value_or("") == "");

            AND_WHEN("the new user logs in with the given username and password") {
              auto login = http.post(base_url + "/login")
                .body(TYPE_FORM, "actual_username=myuser&password=mypassword")
                .dispatch_and_wait();

              AND_WHEN("the login succeeds") {
                REQUIRE(login->error().value_or("") == "");
                const auto cookie = get_login_cookie(login);

                AND_WHEN("the user views the home page while logged in") {
                  auto home = http.get(base_url).header("cookie", cookie).dispatch_and_wait();
                  CHECK(home->error().value_or("") == "");
                  auto page = html(home);

                  THEN("the 'account is not yet approved' banner is not present") {
                    CHECK_FALSE(page.xpath_exists(R"(//div[@id="banner-not-approved"])"));
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  GIVEN("a server which requires invite codes from an admin") {
    setup.registration_enabled = true;
    setup.registration_application_required = false;
    setup.registration_invite_required = true;
    setup.invite_admin_only = true;
    instance->first_run_setup(std::move(setup));

    WHEN("a user visits the Register page") {
      auto rsp = http.get(base_url + "/register").dispatch_and_wait();
      auto page = html(rsp);

      THEN("the application reason field is not present") {
        CHECK_FALSE(page.xpath_exists(R"(//textarea[@name="application_reason"])"));
      }

      THEN("the invite code field is present") {
        CHECK(page.xpath_exists(R"(//input[@name="invite_code"])"));
      }
    }
  }

  /*
  GIVEN("a server which requires invite codes from any user") {
    setup.registration_enabled = true;
    setup.registration_application_required = false;
    setup.registration_invite_required = true;
    setup.invite_admin_only = false;
    instance->first_run_setup(std::move(setup));
  }
  */

  GIVEN("a server with registration disabled") {
    setup.registration_enabled = false;
    setup.registration_application_required = false;
    setup.registration_invite_required = false;
    instance->first_run_setup(std::move(setup));

    WHEN("a user visits the home page") {
      auto rsp = http.get(base_url).dispatch_and_wait();
      CHECK(rsp->error().value_or("") == "");
      auto page = html(rsp);

      THEN("there is no Register link") {
        CHECK_FALSE(page.xpath_exists(R"(//a[@href="/register"])"));
      }
    }

    WHEN("a user visits the Register page") {
      auto rsp = http.get(base_url + "/register").dispatch_and_wait();

      THEN("the page does not exist") {
        CHECK(rsp->status() >= 400);
      }
    }

    WHEN("a user attempts to submit a registation form") {
      auto rsp = http.post(base_url + "/register")
        .body(TYPE_FORM, "actual_username=myuser&password=mypassword&confirm_password=mypassword&email=myuser@foo.test")
        .dispatch_and_wait();

      THEN("the request fails") {
        CHECK(rsp->status() >= 400);
      }
    }
  }
}
