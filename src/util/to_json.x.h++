#ifndef XNAMESPACE
#  define XNAMESPACE()
#endif

#define XBEGIN(NAME) \
  template<> struct JsonSerialize<XNAMESPACE()NAME> { \
    static auto from_json(simdjson::ondemand::value) -> XNAMESPACE()NAME; \
    static auto to_json(const XNAMESPACE()NAME& v, std::string& out) -> void { \
      out += "{"; \
      bool comma = false;

#define X(FIELD, TYPE) \
      if (JsonEntrySerialize<TYPE>::to_json_entry(#FIELD, v.FIELD, comma, out)) comma = true;

#define XEND \
      out += "}"; \
    } \
  };
