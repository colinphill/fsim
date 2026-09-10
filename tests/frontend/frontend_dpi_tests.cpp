// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

void test_systemverilog_dpi_declaration_ownership() {
  using namespace fsim::frontend;

  const auto parsed = parse_text(
      "dpi-ownership.sv",
      R"(
import "DPI-C" context unit_c_add = function int unit_add(input int lhs, input int rhs);
export "DPI-C" unit_c_notify = task unit_notify;
task unit_notify;
endtask : unit_notify

package dependency_pkg;
  parameter int value = 1;
endpackage : dependency_pkg

package dpi_pkg;
  import "DPI-C" pure function int c_abs(input int value);
  export "DPI-C" function sv_abs;
  import dependency_pkg::*;
  export dependency_pkg::*;
  function int sv_abs(input int value);
    return value;
  endfunction : sv_abs
endpackage : dpi_pkg

interface dpi_if;
  import "DPI-C" function void drive(input int value);
endinterface : dpi_if

program dpi_program;
  export "DPI-C" task program_notify;
  task program_notify;
  endtask : program_notify
endprogram : dpi_program

module dpi_top;
  import "DPI-C" function chandle acquire();
  import "DPI-C" task transfer(input logic [7:0] source[4], output int result, inout int state, int unnamed, input int defaulted = 7);
  export "DPI-C" function release_handle;
  function void release_handle;
  endfunction : release_handle
endmodule : dpi_top
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "DPI ownership source must parse");
  require(
      parsed.design.systemverilog_dpi_declarations.size() == 2,
      "compilation unit owns its DPI import and export");

  const auto dpi_2005 = parse_verilog(
      SourceText { "dpi-2005.sv",
          "import \"DPI-C\" function int c_value();\n" },
      StandardRevision::SystemVerilog2005);
  const auto dpi_in_verilog = parse_verilog(
      SourceText { "dpi-in-verilog.v",
          "import \"DPI-C\" function integer c_value();\n" },
      StandardRevision::Verilog2005);
  require(
      dpi_2005.ok()
          && dpi_2005.design.systemverilog_dpi_declarations.size() == 1
          && dpi_2005.design.systemverilog_dpi_declarations.front()
                  .standard_revision
              == StandardRevision::SystemVerilog2005
          && !dpi_in_verilog.ok()
          && std::ranges::any_of(dpi_in_verilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-351";
              }),
      "DPI starts in SystemVerilog-2005 and cannot leak into Verilog modes");

  const auto& unit_import =
      parsed.design.systemverilog_dpi_declarations.front();
  require(
      unit_import.direction == SystemVerilogDpiDirection::Import
          && unit_import.standard_revision
              == StandardRevision::SystemVerilog2017
          && unit_import.owner_kind
              == SystemVerilogDpiOwnerKind::CompilationUnit
          && unit_import.owner_identity == "$unit"
          && unit_import.link_name == "DPI-C"
          && unit_import.link_name_token
          && unit_import.qualifier == SystemVerilogDpiQualifier::Context
          && unit_import.qualifier_token
          && unit_import.callable_kind
              == SystemVerilogDpiCallableKind::Function
          && unit_import.callable_token
          && unit_import.systemverilog_name == "unit_add"
          && unit_import.systemverilog_name_token
          && unit_import.c_identifier
          && *unit_import.c_identifier == "unit_c_add"
          && unit_import.c_identifier_token
          && unit_import.linkage_name == "unit_c_add"
          && unit_import.validated
          && unit_import.return_type_tokens.size() == 1
          && unit_import.return_type_tokens.front().text == "int"
          && unit_import.formals.size() == 2
          && unit_import.formals.front().direction == PortDirection::Input
          && unit_import.formals.front().name == "lhs"
          && unit_import.formals.front().type_tokens.size() == 1
          && unit_import.formals.front().type_tokens.front().text == "int"
          && !unit_import.formals.front().span.empty()
          && !unit_import.profile_span.empty()
          && unit_import.tokens.front().text == "import"
          && unit_import.tokens.at(1).kind == TokenKind::StringLiteral
          && unit_import.tokens.at(1).text == "\"DPI-C\""
          && unit_import.tokens.back().kind == TokenKind::Semicolon
          && unit_import.span.source_name == "dpi-ownership.sv"
          && !unit_import.span.empty(),
      "compilation-unit DPI tokens, span, direction, and identity are exact");
  const auto& unit_export =
      parsed.design.systemverilog_dpi_declarations.back();
  require(
      unit_export.direction == SystemVerilogDpiDirection::Export
          && unit_export.qualifier == SystemVerilogDpiQualifier::None
          && unit_export.callable_kind
              == SystemVerilogDpiCallableKind::Task
          && unit_export.systemverilog_name == "unit_notify"
          && unit_export.c_identifier
          && *unit_export.c_identifier == "unit_c_notify"
          && unit_export.linkage_name == "unit_c_notify"
          && unit_export.validated
          && unit_export.resolved_profile
          && !unit_export.resolved_profile->return_type
          && unit_export.resolved_profile->formals.empty()
          && !unit_export.resolved_profile->callable_span.empty(),
      "export callable kind, SystemVerilog name, and C alias are structured");

  const auto unit_for = [&](const std::string_view name) {
    return std::ranges::find(parsed.design.units, name, &DesignUnit::name);
  };
  for (const auto name : {"dpi_pkg", "dpi_if", "dpi_program", "dpi_top"}) {
    const auto unit = unit_for(name);
    require(
        unit != parsed.design.units.end()
            && !unit->systemverilog_dpi_declarations.empty(),
        "each supported design-unit kind owns its DPI declaration");
    for (const auto& declaration : unit->systemverilog_dpi_declarations) {
      require(
          declaration.owner_kind
                  == SystemVerilogDpiOwnerKind::DesignUnit
              && declaration.owner_identity == name
              && declaration.tokens.front().text
                  == (declaration.direction
                          == SystemVerilogDpiDirection::Import
                      ? "import"
                      : "export")
              && declaration.tokens.back().kind == TokenKind::Semicolon
              && declaration.span.source_name == "dpi-ownership.sv"
              && !declaration.span.empty(),
          "design-unit DPI ownership and lossless source are exact");
    }
  }
  const auto package = unit_for("dpi_pkg");
  require(
      package->systemverilog_dpi_declarations.size() == 2
          && package->systemverilog_dpi_declarations.front().qualifier
              == SystemVerilogDpiQualifier::Pure
          && package->systemverilog_dpi_declarations.front()
              .systemverilog_name == "c_abs"
          && !package->systemverilog_imports.empty()
          && !package->systemverilog_exports.empty(),
      "DPI recognition does not disturb package import and export clauses");
  const auto module = unit_for("dpi_top");
  const auto task = std::ranges::find_if(
      module->systemverilog_dpi_declarations,
      [](const SystemVerilogDpiDeclaration& declaration) {
        return declaration.systemverilog_name == "transfer";
      });
  require(
      task != module->systemverilog_dpi_declarations.end()
          && task->formals.size() == 5
          && task->formals[0].direction == PortDirection::Input
          && !task->formals[0].type_tokens.empty()
          && task->formals[0].type_tokens.front().text == "logic"
          && task->formals[0].dimension_tokens.size() == 3
          && task->formals[1].direction == PortDirection::Output
          && task->formals[2].direction == PortDirection::Inout
          && task->formals[3].direction == PortDirection::Input
          && task->formals[3].name == "unnamed"
          && task->formals[4].name == "defaulted"
          && task->formals[4].default_tokens.size() == 1
          && task->formals[4].default_tokens.front().text == "7",
      "task directions, packed type, dimensions, and defaults parse");

  const auto revised_profile = parse_verilog(
      SourceText { "dpi-revised-profile.sv", R"(
import "DPI-C" pure aliases = function int left(input int, bit [7:0]);
module dpi_alias_owner;
  import "DPI-C" pure aliases = function int right(input int value, bit [7:0] payload = 8'h2a);
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      revised_profile.ok()
          && revised_profile.design.systemverilog_dpi_declarations.size() == 1
          && revised_profile.design.units.size() == 1
          && revised_profile.design.units.front()
                 .systemverilog_dpi_declarations.size() == 1
          && revised_profile.design.systemverilog_dpi_declarations.front()
                 .formals.front().name.empty()
          && revised_profile.design.units.front()
                 .systemverilog_dpi_declarations.front()
                 .formals.back().default_tokens.size() == 1,
      "2023 DPI aliases retain compatible profiles across owner scopes");

  const auto legacy_pli_composition = parse_verilog(
      SourceText { "sv2023-foreign-composition.sv", R"(
import "DPI-C" context function int dpi_transform(input int value);
module sv2023_foreign_composition;
  int value;
  initial begin
    $fsim_tf_link_probe();
    value = $fsim_tf_function_probe();
    value = dpi_transform(value);
  end
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      legacy_pli_composition.ok()
          && legacy_pli_composition.design
                 .systemverilog_dpi_declarations.size() == 1
          && legacy_pli_composition.design.units.size() == 1
          && legacy_pli_composition.design.units.front().standard_revision
              == StandardRevision::SystemVerilog2023
          && legacy_pli_composition.design.units.front().processes.size() == 1,
      "exact-2023 source retains DPI and registered legacy TF call sites in "
      "one design unit");

  const auto unterminated = parse_text(
      "dpi-unterminated.sv",
      "import \"DPI-C\" function int missing()",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unterminated.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-324";
          }),
      "unterminated DPI declaration has a stable diagnostic");

  const auto invalid = parse_text(
      "dpi-structure-invalid.sv",
      R"(
import "DPI" function int wrong_link();
import "DPI-C" mystery invalid_callable;
import "DPI-C" function int ();
export "DPI-C" pure function invalid_export;
import "DPI-C" pure task invalid_pure_task();
import "DPI-C" pure function int invalid_pure_output(output int value);
import "DPI-C" function int bad_ref(ref int value);
import "DPI-C" function int bad_default(output int value = 1);
import "DPI-C" shared_a = function int alias_a(input int value);
module invalid_scope;
  import "DPI-C" shared_a = function int alias_b(input longint value);
  import "DPI-C" function int duplicate(input int value);
  import "DPI-C" function int duplicate(input int other);
  import "DPI-C" function int native_conflict();
  export "DPI-C" task wrong_kind;
  function int native_conflict;
    return 0;
  endfunction
  function int wrong_kind;
    return 0;
  endfunction
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_code("FSIM-SV-SEM-222")
          && has_code("FSIM-SV-PARSE-325")
          && has_code("FSIM-SV-PARSE-326")
          && has_code("FSIM-SV-SEM-223")
          && has_code("FSIM-SV-SEM-224")
          && has_code("FSIM-SV-SEM-225")
          && has_code("FSIM-SV-SEM-227")
          && has_code("FSIM-SV-SEM-228")
          && has_code("FSIM-SV-SEM-229")
          && has_code("FSIM-SV-SEM-230"),
      "DPI link, callable-kind, and name failures have stable diagnostics");
  require(
      has_code("FSIM-SV-SEM-393"),
      "incompatible declarations sharing a C linkage name are rejected");
}

}  // namespace fsim::tests::frontend
