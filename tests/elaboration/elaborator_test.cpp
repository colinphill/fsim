// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
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
    const auto rejected_not =
        fsim::elaboration::elaborate(logical_not.design, "logical_not");
    assert(!rejected_not.ok());
    assert(has_diagnostic(rejected_not, "FSIM-ELAB-044"));

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

    std::cout << "elaborator tests passed\n";
}
