#include "util.h++"
#include "services/db.h++"
#include "controllers/instance.h++"
#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>

using namespace Ludwig;

TEST_CASE("hash password", "[controller]") {
  static constexpr std::string_view
    salt = "0123456789abcdef",
    password = "fhqwhgads",
    // Generated with https://argon2.online/ using this password and salt, mem 65535, iteration 3
    expected_hash = "1fbf4b3d9639fc815fb394b95ac2913d14e0f9375a5a7e6fa6291ab660b9d9e6";

  TempFile file;
  auto db = std::make_shared<DB>(file.name);
  auto controller = std::make_shared<InstanceController>(db);
  uint8_t hash[32];
  controller->hash_password({ std::string(password) }, reinterpret_cast<const uint8_t*>(salt.data()), hash);
  std::ostringstream actual_hash;
  actual_hash << std::hex;
  for(size_t i = 0; i < 32; i++) {
    actual_hash << std::setw(2) << std::setfill('0') << (int)hash[i];
  }
  REQUIRE(actual_hash.str() == expected_hash);
}
