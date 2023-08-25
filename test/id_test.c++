#include <catch2/catch_test_macros.hpp>
#include "../src/id.h++"

TEST_CASE("IDs are monotonic", "[id]") {
  const auto id1 = Ludwig::next_id(), id2 = Ludwig::next_id(), id3 = Ludwig::next_id();
  REQUIRE(id2 > id1);
  REQUIRE(id3 > id2);
}
