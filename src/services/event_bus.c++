#include "controller_with_events.h++"

namespace Ludwig {

  ControllerWithEvents::ControllerWithEvents(std::shared_ptr<DB> db, std::shared_ptr<asio::io_context> io)
    : Controller(db), io(io), work(io->get_executor()) {}

  auto ControllerWithEvents::dispatch_event(Event event, uint64_t subject_id) -> void {
    std::shared_lock<std::shared_mutex> lock(listener_lock);
    if (event == Event::SiteUpdate) subject_id = 0;
    auto range = event_listeners.equal_range({ event, subject_id });
    for (auto i = range.first; i != range.second; i++) {
      io->dispatch((*i).second);
    }
  }

  auto ControllerWithEvents::on_event(Event event, uint64_t subject_id, EventCallback&& callback) -> EventSubscription {
    std::unique_lock<std::shared_mutex> lock(listener_lock);
    auto id = next_event_id++;
    event_listeners.emplace(std::pair(event, subject_id), EventListener(id, event, subject_id, std::move(callback)));
    return EventSubscription(std::dynamic_pointer_cast<ControllerWithEvents>(shared_from_this()), id, event, subject_id);
  }

  EventSubscription::~EventSubscription() {
    if (auto ctrl = controller.lock()) {
      std::unique_lock<std::shared_mutex> lock(ctrl->listener_lock);
      auto range = ctrl->event_listeners.equal_range(key);
      ctrl->event_listeners.erase(std::find_if(range.first, range.second, [this](auto p) {
        return p.second.id == this->id;
      }));
    }
  }
}
