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
  std::array<std::string, 7> values;
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
  typedef struct packed {
    logic [3:0] tag;
    logic [3:0] data;
  } packet_t;
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

import aggregate_types::*;
module aggregate_multidimensional_top #(
  parameter type VALUE_T = record_t,
  parameter type PACKET_T = packet_t
);
  VALUE_T aggregate;
  logic [3:0] matrix[1:0][0:2];
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
  int row;
  int column;

  function automatic logic [3:0] pick(
      input logic [3:0] value[1:0][0:2],
      input int selected_row,
      input int selected_column);
    return value[selected_row][selected_column];
  endfunction

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
    value[1][1] = 4'h)" << replacement << R"(;
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
  constexpr std::array<std::string_view, 7> names{
      "aggregate_multidimensional_top.aggregate_observed",
      "aggregate_multidimensional_top.matrix_observed",
      "aggregate_multidimensional_top.dynamic_observed",
      "aggregate_multidimensional_top.child_observed",
      "aggregate_multidimensional_top.aggregate_child_observed",
      "aggregate_multidimensional_top.container_observed",
      "aggregate_multidimensional_top.packet_child_observed"};
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
  assert(capture.vcd.find("#1") != std::string::npos);
  assert(capture.debugger.find("matrix = [") != std::string::npos);
  assert(capture.debugger.find("packet_matrix = [") != std::string::npos);
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
