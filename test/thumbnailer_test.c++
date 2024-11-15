#include "test_common.h++"
#include "services/thumbnail_cache.h++"

using namespace Ludwig;

static inline void thumbnail(std::string extension, bool should_pass = true) {
  auto src = load_file(test_root() / "images" / ("test." + extension));
  REQUIRE(!src.empty());
  auto thumbnail = ThumbnailCache::generate_thumbnail("image/" + extension, src, 256);
  REQUIRE(!!thumbnail == should_pass);
  if (should_pass) {
    std::ofstream output(test_root() / "images" / ("thumbnail_" + extension + ".webp"));
    std::string_view sv = thumbnail;
    REQUIRE(sv.length() > 256);
    output << sv;
  }
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

TEST_CASE("avif thumbnail", "[thumbnailer]") {
# ifdef LUDWIG_THUMBNAIL_AVIF
  thumbnail("avif", true);
# else
  thumbnail("avif", false);
# endif
}

TEST_CASE("jxl thumbnail", "[thumbnailer]") {
# ifdef LUDWIG_THUMBNAIL_JXL
  thumbnail("jxl", true);
# else
  thumbnail("jxl", false);
# endif
}

TEST_CASE("svg thumbnail", "[thumbnailer]") {
# ifdef LUDWIG_THUMBNAIL_SVG
  thumbnail("svg", true);
# else
  thumbnail("svg", false);
# endif
}

TEST_CASE("pdf thumbnail", "[thumbnailer]") {
# ifdef LUDWIG_THUMBNAIL_PDF
  thumbnail("pdf", true);
# else
  thumbnail("pdf", false);
# endif
}

TEST_CASE("garbage thumbnail", "[thumbnailer]") {
  thumbnail("garbage", false);
}