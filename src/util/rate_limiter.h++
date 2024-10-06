#pragma once
#include "util/asio_common.h++"
#include <concurrent_lru_cache.h>

namespace Ludwig {
  // Based on https://github.com/mfycheng/ratelimiter, Apache 2.0 license
  class RateLimiter {
  private:
    double interval, max_permits, stored_permits;
    std::chrono::microseconds next_free;
    auto claim_next(uint32_t permits) noexcept -> std::chrono::microseconds;
  public:
    RateLimiter() : interval(0), max_permits(0), stored_permits(0), next_free(0) {}
    RateLimiter(double permits_per_second, uint32_t max_permits);
    auto acquire_or_block(uint32_t permits = 1) -> void {
      if (permits <= 0) throw std::runtime_error("RateLimiter: Must request positive amount of permits");
      std::this_thread::sleep_for(claim_next(permits));
    }
    auto acquire_or_asio_await(uint32_t permits = 1) -> Async<void> {
      if (permits <= 0) throw std::runtime_error("RateLimiter: Must request positive amount of permits");
      const auto wait = claim_next(permits);
      if (wait == std::chrono::microseconds::zero()) co_return;
      asio::steady_timer timer(co_await asio::this_coro::executor, wait);
      co_await timer.async_wait(asio::deferred);
    }
    auto try_acquire(uint32_t permits = 1) -> bool {
      if (permits <= 0) throw std::runtime_error("RateLimiter: Must request positive amount of permits");
      using namespace std::chrono;
      const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());
      if (next_free > now) return false;
      const auto wait = claim_next(permits);
      assert(wait == microseconds::zero());
      return true;
    }
    auto try_acquire_or_block(std::chrono::steady_clock::duration timeout, uint32_t permits = 1) -> bool {
      using namespace std::chrono;
      const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());
      if (next_free > now + timeout) return false;
      acquire_or_block(permits);
      return true;
    }
    auto try_acquire_or_asio_await(std::chrono::steady_clock::duration timeout, uint32_t permits = 1) -> Async<bool> {
      using namespace std::chrono;
      const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());
      if (next_free > now + timeout) co_return false;
      co_await acquire_or_asio_await(permits);
      co_return true;
    }

    friend class KeyedRateLimiter;
  };

  // Unlike RateLimiter, this one is thread-safe
  class KeyedRateLimiter {
  private:
    tbb::concurrent_lru_cache<std::string, RateLimiter, std::function<RateLimiter (std::string_view)>> by_key;
  public:
    KeyedRateLimiter(double permits_per_second, uint32_t max_permits, size_t max_keys = 65536)
      : by_key([permits_per_second, max_permits](std::string_view)->RateLimiter{ return RateLimiter(permits_per_second, max_permits); }, max_keys) {}
    auto try_acquire(std::string key, uint32_t permits = 1) -> bool {
      auto handle = by_key[key];
      return handle.value().try_acquire(permits);
    }
    auto try_acquire_or_block(std::string key, std::chrono::steady_clock::duration timeout, uint32_t permits = 1) -> bool {
      using namespace std::chrono;
      if (permits <= 0) throw std::runtime_error("KeyedRateLimiter: Must request positive amount of permits");
      microseconds wait;
      {
        auto handle = by_key[key];
        const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());
        if (handle.value().next_free > now + timeout) return false;
        wait = handle.value().claim_next(permits);
      }
      std::this_thread::sleep_for(wait);
      return true;
    }
    auto try_acquire_or_asio_await(std::string key, std::chrono::steady_clock::duration timeout, uint32_t permits = 1) -> Async<bool> {
      using namespace std::chrono;
      if (permits <= 0) throw std::runtime_error("KeyedRateLimiter: Must request positive amount of permits");
      microseconds wait;
      {
        auto handle = by_key[key];
        const auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch());
        if (handle.value().next_free > now + timeout) co_return false;
        wait = handle.value().claim_next(permits);
        if (wait == microseconds::zero()) co_return true;
      }
      asio::steady_timer timer(co_await asio::this_coro::executor, wait);
      co_await timer.async_wait(asio::deferred);
      co_return true;
    }
  };
}
