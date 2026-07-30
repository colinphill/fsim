// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <variant>

namespace fsim::tests::elaboration {

void test_systemverilog_container_lowering() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "container-lowering.sv",
      R"(
module container_lowering #(
    parameter int STATIC_LEFT = 3);
  typedef logic signed [31:0] key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  logic [7:0] fixed_down[STATIC_LEFT:1];
  bit [3:0] fixed_up[-1:1];

  function automatic int count(input byte source[$:2]);
    return source.size();
  endfunction

  function automatic int isolated_count(input byte source[key_t]);
    byte copy[key_t];
    copy = source;
    copy.delete(3);
    return copy.size();
  endfunction

  task automatic mutate(inout byte target[$:2]);
    target.push_back(4);
    #1;
    target.pop_front();
  endtask

  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask

  function automatic byte copied_static(
      input logic [7:0] source[STATIC_LEFT:1]);
    logic [7:0] copy[STATIC_LEFT:1];
    copy = source;
    copy[2] = 8'h99;
    return copy[3];
  endfunction

  task automatic mutate_static(
      inout logic [7:0] target[STATIC_LEFT:1]);
    target[2] = 8'h22;
    #1;
    target[1] = 8'h11;
  endtask

  initial begin
    key_t key;
    values = new[2];
    values[0] = 7;
    pending.push_back(1);
    pending.push_back(2);
    lookup[3] = 30;
    lookup[-1] = 10;
    assert ($isunknown(fixed_down[2]));
    fixed_down[3] = 8'h33;
    fixed_up[-1] = 4'ha;
    fixed_up[1] = 4'hc;
    assert (copied_static(fixed_down) == 8'h33);
    assert ($isunknown(fixed_down[2]));
    assert (fixed_up[-1] == 4'ha);
    assert (fixed_up[0] == 0);
    assert (lookup.size() == 2);
    assert (isolated_count(lookup) == 1);
    assert (lookup.size() == 2);
    assert (lookup.exists(3) == 1);
    assert (lookup[4] == 0);
    assert (lookup.first(key) == 1);
    assert (key == -1);
    assert (lookup.next(key) == 1);
    assert (key == 3);
    mutate(pending);
    mutate_lookup(lookup);
    mutate_static(fixed_down);
    assert (values[0] == 7);
    assert (count(pending) == 2);
    assert (fixed_down[3] == 8'h33);
    assert (fixed_down[2] == 8'h22);
    assert (fixed_down[1] == 8'h11);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "container_lowering");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 5);
  const auto& process = elaborated.design->processes().front();
  assert(process.container_register_count != 0);
  assert(!process.debug_container_locals.empty());
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ResizeContainer>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ContainerExists>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<TraverseContainer>(operation);
      }));
  const auto values =
      elaborated.design->container_objects()[0].id;
  const auto pending =
      elaborated.design->container_objects()[1].id;
  const auto lookup =
      elaborated.design->container_objects()[2].id;
  const auto fixed_down =
      elaborated.design->container_objects()[3].id;
  const auto fixed_up =
      elaborated.design->container_objects()[4].id;
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 3);
  const auto& values_result =
      interpreter->container_object_value(values);
  const auto& pending_result =
      interpreter->container_object_value(pending);
  const auto& lookup_result =
      interpreter->container_object_value(lookup);
  const auto& fixed_down_result =
      interpreter->container_object_value(fixed_down);
  const auto& fixed_up_result =
      interpreter->container_object_value(fixed_up);
  assert(
      values_result.elements.size() == 2
      && values_result.elements[0].low_word().aval == 7
      && pending_result.elements.size() == 2
      && pending_result.elements[0].low_word().aval == 2
      && pending_result.elements[1].low_word().aval == 4
      && lookup_result.keys.size() == 1
      && lookup_result.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && lookup_result.elements[0].low_word().aval == 9
      && fixed_down_result.type.fixed
      && fixed_down_result.type.index_left == 3
      && fixed_down_result.type.index_right == 1
      && fixed_down_result.elements.size() == 3
      && fixed_down_result.elements[0].low_word().aval == 0x33
      && fixed_down_result.elements[1].low_word().aval == 0x22
      && fixed_down_result.elements[2].low_word().aval == 0x11
      && fixed_up_result.type.fixed
      && fixed_up_result.type.index_left == -1
      && fixed_up_result.type.index_right == 1
      && fixed_up_result.elements[0].low_word().aval == 0xa
      && fixed_up_result.elements[1].low_word().aval == 0
      && fixed_up_result.elements[2].low_word().aval == 0xc);

  const auto invalid = fsim::frontend::parse_text(
      "container-invalid-lowering.sv",
      R"(
module container_invalid_lowering;
  typedef struct packed {
    logic [3:0] value;
  } pair_t;
  byte composite_key[pair_t];
  pair_t composite_element[int];
  byte lookup[int];
  byte dynamic[];
  byte fixed[1:0];
  byte too_large[0:4096];
  int runtime_bound;
  byte nonconstant[runtime_bound:0];
  int result;
  initial begin
    lookup.push_back(1);
    result = lookup.sort();
    lookup[0] <= 1;
    dynamic.delete(0);
    fixed.delete();
    fixed = new[2];
    $readmemh("invalid.hex", dynamic);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "container_invalid_lowering");
  assert(!rejected.ok());
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-008"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-013"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-018"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-019"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-009"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-020"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-021"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-014"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVMEMORY-003"));
}

}  // namespace fsim::tests::elaboration
