#pragma once
#include "util/json.h++"

namespace Ludwig {
  namespace Lemmy {
#  define XBEGIN(name) struct name {
#  define X(field, type) type field;
#  define XEND };
#  include "lemmy_api_types.x.h++"
  }

#  ifdef XNAMESPACE
#    undef XNAMESPACE
#  endif
#  define XNAMESPACE() Lemmy::
#  include "util/to_json.x.h++"
#  include "lemmy_api_types.x.h++"

#  include "util/from_json.x.h++"
#  include "lemmy_api_types.x.h++"
#  undef XNAMESPACE
}
