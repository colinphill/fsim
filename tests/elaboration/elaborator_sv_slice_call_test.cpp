// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <variant>

namespace fsim::tests::elaboration {

void test_systemverilog_static_slice_calls() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "static-slice-calls.sv",
      R"(
module static_slice_calls;
  logic [7:0] source[3:0];
  logic [7:0] produced[3:0];
  logic [7:0] changed[3:0];
  logic [7:0] result;

  function automatic logic [7:0] inspect(
      input logic [7:0] value[6:5]);
    assert ($left(value) == 6);
    assert (value[6] == 8'h20);
    assert (value[5] == 8'h30);
    return value.sum();
  endfunction

  function automatic logic [7:0] nested(
      input logic [7:0] value[3:0]);
    return inspect(value[2:1]);
  endfunction

  task automatic transfer(
      input logic [7:0] incoming[9:8],
      output logic [7:0] outgoing[1:0],
      inout logic [7:0] working[5:4],
      input bit early);
    assert ($isunknown(outgoing[1]));
    assert ($isunknown(outgoing[0]));
    outgoing[1] = incoming[9];
    working[5] = working[5] + 8'h10;
    if (early)
      return;
    #1;
    outgoing[0] = incoming[8];
    working[4] = working[4] + 8'h20;
  endtask

  initial begin
    source = '{8'h10, 8'h20, 8'h30, 8'h40};
    produced = '{default: 8'h00};
    changed = '{8'h01, 8'h02, 8'h03, 8'h04};
    result = inspect(source[1 +: 2]);
    assert (result == 8'h50);
    result = nested(source);
    assert (result == 8'h50);
    transfer(
        source[2 +: 2],
        produced[2 +: 2],
        changed[2 +: 2],
        1);
    assert (produced[3] == 8'h10);
    assert ($isunknown(produced[2]));
    assert (changed[3] == 8'h11);
    assert (changed[2] == 8'h02);
    transfer(
        source[1 -: 2],
        produced[1 -: 2],
        changed[1 -: 2],
        0);
    assert (produced[1] == 8'h30);
    assert (produced[0] == 8'h40);
    assert (changed[1] == 8'h13);
    assert (changed[0] == 8'h24);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "static_slice_calls");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 3);
  const auto& process = elaborated.design->processes().front();
  const auto has_type =
      [&](const std::int32_t left,
          const std::int32_t right) {
        return std::ranges::any_of(
            process.container_register_types,
            [&](const auto& type) {
              return type.fixed
                  && type.element_width == 8
                  && !type.two_state
                  && !type.signed_elements
                  && type.index_left == left
                  && type.index_right == right;
            });
      };
  assert(
      has_type(2, 1)
      && has_type(6, 5)
      && has_type(9, 8)
      && has_type(1, 0)
      && has_type(5, 4));
  assert(
      std::ranges::count_if(
          process.operations,
          [](const auto& operation) {
            return std::holds_alternative<
                CopyContainerRegister>(operation);
          })
      >= 12);
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 1);

  const auto invalid = fsim::frontend::parse_text(
      "static-slice-call-invalid.sv",
      R"(
module static_slice_call_invalid(
    input logic [7:0] read_only[3:0]);
  logic [7:0] down[3:0];
  logic [3:0] narrow[3:0];
  bit [7:0] bits[3:0];
  logic signed [7:0] signed_values[3:0];
  logic [7:0] dynamic[];
  int runtime_bound;
  logic [7:0] result;

  function automatic logic [7:0] accept(
      input logic [7:0] value[1:0]);
    return value.sum();
  endfunction

  task automatic produce(
      output logic [7:0] value[1:0]);
    value = '{8'h01, 8'h02};
  endtask

  task automatic edit(
      inout logic [7:0] value[1:0]);
    value[1] = 8'h03;
  endtask

  initial begin
    result = accept(down[runtime_bound:0]);
    result = accept(down[32'hxxxxxxxx:0]);
    result = accept(down[0:1]);
    result = accept(down[3 +: 2]);
    result = accept(down[1 +: 0]);
    result = accept(down[3:1]);
    result = accept(narrow[1:0]);
    result = accept(bits[1:0]);
    result = accept(signed_values[1:0]);
    result = accept(dynamic[1:0]);
    result = accept(down[3:2][1:0]);
    produce(read_only[0 +: 2]);
    produce(down[3:1]);
    edit(read_only[1 -: 2]);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "static_slice_call_invalid");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-003"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-005"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-006"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-009"));
}

}  // namespace fsim::tests::elaboration
