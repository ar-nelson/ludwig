#pragma once
#include <util/common.h++>
#include <spdlog/fmt/chrono.h>
#include <simdjson.h>

// util.h clearly wasn't meant to be accessed like this, so this undef is
// necessary to build with musl because it no longer has the config
// information from cmake and doesn't know that musl doesn't define strtoull_l.
#undef FLATBUFFERS_LOCALE_INDEPENDENT
#include <flatbuffers/util.h>

namespace Ludwig {

static inline auto pad_json_string(std::string& str) -> void {
  static constexpr ConstArray<char, simdjson::SIMDJSON_PADDING> PADDING_BYTES(' ');
  static constexpr std::string_view JSON_STRING_PADDING(PADDING_BYTES.arr, simdjson::SIMDJSON_PADDING);
  const auto len = str.length();
  str += JSON_STRING_PADDING;
  str.resize(len);
}

template <typename T> struct JsonSerialize;
template<> struct JsonSerialize<uint64_t> {
  static auto to_json(uint64_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> uint64_t { return value.get_uint64().value(); }
};
template<> struct JsonSerialize<int64_t> {
  static auto to_json(int64_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> int64_t { return value.get_int64().value(); }
};
template<> struct JsonSerialize<uint32_t> {
  static auto to_json(uint32_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> uint32_t { return (uint32_t)value.get_uint64().value(); }
};
template<> struct JsonSerialize<int32_t> {
  static auto to_json(int32_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> int32_t { return (int32_t)value.get_int64().value(); }
};
template<> struct JsonSerialize<uint16_t> {
  static auto to_json(uint16_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> uint16_t { return (uint16_t)value.get_uint64().value(); }
};
template<> struct JsonSerialize<int16_t> {
  static auto to_json(int16_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> int16_t { return (int16_t)value.get_int64().value(); }
};
template<> struct JsonSerialize<uint8_t> {
  static auto to_json(uint8_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> uint8_t { return (uint8_t)value.get_uint64().value(); }
};
template<> struct JsonSerialize<int8_t> {
  static auto to_json(int8_t v, std::string& out) { out += std::to_string(v); }
  static auto from_json(simdjson::ondemand::value value) -> int8_t { return (int8_t)value.get_int64().value(); }
};
template <> struct JsonSerialize<bool> {
  static auto to_json(bool v, std::string& out) { out += v ? "true" : "false"; }
  static auto from_json(simdjson::ondemand::value value) -> bool { return value.get_bool().value(); }
};
template <> struct JsonSerialize<double> {
  static auto to_json(double v, std::string& out) { out += flatbuffers::NumToString(v); }
  static auto from_json(simdjson::ondemand::value value) -> double { return value.get_double().value(); }
};
template <> struct JsonSerialize<Timestamp> {
  static auto to_json(Timestamp v, std::string& out) {
    fmt::format_to(std::back_inserter(out), "\"{:%FT%TZ}\"", fmt::gmtime(v));
  }
  static auto from_json(simdjson::ondemand::value value) -> Timestamp {
    std::string str(std::string(value.get_string().value()));
    std::istringstream ss(str);
    std::tm t = {};
    ss >> std::get_time(&t, "%Y-%b-%dT%H:%M:%SZ");
    if (ss.fail()) throw std::runtime_error("Not a timestamp: " + str);
    return std::chrono::system_clock::from_time_t(std::mktime(&t));
  }
};
template <> struct JsonSerialize<std::string_view> {
  static auto to_json(std::string_view v, std::string& out) {
    if (!flatbuffers::EscapeString(v.data(), v.length(), &out, false, true)) {
      throw std::runtime_error("Cannot write non-UTF-8 string data as JSON");
    }
  }
  static auto from_json(simdjson::ondemand::value value) -> std::string_view {
    return value.get_string().value();
  }
};
template <> struct JsonSerialize<std::string> {
  static auto to_json(const std::string& v, std::string& out) {
    if (!flatbuffers::EscapeString(v.c_str(), v.length(), &out, false, true)) {
      throw std::runtime_error("Cannot write non-UTF-8 string data as JSON");
    }
  }
  static auto from_json(simdjson::ondemand::value value) -> std::string {
    return std::string(value.get_string().value());
  }
};
template <> struct JsonSerialize<SecretString> {
  static auto to_json(const SecretString& v, std::string& out) {
    JsonSerialize<std::string>::to_json(v.data, out);
  }
  static auto from_json(simdjson::ondemand::value value) -> SecretString {
    return SecretString(value.get_string().value());
  }
};
template <typename T> struct JsonSerialize<std::optional<T>> {
  static auto to_json(const std::optional<T>& v, std::string& out) {
    if (v) JsonSerialize<T>::to_json(*v, out); else out += "null";
  }
  static auto from_json(simdjson::ondemand::value value) -> std::optional<T> {
    if (value.is_null().value()) return {};
    return std::optional(JsonSerialize<T>::from_json(value));
  }
};
template <typename T> struct JsonSerialize<std::vector<T>> {
  static auto to_json(const std::vector<T>& v, std::string& out) {
    out += "[";
    for (size_t i = 0; i < v.size(); i++) {
      if (i) out += ",";
      JsonSerialize<T>::to_json(v[i], out);
    }
    out += "]";
  }
  static auto from_json(simdjson::ondemand::value value) -> std::vector<T> {
    std::vector<T> out;
    for (auto v : value.get_array().value()) {
      out.push_back(JsonSerialize<T>::from_json(v.value()));
    }
    return out;
  }
};

template <typename T> struct JsonEntrySerialize {
  static auto to_json_entry(std::string_view key, const T& v, bool comma, std::string& out) -> bool {
    out += comma ? ",\"" : "\"";
    out += key;
    out += "\":";
    JsonSerialize<T>::to_json(v, out);
    return true;
  }
  static auto from_json_entry(std::string_view key, simdjson::ondemand::object object) -> T {
    return JsonSerialize<T>::from_json(object[key].value());
  }
};
template <typename T> struct JsonEntrySerialize<std::optional<T>> {
  static auto to_json_entry(std::string_view key, const std::optional<T>& v, bool comma, std::string& out) -> bool {
    if (!v) return false;
    out += comma ? ",\"" : "\"";
    out += key;
    out += "\":";
    JsonSerialize<T>::to_json(*v, out);
    return true;
  }
  static auto from_json_entry(std::string_view key, simdjson::ondemand::object object) -> std::optional<T> {
    const auto key_result = object[key];
    if (key_result.error()) return {};
    return JsonSerialize<std::optional<T>>::from_json(key_result.value_unsafe());
  }
};
template <typename T> struct JsonEntrySerialize<std::vector<T>> {
  static auto to_json_entry(std::string_view key, const std::vector<T>& v, bool comma, std::string& out) -> bool {
    out += comma ? ",\"" : "\"";
    out += key;
    out += "\":";
    JsonSerialize<std::vector<T>>::to_json(v, out);
    return true;
  }
  static auto from_json_entry(std::string_view key, simdjson::ondemand::object object) -> std::vector<T> {
    const auto key_result = object[key];
    if (key_result.error()) return {};
    return JsonSerialize<std::vector<T>>::from_json(key_result.value_unsafe());
  }
};

}
