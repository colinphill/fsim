// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
  std::array<std::string, 44> values;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
  std::string debugger;
  std::string vcd;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& child_source,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-parameter-sizing";
  config.project.top = "sv:work.sized_parameter_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language =
      fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files.push_back(child_source);
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design.specializations().size() == 4);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 44> paths{
      "sized_parameter_top.default_signed_byte",
      "sized_parameter_top.default_unsigned_byte",
      "sized_parameter_top.default_short",
      "sized_parameter_top.default_signed_vector",
      "sized_parameter_top.default_unsigned_vector",
      "sized_parameter_top.default_unsigned_int",
      "sized_parameter_top.override_signed_byte",
      "sized_parameter_top.override_unsigned_byte",
      "sized_parameter_top.override_short",
      "sized_parameter_top.override_signed_vector",
      "sized_parameter_top.override_unsigned_vector",
      "sized_parameter_top.override_unsigned_int",
      "sized_parameter_top.typed_max",
      "sized_parameter_top.typed_minus_one",
      "sized_parameter_top.typed_mixed_width",
      "sized_parameter_top.typed_unknown",
      "sized_parameter_top.short_and_count",
      "sized_parameter_top.short_or_count",
      "sized_parameter_top.true_count",
      "sized_parameter_top.false_count",
      "sized_parameter_top.unknown_count",
      "sized_parameter_top.unknown_merge",
      "sized_parameter_top.dynamic_plus",
      "sized_parameter_top.dynamic_minus",
      "sized_parameter_top.dynamic_partial",
      "sized_parameter_top.dynamic_unknown",
      "sized_parameter_top.ascending_plus",
      "sized_parameter_top.ascending_minus",
      "sized_parameter_top.part_select_count",
      "sized_parameter_top.stream_bits",
      "sized_parameter_top.stream_pairs",
      "sized_parameter_top.stream_right",
      "sized_parameter_top.stream_partial",
      "sized_parameter_top.stream_constant",
      "sized_parameter_top.context_add",
      "sized_parameter_top.context_shift",
      "sized_parameter_top.context_multiply",
      "sized_parameter_top.context_unary",
      "sized_parameter_top.context_unbased_one",
      "sized_parameter_top.context_unbased_x",
      "sized_parameter_top.context_conditional",
      "sized_parameter_top.context_power",
      "sized_parameter_top.context_concat",
      "sized_parameter_top.context_replication"};
  std::array<fsim::runtime::simir::SignalId, paths.size()> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  const auto dynamic_trace = vcd.declare_signal(
      "sized_parameter_top.dynamic_plus", 4);
  const auto stream_trace = vcd.declare_signal(
      "sized_parameter_top.stream_pairs", 8);
  const auto context_trace = vcd.declare_signal(
      "sized_parameter_top.context_add", 17);
  vcd.begin(simulation.now());
  vcd.change(dynamic_trace, simulation.read_signal(signals[22]));
  vcd.change(stream_trace, simulation.read_signal(signals[30]));
  vcd.change(context_trace, simulation.read_signal(signals[34]));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        if (signal == signals[22]) {
          vcd.set_time(time);
          vcd.change(dynamic_trace, value);
        } else if (signal == signals[30]) {
          vcd.set_time(time);
          vcd.change(stream_trace, value);
        } else if (signal == signals[34]) {
          vcd.set_time(time);
          vcd.change(context_trace, value);
        }
      });

  capture.result = simulation.run();
  vcd.flush();
  capture.vcd = vcd_output.str();
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "dynamic_plus"});
  debugger.execute({"show", "stream_pairs"});
  debugger.execute({"show", "context_add"});
  assert(debugger_error.str().empty());
  capture.debugger = debugger_output.str();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert(
      capture.debugger.find(
          "sized_parameter_top.dynamic_plus = 1100")
      != std::string::npos);
  assert(
      capture.debugger.find(
          "sized_parameter_top.stream_pairs = 10000111")
      != std::string::npos);
  assert(
      capture.debugger.find(
          "sized_parameter_top.context_add = 10000000000000000")
      != std::string::npos);
  assert(
      capture.vcd.find("dynamic_plus")
          != std::string::npos
      && capture.vcd.find("b1100") != std::string::npos);
  assert(
      capture.vcd.find("stream_pairs")
          != std::string::npos
      && capture.vcd.find("b10000111") != std::string::npos);
  assert(
      capture.vcd.find("context_add")
          != std::string::npos
      && capture.vcd.find("b10000000000000000")
          != std::string::npos);
  assert((
      capture.values
      == std::array<std::string, 44>{
          "11111111111111111111111111111111",
          "00000000000000000000000011111111",
          "11111111111111111000000000000000",
          "11111111111111111111111110000000",
          "00000000000000000000000011111111",
          "11111111111111111111111111111111",
          "11111111111111111111111110000000",
          "00000000000000000000000011111110",
          "11111111111111111111111111111111",
          "11111111111111111111111111111111",
          "00000000000000000000000011111110",
          "11111111111111111111111111111110",
          "1111111111111111111111111111111111111111111111111111111111111111",
          "1111111111111111111111111111111111111111111111111111111111111110",
          "00010000",
          "10X1",
          "00000000000000000000000000000000",
          "00000000000000000000000000000000",
          "00000000000000000000000000000001",
          "00000000000000000000000000000010",
          "00000000000000000000000000000100",
          "X",
          "1100",
          "1100",
          "XX10",
          "XXXX",
          "1011",
          "1011",
          "00000000000000000000000000000001",
          "10110011",
          "10000111",
          "10100101",
          "01001011",
          "10000111",
          "10000000000000000",
          "10000000000000000",
          "0000000111111110",
          "0000000010000000",
          "11111111111111111",
          "XXXXXXXXXXXXXXXXX",
          "00000000011111111",
          "0000000100000000",
          "0000000010100101",
          "0000000000110011"}));
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-parameter-sizing-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto child_source =
      directory.path / "parameter_children.sv";
  const auto source = directory.path / "parameter_sizing.sv";
  {
    std::ofstream output(child_source, std::ios::binary);
    output << R"(
module sized_parameter_child #(
  parameter byte SIGNED_BYTE = 8'hff,
  parameter byte unsigned UNSIGNED_BYTE = -1,
  parameter shortint SHORT_VALUE = 16'h8000,
  parameter logic signed [7:0] SIGNED_VECTOR = 8'h80,
  parameter logic [7:0] UNSIGNED_VECTOR = -1,
  parameter int unsigned UNSIGNED_INT = -1
) (
  output logic [31:0] signed_byte_value,
  output logic [31:0] unsigned_byte_value,
  output logic [31:0] short_value,
  output logic [31:0] signed_vector_value,
  output logic [31:0] unsigned_vector_value,
  output logic [31:0] unsigned_int_value
);
  initial begin
    signed_byte_value = SIGNED_BYTE;
    unsigned_byte_value = UNSIGNED_BYTE;
    short_value = SHORT_VALUE;
    signed_vector_value = SIGNED_VECTOR;
    unsigned_vector_value = UNSIGNED_VECTOR;
    unsigned_int_value = UNSIGNED_INT;
  end
endmodule

module typed_constant_child #(
  parameter longint unsigned MAX_VALUE = 64'hffffffffffffffff,
  parameter longint unsigned MINUS_ONE = MAX_VALUE - 1,
  parameter logic [7:0] MIXED_WIDTH = 4'hf + 8'h01,
  parameter logic [3:0] UNKNOWN_VALUE = 4'b10x1
) (
  output logic [63:0] max_value,
  output logic [63:0] minus_one,
  output logic [7:0] mixed_width,
  output logic [3:0] unknown_value
);
  initial begin
    max_value = MAX_VALUE;
    minus_one = MINUS_ONE;
    mixed_width = MIXED_WIDTH;
    unknown_value = UNKNOWN_VALUE;
  end
endmodule
)";
    assert(output.good());
  }
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
module sized_parameter_top #(
  parameter longint unsigned TYPED_MAX = 64'hffffffffffffffff
);
  localparam logic [7:0] STREAM_CONSTANT = {<<2{8'hd2}};
  localparam logic [63:0] STREAM_WIDE =
      {>>{64'h0123456789abcdef}};
  logic [31:0] default_signed_byte;
  logic [31:0] default_unsigned_byte;
  logic [31:0] default_short;
  logic [31:0] default_signed_vector;
  logic [31:0] default_unsigned_vector;
  logic [31:0] default_unsigned_int;
  logic [31:0] override_signed_byte;
  logic [31:0] override_unsigned_byte;
  logic [31:0] override_short;
  logic [31:0] override_signed_vector;
  logic [31:0] override_unsigned_vector;
  logic [31:0] override_unsigned_int;
  logic [63:0] typed_max;
  logic [63:0] typed_minus_one;
  logic [7:0] typed_mixed_width;
  logic [3:0] typed_unknown;
  logic [31:0] call_count;
  logic [31:0] short_and_count;
  logic [31:0] short_or_count;
  logic [31:0] true_count;
  logic [31:0] false_count;
  logic [31:0] unknown_count;
  logic unknown_merge;
  logic [15:0] descending_source;
  logic [0:15] ascending_source;
  logic [31:0] dynamic_base;
  logic [3:0] dynamic_plus;
  logic [3:0] dynamic_minus;
  logic [3:0] dynamic_partial;
  logic [3:0] dynamic_unknown;
  logic [3:0] ascending_plus;
  logic [3:0] ascending_minus;
  logic [31:0] part_select_count;
  logic [7:0] stream_bits;
  logic [7:0] stream_pairs;
  logic [7:0] stream_right;
  logic [7:0] stream_partial;
  logic [7:0] stream_constant;
  logic [16:0] context_add;
  logic [16:0] context_shift;
  logic [15:0] context_multiply;
  logic [15:0] context_unary;
  logic [16:0] context_unbased_one;
  logic [16:0] context_unbased_x;
  logic [16:0] context_conditional;
  logic [15:0] context_power;
  logic [15:0] context_concat;
  logic [15:0] context_replication;

  function automatic logic counted(input logic value);
    begin
      call_count = call_count + 1;
      counted = value;
    end
  endfunction

  function automatic logic [31:0] counted_base(
    input logic [31:0] value
  );
    begin
      call_count = call_count + 1;
      counted_base = value;
    end
  endfunction

  sized_parameter_child defaults(
    default_signed_byte,
    default_unsigned_byte,
    default_short,
    default_signed_vector,
    default_unsigned_vector,
    default_unsigned_int
  );
  sized_parameter_child #(
    .SIGNED_BYTE(8'h80),
    .UNSIGNED_BYTE(-2),
    .SHORT_VALUE(16'hffff),
    .SIGNED_VECTOR(8'hff),
    .UNSIGNED_VECTOR(-2),
    .UNSIGNED_INT(-2)
  ) overrides(
    override_signed_byte,
    override_unsigned_byte,
    override_short,
    override_signed_vector,
    override_unsigned_vector,
    override_unsigned_int
  );
  typed_constant_child #(.MAX_VALUE(TYPED_MAX)) typed(
    typed_max,
    typed_minus_one,
    typed_mixed_width,
    typed_unknown
  );

  initial begin
    call_count = 0;
    unknown_merge = 1'b0 && counted(1'b1);
    short_and_count = call_count;
    unknown_merge = 1'b1 || counted(1'b0);
    short_or_count = call_count;
    unknown_merge = 1'b1 ? counted(1'b1) : counted(1'b0);
    true_count = call_count;
    unknown_merge = 1'b0 ? counted(1'b1) : counted(1'b0);
    false_count = call_count;
    unknown_merge = 1'bx ? counted(1'b1) : counted(1'b0);
    unknown_count = call_count;
    descending_source = 16'habcd;
    ascending_source = 16'habcd;
    call_count = 0;
    dynamic_plus = descending_source[counted_base(4) +: 4];
    part_select_count = call_count;
    dynamic_base = 7;
    dynamic_minus = descending_source[dynamic_base -: 4];
    dynamic_base = 14;
    dynamic_partial = descending_source[dynamic_base +: 4];
    dynamic_base = 32'bx;
    dynamic_unknown = descending_source[dynamic_base +: 4];
    dynamic_base = 4;
    ascending_plus = ascending_source[dynamic_base +: 4];
    dynamic_base = 7;
    ascending_minus = ascending_source[dynamic_base -: 4];
    stream_bits = {<<{descending_source[7:0]}};
    stream_pairs = {<<2{8'hd2}};
    stream_right = {>>4{{4'ha, 4'h5}}};
    stream_partial = {<<3{8'hd2}};
    stream_constant = STREAM_CONSTANT;
    context_add = 16'hffff + 1'b1;
    context_shift = 16'h8000 << 1;
    context_multiply = 8'hff * 8'h02;
    context_unary = -8'sh80;
    context_unbased_one = '1;
    context_unbased_x = 'x;
    context_conditional = 1'b1 ? 8'hff : 4'h0;
    context_power = 8'd2 ** 32'd8;
    context_concat = {4'ha, 4'h5};
    context_replication = {2{4'h3}};
    #1;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

  std::vector<std::string> baseline_o2_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config =
        make_config(
            directory.path,
            child_source,
            source,
            optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.values == cold.values);
    assert(reference.values == warm.values);
    assert(reference.specialization_keys == cold.specialization_keys);
    assert(cold.specialization_keys == warm.specialization_keys);
    if (optimization == fsim::project::Optimization::o2) {
      baseline_o2_keys = warm.specialization_keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 4);
    assert(cold.compiled_modules == 4);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 4);
    assert(cold.cache.stores == 4);
    assert(warm.cache.hits == 4);
    assert(warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }

  std::ifstream input(source, std::ios::binary);
  std::string changed_source{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  assert(input.good() || input.eof());
  constexpr std::string_view original_max{
      "64'hffffffffffffffff"};
  constexpr std::string_view changed_max{
      "64'hfffffffffffffffe"};
  const auto max_position = changed_source.find(original_max);
  assert(max_position != std::string::npos);
  changed_source.replace(
      max_position, original_max.size(), changed_max);
  {
    std::ofstream output(
        source, std::ios::binary | std::ios::trunc);
    output << changed_source;
    assert(output.good());
  }

  const auto changed_config = make_config(
      directory.path,
      child_source,
      source,
      fsim::project::Optimization::o2);
  const auto changed =
      run_once(changed_config, fsim::app::SimulationEngine::compiled);
  assert(changed.result.status == fsim::runtime::RunStatus::stopped);
  assert((
      changed.values
      == std::array<std::string, 44>{
          "11111111111111111111111111111111",
          "00000000000000000000000011111111",
          "11111111111111111000000000000000",
          "11111111111111111111111110000000",
          "00000000000000000000000011111111",
          "11111111111111111111111111111111",
          "11111111111111111111111110000000",
          "00000000000000000000000011111110",
          "11111111111111111111111111111111",
          "11111111111111111111111111111111",
          "00000000000000000000000011111110",
          "11111111111111111111111111111110",
          "1111111111111111111111111111111111111111111111111111111111111110",
          "1111111111111111111111111111111111111111111111111111111111111101",
          "00010000",
          "10X1",
          "00000000000000000000000000000000",
          "00000000000000000000000000000000",
          "00000000000000000000000000000001",
          "00000000000000000000000000000010",
          "00000000000000000000000000000100",
          "X",
          "1100",
          "1100",
          "XX10",
          "XXXX",
          "1011",
          "1011",
          "00000000000000000000000000000001",
          "10110011",
          "10000111",
          "10100101",
          "01001011",
          "10000111",
          "10000000000000000",
          "10000000000000000",
          "0000000111111110",
          "0000000010000000",
          "11111111111111111",
          "XXXXXXXXXXXXXXXXX",
          "00000000011111111",
          "0000000100000000",
          "0000000010100101",
          "0000000000110011"}));
  assert(baseline_o2_keys.size() == 4);
  assert(changed.specialization_keys.size() == 4);
  assert(changed.specialization_keys[0] != baseline_o2_keys[0]);
  assert(changed.specialization_keys[1] == baseline_o2_keys[1]);
  assert(changed.specialization_keys[2] == baseline_o2_keys[2]);
  assert(changed.specialization_keys[3] != baseline_o2_keys[3]);
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 2);
  assert(changed.cache.misses == 2);
  assert(changed.cache.stores == 2);
#endif
  return 0;
}
