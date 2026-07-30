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
module container_lowering;
  typedef logic signed [31:0] key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];

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

  initial begin
    key_t key;
    values = new[2];
    values[0] = 7;
    pending.push_back(1);
    pending.push_back(2);
    lookup[3] = 30;
    lookup[-1] = 10;
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
    assert (values[0] == 7);
    assert (count(pending) == 2);
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
  assert(elaborated.design->container_objects().size() == 3);
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
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 2);
  const auto& values_result =
      interpreter->container_object_value(values);
  const auto& pending_result =
      interpreter->container_object_value(pending);
  const auto& lookup_result =
      interpreter->container_object_value(lookup);
  assert(
      values_result.elements.size() == 2
      && values_result.elements[0].low_word().aval == 7
      && pending_result.elements.size() == 2
      && pending_result.elements[0].low_word().aval == 2
      && pending_result.elements[1].low_word().aval == 4
      && lookup_result.keys.size() == 1
      && lookup_result.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && lookup_result.elements[0].low_word().aval == 9);

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
  int result;
  initial begin
    lookup.push_back(1);
    result = lookup.sort();
    lookup[0] <= 1;
    dynamic.delete(0);
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
}

}  // namespace fsim::tests::elaboration
