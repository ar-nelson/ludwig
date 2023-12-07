#include "zstd_db_dump.h++"
#include <zstd.h>

using std::copy, std::make_unique, std::runtime_error, std::string_view, std::unique_ptr;

namespace Ludwig {
  auto zstd_db_dump_import(
    const char* db_filename,
    FILE* zstd_dump_file,
    size_t file_size,
    std::optional<std::shared_ptr<SearchEngine>> search,
    size_t map_size_mb
  ) -> DB {
    unique_ptr<ZSTD_DCtx, void(*)(ZSTD_DCtx*)> dctx(ZSTD_createDCtx(), [](auto* c) { ZSTD_freeDCtx(c); });
    if (dctx == nullptr) throw runtime_error("zstd init failed");
    const size_t in_buf_size = ZSTD_DStreamInSize(), out_buf_size = ZSTD_DStreamOutSize();
    const auto in_buf = make_unique<uint8_t[]>(in_buf_size), out_buf = make_unique<uint8_t[]>(out_buf_size);
    size_t out_pos = 0, out_max = 0, total_read = 0;
    ZSTD_inBuffer input { in_buf.get(), 0, 0 };
    return DB(db_filename, [&](uint8_t* buf, size_t expected) -> size_t {
      uint8_t* buf_offset = buf;
      size_t remaining_expected = expected;
      do {
        if (out_pos < out_max) {
          size_t remaining_available = out_max - out_pos;
          if (remaining_available >= remaining_expected) {
            copy(out_buf.get() + out_pos, out_buf.get() + out_pos + remaining_expected, buf_offset);
            out_pos += remaining_expected;
            return expected;
          }
          copy(out_buf.get() + out_pos, out_buf.get() + out_pos + remaining_available, buf_offset);
          buf_offset += remaining_available;
          remaining_expected -= remaining_available;
        }
        if (input.pos >= input.size) {
          const size_t bytes = fread(in_buf.get(), 1, in_buf_size, zstd_dump_file);
          if (!bytes) return expected - remaining_expected;
          total_read += bytes;
          spdlog::info("{:.2f}%", 100.0 * ((double)total_read / (double)file_size));
          input = { in_buf.get(), bytes, 0 };
        }
        ZSTD_outBuffer output { out_buf.get(), out_buf_size, 0 };
        const auto ret = ZSTD_decompressStream(dctx.get(), &output, &input);
        if (ZSTD_isError(ret)) throw runtime_error(ZSTD_getErrorName(ret));
        out_pos = 0;
        out_max = output.pos;
      } while (remaining_expected);
      return expected;
    }, search, map_size_mb);
  }

  auto zstd_db_dump_export(ReadTxnBase& txn, uWS::MoveOnlyFunction<void (std::unique_ptr<uint8_t[]>&&, size_t)>&& callback) -> void {
    unique_ptr<ZSTD_CCtx, void(*)(ZSTD_CCtx*)> cctx(ZSTD_createCCtx(), [](auto* c) { ZSTD_freeCCtx(c); });
    if (cctx == nullptr) throw runtime_error("zstd init failed");
    const size_t in_buf_size = ZSTD_CStreamInSize(), out_buf_size = ZSTD_CStreamOutSize();
    auto in_buf = make_unique<uint8_t[]>(in_buf_size), out_buf = make_unique<uint8_t[]>(out_buf_size);
    size_t in_pos = 0;
    txn.dump([&](auto& span, bool last_chunk){
      assert(span.size() <= in_buf_size);
      if (in_pos + span.size() <= in_buf_size) {
        copy(span.begin(), span.end(), in_buf.get() + in_pos);
        in_pos += span.size();
        if (!last_chunk) return;
      }
      ZSTD_inBuffer input = { in_buf.get(), in_pos, 0 };
      bool finished;
      do {
        ZSTD_outBuffer output = { out_buf.get(), out_buf_size, 0 };
        const auto remaining = ZSTD_compressStream2(cctx.get(), &output, &input, last_chunk ? ZSTD_e_end : ZSTD_e_continue);
        callback(std::move(out_buf), output.pos);
        out_buf.reset(new uint8_t[out_buf_size]);
        finished = last_chunk ? (remaining == 0) : (input.pos == input.size);
      } while (!finished);
      if (!last_chunk) {
        copy(span.begin(), span.end(), in_buf.get());
        in_pos = span.size();
      }
    });
  }
}
