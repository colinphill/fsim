// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

ParseResult parse_verilog(SourceText source, bool system_verilog) {
  return parse_verilog(
      std::move(source),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(source), standard_revision, "none");
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return VerilogParser(
      lex(std::move(source), language_for_standard_revision(standard_revision)),
      standard_revision, std::string { compatibility_profile }).run();
}

ParseResult parse_verilog(LexResult lexed, bool system_verilog) {
  return parse_verilog(
      std::move(lexed),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(lexed), standard_revision, "none");
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return VerilogParser(std::move(lexed), standard_revision,
      std::string { compatibility_profile }).run();
}

}  // namespace fsim::frontend
