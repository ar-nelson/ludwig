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
    expected_hash = "3e7bdeadbcbede063612b1ced9c42852848d088c4bfa5ed160862d168ec11e99";

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
