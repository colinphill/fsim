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

    void require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    [[maybe_unused]] std::filesystem::path make_test_directory(
        std::string_view name)
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto directory = std::filesystem::temp_directory_path()
            / ("fsim-" + std::string { name } + "-"
                + std::to_string(suffix));
        std::filesystem::create_directories(directory);
        return directory;
    }

    [[maybe_unused]] void write_text(
        const std::filesystem::path& path,
        const std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << text;
        require(
            output.good(),
            "frontend test fixture must be writable");
    }

} // namespace

void test_vhdl_revision_statement_profiles()
{
    const auto diagnostic_count = [](const auto& diagnostics,
                                      const std::string_view code) {
        return std::ranges::count_if(
            diagnostics, [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };
    const auto parse_architecture = [](const std::string_view name,
                                        const std::string_view declarations,
                                        const std::string_view statements,
                                        const VhdlStandard standard) {
        return parse_text(
            std::string { name } + ".vhd",
            "architecture rtl of " + std::string { name } + " is\n"
                + std::string { declarations } + "\nbegin\n"
                + std::string { statements } + "\nend architecture;\n",
            Language::Vhdl2008,
            standard);
    };
    const auto expect_revision_error = [&](const std::string_view name,
                                           const std::string_view source,
                                           const VhdlStandard standard,
                                           const std::string_view feature) {
        const auto parsed = parse_text(
            std::string { name } + ".vhd", source,
            Language::Vhdl2008, standard);
        require(
            diagnostic_count(parsed.diagnostics, "FSIM-FE-VHSTD-003") == 1U
                && std::ranges::any_of(
                    parsed.diagnostics,
                    [&](const Diagnostic& diagnostic) {
                        return diagnostic.code == "FSIM-FE-VHSTD-003"
                            && diagnostic.message.find(feature)
                            != std::string::npos;
                    }),
            std::string { name }
                + " must have one exact revision diagnostic naming its "
                  "migration feature");
    };

    const auto vhdl_1987 = parse_architecture(
        "vhdl87_statements",
        R"(  component cell
    port (source : in bit; result : out bit);
  end component;
  signal source : bit;
  signal result : bit;)",
        R"(  u_cell : cell port map (source, result);
  update_result : process(source)
  begin
    if source = '1' then
      result <= '1';
    else
      result <= '0';
    end if;
  end process update_result;)",
        VhdlStandard::Vhdl1987);
    require(
        vhdl_1987.ok()
            && vhdl_1987.design.units.front().instances.size() == 1U
            && vhdl_1987.design.units.front().processes.size() == 1U,
        "VHDL-87 component instantiation, positional port maps, explicit "
        "sensitivity lists, and sequential statements remain legal");

    const auto vhdl_1993 = parse_text(
        "vhdl93_statements.vhd",
        R"(entity child is
  port (source : in bit; result : out bit);
end entity;
architecture rtl of child is begin result <= source; end architecture;
entity vhdl93_statements is end entity;
architecture rtl of vhdl93_statements is
  signal source : bit;
  signal result : bit;
begin
  u_child : entity work.child(rtl) port map (source, result);
  observe : postponed process(source)
  begin
    report "observed" severity note;
  end postponed process observe;
end architecture;)",
        Language::Vhdl2008,
        VhdlStandard::Vhdl1993);
    require(
        vhdl_1993.ok()
            && vhdl_1993.design.units.back().instances.size() == 1U
            && vhdl_1993.design.units.back().processes.front().vhdl_postponed,
        "VHDL-93 direct entity instantiation, postponed processes, and report "
        "statements remain legal");

    expect_revision_error(
        "vhdl87_direct_entity",
        R"(architecture rtl of vhdl87_direct_entity is begin
  u : entity work.child;
end architecture;)",
        VhdlStandard::Vhdl1987, "direct entity instantiation");
    expect_revision_error(
        "vhdl87_direct_configuration",
        R"(architecture rtl of vhdl87_direct_configuration is begin
  u : configuration work.selected_configuration;
end architecture;)",
        VhdlStandard::Vhdl1987, "direct configuration instantiation");
    expect_revision_error(
        "vhdl87_postponed",
        R"(architecture rtl of vhdl87_postponed is begin
  postponed process begin wait; end postponed process;
end architecture;)",
        VhdlStandard::Vhdl1987, "postponed concurrent statements");
    expect_revision_error(
        "vhdl87_report",
        R"(architecture rtl of vhdl87_report is begin
  process begin report "message"; wait; end process;
end architecture;)",
        VhdlStandard::Vhdl1987, "report statements");
    expect_revision_error(
        "vhdl2002_context_declaration",
        R"(context legacy_context is
  library ieee;
end context;)",
        VhdlStandard::Vhdl2002, "context declarations");
    expect_revision_error(
        "vhdl2002_context_reference",
        R"(context work.legacy_context;
entity vhdl2002_context_reference is end entity;)",
        VhdlStandard::Vhdl2002, "context references");
    expect_revision_error(
        "vhdl2002_process_all",
        R"(architecture rtl of vhdl2002_process_all is begin
  process(all) begin null; end process;
end architecture;)",
        VhdlStandard::Vhdl2002, "process(all)");
    expect_revision_error(
        "vhdl2002_selected_assignment",
        R"(architecture rtl of vhdl2002_selected_assignment is begin
  process
    variable selector : bit;
    variable result : bit;
  begin
    with selector select result := '0' when '0', '1' when others;
    wait;
  end process;
end architecture;)",
        VhdlStandard::Vhdl2002, "sequential selected assignments");
    expect_revision_error(
        "vhdl2002_conditional_assignment",
        R"(architecture rtl of vhdl2002_conditional_assignment is begin
  process
    variable selector : boolean;
    variable result : bit;
  begin
    result := '1' when selector else '0';
    wait;
  end process;
end architecture;)",
        VhdlStandard::Vhdl2002, "sequential conditional assignments");
    expect_revision_error(
        "vhdl2002_force",
        R"(architecture rtl of vhdl2002_force is
  signal result : bit;
begin
  process begin result <= force '1'; wait; end process;
end architecture;)",
        VhdlStandard::Vhdl2002, "force and release assignments");
    expect_revision_error(
        "vhdl2002_matching_case",
        R"(architecture rtl of vhdl2002_matching_case is begin
  process begin case? bit'('0') is when '0' => null; when others => null;
  end case?; wait; end process;
end architecture;)",
        VhdlStandard::Vhdl2002, "matching case statements");
    expect_revision_error(
        "vhdl2002_matching_selected",
        R"(architecture rtl of vhdl2002_matching_selected is
  signal selector : bit;
  signal result : bit;
begin
  with selector select? result <= '0' when '0', '1' when others;
end architecture;)",
        VhdlStandard::Vhdl2002, "matching selected assignments");
    expect_revision_error(
        "vhdl2002_unaffected",
        R"(architecture rtl of vhdl2002_unaffected is
  signal result : bit;
begin
  process begin result <= unaffected; wait; end process;
end architecture;)",
        VhdlStandard::Vhdl2002,
        "unaffected in sequential signal assignments");
    expect_revision_error(
        "vhdl2002_case_generate",
        R"(architecture rtl of vhdl2002_case_generate is begin
  choose : case 0 generate
    zero_choice : when 0 => null;
    other_choice : when others => null;
  end generate choose;
end architecture;)",
        VhdlStandard::Vhdl2002, "case-generate statements");
    expect_revision_error(
        "vhdl2002_else_generate",
        R"(architecture rtl of vhdl2002_else_generate is begin
  choose : if true generate
  else generate
  end generate choose;
end architecture;)",
        VhdlStandard::Vhdl2002, "if-generate alternatives");

    const auto vhdl_1987_environment = parse_architecture(
        "vhdl87_predefined_environment",
        "  signal data : bit_vector(136 downto 0);\n"
        "  signal delay : time;\n"
        "  signal count : natural;",
        "  delay <= 1 ns;\n  count <= data'length + 137;",
        VhdlStandard::Vhdl1987);
    require(
        vhdl_1987_environment.ok(),
        "VHDL-87 std.standard scalar, vector, time, universal integer and "
        "base array attributes remain implicitly visible");
    const auto vhdl_1993_environment = parse_architecture(
        "vhdl93_predefined_environment",
        "  signal data : bit_vector(136 downto 0);\n"
        "  signal direction : boolean;\n"
        "  signal image_value : string;",
        "  direction <= data'ascending;\n"
        "  image_value <= boolean'image(true);",
        VhdlStandard::Vhdl1993);
    require(
        vhdl_1993_environment.ok(),
        "VHDL-93 ascending and image attributes remain implicitly visible");
    const auto vhdl_2000_environment = parse_architecture(
        "vhdl2000_predefined_environment",
        "  signal data : bit;\n  signal driven : boolean;",
        "  driven <= data'driving;",
        VhdlStandard::Vhdl2000);
    require(
        vhdl_2000_environment.ok(),
        "VHDL-2000 driving attributes remain implicitly visible");
    const auto vhdl_2008_environment = parse_architecture(
        "vhdl2008_predefined_environment",
        "  signal flags : boolean_vector(136 downto 0);\n"
        "  signal label_value : string;",
        "  label_value <= flags'simple_name;",
        VhdlStandard::Vhdl2008);
    require(
        vhdl_2008_environment.ok(),
        "VHDL-2008 vector types and standard-name attributes are visible");

    expect_revision_error(
        "vhdl87_file_open_kind",
        R"(architecture rtl of vhdl87_file_open_kind is begin
  process variable mode : file_open_kind; begin wait; end process;
end architecture;)",
        VhdlStandard::Vhdl1987, "file_open_kind");
    expect_revision_error(
        "vhdl87_ascending",
        R"(architecture rtl of vhdl87_ascending is
  signal data : bit_vector(1 downto 0);
  signal result : boolean;
begin result <= data'ascending; end architecture;)",
        VhdlStandard::Vhdl1987, "ascending");
    expect_revision_error(
        "vhdl87_driving",
        R"(architecture rtl of vhdl87_driving is
  signal data : bit;
  signal result : boolean;
begin result <= data'driving; end architecture;)",
        VhdlStandard::Vhdl1987, "driving");
    expect_revision_error(
        "vhdl2002_boolean_vector",
        R"(architecture rtl of vhdl2002_boolean_vector is
  signal flags : boolean_vector(136 downto 0);
begin end architecture;)",
        VhdlStandard::Vhdl2002, "boolean_vector");
    expect_revision_error(
        "vhdl2002_subtype",
        R"(architecture rtl of vhdl2002_subtype is
  signal data : bit;
begin data <= data'subtype; end architecture;)",
        VhdlStandard::Vhdl2002, "subtype");

    const auto sequential_blocks = parse_text(
        "vhdl2019_sequential_blocks.vhd",
        R"(entity vhdl2019_sequential_blocks is end entity;
architecture rtl of vhdl2019_sequential_blocks is
  signal result : integer;
begin
  exercise : process
  begin
    outer : block is
      constant seed : integer := 2;
      subtype local_integer is integer range 0 to 7;
      variable value : local_integer := seed;
      function bump(input : integer) return integer is
      begin
        return input + 1;
      end function;
      procedure assign(variable target : out integer) is
      begin
        target := bump(seed);
      end procedure;
    begin
      inner : block
        variable nested : integer := bump(value);
      begin
        assign(nested);
        result <= nested;
      end block inner;
    end outer;
    wait;
  end process;
end architecture;)",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        sequential_blocks.ok()
            && sequential_blocks.design.units.back().processes.size() == 1U,
        "VHDL-2019 nested sequential blocks must parse");
    const auto& outer = sequential_blocks.design.units.back()
                            .processes.front().statements.front();
    require(
        outer.kind == StatementKind::Block
            && outer.label == "outer"
            && outer.constants.size() == 1U
            && outer.type_aliases.size() == 1U
            && outer.declarations.size() == 1U
            && outer.functions.size() == 1U
            && outer.procedures.size() == 1U
            && outer.statements.size() == 1U
            && outer.statements.front().kind == StatementKind::Block
            && outer.statements.front().label == "inner"
            && outer.statements.front().declarations.size() == 1U
            && outer.statements.front().statements.size() == 2U,
        "sequential blocks must retain complete nested declarative regions "
        "and source-ordered bodies");
    expect_revision_error(
        "vhdl2008_sequential_block",
        R"(architecture rtl of vhdl2008_sequential_block is begin
  process begin block begin null; end block; wait; end process;
end architecture;)",
        VhdlStandard::Vhdl2008,
        "sequential block statements");

}

void test_vhdl_sequential_for_loops()
{
    const auto result = parse_text(
        "sequential_for.vhd",
        R"(
entity sequential_for is
end entity;
architecture rtl of sequential_for is
  signal ascending : std_logic_vector(2 downto 0);
  signal descending : std_logic_vector(3 downto 1);
begin
  populate: process
  begin
    for lane in 0 to 2 loop
      ascending(lane) <= '1';
    end loop;
    for lane in 3 downto 1 loop
      descending(lane) <= '0';
    end loop;
    while false loop
      null;
    end loop;
    wait for 1 ns;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(result.ok(), "VHDL sequential for loops must parse");
    const auto* architecture = result.design.find(UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture != nullptr
            && architecture->processes.size() == 1
            && architecture->processes.front().statements.size() == 4,
        "VHDL sequential for-loop process");
    const auto& ascending = architecture->processes.front().statements[0];
    const auto& descending = architecture->processes.front().statements[1];
    require(
        ascending.kind == StatementKind::Loop
            && ascending.loop_variable == "lane"
            && !ascending.loop_descending
            && ascending.loop_initial.text == "0"
            && ascending.loop_limit.text == "2"
            && ascending.statements.size() == 1
            && descending.kind == StatementKind::Loop
            && descending.loop_descending
            && descending.loop_initial.text == "3"
            && descending.loop_limit.text == "1"
            && architecture->processes.front()
                    .statements[2]
                    .kind
                == StatementKind::Loop
            && architecture->processes.front()
                .statements[2]
                .loop_runtime
            && architecture->processes.front()
                    .statements[2]
                    .condition.kind
                == ExpressionKind::BooleanLiteral,
        "VHDL sequential for-loop ranges and bodies are retained");

    const auto labeled_end = parse_text(
        "labeled_sequential_for.vhd",
        R"(
architecture rtl of labeled_sequential_for is
begin
  populate: process
  begin
    populate_loop: for lane in 0 to 1 loop
      null;
    end loop populate_loop;
    wait for 1 ns;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(
        labeled_end.ok()
            && labeled_end.design.units.back()
                    .processes.front()
                    .statements.front()
                    .loop_label
                == "populate_loop",
        "matching opening and end labels are retained on VHDL loops");
}

void test_systemverilog_vertical_slice()
{
    constexpr std::string_view source = R"(
module counter(
  input logic clk,
  output logic [7:0] q
);
  logic [7:0] next;
  assign next = q + 1;
  child u_child(.value(q), .result(next));

  always_ff @(posedge clk) begin
    q <= next;
  end

  initial begin
    #10 $finish;
  end
endmodule
)";

    const auto result = parse_text("counter.sv", source, Language::SystemVerilog2017);
    require(result.ok(),
        "SystemVerilog vertical slice must parse without errors");
    require(result.design.units.size() == 1, "SV module count");
    const auto& module = result.design.units.front();
    require(module.kind == UnitKind::VerilogModule, "SV unit kind");
    require(module.name == "counter", "SV module name");
    require(module.ports.size() == 2, "SV ANSI port count");
    require(module.ports[1].type.width() == 8, "SV vector width");
    require(module.signals.size() == 1, "SV signal declaration");
    require(module.concurrent_statements.size() == 1,
        "SV continuous assignment");
    require(module.instances.size() == 1, "SV module instance");
    require(module.instances.front().unit_name == "child"
            && module.instances.front().name == "u_child",
        "SV instance unit and name");
    require(module.instances.front().connections.size() == 2
            && module.instances.front().connections.front().port
                == std::optional<std::string> { "value" },
        "SV named port connection");
    require(module.processes.size() == 2, "SV process count");

    const auto& always = module.processes[0];
    require(always.kind == ProcessKind::SystemVerilogAlwaysFF,
        "always_ff process kind");
    require(always.sensitivities.size() == 1 && always.sensitivities.front().edge == EdgeKind::Positive,
        "SV posedge sensitivity");
    require(always.statements.size() == 1 && always.statements.front().assignment_kind == AssignmentKind::NonBlocking,
        "SV nonblocking assignment");

    const auto& initial = module.processes[1];
    require(initial.kind == ProcessKind::Initial, "initial process kind");
    require(initial.statements.size() == 1 && initial.statements.front().kind == StatementKind::Delay,
        "initial delay statement");
    require(initial.statements.front().delay->magnitude == 10,
        "Verilog delay magnitude");
    require(initial.statements.front().statements.size() == 1 && initial.statements.front().statements.front().kind == StatementKind::Finish,
        "delayed $finish");

    const auto implicit_connections = parse_text(
        "implicit-connections.sv",
        R"(
module implicit_connections(input logic clk, input logic value);
  logic [2:0] sized;
  child shorthand(.clk, .value(value));
  child wildcard(.*);
  initial sized = 3'(value);
endmodule
)",
        Language::SystemVerilog2017);
    require(
        implicit_connections.ok()
            && implicit_connections.design.units.size() == 1
            && implicit_connections.design.units.front().instances.size()
                == 2,
        "SystemVerilog implicit named and wildcard connections parse");
    const auto& shorthand = implicit_connections.design.units.front()
                                .instances[0]
                                .connections;
    const auto& wildcard = implicit_connections.design.units.front()
                               .instances[1]
                               .connections;
    require(
        shorthand.size() == 2
            && shorthand[0].port
                == std::optional<std::string> { "clk" }
            && shorthand[0].value.kind
                == ExpressionKind::Identifier
            && shorthand[0].value.text == "clk"
            && wildcard.size() == 1
            && wildcard[0].port
                == std::optional<std::string> { "*" }
            && implicit_connections.design.units.front()
                   .processes.front().statements.front().value.text
                == "@sv-cast:3"
            && implicit_connections.design.units.front()
                   .processes.front().statements.front().value.call_result_width
                == 3,
        "implicit connections and sized casts retain executable HIR");
}

void test_non_ansi_verilog_ports()
{
    constexpr std::string_view source = R"(
module edge_reg(clk, d, q);
  input clk, d;
  output q;
  reg q;
  always @(posedge clk) q <= d;
endmodule
)";
    const auto result = parse_text("edge_reg.v", source, Language::Verilog2005);
    require(result.ok(), "Verilog-2005 non-ANSI module must parse");
    const auto& module = result.design.units.front();
    require(module.ports.size() == 3,
        "body port declarations must update header placeholders");
    require(module.signals.empty(),
        "a non-ANSI output reg must not become a duplicate signal");
    require(module.ports[2].type.spelling == "reg",
        "a non-ANSI reg redeclaration must refine the port type");
    require(module.processes.front().kind == ProcessKind::VerilogAlways,
        "Verilog always process kind");

    const auto duplicate = parse_text(
        "duplicate_non_ansi_port.v",
        R"(
module duplicate_non_ansi_port(value);
  input value;
  input value;
endmodule
)",
        Language::Verilog2005);
    require(!duplicate.ok(), "duplicate non-ANSI ports must fail");
    require(
        std::ranges::any_of(
            duplicate.diagnostics,
            [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-004";
            }),
        "duplicate non-ANSI ports have a stable diagnostic");
}

void test_diagnostics_and_spans()
{
    const auto result = parse_text(
        "broken.sv", "module broken(input logic a)\nassign = a;\nendmodule",
        Language::SystemVerilog2017);
    require(!result.ok(), "malformed source must fail");
    require(!result.diagnostics.empty(), "malformed source diagnostic");
    require(result.diagnostics.front().span.source_name == "broken.sv",
        "diagnostic source name");
    require(result.diagnostics.front().span.begin.line >= 1,
        "diagnostic line is one-based");
    require(result.diagnostics.front().code.starts_with("FSIM-"),
        "diagnostics have stable codes");
}

void test_vhdl_context_diagnostics()
{
    const auto declaration = parse_text(
        "context_declaration.vhd",
        R"(
context shared_context is
  library ieee;
  use ieee.std_logic_1164.all;
  context work.base;
end context shared_context;
context work.shared_context;
entity context_user is
end entity;
)",
        Language::Vhdl2008);
    require(
        declaration.ok() && declaration.design.units.size() == 2,
        "bounded context declaration and following unit must parse");
    require(
        declaration.design.units[0].kind == UnitKind::VhdlContext
            && declaration.design.units[0].name == "shared_context"
            && declaration.design.units[0].vhdl_context.size() == 3
            && declaration.design.units[0]
                    .vhdl_context.back()
                    .kind
                == VhdlContextItemKind::ContextReference,
        "context declaration retains reusable context items");
    require(
        declaration.design.units[1].vhdl_context.size() == 1
            && declaration.design.units[1]
                    .vhdl_context.front()
                    .selected_names.front()
                == "work.shared_context",
        "context reference remains attached to the following unit");

    const auto malformed = parse_text(
        "malformed_context.vhd",
        "use ieee.; entity context_user is end entity;",
        Language::Vhdl2008);
    require(!malformed.ok(), "a malformed context clause must be rejected");
    require(
        std::any_of(
            malformed.diagnostics.begin(),
            malformed.diagnostics.end(),
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-044";
            }),
        "malformed context clause needs a targeted diagnostic");

    const auto adjacent_identifiers = parse_text(
        "adjacent_context_identifiers.vhd",
        "library e is; entity recovered is end entity;",
        Language::Vhdl2008);
    require(
        !adjacent_identifiers.ok()
            && std::any_of(
                adjacent_identifiers.diagnostics.begin(),
                adjacent_identifiers.diagnostics.end(),
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-PARSE-044";
                }),
        "adjacent context identifiers must be consumed and diagnosed");

    const auto invalid_declaration = parse_text(
        "invalid_context_declaration.vhd",
        R"(
context invalid is
  signal not_a_context_item : bit;
end context mismatched;
entity recovered is
end entity recovered;
)",
        Language::Vhdl2008);
    require(
        !invalid_declaration.ok()
            && invalid_declaration.design.units.size() == 2
            && std::any_of(
                invalid_declaration.diagnostics.begin(),
                invalid_declaration.diagnostics.end(),
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code
                        == "FSIM-VHDL-UNSUPPORTED-024";
                })
            && std::any_of(
                invalid_declaration.diagnostics.begin(),
                invalid_declaration.diagnostics.end(),
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code
                        == "FSIM-VHDL-PARSE-092";
                }),
        "invalid context declaration items and end names are targeted");
}

void test_vhdl_package_constants()
{
    const auto parsed = parse_text(
        "package_constants.vhd",
        R"(
library support;
package constants is
  constant width, lanes : natural := 4;
  constant next_value : integer := width + 1;
  constant enabled : boolean := true;
  constant initial_bit : bit := '1';
  constant deferred_vector : bit_vector(256 downto 0);
end package constants;

package body constants is
  constant deferred_vector : bit_vector(256 downto 0) := (others => '0');
end package body constants;

use work.constants.all;
entity package_user is
end entity package_user;
)",
        Language::Vhdl2008);
    require(parsed.ok(), "bounded VHDL package constants must parse");
    require(
        parsed.design.units.size() == 3,
        "package, package body, and following entity are retained");
    const auto& package = parsed.design.units[0];
    require(
        package.kind == UnitKind::VhdlPackage
            && package.name == "constants"
            && package.parameters.size() == 6,
        "package declaration records each constant");
    require(
        package.parameters[0].name == "width"
            && package.parameters[1].name == "lanes"
            && package.parameters[2].default_value.kind
                == ExpressionKind::Binary
            && package.parameters[3].type.domain
                == ValueDomain::Boolean
            && package.parameters[4].type.domain
                == ValueDomain::Bit2
            && package.parameters[5].vhdl_deferred
            && package.parameters[5].type.width() == 257,
        "package constants retain scalar, arbitrary-width, and deferred forms");
    require(
        package.vhdl_context.size() == 1
            && package.vhdl_context.front().kind
                == VhdlContextItemKind::LibraryClause,
        "package retains its own context");
    require(
        parsed.design.units[2].vhdl_context.size() == 1
            && parsed.design.units[2]
                    .vhdl_context.front()
                    .selected_names.front()
                == "work.constants.all",
        "following unit retains package use visibility");

    const auto selected_names = parse_text(
        "selected_package_names.vhd",
        R"(
entity selected_package_names is
  port (
    observed : out unsigned(
      work.constants.width - 1 downto 0)
  );
end entity selected_package_names;
architecture rtl of selected_package_names is
begin
  observed <= constants.next_value;
end architecture rtl;
)",
        Language::Vhdl2008);
    require(
        selected_names.ok()
            && selected_names.design.units.size() == 2
            && selected_names.design.units[0]
                .ports.front()
                .type.packed_range_expression
            && selected_names.design.units[0]
                    .ports.front()
                    .type.packed_range_expression
                    ->left.operands.front()
                    .text
                == "work.constants.width"
            && selected_names.design.units[1]
                    .concurrent_statements.front()
                    .value.text
                == "constants.next_value",
        "two- and three-part selected package names survive HIR parsing");

    const auto invalid = parse_text(
        "invalid_package_constants.vhd",
        R"(
package invalid_constants is
  constant duplicate : natural := 1;
  constant duplicate : natural := 2;
  constant missing : natural;
  constant vector_value : bit_vector(3 downto 0) := "0000";
end package invalid_constants;
package body invalid_constants is
  constant missing : natural;
end package body invalid_constants;
)",
        Language::Vhdl2008);
    require(!invalid.ok(), "invalid package constants must fail");
    const auto has_code = [&](const std::string_view code) {
        return std::any_of(
            invalid.diagnostics.begin(),
            invalid.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };
    require(
        has_code("FSIM-VHDL-SEM-020")
            && has_code("FSIM-VHDL-PARSE-088")
            && !has_code("FSIM-VHDL-UNSUPPORTED-023"),
        "invalid package constants have targeted diagnostics");

    const auto body = parse_text(
        "package_body.vhd",
        R"(
package body unsupported is
  function identity(value : integer) return integer is
  begin
    return value;
  end function identity;
end package body unsupported;
entity recovered is
end entity recovered;
)",
        Language::Vhdl2008);
    require(
        body.ok()
            && body.design.units.size() == 2
            && body.design.units.front().name == "unsupported"
            && body.design.units.front().primary_name
                == "unsupported"
            && body.design.units.front().functions.size() == 1
            && body.design.units.front().functions.front().defined
            && body.design.units.back().name == "recovered",
        "bounded package function bodies retain HIR and recover at the "
        "outer end clause");
}

void test_ignored_initializers_are_rejected()
{
    const auto vhdl = parse_text(
        "initializers.vhd",
        R"(
entity initializers is
  port (input : in std_logic := '0');
end entity;
architecture rtl of initializers is
  signal state : std_logic := '1';
begin
end architecture;
)",
        Language::Vhdl2008);
    require(vhdl.ok(), "VHDL signal initializers must be retained");
    const auto* entity = vhdl.design.find(UnitKind::VhdlEntity, "initializers");
    require(
        entity != nullptr && entity->ports.size() == 1
            && entity->ports.front().default_value
            && entity->ports.front().default_value->kind
                == ExpressionKind::LogicLiteral,
        "VHDL input-port defaults must remain in entity HIR");
    const auto* architecture = vhdl.design.find(
        UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture != nullptr && architecture->signals.size() == 1
            && architecture->signals.front().default_value
            && architecture->signals.front().default_value->kind
                == ExpressionKind::LogicLiteral,
        "VHDL signal initializers must remain in architecture HIR");

    const auto invalid_port_default = parse_text(
        "invalid-port-default.vhd",
        R"(
entity invalid_port_default is
  port (output_value : out bit := '0');
end entity;
)",
        Language::Vhdl2008);
    require(
        !invalid_port_default.ok()
            && std::ranges::any_of(
                invalid_port_default.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-075";
                }),
        "non-input VHDL entity-port defaults need a targeted diagnostic");

    const auto sv = parse_text(
        "initializers.sv",
        R"(
module initializers(
  input logic input_value = 1'b0
);
  logic state = 1'b1;
endmodule
)",
        Language::SystemVerilog2017);
    require(!sv.ok(), "an unsupported SV port default must be rejected");
    bool port_default_sv = false;
    for (const auto& diagnostic : sv.diagnostics) {
        port_default_sv = port_default_sv
            || diagnostic.code == "FSIM-SV-UNSUPPORTED-010";
    }
    require(port_default_sv, "SV port default needs a targeted diagnostic");
    const auto* sv_unit = sv.design.find(
        UnitKind::VerilogModule, "initializers");
    require(
        sv_unit != nullptr && sv_unit->processes.size() == 1
            && sv_unit->processes.front().name
                == "$declaration_initializer_state"
            && sv_unit->processes.front().statements.size() == 1
            && sv_unit->processes.front().statements.front().value.text
                == "1'b1",
        "SV declaration initializers become explicit initial-process HIR");
}

void test_duplicate_declarations_are_rejected()
{
    const auto vhdl = parse_text(
        "duplicates.vhd",
        R"(
entity duplicates is
  port (value : in std_logic; VALUE : out std_logic);
end entity;
architecture rtl of duplicates is
  signal state : std_logic;
  signal STATE : std_logic;
begin
end architecture;
)",
        Language::Vhdl2008);
    require(!vhdl.ok(), "VHDL duplicate declarations must be rejected");
    bool duplicate_port = false;
    bool duplicate_signal = false;
    for (const auto& diagnostic : vhdl.diagnostics) {
        duplicate_port = duplicate_port || diagnostic.code == "FSIM-VHDL-SEM-002";
        duplicate_signal = duplicate_signal || diagnostic.code == "FSIM-VHDL-SEM-003";
    }
    require(duplicate_port, "VHDL duplicate port diagnostic");
    require(duplicate_signal, "VHDL duplicate signal diagnostic");

    const auto sv = parse_text(
        "duplicates.sv",
        R"(
module duplicates(input logic value, input logic value);
  logic state;
  logic state;
endmodule
)",
        Language::SystemVerilog2017);
    require(!sv.ok(), "SV duplicate declarations must be rejected");
    duplicate_port = false;
    duplicate_signal = false;
    for (const auto& diagnostic : sv.diagnostics) {
        duplicate_port = duplicate_port || diagnostic.code == "FSIM-SV-SEM-003";
        duplicate_signal = duplicate_signal || diagnostic.code == "FSIM-SV-SEM-006";
    }
    require(duplicate_port, "SV duplicate port diagnostic");
    require(duplicate_signal, "SV duplicate signal diagnostic");
}

void test_systemverilog_timescale_context()
{
    const auto result = parse_text(
        "timescale.sv",
        R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)",
        Language::SystemVerilog2017);
    require(result.ok(), "a legal `timescale must be represented");
    require(result.design.units.size() == 1, "timescale module count");
    const auto& unit = result.design.units.front();
    require(unit.time_unit == "10ns", "module time unit");
    require(unit.time_precision == "100ps", "module time precision");
    require(
        unit.processes.size() == 1
            && unit.processes.front().statements.size() == 1,
        "timed initial process");
    const auto& delay = *unit.processes.front().statements.front().delay;
    require(delay.magnitude == 20 && delay.unit == "ns",
        "integer delay must inherit and scale by `timescale");

    const auto mid_module_directive = parse_text(
        "mid_module_timescale.sv",
        R"(`timescale 1ns/1ns
module first;
  `timescale 10ns/1ns
  initial #2 $finish;
endmodule
module second;
  initial #2 $finish;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        mid_module_directive.ok(),
        "a directive inside a module must affect only following modules");
    require(
        mid_module_directive.design.units.size() == 2,
        "mid-module timescale design unit count");
    const auto& first = mid_module_directive.design.units[0];
    const auto& second = mid_module_directive.design.units[1];
    require(
        first.time_unit == "1ns"
            && first.processes.front().statements.front().delay->magnitude == 2,
        "current module must retain the timescale active at its start");
    require(
        second.time_unit == "10ns"
            && second.processes.front().statements.front().delay->magnitude == 20,
        "mid-module directive must apply to a following module");

    const auto invalid_precision = parse_text(
        "bad_timescale.sv",
        "`timescale 1ps/1ns\nmodule bad; endmodule\n",
        Language::SystemVerilog2017);
    require(!invalid_precision.ok(), "coarse precision must be rejected");
    require(
        std::any_of(
            invalid_precision.diagnostics.begin(),
            invalid_precision.diagnostics.end(),
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-009";
            }),
        "coarse precision diagnostic");
}

void test_systemverilog_time_declarations()
{
    const auto parsed = parse_text(
        "time-declarations.sv",
        R"(timeunit 10ns;
timeprecision 1ps;
module inherited_time;
  logic marker;
  initial begin
    #1.25 marker = 1'b1;
    #(2.5ps) marker = 1'b0;
  end
endmodule
module local_time;
  timeunit 1us / 10ns;
  logic marker;
  initial begin
    #1.25e-1 marker = 1'b1;
  end
endmodule
module local_precision;
  timeprecision 100ps;
  initial #1 $finish;
endmodule
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "SystemVerilog time declarations must parse");
    require(
        parsed.design.units.size() == 3,
        "time declaration module count");
    const auto& inherited = parsed.design.units[0];
    require(
        inherited.time_unit == "10ns"
            && inherited.time_precision == "1ps",
        "compilation-unit time declarations attach to a module");
    const auto& inherited_delays = inherited.processes.front().statements;
    require(
        inherited_delays.size() == 2
            && inherited_delays[0].delay
            && inherited_delays[0].delay->magnitude == 50
            && inherited_delays[0].delay->divisor == 4
            && inherited_delays[0].delay->unit == "ns",
        "fractional delay retains an exact rational under inherited timeunit");
    require(
        inherited_delays[1].delay
            && inherited_delays[1].delay->magnitude == 5
            && inherited_delays[1].delay->divisor == 2
            && inherited_delays[1].delay->unit == "ps",
        "an explicit time-literal suffix overrides the module timeunit");

    const auto& local = parsed.design.units[1];
    require(
        local.time_unit == "1us"
            && local.time_precision == "10ns",
        "combined module-local timeunit/timeprecision overrides inheritance");
    const auto& local_delay = *local.processes.front().statements.front().delay;
    require(
        local_delay.magnitude == 1
            && local_delay.divisor == 8
            && local_delay.unit == "us",
        "scientific fractional delays remain exact in typed HIR");
    const auto& local_precision = parsed.design.units[2];
    require(
        local_precision.time_unit == "10ns"
            && local_precision.time_precision == "100ps",
        "a module-local timeprecision overrides inherited precision only");

    const auto directive_precedence = parse_text(
        "time-directive-precedence.sv",
        R"(`timescale 100ns/10ns
timeunit 1ns / 1ps;
`resetall
module declared_time_wins;
  initial #1 $finish;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        directive_precedence.ok()
            && directive_precedence.design.units.front().time_unit == "1ns"
            && directive_precedence.design.units.front().time_precision
                == "1ps"
            && directive_precedence.design.units.front()
                    .processes.front()
                    .statements.front()
                    .delay->magnitude
                == 1
            && directive_precedence.design.units.front()
                    .processes.front()
                    .statements.front()
                    .delay->unit
                == "ns",
        "declarations override directive context and survive `resetall");

    const auto malformed = parse_text(
        "bad-time-declarations.sv",
        R"(timeunit 2ns;
timeunit 1ns;
timeunit 10ns;
module bad;
  timeprecision 100ns;
  timeprecision 1ps;
  logic marker;
  timeunit 1ns;
  initial #0.00000000000000000001 marker = 1'b1;
endmodule
timeprecision 1ps;
)",
        Language::SystemVerilog2017);
    const auto has_code =
        [&](const std::string_view code) {
            return std::ranges::any_of(
                malformed.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    require(
        !malformed.ok()
            && has_code("FSIM-SV-SEM-045")
            && has_code("FSIM-SV-SEM-046")
            && has_code("FSIM-SV-SEM-047")
            && has_code("FSIM-SV-SEM-048")
            && has_code("FSIM-SV-SEM-049"),
        "invalid, duplicate, late, coarse, and overflowing "
        "fractional time forms receive targeted diagnostics");

    const auto unitless = parse_text(
        "unitless-fractional.sv",
        "module bad; initial #1.5 $finish; endmodule\n",
        Language::SystemVerilog2017);
    require(
        !unitless.ok()
            && std::ranges::any_of(
                unitless.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-050";
                }),
        "a unitless fractional delay without time context is rejected");

    const auto verilog = parse_text(
        "verilog-timeunit.v",
        "timeunit 1ns; module bad; endmodule\n",
        Language::Verilog2005);
    require(
        !verilog.ok()
            && std::ranges::any_of(
                verilog.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-044";
                }),
        "time declarations are rejected in Verilog-2005");

    const auto verilog_suffix = parse_text(
        "verilog-time-suffix.v",
        "module bad; initial #1ns $finish; endmodule\n",
        Language::Verilog2005);
    require(
        !verilog_suffix.ok()
            && std::ranges::any_of(
                verilog_suffix.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-051";
                }),
        "explicit delay-unit suffixes are rejected in Verilog-2005");

    const auto verilog_identifier = parse_text(
        "verilog-timeunit-identifier.v",
        R"(module timeunit;
endmodule
module top;
  timeunit child();
endmodule
)",
        Language::Verilog2005);
    require(
        verilog_identifier.ok()
            && verilog_identifier.design.units[1].instances.size() == 1,
        "timeunit remains an ordinary Verilog-2005 identifier when its "
        "syntax is not a declaration");
}

void test_systemverilog_delay_triples()
{
    const auto parsed = parse_text(
        "delay-triples.sv",
        R"(timeunit 1ns / 1ps;
module delay_triples;
  logic source;
  logic continuous_result;
  logic gate_result;
  wire listed_result;
  wire listed_inverted;
  event fired;

  assign #(
      1e-3:2e-3:3e-3,
      4ps:5ps:6ps,
      7fs:8fs:9fs) continuous_result = source;
  buf #(1ps:2ps:3ps, 4ps:5ps:6ps) (gate_result, source);
  assign (weak0, strong1) #(2ps, 3ps, 4ps)
      listed_result = source, listed_inverted = ~source;

  initial begin
    #(0.1:0.2:0.3) source = 1'b1;
    source = #(4ps:5ps:6ps) 1'b0;
    source <= #(7ps:8ps:9ps) 1'b1;
    ->> #(10ps:11ps:12ps) fired;
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "SystemVerilog delay triples must parse");
    const auto& unit = parsed.design.units.front();
    require(
        unit.concurrent_statements.size() == 4,
        "continuous lists and gate delay triples are retained");
    require(
        unit.concurrent_statements[2].target.text == "listed_result"
            && unit.concurrent_statements[3].target.text
                == "listed_inverted"
            && unit.concurrent_statements[2].delay
            && unit.concurrent_statements[3].delay
            && unit.concurrent_statements[2].delay->magnitude == 2
            && unit.concurrent_statements[3].delay->magnitude == 2
            && unit.concurrent_statements[2].delay->additional_values.size() == 2
            && unit.concurrent_statements[3].delay->additional_values.size() == 2
            && unit.concurrent_statements[2].delay->additional_values[0].magnitude == 3
            && unit.concurrent_statements[3].delay->additional_values[0].magnitude == 3
            && unit.concurrent_statements[2].delay->additional_values[1].magnitude == 4
            && unit.concurrent_statements[3].delay->additional_values[1].magnitude == 4
            && unit.concurrent_statements[2].verilog_drive_strength
            && unit.concurrent_statements[3].verilog_drive_strength
            && unit.concurrent_statements[2].verilog_drive_strength->zero
                == VerilogStrength::Weak
            && unit.concurrent_statements[3].verilog_drive_strength->zero
                == VerilogStrength::Weak
            && unit.concurrent_statements[2].verilog_drive_strength->one
                == VerilogStrength::Strong
            && unit.concurrent_statements[3].verilog_drive_strength->one
                == VerilogStrength::Strong,
        "each net assignment retains the shared delay and strength prefix");
    const auto& process = unit.processes.front().statements;
    require(
        process.size() == 4
            && process[0].kind == StatementKind::Delay
            && process[0].statements.size() == 1
            && process[1].kind == StatementKind::Assignment
            && process[1].assignment_kind == AssignmentKind::Blocking
            && process[2].kind == StatementKind::Assignment
            && process[2].assignment_kind == AssignmentKind::NonBlocking
            && process[3].kind == StatementKind::EventTrigger
            && process[3].assignment_kind == AssignmentKind::NonBlocking,
        "procedural, assignment, and named-event triple forms");

    const auto verify =
        [](const Delay& delay,
            const std::array<std::uint64_t, 3>& magnitudes,
            const std::array<std::uint64_t, 3>& divisors,
            const std::string_view unit_name) {
            require(
                delay.minimum && delay.typical && delay.maximum,
                "all min:typ:max HIR branches are present");
            require(
                delay.minimum->magnitude == magnitudes[0]
                    && delay.typical->magnitude == magnitudes[1]
                    && delay.maximum->magnitude == magnitudes[2]
                    && delay.minimum->divisor == divisors[0]
                    && delay.typical->divisor == divisors[1]
                    && delay.maximum->divisor == divisors[2]
                    && delay.minimum->unit == unit_name
                    && delay.typical->unit == unit_name
                    && delay.maximum->unit == unit_name,
                "delay triple values retain exact magnitudes, divisors, and units");
            require(
                delay.magnitude == magnitudes[1]
                    && delay.divisor == divisors[1]
                    && delay.unit == unit_name,
                "typed HIR defaults a delay triple to its typical branch");
        };

    verify(
        *unit.concurrent_statements[0].delay,
        { 1, 1, 3 },
        { 1000, 500, 1000 },
        "ns");
    verify(
        *unit.concurrent_statements[1].delay,
        { 1, 2, 3 },
        { 1, 1, 1 },
        "ps");
    require(
        unit.concurrent_statements[0].delay->additional_values.size() == 2
            && unit.concurrent_statements[1].delay->additional_values.size()
                == 1,
        "continuous and gate rise/fall/turnoff delay-list arity");
    verify(
        unit.concurrent_statements[0].delay->additional_values[0],
        { 4, 5, 6 },
        { 1, 1, 1 },
        "ps");
    verify(
        unit.concurrent_statements[0].delay->additional_values[1],
        { 7, 8, 9 },
        { 1, 1, 1 },
        "fs");
    verify(
        unit.concurrent_statements[1].delay->additional_values[0],
        { 4, 5, 6 },
        { 1, 1, 1 },
        "ps");
    verify(*process[0].delay, { 1, 1, 3 }, { 10, 5, 10 }, "ns");
    verify(*process[1].delay, { 4, 5, 6 }, { 1, 1, 1 }, "ps");
    verify(*process[2].delay, { 7, 8, 9 }, { 1, 1, 1 }, "ps");
    verify(*process[3].delay, { 10, 11, 12 }, { 1, 1, 1 }, "ps");

    const auto parameterized = parse_text(
        "parameterized-delays.sv",
        R"(timeunit 10ps / 1ps;
module parameterized_delays #(
    parameter int RISE = 2,
    localparam int FALL = RISE + 1);
  logic source;
  wire result;
  wire #(RISE, FALL, RISE + 2) net_result;
  wire #2 initialized = source;
  assign #(RISE:FALL:RISE + 2, FALL) result = source;
  assign net_result = source;
  initial #(RISE + 1) source = 1'b1;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        parameterized.ok(),
        "locally constant parameterized delay expressions must parse");
    const auto& parameterized_unit = parameterized.design.units.front();
    const auto explicit_driver = std::ranges::find_if(
        parameterized_unit.concurrent_statements,
        [](const Statement& statement) {
            return statement.target.text == "result";
        });
    require(
        explicit_driver
            != parameterized_unit.concurrent_statements.end(),
        "parameterized continuous driver is retained");
    const auto& continuous = *explicit_driver->delay;
    require(
        continuous.expression
            && continuous.expression->kind == ExpressionKind::Identifier
            && continuous.expression->text == "FALL"
            && continuous.magnitude == 10
            && continuous.unit == "ps"
            && continuous.minimum && continuous.minimum->expression
            && continuous.typical && continuous.typical->expression
            && continuous.maximum && continuous.maximum->expression
            && continuous.additional_values.size() == 1
            && continuous.additional_values.front().expression,
        "parameterized transition-delay HIR retains expressions and time scale");
    const auto& procedural = *parameterized_unit.processes.front().statements.front().delay;
    require(
        procedural.expression
            && procedural.expression->kind == ExpressionKind::Binary
            && procedural.expression->text == "+"
            && procedural.magnitude == 10
            && procedural.unit == "ps",
        "parameterized procedural-delay HIR retains its expression tree");
    const auto net_result = std::ranges::find_if(
        parameterized_unit.signals,
        [](const SignalDeclaration& signal) {
            return signal.name == "net_result";
        });
    const auto initialized = std::ranges::find_if(
        parameterized_unit.signals,
        [](const SignalDeclaration& signal) {
            return signal.name == "initialized";
        });
    require(
        net_result != parameterized_unit.signals.end()
            && net_result->net_delay
            && net_result->net_delay->expression
            && net_result->net_delay->additional_values.size() == 2
            && initialized != parameterized_unit.signals.end()
            && initialized->net_delay
            && initialized->net_delay->magnitude == 20
            && std::ranges::any_of(
                parameterized_unit.concurrent_statements,
                [](const Statement& statement) {
                    return statement.target.text == "initialized"
                        && statement.assignment_kind
                        == AssignmentKind::Continuous;
                }),
        "net-declaration delays and declaration assignments retain HIR");

    const auto invalid_net_delay = parse_text(
        "invalid-net-delay.sv",
        "module invalid_net_delay; logic #2 value; endmodule\n",
        Language::SystemVerilog2017);
    require(
        !invalid_net_delay.ok()
            && std::ranges::any_of(
                invalid_net_delay.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-110";
                }),
        "net-declaration delays reject variable data types");

    const auto malformed = parse_text(
        "bad-delay-triples.sv",
        R"(module bad_delay_triples;
  wire source;
  wire result;
  assign #(1, 2, 3, 4) result = source;
  assign result = source, ;
  buf #(1, 2, 3) (result, source);
  initial begin
    #1:2:3;
    #(1:2);
    #(1::3);
    #(1, 2);
    #(1,);
  end
endmodule
)",
        Language::SystemVerilog2017);
    const auto has_code =
        [&](const std::string_view code) {
            return std::ranges::any_of(
                malformed.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    require(
        !malformed.ok()
            && has_code("FSIM-SV-SEM-052")
            && has_code("FSIM-SV-SEM-053")
            && has_code("FSIM-SV-PARSE-134")
            && has_code("FSIM-SV-PARSE-135")
            && has_code("FSIM-SV-PARSE-009")
            && has_code("FSIM-SV-PARSE-023"),
        "malformed triples and illegal transition-delay lists are targeted");
}

void test_systemverilog_procedural_assignment_controls()
{
    const auto parsed = parse_text(
        "assignment-controls.sv",
        R"(timeunit 1ns / 1ps;
module assignment_controls;
  logic clock;
  logic enable;
  logic source;
  logic result;

  initial begin
    result = #3 source;
    result <= #(1:2:3) source;
    result = @(posedge clock) source;
    result <= @(negedge clock or enable) source;
    result = @clock source;
    result <= @* source;
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(
        parsed.ok(),
        "blocking and nonblocking procedural assignment controls parse");
    const auto& statements = parsed.design.units.front().processes.front().statements;
    require(
        statements.size() == 6,
        "all procedural assignment control forms are retained");
    require(
        statements[0].procedural_assignment_control
                == ProceduralAssignmentControl::Delay
            && statements[0].assignment_kind
                == AssignmentKind::Blocking
            && statements[0].delay
            && statements[0].delay->magnitude == 3,
        "blocking intra-assignment delay has typed HIR");
    require(
        statements[1].procedural_assignment_control
                == ProceduralAssignmentControl::Delay
            && statements[1].assignment_kind
                == AssignmentKind::NonBlocking
            && statements[1].delay
            && statements[1].delay->minimum
            && statements[1].delay->typical
            && statements[1].delay->maximum,
        "nonblocking min/typ/max assignment delay is retained");
    require(
        statements[2].procedural_assignment_control
                == ProceduralAssignmentControl::Event
            && statements[2].sensitivities.size() == 1
            && statements[2].sensitivities.front().edge
                == EdgeKind::Positive
            && statements[2].sensitivities.front().signal
                == "clock",
        "blocking edge event control has typed sensitivity HIR");
    require(
        statements[3].procedural_assignment_control
                == ProceduralAssignmentControl::Event
            && statements[3].assignment_kind
                == AssignmentKind::NonBlocking
            && statements[3].sensitivities.size() == 2
            && statements[3].sensitivities[0].edge
                == EdgeKind::Negative
            && statements[3].sensitivities[1].edge
                == EdgeKind::Any,
        "nonblocking event lists preserve source order and edge kinds");
    require(
        statements[4].sensitivities.size() == 1
            && statements[4].sensitivities.front().signal == "clock",
        "an unparenthesized scalar event expression is accepted");
    require(
        statements[5].sensitivities.size() == 1
            && statements[5].sensitivities.front().signal == "*",
        "wildcard assignment event control is retained for RHS inference");

    const auto has_code =
        [](const ParseResult& result, const std::string_view code) {
            return std::ranges::any_of(
                result.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    const auto empty = parse_text(
        "empty-assignment-event.sv",
        R"(module empty_assignment_event;
  logic source;
  logic result;
  initial result = @() source;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        !empty.ok() && has_code(empty, "FSIM-SV-PARSE-136"),
        "an empty assignment event list has a stable diagnostic");

    const auto repeated = parse_text(
        "repeated-assignment-control.sv",
        R"(module repeated_assignment_control;
  logic clock;
  logic source;
  logic result;
  initial result <= #1 @(posedge clock) source;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        !repeated.ok()
            && has_code(repeated, "FSIM-SV-SEM-054"),
        "a repeated assignment control has a stable diagnostic");

    const auto repeat_event = parse_text(
        "repeat-assignment-event.sv",
        R"(module repeat_assignment_event;
  logic clock;
  logic source;
  logic result;
  initial result <= repeat (2) @(posedge clock) source;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        repeat_event.ok()
            && repeat_event.design.units.front().processes.front().statements.front().procedural_assignment_repeat
            && repeat_event.design.units.front().processes.front().statements.front().loop_limit.text == "2"
            && repeat_event.design.units.front().processes.front().statements.front().sensitivities.front().edge
                == EdgeKind::Positive,
        "repeated NBA event control retains count and event metadata");

    const auto restricted = parse_text(
        "restricted-assignment-controls.sv",
        R"(module restricted_assignment_controls;
  logic clock;
  logic source;
  logic result;
  always_comb result = #1 source;
  final result = @(posedge clock) source;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        !restricted.ok()
            && has_code(restricted, "FSIM-SV-SEM-012")
            && has_code(restricted, "FSIM-SV-SEM-032"),
        "assignment controls participate in procedural restriction checks");
}

} // namespace fsim::tests::frontend
