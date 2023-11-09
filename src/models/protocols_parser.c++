#include "protocols_parser.h++"
#include "models/protocols.fbs.h++"

namespace Ludwig {
  auto get_protocols_parser() -> flatbuffers::Parser& {
    static thread_local flatbuffers::Parser parser;
    static thread_local bool parser_ready = false;
    if (!parser_ready) {
      parser.Parse(std::string(protocols_fbs_str()).c_str(), nullptr, "protocols.fbs");
      parser_ready = true;
    }
    return parser;
  }
}
