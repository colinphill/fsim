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
  logic [7:0] conditional_fixed[3:0];
  logic [7:0] merged_fixed[3:0];
  int fixed_query;
  logic fixed_logical_equal;
  logic fixed_case_equal;
  logic fixed_case_not_equal;

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
    conditional_fixed = 1'b0
        ? named_result(8'h30) : named_result(8'h40);
    merged_fixed = 1'bx
        ? named_result(8'h50) : named_result(8'h58);
    fixed_query = $bits(named_result(8'h60))
        + $dimensions(named_result(8'h60))
        + $unpacked_dimensions(named_result(8'h60))
        + $left(named_result(8'h60))
        + $right(named_result(8'h60))
        + $low(named_result(8'h60))
        + $high(named_result(8'h60))
        + $increment(named_result(8'h60))
        + $size(named_result(8'h60));
    fixed_logical_equal =
        from_slice(source) == from_slice(source);
    fixed_case_equal =
        from_slice(source) === from_slice(source);
    fixed_case_not_equal =
        named_result(8'h20) !== named_result(8'h21);
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
         == 17);
  assert(std::ranges::count_if(
             fixed_process.operations,
             [](const auto& operation) {
               return std::holds_alternative<
                   fsim::runtime::simir::ConditionalContainerSelect>(
                   operation);
             })
         == 2);
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
  assert(bytes(object_value("conditional_fixed"))
         == std::vector<std::uint64_t>({0x40, 0x41, 0x42, 0x43}));
  const auto fixed_query =
      fixed_elaborated.design->find_signal("fixed_query");
  assert(
      fixed_query
      && fixed_interpreter->signal_value(*fixed_query)
             .low_word().aval == 46);
  const auto fixed_logical_equal =
      fixed_elaborated.design->find_signal("fixed_logical_equal");
  const auto fixed_case_equal =
      fixed_elaborated.design->find_signal("fixed_case_equal");
  const auto fixed_case_not_equal =
      fixed_elaborated.design->find_signal("fixed_case_not_equal");
  assert(
      fixed_logical_equal && fixed_case_equal
      && fixed_case_not_equal
      && fixed_interpreter->signal_value(*fixed_logical_equal)
             .low_word().bval == 1
      && fixed_interpreter->signal_value(*fixed_case_equal)
             .low_word().aval == 1
      && fixed_interpreter->signal_value(*fixed_case_not_equal)
             .low_word().aval == 1);
  const auto& merged_fixed = object_value("merged_fixed");
  assert(
      merged_fixed.elements.size() == 4
      && merged_fixed.elements[0].low_word().bval == 0x08
      && merged_fixed.elements[1].low_word().bval == 0x08
      && merged_fixed.elements[2].low_word().bval == 0x08
      && merged_fixed.elements[3].low_word().bval == 0x08);
  const auto& reset = object_value("partial_second");
  assert(
      reset.elements[0].low_word().aval == 0x91
      && reset.elements[0].low_word().bval == 0
      && reset.elements[1].low_word().bval == 0xff
      && reset.elements[2].low_word().bval == 0xff
      && reset.elements[3].low_word().bval == 0xff);

  const auto nonstatic_returns = fsim::frontend::parse_text(
      "nonstatic_function_returns.sv",
      R"(
module nonstatic_function_returns;
  byte dynamic_source[];
  byte dynamic_result[];
  byte queue_source[$:3];
  byte queue_result[$:3];
  byte associative_source[int];
  byte associative_alternative[int];
  byte associative_result[int];
  int query_result;
  logic [7:0] reduction_result;
  byte maximum_result[$];
  logic [7:0] direct_result;
  byte dynamic_alternative[];
  byte dynamic_short[];
  byte conditional_result[];
  byte conditional_merged[];
  byte conditional_shape[];
  logic [7:0] transformed_result;
  byte found_result[$];
  int found_index_result[$];
  byte unique_result[$];
  logic dynamic_equal;
  logic queue_not_equal;
  logic associative_case_equal;
  logic associative_not_equal;

  function automatic byte copy_dynamic[](
      input byte value[]);
    return value;
  endfunction

  function automatic byte copy_queue[$:3](
      input byte value[$:3]);
    copy_queue = value;
  endfunction

  function automatic byte copy_associative[int](
      input byte value[int]);
    return value;
  endfunction

  initial begin
    dynamic_source = '{11, 12, 13};
    queue_source = '{21, 22, 23};
    associative_source = '{-1: 31, 4: 44};
    associative_alternative = '{-1: 31, 5: 44};
    dynamic_alternative = '{11, 99, 13};
    dynamic_short = '{11, 12};
    dynamic_result = copy_dynamic(
        copy_dynamic(dynamic_source));
    queue_result = copy_queue(queue_source);
    associative_result = copy_associative(
        associative_source);
    query_result = $size(copy_dynamic(dynamic_source))
        + copy_queue(queue_source).size()
        + $bits(copy_dynamic(dynamic_source))
        + $dimensions(copy_dynamic(dynamic_source))
        + $unpacked_dimensions(copy_dynamic(dynamic_source))
        + $left(copy_dynamic(dynamic_source))
        + $low(copy_dynamic(dynamic_source))
        + $increment(copy_dynamic(dynamic_source))
        + $right(copy_dynamic(dynamic_source))
        + $high(copy_dynamic(dynamic_source));
    reduction_result = copy_dynamic(dynamic_source).sum();
    maximum_result = copy_queue(queue_source).max();
    direct_result = copy_dynamic(dynamic_source)[1];
    conditional_result = 1'b1
        ? copy_dynamic(dynamic_source)
        : copy_dynamic(dynamic_alternative);
    conditional_merged = 1'bx
        ? copy_dynamic(dynamic_source)
        : copy_dynamic(dynamic_alternative);
    conditional_shape = 1'bx
        ? copy_dynamic(dynamic_source)
        : copy_dynamic(dynamic_short);
    transformed_result =
        copy_dynamic(dynamic_source).sum() with (
            item.index == 1 ? item : 0);
    found_result = copy_dynamic(dynamic_source).find() with (
        item > 11);
    found_index_result =
        copy_queue(queue_source).find_index() with (
            item >= 22);
    unique_result =
        copy_dynamic(dynamic_source).unique() with (
            item == 13 ? 12 : item);
    dynamic_equal =
        copy_dynamic(dynamic_source)
        == copy_dynamic(dynamic_source);
    queue_not_equal =
        copy_queue(queue_source) != copy_queue(queue_source);
    associative_case_equal =
        copy_associative(associative_source)
        === copy_associative(associative_source);
    associative_not_equal =
        copy_associative(associative_source)
        != copy_associative(associative_alternative);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(nonstatic_returns.ok());
  const auto nonstatic_elaborated = fsim::elaboration::elaborate(
      nonstatic_returns.design,
      "sv:work.nonstatic_function_returns");
  if (!nonstatic_elaborated.ok()) {
    for (const auto& diagnostic : nonstatic_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(nonstatic_elaborated.ok());
  const auto& nonstatic_process =
      nonstatic_elaborated.design->processes().front();
  assert(std::ranges::count_if(
             nonstatic_process.operations,
             [](const auto& operation) {
               return std::holds_alternative<
                   fsim::runtime::simir::Call>(operation);
             })
         == 30);
  assert(std::ranges::count_if(
             nonstatic_process.operations,
             [](const auto& operation) {
               return std::holds_alternative<
                   fsim::runtime::simir::ConditionalContainerSelect>(
                   operation);
             })
         == 3);
  assert(std::ranges::any_of(
      nonstatic_process.debug_container_locals,
      [](const auto& local) {
        return local.name == "copy_dynamic.copy_dynamic"
            && !local.type.fixed
            && !local.type.queue
            && !local.type.associative;
      }));
  assert(std::ranges::any_of(
      nonstatic_process.debug_container_locals,
      [](const auto& local) {
        return local.type.queue
            && local.type.maximum_elements
                == std::optional<std::uint32_t>{4};
      }));
  assert(std::ranges::any_of(
      nonstatic_process.debug_container_locals,
      [](const auto& local) {
        return local.type.associative
            && local.type.index_width == 32
            && local.type.signed_indices;
      }));
  auto nonstatic_interpreter =
      nonstatic_elaborated.design->create_interpreter();
  assert(
      nonstatic_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto nonstatic_value =
      [&](const std::string_view name) -> const auto& {
        const auto id =
            nonstatic_elaborated.design->find_container(name);
        assert(id);
        return nonstatic_interpreter->container_object_value(*id);
      };
  const auto& dynamic_value = nonstatic_value("dynamic_result");
  const auto& queue_value = nonstatic_value("queue_result");
  const auto& associative_value =
      nonstatic_value("associative_result");
  assert(
      bytes(dynamic_value)
          == std::vector<std::uint64_t>({11, 12, 13})
      && !dynamic_value.type.fixed
      && !dynamic_value.type.queue
      && !dynamic_value.type.associative);
  assert(
      bytes(queue_value)
          == std::vector<std::uint64_t>({21, 22, 23})
      && queue_value.type.queue
      && queue_value.type.maximum_elements
          == std::optional<std::uint32_t>{4});
  assert(
      bytes(associative_value)
          == std::vector<std::uint64_t>({31, 44})
      && associative_value.type.associative
      && associative_value.keys.size() == 2
      && associative_value.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && associative_value.keys[1].low_word().aval == 4);
  const auto query_result =
      nonstatic_elaborated.design->find_signal("query_result");
  const auto reduction_result =
      nonstatic_elaborated.design->find_signal("reduction_result");
  const auto direct_result =
      nonstatic_elaborated.design->find_signal("direct_result");
  assert(query_result && reduction_result && direct_result);
  assert(
      nonstatic_interpreter->signal_value(*query_result)
              .low_word().aval == 36
      && nonstatic_interpreter->signal_value(*reduction_result)
              .low_word().aval == 36
      && nonstatic_interpreter->signal_value(*direct_result)
              .low_word().aval == 12
      && bytes(nonstatic_value("maximum_result"))
          == std::vector<std::uint64_t>({23}));
  assert(bytes(nonstatic_value("conditional_result"))
         == std::vector<std::uint64_t>({11, 12, 13}));
  const auto& conditional_merged =
      nonstatic_value("conditional_merged");
  assert(
      bytes(conditional_merged)
          == std::vector<std::uint64_t>({11, 0, 13})
      && conditional_merged.elements[0].low_word().bval == 0
      && conditional_merged.elements[1].low_word().bval == 0
      && conditional_merged.elements[2].low_word().bval == 0);
  assert(nonstatic_value("conditional_shape").elements.empty());
  const auto transformed_result =
      nonstatic_elaborated.design->find_signal("transformed_result");
  assert(
      transformed_result
      && nonstatic_interpreter->signal_value(*transformed_result)
             .low_word().aval == 12
      && bytes(nonstatic_value("found_result"))
          == std::vector<std::uint64_t>({12, 13})
      && bytes(nonstatic_value("found_index_result"))
          == std::vector<std::uint64_t>({1, 2})
      && bytes(nonstatic_value("unique_result"))
          == std::vector<std::uint64_t>({11, 12}));
  const auto dynamic_equal =
      nonstatic_elaborated.design->find_signal("dynamic_equal");
  const auto queue_not_equal =
      nonstatic_elaborated.design->find_signal("queue_not_equal");
  const auto associative_case_equal =
      nonstatic_elaborated.design->find_signal(
          "associative_case_equal");
  const auto associative_not_equal =
      nonstatic_elaborated.design->find_signal(
          "associative_not_equal");
  assert(
      dynamic_equal && queue_not_equal && associative_case_equal
      && associative_not_equal
      && nonstatic_interpreter->signal_value(*dynamic_equal)
             .low_word().aval == 1
      && nonstatic_interpreter->signal_value(*queue_not_equal)
             .low_word().aval == 0
      && nonstatic_interpreter->signal_value(*associative_case_equal)
             .low_word().aval == 1
      && nonstatic_interpreter->signal_value(*associative_not_equal)
             .low_word().aval == 1);

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
      "conditional_container_profile_mismatch.sv",
      R"(
module conditional_container_profile_mismatch;
  byte dynamic_source[];
  byte queue_source[$];
  int target;
  function automatic byte dynamic_value[]();
    return dynamic_source;
  endfunction
  function automatic byte queue_value[$]();
    return queue_source;
  endfunction
  initial target = $dimensions(
      1'b1 ? dynamic_value() : queue_value());
endmodule
)",
      "sv:work.conditional_container_profile_mismatch",
      "FSIM-ELAB-SVCOND-002");
  reject_fixed_return(
      "conditional_associative_result.sv",
      R"(
module conditional_associative_result;
  byte source[int];
  byte target[int];
  function automatic byte value[int]();
    return source;
  endfunction
  initial target = 1'b1 ? value() : value();
endmodule
)",
      "sv:work.conditional_associative_result",
      "FSIM-ELAB-SVCOND-003");
  reject_fixed_return(
      "mutating_temporary_function_result.sv",
      R"(
module mutating_temporary_function_result;
  byte source[$];
  logic [7:0] target;
  function automatic byte value[$]();
    return source;
  endfunction
  initial target = value().pop_front();
endmodule
)",
      "sv:work.mutating_temporary_function_result",
      "FSIM-ELAB-SVCONTAINER-022");
  reject_fixed_return(
      "container_equality_scalar_operand.sv",
      R"(
module container_equality_scalar_operand;
  byte source[];
  logic result;
  function automatic byte value[](); return source; endfunction
  initial result = value() == 1;
endmodule
)",
      "sv:work.container_equality_scalar_operand",
      "FSIM-ELAB-SVEQUAL-002");
  reject_fixed_return(
      "container_equality_profile_mismatch.sv",
      R"(
module container_equality_profile_mismatch;
  byte dynamic_source[];
  byte queue_source[$];
  logic result;
  function automatic byte dynamic_value[]();
    return dynamic_source;
  endfunction
  function automatic byte queue_value[$]();
    return queue_source;
  endfunction
  initial result = dynamic_value() == queue_value();
endmodule
)",
      "sv:work.container_equality_profile_mismatch",
      "FSIM-ELAB-SVEQUAL-003");
  reject_fixed_return(
      "container_relational_unsupported.sv",
      R"(
module container_relational_unsupported;
  byte source[];
  logic result;
  function automatic byte value[](); return source; endfunction
  initial result = value() < value();
endmodule
)",
      "sv:work.container_relational_unsupported",
      "FSIM-ELAB-SVEQUAL-001");
  reject_fixed_return(
      "container_wildcard_equality_unsupported.sv",
      R"(
module container_wildcard_equality_unsupported;
  byte source[];
  logic result;
  function automatic byte value[](); return source; endfunction
  initial result = value() ==? value();
endmodule
)",
      "sv:work.container_wildcard_equality_unsupported",
      "FSIM-ELAB-SVEQUAL-001");
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
      "FSIM-ELAB-SVSLICE-006");
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
      "FSIM-ELAB-SVSLICE-006");
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
      "FSIM-ELAB-SVSLICE-006");
  reject_fixed_return(
      "nonstatic_kind_mismatch_function_return.sv",
      R"(
module nonstatic_kind_mismatch_function_return;
  byte dynamic_result[];
  byte queue_source[$];
  function automatic byte queue_result[$]();
    return queue_source;
  endfunction
  initial dynamic_result = queue_result();
endmodule
)",
      "sv:work.nonstatic_kind_mismatch_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "queue_bound_mismatch_function_return.sv",
      R"(
module queue_bound_mismatch_function_return;
  byte target[$:2];
  byte source[$:3];
  function automatic byte result[$:3]();
    return source;
  endfunction
  initial target = result();
endmodule
)",
      "sv:work.queue_bound_mismatch_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "element_profile_mismatch_function_return.sv",
      R"(
module element_profile_mismatch_function_return;
  bit [7:0] target[];
  logic [7:0] source[];
  function automatic logic [7:0] result[]();
    return source;
  endfunction
  initial target = result();
endmodule
)",
      "sv:work.element_profile_mismatch_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "index_profile_mismatch_function_return.sv",
      R"(
module index_profile_mismatch_function_return;
  byte target[byte];
  byte source[int];
  function automatic byte result[int]();
    return source;
  endfunction
  initial target = result();
endmodule
)",
      "sv:work.index_profile_mismatch_function_return",
      "FSIM-ELAB-SVFUNC-008");
  reject_fixed_return(
      "function_container_argument_mismatch.sv",
      R"(
module function_container_argument_mismatch;
  byte queue_source[$];
  byte dynamic_target[];
  function automatic byte copy_dynamic[](
      input byte value[]);
    return value;
  endfunction
  initial dynamic_target = copy_dynamic(queue_source);
endmodule
)",
      "sv:work.function_container_argument_mismatch",
      "FSIM-ELAB-SVFUNC-009");
  reject_fixed_return(
      "task_container_argument_mismatch.sv",
      R"(
module task_container_argument_mismatch;
  byte queue_source[$];
  task automatic take_dynamic(input byte value[]);
  endtask
  initial take_dynamic(queue_source);
endmodule
)",
      "sv:work.task_container_argument_mismatch",
      "FSIM-ELAB-SVTASK-011");
  reject_fixed_return(
      "recursive_dynamic_function_return.sv",
      R"(
module recursive_dynamic_function_return;
  byte source[];
  byte target[];
  function automatic byte recurse[](input byte value[]);
    return recurse(value);
  endfunction
  initial target = recurse(source);
endmodule
)",
      "sv:work.recursive_dynamic_function_return",
      "FSIM-ELAB-SVFUNC-006");
  reject_fixed_return(
      "dynamic_slice_function_return.sv",
      R"(
module dynamic_slice_function_return;
  byte source[];
  byte target[$];
  function automatic byte sliced[$]();
    return source[1:0];
  endfunction
  initial target = sliced();
endmodule
)",
      "sv:work.dynamic_slice_function_return",
      "FSIM-ELAB-SVSLICE-001");
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
