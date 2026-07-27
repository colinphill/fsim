// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/cli/driver.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::systemc {
class HierarchyRegistry;
}

namespace fsim::app {

struct CheckedSource {
  std::filesystem::path path;
  /// SHA-256 of the exact root bytes supplied to analysis/preprocessing.
  std::string content_digest;
  struct Dependency {
    std::filesystem::path path;
    std::string content_digest;
  };
  /// Exact transitive preprocessing inputs, in deterministic first-use order.
  std::vector<Dependency> dependencies;
  /// Digest of every ordered root/include snapshot in this compilation unit.
  std::string compilation_unit_digest;
};

struct CheckedProject {
  frontend::ParsedDesign parsed;
  std::vector<CheckedSource> hdl_sources;
  std::size_t source_count{};
};

struct BuiltProject {
  elaboration::ElaboratedDesign design;
  std::string cache_key;
  std::string time_resolution;
  std::filesystem::path cache_path;
  project::Optimization optimization{project::Optimization::o2};
  /// Dense specialization-ID-indexed provenance keys for native modules.
  std::vector<std::string> specialization_cache_keys;
  std::vector<std::filesystem::path> systemc_plugins;
  std::shared_ptr<systemc::HierarchyRegistry> systemc_hierarchy;
  std::vector<std::uint64_t> systemc_roots;
  bool cache_hit{};
};

/// Parse all HDL source files in deterministic manifest order. Independent
/// compilation units may be analyzed concurrently; roots within a shared
/// Verilog/SV unit remain ordered. Units are merged back into source order.
[[nodiscard]] std::optional<CheckedProject> check_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics);

/// Check and elaborate the selected top, then populate the persistent analysis
/// cache. The interpreter remains the reference/default engine when LLVM is not
/// configured or when the process is outside the current JIT subset.
[[nodiscard]] std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics);

enum class SimulationEngine : std::uint8_t {
  /// Use the reference SimIR evaluator for every process.
  interpreter,
  /// Compile supported processes when the LLVM backend is available and use
  /// the reference evaluator for explicitly unsupported processes.
  compiled,
  /// Compile supported processes at O0 for debugger use, retaining the
  /// reference evaluator for explicitly unsupported processes.
  ///
  /// Executable statement, wait, assertion, and process-boundary points retain
  /// source locations. Call safe points and addressable locals remain future
  /// work.
  debug,
};

struct NativeCacheStatistics {
  std::uint64_t hits{};
  std::uint64_t misses{};
  std::uint64_t stores{};
  std::uint64_t rejected_entries{};
  std::uint64_t load_failures{};
  std::uint64_t store_failures{};

  friend bool operator==(
      NativeCacheStatistics,
      NativeCacheStatistics) = default;
};

class Simulation final {
 public:
  using SignalChangeHook = std::function<void(
      runtime::simir::SignalId,
      const runtime::PackedLogic4&,
      runtime::SimulationTick,
      std::uint64_t)>;
  using ExecutionPointHook =
      runtime::simir::Interpreter::ExecutionPointHook;

  Simulation(
      BuiltProject project,
      std::uint64_t max_deltas,
      SimulationEngine engine = SimulationEngine::compiled);
  ~Simulation();
  Simulation(Simulation&&) noexcept;
  Simulation& operator=(Simulation&&) noexcept;
  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;

  [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
  [[nodiscard]] std::string_view time_resolution() const noexcept;
  [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
      std::string_view path) const noexcept;
  [[nodiscard]] const runtime::PackedLogic4& read_signal(
      runtime::simir::SignalId signal) const;
  [[nodiscard]] runtime::PackedLogic4 read_process_local(
      runtime::simir::ProcessId process, std::size_t local_index) const;
  void deposit_signal(
      runtime::simir::SignalId signal,
      runtime::PackedLogic4 value);
  void force_signal(
      runtime::simir::SignalId signal,
      runtime::PackedLogic4 value);
  void release_signal(runtime::simir::SignalId signal);
  [[nodiscard]] bool signal_is_forced(
      runtime::simir::SignalId signal) const;

  void start();
  [[nodiscard]] runtime::RunResult run(
      std::optional<runtime::SimulationTick> until = std::nullopt);
  void request_stop() noexcept;
  void clear_stop() noexcept;
  [[nodiscard]] runtime::SimulationTick now() const noexcept;
  [[nodiscard]] std::uint64_t delta() const noexcept;
  [[nodiscard]] bool has_pending() const noexcept;
  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] bool poisoned() const noexcept;
  /// Number of processes using the LLVM executor. This is zero for the
  /// reference engine and for builds without the LLVM adapter.
  [[nodiscard]] std::size_t compiled_process_count() const noexcept;
  /// Number of elaborated specialization modules containing those processes.
  ///
  /// Several processes directly owned by one specialization share a single
  /// LLVM optimization and native-object cache unit.
  [[nodiscard]] std::size_t compiled_module_count() const noexcept;
  /// Native-object cache activity incurred while materializing this
  /// simulation's compiled specialization modules.
  [[nodiscard]] NativeCacheStatistics
  native_cache_statistics() const noexcept;
  void set_signal_change_hook(SignalChangeHook hook);
  /// Add an independent signal observer without replacing the trace/API hook.
  /// The returned token remains valid until removed or the Simulation dies.
  [[nodiscard]] std::uint64_t add_signal_change_hook(SignalChangeHook hook);
  void remove_signal_change_hook(std::uint64_t token) noexcept;
  void set_safe_point_hook(runtime::Scheduler::SafePointHook hook);
  void set_execution_point_hook(ExecutionPointHook hook);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Parse a canonical 2/4-state textual value of exactly width bits.
[[nodiscard]] std::optional<runtime::PackedLogic4> parse_value(
    std::string_view text,
    std::size_t width,
    std::string& error);

/// Convert a manifest/CLI time into global ticks at the selected resolution.
[[nodiscard]] std::optional<runtime::SimulationTick> parse_time(
    std::string_view text,
    std::string_view resolution,
    std::string& error);

/// Concrete command handlers used by fsim and its traditional aliases.
[[nodiscard]] cli::Services make_cli_services();
/// Stream-injectable variant used by embedders and non-interactive tests.
/// The input stream must outlive the returned services object.
[[nodiscard]] cli::Services make_cli_services(std::istream& input);

/// Run the command-line debugger against an already-started simulation.
int run_debug_repl(
    Simulation& simulation,
    std::istream& input,
    std::ostream& output,
    std::ostream& error);

}  // namespace fsim::app
