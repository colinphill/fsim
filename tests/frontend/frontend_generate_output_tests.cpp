// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

void test_conditional_generate_hierarchy() {
  const auto systemverilog = parse_text(
      "generate.sv",
      R"(
module leaf(input logic value, output logic result);
  assign result = value;
endmodule

module generated #(parameter ENABLED = 1) (
  input logic value,
  output logic result
);
  generate
    if (ENABLED) begin : selected
      leaf active(.value(value), .result(result));
      if (ENABLED) begin : nested_selected
        leaf nested_active(.value(value), .result(result));
      end else begin : nested_rejected
        leaf nested_inactive(.value(value), .result(result));
      end
    end else begin : rejected
      leaf inactive(.value(value), .result(result));
    end
  endgenerate
endmodule

module generated_loop #(parameter COUNT = 3) (
  input logic value,
  output logic result
);
  genvar i;
  generate
    for (i = 0; i < COUNT; i++) begin : lane
      logic [3:0] generated_value;
      assign generated_value = i;
      initial generated_value = i + 1;
      leaf child(.value(value), .result(result));
    end
  endgenerate
endmodule

module generated_case #(parameter MODE = 1) (
  input logic value,
  output logic result
);
  generate
    case (MODE)
      0: begin : zero
        leaf child(.value(value), .result(result));
      end
      1, 2: begin : selected
        leaf child(.value(value), .result(result));
      end
      default: begin : fallback
        leaf child(.value(value), .result(result));
      end
    endcase
  endgenerate
endmodule

module implicit_generated #(parameter ENABLED = 1);
  if (ENABLED) begin : implicit_scope
    localparam int BASE = 5;
    parameter int NEXT_VALUE = BASE + 1;
    logic [3:0] generated_value;
    initial generated_value = NEXT_VALUE;
  end
endmodule

module implicit_loop_generated #(parameter COUNT = 2);
  for (genvar i = 0; i < COUNT; i += 1) begin : lane
    logic [3:0] generated_value;
    initial generated_value = i;
  end
endmodule

module descending_loop_generated;
  generate
    for (genvar j = 2; j >= 0; --j) begin : lane
      logic [3:0] generated_value;
      initial generated_value = j;
    end
  endgenerate
endmodule

module compound_subtract_loop_generated;
  for (genvar m = 2; m >= 0; m -= 1) begin : lane
    logic generated_value;
  end
endmodule

module late_genvar_generated;
  generate
    for (k = 0; k < 1; ++k) begin : lane
      logic generated_value;
    end
  endgenerate
  genvar k;
endmodule

module implicit_case_generated #(parameter MODE = 1);
  case (MODE)
    1: begin : selected
      logic generated_value;
    end
    default: begin : fallback
      logic generated_value;
    end
  endcase
endmodule

module direct_generated;
  generate
    localparam int DIRECT_BASE = 3;
    logic [3:0] direct_value;
    initial direct_value = 4'd2;
    begin : named_scope
      localparam int NESTED_VALUE = DIRECT_BASE;
      logic [3:0] nested_value;
      initial nested_value = NESTED_VALUE;
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog conditional instance generate must parse");
  const auto* sv_unit =
      systemverilog.design.find(UnitKind::VerilogModule, "generated");
  require(
      sv_unit != nullptr
          && sv_unit->generate_regions.size() == 1,
      "SystemVerilog generate HIR count");
  const auto& sv_generate =
      sv_unit->generate_regions.front();
  require(
      sv_generate.then_scope == "selected"
          && sv_generate.else_scope == "rejected"
          && sv_generate.condition.kind
              == ExpressionKind::Identifier
          && sv_generate.condition.text == "ENABLED"
          && sv_generate.then_body.instances.size() == 1
          && sv_generate.then_body.instances.front().name == "active"
          && sv_generate.else_body.instances.size() == 1
          && sv_generate.else_body.instances.front().name == "inactive"
          && sv_generate.then_body.generate_regions.size() == 1
          && sv_generate.then_body.generate_regions.front().then_scope
              == "nested_selected"
          && sv_generate.then_body.generate_regions.front()
                 .then_body.instances
                 .front()
                 .name
              == "nested_active",
      "SystemVerilog conditional generate branches and labels");
  const auto* sv_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "generated_loop");
  require(
      sv_loop_unit != nullptr
          && sv_loop_unit->generate_regions.size() == 1,
      "SystemVerilog loop-generate HIR count");
  const auto& sv_loop = sv_loop_unit->generate_regions.front();
  require(
      sv_loop.kind == GenerateKind::Iterative
          && sv_loop.then_scope == "lane"
          && sv_loop.variable == "i"
          && sv_loop.initial.kind == ExpressionKind::IntegerLiteral
          && sv_loop.condition.kind == ExpressionKind::Binary
          && sv_loop.condition.text == "<"
          && sv_loop.iteration.kind == ExpressionKind::Binary
          && sv_loop.iteration.text == "+"
          && sv_loop.then_body.signals.size() == 1
          && sv_loop.then_body.signals.front().name
              == "generated_value"
          && sv_loop.then_body.concurrent_statements.size() == 1
          && sv_loop.then_body.processes.size() == 1
          && sv_loop.then_body.instances.front().name == "child"
          && std::none_of(
              sv_loop_unit->signals.begin(),
              sv_loop_unit->signals.end(),
              [](const auto& signal) {
                return signal.name == "i";
              }),
      "SystemVerilog canonical genvar-for region");
  const auto* sv_descending_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "descending_loop_generated");
  const auto* sv_late_genvar_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "late_genvar_generated");
  const auto* sv_compound_subtract_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "compound_subtract_loop_generated");
  require(
      sv_descending_loop_unit != nullptr
          && sv_descending_loop_unit->generate_regions.size() == 1
          && sv_descending_loop_unit->generate_regions.front()
                 .iteration.text
              == "-"
          && sv_late_genvar_unit != nullptr
          && sv_late_genvar_unit->generate_regions.size() == 1
          && sv_late_genvar_unit->generate_regions.front()
                 .iteration.text
              == "+"
          && sv_compound_subtract_unit != nullptr
          && sv_compound_subtract_unit->generate_regions.size() == 1
          && sv_compound_subtract_unit->generate_regions.front()
                 .iteration.text
              == "-",
      "SystemVerilog prefix updates and late module genvar");
  const auto* sv_case_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "generated_case");
  require(
      sv_case_unit != nullptr
          && sv_case_unit->generate_regions.size() == 1,
      "SystemVerilog case-generate HIR count");
  const auto& sv_case = sv_case_unit->generate_regions.front();
  require(
      sv_case.kind == GenerateKind::Selection
          && sv_case.condition.text == "MODE"
          && sv_case.alternatives.size() == 3
          && sv_case.alternatives[0].scope == "zero"
          && sv_case.alternatives[1].scope == "selected"
          && sv_case.alternatives[1].choices.size() == 2
          && sv_case.alternatives[2].scope == "fallback"
          && sv_case.alternatives[2].is_default,
      "SystemVerilog case-generate alternatives");
  const auto* sv_implicit_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "implicit_generated");
  require(
      sv_implicit_unit != nullptr
          && sv_implicit_unit->generate_regions.size() == 1
          && sv_implicit_unit->generate_regions.front().kind
              == GenerateKind::Conditional
          && sv_implicit_unit->generate_regions.front()
                 .then_scope
              == "implicit_scope"
          && sv_implicit_unit->generate_regions.front()
                 .then_body.constants.size()
              == 2
          && sv_implicit_unit->generate_regions.front()
                 .then_body.signals.size()
              == 1,
      "implicit SystemVerilog generate-if body");
  const auto* sv_implicit_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "implicit_loop_generated");
  require(
      sv_implicit_loop_unit != nullptr
          && sv_implicit_loop_unit->generate_regions.size() == 1
          && sv_implicit_loop_unit->generate_regions.front().kind
              == GenerateKind::Iterative
          && sv_implicit_loop_unit->generate_regions.front()
                 .then_scope
              == "lane",
      "implicit SystemVerilog generate-for body");
  const auto* sv_implicit_case_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "implicit_case_generated");
  require(
      sv_implicit_case_unit != nullptr
          && sv_implicit_case_unit->generate_regions.size() == 1
          && sv_implicit_case_unit->generate_regions.front().kind
              == GenerateKind::Selection
          && sv_implicit_case_unit->generate_regions.front()
                 .alternatives.size()
              == 2,
      "implicit SystemVerilog generate-case body");
  const auto* sv_direct_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "direct_generated");
  require(
      sv_direct_unit != nullptr
          && sv_direct_unit->generate_regions.size() == 1
          && sv_direct_unit->generate_regions[0].kind
              == GenerateKind::StaticBlock
          && sv_direct_unit->generate_regions[0]
                 .then_body.constants.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.signals.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0].kind
              == GenerateKind::StaticBlock
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0].then_scope
              == "named_scope"
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0]
                 .then_body.constants.size()
              == 1,
      "direct and named SystemVerilog generate blocks");

  const auto vhdl = parse_text(
      "generate.vhd",
      R"(
entity generated is
  generic (enabled : boolean := true);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated is
begin
  selection: if enabled generate
    subtype branch_word_t is unsigned(3 downto 0);
    type branch_state_t is (idle, ready);
    constant branch_value : natural := 1;
    signal branch_signal : branch_word_t;
    function adjust(value : branch_word_t)
      return branch_word_t;
    pure function adjust(value : branch_word_t)
      return branch_word_t is
    begin
      return value + 1;
    end function adjust;
    procedure drive(
      target : out branch_word_t;
      value : branch_word_t);
    procedure drive(
      target : out branch_word_t;
      value : branch_word_t) is
    begin
      target := adjust(value);
    end procedure drive;
    generic (amount : natural := 0)
    function shifted(value : branch_word_t)
      return branch_word_t is
    begin
      return adjust(value) + amount;
    end function shifted;
    function mapped_shift is new shifted
      generic map (amount => 1);
    generic (amount : natural := 0)
    procedure shifted_drive(
      variable target : out branch_word_t;
      value : branch_word_t) is
    begin
      target := value + amount;
    end procedure shifted_drive;
    procedure mapped_drive is new shifted_drive
      generic map (amount => 1);
  begin
    active: entity work.leaf(rtl)
      port map (value => value, result => result);
    nested: if enabled generate
      nested_active: entity work.leaf(rtl)
        port map (value => value, result => result);
    else generate
      nested_inactive: entity work.leaf(rtl)
        port map (value => value, result => result);
    end generate nested;
  else generate
    subtype branch_word_t is unsigned(3 downto 0);
    type branch_state_t is (idle, ready);
    constant branch_value : natural := 0;
    signal branch_signal : branch_word_t;
    pure function adjust(value : branch_word_t)
      return branch_word_t is
    begin
      return value + 2;
    end function adjust;
    procedure drive(
      target : out branch_word_t;
      value : branch_word_t) is
    begin
      target := adjust(value);
    end procedure drive;
  begin
    inactive: entity work.leaf(rtl)
      port map (value => value, result => result);
  end generate selection;
end architecture;

entity generated_loop is
  generic (count : natural := 3);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated_loop is
begin
  lanes: for i in 2 downto 0 generate
    subtype lane_word_t is unsigned(i + 1 downto 0);
    signal generated_value : lane_word_t;
  begin
    generated_value <= i;
    worker: process(generated_value)
    begin
      result <= generated_value(0);
    end process;
    child: entity work.leaf(rtl)
      port map (value => value, result => result);
  end generate lanes;
end architecture;

entity generated_case is
  generic (mode : integer := 1);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated_case is
begin
  selection: case mode generate
    zero: when 0 =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    selected: when 1 to 2 | 7 downto 5 =>
      type choice_bits_t is array (0 to 3) of bit;
      subtype choice_word_t is unsigned(3 downto 0);
      constant choice_value : natural := 9;
      signal choice_signal : choice_word_t;
    begin
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    empty_choice: when 3 to 1 =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    fallback: when others =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
  end generate selection;
end architecture;

entity block_generated is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of block_generated is
begin
  static_scope: block is
    constant base_value : natural := 4;
    constant generated_constant : natural := base_value + 1;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= generated_constant;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  end block static_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      vhdl.ok(),
      "VHDL conditional instance generate must parse");
  const auto* vhdl_unit =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      vhdl_unit != nullptr
          && vhdl_unit->generate_regions.size() == 1,
      "VHDL generate HIR count");
  const auto& vhdl_generate =
      vhdl_unit->generate_regions.front();
  require(
      vhdl_generate.then_scope == "selection"
          && vhdl_generate.else_scope == "selection"
          && vhdl_generate.condition.text == "enabled"
          && vhdl_generate.then_body.type_aliases.size() == 2
          && vhdl_generate.then_body.type_aliases[0].name
              == "branch_word_t"
          && vhdl_generate.then_body.type_aliases[0].declaration_kind
              == TypeDeclarationKind::VhdlSubtype
          && vhdl_generate.then_body.type_aliases[1].declaration_kind
              == TypeDeclarationKind::VhdlEnumeration
          && vhdl_generate.then_body.constants.size() == 1
          && vhdl_generate.then_body.signals.size() == 1
          && vhdl_generate.then_body.functions.size() == 2
          && vhdl_generate.then_body.functions.front().name
              == "adjust"
          && !vhdl_generate.then_body.functions.front().defined
          && vhdl_generate.then_body.functions[1].defined
          && vhdl_generate.then_body.functions.front()
                 .return_type.named_type
              == "branch_word_t"
          && vhdl_generate.then_body.procedures.size() == 2
          && vhdl_generate.then_body.procedures.front().name
              == "drive"
          && !vhdl_generate.then_body.procedures.front().defined
          && vhdl_generate.then_body.procedures[1].defined
          && vhdl_generate.then_body.generic_function_templates.size()
              == 1
          && vhdl_generate.then_body.generic_function_instances.size()
              == 1
          && vhdl_generate.then_body.generic_procedure_templates.size()
              == 1
          && vhdl_generate.then_body.generic_procedure_instances.size()
              == 1
          && vhdl_generate.else_body.type_aliases.size() == 2
          && vhdl_generate.else_body.constants.size() == 1
          && vhdl_generate.else_body.signals.size() == 1
          && vhdl_generate.else_body.functions.size() == 1
          && vhdl_generate.else_body.procedures.size() == 1
          && vhdl_generate.then_body.instances.front().name == "active"
          && vhdl_generate.else_body.instances.front().name == "inactive"
          && vhdl_generate.then_body.generate_regions.size() == 1
          && vhdl_generate.then_body.generate_regions.front().then_scope
              == "nested"
          && vhdl_generate.then_body.generate_regions.front()
                 .then_body.instances
                 .front()
                 .name
              == "nested_active",
      "VHDL conditional generate branches and canonical label");
  const auto vhdl_loop_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "generated_loop";
      });
  require(
      vhdl_loop_unit != vhdl.design.units.end()
          && vhdl_loop_unit->generate_regions.size() == 1,
      "VHDL loop-generate HIR count");
  const auto& vhdl_loop =
      vhdl_loop_unit->generate_regions.front();
  require(
      vhdl_loop.kind == GenerateKind::Iterative
          && vhdl_loop.then_scope == "lanes"
          && vhdl_loop.variable == "i"
          && vhdl_loop.condition.text == ">="
          && vhdl_loop.iteration.text == "-"
          && vhdl_loop.then_body.type_aliases.size() == 1
          && vhdl_loop.then_body.type_aliases.front().name
              == "lane_word_t"
          && vhdl_loop.then_body.type_aliases.front()
                 .type.packed_range_expression
          && vhdl_loop.then_body.signals.size() == 1
          && vhdl_loop.then_body.concurrent_statements.size() == 1
          && vhdl_loop.then_body.processes.size() == 1
          && vhdl_loop.then_body.instances.front().name == "child",
      "VHDL descending for-generate region");
  const auto vhdl_case_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "generated_case";
      });
  require(
      vhdl_case_unit != vhdl.design.units.end()
          && vhdl_case_unit->generate_regions.size() == 1,
      "VHDL case-generate HIR count");
  const auto& vhdl_case =
      vhdl_case_unit->generate_regions.front();
  require(
      vhdl_case.kind == GenerateKind::Selection
          && vhdl_case.condition.text == "mode"
          && vhdl_case.alternatives.size() == 4
          && vhdl_case.alternatives[0].scope == "zero"
          && vhdl_case.alternatives[1].scope == "selected"
          && vhdl_case.alternatives[1].choices.size() == 2
          && vhdl_case.alternatives[1].choices[0].right
          && !vhdl_case.alternatives[1].choices[0].descending
          && vhdl_case.alternatives[1].choices[1].right
          && vhdl_case.alternatives[1].choices[1].descending
          && vhdl_case.alternatives[1].body.type_aliases.size() == 2
          && vhdl_case.alternatives[1].body.type_aliases[0]
                 .declaration_kind
              == TypeDeclarationKind::VhdlArray
          && vhdl_case.alternatives[1].body.constants.size() == 1
          && vhdl_case.alternatives[1].body.signals.size() == 1
          && vhdl_case.alternatives[2].scope == "empty_choice"
          && vhdl_case.alternatives[2].choices.front().right
          && !vhdl_case.alternatives[2].choices.front().descending
          && vhdl_case.alternatives[3].scope == "fallback"
          && vhdl_case.alternatives[3].is_default,
      "VHDL case-generate alternatives");
  const auto vhdl_block_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "block_generated";
      });
  require(
      vhdl_block_unit != vhdl.design.units.end()
          && vhdl_block_unit->generate_regions.size() == 1
          && vhdl_block_unit->generate_regions.front().kind
              == GenerateKind::StaticBlock
          && vhdl_block_unit->generate_regions.front().then_scope
              == "static_scope"
          && vhdl_block_unit->generate_regions.front()
                 .then_body.constants.size()
              == 2
          && vhdl_block_unit->generate_regions.front()
                 .then_body.signals.size()
              == 1
          && vhdl_block_unit->generate_regions.front()
                 .then_body.processes.size()
              == 1,
      "VHDL unguarded static block body");

  const auto unlabeled_systemverilog = parse_text(
      "unlabeled_generate.sv",
      R"(
module invalid #(parameter ENABLED = 1);
  generate
    if (ENABLED) begin
    end
    for (genvar i = 0; i < 1; i++) begin
      localparam int WIDTH = i + 2;
      typedef logic [WIDTH-1:0] word_t;
      word_t value;
      function automatic word_t make(input word_t input_value);
        return input_value;
      endfunction
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      unlabeled_systemverilog.ok()
          && unlabeled_systemverilog.design.units.size() == 1
          && unlabeled_systemverilog.design.units.front()
                 .generate_regions.size()
              == 2
          && unlabeled_systemverilog.design.units.front()
                 .generate_regions.front().then_scope
              == "genblk1"
          && unlabeled_systemverilog.design.units.front()
                 .generate_regions[1].then_scope
              == "genblk2"
          && unlabeled_systemverilog.design.units.front()
                 .generate_regions[1].then_body.type_aliases.size()
              == 1
          && unlabeled_systemverilog.design.units.front()
                 .generate_regions[1].then_body.functions.front()
                 .return_type.named_type
              == "word_t",
      "unlabeled generate names and local typedef/callable HIR are stable");

  const auto generated_type_conflict = parse_text(
      "generated_type_conflict.sv",
      R"(module invalid;
  generate
    if (1) begin : selected
      typedef logic word_t;
      logic word_t;
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !generated_type_conflict.ok()
          && std::ranges::any_of(
              generated_type_conflict.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-124";
              }),
      "generated type/object declaration conflicts are diagnosed");

  const auto unsupported_vhdl_body = parse_text(
      "unsupported_generate.vhd",
      R"(
architecture rtl of generated is
  signal q : std_logic;
begin
  selection: if true generate
    report "unsupported";
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !unsupported_vhdl_body.ok()
          && std::any_of(
              unsupported_vhdl_body.diagnostics.begin(),
              unsupported_vhdl_body.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-020";
              }),
      "unsupported VHDL generate body is targeted");

  const auto invalid_systemverilog_loop = parse_text(
      "invalid_loop_generate.sv",
      R"(
module invalid_loop;
  generate
    for (i = 0; i < 2; i = i + 1) begin : lane
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_systemverilog_loop.ok()
          && std::any_of(
              invalid_systemverilog_loop.diagnostics.begin(),
              invalid_systemverilog_loop.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-066";
              }),
      "non-inline-genvar loop generate is targeted");

  const auto duplicate_systemverilog_genvar = parse_text(
      "duplicate_genvar.sv",
      R"(
module duplicate_genvar;
  genvar i, i;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !duplicate_systemverilog_genvar.ok()
          && std::any_of(
              duplicate_systemverilog_genvar.diagnostics.begin(),
              duplicate_systemverilog_genvar.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-022";
              }),
      "duplicate module-scope genvar is targeted");

  const auto generated_port_direction = parse_text(
      "generated_port_direction.sv",
      R"(
module invalid_generated_port;
  generate
    if (1) begin : selected
      output logic invalid_port;
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !generated_port_direction.ok()
          && std::any_of(
              generated_port_direction.diagnostics.begin(),
              generated_port_direction.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-UNSUPPORTED-022";
              }),
      "generated port direction is targeted");

  const auto invalid_vhdl_loop = parse_text(
      "invalid_loop_generate.vhd",
      R"(
architecture rtl of invalid_loop is
begin
  lanes: for i in 0 generate
  end generate lanes;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl_loop.ok()
          && std::any_of(
              invalid_vhdl_loop.diagnostics.begin(),
              invalid_vhdl_loop.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-063";
              }),
      "missing VHDL generate range direction is targeted");

  const auto duplicate_systemverilog_default = parse_text(
      "duplicate_case_generate.sv",
      R"(
module duplicate_case_generate #(parameter MODE = 0);
  generate
    case (MODE)
      default: begin : first
      end
      default: begin : second
      end
    endcase
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !duplicate_systemverilog_default.ok()
          && std::any_of(
              duplicate_systemverilog_default.diagnostics.begin(),
              duplicate_systemverilog_default.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-021";
              }),
      "duplicate SystemVerilog generate default is targeted");

  const auto unlabeled_vhdl_alternative = parse_text(
      "unlabeled_case_generate.vhd",
      R"(
architecture rtl of invalid_case is
begin
  selection: case 0 generate
    when 0 =>
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !unlabeled_vhdl_alternative.ok()
          && std::any_of(
              unlabeled_vhdl_alternative.diagnostics.begin(),
              unlabeled_vhdl_alternative.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-070";
              }),
      "unlabeled VHDL case-generate alternative is targeted");

  const auto nonfinal_vhdl_others = parse_text(
      "nonfinal_case_generate_others.vhd",
      R"(
architecture rtl of invalid_case is
begin
  selection: case 0 generate
    fallback: when others =>
    late: when 0 =>
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !nonfinal_vhdl_others.ok()
          && std::any_of(
              nonfinal_vhdl_others.diagnostics.begin(),
              nonfinal_vhdl_others.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-018";
              }),
      "nonfinal VHDL case-generate others is targeted");

  const auto guarded_vhdl_block = parse_text(
      "guarded_block.vhd",
      R"(
architecture rtl of guarded is
  signal enabled : boolean;
  signal source_value : bit;
  signal result_value : bit;
begin
  guarded_scope: block (enabled and true) is
    generic (
      width : natural := 4;
      offset : natural := 1
    );
    generic map (width => 8, offset => open);
    port (
      input_value : in bit;
      output_value : out bit
    );
    port map (
      input_value => source_value,
      output_value => result_value
    );
  begin
  end block guarded_scope;
end architecture;
)",
      Language::Vhdl2008);
  const auto& guarded_region =
      guarded_vhdl_block.design.units.front().generate_regions.front();
  require(
      guarded_vhdl_block.ok()
          && guarded_region.kind == GenerateKind::StaticBlock
          && guarded_region.then_scope == "guarded_scope"
          && guarded_region.condition.kind == ExpressionKind::Binary
          && guarded_region.condition.text == "and"
          && guarded_region.condition.operands.size() == 2
          && guarded_region.condition.operands.front().text == "enabled"
          && guarded_region.block_generics.size() == 2
          && guarded_region.block_generics.front().name == "width"
          && guarded_region.block_generic_map.size() == 2
          && guarded_region.block_generic_map.front().name
              == std::optional<std::string>{"width"}
          && guarded_region.block_generic_map.back().default_box
          && guarded_region.block_ports.size() == 2
          && guarded_region.block_ports.front().name == "input_value"
          && guarded_region.block_port_map.size() == 2
          && guarded_region.block_port_map.back().port
              == std::optional<std::string>{"output_value"},
      "guarded VHDL block retains its expression, interface, and maps");

  const auto nonvalue_vhdl_block = parse_text(
      "nonvalue_block.vhd",
      R"(
package template is
  generic (bias : integer := 1);
end package;
architecture rtl of nonvalue_block is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure observe(value : integer) is
  begin
    null;
  end procedure;
  package selected is new work.template generic map (bias => 2);
begin
  selected_scope: block is
    generic (
      type item_t;
      function transform(value : item_t) return item_t;
      procedure publish(value : item_t) is observe;
      package api is new work.template generic map (<>));
    generic map (
      item_t => integer,
      transform => increment,
      publish => open,
      api => selected);
  begin
  end block selected_scope;
end architecture;
)",
      Language::Vhdl2008);
  const auto& nonvalue_region =
      nonvalue_vhdl_block.design.units.back().generate_regions.front();
  require(
      nonvalue_vhdl_block.ok()
          && nonvalue_region.block_generics.size() == 4
          && nonvalue_region.block_generics[0].kind
              == ParameterKind::Type
          && nonvalue_region.block_generics[1].kind
              == ParameterKind::Function
          && nonvalue_region.block_generics[2].kind
              == ParameterKind::Procedure
          && nonvalue_region.block_generics[3].kind
              == ParameterKind::Package
          && nonvalue_region.block_generic_map.size() == 4
          && nonvalue_region.block_generic_map[2].default_box,
      "VHDL block retains type, function, procedure, and package generics");

  const auto missing_block_generic_map_semicolon = parse_text(
      "missing_block_generic_map_semicolon.vhd",
      R"(
architecture rtl of malformed is
begin
  malformed_scope: block is
    generic (width : natural := 4);
    generic map (width => open)
  begin
  end block malformed_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !missing_block_generic_map_semicolon.ok()
          && std::ranges::any_of(
              missing_block_generic_map_semicolon.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-232";
              }),
      "VHDL block generic map requires a trailing semicolon");

  const auto missing_block_port_map_semicolon = parse_text(
      "missing_block_port_map_semicolon.vhd",
      R"(
architecture rtl of malformed is
  signal source_value : bit;
begin
  malformed_scope: block is
    port (input_value : in bit);
    port map (input_value => source_value)
  begin
  end block malformed_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !missing_block_port_map_semicolon.ok()
          && std::ranges::any_of(
              missing_block_port_map_semicolon.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-233";
              }),
      "VHDL block port map requires a trailing semicolon");

  const auto malformed_vhdl_guard = parse_text(
      "malformed_guarded_block.vhd",
      R"(
architecture rtl of guarded is
  signal enabled : boolean;
begin
  guarded_scope: block (enabled
  begin
  end block guarded_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !malformed_vhdl_guard.ok()
          && std::any_of(
              malformed_vhdl_guard.diagnostics.begin(),
              malformed_vhdl_guard.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-231";
              }),
      "guarded VHDL block requires a closing parenthesis");

  const auto missing_generate_begin = parse_text(
      "missing_generate_begin.vhd",
      R"(
architecture rtl of missing_begin is
begin
  selected: if true generate
    constant value : natural := 1;
    observed <= value;
  end generate selected;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !missing_generate_begin.ok()
          && std::ranges::any_of(
              missing_generate_begin.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-234";
              }),
      "a generated declarative part requires begin before statements");

  const auto mismatched_vhdl_block = parse_text(
      "mismatched_block.vhd",
      R"(
architecture rtl of mismatched is
begin
  opening_name: block
  begin
  end block closing_name;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !mismatched_vhdl_block.ok()
          && std::any_of(
              mismatched_vhdl_block.diagnostics.begin(),
              mismatched_vhdl_block.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-081";
              }),
      "VHDL block end-label mismatch is targeted");

  const auto invalid_generated_constants = parse_text(
      "invalid_generated_constants.vhd",
      R"(
architecture rtl of invalid_constants is
begin
  invalid_scope: block
    constant missing_value : natural;
    constant duplicate_name : natural := 1;
    signal duplicate_name : bit;
  begin
  end block invalid_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_generated_constants.ok()
          && std::any_of(
              invalid_generated_constants.diagnostics.begin(),
              invalid_generated_constants.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-084";
              })
          && std::any_of(
              invalid_generated_constants.diagnostics.begin(),
              invalid_generated_constants.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-019";
              }),
      "invalid VHDL generated constants are targeted");

  const auto duplicate_generated_type = parse_text(
      "duplicate_generated_type.vhd",
      R"(
architecture rtl of duplicate_generated_type is
begin
  selected: if true generate
    type local_t is (first, second);
    subtype local_t is bit;
  begin
  end generate selected;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !duplicate_generated_type.ok()
          && std::ranges::any_of(
              duplicate_generated_type.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-036";
              }),
      "duplicate types in one generated declarative region are rejected");

  const auto malformed_generated_purity = parse_text(
      "malformed_generated_purity.vhd",
      R"(
architecture rtl of malformed_generated_purity is
begin
  selected: if true generate
    pure procedure invalid is
    begin
    end procedure invalid;
  begin
  end generate selected;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !malformed_generated_purity.ok()
          && std::ranges::any_of(
              malformed_generated_purity.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-235";
              }),
      "a generated purity prefix requires a function");
}

void test_systemverilog_named_events() {
  const auto parsed = parse_text(
      "named_events.sv",
      R"(
module named_events;
  event fired, acknowledged;
  logic observed;
  initial begin
    -> fired;
    @(acknowledged);
  end
  always @(fired) observed = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog named events must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.signals.size() == 3
          && unit.signals[0].name == "fired"
          && unit.signals[0].type.spelling == "event"
          && unit.signals[1].name == "acknowledged"
          && unit.signals[1].type.spelling == "event",
      "named event declarations");
  require(
      unit.processes.size() == 2
          && unit.processes[0].statements.size() == 2
          && unit.processes[0].statements[0].kind
              == StatementKind::EventTrigger
          && unit.processes[0].statements[0].target.text == "fired"
          && unit.processes[0].statements[1].kind
              == StatementKind::WaitOn
          && unit.processes[0].statements[1]
                 .sensitivities.front().signal
              == "acknowledged"
          && unit.processes[1].sensitivities.front().signal == "fired",
      "named event trigger and controls");

  const auto nonblocking = parse_text(
      "nonblocking_event.sv",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
  initial ->> #2 fired;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [](const auto& result, const std::string_view code) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [&](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      nonblocking.ok()
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .kind
              == StatementKind::EventTrigger
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .assignment_kind
              == AssignmentKind::NonBlocking
          && !nonblocking.design.units.front()
                  .processes.front().statements.front().delay
          && nonblocking.design.units.front().processes.size() == 2
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay->magnitude
              == 2,
      "nonblocking event trigger HIR");

  const auto verilog_nonblocking = parse_text(
      "nonblocking_event.v",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
endmodule
)",
      Language::Verilog2005);
  require(
      has_code(
          verilog_nonblocking, "FSIM-VERILOG-SEM-008"),
      "nonblocking event trigger language diagnostic");

  const auto invalid_immediate_delay = parse_text(
      "immediate_delay.sv",
      R"(
module immediate_delay;
  event fired;
  initial -> #1 fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(invalid_immediate_delay, "FSIM-SV-SEM-036"),
      "delayed immediate event trigger diagnostic");

  const auto duplicate = parse_text(
      "duplicate_event.sv",
      R"(
module duplicate_event;
  logic fired;
  event fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(duplicate, "FSIM-SV-SEM-035"),
      "duplicate named event diagnostic");
}

void test_verilog_literal_display() {
  const auto parsed = parse_text(
      "display.v",
      R"(
module display;
  initial begin
    $display("hello\nworld\t\"quote\"\\slash\101");
    $display();
    $display;
  end
endmodule
)",
      Language::Verilog2005);
  require(parsed.ok(), "literal $display tasks must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 3
          && std::all_of(
              statements.begin(),
              statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline;
              })
          && statements.front().output_text
              == "hello\nworld\t\"quote\"\\slashA"
          && statements[1].output_text.empty()
          && statements[2].output_text.empty(),
      "literal and empty $display HIR");

  const auto formatted = parse_text(
      "formatted_display.sv",
      R"(
module formatted_display;
  logic q;
  initial begin
    $display("q=%%:%b!", q);
    $write("%b", q);
    $display("%h", q);
    $display("%o", q);
    $display("%d", q);
    $display("%c", q);
    $display("%s", q);
    $display("%0h", q);
    $display("%B", q);
    $display("%X", q);
    $display("%O", q);
    $display("%D", q);
    $display("%C", q);
    $display("%S", q);
    $display("%8h", q);
    $display("%-8x", q);
    $display("%08d", q);
    $display("scope=%m q=%B", q);
    $display("time=%08T");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      formatted.ok()
          && formatted.design.units.front().processes.front()
                 .statements.size() == 19
          && formatted.design.units.front().processes.front()
                 .statements[0].output_format
              == OutputFormat::Binary
          && formatted.design.units.front().processes.front()
                 .statements[0].output_prefix == "q=%:"
          && formatted.design.units.front().processes.front()
                 .statements[0].output_suffix == "!"
          && formatted.design.units.front().processes.front()
                 .statements[0].value.text == "q"
          && !formatted.design.units.front().processes.front()
                  .statements[1].output_newline
          && formatted.design.units.front().processes.front()
                 .statements[2].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[3].output_format
              == OutputFormat::Octal
          && formatted.design.units.front().processes.front()
                 .statements[4].output_format
              == OutputFormat::Decimal
          && formatted.design.units.front().processes.front()
                 .statements[5].output_format
              == OutputFormat::Character
          && formatted.design.units.front().processes.front()
                 .statements[6].output_format
              == OutputFormat::String
          && formatted.design.units.front().processes.front()
                 .statements[7].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[7].output_suppress_leading_zero,
          "single-value formats, %0 suppression, and %% decoding");
  const auto& formatted_statements =
      formatted.design.units.front().processes.front().statements;
  require(
      formatted_statements[8].output_format
              == OutputFormat::Binary
          && formatted_statements[9].output_format
              == OutputFormat::Hexadecimal
          && formatted_statements[10].output_format
              == OutputFormat::Octal
          && formatted_statements[11].output_format
              == OutputFormat::Decimal
          && formatted_statements[12].output_format
              == OutputFormat::Character
          && formatted_statements[13].output_format
              == OutputFormat::String,
      "uppercase and %x conversion aliases");
  require(
      formatted_statements[14].output_minimum_width == 8
          && !formatted_statements[14].output_left_justify
          && !formatted_statements[14].output_zero_pad
          && formatted_statements[15].output_minimum_width == 8
          && formatted_statements[15].output_left_justify
          && !formatted_statements[15].output_zero_pad
          && formatted_statements[16].output_minimum_width == 8
          && !formatted_statements[16].output_left_justify
          && formatted_statements[16].output_zero_pad,
      "minimum-width, left-justification, and zero-padding metadata");
  require(
      formatted_statements[17].output_values.size() == 2
          && formatted_statements[17].output_values[0].format
              == OutputFormat::Hierarchy
          && !formatted_statements[17].output_values[0].value.valid()
          && formatted_statements[17].output_values[0].prefix
              == "scope="
          && formatted_statements[17].output_values[1].format
              == OutputFormat::Binary
          && formatted_statements[17].output_values[1].value.text
              == "q",
      "%m hierarchy substitution does not consume a runtime value");
  require(
      formatted_statements[18].output_values.size() == 1
          && formatted_statements[18].output_values[0].format
              == OutputFormat::Time
          && !formatted_statements[18].output_values[0].value.valid()
          && formatted_statements[18].output_values[0].minimum_width
              == 8
          && formatted_statements[18].output_values[0].zero_pad,
      "%t current-time substitution and width metadata");

  const auto invalid_format_width = parse_text(
      "invalid_format_width.sv",
      R"(
module invalid_format_width;
  logic q;
  initial begin
    $display("%-h", q);
    $display("%0s", q);
    $display("%999999999999999999999h", q);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::count_if(
          invalid_format_width.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          })
          == 3,
      "invalid field modifiers need targeted diagnostics");

  const auto unsupported = parse_text(
      "unsupported_format.sv",
      R"(
module unsupported_format;
  logic q;
  initial $display("%v", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unsupported.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          }),
      "unsupported output conversions need a targeted diagnostic");

  const auto bad_escape = parse_text(
      "bad_escape.sv",
      R"(
module bad_escape;
  initial $display("bad\q");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          bad_escape.diagnostics.begin(),
          bad_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "unsupported output-string escapes need a targeted diagnostic");

  const auto wide_octal_escape = parse_text(
      "wide_octal_escape.sv",
      R"(
module wide_octal_escape;
  initial $display("\777");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          wide_octal_escape.diagnostics.begin(),
          wide_octal_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "out-of-byte-range octal escapes need a targeted diagnostic");

  const auto write = parse_text(
      "write.sv",
      R"(
module write;
  initial begin
    $write("hello");
    $write();
    $write;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(write.ok(), "literal $write tasks must parse");
  const auto& write_statements =
      write.design.units.front().processes.front().statements;
  require(
      write_statements.size() == 3
          && std::all_of(
              write_statements.begin(),
              write_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && !statement.output_newline;
              })
          && write_statements.front().output_text == "hello"
          && write_statements[1].output_text.empty()
          && write_statements[2].output_text.empty(),
      "literal and empty $write HIR");

  const auto multiple_write = parse_text(
      "multiple_write.sv",
      R"(
module formatted_write;
  logic [3:0] a;
  logic [7:0] b;
  initial begin
    $write("a=%b b=%h tail=", a, b, a);
    $display(a, b);
    $display("prefix=", a);
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& multi_statements =
      multiple_write.design.units.front().processes.front().statements;
  require(
      multiple_write.ok() && multi_statements.size() == 3
          && multi_statements[0].output_values.size() == 3
          && multi_statements[0].output_values[0].format
              == OutputFormat::Binary
          && multi_statements[0].output_values[0].prefix == "a="
          && multi_statements[0].output_values[1].format
              == OutputFormat::Hexadecimal
          && multi_statements[0].output_values[1].prefix == " b="
          && multi_statements[0].output_values[2].format
              == OutputFormat::Decimal
          && multi_statements[0].output_values[2].prefix == " tail="
          && multi_statements[1].output_values.size() == 2
          && multi_statements[1].output_values[0].format
              == OutputFormat::Decimal
          && multi_statements[2].output_values.size() == 1
          && multi_statements[2].output_values[0].prefix == "prefix=",
      "multiple conversions and default runtime arguments in source order");

  const auto missing_format_value = parse_text(
      "missing_format_value.sv",
      R"(
module missing_format_value;
  logic q;
  initial $display("%b %h", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          missing_format_value.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "missing formatted values need a task-specific diagnostic");

  const auto strobe = parse_text(
      "strobe.sv",
      R"(
module strobe;
  initial begin
    $strobe("later");
    $strobe();
    $strobe;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(strobe.ok(), "literal $strobe tasks must parse");
  const auto& strobe_statements =
      strobe.design.units.front().processes.front().statements;
  require(
      strobe_statements.size() == 3
          && std::all_of(
              strobe_statements.begin(),
              strobe_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && strobe_statements.front().output_text == "later"
          && strobe_statements[1].output_text.empty()
          && strobe_statements[2].output_text.empty(),
      "literal and empty $strobe HIR");

  const auto unsupported_strobe = parse_text(
      "formatted_strobe.sv",
      R"(
module formatted_strobe;
  logic q;
  initial $strobe("q=%b", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      unsupported_strobe.ok()
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_format
              == OutputFormat::Binary
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_postponed
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_prefix == "q=",
      "formatted $strobe HIR and postponed policy");

  const auto monitor = parse_text(
      "monitor.sv",
      R"(
module monitor;
  initial begin
    $monitor("once");
    $monitor();
    $monitor;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(monitor.ok(), "literal $monitor tasks must parse");
  const auto& monitor_statements =
      monitor.design.units.front().processes.front().statements;
  require(
      monitor_statements.size() == 3
          && std::all_of(
              monitor_statements.begin(),
              monitor_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && monitor_statements.front().output_text == "once",
      "literal $monitor initial-publication HIR");

  const auto formatted_monitor = parse_text(
      "formatted_monitor.sv",
      R"(
module formatted_monitor;
  logic q;
  initial begin
    $monitor("q=%b t=%t", q);
    $monitoroff;
    $monitoron();
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& formatted_monitor_statements =
      formatted_monitor.design.units.front().processes.front().statements;
  require(
      formatted_monitor.ok()
          && formatted_monitor_statements.size() == 3
          && formatted_monitor_statements[0].output_monitor
          && formatted_monitor_statements[0].output_postponed
          && formatted_monitor_statements[0].output_values.size() == 2
          && formatted_monitor_statements[0].output_values[0].format
              == OutputFormat::Binary
          && formatted_monitor_statements[0].output_values[1].format
              == OutputFormat::Time
          && formatted_monitor_statements[1].kind
              == StatementKind::MonitorControl
          && !formatted_monitor_statements[1].monitor_enabled
          && formatted_monitor_statements[2].kind
              == StatementKind::MonitorControl
          && formatted_monitor_statements[2].monitor_enabled,
      "value-sensitive monitor registration and on/off controls");

  const auto numeric = parse_text(
      "numeric_output.sv",
      R"(
module numeric_output;
  initial begin
    $display(42);
    $write(8'h2a);
    $strobe(6'b10_1010);
    $display(8'shff);
    $write(4'sb0111);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(numeric.ok(), "constant numeric output tasks must parse");
  const auto& numeric_statements =
      numeric.design.units.front().processes.front().statements;
  require(
      numeric_statements.size() == 5
          && numeric_statements[0].output_text == "42"
          && numeric_statements[0].output_newline
          && numeric_statements[1].output_text == "42"
          && !numeric_statements[1].output_newline
          && numeric_statements[2].output_text == "42"
          && numeric_statements[2].output_postponed
          && numeric_statements[3].output_text == "-1"
          && numeric_statements[3].output_newline
          && numeric_statements[4].output_text == "7"
          && !numeric_statements[4].output_newline,
      "unsigned and signed numeric output literal folding");

  const auto unknown_numeric = parse_text(
      "unknown_numeric_output.sv",
      R"(
module unknown_numeric_output;
  initial $display(4'bx001);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unknown_numeric.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "unknown numeric output literals need a targeted diagnostic");
}

void test_systemverilog_random_functions() {
  const auto parsed = parse_text(
      "random_functions.sv",
      R"(
module random_functions;
  logic [31:0] a, b, c, d, e, f, u;
  initial begin
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(9, 3);
    u = $urandom_range(4'bx);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "random system functions must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 7
          && std::ranges::all_of(
              statements,
              [](const Statement& statement) {
                return statement.kind == StatementKind::Assignment
                    && statement.value.kind == ExpressionKind::Call;
              })
          && statements[0].value.text == "$urandom"
          && statements[0].value.operands.empty()
          && statements[1].value.text == "$urandom"
          && statements[1].value.operands.empty()
          && statements[2].value.text == "$random"
          && statements[2].value.operands.empty()
          && statements[3].value.text == "$random"
          && statements[3].value.operands.empty()
          && statements[4].value.text == "$urandom_range"
          && statements[4].value.operands.size() == 1
          && statements[5].value.text == "$urandom_range"
          && statements[5].value.operands.size() == 2
          && statements[6].value.text == "$urandom_range"
          && statements[6].value.operands.size() == 1
          && statements[6].value.operands[0].kind
              == ExpressionKind::LogicLiteral,
      "bare/empty random calls and range arguments in typed HIR");
}

} // namespace fsim::tests::frontend
