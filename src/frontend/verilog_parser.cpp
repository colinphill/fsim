// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

ParseResult parse_verilog(SourceText source, bool system_verilog) {
  const auto language = system_verilog ? Language::SystemVerilog2017
                                       : Language::Verilog2005;
  return VerilogParser(lex(std::move(source), language), system_verilog).run();
}

ParseResult parse_verilog(LexResult lexed, bool system_verilog) {
  return VerilogParser(std::move(lexed), system_verilog).run();
}

}  // namespace fsim::frontend
