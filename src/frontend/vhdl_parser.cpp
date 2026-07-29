// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

ParseResult parse_vhdl(SourceText source) {
  return VhdlParser(lex(std::move(source), Language::Vhdl2008)).run();
}

}  // namespace fsim::frontend
