#pragma once
#include "util/common.h++"
#include "services/event_bus.h++"
#include <map>
#include <shared_mutex>
#include <asio.hpp>

namespace Ludwig {
  class AsioEventBus;

  struct EventListener {
    uint64_t id, subject_id;
    Event event;
    EventBus::Callback callback;

    EventListener(
      uint64_t id,
      Event event,
      uint64_t subject_id,
      EventBus::Callback&& callback
    ) : id(id), subject_id(subject_id), event(event), callback(std::move(callback)) {}
  };

  struct EventListenerInstance {
    std::weak_ptr<EventListener> listener;
    uint64_t subject_id;

    inline auto operator()() -> void {
      if (auto ptr = listener.lock()) ptr->callback(ptr->event, subject_id);
    }
  };

  class AsioEventBus : public EventBus {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::executor_work_guard<decltype(io->get_executor())> work;
    std::shared_mutex listener_lock;
    uint64_t next_event_id = 0;
    std::multimap<std::pair<Event, uint64_t>, std::shared_ptr<EventListener>> event_listeners;

  protected:
    auto unsubscribe(uint64_t event_id, std::pair<Event, uint64_t> key) -> void;

  public:
    AsioEventBus(std::shared_ptr<asio::io_context> io) : io(io), work(io->get_executor()) {};

    auto dispatch(Event event, uint64_t subject_id = 0) -> void;
    auto on_event(Event event, uint64_t subject_id, Callback&& callback) -> Subscription;
    auto on_event_async(Event event, uint64_t subject_id, CoroCallback&& callback) -> Subscription;
  };
}
