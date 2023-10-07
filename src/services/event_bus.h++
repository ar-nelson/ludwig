#pragma once
#include <map>
#include <shared_mutex>
#include <asio.hpp>
#include "controller.h++"

namespace Ludwig {

  class ControllerWithEvents;

  typedef std::function<auto (Event, uint64_t) -> void> EventCallback;

  struct EventListener {
    uint64_t id, subject_id;
    Event event;
    EventCallback callback;

    EventListener(
      uint64_t id,
      Event event,
      uint64_t subject_id,
      EventCallback&& callback
    ) : id(id), subject_id(subject_id), event(event), callback(std::move(callback)) {}

    void operator()() {
      callback(event, subject_id);
    }
  };

  class EventSubscription {
  private:
    std::weak_ptr<ControllerWithEvents> controller;
    uint64_t id;
    std::pair<Event, uint64_t> key;
  public:
    EventSubscription(
      std::shared_ptr<ControllerWithEvents> controller,
      uint64_t id,
      Event event,
      uint64_t subject_id
    ) : controller(controller), id(id), key(event, subject_id) {}
    ~EventSubscription();
  };

  class ControllerWithEvents : public Controller {
  private:
    std::shared_ptr<asio::io_context> io;
    asio::executor_work_guard<decltype(io->get_executor())> work;
    std::shared_mutex listener_lock;
    uint64_t next_event_id = 0;
    std::multimap<std::pair<Event, uint64_t>, EventListener> event_listeners;

    auto dispatch_event(Event event, uint64_t subject_id = 0) -> void;
  public:
    ControllerWithEvents(std::shared_ptr<DB> db, std::shared_ptr<asio::io_context> io);

    auto on_event(Event event, uint64_t subject_id, EventCallback&& callback) -> EventSubscription;

    friend class EventSubscription;
  };
}
