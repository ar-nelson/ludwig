#pragma once
#include "util/common.h++"

namespace Ludwig {

  enum class Event : uint8_t {
    SiteUpdate,
    UserUpdate,
    UserStatsUpdate,
    LocalUserUpdate,
    UserDelete,
    BoardUpdate,
    BoardStatsUpdate,
    LocalBoardUpdate,
    BoardDelete,
    ThreadFetchLinkCard,
    ThreadUpdate,
    ThreadDelete,
    CommentUpdate,
    CommentDelete,
    PostStatsUpdate,
    MAX
  };

  class EventBus : public std::enable_shared_from_this<EventBus> {
    protected:
      virtual auto unsubscribe(uint64_t event_id, std::pair<Event, uint64_t> key) -> void = 0;
    public:
      using Callback = uWS::MoveOnlyFunction<void (Event, uint64_t)>;

      class Subscription {
      private:
        std::weak_ptr<EventBus> bus;
        uint64_t id;
        std::pair<Event, uint64_t> key;
      public:
        Subscription(std::shared_ptr<EventBus> bus, uint64_t id, std::pair<Event, uint64_t> key)
          : bus(bus), id(id), key(key) {}
        Subscription(const Subscription&) = delete;
        auto operator=(const Subscription&) = delete;
        Subscription(Subscription&& from) : bus(from.bus), id(from.id) {
          from.bus.reset();
        }
        inline auto operator=(Subscription&& from) noexcept -> Subscription& {
          bus = from.bus;
          id = from.id;
          from.bus.reset();
          return *this;
        }
        ~Subscription() {
          if (auto bus_ptr = bus.lock()) bus_ptr->unsubscribe(id, key);
        }
      };

      virtual ~EventBus() = default;

      virtual auto dispatch(Event event, uint64_t subject_id = 0) -> void = 0;
      inline auto on_event(Event event, Callback&& callback) -> Subscription {
        return on_event(event, 0, std::move(callback));
      }
      virtual auto on_event(Event event, uint64_t subject_id, Callback&& callback) -> Subscription = 0;
  };

  class DummyEventBus : public EventBus {
    protected:
      inline auto unsubscribe(uint64_t, std::pair<Event, uint64_t>) -> void {};
    public:
      inline auto dispatch(Event, uint64_t = 0) -> void {}
      inline auto on_event(Event e, uint64_t s, Callback&&) -> Subscription { return Subscription(this->shared_from_this(), 0, {e,s}); }
  };
}
