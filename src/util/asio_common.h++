#pragma once
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
}
