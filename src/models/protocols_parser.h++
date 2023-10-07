#pragma once
#include <flatbuffers/idl.h>

namespace Ludwig {
  // Returns a thread-local instance of Parser with protocols.fbs loaded.
  // Careful: The returned reference is global!
  auto get_protocols_parser() -> flatbuffers::Parser&;
}
