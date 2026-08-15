// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/frontend/frontend.hpp"

#include <array>
#include <cassert>
#include <ranges>

namespace fsim::tests::elaboration {

void test_verilog_strength_hierarchy() {
    const auto verilog = fsim::frontend::parse_text(
        "strength-hierarchy.v",
        R"(
module strength_leaf(output wire q);
  pullup source(q);
endmodule
module strength_top(output wire q);
  strength_leaf child(q);
endmodule
module generated_strength #(parameter ENABLE_PULL = 1);
  generate if (ENABLE_PULL) begin: enabled
    wire value;
    pullup generated_pull(value);
  end endgenerate
endmodule
module specialized_strength;
  generated_strength #(.ENABLE_PULL(1)) enabled();
  generated_strength #(.ENABLE_PULL(0)) disabled();
endmodule
module topology_corner;
  wire cycle_a, cycle_b, cycle_c;
  wire disconnected_a, disconnected_b;
  assign cycle_a = 1'b1;
  tran cycle_ab(cycle_a, cycle_b);
  tran cycle_bc(cycle_b, cycle_c);
  tran cycle_ca(cycle_c, cycle_a);
  tran disconnected(disconnected_a, disconnected_b);
endmodule
module selected_topology;
  wire [7:0] source;
  wire [7:0] target;
  assign source = 8'ha3;
  tran selected(source[7:4], target[3:0]);
endmodule
module strength_systemc_wrapper;
  wire value;
  supply1 high;
  rtran link(value, high);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog.ok());

    const auto generated = fsim::elaboration::elaborate(
        verilog.design, "verilog:work.generated_strength");
    assert(generated.ok());
    const auto generated_value = generated.design->find_signal("generated_strength.enabled.value");
    assert(generated_value);
    auto generated_runtime = generated.design->create_interpreter();
    assert(generated_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    assert(generated_runtime->signal_value(*generated_value).to_msb_string()
        == "1");

    const auto specialized = fsim::elaboration::elaborate(
        verilog.design, "verilog:work.specialized_strength");
    assert(specialized.ok());
    const auto& specializations = specialized.design->specializations();
    assert(std::ranges::any_of(specializations, [](const auto& entry) {
        return entry.instance == "specialized_strength.enabled"
            && std::ranges::find(
                   entry.parameter_values,
                   std::pair<std::string, std::string> { "ENABLE_PULL", "1" })
            != entry.parameter_values.end();
    }));
    assert(std::ranges::any_of(specializations, [](const auto& entry) {
        return entry.instance == "specialized_strength.disabled"
            && std::ranges::find(
                   entry.parameter_values,
                   std::pair<std::string, std::string> { "ENABLE_PULL", "0" })
            != entry.parameter_values.end();
    }));

    const auto topology = fsim::elaboration::elaborate(
        verilog.design, "verilog:work.topology_corner");
    assert(topology.ok());
    const auto cycle_c = topology.design->find_signal("topology_corner.cycle_c");
    const auto disconnected_b = topology.design->find_signal("topology_corner.disconnected_b");
    assert(cycle_c && disconnected_b);
    auto topology_runtime = topology.design->create_interpreter();
    assert(topology_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    assert(topology_runtime->signal_value(*cycle_c).to_msb_string() == "1");
    assert(topology_runtime->signal_value(*disconnected_b).to_msb_string()
        == "Z");

    const auto selected = fsim::elaboration::elaborate(
        verilog.design, "verilog:work.selected_topology");
    assert(selected.ok());
    const auto selected_target = selected.design->find_signal("selected_topology.target");
    assert(selected_target);
    assert(std::ranges::any_of(
        selected.design->processes(), [](const auto& process) {
            return process.switch_bidirectional
                && process.switch_source_offset == 4
                && process.switch_target_offset == 0
                && process.switch_width == 4;
        }));
    auto selected_runtime = selected.design->create_interpreter();
    assert(selected_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    assert(selected_runtime->signal_value(*selected_target).to_msb_string()
        == "ZZZZ1010");

    const std::array roots {
        fsim::elaboration::Root { "strength_top", "first" },
        fsim::elaboration::Root { "strength_top", "second" }
    };
    const auto multiple = fsim::elaboration::elaborate(
        verilog.design, roots, { }, { }, nullptr, { });
    assert(multiple.ok());
    assert((multiple.design->roots()
        == std::vector<std::string> { "first", "second" }));
    const auto first_q = multiple.design->find_signal("first.q");
    const auto second_q = multiple.design->find_signal("second.q");
    assert(first_q && second_q);
    auto multiple_runtime = multiple.design->create_interpreter();
    assert(multiple_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    assert(multiple_runtime->signal_value(*first_q).to_msb_string() == "1");
    assert(multiple_runtime->signal_value(*second_q).to_msb_string() == "1");

    auto searched = verilog.design;
    for (auto& unit : searched.units) {
        if (unit.name == "strength_leaf") {
            unit.library = "vendor";
        }
    }
  const std::array<std::string, 1> search_libraries{"vendor"};
  const auto searched_result = fsim::elaboration::elaborate(
      searched,
      "verilog:work.strength_top",
      {},
      {},
      nullptr,
      search_libraries);
  assert(searched_result.ok());
  assert(std::ranges::any_of(
      searched_result.design->specializations(), [](const auto& entry) {
        return entry.instance == "strength_top.child"
            && entry.library == "vendor";
      }));
  const auto searched_q = searched_result.design->find_signal("strength_top.q");
  assert(searched_q);
  auto searched_runtime = searched_result.design->create_interpreter();
  assert(searched_runtime->run().status
         == fsim::runtime::RunStatus::completed);
  assert(searched_runtime->signal_value(*searched_q).to_msb_string() == "1");

  const auto vhdl = fsim::frontend::parse_text(
      "strength-parent.vhd",
      R"(
entity strength_parent is end entity;
architecture rtl of strength_parent is
  component strength_top is port (q : out std_logic); end component;
  signal observed : std_logic;
begin
  wrapped: strength_top port map (q => observed);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(vhdl.ok());
  auto mixed = verilog.design;
  mixed.units.insert(
      mixed.units.end(), vhdl.design.units.begin(), vhdl.design.units.end());
  const auto vhdl_boundary = fsim::elaboration::elaborate(
      mixed, "vhdl:work.strength_parent(rtl)");
  assert(vhdl_boundary.ok());
  assert(std::ranges::any_of(
      vhdl_boundary.design->specializations(), [](const auto& entry) {
        return entry.instance == "strength_parent.wrapped.child";
      }));

}

}  // namespace fsim::tests::elaboration
