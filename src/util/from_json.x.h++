#ifndef XNAMESPACE
#  define XNAMESPACE()
#endif

#define XBEGIN(NAME) \
  inline auto JsonSerialize<XNAMESPACE()NAME>::from_json(simdjson::ondemand::value v) -> XNAMESPACE()NAME { \
    auto obj = v.get_object().value(); \
    return {

#define X(FIELD, TYPE) .FIELD = JsonEntrySerialize<TYPE>::from_json_entry(#FIELD, obj),

#define XEND \
    }; \
  }
