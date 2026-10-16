// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>

namespace fsim::tests::elaboration {

void test_systemverilog_static_slice_ports() {
  using fsim::runtime::PackedLogic4;
  const auto parsed = fsim::frontend::parse_text(
      "static-slice-ports.sv",
      R"(
module slice_port_leaf(
    input logic signed [7:0] source[-2:0],
    output logic signed [7:0] result[9:7],
    inout logic signed [7:0] shared[4:6]);
  initial begin
    #1;
    assert (source[-2] === 8'b10xz0011);
    assert (source[-1] === 8'h32);
    assert (source[0] === 8'h22);
    assert (shared[4] === 8'h11);
    assert (shared[5] === 8'b0000x001);
    assert (shared[6] === 8'h13);
    result[9] = source[-2];
    result[8] = source[-1];
    result[7] = source[0];
    shared[4] = 8'ha1;
    shared[5] = 8'b0000z010;
    shared[6] = 8'ha3;
  end
endmodule

module slice_port_mid(
    input logic signed [7:0] source[-2:0],
    output logic signed [7:0] result[9:7],
    inout logic signed [7:0] shared[4:6]);
  generate
    if (1) begin : generated
      slice_port_leaf child(source, result, shared);
    end
  endgenerate
endmodule

module slice_probe(input logic signed [7:0] source[1:0]);
  initial begin
    #1;
    assert (source[1] === 8'h12);
    assert (source[0] === 8'h02);
  end
endmodule

module slice_writer(output logic [7:0] result[2:0]);
  initial begin
    #1;
    result[2] = 8'hc2;
    result[1] = 8'hc1;
    result[0] = 8'hc0;
  end
endmodule

module static_slice_port_top #(
    parameter int HIGH = 4,
    parameter int LOW = 2);
  logic signed [7:0] source[5:0];
  logic signed [7:0] result[4:0];
  logic signed [7:0] shared[-2:2];
  logic [7:0] partition[5:0];

  slice_port_mid mid(
      .source(source[LOW +: HIGH - LOW + 1]),
      .result(result[1 +: 3]),
      .shared(shared[-1 +: 3]));
  slice_probe positional(source[1:0]);
  slice_writer high(.result(partition[5:3]));
  slice_writer low(.result(partition[2:0]));

  initial begin
    source = '{
        8'h52, 8'b10xz0011, 8'h32,
        8'h22, 8'h12, 8'h02};
    result = '{8'hee, 8'h43, 8'h33, 8'h23, 8'hdd};
    shared = '{
        8'hf2, 8'h11, 8'b0000x001, 8'h13, 8'he2};
    partition = '{default: 8'h00};
    #2;
    assert (result[4] === 8'hee);
    assert (result[3] === 8'b10xz0011);
    assert (result[2] === 8'h32);
    assert (result[1] === 8'h22);
    assert (result[0] === 8'hdd);
    assert (shared[-2] === 8'hf2);
    assert (shared[-1] === 8'ha1);
    assert (shared[0] === 8'b0000z010);
    assert (shared[1] === 8'ha3);
    assert (shared[2] === 8'he2);
    assert (partition[5] === 8'hc2);
    assert (partition[3] === 8'hc0);
    assert (partition[2] === 8'hc2);
    assert (partition[0] === 8'hc0);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = compile_and_elaborate(
      parsed.design, "static_slice_port_top");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 10);

  const auto source =
      elaborated.design->find_container(
          "static_slice_port_top.source");
  const auto mid_source =
      elaborated.design->find_container(
          "static_slice_port_top.mid.source");
  const auto leaf_source =
      elaborated.design->find_container(
          "static_slice_port_top.mid.generated.child.source");
  assert(
      source && mid_source && leaf_source
      && *source != *mid_source
      && *mid_source == *leaf_source);
  const auto& mid_source_info =
      elaborated.design->container_objects().at(*mid_source);
  assert(
      mid_source_info.type.index_left == -2
      && mid_source_info.type.index_right == 0
      && mid_source_info.type.element_width == 8
      && mid_source_info.type.signed_elements
      && !mid_source_info.type.two_state
      && mid_source_info.slice_alias
      && mid_source_info.slice_alias->object == *source
      && mid_source_info.slice_alias->selected_left == 4
      && mid_source_info.slice_alias->selected_right == 2);

  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 2);
  const auto result_object =
      elaborated.design->find_container(
          "static_slice_port_top.result");
  const auto shared_object =
      elaborated.design->find_container(
          "static_slice_port_top.shared");
  assert(result_object && shared_object);
  const auto& result_value =
      interpreter->container_object_value(*result_object);
  const auto& shared_value =
      interpreter->container_object_value(*shared_object);
  assert(
      result_value.elements.front()
          == PackedLogic4::from_msb_string("11101110")
      && result_value.elements[1].to_msb_string()
          == "10XZ0011"
      && result_value.elements.back()
          == PackedLogic4::from_msb_string("11011101")
      && shared_value.elements.front()
          == PackedLogic4::from_msb_string("11110010")
      && shared_value.elements[2].to_msb_string()
          == "0000Z010"
      && shared_value.elements.back()
          == PackedLogic4::from_msb_string("11100010"));

  const auto ordinal_view = fsim::frontend::parse_text(
      "whole-array-ordinal-view-ports.sv",
      R"(
module ordinal_view_leaf(
    input logic [7:0] sampled[7:5],
    output logic [7:0] result[10:8]);
  initial begin
    #1;
    assert (sampled[7] === 8'h11);
    assert (sampled[6] === 8'h22);
    assert (sampled[5] === 8'h33);
    result[10] = sampled[7];
    result[9] = sampled[6];
    result[8] = sampled[5];
  end
endmodule

module whole_array_ordinal_view_top;
  logic [7:0] sampled[0:2];
  logic [7:0] result[18:20];
  ordinal_view_leaf child(
      .sampled(sampled),
      .result(result));
  initial begin
    sampled = '{8'h11, 8'h22, 8'h33};
    result = '{default: 8'h00};
    #2;
    assert (result[18] === 8'h11);
    assert (result[19] === 8'h22);
    assert (result[20] === 8'h33);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(ordinal_view.ok());
  const auto ordinal_view_elaborated = compile_and_elaborate(
      ordinal_view.design, "whole_array_ordinal_view_top");
  assert(ordinal_view_elaborated.ok());
  const auto ordinal_input = ordinal_view_elaborated.design->find_container(
      "whole_array_ordinal_view_top.sampled");
  const auto ordinal_child_input =
      ordinal_view_elaborated.design->find_container(
          "whole_array_ordinal_view_top.child.sampled");
  const auto ordinal_output = ordinal_view_elaborated.design->find_container(
      "whole_array_ordinal_view_top.result");
  const auto ordinal_child_output =
      ordinal_view_elaborated.design->find_container(
          "whole_array_ordinal_view_top.child.result");
  assert(
      ordinal_input && ordinal_child_input && ordinal_output
      && ordinal_child_output);
  const auto& ordinal_input_info =
      ordinal_view_elaborated.design->container_objects().at(
          *ordinal_child_input);
  const auto& ordinal_output_info =
      ordinal_view_elaborated.design->container_objects().at(
          *ordinal_child_output);
  assert(
      ordinal_input_info.type.index_left == 7
      && ordinal_input_info.type.index_right == 5
      && ordinal_input_info.slice_alias
      && ordinal_input_info.slice_alias->object == *ordinal_input
      && ordinal_input_info.slice_alias->selected_left == 0
      && ordinal_input_info.slice_alias->selected_right == 2);
  assert(
      ordinal_output_info.type.index_left == 10
      && ordinal_output_info.type.index_right == 8
      && ordinal_output_info.slice_alias
      && ordinal_output_info.slice_alias->object == *ordinal_output
      && ordinal_output_info.slice_alias->selected_left == 18
      && ordinal_output_info.slice_alias->selected_right == 20);
  auto ordinal_view_interpreter =
      ordinal_view_elaborated.design->create_interpreter();
  const auto ordinal_view_result = ordinal_view_interpreter->run();
  assert(
      ordinal_view_result.status == fsim::runtime::RunStatus::completed
      && ordinal_view_result.time == 2);
  const auto& ordinal_output_value =
      ordinal_view_interpreter->container_object_value(*ordinal_output);
  assert(
      ordinal_output_value.elements.front()
          == PackedLogic4::from_msb_string("00010001")
      && ordinal_output_value.elements[1]
          == PackedLogic4::from_msb_string("00100010")
      && ordinal_output_value.elements.back()
          == PackedLogic4::from_msb_string("00110011"));

  const auto scalar_array_output = fsim::frontend::parse_text(
      "instance-array-scalar-output-actual.sv",
      R"(
module scalar_array_output_leaf(
    input logic [7:0] value,
    output logic [7:0] result);
  assign result = value;
endmodule

module scalar_array_output_wrapper(
    output logic [7:0] result[3:0]);
  logic [7:0] values[20:17];
  scalar_array_output_leaf child[0:3](
      .value(values),
      .result(result));
  initial values = '{8'h13, 8'h24, 8'h35, 8'h46};
endmodule

module instance_array_scalar_output_actual;
  logic [7:0] observed[16:19];
  scalar_array_output_wrapper wrapper(.result(observed));
  initial begin
    #1;
    assert (observed[16] === 8'h13);
    assert (observed[17] === 8'h24);
    assert (observed[18] === 8'h35);
    assert (observed[19] === 8'h46);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(scalar_array_output.ok());
  const auto scalar_array_output_elaborated = compile_and_elaborate(
      scalar_array_output.design,
      "instance_array_scalar_output_actual");
  assert(scalar_array_output_elaborated.ok());
  auto scalar_array_output_interpreter =
      scalar_array_output_elaborated.design->create_interpreter();
  const auto scalar_array_output_result
      = scalar_array_output_interpreter->run();
  assert(
      scalar_array_output_result.status
          == fsim::runtime::RunStatus::completed
      && scalar_array_output_result.time == 1);
  const auto scalar_array_output_value
      = scalar_array_output_elaborated.design->find_container(
          "instance_array_scalar_output_actual.observed");
  assert(scalar_array_output_value);
  const auto& scalar_array_output_elements
      = scalar_array_output_interpreter->container_object_value(
          *scalar_array_output_value);
  assert(
      scalar_array_output_elements.elements[0]
          == PackedLogic4::from_msb_string("00010011")
      && scalar_array_output_elements.elements[1]
          == PackedLogic4::from_msb_string("00100100")
      && scalar_array_output_elements.elements[2]
          == PackedLogic4::from_msb_string("00110101")
      && scalar_array_output_elements.elements[3]
          == PackedLogic4::from_msb_string("01000110"));

  const auto invalid = fsim::frontend::parse_text(
      "static-slice-port-invalid.sv",
      R"(
module slice_input(input logic [7:0] value[2:0]);
endmodule

module slice_output(output logic [7:0] value[2:0]);
endmodule

module slice_forward(input logic [7:0] value[5:0]);
  slice_output read_only(.value(value[4:2]));
endmodule

module static_slice_port_invalid;
  logic [7:0] down[5:0];
  logic [7:0] pair[1:0];
  logic [3:0] narrow[2:0];
  bit [7:0] two_state[2:0];
  logic signed [7:0] signed_value[2:0];
  logic [7:0] dynamic[];
  int runtime_bound;

  slice_input runtime(.value(down[runtime_bound:2]));
  slice_input unknown(.value(down[32'hxxxxxxxx:2]));
  slice_input reversed(.value(down[2:4]));
  slice_input out_of_range(.value(down[6:4]));
  slice_input indexed(.value(down[4 +: 3]));
  slice_input zero_width(.value(down[2 +: 0]));
  slice_input negative_width(.value(down[2 -: -1]));
  slice_input runtime_width(.value(down[2 +: runtime_bound]));
  slice_input indirect(.value(down[4:2][2:0]));
  slice_input nonstatic(.value(dynamic[2:0]));
  slice_input element(.value(down[2]));
  slice_input missing(.value(not_declared));
  slice_input wrong_count(.value(pair[1:0]));
  slice_input wrong_width(.value(narrow[2:0]));
  slice_input wrong_state(.value(two_state[2:0]));
  slice_input wrong_sign(.value(signed_value[2:0]));
  slice_output overlap_a(.value(down[3 +: 3]));
  slice_output overlap_b(.value(down[2 +: 3]));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = compile_and_elaborate(
      invalid.design, "static_slice_port_invalid");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-003"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-005"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-006"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-007"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVPORT-008"));

  const auto scalar_array_output_controls = fsim::frontend::parse_text(
      "instance-array-scalar-output-controls.sv",
      R"(
module scalar_array_control_leaf(output logic [7:0] result);
  assign result = 8'h5a;
endmodule

module scalar_array_count_mismatch;
  logic [7:0] result[2:0];
  scalar_array_control_leaf child[3:0](.result(result));
endmodule

module scalar_array_profile_mismatch;
  logic [6:0] result[3:0];
  scalar_array_control_leaf child[3:0](.result(result));
endmodule

module scalar_array_read_only(input logic [7:0] result[3:0]);
  scalar_array_control_leaf child[3:0](.result(result));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(scalar_array_output_controls.ok());
  const auto scalar_array_count_rejected = compile_and_elaborate(
      scalar_array_output_controls.design, "scalar_array_count_mismatch");
  assert(
      !scalar_array_count_rejected.ok()
      && has_diagnostic(
          scalar_array_count_rejected, "FSIM-ELAB-SVPORT-007"));
  const auto scalar_array_profile_rejected = compile_and_elaborate(
      scalar_array_output_controls.design,
      "scalar_array_profile_mismatch");
  assert(
      !scalar_array_profile_rejected.ok()
      && has_diagnostic(
          scalar_array_profile_rejected, "FSIM-ELAB-SVPORT-007"));
  const auto scalar_array_read_only_rejected = compile_and_elaborate(
      scalar_array_output_controls.design, "scalar_array_read_only");
  assert(
      !scalar_array_read_only_rejected.ok()
      && has_diagnostic(
          scalar_array_read_only_rejected, "FSIM-ELAB-SVPORT-009"));

  const auto scalar_array_nominal_controls = fsim::frontend::parse_text(
      "instance-array-nominal-profile-controls.sv",
      R"(
package scalar_array_nominal_profile_pkg;
  typedef struct packed {
    bit high;
    bit low;
  } pair_a_t;
  typedef struct packed {
    bit upper;
    bit lower;
  } pair_b_t;
  typedef union packed {
    bit [1:0] bits;
    pair_a_t pair;
  } pair_union_t;
  typedef enum bit [1:0] {
    enum_zero = 2'b00,
    enum_one = 2'b01
  } pair_enum_t;
endpackage

module scalar_array_vector_output_leaf(output bit [1:0] result);
endmodule

module scalar_array_struct_output_leaf(
    output scalar_array_nominal_profile_pkg::pair_a_t result);
endmodule

module scalar_array_logic_output_leaf(output logic [1:0] result);
endmodule

module scalar_array_signed_output_leaf(output bit signed [1:0] result);
endmodule

module scalar_array_struct_vector_compatible;
  scalar_array_nominal_profile_pkg::pair_a_t result[2];
  scalar_array_vector_output_leaf child[2](.result(result));
endmodule

module scalar_array_distinct_structs_compatible;
  scalar_array_nominal_profile_pkg::pair_b_t result[2];
  scalar_array_struct_output_leaf child[2](.result(result));
endmodule

module scalar_array_union_vector_compatible;
  scalar_array_nominal_profile_pkg::pair_union_t result[2];
  scalar_array_vector_output_leaf child[2](.result(result));
endmodule

module scalar_array_enum_vector_incompatible;
  scalar_array_nominal_profile_pkg::pair_enum_t result[2];
  scalar_array_vector_output_leaf child[2](.result(result));
endmodule

module scalar_array_domain_mismatch;
  bit [1:0] result[2];
  scalar_array_logic_output_leaf child[2](.result(result));
endmodule

module scalar_array_signedness_mismatch;
  bit [1:0] result[2];
  scalar_array_signed_output_leaf child[2](.result(result));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(scalar_array_nominal_controls.ok());
  for (const auto* top : {
           "scalar_array_struct_vector_compatible",
           "scalar_array_distinct_structs_compatible",
           "scalar_array_union_vector_compatible" }) {
    const auto compatible = compile_and_elaborate(
        scalar_array_nominal_controls.design, top);
    assert(compatible.ok());
  }
  for (const auto* top : {
           "scalar_array_enum_vector_incompatible",
           "scalar_array_domain_mismatch",
           "scalar_array_signedness_mismatch" }) {
    const auto incompatible = compile_and_elaborate(
        scalar_array_nominal_controls.design, top);
    assert(
        !incompatible.ok()
        && has_diagnostic(incompatible, "FSIM-ELAB-SVPORT-007"));
  }

  const auto read_only = fsim::frontend::parse_text(
      "static-slice-port-read-only.sv",
      R"(
module slice_output(output logic [7:0] value[2:0]);
endmodule
module slice_forward(input logic [7:0] value[5:0]);
  slice_output read_only(.value(value[4:2]));
endmodule
module static_slice_port_read_only;
  logic [7:0] value[5:0];
  slice_forward forward(.value(value));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(read_only.ok());
  const auto read_only_rejected =
      compile_and_elaborate(
          read_only.design,
          "static_slice_port_read_only");
  assert(
      !read_only_rejected.ok()
      && has_diagnostic(
          read_only_rejected, "FSIM-ELAB-SVPORT-009"));

  const auto selected_word = fsim::frontend::parse_text(
      "static-array-selected-word-port.sv",
      R"(
module selected_word_probe(
    input logic [7:0] value,
    output logic [7:0] observed);
  assign observed = value;
endmodule

module static_array_selected_word_port;
  logic [7:0] values[0:3];
  wire [7:0] observed[0:3];
  integer index;
  genvar word;
  generate
    for (word = 0; word < 4; word = word + 1) begin : probes
      selected_word_probe probe(
          .value(values[word]),
          .observed(observed[word]));
    end
  endgenerate
  initial begin
    for (index = 0; index < 4; index = index + 1)
      values[index] <= index + 8'h10;
    #1;
    assert (observed[0] === 8'h10);
    assert (observed[1] === 8'h11);
    assert (observed[2] === 8'h12);
    assert (observed[3] === 8'h13);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(selected_word.ok());
  const auto selected_word_elaborated =
      compile_and_elaborate(
          selected_word.design,
          "static_array_selected_word_port");
  assert(selected_word_elaborated.ok());
  auto selected_word_interpreter =
      selected_word_elaborated.design->create_interpreter();
  const auto selected_word_result =
      selected_word_interpreter->run();
  assert(
      selected_word_result.status
          == fsim::runtime::RunStatus::completed
      && selected_word_result.time == 1);
}

}  // namespace fsim::tests::elaboration
