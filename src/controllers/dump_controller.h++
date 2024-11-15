#pragma once
#include "db/db.h++"
#include "services/search_engine.h++"

namespace Ludwig {

class DumpController {
public:
  static auto import_dump(
    const char* db_filename,
    FILE* zstd_dump_file,
    size_t file_size,
    std::shared_ptr<SearchEngine> search = nullptr,
    size_t map_size_mb = 1024
  ) -> void;

  auto export_dump(ReadTxn& txn) -> std::generator<std::span<uint8_t>>;
};

}
