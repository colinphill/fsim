// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace fsim::tests::elaboration {

namespace {

[[nodiscard]] fsim::frontend::ParseResult parse_bridge_child() {
  return fsim::frontend::parse_text(
      "container-bridge-child.sv",
      R"(
module fixed_container_bridge(
    input logic [7:0] source [3:0],
    output logic [7:0] result [3:0]);
  initial begin
    #1;
    result[3] = source[0];
    result[2] = source[1];
    result[1] = source[2];
    result[0] = source[3];
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
}

[[nodiscard]] fsim::frontend::ParseResult parse_bridge_parent(
    const bool mismatched_shape) {
  const auto source = mismatched_shape
      ? R"(
entity container_bridge_top is
end entity;

architecture rtl of container_bridge_top is
  type words_t is array (1 downto 0)
    of std_logic_vector(15 downto 0);
  signal source : words_t;
  signal result : words_t;
  component fixed_container_bridge is
    port (
      source : in words_t;
      result : out words_t);
  end component;
begin
  child: fixed_container_bridge
    port map (source => source, result => result);
end architecture;
)"
      : R"(
entity container_bridge_top is
end entity;

architecture rtl of container_bridge_top is
  type bytes_t is array (3 downto 0)
    of std_logic_vector(7 downto 0);
  signal source : bytes_t;
  signal result : bytes_t;
  component fixed_container_bridge is
    port (
      source : in bytes_t;
      result : out bytes_t);
  end component;
begin
  drive: process
  begin
    source(3) <= "10100101";
    source(2) <= "00111100";
    source(1) <= "01111110";
    source(0) <= "00000001";
    wait;
  end process;
  child: fixed_container_bridge
    port map (source => source, result => result);
end architecture;
)";
  return fsim::frontend::parse_text(
      mismatched_shape
          ? "container-bridge-shape-mismatch.vhd"
          : "container-bridge-parent.vhd",
      source,
      fsim::frontend::Language::Vhdl2008);
}

[[nodiscard]] fsim::elaboration::ElaborationResult elaborate_bridge(
    const bool mismatched_shape) {
  auto parent = parse_bridge_parent(mismatched_shape);
  auto child = parse_bridge_child();
  if (!parent.ok()) {
    for (const auto& diagnostic : parent.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  if (!child.ok()) {
    for (const auto& diagnostic : child.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parent.ok() && child.ok());
  parent.design.units.insert(
      parent.design.units.end(),
      child.design.units.begin(),
      child.design.units.end());
  const std::vector<fsim::elaboration::Binding> bindings{
      {"container_bridge_top.child",
       "sv:work.fixed_container_bridge",
       std::nullopt}};
  return fsim::elaboration::elaborate(
      parent.design,
      "vhdl:work.container_bridge_top(rtl)",
      bindings);
}

}  // namespace

void test_systemverilog_cross_language_container_bridge() {
  const auto elaborated = elaborate_bridge(false);
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto result = elaborated.design->find_signal("result");
  assert(result);
  auto interpreter = elaborated.design->create_interpreter();
  assert(
      interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*result).to_msb_string()
      == "00000001011111100011110010100101");

  const auto rejected = elaborate_bridge(true);
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-004"));
}

}  // namespace fsim::tests::elaboration
