// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_function_lowering() {
  const auto parsed = fsim::frontend::parse_text(
      "function_runtime.sv",
      R"(
module function_runtime #(
    parameter int WIDTH = 8
) (
    input logic select,
    input logic [WIDTH-1:0] value,
    output logic [WIDTH-1:0] result
);
  function automatic logic [WIDTH-1:0] increment(
      input logic [WIDTH-1:0] argument);
    return argument + 1;
  endfunction

  function automatic logic [WIDTH-1:0] choose(
      input logic condition,
      input logic [WIDTH-1:0] argument);
    logic [WIDTH-1:0] temporary;
    if (condition)
      temporary = increment(argument);
    else
      temporary = argument;
    choose = temporary;
  endfunction

  initial result = choose(select, value);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "sv:work.function_runtime");
  assert(elaborated.ok());
  const auto select =
      elaborated.design->find_signal("select");
  const auto value =
      elaborated.design->find_signal("value");
  const auto result =
      elaborated.design->find_signal("result");
  assert(select && value && result);
  const auto& operations =
      elaborated.design->processes().front().operations;
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        return std::holds_alternative<
            fsim::runtime::simir::Call>(operation);
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        return std::holds_alternative<
            fsim::runtime::simir::Return>(operation);
      }));

  auto interpreter =
      elaborated.design->create_interpreter();
  interpreter->deposit_signal(
      *select,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  interpreter->deposit_signal(
      *value,
      fsim::runtime::PackedLogic4::from_msb_string("00101001"));
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*result).to_msb_string()
      == "00101010");

  const auto recursive = fsim::frontend::parse_text(
      "recursive_function.sv",
      R"(
module recursive_function(
    input logic value,
    output logic result);
  function automatic logic recurse(input logic argument);
    recurse = argument ? recurse(argument) : 1'b0;
  endfunction
  initial result = recurse(value);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(recursive.ok());
  const auto rejected = fsim::elaboration::elaborate(
      recursive.design, "sv:work.recursive_function");
  assert(
      !rejected.ok()
      && has_diagnostic(rejected, "FSIM-ELAB-SVFUNC-006"));

  const auto wrong_arity = fsim::frontend::parse_text(
      "function_arity.sv",
      R"(
module function_arity(output logic result);
  function automatic logic identity(input logic argument);
    identity = argument;
  endfunction
  initial result = identity();
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(wrong_arity.ok());
  const auto rejected_arity = fsim::elaboration::elaborate(
      wrong_arity.design, "sv:work.function_arity");
  assert(
      !rejected_arity.ok()
      && has_diagnostic(
          rejected_arity, "FSIM-ELAB-SVFUNC-003"));

  const auto packages = fsim::frontend::parse_text(
      "package_functions.sv",
      R"(
package math_pkg;
  function automatic logic [7:0] add_two(
      input logic [7:0] argument);
    return argument + 2;
  endfunction
endpackage

module imported_function(output logic [7:0] result);
  import math_pkg::*;
  initial result = add_two(8'd40);
endmodule

module qualified_function(output logic [7:0] result);
  initial result = math_pkg::add_two(8'd40);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(packages.ok());
  for (const auto top :
       {"sv:work.imported_function",
        "sv:work.qualified_function"}) {
    const auto package_elaborated =
        fsim::elaboration::elaborate(packages.design, top);
    assert(package_elaborated.ok());
    const auto package_result =
        package_elaborated.design->find_signal("result");
    assert(package_result);
    auto package_interpreter =
        package_elaborated.design->create_interpreter();
    assert(
        package_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        package_interpreter
            ->signal_value(*package_result)
            .to_msb_string()
        == "00101010");
  }

  const auto constant = fsim::frontend::parse_text(
      "constant_function.sv",
      R"(
module constant_function(output logic [3:0] result);
  function automatic int width_for(input int argument);
    if (argument > 3)
      return 4;
    return 2;
  endfunction
  localparam int WIDTH = width_for(5);
  logic [WIDTH-1:0] internal;
  if (width_for(5) == 4) begin : selected
    initial begin
      internal = 4'b1010;
      result = internal;
    end
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(constant.ok());
  const auto constant_elaborated =
      fsim::elaboration::elaborate(
          constant.design, "sv:work.constant_function");
  assert(constant_elaborated.ok());
  const auto internal =
      constant_elaborated.design->find_signal("internal");
  const auto constant_result =
      constant_elaborated.design->find_signal("result");
  assert(internal && constant_result);
  assert(
      constant_elaborated.design->signals().at(*internal).width
      == 4);
  auto constant_interpreter =
      constant_elaborated.design->create_interpreter();
  assert(
      constant_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  assert(
      constant_interpreter
          ->signal_value(*constant_result)
          .to_msb_string()
      == "1010");

  const auto fixed_returns = fsim::frontend::parse_text(
      "fixed_function_returns.sv",
      R"(
module fixed_function_returns;
  logic [7:0] source[7:2];
  logic [7:0] whole[20:17];
  logic [7:0] colon_whole[-4:-1];
  logic [7:0] nested[-1:2];
  logic [7:0] selected[5:0];
  logic [7:0] partial_first[3:0];
  logic [7:0] partial_second[3:0];

  function automatic logic [7:0] from_slice[10:7](
      input logic [7:0] value[7:2]);
    return value[6 -: 4];
  endfunction

  function automatic logic [7:0] named_result[3:0](
      input logic [7:0] seed);
    named_result[3] = seed;
    named_result[2] = seed + 1;
    named_result[1] = seed + 2;
    named_result[0] = seed + 3;
  endfunction

  function automatic logic [7:0] colon_result[-1:2](
      input logic [7:0] value[7:2]);
    return value[6:3];
  endfunction

  function automatic logic [7:0] relay[-1:2](
      input logic [7:0] value[3:0]);
    return value;
  endfunction

  function automatic logic [7:0] partial[3:0](
      input logic complete);
    partial[3] = 8'h91;
    if (complete) begin
      partial[2] = 8'h82;
      partial[1] = 8'h73;
      partial[0] = 8'h64;
    end
  endfunction

  initial begin
    source = '{
        8'h70, 8'h60, 8'b10xz0011,
        8'h40, 8'h30, 8'h20};
    selected = '{default: 8'hee};
    whole = from_slice(source);
    colon_whole = colon_result(source);
    selected[4 -: 4] = named_result(8'h10);
    nested = relay(named_result(8'h20));
    partial_first = partial(1'b1);
    partial_second = partial(1'b0);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(fixed_returns.ok());
  const auto fixed_elaborated = fsim::elaboration::elaborate(
      fixed_returns.design,
      "sv:work.fixed_function_returns");
  if (!fixed_elaborated.ok()) {
    for (const auto& diagnostic : fixed_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(fixed_elaborated.ok());
  const auto& fixed_process =
      fixed_elaborated.design->processes().front();
  assert(std::ranges::count_if(
             fixed_process.operations,
             [](const auto& operation) {
               return std::holds_alternative<
                   fsim::runtime::simir::Call>(operation);
             })
         == 7);
  assert(std::ranges::any_of(
      fixed_process.debug_container_locals,
      [](const auto& local) {
        return local.name == "from_slice.from_slice"
            && local.type.fixed
            && local.type.index_left == 10
            && local.type.index_right == 7;
      }));
  auto fixed_interpreter =
      fixed_elaborated.design->create_interpreter();
  assert(
      fixed_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto object_value =
      [&](const std::string_view name) -> const auto& {
        const auto id = fixed_elaborated.design->find_container(name);
        assert(id);
        return fixed_interpreter->container_object_value(*id);
      };
  const auto bytes =
      [](const auto& container_value) {
        std::vector<std::uint64_t> octets;
        for (const auto& element : container_value.elements) {
          octets.push_back(element.low_word().aval);
        }
        return octets;
      };
  assert(bytes(object_value("whole"))
         == std::vector<std::uint64_t>({0x60, 0xa3, 0x40, 0x30}));
  assert(bytes(object_value("colon_whole"))
         == std::vector<std::uint64_t>({0x60, 0xa3, 0x40, 0x30}));
  assert(
      object_value("whole").elements[1].low_word().bval == 0x30
      && object_value("colon_whole")
             .elements[1].low_word().bval == 0x30);
  assert(bytes(object_value("selected"))
         == std::vector<std::uint64_t>(
             {0xee, 0x10, 0x11, 0x12, 0x13, 0xee}));
  assert(bytes(object_value("nested"))
         == std::vector<std::uint64_t>({0x20, 0x21, 0x22, 0x23}));
  assert(bytes(object_value("partial_first"))
         == std::vector<std::uint64_t>({0x91, 0x82, 0x73, 0x64}));
  const auto& reset = object_value("partial_second");
  assert(
      reset.elements[0].low_word().aval == 0x91
      && reset.elements[0].low_word().bval == 0
      && reset.elements[1].low_word().bval == 0xff
      && reset.elements[2].low_word().bval == 0xff
      && reset.elements[3].low_word().bval == 0xff);

  const auto reject_fixed_return =
      [](const std::string_view path,
         const std::string_view source,
         const std::string_view top,
         const std::string_view code) {
        const auto candidate = fsim::frontend::parse_text(
            path,
            source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(candidate.ok());
        const auto rejected_result = fsim::elaboration::elaborate(
            candidate.design, top);
        if (rejected_result.ok()
            || !has_diagnostic(rejected_result, code)) {
          std::cerr << "unexpected fixed-return diagnostic set for "
                    << path << " (expected " << code << ")\n";
          for (const auto& diagnostic : rejected_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
          }
        }
        assert(
            !rejected_result.ok()
            && has_diagnostic(rejected_result, code));
      };
  reject_fixed_return(
      "dynamic_function_return.sv",
      R"(
module dynamic_function_return;
  byte result[1:0];
  function automatic byte dynamic_result[]();
    dynamic_result = dynamic_result;
  endfunction
  initial result = dynamic_result();
endmodule
)",
      "sv:work.dynamic_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "queue_function_return.sv",
      R"(
module queue_function_return;
  byte result[1:0];
  function automatic byte queue_result[$:2]();
    queue_result = queue_result;
  endfunction
  initial result = queue_result();
endmodule
)",
      "sv:work.queue_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "associative_function_return.sv",
      R"(
module associative_function_return;
  byte result[1:0];
  function automatic byte associative_result[int]();
    associative_result = associative_result;
  endfunction
  initial result = associative_result();
endmodule
)",
      "sv:work.associative_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "runtime_bound_function_return.sv",
      R"(
module runtime_bound_function_return(input int limit);
  byte result[1:0];
  function automatic byte runtime_bound[limit:0]();
    runtime_bound = runtime_bound;
  endfunction
  initial result = runtime_bound();
endmodule
)",
      "sv:work.runtime_bound_function_return",
      "FSIM-ELAB-SVCONTAINER-020");
  reject_fixed_return(
      "runtime_slice_function_return.sv",
      R"(
module runtime_slice_function_return;
  byte source[3:0];
  byte result[1:0];
  function automatic byte runtime_slice[1:0](input int base);
    return source[base +: 2];
  endfunction
  initial result = runtime_slice(1);
endmodule
)",
      "sv:work.runtime_slice_function_return",
      "FSIM-ELAB-SVSLICE-002");
  reject_fixed_return(
      "count_mismatch_function_return.sv",
      R"(
module count_mismatch_function_return;
  byte source[1:0];
  byte result[2:0];
  function automatic byte wrong_count[2:0]();
    return source;
  endfunction
  initial result = wrong_count();
endmodule
)",
      "sv:work.count_mismatch_function_return",
      "FSIM-ELAB-SVSLICE-005");
  reject_fixed_return(
      "profile_mismatch_function_return.sv",
      R"(
module profile_mismatch_function_return;
  logic [7:0] source[1:0];
  bit [7:0] result[1:0];
  function automatic bit [7:0] wrong_profile[1:0]();
    return source;
  endfunction
  initial result = wrong_profile();
endmodule
)",
      "sv:work.profile_mismatch_function_return",
      "FSIM-ELAB-SVSLICE-006");
  reject_fixed_return(
      "recursive_fixed_function_return.sv",
      R"(
module recursive_fixed_function_return;
  byte source[1:0];
  byte result[1:0];
  function automatic byte recurse[1:0](input byte value[1:0]);
    return recurse(value);
  endfunction
  initial result = recurse(source);
endmodule
)",
      "sv:work.recursive_fixed_function_return",
      "FSIM-ELAB-SVFUNC-006");
}

} // namespace fsim::tests::elaboration
