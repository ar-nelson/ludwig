#include "rate_limiter.h++"
#include "util/web.h++"

namespace Ludwig {
  RateLimiter::RateLimiter(double permits_per_second, uint32_t max_permits)
    : interval(1'000'000.0 / permits_per_second), max_permits(max_permits), stored_permits(0), next_free(0)
  {
    if (permits_per_second <= 0) throw std::runtime_error("RateLimiter: permits_per_second must be > 0");
  }

  // Based on https://github.com/mfycheng/ratelimiter, Apache 2.0 license
  auto RateLimiter::claim_next(uint32_t count) noexcept -> std::chrono::microseconds {
    using namespace std::chrono;
    const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());

    // If we're past the next_free, then recalculate
    // stored permits, and update next_free
    if (now > next_free) {
      stored_permits = std::min(max_permits, stored_permits + (double)(now - next_free).count() / interval);
      next_free = now;
    }

    // Since we synced before hand, this will always be >= 0.
    const auto wait = next_free - now;

    // Determine how many stored and freh permits to consume
    const double permits = (double)count,
      stored = std::min(permits, stored_permits),
      fresh = permits - stored;

    // In the general RateLimiter, stored permits have no wait time,
    // and thus we only have to wait for however many fresh permits we consume
    const microseconds new_next_free((uint64_t)(fresh * interval));

    next_free += new_next_free;
    stored_permits -= stored;

    return wait;
  }
}
