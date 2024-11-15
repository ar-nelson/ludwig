#pragma once
#include "asio/this_coro.hpp"
#include "db/db.h++"
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
  
  template <typename T, typename Fn>
  static inline auto asio_callback_awaiter(Fn fn) -> Async<T> {
    ConcurrentChan<T> chan(co_await asio::this_coro::executor);
    fn([&](T&& t) {
      chan.async_send(asio::error_code(), std::move(t), asio::detached);
    });
    co_return co_await chan.async_receive(asio::deferred);
  }

  static inline auto open_write_txn_async_asio(DB& db, WritePriority priority = WritePriority::Medium) -> Async<WriteTxn> {
    std::optional<WriteTxn> txn;
    ConcurrentChan<> chan(co_await asio::this_coro::executor);
    if (db.open_write_txn_async([&](WriteTxn _txn, bool async) {
        txn.emplace(std::move(_txn));
        if (async) chan.async_send(asio::error_code(), asio::detached);
      }, priority)) {
      co_await chan.async_receive(asio::deferred);
    }
    co_return std::move(txn.value());
  }
}
