#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <functional>
#include <limits>
#include <list>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <libxml/parser.h>
#include <asio.hpp>
#include <uWebSockets/MoveOnlyFunction.h>

namespace Ludwig {
  constexpr uint64_t ID_MAX = std::numeric_limits<uint64_t>::max();

  template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
  template<class... Ts> overload(Ts...) -> overload<Ts...>;

  template<typename T> static inline auto to_ascii_lowercase(T in) -> std::string {
    std::string out;
    out.reserve(in.size());
    std::transform(in.begin(), in.end(), std::back_inserter(out), [](auto c){ return std::tolower(c); });
    return out;
  }

  static inline auto now_s() -> uint64_t {
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
    );
  }

  // Based on https://stackoverflow.com/a/53526139/548027
  struct Url {
  private:
    static const inline char
      *SCHEME_REGEX   = "(?:(\\w+)://)",      // mandatory, match protocol before the ://
      *USER_REGEX     = "(?:([^@/:\\s]+)@)?", // match anything other than @ / : or whitespace before the ending @
      *HOST_REGEX     = "([^@/:\\s]+)",       // mandatory. match anything other than @ / : or whitespace
      *PORT_REGEX     = "(?::([0-9]{1,5}))?", // after the : match 1 to 5 digits
      *PATH_REGEX     = "(/[^:#?\\s]*)?",     // after the / match anything other than : # ? or whitespace
      *QUERY_REGEX    = "(\\?(?:(?:[^?;&#=]+(?:=[^?;&#=]*)?)(?:[;|&](?:[^?;&#=]+(?:=[^?;&#=]*)?))*))?", // after the ? match any number of x=y pairs, seperated by & or ;
      *FRAGMENT_REGEX = "(?:#([^#\\s]*))?";   // after the # match anything other than # or whitespace
    static const inline auto regex = std::regex(std::string("^")
      + SCHEME_REGEX + USER_REGEX
      + HOST_REGEX + PORT_REGEX
      + PATH_REGEX + QUERY_REGEX
      + FRAGMENT_REGEX + "$"
    );
  public:
    std::string scheme, user, host, port, path, query, fragment;

    static auto parse(std::string str) noexcept -> std::optional<Url> {
      std::smatch match;
      if (!std::regex_match(str, match, regex)) return {};
      return {{
        match[1].str(), match[2].str(), match[3].str(), match[4].str(),
        match[5].str(), match[6].str(), match[7].str()
      }};
    }

    inline auto is_http_s() const noexcept -> bool {
      return scheme == "http" || scheme == "https";
    }

    inline auto to_string() const noexcept -> std::string {
      return fmt::format("{}://{}{}{}{}{}{}{}{}",
        scheme,
        user.empty() ? "" : user,
        user.empty() ? "" : "@",
        host,
        port.empty() ? "" : ":",
        port.empty() ? "" : port,
        path,
        query,
        fragment.empty() ? "" : "#",
        fragment.empty() ? "" : fragment
      );
    }
  };

    template <typename T = std::monostate> class AsyncCell {
    private:
      using Handles = std::list<uWS::MoveOnlyFunction<void()>>;
      struct State {
        std::mutex mutex;
        std::variant<Handles, T> v;
      };
      std::shared_ptr<State> state = std::make_shared<State>();
    public:
      template<asio::completion_token_for<void(T&)> CompletionToken>
      inline auto async_get(CompletionToken&& token) const {
        auto init = [&](asio::completion_handler_for<void(T&)> auto handler) {
          auto complete = [state = std::weak_ptr(state), handler = std::move(handler)] mutable {
            auto work = asio::make_work_guard(handler);
            auto alloc = asio::get_associated_allocator(handler, asio::recycling_allocator<void>());
            asio::dispatch(work.get_executor(),
              asio::bind_allocator(alloc, [state = state, handler = std::move(handler)] mutable {
                if (auto s = state.lock()) std::move(handler)(std::get<1>(s->v));
              })
            );
          };
          std::lock_guard<std::mutex> lock(state->mutex);
          if (auto* handles = std::get_if<Handles>(&state->v)) handles->push_back(std::move(complete));
          else complete();
        };
        return asio::async_initiate<CompletionToken, void(T&)>(init, token);
      }

      inline auto set(T new_value) -> void {
        Handles handles;
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (auto* existing_handles = std::get_if<Handles>(&state->v)) {
            std::swap(handles, *existing_handles);
            state->v = new_value;
          } else return;
        }
        for (auto& handle : handles) std::move(handle)();
      }
    };
}
