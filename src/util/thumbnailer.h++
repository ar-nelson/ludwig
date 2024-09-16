#pragma once
#include "util/common.h++"
#include <vips/vips8>
#include <xxhash.h>

namespace Ludwig {

  class ImageRef {
  private:
    VipsArea* area;
    uint64_t _hash;
  public: 
    ImageRef() : area(nullptr), _hash(0) {}
    ImageRef(VipsBlob* blob) :
      area(blob ? &blob->area : nullptr),
      _hash(area ? XXH3_64bits(area->data, area->length) : 0) {}
    ~ImageRef() {
      if (area) vips_area_unref(area);
    }
    ImageRef(const ImageRef& from) : area(from.area), _hash(from._hash) {
      if (area) from.area->count++;
    }
    ImageRef(ImageRef&& from) : area(from.area), _hash(from._hash) {
      from.area = nullptr;
      from._hash = 0;
    }
    void operator=(const ImageRef& from) {
      if (from.area) from.area->count++;
      if (area) vips_area_unref(area);
      area = from.area;
      _hash = from._hash;
    }
    void operator=(ImageRef&& from) {
      if (area) vips_area_unref(area);
      area = from.area;
      _hash = from._hash;
      from.area = nullptr;
      from._hash = 0;
    }
    uint64_t hash() const {
      return _hash;
    }
    const void* data() const {
      return area ? area->data : nullptr;
    }
    size_t length() const {
      return area ? area->length : 0;
    }
    operator bool() const {
      return !!area;
    }
    operator std::string_view() const {
      return {(const char*)data(), length() };
    }
  };

  auto generate_thumbnail(
    std::optional<std::string_view> mimetype,
    std::string_view data,
    uint16_t width,
    uint16_t height = 0
  ) -> ImageRef;
}
