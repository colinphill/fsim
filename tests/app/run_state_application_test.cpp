// SPDX-License-Identifier: Apache-2.0
//
// Run-state save and restore of a whole simulation on the reference evaluator
// and on engine v4 (#128; IEEE 1800-2023 D.9, IEEE 1076-2019 20.7-20.9). A run
// is stopped in the middle, saved, restored into a second simulation of the
// same design, and both continue; the saved run and the restored one must end
// in the same state. Lines marked "unverified" depend on scheduler and
// kernel code this test does not read.
#include "fsim/app/application.hpp"

#include "fsim/runtime/run_state_image.hpp"
#include "../../src/app/application_foreign_run_state.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "fsim/runtime/run_state_archive.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/runtime/vpi_callback.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

// The test reaches the interpreter of a simulation through the friend the
// application already grants for tests.
struct RunStateTestAccess {
  [[nodiscard]] static Interpreter& interpreter(fsim::app::Simulation& simulation) {
    return *simulation.impl_->interpreter;
  }
};

}  // namespace fsim::runtime::simir

namespace {

using fsim::runtime::simir::RunStateTestAccess;

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Language language,
    const std::string& top) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "run-state";
  config.project.top = top;
  config.project.time_resolution = language == fsim::project::Language::vhdl
      ? "auto" : "1ns";
  config.build.optimization = fsim::project::Optimization::o0;
  config.build.cache_path = directory / "cache";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = language;
  if (language == fsim::project::Language::vhdl) {
    sources.standard = "2008";
  } else {
    sources.standard = "2017";
    sources.compilation_unit = "file";
  }
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

std::unique_ptr<fsim::app::Simulation> make_simulation(
    const fsim::project::Config& config, const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  return std::make_unique<fsim::app::Simulation>(
      std::move(*project), config.run.max_deltas, engine);
}

std::uint64_t value_of(
    fsim::app::Simulation& simulation, const std::string& path) {
  const auto signal = simulation.find_signal(path);
  assert(signal);
  const auto value = simulation.read_signal(*signal).known_unsigned_value();
  assert(value);
  return *value;
}

// Runs `design` to `save_at`, saves, restores into a second simulation, runs
// both to `finish_at` and compares them.
void round_trip(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine, const std::string& signal,
    const fsim::runtime::SimulationTick save_at,
    const fsim::runtime::SimulationTick finish_at,
    const std::uint64_t value_at_save) {
  auto original = make_simulation(config, engine);
  static_cast<void>(original->run(save_at));
  assert(value_of(*original, signal) == value_at_save);

  auto& source = RunStateTestAccess::interpreter(*original);
  fsim::runtime::RunStateImage image { "run-state-application-test" };
  fsim::runtime::simir::RunStateReport report;
  const bool saved = source.record_run_state(image, report);
  for (const auto& message : report.messages) {
    std::cerr << message << '\n';
  }
  assert(saved);
  const auto hash = source.run_state_hash();
  assert(hash != 0U);
  fsim::runtime::RunStateImage second { "run-state-application-test" };
  assert(source.record_run_state(second, report));
  assert(second.serialize() == image.serialize());

  fsim::runtime::RunStateImageError parse_error { };
  const auto parsed = fsim::runtime::RunStateImage::parse(
      image.serialize(), parse_error);
  assert(parsed);

  auto restored = make_simulation(config, engine);
  restored->start();
  auto& target = RunStateTestAccess::interpreter(*restored);
  fsim::runtime::simir::RunStateReport restore_report;
  const bool ok = target.restore_run_state(*parsed, restore_report);
  for (const auto& message : restore_report.messages) {
    std::cerr << message << '\n';
  }
  assert(ok);
  if (target.run_state_hash() != hash) {
    // Name the sections the restore did not reproduce.
    fsim::runtime::RunStateImage after { "run-state-application-test" };
    fsim::runtime::simir::RunStateReport after_report;
    static_cast<void>(target.record_run_state(after, after_report));
    for (const auto& section : image.sections()) {
      const auto match = std::ranges::find_if(after.sections(),
          [&](const auto& candidate) { return candidate.name == section.name; });
      if (match == after.sections().end() || match->bytes != section.bytes) {
        std::cerr << signal << " engine " << static_cast<int>(engine)
                  << ": section " << section.name << " differs after restore\n";
        if (section.name == "sched.state" && match != after.sections().end()) {
          const auto dump = [](const std::string& raw) {
            std::string inner;
            auto reader = fsim::runtime::RunStateArchive::reader(raw);
            reader.io(inner);
            fsim::runtime::SchedulerSavedState state;
            std::string error;
            if (!fsim::runtime::deserialize_scheduler_state(
                    std::as_bytes(std::span { inner.data(), inner.size() }),
                    state, error)) {
              std::cerr << "  (unreadable: " << error << ")\n";
              return;
            }
            std::cerr << "  now=" << state.now << " cycles=" << state.cycles
                      << " callbacks=" << state.callbacks
                      << " next_sequence=" << state.next_sequence
                      << " events=" << state.events.size() << '\n';
            for (const auto& event : state.events) {
              std::cerr << "    " << event.kind << " t=" << event.time
                        << " seq=" << event.sequence << " bytes="
                        << event.payload.size() << '\n';
            }
          };
          dump(section.bytes);
          dump(match->bytes);
        }
      }
    }
  }
  assert(target.run_state_hash() == hash);
  assert(value_of(*restored, signal) == value_at_save);

  static_cast<void>(original->run(finish_at));
  static_cast<void>(restored->run(finish_at));
  assert(value_of(*original, signal) == value_of(*restored, signal));
  assert(original->now() == restored->now());
  if (RunStateTestAccess::interpreter(*original).run_state_hash()
      != RunStateTestAccess::interpreter(*restored).run_state_hash()) {
    // Name the sections the two runs disagree in.
    fsim::runtime::RunStateImage left { "run-state-application-test" };
    fsim::runtime::RunStateImage right { "run-state-application-test" };
    fsim::runtime::simir::RunStateReport ignored;
    static_cast<void>(RunStateTestAccess::interpreter(*original)
                          .record_run_state(left, ignored));
    static_cast<void>(RunStateTestAccess::interpreter(*restored)
                          .record_run_state(right, ignored));
    for (const auto& section : left.sections()) {
      const auto* const other = right.find(section.name);
      if (other == nullptr || other->bytes != section.bytes) {
        std::size_t at = 0U;
        if (other != nullptr) {
          while (at < section.bytes.size() && at < other->bytes.size()
              && section.bytes[at] == other->bytes[at]) {
            ++at;
          }
        }
        std::cerr << "round_trip: section " << section.name
                  << " differs at byte " << at << " of "
                  << section.bytes.size() << '\n';
      }
    }
  }
  assert(RunStateTestAccess::interpreter(*original).run_state_hash()
      == RunStateTestAccess::interpreter(*restored).run_state_hash());
}

// The foreign sections: the data sets by id, the highest id and the restart
// callback registrations survive the archive, and the sections register as
// optional ones (a run without that interface writes none).
void foreign_state_round_trip() {
  using fsim::app::application_detail::ForeignCallbackRecord;
  using fsim::app::application_detail::ForeignRunState;
  ForeignRunState state;
  state.data[1] = std::string("\x07\0\0\0\x09\0\0\0", 8);
  state.data[4] = "later";
  state.last_id = 4;
  state.callbacks.push_back({6, "libplugin.so", 0x1234U, 1U, 0});
  std::string bytes;
  auto writer = fsim::runtime::RunStateArchive::writer(bytes);
  writer.io(state);
  assert(writer.ok());
  ForeignRunState back;
  auto reader = fsim::runtime::RunStateArchive::reader(bytes);
  reader.io(back);
  assert(reader.finished());
  assert(back.data == state.data && back.last_id == 4);
  assert(back.callbacks.size() == 1U && back.callbacks[0].module == "libplugin.so"
         && back.callbacks[0].offset == 0x1234U && back.callbacks[0].user_data == 1U);

  fsim::runtime::RunStateSectionRegistry registry;
  fsim::app::application_detail::register_foreign_run_state_sections(
      registry, nullptr, nullptr);
  assert(registry.owners().empty());
}

// A restart-callback routine is a module path and an offset (IEEE 1076-2019
// 20.7, IEEE 1800-2023 38.36.3): a routine of the C library (a shared module
// on every platform the tree builds for; getenv is not an indirect function)
// encodes and decodes to the same address with the Linux, macOS or Windows
// lookup, and a module that is not loaded decodes to null, which the restart
// reports as FSIM-SAVE-0016.
void callback_routine_round_trip() {
  using fsim::app::application_detail::decode_callback_routine;
  using fsim::app::application_detail::encode_callback_routine;
  const void* const routine = reinterpret_cast<const void*>(&std::getenv);
  const auto where = encode_callback_routine(routine);
  assert(where.has_value() && !where->first.empty());
  assert(decode_callback_routine(where->first, where->second)
         == const_cast<void*>(routine));
  assert(!encode_callback_routine(nullptr).has_value());
  assert(decode_callback_routine("/no/such/fsim-module.so", 16U) == nullptr);
}

// The save audit (#128, FSIM-SAVE-0011): an owner reports each class S member
// that is not at rest, the registry collects the lines of every owner (also of
// one that keeps no section), and clear_all calls every owner's clear.
void section_audit() {
  fsim::runtime::RunStateSectionRegistry registry;
  bool busy = false;
  int cleared = 0;
  fsim::runtime::RunStateSectionOwner owner;
  owner.name = "test.audit";
  owner.version = 1U;
  owner.audit = [&busy](std::vector<std::string>& messages) {
    if (busy) {
      messages.push_back("FSIM-SAVE-0011: the test member is not at rest at "
                         "the save point");
    }
  };
  owner.clear = [&cleared] { ++cleared; };
  registry.add(std::move(owner));
  std::vector<std::string> errors;
  assert(registry.audit(errors) && errors.empty());
  busy = true;
  assert(!registry.audit(errors));
  assert(errors.size() == 1U && errors.front().starts_with("FSIM-SAVE-0011"));
  registry.clear_all();
  assert(cleared == 1);
}

// A restore is not a start of simulation (IEEE 1800-2023 38.36.3; owner
// decision 2026-11-07): a callback of a host service (the kind the VHPI bridge
// registers) for cbStartOfSimulation hears the start of a run but not the
// restore of one. unverified: run(27) leaves the scheduler at a save point.
void restore_fires_no_start_callbacks(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::filesystem::path& state) {
  const auto counting = [](fsim::app::Simulation& simulation, int& count) {
    fsim::runtime::SystemVerilogVpiCallbackRegistration registration;
    registration.kind =
        fsim::runtime::SystemVerilogVpiCallbackKind::StartOfSimulation;
    registration.host_service = true;
    registration.callback =
        [&count](const fsim::runtime::SystemVerilogVpiCallbackEvent&) {
          ++count;
        };
    const auto registered =
        simulation.systemverilog_vpi_callbacks().register_callback(
            std::move(registration));
    assert(registered);
  };
  int started = 0;
  auto original = make_simulation(config, engine);
  counting(*original, started);
  static_cast<void>(original->run(27));
  assert(started == 1);
  const auto saved = original->save_state(state, false);
  assert(saved.ok);

  int restored_count = 0;
  auto restored = make_simulation(config, engine);
  counting(*restored, restored_count);
  const auto result = restored->restore_state(state);
  if (!result.ok) {
    std::cerr << result.error << '\n';
  }
  assert(result.ok);
  static_cast<void>(restored->run(60));
  assert(restored_count == 0);
}

// A reset is refused (FSIM-SAVE-0018) while structure a foreign interface
// created exists: a driver owner process (vhpi_create of a process or a driver
// of no process) or a host driver (IEEE 1800-2023 D.8, IEEE 1076-2019 20.9,
// 23.6). The twin a reset builds is the design from the artifact and cannot
// recreate either. The refusal is saved with the run (app.control), so a
// restored run is refused as well. A reset source is not installed here, so
// the refusal text names the structure only after it was created.
void reset_refused_for_foreign_structure(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::filesystem::path& state) {
  const auto created = [](const std::string& refusal) {
    return refusal.starts_with("FSIM-SAVE-0018")
        && refusal.find("structure a foreign interface created")
            != std::string::npos;
  };
  auto owner_run = make_simulation(config, engine);
  static_cast<void>(owner_run->run(7));
  assert(!created(owner_run->reset_refusal()));
  static_cast<void>(owner_run->add_foreign_driver_owner(0));
  assert(created(owner_run->reset_refusal()));
  assert(owner_run->reset_refusal().find("driver owner process")
      != std::string::npos);

  auto driver_run = make_simulation(config, engine);
  const auto signal = driver_run->find_signal("run_state_counter.count");
  assert(signal);
  assert(!created(driver_run->reset_refusal()));
  driver_run->add_foreign_driver(0, *signal);
  assert(created(driver_run->reset_refusal()));
  assert(driver_run->reset_refusal().find("a driver created") != std::string::npos);

  // Saved in app.control: a restored run keeps the refusal.
  static_cast<void>(driver_run->run(7));
  assert(driver_run->save_state(state, false).ok);
  auto restored = make_simulation(config, engine);
  assert(!created(restored->reset_refusal()));
  const auto result = restored->restore_state(state);
  if (!result.ok) {
    std::cerr << result.error << '\n';
  }
  assert(result.ok);
  assert(created(restored->reset_refusal()));
}

}  // namespace

int main() {
  foreign_state_round_trip();
  callback_routine_round_trip();
  section_audit();
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-run-state-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);

  // SystemVerilog. clk starts at 0 and toggles at 5, 10, 15, ...; the always
  // block adds one to count (non-blocking) at each rising edge: 5, 15, 25,
  // 35, ... (IEEE 1800-2023 9.4.2, 4.7). count is 3 after time 25 and still 3
  // at 27; the run to 60 adds the edges at 35, 45 and 55: 6.
  const auto sv_source = directory.path / "run_state_top.sv";
  {
    std::ofstream output(sv_source, std::ios::binary);
    output << R"(
module run_state_top;
  logic clk = 1'b0;
  logic [7:0] count = 8'd0;
  always #5 clk = ~clk;
  always @(posedge clk) count <= count + 8'd1;
endmodule
)";
  }
  const auto sv_config = make_config(directory.path, sv_source,
      fsim::project::Language::system_verilog, "sv:work.run_state_top");
  for (const auto engine : { fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::v4 }) {
    // unverified: run(27) leaves the scheduler at a save point.
    round_trip(sv_config, engine, "run_state_top.count", 27, 60, 3);
  }
  for (const auto engine : { fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::v4 }) {
    restore_fires_no_start_callbacks(sv_config, engine,
        directory.path / (engine == fsim::app::SimulationEngine::v4
                                 ? "start_v4.state" : "start_interpreter.state"));
  }

  // VHDL. The process adds one to count every 5 ns (IEEE 1076-2019 10.2, the
  // signal updates in the cycle that follows the wait): 5, 10 and 15 ns give
  // 3 at 17 ns; 20 through 30 ns add three more (unverified: inclusive end).
  const auto vhdl_source = directory.path / "run_state_counter.vhd";
  {
    std::ofstream output(vhdl_source, std::ios::binary);
    output << R"(
entity run_state_counter is
end entity;

architecture rtl of run_state_counter is
  signal count : integer := 0;
begin
  process
  begin
    wait for 5 ns;
    count <= count + 1;
  end process;
end architecture;
)";
  }
  const auto vhdl_config = make_config(directory.path, vhdl_source,
      fsim::project::Language::vhdl, "vhdl:work.run_state_counter(rtl)");
  for (const auto engine : { fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::v4 }) {
    // The auto resolution of a VHDL design is 1 fs: 17 ns and 30 ns.
    round_trip(vhdl_config, engine, "run_state_counter.count",
        17'000'000, 30'000'000, 3);
  }
  // unverified: add_host_driver on process 0 of this design (the process that
  // drives count) and save_state after run(7) at a save point.
  for (const auto engine : { fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::v4 }) {
    reset_refused_for_foreign_structure(vhdl_config, engine,
        directory.path / (engine == fsim::app::SimulationEngine::v4
                                 ? "foreign_v4.state"
                                 : "foreign_interpreter.state"));
  }
  return 0;
}
