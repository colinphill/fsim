// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/cli/driver.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/class_specialization.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
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
  /// Optional consumer-local backing file for a relocatable logical path.
  std::filesystem::path backing_path;
};

struct CheckedProject {
  struct ObjectProvenance {
    std::filesystem::path directory;
    std::string metadata_digest;
    std::string compilation_digest;
    std::string language;
    std::string standard;
    std::string library;
    std::vector<std::string> unit_checksums;
    project::SourceSet source_settings;
  };
  struct MappedLibrary {
    std::string library;
    std::filesystem::path directory;
    std::string metadata_digest;
    std::vector<std::string> unit_checksums;
    std::vector<project::SourceSet> source_settings;
    bool native_accepted{};
    std::string native_kind;
    std::string native_fingerprint;
    std::filesystem::path systemc_plugin;
    std::vector<std::filesystem::path> systemc_sources;
  };
  frontend::ParsedDesign parsed;
  std::vector<frontend::SystemVerilogClassSpecialization>
      systemverilog_class_specializations;
  /// Parser-independent semantic identities and owned source provenance in
  /// deterministic manifest/declaration order. No record retains an address
  /// into `parsed`.
  semantic::Model semantics;
  /// Owning VHDL semantic HIR linked exclusively through `semantics` IDs.
  semantic::vhdl::Hir vhdl_hir;
  /// Owning Verilog/SystemVerilog semantic HIR linked by shared IDs.
  semantic::sv::Hir systemverilog_hir;
  /// HDL roots named by the project manifest, in manifest order.
  std::vector<CheckedSource> hdl_sources;
  /// SystemC translation-unit roots named by the manifest. These participate
  /// in the owning semantic source table even though the host compiler, not
  /// the HDL parser, consumes them.
  std::vector<CheckedSource> systemc_sources;
  /// Checksum-pinned compiler-supplied standard-library roots.
  std::vector<CheckedSource> standard_sources;
  /// Lazily opened precompiled libraries in deterministic dependency order.
  std::vector<MappedLibrary> mapped_libraries;
  /// Explicit non-project objects in command-line declaration order.
  std::vector<ObjectProvenance> objects;
  std::size_t source_count{};
};

struct MappedLibraryProvenance {
  std::string library;
  std::string metadata_digest;
  std::vector<std::string> unit_checksums;
  bool native_accepted{};
  std::string native_kind;
  std::string native_fingerprint;
};

struct BuiltProject {
  elaboration::ElaboratedDesign design;
  /// Stable-ID elaborated hierarchy and executable metadata. The legacy
  /// `design` remains the Task 8 compatibility/runtime adapter.
  semantic::design::DesignIr design_ir;
  /// Owner for every semantic ID referenced by `design_ir`.
  semantic::Model semantics;
  /// Owning SystemVerilog class/constraint HIR used by live source
  /// randomization and restored from the durable constraint-HIR payload.
  semantic::sv::Hir systemverilog_hir;
  std::string cache_key;
  std::string time_resolution;
  std::filesystem::path cache_path;
  project::Optimization optimization{project::Optimization::o2};
  /// Dense specialization-ID-indexed provenance keys for native modules.
  std::vector<std::string> specialization_cache_keys;
  std::vector<std::filesystem::path> systemc_plugins;
  std::shared_ptr<systemc::HierarchyRegistry> systemc_hierarchy;
  std::vector<std::uint64_t> systemc_roots;
  std::uint64_t seed{1};
  bool entropy_seed{};
  bool cache_hit{};
  /// Manifest directory used as the sandbox root for HDL file operations.
  std::filesystem::path file_root;
  /// Every logical-library-specific SystemC plug-in used by this build.
  std::vector<std::shared_ptr<systemc::HierarchyRegistry>>
      systemc_hierarchies;
  std::vector<MappedLibraryProvenance> mapped_libraries;
  std::vector<CheckedProject::ObjectProvenance> objects;
  /// Content-only identity of a loaded standalone design artifact. Project
  /// builds leave this empty; standalone native-cache keys include it.
  std::string artifact_identity;
  /// Owning class specialization metadata used to construct the shared
  /// simulation heap, static state, and method-dispatch services.
  std::vector<frontend::SystemVerilogClassSpecialization>
      systemverilog_class_specializations;
};

/// Parse all HDL source files in deterministic manifest order. Independent
/// compilation units may be analyzed concurrently; roots within a shared
/// Verilog/SV unit remain ordered. Units are merged back into source order;
/// VHDL primary, secondary, visibility, and binding dependencies must name
/// previously analyzed units.
[[nodiscard]] std::optional<CheckedProject> check_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics);

/// Load and merge explicitly compiled HDL objects in declaration order.
/// Every indexed payload is checksum-verified and deserialized directly;
/// producer sources are never preprocessed or parsed again.
[[nodiscard]] std::optional<CheckedProject> load_objects(
    std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics);

// Checks and publishes one project-built logical library as a relocatable,
// read-only .fsimlib directory. Existing destinations are not overwritten.
[[nodiscard]] bool export_library(
    const project::Config& config,
    std::string_view logical_library,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

/// Check and elaborate the selected top, then populate the persistent analysis
/// cache. The interpreter remains the reference/default engine when LLVM is not
/// configured or when the process is outside the current JIT subset.
[[nodiscard]] std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics);

/// Elaborate an already compiled, ordered HDL object set without preprocessing
/// or parsing producer sources.
[[nodiscard]] std::optional<BuiltProject> build_objects(
    const project::Config& config,
    std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics);

/// Elaborate ordered portable HDL objects and linked SystemC plug-in artifacts
/// in one manifest-free hierarchy. Either input span may be empty.
[[nodiscard]] std::optional<BuiltProject> build_objects(
    const project::Config& config,
    std::span<const std::filesystem::path> objects,
    std::span<const std::filesystem::path> systemc_plugins,
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
  std::uint64_t pruned_entries{};
  std::uintmax_t pruned_bytes{};
  std::uint64_t prune_failures{};

  friend bool operator==(
      NativeCacheStatistics,
      NativeCacheStatistics) = default;
};

struct ClassPackedTraceValue {
  std::string path;
  runtime::PackedLogic4 value;
  std::optional<runtime::SystemVerilogClassHandle> object;
};

enum class ClassRandomizationTraceKind : std::uint8_t {
  property,
  constraint,
};

struct ClassRandomizationTraceState {
  std::string path;
  runtime::SystemVerilogClassHandle object{};
  ClassRandomizationTraceKind kind{ClassRandomizationTraceKind::property};
  bool enabled{};
  std::uint64_t revision{};
  std::uint64_t stream_seed{};
  std::uint64_t domain_signature{};
  std::uint64_t cycle{};
  std::uint64_t used_values{};
};

class Simulation final {
 public:
  using SignalChangeHook = std::function<void(
      runtime::simir::SignalId,
      const runtime::PackedLogic4&,
      runtime::SimulationTick,
      std::uint64_t)>;
  using ScalarSignalChangeHook = std::function<void(
      runtime::simir::SignalId,
      const runtime::SystemVerilogScalarValue&,
      runtime::SimulationTick,
      std::uint64_t)>;
  using ExecutionPointHook =
      runtime::simir::Interpreter::ExecutionPointHook;
  using OutputHook = runtime::simir::Interpreter::OutputHook;
  using ReportHook = runtime::simir::Interpreter::ReportHook;
  using SafePointHook = runtime::Scheduler::SafePointHook;
  using ClassPropertyChangeHook = std::function<void(
      runtime::SystemVerilogClassHandle,
      std::string_view,
      const runtime::PackedLogic4&,
      runtime::SimulationTick,
      std::uint64_t)>;
  using ClassStaticPropertyChangeHook = std::function<void(
      std::string_view,
      std::string_view,
      const runtime::PackedLogic4&,
      runtime::SimulationTick,
      std::uint64_t)>;
  using ClassMethodCompletion = std::function<void(
      const runtime::SystemVerilogClassInvocationResult&,
      const std::vector<runtime::SystemVerilogClassMethodValue>&)>;

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
  /// Execution-payload compatibility adapter. Stable identity, hierarchy,
  /// provenance, and public metadata must come from `design_ir()`.
  [[nodiscard]] const elaboration::ElaboratedDesign&
  runtime_adapter() const noexcept;
  [[nodiscard]] const semantic::design::DesignIr& design_ir() const noexcept;
  [[nodiscard]] const semantic::Model& semantics() const noexcept;
  [[nodiscard]] const std::vector<MappedLibraryProvenance>&
  mapped_libraries() const noexcept;
  [[nodiscard]] std::string_view time_resolution() const noexcept;
  [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
      std::string_view path) const noexcept;
  [[nodiscard]] const runtime::PackedLogic4& read_signal(
      runtime::simir::SignalId signal) const;
  [[nodiscard]] runtime::SystemVerilogScalarValue read_scalar_signal(
      runtime::simir::SignalId signal) const;
  [[nodiscard]] std::vector<runtime::simir::SystemVerilogScalarSignalSnapshot>
  scalar_signal_snapshots() const;
  [[nodiscard]] const runtime::PackedLogic4& read_driver(
      runtime::simir::ProcessId process,
      runtime::simir::SignalId signal) const;
  [[nodiscard]] runtime::PackedLogic4 read_process_local(
      runtime::simir::ProcessId process, std::size_t local_index) const;
  [[nodiscard]] runtime::SystemVerilogScalarValue read_process_scalar_local(
      runtime::simir::ProcessId process, std::size_t local_index) const;
  [[nodiscard]] std::string read_process_string_local(
      runtime::simir::ProcessId process, std::size_t local_index) const;
  [[nodiscard]] runtime::simir::ContainerValue
  read_process_container_local(
      runtime::simir::ProcessId process, std::size_t local_index) const;
  [[nodiscard]] const std::string& read_string_object(
      runtime::simir::StringObjectId object) const;
  [[nodiscard]] const runtime::simir::ContainerValue&
  read_container_object(
      runtime::simir::ContainerObjectId object) const;
  [[nodiscard]] const std::vector<
      frontend::SystemVerilogClassSpecialization>&
  class_specializations() const noexcept;
  [[nodiscard]] runtime::SystemVerilogClassHeap& class_heap() noexcept;
  [[nodiscard]] const runtime::SystemVerilogClassHeap&
  class_heap() const noexcept;
  [[nodiscard]] runtime::SystemVerilogChandleRegistry&
  chandle_registry() noexcept;
  [[nodiscard]] const runtime::SystemVerilogChandleRegistry&
  chandle_registry() const noexcept;
  [[nodiscard]] runtime::SystemVerilogClassStaticStore&
  class_static_store() noexcept;
  [[nodiscard]] const runtime::SystemVerilogClassStaticStore&
  class_static_store() const noexcept;
  [[nodiscard]] runtime::SystemVerilogClassMethodRuntime&
  class_methods() noexcept;
  [[nodiscard]] const runtime::SystemVerilogClassMethodRuntime&
  class_methods() const noexcept;
  /// Deterministic packed class-property/static values suitable for trace
  /// declaration or snapshots. Paths and handles contain no host addresses.
  [[nodiscard]] std::vector<ClassPackedTraceValue>
  class_packed_trace_values() const;
  /// Stable randomization provenance snapshots for debugger, callbacks, and
  /// trace backends. Paths and state contain no host addresses or RNG objects.
  [[nodiscard]] std::vector<ClassRandomizationTraceState>
  class_randomization_trace_states() const;
  [[nodiscard]] runtime::SystemVerilogClassHandle allocate_class(
      std::string_view specialization_identity,
      std::string_view declared_type = {});
  [[nodiscard]] const runtime::SystemVerilogClassPropertyValue&
  read_class_property(
      runtime::SystemVerilogClassHandle handle,
      std::string_view property) const;
  void deposit_class_property(
      runtime::SystemVerilogClassHandle handle,
      std::string_view property,
      runtime::PackedLogic4 value);
  [[nodiscard]] runtime::SystemVerilogClassInvocationResult
  invoke_class_method(
      std::string_view canonical_method,
      runtime::SystemVerilogClassHandle this_handle,
      std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
      std::optional<std::uint32_t> virtual_slot = std::nullopt);
  void schedule_class_method(
      runtime::SimulationTick time,
      runtime::StableOrder stable_order,
      std::string canonical_method,
      runtime::SystemVerilogClassHandle this_handle,
      std::vector<runtime::SystemVerilogClassMethodValue> actuals,
      std::optional<std::uint32_t> virtual_slot = std::nullopt,
      ClassMethodCompletion completion = {});
  void deposit_string_object(
      runtime::simir::StringObjectId object, std::string_view value);
  void deposit_container_object(
      runtime::simir::ContainerObjectId object,
      runtime::simir::ContainerValue value);
  void deposit_signal(
      runtime::simir::SignalId signal,
      runtime::PackedLogic4 value);
  void deposit_scalar_signal(
      runtime::simir::SignalId signal,
      runtime::SystemVerilogScalarValue value);
  void force_signal(
      runtime::simir::SignalId signal,
      runtime::PackedLogic4 value);
  void force_scalar_signal(
      runtime::simir::SignalId signal,
      runtime::SystemVerilogScalarValue value);
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
  void set_scalar_signal_change_hook(ScalarSignalChangeHook hook);
  [[nodiscard]] std::uint64_t add_scalar_signal_change_hook(
      ScalarSignalChangeHook hook);
  void remove_scalar_signal_change_hook(std::uint64_t token) noexcept;
  void set_safe_point_hook(SafePointHook hook);
  /// Add an independent scheduler safe-point observer without replacing the
  /// debugger, interrupt, or API control hook.
  [[nodiscard]] std::uint64_t add_safe_point_hook(SafePointHook hook);
  void remove_safe_point_hook(std::uint64_t token) noexcept;
  void set_execution_point_hook(ExecutionPointHook hook);
  void set_output_hook(OutputHook hook);
  void set_report_hook(ReportHook hook);
  void set_class_property_change_hook(ClassPropertyChangeHook hook);
  void set_class_static_property_change_hook(
      ClassStaticPropertyChangeHook hook);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Stateful command adapter over the same debugger engine used by the CLI.
///
/// Embedders supply already-tokenized commands and own the streams for the
/// adapter lifetime. This retains breakpoints, scope, and selected process
/// state across calls without exposing debugger implementation layouts.
class DebuggerControl final {
 public:
  DebuggerControl(
      Simulation& simulation,
      std::ostream& output,
      std::ostream& error);
  DebuggerControl(
      Simulation& simulation,
      std::ostream& output,
      std::ostream& error,
      const project::Config& config,
      diagnostic::Engine& diagnostics);
  ~DebuggerControl();
  DebuggerControl(DebuggerControl&&) noexcept;
  DebuggerControl& operator=(DebuggerControl&&) noexcept;
  DebuggerControl(const DebuggerControl&) = delete;
  DebuggerControl& operator=(const DebuggerControl&) = delete;

  void execute(const std::vector<std::string>& command);

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
