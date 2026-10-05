// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <type_traits>
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
  return compile_and_elaborate(
      parent.design,
      "vhdl:work.container_bridge_top(rtl)",
      bindings);
}

void test_projected_array_writer_retains_aggregate_fallback() {
  auto parent = fsim::frontend::parse_text(
      "projected-array-writer-top.sv",
      R"(
module projected_array_writer_top;
  timeunit 1ns / 1ps;
  wire [7:0] words[1:0];
  logic [7:0] first_sample;
  logic [7:0] second_sample;
  wire [7:0] ordinary_words[1:0];
  logic [7:0] ordinary_sample;

  projected_array_writer child (.data(words));

  assign ordinary_words[1] = 8'hA5;

  initial begin
    #2;
    first_sample = words[0];
    ordinary_sample = ordinary_words[1];
    #2;
    second_sample = words[0];
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  auto child = fsim::frontend::parse_text(
      "projected-array-writer-child.vhd",
      R"(
entity projected_array_writer is
  port (data : out std_logic_vector(15 downto 0));
end entity;

architecture rtl of projected_array_writer is
begin
  writer : process
  begin
    data <= transport X"0201" after 1 ns,
            X"0403" after 3 ns;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parent.ok() && child.ok());
  append_component_design(parent.design, std::move(child.design));
  const std::vector<fsim::elaboration::Binding> bindings {
      { "projected_array_writer_top.child",
          "vhdl:work.projected_array_writer(rtl)",
          std::nullopt }
  };
  const auto elaborated = compile_and_elaborate(
      std::move(parent.design),
      "sv:work.projected_array_writer_top",
      bindings);
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());

  const auto object = elaborated.design->find_container(
      "projected_array_writer_top.words");
  const auto proxy = elaborated.design->find_signal(
      "projected_array_writer_top.words");
  const auto first_sample = elaborated.design->find_signal(
      "projected_array_writer_top.first_sample");
  const auto second_sample = elaborated.design->find_signal(
      "projected_array_writer_top.second_sample");
  const auto ordinary_object = elaborated.design->find_container(
      "projected_array_writer_top.ordinary_words");
  const auto ordinary_proxy = elaborated.design->find_signal(
      "projected_array_writer_top.ordinary_words");
  const auto ordinary_sample = elaborated.design->find_signal(
      "projected_array_writer_top.ordinary_sample");
  assert(object && proxy && first_sample && second_sample
      && ordinary_object && ordinary_proxy && ordinary_sample);
  assert(std::ranges::none_of(
      elaborated.design->container_element_signal_aliases(),
      [&](const auto& alias) { return alias.object == *object; }));
  assert(std::ranges::none_of(
      elaborated.design->container_aggregate_signal_aliases(),
      [&](const auto& alias) { return alias.object == *object; }));
  assert(std::ranges::any_of(
      elaborated.design->state().container_signal_aliases,
      [&](const auto& alias) {
        return alias.object == *object && alias.signal == *proxy;
      }));

  bool projected_proxy_write { };
  for (const auto& process : elaborated.design->processes()) {
    for (std::size_t index = 0U;
        index < process.operations.size();
        ++index) {
      const auto operation = process.operations.expanded(index);
      fsim::runtime::simir::visit_operation(
          [&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<
                    Type, fsim::runtime::simir::WriteProjected>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteProjectedWaveform>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteProjectedSlice>
                || std::is_same_v<
                    Type,
                    fsim::runtime::simir::WriteProjectedWaveformSlice>
                || std::is_same_v<
                    Type,
                    fsim::runtime::simir::WriteProjectedDynamicSlice>
                || std::is_same_v<
                    Type,
                    fsim::runtime::simir::WriteProjectedWaveformDynamicSlice>) {
              projected_proxy_write
                  = projected_proxy_write || value.signal == *proxy;
            }
          },
          operation);
    }
  }
  assert(projected_proxy_write);

  std::vector<fsim::runtime::simir::SignalId> ordinary_leaves;
  for (const auto& alias
      : elaborated.design->container_element_signal_aliases()) {
    if (alias.object == *ordinary_object) {
      ordinary_leaves.push_back(alias.signal);
    }
  }
  assert(ordinary_leaves.size() == 2U);
  assert(std::ranges::any_of(
      elaborated.design->container_aggregate_signal_aliases(),
      [&](const auto& alias) {
        return alias.object == *ordinary_object
            && alias.signal == *ordinary_proxy;
      }));
  assert(std::ranges::none_of(
      elaborated.design->state().container_signal_aliases,
      [&](const auto& alias) { return alias.object == *ordinary_object; }));

  std::size_t ordinary_leaf_reads { };
  std::size_t ordinary_leaf_writes { };
  for (const auto& process : elaborated.design->processes()) {
    for (std::size_t index = 0U;
        index < process.operations.size();
        ++index) {
      const auto operation = process.operations.expanded(index);
      fsim::runtime::simir::visit_operation(
          [&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            const auto is_ordinary_leaf = [&](const auto signal) {
              return std::ranges::find(ordinary_leaves, signal)
                  != ordinary_leaves.end();
            };
            if constexpr (std::is_same_v<
                              Type, fsim::runtime::simir::ReadSignal>) {
              if (is_ordinary_leaf(value.signal)) {
                ++ordinary_leaf_reads;
              }
            } else if constexpr (
                std::is_same_v<
                    Type,
                    fsim::runtime::simir::WriteBlocking>) {
              if (is_ordinary_leaf(value.signal)) {
                ++ordinary_leaf_writes;
              }
            } else if constexpr (
                std::is_same_v<Type, fsim::runtime::simir::WriteUpdate>
                || std::is_same_v<Type, fsim::runtime::simir::WriteAfter>
                || std::is_same_v<Type, fsim::runtime::simir::WriteInertial>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteBlockingSlice>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteUpdateSlice>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteAfterSlice>
                || std::is_same_v<
                    Type, fsim::runtime::simir::WriteInertialSlice>) {
              if (is_ordinary_leaf(value.signal)) {
                ++ordinary_leaf_writes;
              }
            }
          },
          operation);
    }
  }
  assert(ordinary_leaf_reads > 0U);
  assert(ordinary_leaf_writes > 0U);

  auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(interpreter->signal_value(*first_sample).to_msb_string()
      == "00000001");
  assert(interpreter->signal_value(*second_sample).to_msb_string()
      == "00000011");
  assert(interpreter->signal_value(*ordinary_sample).to_msb_string()
      == "10100101");
  const auto& final_words = interpreter->container_object_value(*object);
  assert(final_words.elements.size() == 2U);
  assert(final_words.elements[0].to_msb_string() == "00000100");
  assert(final_words.elements[1].to_msb_string() == "00000011");
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

  test_projected_array_writer_retains_aggregate_fallback();

  const auto rejected = elaborate_bridge(true);
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-004"));
}

}  // namespace fsim::tests::elaboration
