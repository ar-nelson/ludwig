#pragma once
#include "services/db.h++"

namespace Ludwig {
  auto zstd_db_dump_import(
    const char* db_filename,
    FILE* zstd_dump_file,
    size_t file_size,
    std::optional<std::shared_ptr<SearchEngine>> search = {},
    size_t map_size_mb = 1024
  ) -> void;

  auto zstd_db_dump_export(ReadTxn& txn) -> std::generator<std::span<uint8_t>>;
}
