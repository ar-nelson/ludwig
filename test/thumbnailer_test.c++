#include <catch2/catch_test_macros.hpp>
#include "../src/thumbnailer.h++"
#include <fstream>
#include <filesystem>
#include <iterator>

using namespace Ludwig;

static inline void thumbnail(std::string extension) {
  std::filesystem::path p(__FILE__), file = p.parent_path() / "images" / ("test." + extension);
  REQUIRE(std::filesystem::exists(file));
  std::string src;
  {
    std::ostringstream ss;
    std::ifstream input(file.c_str(), std::ios::binary);
    ss << input.rdbuf();
    src = ss.str();
  }
  REQUIRE(!src.empty());
  auto thumbnail = generate_thumbnail("image/" + extension, src, 256);
  REQUIRE(!thumbnail.empty());
  std::ofstream output(p.parent_path() / "images" / ("thumbnail_" + extension + ".webp"));
  output << thumbnail;
}

TEST_CASE("webp thumbnail", "[thumbnailer]") {
  thumbnail("webp");
}

TEST_CASE("jpeg thumbnail", "[thumbnailer]") {
  thumbnail("jpg");
}

TEST_CASE("png thumbnail", "[thumbnailer]") {
  thumbnail("png");
}

TEST_CASE("gif thumbnail", "[thumbnailer]") {
  thumbnail("gif");
}
