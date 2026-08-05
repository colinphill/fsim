// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_verilog_udp_resolution() {
    const auto parsed = fsim::frontend::parse_text(
        "udp-resolution.v",
        R"(
primitive udp_inv (q, d);
  output q;
  input d;
  table
    0 : 1;
    1 : 0;
    x : x;
  endtable
endprimitive

module udp_parent(input d, output q);
  udp_inv selected(q, d);
endmodule

primitive dff_udp (q, d, clock);
  output q; reg q; input d, clock;
  initial q = 1'b0;
  table
    0 (01) : ? : 0;
    1 r    : ? : 1;
    ? n    : ? : -;
  endtable
endprimitive

module dff_parent(input d, input clock, output q);
  dff_udp stateful(q, d, clock);
endmodule

module builtin_parent(input a, input b, output q);
  and builtin_gate(q, a, b);
endmodule

module udp_named(input d, output q);
  udp_inv invalid_named(.q(q), .d(d));
endmodule

module udp_parameterized(input d, output q);
  udp_inv #(.UNSUPPORTED(1)) invalid_parameter(q, d);
endmodule

module udp_forms;
  wire d0, d1, d2, q0, q1, q2;
  wire [1:0] dv, qv;
  udp_inv (q0, d0);
  udp_inv first(q1, d1), second(q2, d2);
  udp_inv bank[1:0](qv, dv);
  udp_inv #(1, 2, 3) delayed(q0, d0);
endmodule

module udp_delays;
  reg drive;
  wire delayed;
  wire zero;
  udp_inv #(5, 7, 11) timed(delayed, drive);
  udp_inv #0 immediate(zero, drive);
  initial begin
    drive = 1'b0;
    #10 drive = 1'b1;
    #2 drive = 1'b0;
    #18 drive = 1'b1;
    #20 drive = 1'b1;
    #10 drive = 1'bz;
    #20 $finish;
  end
endmodule

module udp_generate_leaf #(parameter WIDTH = 1)(
  input [WIDTH-1:0] d,
  output [WIDTH-1:0] q);
  genvar lane;
  generate
    for (lane = 0; lane < WIDTH; lane = lane + 1) begin : lanes
      wire lane_d;
      wire lane_q;
      assign lane_d = d[lane];
      assign q[lane] = lane_q;
      udp_inv gate(lane_q, lane_d);
    end
  endgenerate
endmodule

module udp_generate_top;
  reg one_d;
  reg [1:0] two_d;
  wire one_q;
  wire [1:0] two_q;
  udp_generate_leaf #(.WIDTH(1)) one(.d(one_d), .q(one_q));
  udp_generate_leaf #(.WIDTH(2)) two(.d(two_d), .q(two_q));
  initial begin
    one_d = 1'b1;
    two_d = 2'b01;
  end
endmodule

module udp_wrapper;
  reg d;
  wire q;
  udp_inv gate(q, d);
  initial d = 1'b0;
endmodule

module udp_boundary_wrapper(input d, output q);
  udp_inv gate(q, d);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parsed.ok());

    const auto selected = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_parent");
    assert(selected.ok());
    assert(selected.design->specializations().size() == 2);
    const auto& udp = selected.design->specializations().back();
    assert(udp.instance == "udp_parent.selected");
    assert(udp.unit == "sv:work.udp_inv");
    assert(selected.design->udp_tables().size() == 1);
    const auto& table = selected.design->udp_tables().front();
    assert(
        table.id == 0 && table.identity == "udp:work.udp_inv"
        && table.digest.size() == 64 && !table.sequential
        && table.terminals
            == std::vector<std::string>({"q", "d"})
        && table.rows.size() == 3);
    assert(
        udp.parameter_identity_values.size() == 2
        && udp.parameter_identity_values[0].first == "__udp"
        && udp.parameter_identity_values[0].second
            == "udp:work.udp_inv"
        && udp.parameter_identity_values[1].first == "__udp_table"
        && udp.parameter_identity_values[1].second == table.digest);
    const auto d = selected.design->find_signal("d");
    const auto q = selected.design->find_signal("q");
    assert(d && q);
    auto interpreter = selected.design->create_interpreter();
    for (const auto& [input, expected] :
         std::array{
             std::pair{"0", "1"},
             std::pair{"1", "0"},
             std::pair{"X", "X"},
             std::pair{"Z", "X"}}) {
        interpreter->deposit_signal(
            *d, fsim::runtime::PackedLogic4::from_msb_string(input));
        (void)interpreter->run();
        assert(interpreter->signal_value(*q).to_msb_string() == expected);
    }

    const auto stateful = fsim::elaboration::elaborate(
        parsed.design, "sv:work.dff_parent");
    assert(stateful.ok());
    assert(
        stateful.design->udp_tables().size() == 1
        && stateful.design->udp_tables().front().sequential
        && stateful.design->udp_tables().front().initial_output);
    const auto state_d = stateful.design->find_signal("d");
    const auto state_clock = stateful.design->find_signal("clock");
    const auto state_q = stateful.design->find_signal("q");
    const auto previous_d = stateful.design->find_signal(
        "dff_parent.stateful.$udp_previous$0");
    const auto previous_clock = stateful.design->find_signal(
        "dff_parent.stateful.$udp_previous$1");
    assert(state_d && state_clock && state_q && previous_d && previous_clock);
    auto state_interpreter = stateful.design->create_interpreter();
    (void)state_interpreter->run();
    assert(state_interpreter->signal_value(*state_q).to_msb_string() == "0");
    const auto deposit_and_run = [&](const auto signal,
                                     const std::string_view value) {
        state_interpreter->deposit_signal(
            signal, fsim::runtime::PackedLogic4::from_msb_string(value));
        (void)state_interpreter->run();
    };
    deposit_and_run(*state_d, "1");
    deposit_and_run(*state_clock, "0");
    assert(
        state_interpreter->signal_value(*previous_d).to_msb_string() == "1"
        && state_interpreter->signal_value(*previous_clock).to_msb_string()
            == "0");
    assert(state_interpreter->signal_value(*state_q).to_msb_string() == "0");
    deposit_and_run(*state_clock, "1");
    assert(state_interpreter->signal_value(*state_q).to_msb_string() == "1");
    deposit_and_run(*state_d, "0");
    deposit_and_run(*state_clock, "0");
    assert(state_interpreter->signal_value(*state_q).to_msb_string() == "1");
    deposit_and_run(*state_clock, "1");
    assert(state_interpreter->signal_value(*state_q).to_msb_string() == "0");

    const auto builtin = fsim::elaboration::elaborate(
        parsed.design, "sv:work.builtin_parent");
    assert(builtin.ok());
    assert(builtin.design->specializations().size() == 1);

    const auto named = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_named");
    assert(!named.ok());
    assert(has_diagnostic(named, "FSIM-ELAB-BIND-061"));

    const auto parameterized = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_parameterized");
    assert(!parameterized.ok());
    assert(has_diagnostic(parameterized, "FSIM-ELAB-BIND-060"));

    const auto forms = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_forms");
    if (!forms.ok()) {
        for (const auto& diagnostic : forms.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(forms.ok());
    assert(forms.design->specializations().size() == 7);
    assert(
        forms.design->udp_tables().size() == 1
        && forms.design->udp_tables().front().digest == table.digest);
    const auto has_instance = [&](const std::string_view path) {
        return std::ranges::any_of(
            forms.design->specializations(),
            [&](const auto& specialization) {
                return specialization.instance == path;
            });
    };
    assert(has_instance("udp_forms.first"));
    assert(has_instance("udp_forms.second"));
    assert(has_instance("udp_forms.bank[1]"));
    assert(has_instance("udp_forms.bank[0]"));
    assert(has_instance("udp_forms.delayed"));
    assert(std::ranges::any_of(
        forms.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance.starts_with(
                "udp_forms.$udp$");
        }));

    const auto delayed = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_delays");
    assert(delayed.ok());
    const auto delayed_q = delayed.design->find_signal("delayed");
    const auto zero_q = delayed.design->find_signal("zero");
    assert(delayed_q && zero_q);
    std::vector<std::pair<
        std::string, fsim::runtime::SimulationTick>> delayed_changes;
    std::vector<std::pair<
        std::string, fsim::runtime::SimulationTick>> zero_changes;
    auto delayed_interpreter = delayed.design->create_interpreter();
    delayed_interpreter->set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time) {
            if (signal == *delayed_q) {
                delayed_changes.emplace_back(value.to_msb_string(), time);
            } else if (signal == *zero_q) {
                zero_changes.emplace_back(value.to_msb_string(), time);
            }
        });
    const auto delayed_result = delayed_interpreter->run();
    assert(
        delayed_result.status == fsim::runtime::RunStatus::stopped
        && delayed_result.time == 80);
    assert((
        delayed_changes
        == std::vector<std::pair<
            std::string, fsim::runtime::SimulationTick>>{
            {"1", 5}, {"0", 37}, {"X", 65}}));
    assert((
        zero_changes
        == std::vector<std::pair<
            std::string, fsim::runtime::SimulationTick>>{
            {"X", 0}, {"1", 0}, {"0", 10}, {"1", 12},
            {"0", 30}, {"X", 60}}));

    const auto generated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.udp_generate_top");
    if (!generated.ok()) {
        for (const auto& diagnostic : generated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(generated.ok());
    const auto generated_one = generated.design->find_signal("one_q");
    const auto generated_two = generated.design->find_signal("two_q");
    assert(generated_one && generated_two);
    assert(std::ranges::any_of(
        generated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "udp_generate_top.one.lanes[0].gate";
        }));
    assert(std::ranges::any_of(
        generated.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "udp_generate_top.two.lanes[1].gate";
        }));
    auto generated_interpreter = generated.design->create_interpreter();
    assert(
        generated_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_interpreter->signal_value(*generated_one).to_msb_string()
            == "0"
        && generated_interpreter
               ->signal_value(*generated_two)
               .to_msb_string()
            == "10");

    const std::array roots{
        fsim::elaboration::Root{"udp_parent", "comb"},
        fsim::elaboration::Root{"dff_parent", "state"}};
    const auto multiple_roots = fsim::elaboration::elaborate(
        parsed.design, roots, {}, {}, nullptr, {});
    assert(multiple_roots.ok());
    assert((
        multiple_roots.design->roots()
        == std::vector<std::string>{"comb", "state"}));
    assert(multiple_roots.design->udp_tables().size() == 2);

    auto searched_design = parsed.design;
    for (auto& declaration : searched_design.udp_declarations) {
        declaration.library = "vendor";
    }
    const std::array<std::string, 1> vendor_search{"vendor"};
    const auto searched = fsim::elaboration::elaborate(
        searched_design,
        "sv:work.udp_wrapper",
        {},
        {},
        nullptr,
        vendor_search);
    assert(searched.ok());
    assert(std::ranges::any_of(
        searched.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance == "udp_wrapper.gate"
                && specialization.unit == "sv:vendor.udp_inv";
        }));

    const auto vhdl_parent = fsim::frontend::parse_text(
        "udp-vhdl-parent.vhd",
        R"(
entity udp_vhdl_parent is end entity;
architecture rtl of udp_vhdl_parent is
  component udp_boundary_wrapper is
    port (d : in std_logic; q : out std_logic);
  end component;
  signal d : std_logic;
  signal q : std_logic;
begin
  d <= '0';
  wrapped: udp_boundary_wrapper port map (d => d, q => q);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_parent.ok());
    auto vhdl_mixed = vhdl_parent.design;
    const auto wrapper = std::ranges::find(
        parsed.design.units,
        "udp_boundary_wrapper",
        &fsim::frontend::DesignUnit::name);
    assert(wrapper != parsed.design.units.end());
    vhdl_mixed.units.push_back(*wrapper);
    vhdl_mixed.udp_declarations = parsed.design.udp_declarations;
    const auto vhdl_boundary = fsim::elaboration::elaborate(
        vhdl_mixed, "vhdl:work.udp_vhdl_parent(rtl)");
    if (!vhdl_boundary.ok()) {
        for (const auto& diagnostic : vhdl_boundary.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_boundary.ok());
    assert(std::ranges::any_of(
        vhdl_boundary.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "udp_vhdl_parent.wrapped.gate";
        }));

    fsim::elaboration::SystemCInstanceDescription systemc_root;
    systemc_root.path = "udp_systemc_root";
    systemc_root.target = "systemc:work.udp_systemc_root";
    systemc_root.handle = 500;
    fsim::elaboration::ForeignChild systemc_child;
    systemc_child.handle = 501;
    systemc_child.name = "wrapped";
    systemc_child.module_facade = true;
    systemc_child.implementation = "udp_wrapper";
    systemc_root.foreign_children.push_back(std::move(systemc_child));
    const std::array systemc_roots{systemc_root};
    const auto systemc_boundary = fsim::elaboration::elaborate(
        parsed.design,
        "systemc:work.udp_systemc_root",
        {},
        systemc_roots);
    assert(systemc_boundary.ok());
    assert(std::ranges::any_of(
        systemc_boundary.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "udp_systemc_root.wrapped.gate";
        }));

    auto ambiguous = parsed.design;
    const auto module = fsim::frontend::parse_text(
        "udp-collision.v",
        "module udp_inv(output q, input d); assign q = d; endmodule\n",
        fsim::frontend::Language::Verilog2005);
    assert(module.ok());
    ambiguous.units.insert(
        ambiguous.units.end(),
        module.design.units.begin(),
        module.design.units.end());
    const auto collision = fsim::elaboration::elaborate(
        ambiguous, "sv:work.udp_parent");
    assert(!collision.ok());
    assert(has_diagnostic(collision, "FSIM-ELAB-BIND-017"));

    const auto module_strength = fsim::frontend::parse_text(
        "module-strength.v",
        R"(
module ordinary_child(output q, input d); assign q = d; endmodule
module ordinary_parent;
  wire d, q;
  ordinary_child (weak1, strong0) child(q, d);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(module_strength.ok());
    const auto rejected_module_strength = fsim::elaboration::elaborate(
        module_strength.design, "verilog:work.ordinary_parent");
    assert(!rejected_module_strength.ok());
    assert(has_diagnostic(
        rejected_module_strength, "FSIM-ELAB-BIND-065"));

    auto malformed_table_state = selected.design->state();
    malformed_table_state.udp_tables.front().rows.front().inputs.clear();
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(malformed_table_state)));
    auto malformed_digest_state = selected.design->state();
    malformed_digest_state.udp_tables.front().digest = "not-a-digest";
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(malformed_digest_state)));
    auto malformed_provenance_state = selected.design->state();
    auto& udp_specialization =
        malformed_provenance_state.specializations.back();
    const auto digest_parameter = std::ranges::find(
        udp_specialization.parameter_identity_values,
        "__udp_table", &std::pair<std::string, std::string>::first);
    assert(digest_parameter
           != udp_specialization.parameter_identity_values.end());
    digest_parameter->second.assign(64, '0');
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(malformed_provenance_state)));
    auto malformed_strength_state = selected.design->state();
    malformed_strength_state.processes.front().drive_strength.zero =
        static_cast<fsim::runtime::simir::StrengthRank>(255);
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(malformed_strength_state)));
    auto malformed_switch_state = selected.design->state();
    malformed_switch_state.processes.front().switch_source =
        static_cast<fsim::runtime::simir::SignalId>(
            malformed_switch_state.signals.size());
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(malformed_switch_state)));
    auto incomplete_switch_state = selected.design->state();
    incomplete_switch_state.processes.front().switch_bidirectional = true;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(incomplete_switch_state)));
}

} // namespace fsim::tests::elaboration
