// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <cassert>
#include <csignal>
#include <sstream>
#include <utility>

namespace fsim::test {

volatile std::sig_atomic_t restored_interrupt_count = 0;

extern "C" void record_restored_interrupt(int) {
  restored_interrupt_count = 1;
}

std::streamsize InterruptingOutputBuffer::xsputn(
    const char* value,
    const std::streamsize count) {
  const auto written = std::stringbuf::xsputn(value, count);
  if (!raised_ && str().find("(fsim) ") != std::string::npos) {
    raised_ = true;
    (void)std::raise(SIGINT);
  }
  return written;
}

CapturedSimulation ApplicationTestFixture::capture_simulation(
    app::BuiltProject project,
    const app::SimulationEngine engine,
    const std::optional<runtime::SimulationTick> until) const {
  CapturedSimulation captured;
  captured.process_count = project.design.processes().size();
  app::Simulation candidate(
  std::move(project), base_config().run.max_deltas, engine);
  captured.compiled_processes =
          candidate.compiled_process_count();
  captured.compiled_modules =
          candidate.compiled_module_count();
  captured.native_cache =
          candidate.native_cache_statistics();
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ns", 256);
  std::vector<fsim::runtime::VcdSignal> vcd_signals;
      vcd_signals.reserve(candidate.design().signals().size());
  for (const auto& signal : candidate.design().signals()) {
        vcd_signals.push_back(
            vcd.declare_signal(signal.name, signal.width));
      }
      vcd.begin(candidate.now());
  for (const auto& signal : candidate.design().signals()) {
        vcd.change(
            vcd_signals.at(signal.id),
            candidate.read_signal(signal.id));
      }
      candidate.set_signal_change_hook(
          [&captured, &vcd, &vcd_signals](
              const fsim::runtime::simir::SignalId signal,
              const fsim::runtime::PackedLogic4& value,
              const fsim::runtime::SimulationTick time,
              const std::uint64_t delta) {
        captured.changes.emplace_back(
                signal, value.to_msb_string(), time, delta);
            vcd.set_time(time);
            vcd.change(vcd_signals.at(signal), value);
          });
  captured.result = candidate.run(until);
      vcd.flush();
  captured.normalized_vcd = vcd_output.str();
  captured.final_values.reserve(
          candidate.design().signals().size());
  for (const auto& signal : candidate.design().signals()) {
    captured.final_values.push_back(
            candidate.read_signal(signal.id).to_msb_string());
      }
  return captured;
}

void ApplicationTestFixture::compare_captures(
    const CapturedSimulation& reference,
    const CapturedSimulation& hybrid) {
      assert(reference.result.status == hybrid.result.status);
      assert(reference.result.time == hybrid.result.time);
      assert(reference.result.delta == hybrid.result.delta);
      assert(
          reference.result.callbacks_executed
          == hybrid.result.callbacks_executed);
      assert(reference.changes == hybrid.changes);
      assert(reference.final_values == hybrid.final_values);
      assert(reference.normalized_vcd == hybrid.normalized_vcd);
      assert(
          reference.normalized_vcd.find("$version fsim $end")
          != std::string::npos);
      assert(reference.compiled_processes == 0);
      assert(reference.compiled_modules == 0);
      assert(
          reference.native_cache
          == fsim::app::NativeCacheStatistics{});
#if defined(FSIM_HAS_LLVM)
      assert(hybrid.compiled_processes > 0);
      assert(
          hybrid.compiled_processes
          <= hybrid.process_count);
      assert(hybrid.compiled_modules > 0);
      assert(
          hybrid.compiled_modules
          <= hybrid.compiled_processes);
#else
      assert(hybrid.compiled_processes == 0);
      assert(hybrid.compiled_modules == 0);
#endif
}

}  // namespace fsim::test
