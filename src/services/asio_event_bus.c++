#include "asio_event_bus.h++"

using std::make_shared, std::pair, std::shared_lock, std::shared_mutex,
    std::unique_lock;

namespace Ludwig {

  auto AsioEventBus::dispatch(Event event, uint64_t subject_id) -> void {
    shared_lock<shared_mutex> lock(listener_lock);
    if (event == Event::SiteUpdate) subject_id = 0;
    auto range = event_listeners.equal_range({ event, 0 });
    for (auto i = range.first; i != range.second; i++) {
      io->dispatch(EventListenerInstance{i->second, subject_id});
    }
    if (subject_id) {
      auto range = event_listeners.equal_range({ event, subject_id });
      for (auto i = range.first; i != range.second; i++) {
        io->dispatch(EventListenerInstance{i->second, subject_id});
      }
    }
  }

  auto AsioEventBus::on_event(Event event, uint64_t subject_id, EventCallback&& callback) -> Subscription {
    unique_lock<shared_mutex> lock(listener_lock);
    auto id = next_event_id++;
    const auto key = pair(event, subject_id);
    event_listeners.emplace(key, make_shared<EventListener>(id, event, subject_id, std::move(callback)));
    return Subscription(shared_from_this(), id, {event, subject_id});
  }

  auto AsioEventBus::unsubscribe(uint64_t event_id, pair<Event, uint64_t> key) -> void {
    unique_lock<shared_mutex> lock(listener_lock);
    auto range = event_listeners.equal_range(key);
    event_listeners.erase(std::find_if(range.first, range.second, [event_id](auto p) {
      return p.second->id == event_id;
    }));
  }
}
