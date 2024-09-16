#include "test_common.h++"
#include "util/thumbnailer.h++"

using namespace Ludwig;

static inline void thumbnail(std::string extension) {
  auto src = load_file(test_root() / "images" / ("test." + extension));
  REQUIRE(!src.empty());
  auto thumbnail = generate_thumbnail("image/" + extension, src, 256);
  REQUIRE(!!thumbnail);
  std::ofstream output(test_root() / "images" / ("thumbnail_" + extension + ".webp"));
  std::string_view sv = thumbnail;
  output << sv;
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
