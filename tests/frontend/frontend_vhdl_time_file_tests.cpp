// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_code(
    const ParseResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [&](const auto& diagnostic) {
        return diagnostic.code == code;
      });
}

std::string diagnostics_text(const ParseResult& result) {
  std::string text{"the complete Batch 119 frontend surface must parse"};
  for (const auto& diagnostic : result.diagnostics) {
    text += "\n" + diagnostic.code + ": " + diagnostic.message;
  }
  return text;
}

} // namespace

void test_vhdl_batch119_retained_surface() {
  const auto parsed = parse_text(
      "batch119_surface.vhd",
      R"(
package io_types is
  type integer_file is file of integer;
  type text_file is file of string;
  file package_log : text_file open append_mode is "package.log";
  procedure read_next(
    file source : text_file;
    variable value : out integer);
  impure function exhausted(
    file source : text_file) return boolean;
end package;

use work.io_types.all;
architecture rtl of batch119_surface is
  constant period : time := 10 ns;
  file direct : integer_file;
  file primary, mirror : text_file open read_mode is input_name;
  signal clk, ready, result : boolean;
begin
  worker: process
    file local_output : text_file open write_mode is "output.txt";
    variable line_buffer : line;
  begin
    if ready then
      wait on clk until ready for period / 2;
    else
      wait until exhausted(primary);
    end if;
    while ready loop
      assert ready
        report prefix & " mismatch"
        severity chosen_severity;
      report prefix & " complete" severity warning;
      file_open(status, primary, input_name, read_mode);
      readline(primary, line_buffer);
      read_next(primary, count);
      write(line_buffer, ready);
      writeline(output, line_buffer);
      if endfile(primary) then
        report "end of file";
      end if;
      file_close(primary);
      exit;
    end loop;
    result <= reject reject_width inertial
      true after period, false after period + 5 ns;
    result <= transport false after 1 us;
    wait until not ready for 2 us;
  end process;
  scoped_io: block
    file block_log : text_file open append_mode is "block.log";
  begin
  end block scoped_io;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), diagnostics_text(parsed));

  const auto* package =
      parsed.design.find(UnitKind::VhdlPackage, "io_types");
  require(
      package != nullptr && package->type_aliases.size() == 2
          && package->variables.size() == 1
          && package->procedures.size() == 1
          && package->functions.size() == 1,
      "VHDL file types and TextIO-like profiles are retained");
  require(
      package->variables.front().vhdl_file
          && package->variables.front().vhdl_file_open_kind
          && package->variables.front().initializer,
      "package file object open information is retained");
  require(
      package->type_aliases[0].declaration_kind
              == TypeDeclarationKind::VhdlFile
          && package->type_aliases[0].type.vhdl_file
          && package->type_aliases[0]
                 .type.vhdl_file->element_types.size()
              == 1
          && package->type_aliases[0]
                 .type.vhdl_file->element_types.front().domain
              == ValueDomain::Integer
          && package->type_aliases[1].type.vhdl_file
          && package->type_aliases[1]
                 .type.vhdl_file->element_types.front().domain
              == ValueDomain::String,
      "file element subtypes and nominal declaration kinds");
  require(
      package->procedures.front().arguments.front().object_class
              == InterfaceObjectClass::File
          && package->procedures.front().arguments[1].object_class
              == InterfaceObjectClass::Variable
          && package->functions.front().arguments.front().vhdl_file,
      "file interface object classes survive subprogram profiles");

  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr && architecture->variables.size() == 3
          && architecture->variables[0].vhdl_file
          && !architecture->variables[0].initializer
          && architecture->variables[1].vhdl_file_open_kind
          && architecture->variables[1].vhdl_file_open_kind->text
              == "read_mode"
          && architecture->variables[1].initializer
          && architecture->variables[1].initializer->text
              == "input_name"
          && architecture->variables[2].initializer,
      "file objects retain open kind, logical name, and shared declaration span");
  require(
      architecture->parameters.size() == 1
          && architecture->parameters.front().type.domain
              == ValueDomain::Integer
          && architecture->parameters.front().type.nominal_type
              == "@builtin:time"
          && architecture->parameters.front().type.packed_range
          && architecture->parameters.front().type.packed_range->left == 63
          && architecture->parameters.front().type.packed_range->right == 0
          && architecture->parameters.front().type.integer_range
          && architecture->parameters.front().type.integer_range->left == 0
          && architecture->parameters.front().type.integer_range->right
              == std::numeric_limits<std::int64_t>::max()
          && architecture->parameters.front().default_value.kind
              == ExpressionKind::Call
          && architecture->parameters.front().default_value.text
              == "@vhdl-physical:ns",
      "physical time literals remain exact expression nodes");

  const auto& process = architecture->processes.front();
  require(
      process.variables.size() == 2
          && process.variables.front().vhdl_file
          && process.variables.front().vhdl_file_open_kind
          && process.variables.front().initializer
          && process.variables.front().initializer->kind
              == ExpressionKind::StringLiteral,
      "process-local file object and ordinary local variable coexist");
  const auto& branch = process.statements[0];
  require(
      branch.kind == StatementKind::If
          && branch.statements.front().kind
              == StatementKind::WaitUntil
          && branch.statements.front().sensitivities.size() == 1
          && branch.statements.front().delay
          && branch.statements.front().delay->expression
          && branch.statements.front().delay->expression->kind
              == ExpressionKind::Binary
          && branch.statements.front().delay->expression->text == "/"
          && branch.else_statements.front().kind
              == StatementKind::WaitUntil,
      "nested waits retain sensitivity, condition, and physical-time expression");

  const auto& loop = process.statements[1];
  require(
      loop.kind == StatementKind::Loop
          && loop.statements.size() == 10
          && loop.statements[0].kind == StatementKind::Assert
          && loop.statements[0].vhdl_report_expression.kind
              == ExpressionKind::Binary
          && loop.statements[0].vhdl_report_expression.text == "&"
          && loop.statements[0].vhdl_severity_expression.text
              == "chosen_severity"
          && loop.statements[1].kind == StatementKind::Report
          && loop.statements[1].vhdl_report_expression.kind
              == ExpressionKind::Binary
          && loop.statements[1].assertion_severity
              == AssertionSeverity::Warning
          && loop.statements[2].kind
              == StatementKind::ProcedureCall
          && loop.statements[2].procedure_name == "file_open"
          && loop.statements[3].procedure_name == "readline"
          && loop.statements[6].procedure_name == "writeline"
          && loop.statements[7].condition.kind
              == ExpressionKind::Call
          && loop.statements[7].condition.text == "endfile"
          && loop.statements[8].procedure_name == "file_close",
      "general reports and TextIO-like calls retain complete expression HIR");

  const auto& rejected = process.statements[2];
  const auto& transported = process.statements[3];
  require(
      rejected.vhdl_delay_mechanism == VhdlDelayMechanism::Inertial
          && rejected.vhdl_rejection_limit
          && rejected.vhdl_rejection_limit->expression
          && rejected.vhdl_rejection_limit->expression->text
              == "reject_width"
          && rejected.vhdl_waveform.size() == 2
          && rejected.vhdl_waveform[0].delay
          && rejected.vhdl_waveform[0].delay->expression
          && rejected.vhdl_waveform[0].delay->expression->text
              == "period"
          && rejected.vhdl_waveform[1].delay
          && rejected.vhdl_waveform[1].delay->expression
          && rejected.vhdl_waveform[1].delay->expression->text == "+"
          && transported.vhdl_delay_mechanism
              == VhdlDelayMechanism::Transport
          && transported.vhdl_waveform.front().delay
          && transported.vhdl_waveform.front().delay->magnitude == 1
          && transported.vhdl_waveform.front().delay->unit == "us"
          && process.statements[4].delay
          && process.statements[4].delay->magnitude == 2
          && process.statements[4].delay->unit == "us",
      "inertial, transport, reject, waveform, and wait time HIR");
  require(
      architecture->generate_regions.size() == 1
          && architecture->generate_regions.front()
                 .then_body.variables.size()
              == 1
          && architecture->generate_regions.front()
                 .then_body.variables.front().vhdl_file,
      "block declarative file objects remain in generated-region HIR");
  require(
      rejected.span.source_name == "batch119_surface.vhd"
          && rejected.span.begin.line == 45
          && package->type_aliases.front().span.begin.line == 3,
      "Batch 119 retained constructs preserve exact source spans");

  const auto malformed = parse_text(
      "batch119_malformed.vhd",
      R"(
package malformed_files is
  type text_file is file of string;
  type missing_of is file integer;
end package;
architecture rtl of malformed_files is
  file missing_kind : text_file open is "input.txt";
  file missing_name : text_file open read_mode;
  file empty_name : text_file is;
  file duplicate : text_file;
  file duplicate : text_file;
begin
  process begin
    report severity note;
    report "message" severity;
    assert true report severity warning;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !malformed.ok()
          && has_code(malformed, "FSIM-VHDL-PARSE-254")
          && has_code(malformed, "FSIM-VHDL-PARSE-257")
          && has_code(malformed, "FSIM-VHDL-PARSE-258")
          && has_code(malformed, "FSIM-VHDL-PARSE-259")
          && has_code(malformed, "FSIM-VHDL-SEM-094")
          && has_code(malformed, "FSIM-VHDL-PARSE-261")
          && has_code(malformed, "FSIM-VHDL-PARSE-262"),
      "malformed file, report, and severity forms have targeted diagnostics");
}

} // namespace fsim::tests::frontend
