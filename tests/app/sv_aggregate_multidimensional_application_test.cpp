// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::string, 50> values;
  std::string vcd;
  std::string debugger;
  std::size_t compiled{};
  fsim::app::NativeCacheStatistics cache;
};

void write_source(
    const std::filesystem::path& source,
    const char replacement) {
  std::ofstream output(source, std::ios::binary | std::ios::trunc);
  output << R"(
package aggregate_types;
  typedef enum logic [1:0] { ZERO, ONE, TWO, THREE } code_t;
  typedef struct packed {
    code_t code;
    logic valid;
  } inner_t;
  typedef union packed {
    inner_t inner;
    logic [2:0] raw;
  } overlay_t;
  typedef struct packed {
    logic prefix;
    overlay_t overlay;
    logic [1:0] tail;
  } outer_t;
  typedef struct {
    outer_t packed_value;
    logic [1:0] count;
  } record_t;
  typedef struct {
    logic [3:0] nibble;
  } metadata_t;
  typedef struct {
    metadata_t metadata;
    logic [7:0] code;
    int count;
  } control_t;
  typedef union {
    logic [7:0] primary;
    logic [7:0] alias_value;
  } choice_t;
  typedef struct packed {
    logic [3:0] tag;
    logic [3:0] data;
  } packet_t;
  typedef union packed {
    logic [15:0] wide;
    logic [15:0] mirror;
  } ordinary_t;
  typedef union tagged packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } tagged_t;
  typedef struct packed {
    logic [7:0] payload = 8'ha5;
    inner_t nested = '{code: TWO, valid: 1'b1};
  } initialized_t;
  localparam outer_t NESTED_CONSTANT = '{
    prefix: 1'b1,
    overlay: '{inner: '{code: TWO, valid: 1'b1}},
    tail: 2'b10
  };
  typedef struct {
    real weight;
    time ticks;
    string label;
    chandle cookie;
  } state_t;
endpackage

module matrix_child(
  input logic [3:0] matrix[1:0][0:2],
  output logic [3:0] observed
);
  initial begin
    #1;
    observed = matrix[1][1];
  end
endmodule

module aggregate_child(
  input aggregate_types::record_t value,
  output logic [7:0] observed
);
  initial begin
    #1;
    observed = value;
  end
endmodule

module packet_matrix_child(
  input aggregate_types::packet_t matrix[1:0][0:1],
  output logic [7:0] observed
);
  initial begin
    #1;
    observed = matrix[0][1];
  end
endmodule

module control_matrix_child(
  input aggregate_types::control_t matrix[1:0],
  output logic [43:0] observed
);
  initial begin
    #1;
    observed[43:40] = matrix[0].metadata.nibble;
    observed[39:32] = matrix[0].code;
    observed[31:0] = matrix[0].count;
  end
endmodule

import aggregate_types::*;
module aggregate_multidimensional_top #(
  parameter type VALUE_T = record_t,
  parameter type PACKET_T = packet_t
);
  VALUE_T aggregate;
  logic [3:0] matrix[1:0][0:2];
  logic [3:0] matrix_copy[1:0][0:2];
  logic [3:0] row_copy[0:2];
  logic [7:0] aggregate_observed;
  logic [23:0] matrix_observed;
  logic [3:0] dynamic_observed;
  logic [3:0] child_observed;
  logic [7:0] aggregate_child_observed;
  PACKET_T packets[1:0];
  PACKET_T packet_matrix[1:0][0:1];
  PACKET_T dynamic_packets[];
  PACKET_T pending_packets[$:3];
  logic [63:0] container_observed;
  logic [7:0] packet_child_observed;
  logic [43:0] control_child_observed;
  real real_values[1:0];
  real real_copy[1:0];
  time time_values[];
  time time_copy[];
  chandle handle_values[$];
  chandle handle_copy[$];
  string string_values[];
  string string_copy[];
  string fixed_strings[1:0];
  string queue_strings[$:3];
  string associative_strings[int];
  int string_keyed[string];
  int string_keyed_copy[string];
  string string_keyed_values[string];
  string traversal_key;
  string matrix_strings[1:0][0:1];
  string nested_strings[1:0][];
  string nested_copy[1:0][];
  state_t states[1:0];
  state_t state_copy[1:0];
  logic [7:0] scalar_container_observed;
  struct packed {
    logic [31:0] header;
    struct packed {
      bit [95:0] payload;
      logic valid;
    } body;
    bit [7:0] tail;
  } anonymous_default, anonymous_pattern, anonymous_update;
  logic [136:0] anonymous_default_observed;
  logic [136:0] anonymous_pattern_observed;
  logic [136:0] anonymous_update_observed;
  logic [40:0] anonymous_selected_observed;
  ordinary_t ordinary;
  tagged_t tagged_default;
  tagged_t tagged_narrow;
  tagged_t tagged_same;
  tagged_t tagged_wide;
  tagged_t tagged_casted;
  logic [15:0] ordinary_mirror_observed;
  logic [15:0] ordinary_wide_observed;
  logic [15:0] ordinary_selected_observed;
  logic [16:0] tagged_default_observed;
  logic [16:0] tagged_narrow_observed;
  logic [16:0] tagged_wide_constructor_observed;
  logic [16:0] tagged_wide_observed;
  logic [16:0] tagged_cast_observed;
  logic [7:0] tagged_active_observed;
  logic [15:0] tagged_inactive_observed;
  logic [3:0] tagged_compare_observed;
  initialized_t initialized_default;
  enum { ANON_START = 5, ANON_NEXT } anonymous_enum_value;
  logic [10:0] initialized_default_observed;
  logic [5:0] nested_constant_observed;
  logic [31:0] anonymous_enum_default_observed;
  logic signed [31:0] aggregate_bits_observed;
  logic signed [31:0] aggregate_left_observed;
  logic signed [31:0] aggregate_right_observed;
  logic signed [31:0] aggregate_size_observed;
  logic signed [31:0] aggregate_dimensions_observed;
  logic signed [31:0] aggregate_unpacked_dimensions_observed;
  logic [47:0] subarray_observed;
  control_t controls[1:0];
  logic [7:0] unpacked_code_observed;
  logic [3:0] unpacked_nested_observed;
  logic [31:0] unpacked_count_observed;
  struct {
    logic [7:0] code;
    struct {
      logic [3:0] nibble;
    } metadata;
  } anonymous_controls[1:0];
  choice_t choices[1:0];
  union {
    logic [7:0] primary;
    logic [7:0] alias_value;
  } anonymous_choices[1:0];
  logic [7:0] anonymous_unpacked_code_observed;
  logic [3:0] anonymous_unpacked_nested_observed;
  logic [7:0] named_union_observed;
  logic [7:0] anonymous_union_observed;
  control_t control_patterns[1:0];
  control_t control_pattern_copy[1:0];
  control_t unknown_controls[1:0];
  control_t unknown_control_copy[1:0];
  control_t dynamic_controls[];
  control_t dynamic_control_copy[];
  control_t pending_controls[$:3];
  control_t associative_controls[int];
  control_t associative_control_copy[int];
  choice_t patterned_choices[1:0];
  logic [87:0] control_pattern_observed;
  logic [43:0] control_pattern_copy_observed;
  logic [43:0] dynamic_control_observed;
  logic [43:0] pending_control_observed;
  logic [43:0] associative_control_observed;
  logic [159:0] aggregate_query_matrix_observed;
  logic [7:0] aggregate_compare_matrix_observed;
  logic [15:0] unpacked_union_pattern_observed;
  logic [31:0] string_index_observed;
  int row;
  int column;

  function automatic logic [3:0] pick(
      input logic [3:0] value[1:0][0:2],
      input int selected_row,
      input int selected_column);
    return value[selected_row][selected_column];
  endfunction

  function automatic logic [11:0] pack_row(
      input logic [3:0] value[0:2]);
    return {value[0], value[1], value[2]};
  endfunction

  function automatic logic [3:0] echo_row[0:2](
      input logic [3:0] value[0:2]);
    return value;
  endfunction

  task automatic patch_row(inout logic [3:0] value[0:2]);
    value[2] = 4'hd;
  endtask

  function automatic string select_string(
      input string values[], input int selected);
    return values[selected];
  endfunction

  task automatic patch_string(
      inout string values[], input int selected, input string value);
    values[selected] = value;
  endtask

  function automatic VALUE_T echo(input VALUE_T value);
    return value;
  endfunction

  function automatic PACKET_T packet_pick(
      input PACKET_T value[1:0][0:1],
      input int selected_row,
      input int selected_column);
    return value[selected_row][selected_column];
  endfunction

  task automatic replace(
      inout logic [3:0] value[1:0][0:2]);
    value[1][1] = 4'h)"
         << replacement << R"(;
  endtask

  task automatic replace_packet(
      inout PACKET_T value[1:0][0:1]);
    value[0][1] = '{tag: 4'h7, data: 4'he};
  endtask

  generate
    if (1) begin : generated
      matrix_child child(
        .matrix(matrix),
        .observed(child_observed)
      );
      aggregate_child aggregate_instance(
        .value(aggregate),
        .observed(aggregate_child_observed)
      );
      packet_matrix_child packet_instance(
        .matrix(packet_matrix),
        .observed(packet_child_observed)
      );
      control_matrix_child control_instance(
        .matrix(controls),
        .observed(control_child_observed)
      );
    end
  endgenerate

  initial begin
    aggregate = echo('{
      packed_value: '{
        prefix: 1'b1,
        overlay: '{inner: '{code: TWO, valid: 1'b1}},
        tail: 2'b01
      },
      count: 2'b10
    });
    aggregate_observed = aggregate;
    matrix = '{
      1: '{0: 4'h1, 1: 4'h2, default: 4'h3},
      default: '{4'h4, 4'h5, 4'h6}
    };
    replace(matrix);
    matrix_observed = {
      matrix[1][0], matrix[1][1], matrix[1][2],
      matrix[0][0], matrix[0][1], matrix[0][2]
    };
    matrix_copy = matrix;
    row_copy = matrix_copy[1];
    matrix_copy[0] = row_copy;
    matrix_copy[0][1:2] = matrix_copy[1][0:1];
    matrix_copy[1][1:2] = matrix_copy[1][0:1];
    row_copy = echo_row(matrix_copy[1]);
    patch_row(matrix_copy[0]);
    assert ($size(matrix_copy[1][0:1]) == 2);
    assert ($left(matrix_copy[1][0:1]) == 0);
    assert ($right(matrix_copy[1][0:1]) == 1);
    assert ($dimensions(matrix_copy[1][0:1]) == 2);
    assert ($unpacked_dimensions(matrix_copy[1][0:1]) == 1);
    row = 0;
    subarray_observed = {
      pack_row(row_copy),
      pack_row(matrix_copy[0]),
      pack_row(matrix_copy[1]),
      pack_row(matrix_copy[row])
    };
    row = 0;
    column = 1;
    dynamic_observed = pick(matrix, row, column);
    packets = '{
      '{tag: 4'h1, data: 4'h2},
      '{tag: 4'h3, data: 4'h4}
    };
    packets[0] = '{tag: 4'h3, data: 4'h5};
    packet_matrix = '{
      '{
        '{tag: 4'h4, data: 4'h6},
        '{tag: 4'h5, data: 4'h7}
      },
      '{
        '{tag: 4'h6, data: 4'h8},
        '{tag: 4'h7, data: 4'h9}
      }
    };
    replace_packet(packet_matrix);
    dynamic_packets = '{
      '{tag: 4'h8, data: 4'ha},
      '{tag: 4'h9, data: 4'hb}
    };
    dynamic_packets = new[3](dynamic_packets);
    assert ($isunknown(dynamic_packets[2]));
    pending_packets = '{
      '{tag: 4'ha, data: 4'hb},
      '{tag: 4'hb, data: 4'hc}
    };
    pending_packets.insert(
        1, '{tag: 4'hc, data: 4'hd});
    pending_packets.delete(0);
    container_observed = {
      packets[1], packets[0],
      packet_matrix[1][0], packet_matrix[1][1],
      packet_matrix[0][0],
      packet_pick(packet_matrix, 0, 1),
      dynamic_packets[0], pending_packets[0]
    };
    real_values = '{0.0, 0.0};
    real_copy = real_values;
    assert (real_copy == real_values);
    real_copy[0] = 1.5;
    assert (real_copy != real_values);
    time_values = '{7, 9};
    time_copy = new[3](time_values);
    assert (time_copy[0] == 7);
    assert (time_copy[1] == 9);
    assert (time_copy[2] == 0);
    handle_values = '{0, 0};
    handle_copy = handle_values;
    assert (handle_copy == handle_values);
    string_values = new[2];
    assert (string_values[0] == "");
    assert (string_values[1] == "");
    string_values[0] = "zero";
    patch_string(string_values, 1, "one");
    assert (select_string(string_values, 0) == "zero");
    assert (string_values[1] == "one");
    string_copy = string_values;
    assert (string_copy == string_values);
    assert (string_copy.size() == 2);
    string_copy = new[3](string_copy);
    assert (string_copy[0] == "zero");
    assert (string_copy[1] == "one");
    assert (string_copy[2] == "");
    string_copy[1] = "changed";
    assert (string_copy != string_values);
    fixed_strings = '{"left", "right"};
    assert (fixed_strings[1] == "left");
    assert (fixed_strings[0] == "right");
    fixed_strings[0] = "updated";
    assert (fixed_strings[0] == "updated");
    queue_strings = '{"front", "back"};
    assert (queue_strings.size() == 2);
    assert (queue_strings[0] == "front");
    queue_strings[1] = "tail";
    assert (queue_strings[1] == "tail");
    assert (associative_strings[9] == "");
    associative_strings[9] = "nine";
    associative_strings[-2] = "minus-two";
    assert (associative_strings[9] == "nine");
    assert (associative_strings[-2] == "minus-two");
    assert (string_keyed["missing"] == 0);
    string_keyed["beta"] = 2;
    string_keyed["alpha"] = 1;
    string_keyed["gamma"] = 3;
    assert (string_keyed.exists("alpha"));
    assert (!string_keyed.exists("missing"));
    string_keyed_copy = string_keyed;
    assert (string_keyed_copy == string_keyed);
    traversal_key = "";
    assert (string_keyed.first(traversal_key));
    assert (traversal_key == "alpha");
    assert (string_keyed.next(traversal_key));
    assert (traversal_key == "beta");
    assert (string_keyed.last(traversal_key));
    assert (traversal_key == "gamma");
    assert (string_keyed.prev(traversal_key));
    assert (traversal_key == "beta");
    string_keyed.delete("beta");
    assert (!string_keyed.exists("beta"));
    assert (string_keyed["beta"] == 0);
    string_keyed_values["name"] = "value";
    assert (string_keyed_values["name"] == "value");
    assert (string_keyed_values["missing"] == "");
    string_index_observed =
      string_keyed["alpha"] + string_keyed["gamma"];
    matrix_strings[1][0] = "one-zero";
    matrix_strings[0][1] = "zero-one";
    assert (matrix_strings[1][0] == "one-zero");
    assert (matrix_strings[0][1] == "zero-one");
    nested_strings[1] = new[2];
    nested_strings[1][0] = "nested-zero";
    nested_strings[1][1] = "nested-one";
    assert (nested_strings[1][0] == "nested-zero");
    assert (nested_strings[1][1] == "nested-one");
    nested_copy = nested_strings;
    assert (nested_copy == nested_strings);
    state_copy = states;
    assert (state_copy == states);
    scalar_container_observed = 8'ha5;
    anonymous_default_observed = anonymous_default;
    anonymous_pattern = '{
      header: 32'h1234_5678,
      body: '{payload: {
        32'h0011_2233, 32'h4455_6677, 32'h8899_aabb
      }, default: 1'b1},
      default: '0
    };
    anonymous_pattern_observed = anonymous_pattern;
    anonymous_update = anonymous_pattern;
    anonymous_update.body.payload[95:88] = 8'hfe;
    anonymous_update.body.valid = 1'b0;
    anonymous_update.tail = 8'h3c;
    anonymous_update_observed = anonymous_update;
    anonymous_selected_observed = {
      anonymous_update.header,
      anonymous_update.body.valid,
      anonymous_update.tail
    };
    ordinary = '{mirror: 16'h00ab};
    ordinary_mirror_observed = ordinary;
    ordinary = '{wide: 16'hcdef};
    ordinary_wide_observed = ordinary;
    ordinary.mirror = 16'h005e;
    ordinary_selected_observed = ordinary;
    tagged_default_observed = tagged_default;
    tagged_narrow = tagged narrow 8'h5a;
    tagged_same = tagged narrow 8'h5a;
    tagged_wide = tagged wide 16'h1234;
    tagged_narrow_observed = tagged_narrow;
    tagged_wide_constructor_observed = tagged_wide;
    tagged_wide.narrow = 8'h7c;
    tagged_wide_observed = tagged_wide;
    tagged_active_observed = tagged_narrow.narrow;
    tagged_inactive_observed = tagged_narrow.wide;
    tagged_compare_observed = {
      tagged_narrow === tagged_same,
      tagged_narrow !== tagged_wide,
      tagged_narrow == tagged_same,
      tagged_narrow != tagged_wide
    };
    tagged_casted = tagged_t'(17'h1_00a5);
    tagged_cast_observed = tagged_casted;
    initialized_default_observed = initialized_default;
    nested_constant_observed = NESTED_CONSTANT;
    anonymous_enum_default_observed = anonymous_enum_value;
    aggregate_bits_observed = $bits(initialized_t);
    aggregate_left_observed = $left(initialized_t);
    aggregate_right_observed = $right(initialized_t);
    aggregate_size_observed = $size(initialized_t);
    aggregate_dimensions_observed = $dimensions(initialized_t);
    aggregate_unpacked_dimensions_observed =
      $unpacked_dimensions(initialized_t);
    controls[1].metadata.nibble = 4'hd;
    controls[1].code = 8'ha5;
    controls[1].count = 7;
    controls[0] = controls[1];
    controls[1].metadata.nibble = 4'h2;
    controls[1].code = 8'h3c;
    controls[1].count = 11;
    unpacked_code_observed = controls[0].code;
    unpacked_nested_observed = controls[0].metadata.nibble;
    unpacked_count_observed = controls[0].count;
    assert ($isunknown(anonymous_controls[0].code));
    assert ($isunknown(choices[0].primary));
    anonymous_controls[0].code = 8'h6e;
    anonymous_controls[0].metadata.nibble = 4'hb;
    anonymous_controls[1] = anonymous_controls[0];
    anonymous_controls[0].code = 8'h12;
    anonymous_unpacked_code_observed = anonymous_controls[1].code;
    anonymous_unpacked_nested_observed =
      anonymous_controls[1].metadata.nibble;
    choices[0].primary = 8'hc7;
    choices[1] = choices[0];
    choices[0].primary = 8'h31;
    named_union_observed = choices[1].alias_value;
    anonymous_choices[0].alias_value = 8'h59;
    anonymous_choices[1] = anonymous_choices[0];
    anonymous_choices[0].primary = 8'h24;
    anonymous_union_observed = anonymous_choices[1].primary;
    control_patterns = '{
      1: '{
        metadata: '{nibble: 4'ha},
        code: 8'hb1,
        count: 5
      },
      default: '{
        metadata: '{default: 4'hc},
        code: 8'hd2,
        default: 9
      }
    };
    control_pattern_observed[87:84] =
      control_patterns[1].metadata.nibble;
    control_pattern_observed[83:76] = control_patterns[1].code;
    control_pattern_observed[75:44] = control_patterns[1].count;
    control_pattern_observed[43:40] =
      control_patterns[0].metadata.nibble;
    control_pattern_observed[39:32] = control_patterns[0].code;
    control_pattern_observed[31:0] = control_patterns[0].count;
    control_pattern_copy = control_patterns;
    control_pattern_copy_observed[43:40] =
      control_pattern_copy[1].metadata.nibble;
    control_pattern_copy_observed[39:32] =
      control_pattern_copy[1].code;
    control_pattern_copy_observed[31:0] =
      control_pattern_copy[1].count;
    control_patterns[1].metadata.nibble = 4'h1;
    assert (control_pattern_copy != control_patterns);
    assert (control_pattern_copy !== control_patterns);
    unknown_control_copy = unknown_controls;
    assert ((unknown_control_copy == unknown_controls) === 1'bx);
    assert (unknown_control_copy === unknown_controls);
    dynamic_controls = '{
      '{'{4'h3}, 8'h11, 17},
      '{'{4'h4}, 8'h22, 33}
    };
    dynamic_control_copy = dynamic_controls;
    dynamic_controls[1].code = 8'hee;
    assert (dynamic_control_copy != dynamic_controls);
    dynamic_control_observed[43:40] =
      dynamic_control_copy[1].metadata.nibble;
    dynamic_control_observed[39:32] =
      dynamic_control_copy[1].code;
    dynamic_control_observed[31:0] =
      dynamic_control_copy[1].count;
    pending_controls = '{
      '{'{4'h7}, 8'h88, 19},
      '{'{4'h8}, 8'h99, 21}
    };
    pending_control_observed[43:40] =
      pending_controls[1].metadata.nibble;
    pending_control_observed[39:32] = pending_controls[1].code;
    pending_control_observed[31:0] = pending_controls[1].count;
    associative_controls = '{
      -2: '{
        metadata: '{nibble: 4'h6},
        code: 8'h77,
        count: 14
      },
      7: '{
        metadata: '{nibble: 4'h5},
        code: 8'h66,
        count: 12
      }
    };
    associative_control_copy = associative_controls;
    assert (associative_control_copy == associative_controls);
    associative_controls[-2].code = 8'h12;
    associative_controls.delete(7);
    assert (associative_controls.size() == 1);
    assert (associative_control_copy.size() == 2);
    associative_control_observed[43:40] =
      associative_control_copy[-2].metadata.nibble;
    associative_control_observed[39:32] =
      associative_control_copy[-2].code;
    associative_control_observed[31:0] =
      associative_control_copy[-2].count;
    patterned_choices = '{
      '{primary: 8'ha5},
      '{alias_value: 8'h5a}
    };
    unpacked_union_pattern_observed = {
      patterned_choices[1].alias_value,
      patterned_choices[0].primary
    };
    aggregate_query_matrix_observed[159:128] = $bits(control_t);
    aggregate_query_matrix_observed[127:96] =
      $bits(control_pattern_copy);
    aggregate_query_matrix_observed[95:64] =
      $bits(dynamic_control_copy);
    aggregate_query_matrix_observed[63:32] =
      $dimensions(control_pattern_copy);
    aggregate_query_matrix_observed[31:0] =
      $unpacked_dimensions(control_pattern_copy);
    aggregate_compare_matrix_observed[7] =
      control_pattern_copy == control_patterns;
    aggregate_compare_matrix_observed[6] =
      control_pattern_copy != control_patterns;
    aggregate_compare_matrix_observed[5] =
      control_pattern_copy === control_patterns;
    aggregate_compare_matrix_observed[4] =
      control_pattern_copy !== control_patterns;
    aggregate_compare_matrix_observed[3] =
      unknown_control_copy == unknown_controls;
    aggregate_compare_matrix_observed[2] =
      unknown_control_copy != unknown_controls;
    aggregate_compare_matrix_observed[1] =
      unknown_control_copy === unknown_controls;
    aggregate_compare_matrix_observed[0] =
      unknown_control_copy !== unknown_controls;
    #2 $finish;
  end
endmodule
)";
  assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-aggregate-multidimensional";
  config.project.top = "sv:work.aggregate_multidimensional_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.span.path << ':'
                << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << ": "
                << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  constexpr std::array<std::string_view, 50> names {
      "aggregate_multidimensional_top.aggregate_observed",
      "aggregate_multidimensional_top.matrix_observed",
      "aggregate_multidimensional_top.dynamic_observed",
      "aggregate_multidimensional_top.child_observed",
      "aggregate_multidimensional_top.aggregate_child_observed",
      "aggregate_multidimensional_top.container_observed",
      "aggregate_multidimensional_top.packet_child_observed",
      "aggregate_multidimensional_top.scalar_container_observed",
      "aggregate_multidimensional_top.anonymous_default_observed",
      "aggregate_multidimensional_top.anonymous_pattern_observed",
      "aggregate_multidimensional_top.anonymous_update_observed",
      "aggregate_multidimensional_top.anonymous_selected_observed",
      "aggregate_multidimensional_top.ordinary_mirror_observed",
      "aggregate_multidimensional_top.ordinary_wide_observed",
      "aggregate_multidimensional_top.ordinary_selected_observed",
      "aggregate_multidimensional_top.tagged_default_observed",
      "aggregate_multidimensional_top.tagged_narrow_observed",
      "aggregate_multidimensional_top.tagged_wide_constructor_observed",
      "aggregate_multidimensional_top.tagged_wide_observed",
      "aggregate_multidimensional_top.tagged_cast_observed",
      "aggregate_multidimensional_top.tagged_active_observed",
      "aggregate_multidimensional_top.tagged_inactive_observed",
      "aggregate_multidimensional_top.tagged_compare_observed",
      "aggregate_multidimensional_top.initialized_default_observed",
      "aggregate_multidimensional_top.nested_constant_observed",
      "aggregate_multidimensional_top.anonymous_enum_default_observed",
      "aggregate_multidimensional_top.aggregate_bits_observed",
      "aggregate_multidimensional_top.aggregate_left_observed",
      "aggregate_multidimensional_top.aggregate_right_observed",
      "aggregate_multidimensional_top.aggregate_size_observed",
      "aggregate_multidimensional_top.aggregate_dimensions_observed",
      "aggregate_multidimensional_top.aggregate_unpacked_dimensions_observed",
      "aggregate_multidimensional_top.subarray_observed",
      "aggregate_multidimensional_top.unpacked_code_observed",
      "aggregate_multidimensional_top.unpacked_nested_observed",
      "aggregate_multidimensional_top.unpacked_count_observed",
      "aggregate_multidimensional_top.anonymous_unpacked_code_observed",
      "aggregate_multidimensional_top.anonymous_unpacked_nested_observed",
      "aggregate_multidimensional_top.named_union_observed",
      "aggregate_multidimensional_top.anonymous_union_observed",
      "aggregate_multidimensional_top.control_pattern_observed",
      "aggregate_multidimensional_top.control_pattern_copy_observed",
      "aggregate_multidimensional_top.dynamic_control_observed",
      "aggregate_multidimensional_top.pending_control_observed",
      "aggregate_multidimensional_top.associative_control_observed",
      "aggregate_multidimensional_top.aggregate_query_matrix_observed",
      "aggregate_multidimensional_top.aggregate_compare_matrix_observed",
      "aggregate_multidimensional_top.unpacked_union_pattern_observed",
      "aggregate_multidimensional_top.string_index_observed",
      "aggregate_multidimensional_top.control_child_observed"
  };
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::array<fsim::runtime::VcdSignal, names.size()> traces{};
  std::ostringstream vcd_text;
  fsim::runtime::VcdWriter vcd{vcd_text, "1ns", 32};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{names[index]},
        static_cast<std::uint32_t>(
            simulation.read_signal(*signal).width()));
  }
  vcd.begin(simulation.now());
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        for (std::size_t index = 0; index < signals.size(); ++index) {
          if (signals[index] == signal) {
            vcd.set_time(time);
            vcd.change(traces[index], value);
          }
        }
      });
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  vcd.flush();
  capture.vcd = vcd_text.str();
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "matrix"});
  debugger.execute({"show", "packet_matrix"});
  debugger.execute({"show", "string_copy"});
  debugger.execute({"show", "nested_copy"});
  debugger.execute({"show", "state_copy"});
  debugger.execute({"show", "controls"});
  debugger.execute({"show", "anonymous_controls"});
  debugger.execute({"show", "choices"});
  debugger.execute({"show", "anonymous_choices"});
  debugger.execute({"show", "control_pattern_copy"});
  debugger.execute({"show", "dynamic_control_copy"});
  debugger.execute({"show", "pending_controls"});
  debugger.execute({"show", "associative_control_copy"});
  debugger.execute({"show", "patterned_choices"});
  debugger.execute({"show", "string_keyed_copy"});
  assert(debugger_error.str().empty());
  capture.debugger = debugger_output.str();
  return capture;
}

void verify(
    const Capture& capture,
    const char replacement) {
  const auto replacement_bits =
      replacement == '9' ? std::string{"1001"} : std::string{"1010"};
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 2);
  assert(capture.values[0] == "11010110");
  const auto expected_matrix =
      std::string{"0001"} + replacement_bits
      + "0011010001010110";
  if (capture.values[1] != expected_matrix) {
    std::cerr << "matrix observed " << capture.values[1]
              << ", expected " << expected_matrix << '\n';
  }
  assert(
      capture.values[1] == expected_matrix);
  assert(capture.values[2] == "0101");
  assert(capture.values[3] == replacement_bits);
  assert(capture.values[4] == "11010110");
  assert(
      capture.values[5]
      == "0001001000110101010001100101011101101000011111101000101011001101");
  assert(capture.values[6] == "01111110");
  assert(capture.values[7] == "10100101");
  assert(
      capture.values[8]
      == "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
         "0000000000000000000000000000000000000000000000000000000000000000"
         "00000000000000000000000000000000"
         "X"
         "00000000");
  assert(
      capture.values[9]
      == "00010010001101000101011001111000"
         "0000000000010001001000100011001101000100010101010110011001110111"
         "10001000100110011010101010111011"
         "1"
         "00000000");
  assert(
      capture.values[10]
      == "00010010001101000101011001111000"
         "1111111000010001001000100011001101000100010101010110011001110111"
         "10001000100110011010101010111011"
         "0"
         "00111100");
  assert(
      capture.values[11]
      == "00010010001101000101011001111000000111100");
  assert(capture.values[12] == "0000000010101011");
  assert(capture.values[13] == "1100110111101111");
  assert(capture.values[14] == "0000000001011110");
  assert(capture.values[15] == "0XXXXXXXXXXXXXXXX");
  assert(capture.values[16] == "10000000001011010");
  assert(capture.values[17] == "00001001000110100");
  assert(capture.values[18] == "10000000001111100");
  assert(capture.values[19] == "10000000010100101");
  assert(capture.values[20] == "01011010");
  assert(capture.values[21] == "XXXXXXXXXXXXXXXX");
  assert(capture.values[22] == "1111");
  assert(capture.values[23] == "10100101101");
  assert(capture.values[24] == "110110");
  assert(capture.values[25] == "00000000000000000000000000000000");
  assert(capture.values[26] == "00000000000000000000000000001011");
  assert(capture.values[27] == "00000000000000000000000000001010");
  assert(capture.values[28] == "00000000000000000000000000000000");
  assert(capture.values[29] == "00000000000000000000000000001011");
  assert(capture.values[30] == "00000000000000000000000000000001");
  assert(capture.values[31] == "00000000000000000000000000000000");
  const auto copied_row =
      std::string{"00010001"} + replacement_bits;
  const auto patched_row = std::string{"000100011101"};
  assert(
      capture.values[32]
      == copied_row + patched_row + copied_row + patched_row);
  assert(capture.values[33] == "10100101");
  assert(capture.values[34] == "1101");
  assert(capture.values[35] == "00000000000000000000000000000111");
  assert(capture.values[36] == "01101110");
  assert(capture.values[37] == "1011");
  if (capture.values[38] != "11000111"
      || capture.values[39] != "01011001") {
    std::cerr << "unpacked unions observed named="
              << capture.values[38] << ", anonymous="
              << capture.values[39] << '\n';
  }
  assert(capture.values[38] == "11000111");
  assert(capture.values[39] == "01011001");
  assert(
      capture.values[40]
      == "1010"
         "10110001"
         "00000000000000000000000000000101"
         "1100"
         "11010010"
         "00000000000000000000000000001001");
  assert(
      capture.values[41]
      == "1010"
         "10110001"
         "00000000000000000000000000000101");
  assert(
      capture.values[42]
      == "0100"
         "00100010"
         "00000000000000000000000000100001");
  assert(
      capture.values[43]
      == "1000"
         "10011001"
         "00000000000000000000000000010101");
  assert(
      capture.values[44]
      == "0110"
         "01110111"
         "00000000000000000000000000001110");
  assert(
      capture.values[45]
      == "00000000000000000000000000101100"
         "00000000000000000000000001011000"
         "00000000000000000000000001011000"
         "00000000000000000000000000000001"
         "00000000000000000000000000000001");
  assert(capture.values[46] == "0101XX10");
  assert(capture.values[47] == "1010010101011010");
  assert(capture.values[48]
         == "00000000000000000000000000000100");
  assert(
      capture.values[49]
      == "1101"
         "10100101"
         "00000000000000000000000000000111");
  assert(capture.vcd.find("#1") != std::string::npos);
  assert(capture.debugger.find("matrix = [") != std::string::npos);
  assert(capture.debugger.find("packet_matrix = [") != std::string::npos);
  assert(
      capture.debugger.find(
          "string_copy = [\"zero\", \"changed\", \"\"]")
          != std::string::npos);
  assert(
      capture.debugger.find(
          "nested_copy = [1:[\"nested-zero\", \"nested-one\"], 0:[]]")
          != std::string::npos);
  assert(
      capture.debugger.find("state_copy = [1:{weight=0")
          != std::string::npos);
  assert(capture.debugger.find("controls = [") != std::string::npos);
  assert(
      capture.debugger.find("anonymous_controls = [")
          != std::string::npos);
  assert(capture.debugger.find("choices = [") != std::string::npos);
  assert(
      capture.debugger.find("anonymous_choices = [")
          != std::string::npos);
  assert(
      capture.debugger.find("control_pattern_copy = [")
          != std::string::npos);
  assert(
      capture.debugger.find("dynamic_control_copy = [")
          != std::string::npos);
  assert(
      capture.debugger.find("pending_controls = [")
          != std::string::npos);
  assert(
      capture.debugger.find("associative_control_copy = [")
          != std::string::npos);
  assert(
      capture.debugger.find("patterned_choices = [")
          != std::string::npos);
  assert(
      capture.debugger.find(
          "string_keyed_copy = [\"alpha\"=>"
          "00000000000000000000000000000001, "
          "\"beta\"=>00000000000000000000000000000010, "
          "\"gamma\"=>00000000000000000000000000000011]")
          != std::string::npos);
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-aggregate-multidimensional-"
         + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "aggregate_multidimensional.sv";
  write_source(source, '9');
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference = execute(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, '9');
    verify(cold, '9');
    verify(warm, '9');
    assert(reference.vcd == cold.vcd && cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled > 0 && cold.cache.misses > 0);
    assert(warm.cache.hits > 0);
#else
    assert(cold.compiled == 0 && warm.compiled == 0);
#endif
  }
  write_source(source, 'a');
  const auto edited = execute(
      make_config(
          directory.path, source, fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(edited, 'a');
#if defined(FSIM_HAS_LLVM)
  assert(edited.cache.misses > 0);
#endif
  return 0;
}
