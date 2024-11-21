#pragma once
#include "util/common.h++"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace Ludwig {
  template <typename T> using Async = asio::awaitable<T, asio::io_context::executor_type>;
  template <typename ...Ts> using Chan = asio::experimental::channel<asio::io_context::executor_type, void(asio::error_code, Ts...)>;
  template <typename ...Ts> using ConcurrentChan = asio::experimental::concurrent_channel<asio::io_context::executor_type, void(asio::error_code, Ts...)>;

  template <typename T = std::monostate> class CacheChan {
  private:
    ConcurrentChan<T> chan;
  public:
    CacheChan(asio::io_context& io) : chan(io.get_executor()) {}

    inline auto get() -> Async<T> {
      auto t = co_await chan.async_receive(asio::deferred);
      chan.async_send({}, t, asio::detached);
      co_return t;
    }

    inline auto set(T&& new_value) -> void {
      chan.async_send({}, std::forward<T>(new_value), asio::detached);
    }
  };

  class AsioThreadPool {
  public:
    std::shared_ptr<asio::io_context> io;
  private:
    std::vector<std::thread> threads;
    asio::executor_work_guard<asio::io_context::executor_type> work;
    bool done = false;
  public:
    AsioThreadPool(size_t thread_count = std::thread::hardware_concurrency())
      : io(std::make_shared<asio::io_context>()), work(io->get_executor())
    {
      threads.reserve(thread_count);
      for (size_t i = 0; i < thread_count; i++) {
        threads.emplace_back([io = io]() { io->run(); });
      }
    }
    ~AsioThreadPool() { stop(); }
    auto stop() -> void {
      if (done) return;
      done = true;
      work.reset();
      io->stop();
      for (auto& th : threads) if (th.joinable()) th.join();
    }
    auto post(uWS::MoveOnlyFunction<void()>&& task) -> void {
      asio::post(*io, std::move(task));
    }
  };
  
  template <typename C>
  static inline auto asio_completable(std::shared_ptr<C> c) -> Async<typename C::completion_type> {
    ConcurrentChan<std::optional<typename C::completion_type>> chan(co_await asio::this_coro::executor);
    c->on_complete([&](typename C::completion_type t) {
      chan.async_send(asio::error_code(), {std::move(t)}, asio::detached);
    });
    co_return *co_await chan.async_receive(asio::deferred);
  }
}
