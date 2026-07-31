// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <variant>

namespace fsim::tests::elaboration {

void test_systemverilog_static_slice_ordering() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "static-slice-ordering.sv",
      R"(
module static_slice_ordering;
  logic [7:0] reversed[5:0];
  logic [7:0] ascending[5:0];
  logic [7:0] descending[0:5];
  logic [7:0] keyed[5:0];
  logic [7:0] named[5:0];
  logic [7:0] indexed[5:0];
  logic [3:0] four_state[5:0];

  task automatic suspended_order(
      inout logic [7:0] target[5:0]);
    #1;
    target[4:1].sort();
  endtask

  initial begin
    reversed = '{
        8'hf5, 8'h50, 8'h40, 8'h30, 8'h20, 8'hf0};
    reversed[4:1].reverse();
    assert (reversed[5] == 8'hf5);
    assert (reversed[4] == 8'h20);
    assert (reversed[1] == 8'h50);
    assert (reversed[0] == 8'hf0);

    ascending = '{
        8'hf5, 8'h40, 8'h10, 8'h30, 8'h20, 8'hf0};
    ascending[4:1].sort();
    assert (ascending[5] == 8'hf5);
    assert (ascending[4] == 8'h10);
    assert (ascending[3] == 8'h20);
    assert (ascending[2] == 8'h30);
    assert (ascending[1] == 8'h40);
    assert (ascending[0] == 8'hf0);

    descending = '{
        8'hf0, 8'h10, 8'h40, 8'h20, 8'h30, 8'hf5};
    descending[1:4].rsort();
    assert (descending[0] == 8'hf0);
    assert (descending[1] == 8'h40);
    assert (descending[2] == 8'h30);
    assert (descending[3] == 8'h20);
    assert (descending[4] == 8'h10);
    assert (descending[5] == 8'hf5);

    keyed = '{
        8'hf5, 8'h40, 8'h30, 8'h20, 8'h10, 8'hf0};
    keyed[4:1].sort() with (
        item.index < 3 ? 8'h00 : 8'h01);
    assert (keyed[5] == 8'hf5);
    assert (keyed[4] == 8'h20);
    assert (keyed[3] == 8'h10);
    assert (keyed[2] == 8'h40);
    assert (keyed[1] == 8'h30);
    assert (keyed[0] == 8'hf0);

    named = '{
        8'hf5, 8'h10, 8'h20, 8'h30, 8'h40, 8'hf0};
    named[4:1].rsort(entry) with (
        entry.index < 3 ? 8'h01 : 8'h00);
    assert (named[5] == 8'hf5);
    assert (named[4] == 8'h30);
    assert (named[3] == 8'h40);
    assert (named[2] == 8'h10);
    assert (named[1] == 8'h20);
    assert (named[0] == 8'hf0);

    indexed = '{
        8'hf5, 8'h40, 8'h10, 8'h30, 8'h20, 8'hf0};
    indexed[1 +: 4].sort();
    assert (indexed[5] == 8'hf5);
    assert (indexed[4] == 8'h10);
    assert (indexed[1] == 8'h40);
    assert (indexed[0] == 8'hf0);
    indexed[4 -: 4].reverse();
    assert (indexed[4] == 8'h40);
    assert (indexed[1] == 8'h10);

    four_state = '{
        4'h5, 4'b0x00, 4'b0001, 4'b0z00, 4'b0000, 4'ha};
    four_state[4:1].sort();
    assert (four_state[5] === 4'h5);
    assert (four_state[4] === 4'b0000);
    assert (four_state[3] === 4'b0001);
    assert (four_state[2] === 4'b0x00);
    assert (four_state[1] === 4'b0z00);
    assert (four_state[0] === 4'ha);

    ascending = '{
        8'hee, 8'h04, 8'h01, 8'h03, 8'h02, 8'hdd};
    suspended_order(ascending);
    assert (ascending[5] == 8'hee);
    assert (ascending[4] == 8'h01);
    assert (ascending[1] == 8'h04);
    assert (ascending[0] == 8'hdd);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "static_slice_ordering");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 7);
  const auto& process = elaborated.design->processes().front();
  std::size_t ordering_count = 0;
  std::size_t keyed_count = 0;
  for (std::size_t operation_index = 0;
       operation_index < process.operations.size();
       ++operation_index) {
    const auto* ordering =
        std::get_if<OrderContainer>(
            &process.operations[operation_index]);
    if (ordering == nullptr) {
      continue;
    }
    ++ordering_count;
    const auto& type =
        process.container_register_types[ordering->target];
    assert(
        type.fixed
        && ((type.index_left == 4 && type.index_right == 1)
            || (type.index_left == 1 && type.index_right == 4)));
    const auto* initialize_replacement =
        std::get_if<CopyContainerRegister>(
            &process.operations[operation_index + 1]);
    const auto* commit_replacement =
        std::get_if<CopyContainerRegister>(
            &process.operations[operation_index + 18]);
    assert(
        initialize_replacement != nullptr
        && commit_replacement != nullptr
        && initialize_replacement->source
            == commit_replacement->destination
        && initialize_replacement->destination
            == commit_replacement->source);
    if (!ordering->key.empty()) {
      ++keyed_count;
      assert(std::ranges::any_of(
          ordering->key,
          [](const auto& node) {
            return node.operation
                == ContainerPredicateOperator::index;
          }));
    }
  }
  assert(ordering_count == 9);
  assert(keyed_count == 2);
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 1);

  const auto invalid = fsim::frontend::parse_text(
      "static-slice-ordering-invalid.sv",
      R"(
module static_slice_ordering_invalid(
    input logic [7:0] read_only[5:0]);
  logic [7:0] down[5:0];
  logic [7:0] dynamic[];
  int runtime_bound;
  int collision;
  initial begin
    read_only[1 +: 4].reverse();
    down[runtime_bound:1].sort();
    down[32'hxxxxxxxx:1].sort();
    down[1:4].rsort();
    down[6:5].reverse();
    down[5 +: 2].sort();
    down[2 +: 0].sort();
    dynamic[1:0].sort();
    down[4:3][1:0].sort();
    down[4:1].sort() with (item + 1);
    down[4:1].rsort(collision) with (collision);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "static_slice_ordering_invalid");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-003"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVORDER-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVORDER-006"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVORDER-008"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-009"));
}

}  // namespace fsim::tests::elaboration
