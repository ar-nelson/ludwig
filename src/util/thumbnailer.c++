#include "thumbnailer.h++"
#include <webp/decode.h>
#include <webp/encode.h>
#include <imageio/image_dec.h>
#include <gif_lib.h>

using std::make_unique, std::min, std::optional, std::round, std::runtime_error,
    std::string, std::string_view;

namespace Ludwig {

  static const string ENC_ERROR_MESSAGES[VP8_ENC_ERROR_LAST] = {
    "OK",
    "OUT_OF_MEMORY",
    "BITSTREAM_OUT_OF_MEMORY",
    "NULL_PARAMETER",
    "INVALID_CONFIGURATION",
    "BAD_DIMENSION",
    "PARTITION0_OVERFLOW",
    "PARTITION_OVERFLOW",
    "BAD_WRITE",
    "FILE_TOO_BIG",
    "USER_ABORT"
  };

  static const string DEC_ERROR_MESSAGES[] = {
    "OK",
    "OUT_OF_MEMORY",
    "INVALID_PARAM",
    "BITSTREAM_ERROR",
    "UNSUPPORTED_FEATURE",
    "SUSPENDED",
    "USER_ABORT",
    "NOT_ENOUGH_DATA"
  };

  // GIF is a special case because WebP doesn't implement it for some reason…
  // An animated GIF parser is in the examples, but not a normal GIF parser?
  // Anyway, this impl came from studying:
  //
  // https://github.com/creatale/node-dv/blob/6150a6daec48b7d73e33f7cb846e8fd1cd19a7c8/deps/leptonica/src/gifio.c
  //
  // which I found by Googling for DGifOpen to see how it was used, since ESR
  // barely wrote any documentation for this cursed library.
  //
  // This GIF reader is probably the riskiest thing I've done with memory in the
  // entire codebase. I'd bet money that someone can make Ludwig segfault with a
  // bad GIF file. This whole module needs to be sandboxed with WASM.

  struct GifBuffer {
    const uint8_t* buf;
    size_t off, sz;
  };

  static auto gif_reader(GifFileType* gif, GifByteType* dest, int bytes_to_read) -> int {
    auto& buf = *reinterpret_cast<GifBuffer*>(gif->UserData);
    if (buf.off >= buf.sz) return -1;
    size_t bytes = min((size_t)bytes_to_read, buf.sz - buf.off);
    memcpy(dest, buf.buf + buf.off, bytes);
    buf.off += bytes;
    return (int)bytes;
  }

  struct GifFile {
    GifBuffer buf;
    GifFileType* gif;
    GifFile(const uint8_t* data, size_t sz) {
      buf = { .buf = data, .off = 0, .sz = sz };
      int err = 0;
      gif = DGifOpen(&buf, gif_reader, &err);
      if (err) {
        auto msg = GifErrorString(err);
        throw runtime_error("GIF open failed: " + (msg == nullptr ? "?" : string(msg)));
      }
      if (DGifSlurp(gif) != GIF_OK || gif->SavedImages == nullptr) {
        DGifCloseFile(gif, &err);
        throw runtime_error("GIF read failed");
      }
    }
    auto decode(WebPPicture* pic) -> void {
      if (gif->ImageCount < 1) throw runtime_error("GIF has 0 subimages");
      SavedImage& s = gif->SavedImages[0];

      const size_t width = (size_t)s.ImageDesc.Width, height = (size_t)s.ImageDesc.Height;
      if (!width || !height) throw runtime_error("GIF has 0 width/height");

      ColorMapObject* cmap;
      if (s.ImageDesc.ColorMap != nullptr) cmap = s.ImageDesc.ColorMap;
      else if (gif->SColorMap != nullptr) cmap = gif->SColorMap;
      else throw runtime_error("GIF has no color map");

      auto rgba = make_unique<uint8_t[]>(width * height * 4);
      auto d = rgba.get();

      for (size_t i = 0; i < width * height; i++) {
        int c = s.RasterBits[i];
        if (c < 0 || c >= cmap->ColorCount) {
          *d ++= 0;
          *d ++= 0;
          *d ++= 0;
          *d ++= 0;
        } else {
          GifColorType rgb = cmap->Colors[c];
          *d ++= rgb.Red;
          *d ++= rgb.Green;
          *d ++= rgb.Blue;
          *d ++= 0xff;
        }
      }

      pic->width = (int)width;
      pic->height = (int)height;
      pic->use_argb = true;
      if (!WebPPictureImportRGBA(pic, rgba.get(), (int)width * 4)) {
        throw runtime_error("GIF data import failed, cannot generate thumbnail.");
      }
    }
    ~GifFile() {
      int err;
      DGifCloseFile(gif, &err);
    }
  };

  // And now the actual thumbnailer!
  // This should be a standalone library, eventually.
  //
  // It's really, really inefficient. For non-WebP files, it decodes, encodes,
  // decodes, then encodes again, while keeping two(!) copies of the full-size
  // image in memory. I hate this but I've also spent too long on it to bother
  // fixing it yet.
  //
  // libWebP has exactly one way to resize images: the scale option on the
  // decoder. But to decode, you first have to encode, hence the
  // src->RGBA->WebP->RGBA->WebP dance.

  static auto string_writer(const uint8_t* data, size_t sz, const WebPPicture* pic) -> int {
    auto& out = *reinterpret_cast<string*>(pic->custom_ptr);
    out.append(reinterpret_cast<const char*>(data), sz);
    return true;
  }

  struct Thumbnailer {
    WebPDecoderConfig config = {
      .output = {
        .colorspace = MODE_RGBA
      },
      .options = {
        .no_fancy_upsampling = true,
        .use_scaling = true
      },
    };
    WebPPicture pic;
    Thumbnailer() {
      if (!WebPPictureInit(&pic)) throw runtime_error("Failed to initialize WebP");
    }
    auto generate(
      string_view mimetype,
      const uint8_t* data,
      size_t sz,
      uint16_t target_width,
      uint16_t target_height,
      size_t target_size_bytes
    ) -> string {
      string intermediate;

      /////////////////////////////////////////////////////////////////////////
      // Step 1: Parse non-WebP images
      //
      if (mimetype != "image/webp") {
        pic.use_argb = true;
        pic.writer = string_writer;
        pic.custom_ptr = &intermediate;
        if (mimetype == "image/gif") {
          GifFile gif(data, sz);
          gif.decode(&pic);
        } else {
          WebPImageReader reader;
          if (mimetype == "image/jpeg" || mimetype == "image/jpg") reader = WebPGetImageReader(WEBP_JPEG_FORMAT);
          else if (mimetype == "image/png") reader = WebPGetImageReader(WEBP_PNG_FORMAT);
          else reader = WebPGuessImageReader(data, sz);

          if (!reader(data, sz, &pic, true, nullptr)) {
            throw runtime_error("Image parse failed, cannot generate thumbnail.");
          }
        }
        WebPConfig enc_config = { .lossless = true, .segments = 1, .pass = 1 };
        if (!WebPEncode(&enc_config, &pic)) {
          throw runtime_error(
            "Intermediate image encode failed, cannot generate thumbnail. Error: " + ENC_ERROR_MESSAGES[pic.error_code]
          );
        }
        data = reinterpret_cast<const uint8_t*>(intermediate.data());
        sz = intermediate.length();

        WebPPictureFree(&pic);
        if (!WebPPictureInit(&pic)) throw runtime_error("Failed to initialize WebP");
      }

      /////////////////////////////////////////////////////////////////////////
      // Step 2: Decode full-size WebP, scale and crop
      //
      int original_w, original_h;
      if (!WebPGetInfo(data, sz, &original_w, &original_h)) {
        throw runtime_error("WebP stream is invalid");
      }
      const float aspect_ratio = (float)target_width / (float)target_height,
        original_aspect_ratio = (float)original_w / (float)original_h;
      if ((int)round((float)original_h * aspect_ratio) != original_w) {
        config.options.use_cropping = true;
        if (original_aspect_ratio < aspect_ratio) /* tall */ {
          config.options.crop_width = original_w;
          const auto h = config.options.crop_height = (int)round((float)original_w / aspect_ratio);
          config.options.crop_top = (original_h - h) / 2;
          config.options.scaled_width = min((uint16_t)original_w, target_width);
          config.options.scaled_height = (int)round((float)config.options.scaled_width / aspect_ratio);
        } else /* wide */ {
          config.options.crop_height = original_h;
          const auto w = config.options.crop_width = (int)round((float)original_h * aspect_ratio);
          config.options.crop_left = (original_w - w) / 2;
          config.options.scaled_height = min((uint16_t)original_h, target_height);
          config.options.scaled_width = (int)round((float)config.options.scaled_height * aspect_ratio);
        }
      } else {
        config.options.scaled_width = min((uint16_t)original_w, target_width);
        config.options.scaled_height = min((uint16_t)original_h, target_height);
      }
      if (auto err = WebPDecode(data, sz, &config)) {
        throw runtime_error("WebP decode failed. Error: " + DEC_ERROR_MESSAGES[err]);
      }

      /////////////////////////////////////////////////////////////////////////
      // Step 3: Re-encode scaled and cropped WebP
      //
      string out;
      pic.writer = string_writer;
      pic.custom_ptr = &out;
      pic.width = config.output.width;
      pic.height = config.output.height;
      if (!WebPPictureImportRGBA(&pic, config.output.u.RGBA.rgba, config.output.u.RGBA.stride)) {
        throw runtime_error("Image data import failed, cannot generate thumbnail.");
      }
      WebPConfig enc_config = { .target_size = (int)target_size_bytes, .segments = 1, .pass = 1, .qmin = 50, .qmax = 100 };
      if (!WebPEncode(&enc_config, &pic)) {
        throw runtime_error(
          "Image encode failed, cannot generate thumbnail. Error: " + ENC_ERROR_MESSAGES[pic.error_code]
        );
      }
      return out;
    }
    ~Thumbnailer() {
      WebPPictureFree(&pic);
      WebPFreeDecBuffer(&config.output);
    }
  };

  auto generate_thumbnail(
    optional<string_view> mimetype,
    string_view data,
    uint16_t width,
    uint16_t height
  ) -> string {
    Thumbnailer t;
    return t.generate(
      to_ascii_lowercase(mimetype.value_or("")),
      reinterpret_cast<const uint8_t*>(data.data()),
      data.length(),
      width,
      height ? height : width,
      (width * (height ? height : width)) / 8 // for 256x256, this is about 8KiB
    );
  }
}
