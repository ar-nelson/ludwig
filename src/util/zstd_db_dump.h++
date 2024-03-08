#pragma once
#include "services/db.h++"

namespace Ludwig {
  auto zstd_db_dump_import(
    const char* db_filename,
    FILE* zstd_dump_file,
    size_t file_size,
    std::optional<std::shared_ptr<SearchEngine>> search = {},
    size_t map_size_mb = 1024
  ) -> DB;

  auto zstd_db_dump_export(
    ReadTxn& txn,
    uWS::MoveOnlyFunction<void (std::unique_ptr<uint8_t[]>&&, size_t)>&& callback
  ) -> void;
}
