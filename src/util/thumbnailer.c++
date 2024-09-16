#include "thumbnailer.h++"

using std::optional, std::string, std::string_view, vips::VImage;

namespace Ludwig {
  auto generate_thumbnail(
    optional<string_view> mimetype,
    string_view data,
    uint16_t width,
    uint16_t height
  ) -> ImageRef {
    return ImageRef(
      VImage::thumbnail_buffer((void*)data.data(), data.length(), width,
        VImage::option()
        ->set("height", height)
        ->set("crop", VIPS_INTERESTING_CENTRE)
      ).webpsave_buffer(
        VImage::option()->set("target_size", width * (height ? height : width))
      )
    );
  }
}
