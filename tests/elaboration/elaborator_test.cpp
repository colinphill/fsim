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
module wildcard;
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
            wildcard_processes.design, "sv:work.wildcard");
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
    const auto rejected_signed_comparison =
        fsim::elaboration::elaborate(
            signed_comparison.design,
            "sv:work.signed_comparison");
    assert(!rejected_signed_comparison.ok());
    assert(has_diagnostic(
        rejected_signed_comparison, "FSIM-ELAB-066"));

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

    std::cout << "elaborator tests passed\n";
}
