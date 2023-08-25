#include "id.h++"
#include <chrono>
#include <mutex>
#include <random>

#define TIMESTAMP_BITS 42
#define RANDOM_BITS 22
#define TIMESTAMP_MASK 0b1111111111'1111111111'1111111111'1111111111'1100000000'0000000000'0000ULL
#define RANDOM_MASK    0b0000000000'0000000000'0000000000'0000000000'0011111111'1111111111'1111ULL

static uint64_t last_ms = 0, last_id = 0;
static std::mutex mx;
static std::random_device rnd_dev;
static std::default_random_engine rnd(rnd_dev());
static std::uniform_int_distribution<uint64_t> rnd_int(0, 1 << (RANDOM_BITS - 1));

namespace Ludwig {
  uint64_t now_ms() {
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
    );
  }

  uint64_t next_id() {
    std::lock_guard<std::mutex> guard(mx);
    auto ms = now_ms();
    if (ms > last_ms) {
      last_ms = ms;
      return last_id = (ms << RANDOM_BITS) | rnd_int(rnd);
    }
    return last_id = (last_id & TIMESTAMP_MASK) | (((last_id & RANDOM_MASK) + 1) & RANDOM_MASK);
  }
}
