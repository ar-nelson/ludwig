#pragma once
#include "util/common.h++"
#include <concurrent_lru_cache.h>

namespace Ludwig {
  class RateLimiter {
  private:
    double interval, max_permits, stored_permits;
    uint64_t next_free;
  public:
    RateLimiter() : interval(0), max_permits(0), stored_permits(0), next_free(0) {}
    RateLimiter(double permits_per_second, uint32_t max_permits);
    auto try_acquire(uint32_t count = 1) -> bool;
  };

  class IpRateLimiter {
  private:
    tbb::concurrent_lru_cache<std::string_view, RateLimiter, std::function<RateLimiter (std::string_view)>> by_ip;
  public:
    IpRateLimiter(double permits_per_second, uint32_t max_permits, size_t max_ips = 65536);
    auto acquire_or_throw(std::string_view ip, uint32_t count = 1) -> void;
  };
}
