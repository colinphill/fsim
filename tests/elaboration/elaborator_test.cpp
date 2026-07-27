// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool has_diagnostic(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    constexpr std::string_view source = R"(
module counter(input logic clk, output logic [7:0] q);
  logic [7:0] next;
  assign next = q + 1;
  always_ff @(posedge clk) q <= next;
endmodule
)";
    const auto parsed = fsim::frontend::parse_text(
        "counter.sv", source, fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    auto elaborated = fsim::elaboration::elaborate(parsed.design, "sv:work.counter");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->signals().size() == 3);
    assert(elaborated.design->processes().size() == 2);
    assert(elaborated.design->specializations().size() == 1);
    const auto& counter_specialization =
        elaborated.design->specializations().front();
    assert(counter_specialization.id == 0);
    assert(counter_specialization.unit == "sv:work.counter");
    assert(counter_specialization.instance == "counter");
    assert(counter_specialization.source == "counter.sv");
    assert(
        counter_specialization.language
        == fsim::frontend::Language::SystemVerilog2017);
    assert(counter_specialization.library == "work");
    assert(counter_specialization.parameter_values.empty());
    assert(!counter_specialization.is_cell);
    assert((
        counter_specialization.processes
        == std::vector<fsim::runtime::simir::ProcessId>{0, 1}));

    auto interpreter = elaborated.design->create_interpreter();
    const auto clock = elaborated.design->find_signal("clk");
    const auto q = elaborated.design->find_signal("q");
    assert(clock && q);
    interpreter->deposit_signal(*q, fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    interpreter->start();
    (void)interpreter->run();

    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "00000001");

    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)interpreter->run();
    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "00000010");

    const auto directive_parsed = fsim::frontend::parse_text(
        "directives.sv",
        R"(
`celldefine
module pulled_child(input logic value);
endmodule
`endcelldefine
`unconnected_drive pull1
module pulled_parent;
  pulled_child child_instance();
endmodule
`nounconnected_drive
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(directive_parsed.ok());
    const auto directive_elaborated = fsim::elaboration::elaborate(
        directive_parsed.design, "sv:work.pulled_parent");
    assert(directive_elaborated.ok());
    const auto pulled_value =
        directive_elaborated.design->find_signal(
            "pulled_parent.child_instance.value");
    assert(pulled_value);
    const auto directive_interpreter =
        directive_elaborated.design->create_interpreter();
    assert(
        directive_interpreter->signal_value(*pulled_value).to_msb_string()
        == "1");
    assert(directive_elaborated.design->specializations().size() == 2);
    const auto& directive_specializations =
        directive_elaborated.design->specializations();
    assert(!directive_specializations.front().is_cell);
    assert(directive_specializations.back().is_cell);

    const auto implicit_parsed = fsim::frontend::parse_text(
        "implicit.sv",
        R"(
`default_nettype tri0
module implicit_top;
  assign created = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(implicit_parsed.ok());
    const auto implicit_elaborated = fsim::elaboration::elaborate(
        implicit_parsed.design, "sv:work.implicit_top");
    assert(implicit_elaborated.ok());
    const auto created =
        implicit_elaborated.design->find_signal("created");
    assert(created);
    auto implicit_interpreter =
        implicit_elaborated.design->create_interpreter();
    assert(
        implicit_interpreter->signal_value(*created).to_msb_string()
        == "0");
    implicit_interpreter->start();
    (void)implicit_interpreter->run();
    assert(
        implicit_interpreter->signal_value(*created).to_msb_string()
        == "1");

    const auto parameterized_parsed = fsim::frontend::parse_text(
        "parameterized.sv",
        R"(
module parameterized #(
  parameter int WIDTH = 8,
  parameter int INCREMENT = 1,
  localparam int LAST = WIDTH - 1
) (
  input logic clk,
  output logic [WIDTH - 1:0] q
);
  logic [WIDTH - 1:0] next;
  assign next = q + INCREMENT;
  always_ff @(posedge clk) q <= next;
endmodule

module parameterized_top(
  input logic clk,
  output logic [3:0] q4,
  output logic [7:0] q8
);
  parameterized #(.WIDTH(4), .INCREMENT(2)) four(
    .clk(clk), .q(q4)
  );
  parameterized #(8, 3) eight(
    .clk(clk), .q(q8)
  );
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parameterized_parsed.ok());
    const auto parameterized_elaborated =
        fsim::elaboration::elaborate(
            parameterized_parsed.design,
            "sv:work.parameterized_top");
    if (!parameterized_elaborated.ok()) {
        for (const auto& diagnostic :
             parameterized_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parameterized_elaborated.ok());
    assert(
        parameterized_elaborated.design->specializations().size() == 3);
    const auto& parameter_specializations =
        parameterized_elaborated.design->specializations();
    assert(parameter_specializations[0].parameter_values.empty());
    assert((
        parameter_specializations[1].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"WIDTH", "4"},
            {"INCREMENT", "2"},
            {"LAST", "3"}}));
    assert((
        parameter_specializations[2].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"WIDTH", "8"},
            {"INCREMENT", "3"},
            {"LAST", "7"}}));
    const auto parameter_clock =
        parameterized_elaborated.design->find_signal("clk");
    const auto parameter_q4 =
        parameterized_elaborated.design->find_signal("q4");
    const auto parameter_q8 =
        parameterized_elaborated.design->find_signal("q8");
    const auto parameter_next4 =
        parameterized_elaborated.design->find_signal(
            "parameterized_top.four.next");
    const auto parameter_next8 =
        parameterized_elaborated.design->find_signal(
            "parameterized_top.eight.next");
    assert(
        parameter_clock && parameter_q4 && parameter_q8
        && parameter_next4 && parameter_next8);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_q4).width
        == 4);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_q8).width
        == 8);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_next4).width
        == 4);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_next8).width
        == 8);
    auto parameter_interpreter =
        parameterized_elaborated.design->create_interpreter();
    parameter_interpreter->deposit_signal(
        *parameter_clock,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    parameter_interpreter->deposit_signal(
        *parameter_q4,
        fsim::runtime::PackedLogic4::from_msb_string("0000"));
    parameter_interpreter->deposit_signal(
        *parameter_q8,
        fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    parameter_interpreter->start();
    (void)parameter_interpreter->run();
    parameter_interpreter->deposit_signal(
        *parameter_clock,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)parameter_interpreter->run();
    assert(
        parameter_interpreter->signal_value(*parameter_q4).to_msb_string()
        == "0010");
    assert(
        parameter_interpreter->signal_value(*parameter_q8).to_msb_string()
        == "00000011");

    const auto invalid_parameters = fsim::frontend::parse_text(
        "invalid-parameter-elaboration.sv",
        R"(
module invalid_parameter_target #(
  parameter int WIDTH = 4,
  localparam int LOCAL_WIDTH = WIDTH,
  parameter int BROKEN = 1 / 0
) ();
endmodule
module invalid_parameter_top;
  invalid_parameter_target #(.MISSING(2)) unknown();
  invalid_parameter_target #(.LOCAL_WIDTH(2)) local_override();
  invalid_parameter_target #(1, 2, 3) excessive();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_parameters.ok());
    const auto rejected_parameters = fsim::elaboration::elaborate(
        invalid_parameters.design,
        "sv:work.invalid_parameter_top");
    assert(!rejected_parameters.ok());
    assert(has_diagnostic(
        rejected_parameters, "FSIM-ELAB-PARAM-001"));
    assert(has_diagnostic(
        rejected_parameters, "FSIM-ELAB-PARAM-005"));

    const auto generic_parsed = fsim::frontend::parse_text(
        "generic-specialization.vhd",
        R"(
entity generic_counter is
  generic (
    width : positive := 8;
    increment : natural := 1;
    enabled : boolean := true;
    last : integer := width - 1
  );
  port (
    clk : in std_logic;
    q : out unsigned(last downto 0)
  );
end entity;

architecture rtl of generic_counter is
  signal next_value : unsigned(last downto 0);
begin
  next_value <= q + increment;
  update: process(clk)
  begin
    if rising_edge(clk) then
      if enabled then
        q <= next_value;
      else
        q <= q;
      end if;
    end if;
  end process;
end architecture;

entity generic_top is
  port (
    clk : in std_logic;
    q4 : out unsigned(3 downto 0);
    q8 : out unsigned(7 downto 0)
  );
end entity;

architecture rtl of generic_top is
begin
  four: entity work.generic_counter(rtl)
    generic map (
      width => 4,
      increment => 2
    )
    port map (
      clk => clk,
      q => q4
    );
  eight: entity work.generic_counter(rtl)
    generic map (
      8,
      increment => 3,
      enabled => false
    )
    port map (
      clk => clk,
      q => q8
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(generic_parsed.ok());
    const auto generic_elaborated =
        fsim::elaboration::elaborate(
            generic_parsed.design,
            "vhdl:work.generic_top(rtl)");
    if (!generic_elaborated.ok()) {
        for (const auto& diagnostic :
             generic_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(generic_elaborated.ok());
    assert(
        generic_elaborated.design->specializations().size() == 3);
    const auto& generic_specializations =
        generic_elaborated.design->specializations();
    assert(generic_specializations[0].parameter_values.empty());
    assert((
        generic_specializations[1].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"increment", "2"},
            {"enabled", "true"},
            {"last", "3"}}));
    assert((
        generic_specializations[2].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "8"},
            {"increment", "3"},
            {"enabled", "false"},
            {"last", "7"}}));
    const auto generic_clock =
        generic_elaborated.design->find_signal("clk");
    const auto generic_q4 =
        generic_elaborated.design->find_signal("q4");
    const auto generic_q8 =
        generic_elaborated.design->find_signal("q8");
    const auto generic_next4 =
        generic_elaborated.design->find_signal(
            "generic_top.four.next_value");
    const auto generic_next8 =
        generic_elaborated.design->find_signal(
            "generic_top.eight.next_value");
    assert(
        generic_clock && generic_q4 && generic_q8
        && generic_next4 && generic_next8);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_q4).width
        == 4);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_q8).width
        == 8);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_next4).width
        == 4);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_next8).width
        == 8);
    auto generic_interpreter =
        generic_elaborated.design->create_interpreter();
    generic_interpreter->deposit_signal(
        *generic_clock,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    generic_interpreter->deposit_signal(
        *generic_q4,
        fsim::runtime::PackedLogic4::from_msb_string("0000"));
    generic_interpreter->deposit_signal(
        *generic_q8,
        fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    generic_interpreter->start();
    (void)generic_interpreter->run();
    generic_interpreter->deposit_signal(
        *generic_clock,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)generic_interpreter->run();
    assert(
        generic_interpreter
            ->signal_value(*generic_q4)
            .to_msb_string()
        == "0010");
    assert(
        generic_interpreter
            ->signal_value(*generic_q8)
            .to_msb_string()
        == "00000000");

    const auto invalid_generics = fsim::frontend::parse_text(
        "invalid-generic-elaboration.vhd",
        R"(
entity invalid_generic_target is
  generic (
    required_value : integer;
    natural_value : natural := -1;
    broken_value : integer := missing_value
  );
end entity;
architecture rtl of invalid_generic_target is
begin
end architecture;
entity invalid_generic_top is
end entity;
architecture rtl of invalid_generic_top is
begin
  unknown: entity work.invalid_generic_target(rtl)
    generic map (missing => 2)
    port map ();
  excessive: entity work.invalid_generic_target(rtl)
    generic map (1, 2, 3, 4)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid_generics.ok());
    const auto rejected_generics =
        fsim::elaboration::elaborate(
            invalid_generics.design,
            "vhdl:work.invalid_generic_top(rtl)");
    assert(!rejected_generics.ok());
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-001"));
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-005"));
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-008"));

    auto mixed_actual_sv = fsim::frontend::parse_text(
        "mixed-actuals.sv",
        R"(
module sv_generic_host(output logic [3:0] q);
  vhdl_bound #(.WIDTH(4), .VALUE(5)) child(.q(q));
endmodule

module sv_parameter_child #(
  parameter width = 1,
  parameter value = 1,
  localparam last = width - 1
) (
  output logic [last:0] q
);
  initial q = value;
endmodule

module ambiguous_parameter_child #(
  parameter WIDTH = 1,
  parameter width = 2
) (
  output logic q
);
  initial q = 1'b0;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto mixed_actual_vhdl = fsim::frontend::parse_text(
        "mixed-actuals.vhd",
        R"(
entity vhdl_generic_child is
  generic (
    width : positive := 1;
    value : natural := 1;
    last : integer := width - 1
  );
  port (
    q : out unsigned(last downto 0)
  );
end entity;
architecture rtl of vhdl_generic_child is
begin
  q <= value;
end architecture;

entity vhdl_parameter_host is
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of vhdl_parameter_host is
begin
  child: entity work.foreign_parameter(rtl)
    generic map (
      4,
      value => 6
    )
    port map (
      q => q
    );
end architecture;

entity ambiguous_parameter_host is
  port (
    q : out std_logic
  );
end entity;
architecture rtl of ambiguous_parameter_host is
begin
  child: entity work.foreign_ambiguous(rtl)
    generic map (
      width => 1
    )
    port map (
      q => q
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(mixed_actual_sv.ok());
    assert(mixed_actual_vhdl.ok());
    fsim::frontend::ParsedDesign mixed_actual_design =
        std::move(mixed_actual_sv.design);
    mixed_actual_design.units.insert(
        mixed_actual_design.units.end(),
        std::make_move_iterator(
            mixed_actual_vhdl.design.units.begin()),
        std::make_move_iterator(
            mixed_actual_vhdl.design.units.end()));

    const std::vector<fsim::elaboration::Binding>
        sv_to_vhdl_actual_binding{
            {"sv_generic_host.child",
             "vhdl:work.vhdl_generic_child(rtl)",
             std::nullopt}};
    const auto sv_to_vhdl_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "sv:work.sv_generic_host",
            sv_to_vhdl_actual_binding);
    if (!sv_to_vhdl_actual.ok()) {
        for (const auto& diagnostic :
             sv_to_vhdl_actual.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(sv_to_vhdl_actual.ok());
    assert(
        sv_to_vhdl_actual.design->specializations().size() == 2);
    assert((
        sv_to_vhdl_actual.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"value", "5"},
            {"last", "3"}}));
    const auto sv_to_vhdl_q =
        sv_to_vhdl_actual.design->find_signal("q");
    assert(sv_to_vhdl_q);
    auto sv_to_vhdl_interpreter =
        sv_to_vhdl_actual.design->create_interpreter();
    const auto sv_to_vhdl_result =
        sv_to_vhdl_interpreter->run();
    assert(
        sv_to_vhdl_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        sv_to_vhdl_interpreter
            ->signal_value(*sv_to_vhdl_q)
            .to_msb_string()
        == "0101");

    const std::vector<fsim::elaboration::Binding>
        vhdl_to_sv_actual_binding{
            {"vhdl_parameter_host.child",
             "sv:work.sv_parameter_child",
             std::nullopt}};
    const auto vhdl_to_sv_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "vhdl:work.vhdl_parameter_host(rtl)",
            vhdl_to_sv_actual_binding);
    if (!vhdl_to_sv_actual.ok()) {
        for (const auto& diagnostic :
             vhdl_to_sv_actual.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_to_sv_actual.ok());
    assert(
        vhdl_to_sv_actual.design->specializations().size() == 2);
    assert((
        vhdl_to_sv_actual.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"value", "6"},
            {"last", "3"}}));
    const auto vhdl_to_sv_q =
        vhdl_to_sv_actual.design->find_signal("q");
    assert(vhdl_to_sv_q);
    auto vhdl_to_sv_interpreter =
        vhdl_to_sv_actual.design->create_interpreter();
    const auto vhdl_to_sv_result =
        vhdl_to_sv_interpreter->run();
    assert(
        vhdl_to_sv_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_to_sv_interpreter
            ->signal_value(*vhdl_to_sv_q)
            .to_msb_string()
        == "0110");

    const std::vector<fsim::elaboration::Binding>
        ambiguous_actual_binding{
            {"ambiguous_parameter_host.child",
             "sv:work.ambiguous_parameter_child",
             std::nullopt}};
    const auto ambiguous_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "vhdl:work.ambiguous_parameter_host(rtl)",
            ambiguous_actual_binding);
    assert(!ambiguous_actual.ok());
    assert(has_diagnostic(
        ambiguous_actual, "FSIM-ELAB-PARAM-009"));

    constexpr std::string_view vhdl_source = R"(
entity counter_vhdl is
  port (
    clk : in std_logic;
    q : out std_logic_vector(7 downto 0)
  );
end entity counter_vhdl;

architecture rtl of counter_vhdl is
  signal count : std_logic_vector(7 downto 0);
begin
  q <= count;
  update: process(clk)
  begin
    if rising_edge(clk) then
      count <= count + 1;
    end if;
  end process update;
end architecture rtl;
)";
    const auto parsed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", vhdl_source, fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl.ok());
    auto elaborated_vhdl =
        fsim::elaboration::elaborate(parsed_vhdl.design, "counter_vhdl");
    assert(elaborated_vhdl.ok());

    auto vhdl_interpreter = elaborated_vhdl.design->create_interpreter();
    const auto vhdl_clock = elaborated_vhdl.design->find_signal("clk");
    const auto count = elaborated_vhdl.design->find_signal("count");
    const auto vhdl_q = elaborated_vhdl.design->find_signal("q");
    assert(vhdl_clock && count && vhdl_q);
    vhdl_interpreter->deposit_signal(
        *count, fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    vhdl_interpreter->start();
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000000");
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000001");

    constexpr std::string_view mixed_sv = R"(
module tb;
  logic clk;
  logic reset;
  logic [7:0] q;
  logic [7:0] inverted;

  counter u_counter(.clk(clk), .reset(reset), .q(q));
  child u_child(.value(q), .inverted(inverted));

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 $finish;
  end
endmodule

module child(
  input logic [7:0] value,
  output logic [7:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    constexpr std::string_view mixed_vhdl = R"(
entity counter is
  port (
    clk : in std_logic;
    reset : in std_logic;
    q : out unsigned(7 downto 0)
  );
end entity counter;

architecture rtl of counter is
begin
  update: process(clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        q <= "00000000";
      else
        q <= q + 1;
      end if;
    end if;
  end process update;
end architecture rtl;
)";
    auto parsed_mixed_sv = fsim::frontend::parse_text(
        "tb.sv", mixed_sv, fsim::frontend::Language::SystemVerilog2017);
    auto parsed_mixed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", mixed_vhdl, fsim::frontend::Language::Vhdl2008);
    assert(parsed_mixed_sv.ok() && parsed_mixed_vhdl.ok());
    for (auto& unit : parsed_mixed_vhdl.design.units) {
        parsed_mixed_sv.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> mixed_bindings{
        {"tb.u_counter", "vhdl:work.counter(rtl)", std::nullopt},
        {"tb.u_child", "sv:work.child", std::nullopt},
    };
    auto elaborated_mixed = fsim::elaboration::elaborate(
        parsed_mixed_sv.design, "sv:work.tb", mixed_bindings);
    if (!elaborated_mixed.ok()) {
        for (const auto& diagnostic : elaborated_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated_mixed.ok());
    assert(elaborated_mixed.design->signals().size() == 4);
    assert(elaborated_mixed.design->specializations().size() == 3);
    const auto& mixed_specializations =
        elaborated_mixed.design->specializations();
    assert(mixed_specializations[0].id == 0);
    assert(mixed_specializations[0].unit == "sv:work.tb");
    assert(mixed_specializations[0].instance == "tb");
    assert(mixed_specializations[0].source == "tb.sv");
    assert((
        mixed_specializations[0].processes
        == std::vector<fsim::runtime::simir::ProcessId>{0}));
    assert(mixed_specializations[1].id == 1);
    assert(
        mixed_specializations[1].unit
        == "vhdl:work.counter(rtl)");
    assert(mixed_specializations[1].instance == "tb.u_counter");
    assert(mixed_specializations[1].source == "counter.vhd");
    assert((
        mixed_specializations[1].processes
        == std::vector<fsim::runtime::simir::ProcessId>{1}));
    assert(mixed_specializations[2].id == 2);
    assert(mixed_specializations[2].unit == "sv:work.child");
    assert(mixed_specializations[2].instance == "tb.u_child");
    assert(mixed_specializations[2].source == "tb.sv");
    assert((
        mixed_specializations[2].processes
        == std::vector<fsim::runtime::simir::ProcessId>{2}));
    const auto mixed_q = elaborated_mixed.design->find_signal("q");
    const auto mixed_child_q =
        elaborated_mixed.design->find_signal("tb.u_counter.q");
    const auto mixed_inverted =
        elaborated_mixed.design->find_signal("inverted");
    assert(mixed_q && mixed_child_q && mixed_inverted);
    assert(*mixed_q == *mixed_child_q);
    auto mixed_interpreter =
        elaborated_mixed.design->create_interpreter();
    const auto mixed_result = mixed_interpreter->run();
    assert(mixed_result.status == fsim::runtime::RunStatus::stopped);
    assert(mixed_result.time == 5);
    assert(
        mixed_interpreter->signal_value(*mixed_q).to_msb_string()
        == "00000001");
    assert(
        mixed_interpreter->signal_value(*mixed_inverted).to_msb_string()
        == "11111110");

    constexpr std::string_view vhdl_parent = R"(
entity vhdl_top is
end entity vhdl_top;

architecture rtl of vhdl_top is
  signal source : std_logic_vector(3 downto 0);
  signal inverted : std_logic_vector(3 downto 0);
begin
  source <= "1010";
  u_child: sv_child
    port map (value => source, inverted => inverted);
end architecture rtl;
)";
    constexpr std::string_view sv_bound_child = R"(
module sv_child(
  input logic [3:0] value,
  output logic [3:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    auto parsed_vhdl_parent = fsim::frontend::parse_text(
        "vhdl_top.vhd",
        vhdl_parent,
        fsim::frontend::Language::Vhdl2008);
    auto parsed_sv_child = fsim::frontend::parse_text(
        "sv_child.sv",
        sv_bound_child,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_vhdl_parent.ok() && parsed_sv_child.ok());
    for (auto& unit : parsed_sv_child.design.units) {
        parsed_vhdl_parent.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> reverse_binding{
        {"vhdl_top.u_child", "sv:work.sv_child", std::nullopt},
    };
    auto reverse_mixed = fsim::elaboration::elaborate(
        parsed_vhdl_parent.design,
        "vhdl:work.vhdl_top(rtl)",
        reverse_binding);
    if (!reverse_mixed.ok()) {
        for (const auto& diagnostic : reverse_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(reverse_mixed.ok());
    const auto reverse_source =
        reverse_mixed.design->find_signal("source");
    const auto reverse_output =
        reverse_mixed.design->find_signal("inverted");
    const auto child_source =
        reverse_mixed.design->find_signal("vhdl_top.u_child.value");
    assert(reverse_source && reverse_output && child_source);
    assert(*reverse_source == *child_source);
    auto reverse_interpreter =
        reverse_mixed.design->create_interpreter();
    const auto reverse_result = reverse_interpreter->run();
    assert(reverse_result.status == fsim::runtime::RunStatus::completed);
    assert(
        reverse_interpreter->signal_value(*reverse_source).to_msb_string()
        == "1010");
    assert(
        reverse_interpreter->signal_value(*reverse_output).to_msb_string()
        == "0101");

    constexpr std::string_view systemc_boundary_sv = R"(
module systemc_parent;
  bit clock;
  logic [7:0] value;
  bridge_placeholder u_bridge(.clock(clock), .value(value));
  initial begin
    clock = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_hdl_child(
  input bit clock,
  output bit [7:0] value
);
  assign value = 8'b10100101;
endmodule
)";
    const auto parsed_systemc_boundary = fsim::frontend::parse_text(
        "systemc_boundary.sv",
        systemc_boundary_sv,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_systemc_boundary.ok());
    fsim::frontend::Type systemc_bit{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.bit",
        std::nullopt,
        false};
    fsim::frontend::Type systemc_unsigned{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.unsigned",
        fsim::frontend::PackedRange{7, 0, true},
        false};
    const fsim::elaboration::SystemCInstanceDescription
        nested_systemc{
            "systemc_parent.u_bridge",
            "systemc:models.bridge",
            100,
            0,
            {
                {101, "clock", systemc_bit,
                 fsim::frontend::PortDirection::Input, 0},
                {102, "value", systemc_unsigned,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {
                     {"clock", systemc_bit,
                      fsim::frontend::PortDirection::Input, 101},
                     {"value", systemc_unsigned,
                      fsim::frontend::PortDirection::Output, 102},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        hdl_to_systemc_bindings{
            {"systemc_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_parent.u_bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array hdl_to_systemc_instances{nested_systemc};
    auto hdl_to_systemc = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "sv:work.systemc_parent",
        hdl_to_systemc_bindings,
        hdl_to_systemc_instances);
    if (!hdl_to_systemc.ok()) {
        for (const auto& diagnostic : hdl_to_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(hdl_to_systemc.ok());
    assert(hdl_to_systemc.design->systemc_instances().size() == 1);
    assert(
        hdl_to_systemc.design->systemc_instances().front().instance
        == "systemc_parent.u_bridge");
    const auto systemc_parent_value =
        hdl_to_systemc.design->find_signal("value");
    const auto systemc_port_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.value");
    const auto systemc_child_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.u_hdl.value");
    assert(
        systemc_parent_value && systemc_port_value
        && systemc_child_value);
    assert(*systemc_parent_value == *systemc_port_value);
    assert(*systemc_parent_value == *systemc_child_value);
    auto hdl_to_systemc_interpreter =
        hdl_to_systemc.design->create_interpreter();
    const auto hdl_to_systemc_result =
        hdl_to_systemc_interpreter->run();
    assert(
        hdl_to_systemc_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        hdl_to_systemc_interpreter
            ->signal_value(*systemc_parent_value)
            .to_msb_string()
        == "10100101");

    auto systemc_root = nested_systemc;
    systemc_root.path = "bridge";
    const std::vector<fsim::elaboration::Binding>
        systemc_to_hdl_bindings{
            {"bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array systemc_root_instances{systemc_root};
    auto systemc_to_hdl = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "systemc:models.bridge",
        systemc_to_hdl_bindings,
        systemc_root_instances);
    if (!systemc_to_hdl.ok()) {
        for (const auto& diagnostic : systemc_to_hdl.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(systemc_to_hdl.ok());
    assert(systemc_to_hdl.design->systemc_instances().size() == 1);
    assert(systemc_to_hdl.design->specializations().size() == 1);
    const auto systemc_root_value =
        systemc_to_hdl.design->find_signal("bridge.value");
    const auto systemc_root_child_value =
        systemc_to_hdl.design->find_signal(
            "bridge.u_hdl.value");
    assert(systemc_root_value && systemc_root_child_value);
    assert(*systemc_root_value == *systemc_root_child_value);
    auto systemc_to_hdl_interpreter =
        systemc_to_hdl.design->create_interpreter();
    const auto systemc_to_hdl_result =
        systemc_to_hdl_interpreter->run();
    assert(
        systemc_to_hdl_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        systemc_to_hdl_interpreter
            ->signal_value(*systemc_root_value)
            .to_msb_string()
        == "10100101");

    const std::vector<fsim::elaboration::Binding>
        missing_systemc_child_binding;
    const auto missing_systemc_child =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            missing_systemc_child_binding,
            systemc_root_instances);
    assert(!missing_systemc_child.ok());
    assert(has_diagnostic(
        missing_systemc_child, "FSIM-ELAB-BIND-040"));

    auto thread_systemc = systemc_root;
    thread_systemc.foreign_children.clear();
    thread_systemc.processes.push_back({
        150,
        "thread",
        FSIM_SC_THREAD,
        nullptr,
        nullptr,
        {},
        true});
    const std::array thread_systemc_instances{thread_systemc};
    const auto systemc_thread =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            std::span<const fsim::elaboration::Binding>{},
            thread_systemc_instances);
#if defined(FSIM_HAS_BOOST_CONTEXT)
    assert(systemc_thread.ok());
    assert(systemc_thread.design->systemc_processes().size() == 1);
#else
    assert(!systemc_thread.ok());
    assert(has_diagnostic(
        systemc_thread, "FSIM-ELAB-BIND-042"));
#endif

    constexpr std::string_view systemc_boundary_vhdl = R"(
entity systemc_vhdl_parent is
end entity systemc_vhdl_parent;

architecture rtl of systemc_vhdl_parent is
  signal value : std_logic;
  signal inverted : std_logic;
begin
  value <= '1';
  u_bridge: bridge_placeholder
    port map (value => value, inverted => inverted);
end architecture rtl;

entity systemc_vhdl_child is
  port (
    value : in std_logic;
    inverted : out std_logic
  );
end entity systemc_vhdl_child;

architecture rtl of systemc_vhdl_child is
begin
  inverted <= not value;
end architecture rtl;
)";
    const auto parsed_systemc_vhdl = fsim::frontend::parse_text(
        "systemc_boundary.vhd",
        systemc_boundary_vhdl,
        fsim::frontend::Language::Vhdl2008);
    assert(parsed_systemc_vhdl.ok());
    fsim::frontend::Type systemc_logic{
        fsim::frontend::ValueDomain::Logic4,
        "systemc.logic",
        std::nullopt,
        false};
    const fsim::elaboration::SystemCInstanceDescription
        vhdl_nested_systemc{
            "systemc_vhdl_parent.u_bridge",
            "systemc:models.bridge",
            200,
            0,
            {
                {201, "value", systemc_logic,
                 fsim::frontend::PortDirection::Input, 0},
                {202, "inverted", systemc_logic,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {
                     {"value", systemc_logic,
                      fsim::frontend::PortDirection::Input, 201},
                     {"inverted", systemc_logic,
                      fsim::frontend::PortDirection::Output, 202},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        vhdl_systemc_bindings{
            {"systemc_vhdl_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_vhdl_parent.u_bridge.u_hdl",
             "vhdl:work.systemc_vhdl_child(rtl)",
             std::nullopt},
        };
    const std::array vhdl_systemc_instances{
        vhdl_nested_systemc};
    auto vhdl_systemc = fsim::elaboration::elaborate(
        parsed_systemc_vhdl.design,
        "vhdl:work.systemc_vhdl_parent(rtl)",
        vhdl_systemc_bindings,
        vhdl_systemc_instances);
    if (!vhdl_systemc.ok()) {
        for (const auto& diagnostic : vhdl_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_systemc.ok());
    assert(vhdl_systemc.design->systemc_instances().size() == 1);
    const auto vhdl_systemc_value =
        vhdl_systemc.design->find_signal("value");
    const auto vhdl_systemc_child_value =
        vhdl_systemc.design->find_signal(
            "systemc_vhdl_parent.u_bridge.u_hdl.value");
    const auto vhdl_systemc_inverted =
        vhdl_systemc.design->find_signal("inverted");
    assert(
        vhdl_systemc_value && vhdl_systemc_child_value
        && vhdl_systemc_inverted);
    assert(*vhdl_systemc_value == *vhdl_systemc_child_value);
    auto vhdl_systemc_interpreter =
        vhdl_systemc.design->create_interpreter();
    const auto vhdl_systemc_result =
        vhdl_systemc_interpreter->run();
    assert(
        vhdl_systemc_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_systemc_interpreter
            ->signal_value(*vhdl_systemc_inverted)
            .to_msb_string()
        == "0");

    auto colliding_sv = fsim::frontend::parse_text(
        "duplicate.sv",
        "module duplicate; logic sv_only; endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    auto colliding_vhdl = fsim::frontend::parse_text(
        "duplicate.vhd",
        R"(
entity duplicate is
  port (vhdl_only : in std_logic);
end entity duplicate;
architecture rtl of duplicate is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(colliding_sv.ok() && colliding_vhdl.ok());
    colliding_sv.design.units.front().library = "other";
    for (auto& unit : colliding_vhdl.design.units) {
        unit.library = "work";
        colliding_sv.design.units.push_back(std::move(unit));
    }
    auto selected_vhdl = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate(rtl)");
    assert(selected_vhdl.ok());
    assert(selected_vhdl.design->find_signal("vhdl_only"));
    assert(!selected_vhdl.design->find_signal("sv_only"));
    const auto ambiguous_vhdl_top = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate");
    assert(!ambiguous_vhdl_top.ok());
    assert(has_diagnostic(ambiguous_vhdl_top, "FSIM-ELAB-004"));

    auto cross_library = fsim::frontend::parse_text(
        "cross_library.vhd",
        R"(
entity child is
  port (value : in std_logic);
end entity;
architecture rtl of child is
begin
end architecture;

entity parent is
  port (value : in std_logic);
end entity;
architecture rtl of parent is
begin
  selected: entity other.child(rtl)
    port map (value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(cross_library.ok());
    for (auto& unit : cross_library.design.units) {
        unit.library =
            unit.name == "child"
                || (unit.kind
                        == fsim::frontend::UnitKind::VhdlArchitecture
                    && unit.primary_name == "child")
            ? "other"
            : "work";
    }
    const auto selected_library = fsim::elaboration::elaborate(
        cross_library.design, "vhdl:work.parent(rtl)");
    assert(selected_library.ok());
    assert(selected_library.design->find_signal(
        "parent.selected.value"));

    const auto unsafe_edge = fsim::frontend::parse_text(
        "unsafe_edge.vhd",
        R"(
entity unsafe_edge is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of unsafe_edge is
begin
  p: process(clk)
  begin
    if rising_edge(clk) then
      q <= '1';
    else
      q <= '0';
    end if;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unsafe_edge.ok());
    const auto rejected_edge =
        fsim::elaboration::elaborate(unsafe_edge.design, "unsafe_edge");
    assert(!rejected_edge.ok());
    assert(has_diagnostic(rejected_edge, "FSIM-ELAB-045"));

    const auto logical_not = fsim::frontend::parse_text(
        "logical_not.sv",
        R"(
module logical_not(input logic [3:0] value, output logic result);
  assign result = !value;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_not.ok());
    const auto elaborated_not =
        fsim::elaboration::elaborate(logical_not.design, "logical_not");
    assert(elaborated_not.ok());
    const auto not_value =
        elaborated_not.design->find_signal("value");
    const auto not_result =
        elaborated_not.design->find_signal("result");
    assert(not_value && not_result);
    auto not_interpreter =
        elaborated_not.design->create_interpreter();
    for (const auto& [value, expected] :
         std::array{
             std::pair{
                 std::string_view{"0000"},
                 std::string_view{"1"}},
             std::pair{
                 std::string_view{"00X0"},
                 std::string_view{"X"}},
             std::pair{
                 std::string_view{"01X0"},
                 std::string_view{"0"}}}) {
        not_interpreter->deposit_signal(
            *not_value,
            fsim::runtime::PackedLogic4::from_msb_string(value));
        (void)not_interpreter->run();
        assert(
            not_interpreter
                ->signal_value(*not_result)
                .to_msb_string()
            == expected);
    }

    const auto blocking_delay = fsim::frontend::parse_text(
        "blocking_delay.sv",
        R"(
module blocking_delay;
  logic a;
  logic b;
  initial begin
    a = #5 1'b1;
    b = 1'b1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(blocking_delay.ok());
    const auto rejected_blocking_delay =
        fsim::elaboration::elaborate(
            blocking_delay.design, "blocking_delay");
    assert(!rejected_blocking_delay.ok());
    assert(has_diagnostic(
        rejected_blocking_delay, "FSIM-ELAB-046"));

    const auto width_mismatch = fsim::frontend::parse_text(
        "width_mismatch.sv",
        R"(
module width_mismatch;
  logic [7:0] q;
  initial q = 4'b1010;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(width_mismatch.ok());
    const auto rejected_width_mismatch =
        fsim::elaboration::elaborate(
            width_mismatch.design, "width_mismatch");
    assert(!rejected_width_mismatch.ok());
    assert(has_diagnostic(
        rejected_width_mismatch, "FSIM-ELAB-047"));

    auto unsupported_domain = fsim::frontend::parse_text(
        "unsupported_domain.sv",
        "module unsupported_domain(input logic value); endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unsupported_domain.ok());
    unsupported_domain.design.units.front().ports.front().type.domain =
        fsim::frontend::ValueDomain::Unknown;
    const auto rejected_domain = fsim::elaboration::elaborate(
        unsupported_domain.design, "unsupported_domain");
    assert(!rejected_domain.ok());
    assert(has_diagnostic(
        rejected_domain, "FSIM-ELAB-TYPE-001"));

    const auto default_values = fsim::frontend::parse_text(
        "default_values.vhd",
        R"(
entity default_values is
  port (
    bit_value : out bit;
    boolean_value : out boolean;
    logic_value : out std_logic
  );
end entity;
architecture rtl of default_values is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(default_values.ok());
    const auto elaborated_defaults = fsim::elaboration::elaborate(
        default_values.design, "default_values");
    assert(elaborated_defaults.ok());
    auto default_interpreter =
        elaborated_defaults.design->create_interpreter();
    const auto bit_value =
        elaborated_defaults.design->find_signal("bit_value");
    const auto boolean_value =
        elaborated_defaults.design->find_signal("boolean_value");
    const auto logic_value =
        elaborated_defaults.design->find_signal("logic_value");
    assert(bit_value && boolean_value && logic_value);
    assert(
        default_interpreter->signal_value(*bit_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*boolean_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*logic_value).to_msb_string()
        == "X");

    const auto nine_state_literals = fsim::frontend::parse_text(
        "nine_state_literals.vhd",
        R"(
entity nine_state_literals is
  port (value : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of nine_state_literals is
begin
  value <= "ULH-WZ01";
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(nine_state_literals.ok());
    const auto elaborated_nine_state_literals =
        fsim::elaboration::elaborate(
            nine_state_literals.design, "nine_state_literals");
    assert(elaborated_nine_state_literals.ok());
    auto nine_state_interpreter =
        elaborated_nine_state_literals.design->create_interpreter();
    const auto nine_state_value =
        elaborated_nine_state_literals.design->find_signal("value");
    assert(nine_state_value);
    const auto nine_state_run = nine_state_interpreter->run();
    assert(
        nine_state_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        nine_state_interpreter
            ->signal_value(*nine_state_value)
            .to_msb_string()
        == "X01XXZ01");

    const auto lossy_assignment = fsim::frontend::parse_text(
        "lossy_assignment.sv",
        R"(
module lossy_assignment(input logic source, output bit target);
  assign target = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy_assignment.ok());
    const auto rejected_lossy_assignment =
        fsim::elaboration::elaborate(
            lossy_assignment.design, "lossy_assignment");
    assert(!rejected_lossy_assignment.ok());
    assert(has_diagnostic(
        rejected_lossy_assignment, "FSIM-ELAB-050"));

    const auto two_state_assignment = fsim::frontend::parse_text(
        "two_state_assignment.sv",
        R"(
module two_state_assignment(output bit target);
  initial target = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(two_state_assignment.ok());
    const auto accepted_two_state_assignment =
        fsim::elaboration::elaborate(
            two_state_assignment.design, "two_state_assignment");
    assert(accepted_two_state_assignment.ok());

    const auto unknown_condition = fsim::frontend::parse_text(
        "unknown_condition.sv",
        R"(
module unknown_condition;
  logic condition;
  logic result;
  initial begin
    if (condition)
      result = 1'b1;
    else
      result = 1'b0;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_condition.ok());
    const auto elaborated_unknown_condition =
        fsim::elaboration::elaborate(
            unknown_condition.design, "unknown_condition");
    assert(elaborated_unknown_condition.ok());
    auto unknown_condition_interpreter =
        elaborated_unknown_condition.design->create_interpreter();
    const auto condition_result =
        elaborated_unknown_condition.design->find_signal("result");
    assert(condition_result);
    const auto unknown_condition_run =
        unknown_condition_interpreter->run();
    assert(
        unknown_condition_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        unknown_condition_interpreter
            ->signal_value(*condition_result)
            .to_msb_string()
        == "0");

    const std::vector<fsim::elaboration::Binding>
        architectureless_binding{
            {"tb.u_counter", "vhdl:work.counter", std::nullopt},
        };
    const auto rejected_architectureless =
        fsim::elaboration::elaborate(
            parsed_mixed_sv.design,
            "sv:work.tb",
            architectureless_binding);
    assert(!rejected_architectureless.ok());
    assert(has_diagnostic(
        rejected_architectureless, "FSIM-ELAB-BIND-016"));

    const auto multiple_drivers = fsim::frontend::parse_text(
        "multiple_drivers.sv",
        R"(
module driver(output logic value);
  assign value = 1'b1;
endmodule
module driver_top;
  logic shared;
  driver first(.value(shared));
  driver second(.value(shared));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(multiple_drivers.ok());
    const auto missing_resolver = fsim::elaboration::elaborate(
        multiple_drivers.design, "driver_top");
    assert(!missing_resolver.ok());
    assert(has_diagnostic(missing_resolver, "FSIM-ELAB-BIND-024"));
    const std::vector<fsim::elaboration::Binding> resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"sv_wire"}},
        {"driver_top.second", "sv:work.driver", std::string{"sv_wire"}},
    };
    const auto unavailable_resolution = fsim::elaboration::elaborate(
        multiple_drivers.design, "driver_top", resolver_bindings);
    assert(!unavailable_resolution.ok());
    assert(has_diagnostic(
        unavailable_resolution, "FSIM-ELAB-BIND-029"));

    const auto process_drivers = fsim::frontend::parse_text(
        "process_drivers.sv",
        R"(
module process_drivers;
  logic q;
  initial q = 1'b0;
  initial q = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(process_drivers.ok());
    const auto rejected_process_drivers = fsim::elaboration::elaborate(
        process_drivers.design, "process_drivers");
    assert(!rejected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_process_drivers, "FSIM-ELAB-DRV-001"));

    const auto selected_process_drivers =
        fsim::frontend::parse_text(
            "selected_process_drivers.sv",
            R"(
module selected_process_drivers;
  logic [3:0] q;
  initial q[0] = 1'b0;
  initial q[3:2] = 2'b11;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_process_drivers.ok());
    const auto rejected_selected_process_drivers =
        fsim::elaboration::elaborate(
            selected_process_drivers.design,
            "selected_process_drivers");
    assert(!rejected_selected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_selected_process_drivers,
        "FSIM-ELAB-DRV-001"));

    const auto local_variables = fsim::frontend::parse_text(
        "local_variables.sv",
        R"(
module local_variables;
  logic q;
  initial begin
    logic state = 1'b0;
    state = 1'b1;
    q = state;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(local_variables.ok());
    const auto elaborated_locals = fsim::elaboration::elaborate(
        local_variables.design, "local_variables");
    assert(elaborated_locals.ok());
    const auto& local_process =
        elaborated_locals.design->processes().front();
    assert(
        local_process.debug_locals.size() == 1
        && local_process.debug_locals.front().name == "state"
        && local_process.debug_locals.front().type_name == "logic"
        && local_process.debug_locals.front().width == 1);
    assert(std::any_of(
        local_process.operations.begin(),
        local_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::CopyRegister>(operation);
        }));
    auto local_interpreter =
        elaborated_locals.design->create_interpreter();
    const auto local_run = local_interpreter->run();
    assert(local_run.status == fsim::runtime::RunStatus::completed);
    assert(
        local_interpreter->read_debug_local(0, 0).to_msb_string()
        == "1");
    const auto local_q =
        elaborated_locals.design->find_signal("q");
    assert(local_q);
    assert(
        local_interpreter->signal_value(*local_q).to_msb_string()
        == "1");

    const auto vhdl_local_variables = fsim::frontend::parse_text(
        "local_variables.vhd",
        R"(
entity local_variables is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of local_variables is begin
  worker: process(clk)
    variable state : std_logic := '0';
  begin
    state := not state;
    q <= state;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_local_variables.ok());
    const auto elaborated_vhdl_locals =
        fsim::elaboration::elaborate(
            vhdl_local_variables.design,
            "vhdl:work.local_variables(rtl)");
    assert(elaborated_vhdl_locals.ok());
    const auto& vhdl_local_process =
        elaborated_vhdl_locals.design->processes().front();
    assert(
        vhdl_local_process.debug_locals.size() == 1
        && vhdl_local_process.debug_locals.front().name == "state"
        && vhdl_local_process.debug_locals.front().type_name
            == "std_logic");
    assert(std::count_if(
               vhdl_local_process.operations.begin(),
               vhdl_local_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                     fsim::runtime::simir::CopyRegister>(operation);
               })
           >= 2);
    auto vhdl_local_interpreter =
        elaborated_vhdl_locals.design->create_interpreter();
    const auto vhdl_local_clk =
        elaborated_vhdl_locals.design->find_signal("clk");
    const auto vhdl_local_q =
        elaborated_vhdl_locals.design->find_signal("q");
    assert(vhdl_local_clk && vhdl_local_q);
    vhdl_local_interpreter->schedule_signal_at(
        *vhdl_local_clk,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        1);
    const auto vhdl_local_run = vhdl_local_interpreter->run(2);
    assert(
        vhdl_local_run.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_local_interpreter
            ->signal_value(*vhdl_local_q)
            .to_msb_string()
        == "0");
    assert(
        vhdl_local_interpreter
            ->read_debug_local(0, 0)
            .to_msb_string()
        == "0");

    const auto vhdl_waits = fsim::frontend::parse_text(
        "waits.vhd",
        R"(
entity waits is end entity;
architecture rtl of waits is
  signal trigger : std_logic;
  signal q : std_logic;
begin
  worker: process
  begin
    q <= '0';
    wait for 2 ns;
    q <= '1';
    wait on trigger;
    q <= '0';
    wait on trigger;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_waits.ok());
    const auto elaborated_vhdl_waits =
        fsim::elaboration::elaborate(
            vhdl_waits.design, "vhdl:work.waits(rtl)");
    assert(elaborated_vhdl_waits.ok());
    const auto& vhdl_wait_process =
        elaborated_vhdl_waits.design->processes().front();
    assert(std::count_if(
               vhdl_wait_process.operations.begin(),
               vhdl_wait_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                            fsim::runtime::simir::WaitFor>(operation)
                     || std::holds_alternative<
                            fsim::runtime::simir::WaitOn>(operation);
               })
           == 3);
    assert(std::holds_alternative<fsim::runtime::simir::Jump>(
        vhdl_wait_process.operations.back()));
    auto vhdl_wait_interpreter =
        elaborated_vhdl_waits.design->create_interpreter();
    const auto vhdl_wait_trigger =
        elaborated_vhdl_waits.design->find_signal("trigger");
    const auto vhdl_wait_q =
        elaborated_vhdl_waits.design->find_signal("q");
    assert(vhdl_wait_trigger && vhdl_wait_q);
    vhdl_wait_interpreter->schedule_signal_at(
        *vhdl_wait_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        3);
    const auto vhdl_wait_mid = vhdl_wait_interpreter->run(2);
    assert(
        vhdl_wait_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "1");
    const auto vhdl_wait_end = vhdl_wait_interpreter->run(4);
    assert(
        vhdl_wait_end.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "0");

    const auto sv_events = fsim::frontend::parse_text(
        "events.sv",
        R"(
module events;
  logic trigger;
  logic observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  initial begin
    @(posedge trigger);
    observed = trigger;
    @(negedge trigger) observed = trigger;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(sv_events.ok());
    const auto elaborated_sv_events =
        fsim::elaboration::elaborate(
            sv_events.design, "sv:work.events");
    assert(elaborated_sv_events.ok());
    assert(elaborated_sv_events.design->processes().size() == 2);
    const auto& observer_process =
        elaborated_sv_events.design->processes().back();
    assert(std::count_if(
               observer_process.operations.begin(),
               observer_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                     fsim::runtime::simir::WaitOn>(operation);
               })
           == 2);
    const auto first_dynamic_wait_operation = std::find_if(
        observer_process.operations.begin(),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto second_dynamic_wait_operation = std::find_if(
        std::next(first_dynamic_wait_operation),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto& first_dynamic_wait =
        std::get<fsim::runtime::simir::WaitOn>(
            *first_dynamic_wait_operation);
    const auto& second_dynamic_wait =
        std::get<fsim::runtime::simir::WaitOn>(
            *second_dynamic_wait_operation);
    assert(
        first_dynamic_wait.edges.size() == 1
        && first_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::posedge);
    assert(
        second_dynamic_wait.edges.size() == 1
        && second_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::negedge);
    auto sv_event_interpreter =
        elaborated_sv_events.design->create_interpreter();
    const auto sv_observed =
        elaborated_sv_events.design->find_signal("observed");
    assert(sv_observed);
    const auto sv_event_mid = sv_event_interpreter->run(1);
    assert(
        sv_event_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "1");
    const auto sv_event_end = sv_event_interpreter->run();
    assert(
        sv_event_end.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "0");

    const auto unknown_wait = fsim::frontend::parse_text(
        "unknown_wait.vhd",
        R"(
entity unknown_wait is end entity;
architecture rtl of unknown_wait is begin
  worker: process begin
    wait on missing;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unknown_wait.ok());
    const auto rejected_unknown_wait =
        fsim::elaboration::elaborate(
            unknown_wait.design,
            "vhdl:work.unknown_wait(rtl)");
    assert(!rejected_unknown_wait.ok());
    assert(has_diagnostic(rejected_unknown_wait, "FSIM-ELAB-059"));

    const auto vector_edge_wait = fsim::frontend::parse_text(
        "vector_edge_wait.sv",
        R"(
module vector_edge_wait;
  logic [1:0] trigger;
  initial @(posedge trigger);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(vector_edge_wait.ok());
    const auto rejected_vector_edge_wait =
        fsim::elaboration::elaborate(
            vector_edge_wait.design, "sv:work.vector_edge_wait");
    assert(!rejected_vector_edge_wait.ok());
    assert(has_diagnostic(
        rejected_vector_edge_wait, "FSIM-ELAB-060"));

    const auto wildcard_processes = fsim::frontend::parse_text(
        "wildcard.sv",
        R"(
module wildcard_processes;
  logic a;
  logic q;
  logic y;
  logic latched;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latched = q;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wildcard_processes.ok());
    const auto elaborated_wildcard =
        fsim::elaboration::elaborate(
            wildcard_processes.design, "sv:work.wildcard_processes");
    assert(elaborated_wildcard.ok());
    assert(elaborated_wildcard.design->processes().size() == 3);
    const auto wildcard_a =
        elaborated_wildcard.design->find_signal("a");
    const auto wildcard_q =
        elaborated_wildcard.design->find_signal("q");
    const auto wildcard_y =
        elaborated_wildcard.design->find_signal("y");
    const auto wildcard_latched =
        elaborated_wildcard.design->find_signal("latched");
    assert(
        wildcard_a && wildcard_q && wildcard_y
        && wildcard_latched);
    assert((
        elaborated_wildcard.design->processes()[0]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[1]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[2]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any},
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    auto wildcard_interpreter =
        elaborated_wildcard.design->create_interpreter();
    (void)wildcard_interpreter->run();
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "1");
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "1");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_latched)
            .to_msb_string()
        == "1");

    const auto case_process = fsim::frontend::parse_text(
        "case_process.sv",
        R"(
module case_process;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(case_process.ok());
    const auto elaborated_case =
        fsim::elaboration::elaborate(
            case_process.design, "sv:work.case_process");
    assert(elaborated_case.ok());
    const auto case_selector =
        elaborated_case.design->find_signal("selector");
    const auto case_result =
        elaborated_case.design->find_signal("result");
    assert(case_selector && case_result);
    assert((
        elaborated_case.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*case_selector,
             fsim::runtime::simir::EdgeKind::any}}));
    const auto has_case_equality = std::any_of(
        elaborated_case.design->processes().front()
            .operations.begin(),
        elaborated_case.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
            const auto* binary =
                std::get_if<fsim::runtime::simir::Binary>(
                    &operation);
            return binary != nullptr
                && binary->operation
                    == fsim::runtime::simir::BinaryOperator::
                        case_equal;
        });
    assert(has_case_equality);
    auto case_interpreter =
        elaborated_case.design->create_interpreter();
    (void)case_interpreter->run();
    for (const auto& [selector_value, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"00", "00"},
             {"01", "01"},
             {"10", "01"},
             {"x0", "10"},
             {"z1", "11"},
             {"11", "00"}}) {
        case_interpreter->deposit_signal(
            *case_selector,
            fsim::runtime::PackedLogic4::from_msb_string(
                selector_value));
        (void)case_interpreter->run();
        assert(
            case_interpreter
                ->signal_value(*case_result)
                .to_msb_string()
            == expected);
    }

    const auto mismatched_case = fsim::frontend::parse_text(
        "mismatched_case.sv",
        R"(
module mismatched_case;
  logic [1:0] selector;
  logic result;
  always_comb case (selector)
    1'b0: result = 1'b0;
    default: result = 1'b1;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatched_case.ok());
    const auto rejected_case =
        fsim::elaboration::elaborate(
            mismatched_case.design, "sv:work.mismatched_case");
    assert(!rejected_case.ok());
    assert(has_diagnostic(rejected_case, "FSIM-ELAB-063"));

    const auto conditional_process = fsim::frontend::parse_text(
        "conditional_process.sv",
        R"(
module conditional_process;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(conditional_process.ok());
    const auto elaborated_conditional =
        fsim::elaboration::elaborate(
            conditional_process.design,
            "sv:work.conditional_process");
    assert(elaborated_conditional.ok());
    const auto conditional_select =
        elaborated_conditional.design->find_signal("select");
    const auto conditional_lhs =
        elaborated_conditional.design->find_signal("lhs");
    const auto conditional_rhs =
        elaborated_conditional.design->find_signal("rhs");
    const auto conditional_result =
        elaborated_conditional.design->find_signal("result");
    assert(
        conditional_select && conditional_lhs
        && conditional_rhs && conditional_result);
    assert((
        elaborated_conditional.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*conditional_lhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_rhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_select,
             fsim::runtime::simir::EdgeKind::any}}));
    auto conditional_interpreter =
        elaborated_conditional.design->create_interpreter();
    conditional_interpreter->deposit_signal(
        *conditional_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("101z"));
    conditional_interpreter->deposit_signal(
        *conditional_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("100z"));
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "100Z");
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "101Z");
    for (const auto unknown : {"x", "z"}) {
        conditional_interpreter->deposit_signal(
            *conditional_select,
            fsim::runtime::PackedLogic4::from_msb_string(unknown));
        (void)conditional_interpreter->run();
        assert(
            conditional_interpreter
                ->signal_value(*conditional_result)
                .to_msb_string()
            == "10XZ");
    }

    const auto invalid_conditional = fsim::frontend::parse_text(
        "invalid_conditional.sv",
        R"(
module vector_condition;
  logic [1:0] select;
  logic result;
  always_comb result = select ? 1'b0 : 1'b1;
endmodule
module mismatched_alternatives;
  logic select;
  logic [1:0] result;
  always_comb result = select ? 2'b00 : 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_conditional.ok());
    const auto rejected_vector_condition =
        fsim::elaboration::elaborate(
            invalid_conditional.design,
            "sv:work.vector_condition");
    assert(!rejected_vector_condition.ok());
    assert(has_diagnostic(
        rejected_vector_condition, "FSIM-ELAB-064"));
    const auto rejected_alternatives =
        fsim::elaboration::elaborate(
            invalid_conditional.design,
            "sv:work.mismatched_alternatives");
    assert(!rejected_alternatives.ok());
    assert(has_diagnostic(
        rejected_alternatives, "FSIM-ELAB-065"));

    const auto comparison_process = fsim::frontend::parse_text(
        "comparison_process.sv",
        R"(
module comparison_process;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic neq;
  logic lt;
  logic le;
  logic gt;
  logic ge;
  logic logical_not;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(comparison_process.ok());
    const auto elaborated_comparisons =
        fsim::elaboration::elaborate(
            comparison_process.design,
            "sv:work.comparison_process");
    assert(elaborated_comparisons.ok());
    const auto comparison_lhs =
        elaborated_comparisons.design->find_signal("lhs");
    const auto comparison_rhs =
        elaborated_comparisons.design->find_signal("rhs");
    const auto comparison_neq =
        elaborated_comparisons.design->find_signal("neq");
    const auto comparison_lt =
        elaborated_comparisons.design->find_signal("lt");
    const auto comparison_le =
        elaborated_comparisons.design->find_signal("le");
    const auto comparison_gt =
        elaborated_comparisons.design->find_signal("gt");
    const auto comparison_ge =
        elaborated_comparisons.design->find_signal("ge");
    const auto comparison_not =
        elaborated_comparisons.design->find_signal("logical_not");
    assert(
        comparison_lhs && comparison_rhs && comparison_neq
        && comparison_lt && comparison_le && comparison_gt
        && comparison_ge && comparison_not);
    auto comparison_interpreter =
        elaborated_comparisons.design->create_interpreter();
    const auto run_comparison =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 6>& expected) {
          comparison_interpreter->deposit_signal(
              *comparison_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          comparison_interpreter->deposit_signal(
              *comparison_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)comparison_interpreter->run();
          const std::array signals{
              *comparison_neq,
              *comparison_lt,
              *comparison_le,
              *comparison_gt,
              *comparison_ge,
              *comparison_not};
          for (std::size_t index = 0; index < signals.size();
               ++index) {
            assert(
                comparison_interpreter
                    ->signal_value(signals[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_comparison(
        "0010", "0011", {"1", "1", "1", "0", "0", "0"});
    run_comparison(
        "0000", "0000", {"0", "0", "1", "0", "1", "1"});
    run_comparison(
        "00X0", "0011", {"X", "X", "X", "X", "X", "X"});
    run_comparison(
        "01X0", "0011", {"X", "X", "X", "X", "X", "0"});

    const auto signed_comparison = fsim::frontend::parse_text(
        "signed_comparison.sv",
        R"(
module signed_comparison;
  logic signed [3:0] lhs;
  logic signed [3:0] rhs;
  logic result;
  always_comb result = lhs < rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_comparison.ok());
    const auto elaborated_signed_comparison =
        fsim::elaboration::elaborate(
            signed_comparison.design,
            "sv:work.signed_comparison");
    assert(elaborated_signed_comparison.ok());
    const auto signed_comparison_lhs =
        elaborated_signed_comparison.design->find_signal("lhs");
    const auto signed_comparison_rhs =
        elaborated_signed_comparison.design->find_signal("rhs");
    const auto signed_comparison_result =
        elaborated_signed_comparison.design->find_signal("result");
    assert(
        signed_comparison_lhs && signed_comparison_rhs
        && signed_comparison_result);
    auto signed_comparison_interpreter =
        elaborated_signed_comparison.design->create_interpreter();
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("1111"));
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    (void)signed_comparison_interpreter->run();
    assert(
        signed_comparison_interpreter
            ->signal_value(*signed_comparison_result)
            .to_msb_string()
        == "1");

    const auto logical_process = fsim::frontend::parse_text(
        "logical_process.sv",
        R"(
module logical_process;
  logic [3:0] lhs;
  logic [1:0] rhs;
  logic conjunction;
  logic disjunction;
  always_comb begin
    conjunction = lhs && rhs;
    disjunction = lhs || rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_process.ok());
    const auto elaborated_logical =
        fsim::elaboration::elaborate(
            logical_process.design, "sv:work.logical_process");
    assert(elaborated_logical.ok());
    const auto logical_lhs =
        elaborated_logical.design->find_signal("lhs");
    const auto logical_rhs =
        elaborated_logical.design->find_signal("rhs");
    const auto logical_and =
        elaborated_logical.design->find_signal("conjunction");
    const auto logical_or =
        elaborated_logical.design->find_signal("disjunction");
    assert(logical_lhs && logical_rhs && logical_and && logical_or);
    auto logical_interpreter =
        elaborated_logical.design->create_interpreter();
    const auto run_logical =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view expected_and,
            const std::string_view expected_or) {
          logical_interpreter->deposit_signal(
              *logical_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          logical_interpreter->deposit_signal(
              *logical_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)logical_interpreter->run();
          assert(
              logical_interpreter
                  ->signal_value(*logical_and)
                  .to_msb_string()
              == expected_and);
          assert(
              logical_interpreter
                  ->signal_value(*logical_or)
                  .to_msb_string()
              == expected_or);
        };
    run_logical("0000", "X1", "0", "1");
    run_logical("00X0", "00", "0", "X");
    run_logical("00X0", "01", "X", "1");
    run_logical("0010", "ZZ", "X", "1");
    run_logical("0010", "01", "1", "1");

    const auto reduction_shift_process =
        fsim::frontend::parse_text(
            "reduction_shift_process.sv",
            R"(
module reduction_shift_process;
  logic [3:0] value;
  logic [2:0] amount;
  logic reduced_and;
  logic reduced_or;
  logic reduced_xor;
  logic [3:0] shifted_left;
  logic [3:0] shifted_right;
  always_comb begin
    reduced_and = &value;
    reduced_or = |value;
    reduced_xor = ^value;
    shifted_left = value << amount;
    shifted_right = value >> amount;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reduction_shift_process.ok());
    const auto elaborated_reduction_shift =
        fsim::elaboration::elaborate(
            reduction_shift_process.design,
            "sv:work.reduction_shift_process");
    assert(elaborated_reduction_shift.ok());
    const auto reduction_value =
        elaborated_reduction_shift.design->find_signal("value");
    const auto shift_amount =
        elaborated_reduction_shift.design->find_signal("amount");
    const auto reduced_and =
        elaborated_reduction_shift.design->find_signal("reduced_and");
    const auto reduced_or =
        elaborated_reduction_shift.design->find_signal("reduced_or");
    const auto reduced_xor =
        elaborated_reduction_shift.design->find_signal("reduced_xor");
    const auto shifted_left =
        elaborated_reduction_shift.design->find_signal("shifted_left");
    const auto shifted_right =
        elaborated_reduction_shift.design->find_signal("shifted_right");
    assert(
        reduction_value && shift_amount && reduced_and
        && reduced_or && reduced_xor && shifted_left
        && shifted_right);
    auto reduction_shift_interpreter =
        elaborated_reduction_shift.design->create_interpreter();
    const auto run_reduction_shift =
        [&](const std::string_view value,
            const std::string_view amount,
            const std::string_view expected_and,
            const std::string_view expected_or,
            const std::string_view expected_xor,
            const std::string_view expected_left,
            const std::string_view expected_right) {
          reduction_shift_interpreter->deposit_signal(
              *reduction_value,
              fsim::runtime::PackedLogic4::from_msb_string(value));
          reduction_shift_interpreter->deposit_signal(
              *shift_amount,
              fsim::runtime::PackedLogic4::from_msb_string(amount));
          (void)reduction_shift_interpreter->run();
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_and)
                  .to_msb_string()
              == expected_and);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_or)
                  .to_msb_string()
              == expected_or);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xor)
                  .to_msb_string()
              == expected_xor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_left)
                  .to_msb_string()
              == expected_left);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_right)
                  .to_msb_string()
              == expected_right);
        };
    run_reduction_shift(
        "1111", "001", "1", "1", "0", "1110", "0111");
    run_reduction_shift(
        "1011", "000", "0", "1", "1", "1011", "1011");
    run_reduction_shift(
        "10X1", "001", "0", "1", "X", "0X10", "010X");
    run_reduction_shift(
        "11X1", "011", "X", "1", "X", "1000", "0001");
    run_reduction_shift(
        "00X0", "0X1", "0", "X", "X", "XXXX", "XXXX");
    run_reduction_shift(
        "Z001", "100", "0", "1", "X", "0000", "0000");

    const auto arithmetic_process =
        fsim::frontend::parse_text(
            "arithmetic_process.sv",
            R"(
module arithmetic_process;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] difference;
  logic [7:0] product;
  logic [7:0] quotient;
  logic [7:0] remainder;
  logic [7:0] positive;
  logic [7:0] negative;
  always_comb begin
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    positive = +lhs;
    negative = -lhs;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(arithmetic_process.ok());
    const auto elaborated_arithmetic =
        fsim::elaboration::elaborate(
            arithmetic_process.design,
            "sv:work.arithmetic_process");
    assert(elaborated_arithmetic.ok());
    const auto arithmetic_lhs =
        elaborated_arithmetic.design->find_signal("lhs");
    const auto arithmetic_rhs =
        elaborated_arithmetic.design->find_signal("rhs");
    const std::array arithmetic_outputs{
        elaborated_arithmetic.design->find_signal("difference"),
        elaborated_arithmetic.design->find_signal("product"),
        elaborated_arithmetic.design->find_signal("quotient"),
        elaborated_arithmetic.design->find_signal("remainder"),
        elaborated_arithmetic.design->find_signal("positive"),
        elaborated_arithmetic.design->find_signal("negative")};
    assert(arithmetic_lhs && arithmetic_rhs);
    assert(std::ranges::all_of(
        arithmetic_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto arithmetic_interpreter =
        elaborated_arithmetic.design->create_interpreter();
    const auto run_arithmetic =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 6>& expected) {
          arithmetic_interpreter->deposit_signal(
              *arithmetic_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          arithmetic_interpreter->deposit_signal(
              *arithmetic_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)arithmetic_interpreter->run();
          for (std::size_t index = 0;
               index < arithmetic_outputs.size(); ++index) {
            assert(
                arithmetic_interpreter
                    ->signal_value(*arithmetic_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_arithmetic(
        "11001000",
        "00000111",
        {"11000001", "01111000", "00011100",
         "00000100", "11001000", "00111000"});
    run_arithmetic(
        "10X01000",
        "00000111",
        {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
         "XXXXXXXX", "10X01000", "XXXXXXXX"});
    run_arithmetic(
        "11001000",
        "00000000",
        {"11001000", "00000000", "XXXXXXXX",
         "XXXXXXXX", "11001000", "00111000"});

    const auto signed_arithmetic = fsim::frontend::parse_text(
        "signed_arithmetic.sv",
        R"(
module signed_arithmetic;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic [7:0] unsigned_rhs;
  logic signed [7:0] sum;
  logic signed [7:0] difference;
  logic signed [7:0] product;
  logic signed [7:0] quotient;
  logic signed [7:0] remainder;
  logic less;
  logic mixed_less;
  always_comb begin
    sum = lhs + rhs;
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    less = lhs < rhs;
    mixed_less = lhs < unsigned_rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_arithmetic.ok());
    const auto elaborated_signed_arithmetic =
        fsim::elaboration::elaborate(
            signed_arithmetic.design,
            "sv:work.signed_arithmetic");
    assert(elaborated_signed_arithmetic.ok());
    const auto signed_lhs =
        elaborated_signed_arithmetic.design->find_signal("lhs");
    const auto signed_rhs =
        elaborated_signed_arithmetic.design->find_signal("rhs");
    const auto unsigned_rhs =
        elaborated_signed_arithmetic.design->find_signal(
            "unsigned_rhs");
    const std::array signed_outputs{
        elaborated_signed_arithmetic.design->find_signal("sum"),
        elaborated_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_signed_arithmetic.design->find_signal("product"),
        elaborated_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_signed_arithmetic.design->find_signal("less"),
        elaborated_signed_arithmetic.design->find_signal(
            "mixed_less")};
    assert(signed_lhs && signed_rhs && unsigned_rhs);
    assert(std::ranges::all_of(
        signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto signed_interpreter =
        elaborated_signed_arithmetic.design->create_interpreter();
    const auto run_signed =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view unsigned_value,
            const std::array<std::string_view, 7>& expected) {
          signed_interpreter->deposit_signal(
              *signed_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          signed_interpreter->deposit_signal(
              *signed_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          signed_interpreter->deposit_signal(
              *unsigned_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(
                  unsigned_value));
          (void)signed_interpreter->run();
          for (std::size_t index = 0;
               index < signed_outputs.size(); ++index) {
            assert(
                signed_interpreter
                    ->signal_value(*signed_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_signed(
        "11111011",
        "00000011",
        "00000001",
        {"11111110", "11111000", "11110001",
         "11111111", "11111110", "1", "0"});
    run_signed(
        "00000101",
        "11111101",
        "11111111",
        {"00000010", "00001000", "11110001",
         "11111111", "00000010", "0", "1"});

    const auto vhdl_signed_arithmetic =
        fsim::frontend::parse_text(
            "vhdl_signed_arithmetic.vhd",
            R"(
entity vhdl_signed_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in signed(7 downto 0);
    sum : out signed(7 downto 0);
    difference : out signed(7 downto 0);
    product : out signed(7 downto 0);
    quotient : out signed(7 downto 0);
    remainder : out signed(7 downto 0);
    modulo : out signed(7 downto 0);
    less : out std_logic
  );
end entity;

architecture rtl of vhdl_signed_arithmetic is
begin
  calculate: process(lhs, rhs)
  begin
    sum <= lhs + rhs;
    difference <= lhs - rhs;
    product <= lhs * rhs;
    quotient <= lhs / rhs;
    remainder <= lhs rem rhs;
    modulo <= lhs mod rhs;
    less <= lhs < rhs;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_signed_arithmetic.ok());
    const auto elaborated_vhdl_signed_arithmetic =
        fsim::elaboration::elaborate(
            vhdl_signed_arithmetic.design,
            "vhdl:work.vhdl_signed_arithmetic(rtl)");
    if (!elaborated_vhdl_signed_arithmetic.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_signed_arithmetic.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_signed_arithmetic.ok());
    const auto vhdl_signed_lhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "lhs");
    const auto vhdl_signed_rhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rhs");
    const std::array vhdl_signed_outputs{
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "sum"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "product"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "modulo"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "less")};
    assert(vhdl_signed_lhs && vhdl_signed_rhs);
    assert(std::ranges::all_of(
        vhdl_signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_signed_interpreter =
        elaborated_vhdl_signed_arithmetic.design
            ->create_interpreter();
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "11111011"));
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_rhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000011"));
    (void)vhdl_signed_interpreter->run();
    const std::array<std::string_view, 7>
        expected_vhdl_signed{
            "11111110",
            "11111000",
            "11110001",
            "11111111",
            "11111110",
            "00000001",
            "1"};
    for (std::size_t index = 0;
         index < vhdl_signed_outputs.size(); ++index) {
      assert(
          vhdl_signed_interpreter
              ->signal_value(*vhdl_signed_outputs[index])
              .to_msb_string()
          == expected_vhdl_signed[index]);
    }

    const auto mixed_vhdl_arithmetic =
        fsim::frontend::parse_text(
            "mixed_vhdl_arithmetic.vhd",
            R"(
entity mixed_vhdl_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in unsigned(7 downto 0);
    result : out signed(7 downto 0)
  );
end entity;
architecture rtl of mixed_vhdl_arithmetic is
begin
  result <= lhs + rhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(mixed_vhdl_arithmetic.ok());
    const auto rejected_mixed_vhdl_arithmetic =
        fsim::elaboration::elaborate(
            mixed_vhdl_arithmetic.design,
            "vhdl:work.mixed_vhdl_arithmetic(rtl)");
    assert(!rejected_mixed_vhdl_arithmetic.ok());
    assert(has_diagnostic(
        rejected_mixed_vhdl_arithmetic, "FSIM-ELAB-067"));

    const auto select_concat_process =
        fsim::frontend::parse_text(
            "select_concat_process.sv",
            R"(
module select_concat_process;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected_descending;
  logic selected_ascending;
  logic selected_local;
  logic [3:0] descending_part;
  logic [3:0] ascending_part;
  logic [8:0] joined;
  always_comb begin
    logic [5:2] local_copy;
    local_copy = descending[15:12];
    selected_descending = descending[10];
    selected_ascending = ascending[2];
    selected_local = local_copy[3];
    descending_part = descending[15:12];
    ascending_part = ascending[2:5];
    joined = {
      descending[15:12], descending[10], ascending[4:7]
    };
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(select_concat_process.ok());
    const auto elaborated_select_concat =
        fsim::elaboration::elaborate(
            select_concat_process.design,
            "sv:work.select_concat_process");
    assert(elaborated_select_concat.ok());
    const auto descending =
        elaborated_select_concat.design->find_signal("descending");
    const auto ascending =
        elaborated_select_concat.design->find_signal("ascending");
    const std::array select_concat_outputs{
        elaborated_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_select_concat.design->find_signal(
            "selected_local"),
        elaborated_select_concat.design->find_signal(
            "descending_part"),
        elaborated_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_select_concat.design->find_signal("joined")};
    assert(descending && ascending);
    assert(std::ranges::all_of(
        select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto select_concat_interpreter =
        elaborated_select_concat.design->create_interpreter();
    select_concat_interpreter->deposit_signal(
        *descending,
        fsim::runtime::PackedLogic4::from_msb_string("10XZ0110"));
    select_concat_interpreter->deposit_signal(
        *ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01ZX1100"));
    (void)select_concat_interpreter->run();
    const std::array<std::string_view, 6> expected_select_concat{
        "1", "Z", "X", "10XZ", "ZX11", "10XZ11100"};
    for (std::size_t index = 0;
         index < select_concat_outputs.size(); ++index) {
      assert(
          select_concat_interpreter
              ->signal_value(*select_concat_outputs[index])
              .to_msb_string()
          == expected_select_concat[index]);
    }

    const auto selected_assignment =
        fsim::frontend::parse_text(
            "selected_assignment.sv",
            R"(
module selected_assignment;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic [5:2] local_result;
  initial begin
    logic [5:2] local_copy;
    descending[8] <= 1'b1;
    descending = 8'b00000000;
    descending[9] = 1'b1;
    descending[15:12] = 4'b10xz;
    ascending <= 8'b10101010;
    ascending[4:5] <= 2'bxz;
    local_copy = 4'b0000;
    local_copy[3] = 1'b1;
    local_copy[5:4] = 2'bxz;
    local_result = local_copy;
    descending[11:10] <= #5 2'b11;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_assignment.ok());
    const auto elaborated_selected_assignment =
        fsim::elaboration::elaborate(
            selected_assignment.design,
            "sv:work.selected_assignment");
    if (!elaborated_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_selected_assignment.ok());
    const auto selected_descending =
        elaborated_selected_assignment.design->find_signal(
            "descending");
    const auto selected_ascending =
        elaborated_selected_assignment.design->find_signal(
            "ascending");
    const auto selected_local =
        elaborated_selected_assignment.design->find_signal(
            "local_result");
    assert(
        selected_descending && selected_ascending
        && selected_local);
    auto selected_assignment_interpreter =
        elaborated_selected_assignment.design->create_interpreter();
    const auto selected_assignment_result =
        selected_assignment_interpreter->run();
    assert(
        selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && selected_assignment_result.time == 5);
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_descending)
            .to_msb_string()
        == "10XZ1111");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_assignment_select =
        fsim::frontend::parse_text(
            "reversed_assignment_select.sv",
            R"(
module reversed_assignment_select;
  logic [0:7] value;
  initial value[5:2] = 4'b1010;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_assignment_select.ok());
    const auto rejected_reversed_assignment_select =
        fsim::elaboration::elaborate(
            reversed_assignment_select.design,
            "sv:work.reversed_assignment_select");
    assert(!rejected_reversed_assignment_select.ok());
    assert(has_diagnostic(
        rejected_reversed_assignment_select, "FSIM-ELAB-068"));

    const auto reversed_select = fsim::frontend::parse_text(
        "reversed_select.sv",
        R"(
module reversed_select;
  logic [0:7] value;
  logic [3:0] result;
  always_comb result = value[5:2];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_select.ok());
    const auto rejected_reversed_select =
        fsim::elaboration::elaborate(
            reversed_select.design, "sv:work.reversed_select");
    assert(!rejected_reversed_select.ok());
    assert(has_diagnostic(
        rejected_reversed_select, "FSIM-ELAB-068"));

    const auto dynamic_select = fsim::frontend::parse_text(
        "dynamic_select.sv",
        R"(
module dynamic_select;
  logic [7:0] value;
  logic [2:0] index;
  logic result;
  always_comb result = value[index];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_select.ok());
    const auto rejected_dynamic_select =
        fsim::elaboration::elaborate(
            dynamic_select.design, "sv:work.dynamic_select");
    assert(!rejected_dynamic_select.ok());
    assert(has_diagnostic(
        rejected_dynamic_select, "FSIM-ELAB-068"));

    const auto empty_concatenation =
        fsim::frontend::parse_text(
            "empty_concatenation.sv",
            R"(
module empty_concatenation;
  logic result;
  always_comb result = {};
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_concatenation.ok());
    const auto rejected_empty_concatenation =
        fsim::elaboration::elaborate(
            empty_concatenation.design,
            "sv:work.empty_concatenation");
    assert(!rejected_empty_concatenation.ok());
    assert(has_diagnostic(
        rejected_empty_concatenation, "FSIM-ELAB-069"));

    const auto vhdl_select_concat =
        fsim::frontend::parse_text(
            "vhdl_select_concat.vhd",
            R"(
entity vhdl_select_concat is
  port (
    descending : in std_logic_vector(7 downto 4);
    ascending : in std_logic_vector(2 to 5);
    selected_descending : out std_logic;
    selected_ascending : out std_logic;
    selected_local : out std_logic;
    descending_part : out std_logic_vector(1 downto 0);
    ascending_part : out std_logic_vector(1 downto 0);
    joined : out std_logic_vector(5 downto 0)
  );
end entity;

architecture rtl of vhdl_select_concat is
begin
  observe: process(descending, ascending)
    variable local_copy : std_logic_vector(9 downto 8);
  begin
    local_copy := descending(7 downto 6);
    selected_descending <= descending(5);
    selected_ascending <= ascending(4);
    selected_local <= local_copy(8);
    descending_part <= descending(7 downto 6);
    ascending_part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_select_concat.ok());
    const auto elaborated_vhdl_select_concat =
        fsim::elaboration::elaborate(
            vhdl_select_concat.design,
            "vhdl:work.vhdl_select_concat(rtl)");
    assert(elaborated_vhdl_select_concat.ok());
    const auto vhdl_descending =
        elaborated_vhdl_select_concat.design->find_signal(
            "descending");
    const auto vhdl_ascending =
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending");
    const std::array vhdl_select_concat_outputs{
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_local"),
        elaborated_vhdl_select_concat.design->find_signal(
            "descending_part"),
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_vhdl_select_concat.design->find_signal("joined")};
    assert(vhdl_descending && vhdl_ascending);
    assert(std::ranges::all_of(
        vhdl_select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_select_concat_interpreter =
        elaborated_vhdl_select_concat.design->create_interpreter();
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_descending,
        fsim::runtime::PackedLogic4::from_msb_string("1XZ0"));
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01Z1"));
    (void)vhdl_select_concat_interpreter->run();
    const std::array<std::string_view, 6>
        expected_vhdl_select_concat{
            "Z", "Z", "X", "1X", "1Z", "1X10Z1"};
    for (std::size_t index = 0;
         index < vhdl_select_concat_outputs.size(); ++index) {
      assert(
          vhdl_select_concat_interpreter
              ->signal_value(*vhdl_select_concat_outputs[index])
              .to_msb_string()
          == expected_vhdl_select_concat[index]);
    }

    const auto vhdl_selected_assignment =
        fsim::frontend::parse_text(
            "vhdl_selected_assignment.vhd",
            R"(
entity vhdl_selected_assignment is
end entity;

architecture rtl of vhdl_selected_assignment is
  signal trigger : std_logic;
  signal descending : std_logic_vector(15 downto 8);
  signal ascending : std_logic_vector(0 to 7);
  signal local_result : std_logic_vector(5 downto 2);
begin
  update: process(trigger)
    variable local_copy : std_logic_vector(5 downto 2);
  begin
    descending <= "00000000";
    descending(9) <= '1';
    descending(15 downto 12) <= "10XZ";
    ascending <= "10101010";
    ascending(4 to 5) <= "XZ";
    local_copy := "0000";
    local_copy(3) := '1';
    local_copy(5 downto 4) := "XZ";
    local_result <= local_copy;
    descending(11 downto 10) <= "11" after 5 ns;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_selected_assignment.ok());
    const auto elaborated_vhdl_selected_assignment =
        fsim::elaboration::elaborate(
            vhdl_selected_assignment.design,
            "vhdl:work.vhdl_selected_assignment(rtl)");
    if (!elaborated_vhdl_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_selected_assignment.ok());
    const auto vhdl_assigned_descending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "descending");
    const auto vhdl_assigned_ascending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "ascending");
    const auto vhdl_assigned_local =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "local_result");
    assert(
        vhdl_assigned_descending && vhdl_assigned_ascending
        && vhdl_assigned_local);
    auto vhdl_selected_assignment_interpreter =
        elaborated_vhdl_selected_assignment.design
            ->create_interpreter();
    const auto vhdl_selected_assignment_result =
        vhdl_selected_assignment_interpreter->run();
    assert(
        vhdl_selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && vhdl_selected_assignment_result.time == 5);
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_descending)
            .to_msb_string()
        == "10XZ1110");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_vhdl_select =
        fsim::frontend::parse_text(
            "reversed_vhdl_select.vhd",
            R"(
entity reversed_vhdl_select is
  port (
    value : in std_logic_vector(2 to 5);
    result : out std_logic_vector(1 downto 0)
  );
end entity;
architecture rtl of reversed_vhdl_select is
begin
  result <= value(4 downto 3);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(reversed_vhdl_select.ok());
    const auto rejected_reversed_vhdl_select =
        fsim::elaboration::elaborate(
            reversed_vhdl_select.design,
            "vhdl:work.reversed_vhdl_select(rtl)");
    assert(!rejected_reversed_vhdl_select.ok());
    assert(has_diagnostic(
        rejected_reversed_vhdl_select, "FSIM-ELAB-068"));

    const auto empty_wildcard = fsim::frontend::parse_text(
        "empty_wildcard.sv",
        R"(
module empty_wildcard;
  logic q;
  always @* q = 1'b0;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(empty_wildcard.ok());
    const auto rejected_empty_wildcard =
        fsim::elaboration::elaborate(
            empty_wildcard.design, "sv:work.empty_wildcard");
    assert(!rejected_empty_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_wildcard, "FSIM-ELAB-061"));

    const auto dynamic_wildcard = fsim::frontend::parse_text(
        "dynamic_wildcard.sv",
        R"(
module dynamic_wildcard;
  logic trigger;
  logic observed;
  initial @* observed = trigger;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_wildcard.ok());
    const auto elaborated_dynamic_wildcard =
        fsim::elaboration::elaborate(
            dynamic_wildcard.design, "sv:work.dynamic_wildcard");
    assert(elaborated_dynamic_wildcard.ok());
    const auto& dynamic_wait_process =
        elaborated_dynamic_wildcard.design->processes().front();
    const auto dynamic_wait = std::find_if(
        dynamic_wait_process.operations.begin(),
        dynamic_wait_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    assert(dynamic_wait != dynamic_wait_process.operations.end());
    assert(
        std::get<fsim::runtime::simir::WaitOn>(*dynamic_wait)
            .signals.size()
        == 1);
    auto dynamic_wildcard_interpreter =
        elaborated_dynamic_wildcard.design->create_interpreter();
    const auto dynamic_observed =
        elaborated_dynamic_wildcard.design->find_signal("observed");
    assert(dynamic_observed);
    const auto dynamic_wildcard_run =
        dynamic_wildcard_interpreter->run();
    assert(
        dynamic_wildcard_run.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        dynamic_wildcard_interpreter
            ->signal_value(*dynamic_observed)
            .to_msb_string()
        == "0");

    const auto empty_dynamic_wildcard =
        fsim::frontend::parse_text(
            "empty_dynamic_wildcard.sv",
            R"(
module empty_dynamic_wildcard;
  initial @*;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_dynamic_wildcard.ok());
    const auto rejected_empty_dynamic_wildcard =
        fsim::elaboration::elaborate(
            empty_dynamic_wildcard.design,
            "sv:work.empty_dynamic_wildcard");
    assert(!rejected_empty_dynamic_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_dynamic_wildcard, "FSIM-ELAB-062"));

    const auto parsed_sv_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.sv",
            R"(
module conditional_flow;
  logic zero_case;
  logic one_x_case;
  logic unknown_case;
  logic [3:0] nested_case;
  initial begin
    if (4'b0000)
      zero_case = 1'b1;
    else
      zero_case = 1'b0;
    if (4'bx001)
      one_x_case = 1'b1;
    else
      one_x_case = 1'b0;
    if (4'bx000)
      unknown_case = 1'b1;
    else
      unknown_case = 1'b0;
    if (4'b0010) begin
      if (1'b0)
        nested_case = 4'b0001;
      else
        nested_case = 4'b0010;
    end else begin
      nested_case = 4'b0011;
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_sv_conditionals.ok());
    const auto elaborated_sv_conditionals =
        fsim::elaboration::elaborate(
            parsed_sv_conditionals.design,
            "sv:work.conditional_flow");
    assert(elaborated_sv_conditionals.ok());
    const auto& sv_conditional_process =
        elaborated_sv_conditionals.design->processes().front();
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return std::holds_alternative<
                    fsim::runtime::simir::Branch>(operation);
            })
        == 5);
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return std::holds_alternative<
                    fsim::runtime::simir::LogicalNot>(operation);
            })
        == 10);
    auto sv_conditional_interpreter =
        elaborated_sv_conditionals.design->create_interpreter();
    const auto sv_conditional_result =
        sv_conditional_interpreter->run();
    assert(
        sv_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    const auto zero_case =
        elaborated_sv_conditionals.design->find_signal("zero_case");
    const auto one_x_case =
        elaborated_sv_conditionals.design->find_signal("one_x_case");
    const auto unknown_case =
        elaborated_sv_conditionals.design->find_signal("unknown_case");
    const auto nested_case =
        elaborated_sv_conditionals.design->find_signal("nested_case");
    assert(zero_case && one_x_case && unknown_case && nested_case);
    assert(
        sv_conditional_interpreter
            ->signal_value(*zero_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*one_x_case)
            .to_msb_string()
        == "1");
    assert(
        sv_conditional_interpreter
            ->signal_value(*unknown_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*nested_case)
            .to_msb_string()
        == "0010");

    const auto parsed_vhdl_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.vhd",
            R"(
entity conditional_flow is
  port (
    true_case : out boolean;
    elsif_case : out boolean;
    nested_case : out boolean;
    boolean_expression_case : out boolean
  );
end entity;

architecture rtl of conditional_flow is
  signal trigger : std_logic;
begin
  choose: process(trigger)
  begin
    if true then
      true_case <= true;
    else
      true_case <= false;
    end if;
    if false then
      elsif_case <= false;
    elsif true /= false then
      elsif_case <= true;
    else
      elsif_case <= false;
    end if;
    if 1 = 1 then
      if false then
        nested_case <= false;
      else
        nested_case <= true;
      end if;
    else
      nested_case <= false;
    end if;
    if (not false) and (true nand false)
       and (false nor false) and (true xnor true)
       and (true /= false) then
      boolean_expression_case <= true;
    else
      boolean_expression_case <= false;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl_conditionals.ok());
    const auto elaborated_vhdl_conditionals =
        fsim::elaboration::elaborate(
            parsed_vhdl_conditionals.design,
            "vhdl:work.conditional_flow(rtl)");
    if (!elaborated_vhdl_conditionals.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_conditionals.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_conditionals.ok());
    auto vhdl_conditional_interpreter =
        elaborated_vhdl_conditionals.design->create_interpreter();
    const auto vhdl_conditional_result =
        vhdl_conditional_interpreter->run();
    assert(
        vhdl_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto name :
         {"true_case", "elsif_case", "nested_case",
          "boolean_expression_case"}) {
        const auto signal =
            elaborated_vhdl_conditionals.design->find_signal(name);
        assert(signal);
        assert(
            vhdl_conditional_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "1");
    }

    const auto invalid_vhdl_condition =
        fsim::frontend::parse_text(
            "invalid_condition.vhd",
            R"(
entity invalid_condition is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_condition is
begin
  invalid: process(gate)
  begin
    if gate then
      null;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition.ok());
    const auto rejected_vhdl_condition =
        fsim::elaboration::elaborate(
            invalid_vhdl_condition.design,
            "vhdl:work.invalid_condition(rtl)");
    assert(!rejected_vhdl_condition.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition, "FSIM-ELAB-048"));

    const auto invalid_vhdl_assertion =
        fsim::frontend::parse_text(
            "invalid_assertion.vhd",
            R"(
entity invalid_assertion is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_assertion is
begin
  invalid: process(gate)
  begin
    assert gate;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_assertion.ok());
    const auto rejected_vhdl_assertion =
        fsim::elaboration::elaborate(
            invalid_vhdl_assertion.design,
            "vhdl:work.invalid_assertion(rtl)");
    assert(!rejected_vhdl_assertion.ok());
    assert(has_diagnostic(
        rejected_vhdl_assertion, "FSIM-ELAB-051"));

    const auto vector_assertion =
        fsim::frontend::parse_text(
            "vector_assertion.sv",
            R"(
module vector_assertion;
  initial begin
    assert (4'bx001);
    assert (4'bx000) else $error("vector condition failed");
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(vector_assertion.ok());
    const auto elaborated_vector_assertion =
        fsim::elaboration::elaborate(
            vector_assertion.design,
            "sv:work.vector_assertion");
    assert(elaborated_vector_assertion.ok());
    auto vector_assertion_interpreter =
        elaborated_vector_assertion.design->create_interpreter();
    bool saw_vector_assertion = false;
    try {
        (void)vector_assertion_interpreter->run();
    } catch (const fsim::runtime::simir::AssertionError& error) {
        saw_vector_assertion =
            std::string_view{error.what()}.find(
                "vector condition failed")
            != std::string_view::npos;
    }
    assert(saw_vector_assertion);

    const auto conflicting_systemc_alias_source =
        fsim::frontend::parse_text(
            "conflicting_systemc_alias.sv",
            R"(
module conflict_host;
  logic first;
  logic second;
  conflict_placeholder u_conflict(
    .first(first),
    .second(second));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(conflicting_systemc_alias_source.ok());
    const fsim::elaboration::SystemCInstanceDescription
        conflicting_systemc_instance{
            "conflict_host.u_conflict",
            "systemc:models.conflict",
            1000,
            0,
            {
                {1001,
                 "first",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
                {1002,
                 "second",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
            },
            {},
            {},
            {},
            {{1003, "shared"}},
            {{1003,
              "shared",
              systemc_logic,
              fsim::runtime::PackedLogic4::from_msb_string("0")}},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        conflicting_systemc_bindings{
            {"conflict_host.u_conflict",
             "systemc:models.conflict",
             std::nullopt},
        };
    const auto rejected_systemc_alias =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            std::span{&conflicting_systemc_instance, 1});
    assert(!rejected_systemc_alias.ok());
    assert(has_diagnostic(
        rejected_systemc_alias, "FSIM-ELAB-BIND-046"));

    auto invalid_native_hierarchy = conflicting_systemc_instance;
    for (auto& port : invalid_native_hierarchy.ports) {
        port.bound_object = 0;
    }
    fsim::elaboration::SystemCInstanceDescription invalid_native_child;
    invalid_native_child.path = "conflict_host.u_conflict.leaf";
    invalid_native_child.target = "systemc:models.conflict";
    invalid_native_child.handle = 2000;
    invalid_native_child.parent = 9999;
    invalid_native_hierarchy.native_children.push_back(
        std::move(invalid_native_child));
    const std::array invalid_native_instances{
        invalid_native_hierarchy};
    const auto rejected_native_hierarchy =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            invalid_native_instances);
    assert(!rejected_native_hierarchy.ok());
    assert(has_diagnostic(
        rejected_native_hierarchy, "FSIM-ELAB-BIND-047"));

    std::cout << "elaborator tests passed\n";
}
