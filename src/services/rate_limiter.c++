#include "rate_limiter.h++"
#include "util/web.h++"

namespace Ludwig {
  RateLimiter::RateLimiter(double permits_per_second, uint32_t max_permits)
    : interval(1'000'000.0 / permits_per_second), max_permits(max_permits), stored_permits(0), next_free(0)
  {
    if (permits_per_second <= 0) throw std::runtime_error("RateLimiter: permits_per_second must be > 0");
  }

  auto RateLimiter::try_acquire(uint32_t count) -> bool {
    using namespace std::chrono;
    const uint64_t now = (uint64_t)duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

    // If we're past the next_free, then recalculate
    // stored permits, and update next_free
    if (now > next_free) {
      stored_permits = std::min(max_permits, stored_permits + (double)(now - next_free) / interval);
      next_free = now;
    } else {
      return false;
    }

    // Determine how many stored and freh permits to consume
    const double permits = (double)count,
      stored = std::min(permits, stored_permits),
      fresh = permits - stored;

    // In the general RateLimiter, stored permits have no wait time,
    // and thus we only have to wait for however many fresh permits we consume
    const auto new_next_free = (uint64_t)(fresh * interval);

    next_free += new_next_free;
    stored_permits -= stored;

    return true;
  }

  IpRateLimiter::IpRateLimiter(double permits_per_second, uint32_t max_permits, size_t max_ips)
    : by_ip([permits_per_second, max_permits](std::string_view)->RateLimiter{ return RateLimiter(permits_per_second, max_permits); }, max_ips) {}

  auto IpRateLimiter::acquire_or_throw(std::string_view ip, uint32_t count) -> void {
    auto handle = by_ip[ip];
    if (!handle.value().try_acquire(count)) throw ApiError("Rate limited, try again later", 429);
  }
}
