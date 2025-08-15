#include "common.h++"
#define WUFFS_IMPLEMENTATION 1
#include "wuffs-v0.4.c"

namespace Ludwig {

uint64_t xxhash64(std::string_view data) noexcept {
  auto* h = (wuffs_xxhash64__hasher*)alloca(sizeof__wuffs_xxhash64__hasher());
  auto status = h->initialize(sizeof__wuffs_xxhash64__hasher(), WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
  assert(status.is_ok());
  return h->update_u64(wuffs_base__slice_u8((uint8_t*)(data.data()), data.length()));
}

}