// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
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

struct Change {
  std::string signal;
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::vector<std::pair<std::string, std::string>> final_values;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& hdl_source,
    const std::filesystem::path& systemc_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "systemc-datatypes";
  config.project.top = "sv:work.datatype_host";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet hdl_sources;
  hdl_sources.language = fsim::project::Language::system_verilog;
  hdl_sources.standard = "2017";
  hdl_sources.library = "work";
  hdl_sources.files.push_back(hdl_source);
  config.source_sets.push_back(std::move(hdl_sources));

  fsim::project::SourceSet systemc_sources;
  systemc_sources.language = fsim::project::Language::systemc;
  systemc_sources.library = "models";
  systemc_sources.standard = "2023-subset";
  systemc_sources.files.push_back(systemc_source);
  systemc_sources.include_directories.emplace_back(
      std::filesystem::path{FSIM_TEST_SOURCE_DIR} / "include");
  config.source_sets.push_back(std::move(systemc_sources));

  config.bindings = {
      {
          "datatype_host.model",
          "systemc:models.datatypes",
          std::nullopt},
  };
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
      for (const auto& note : diagnostic.notes) {
        std::cerr << note.message << '\n';
      }
    }
  }
  assert(project);
  assert(project->systemc_plugins.size() == 1);
  assert(project->design.systemc_instances().size() == 1);

  constexpr std::array<std::string_view, 5> names{
      "datatype_host.bit_result",
      "datatype_host.unsigned_result",
      "datatype_host.signed_result",
      "datatype_host.logic_result",
      "datatype_host.scalar_result",
  };
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = project->design.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ns", 128);
  std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto scalar =
        names[index].find("scalar") != std::string_view::npos;
    vcd_signals[index] = vcd.declare_signal(
        std::string{names[index]}, scalar ? 1U : 8U);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < names.size(); ++index) {
    vcd.change(
        vcd_signals[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        capture.changes.push_back({
            std::string{names[index]},
            value.to_msb_string(),
            time,
            delta,
        });
        vcd.set_time(time);
        vcd.change(vcd_signals[index], value);
      });

  capture.result = simulation.run();
  for (std::size_t index = 0; index < names.size(); ++index) {
    capture.final_values.emplace_back(
        names[index],
        simulation.read_signal(signals[index]).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& hdl_source,
    const std::filesystem::path& systemc_source,
    const fsim::project::Optimization optimization) {
  const auto config =
      make_config(directory, hdl_source, systemc_source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 1);
  assert((
      reference.final_values
      == std::vector<std::pair<std::string, std::string>>{
          {"datatype_host.bit_result", "01010000"},
          {"datatype_host.unsigned_result", "10001001"},
          {"datatype_host.signed_result", "11101111"},
          {"datatype_host.logic_result", "10XX0011"},
          {"datatype_host.scalar_result", "1"},
      }));
  assert(reference.vcd.find("$timescale 1ns $end")
         != std::string::npos);
  assert(reference.vcd.find("b10xx0011")
         != std::string::npos);

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.result.delta == actual->result.delta);
    assert(reference.changes == actual->changes);
    assert(reference.final_values == actual->final_values);
    assert(reference.vcd == actual->vcd);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 1);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-systemc-datatype-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);

  const auto hdl_source = directory.path / "datatype_host.sv";
  {
    std::ofstream output(hdl_source, std::ios::binary);
    output << R"(module datatype_host;
  bit [7:0] bit_result;
  logic [7:0] unsigned_result;
  logic signed [7:0] signed_result;
  logic [7:0] logic_result;
  logic scalar_result;

  datatype_model model(
    .bit_result(bit_result),
    .unsigned_result(unsigned_result),
    .signed_result(signed_result),
    .logic_result(logic_result),
    .scalar_result(scalar_result)
  );

  initial #1 $finish;
endmodule
)";
  }

  const auto systemc_source = directory.path / "datatypes.cpp";
  {
    std::ofstream output(systemc_source, std::ios::binary);
    output << R"(#include <systemc>

SC_MODULE(DatatypeModel) {
  sc_core::sc_out<sc_dt::sc_bv<8>> bit_result{"bit_result"};
  sc_core::sc_out<sc_dt::sc_uint<8>> unsigned_result{"unsigned_result"};
  sc_core::sc_out<sc_dt::sc_int<8>> signed_result{"signed_result"};
  sc_core::sc_out<sc_dt::sc_lv<8>> logic_result{"logic_result"};
  sc_core::sc_out<sc_dt::sc_logic> scalar_result{"scalar_result"};

  SC_CTOR(DatatypeModel) {
    SC_METHOD(compute);
  }

  void compute() {
    auto bits = sc_dt::sc_bv<8>{UINT64_C(0xa5)};
    bits[1] = true;
    bit_result.write((bits ^ sc_dt::sc_bv<8>{UINT64_C(0x0f)}) << 1);

    auto unsigned_value = sc_dt::sc_uint<8>{250};
    unsigned_value += 10;
    unsigned_value <<= 1;
    unsigned_value |= sc_dt::sc_uint<8>{UINT64_C(0x81)};
    unsigned_result.write(unsigned_value);

    auto signed_value = sc_dt::sc_int<8>{-100};
    signed_value /= 3;
    signed_value >>= 1;
    signed_result.write(signed_value);

    auto logic_value =
        sc_dt::sc_lv<8>{"10XZ0101"}
        & sc_dt::sc_lv<8>{"11110000"};
    logic_value |= sc_dt::sc_lv<8>{"00000011"};
    logic_result.write(logic_value);

    auto scalar = sc_dt::sc_logic{'Z'};
    scalar &= sc_dt::sc_logic{'0'};
    scalar ^= sc_dt::sc_logic{'1'};
    scalar_result.write(scalar);
  }
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION) {
    return FSIM_SC_ABI_MISMATCH;
  }
  return fsim::systemc::register_module_factory<DatatypeModel>(
      host, registrar, "datatypes");
}
)";
  }

  verify_mode(
      directory.path,
      hdl_source,
      systemc_source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      hdl_source,
      systemc_source,
      fsim::project::Optimization::o2);
  return 0;
}
