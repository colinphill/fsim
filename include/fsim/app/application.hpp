// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/class_specialization.hpp"
#include "fsim/frontend/coverage_persistence.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_callback.hpp"
#include "fsim/runtime/uvm_checkpoint.hpp"
#include "fsim/runtime/uvm_command_line.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_config_db.hpp"
#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_factory.hpp"
#include "fsim/runtime/uvm_foreign.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_phase.hpp"
#include "fsim/runtime/uvm_register_model.hpp"
#include "fsim/runtime/uvm_registry.hpp"
#include "fsim/runtime/uvm_report.hpp"
#include "fsim/runtime/uvm_resource.hpp"
#include "fsim/runtime/uvm_sequence.hpp"
#include "fsim/runtime/uvm_synchronization.hpp"
#include "fsim/runtime/uvm_test_runner.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"
#include "fsim/runtime/uvm_tlm2.hpp"
#include "fsim/runtime/vhdl_psl.hpp"
#include "fsim/runtime/vhpi_object.hpp"
#include "fsim/runtime/vpi_callback.hpp"
#include "fsim/runtime/vpi_control.hpp"
#include "fsim/runtime/vpi_coverage.hpp"
#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/vpi_system.hpp"
#include "fsim/runtime/vpi_time.hpp"
#include "fsim/runtime/vpi_value_control.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::systemc {
class HierarchyRegistry;
}

namespace fsim::app {

class SdfPhaseArtifact;

struct CheckedSource {
    std::filesystem::path path;
    std::string language;
    std::string standard;
    /// SHA-256 of the exact root bytes supplied to analysis/preprocessing.
    std::string content_digest;
    struct Dependency {
        std::filesystem::path path;
        std::string content_digest;
        frontend::StandardRevision standard_revision {
            frontend::StandardRevision::SystemVerilog2017
        };
    };
    /// Exact transitive preprocessing inputs, in deterministic first-use order.
    std::vector<Dependency> dependencies;
    /// Digest of every ordered root/include snapshot in this compilation unit.
    std::string compilation_unit_digest;
    /// Optional consumer-local backing file for a relocatable logical path.
    std::filesystem::path backing_path;
    frontend::StandardRevision standard_revision {
        frontend::StandardRevision::SystemVerilog2017
    };
};

struct SystemVerilogUvmProvenance {
    project::SystemVerilogUvmRelease release {
        project::SystemVerilogUvmRelease::none
    };
    // Content-derived identity of the exact roots, dependencies, or portable
    // objects which supplied the selected release.
    std::string source_identity;

    friend bool operator==(
        const SystemVerilogUvmProvenance&,
        const SystemVerilogUvmProvenance&) = default;
};

struct CheckedProject {
    struct ObjectProvenance {
        std::filesystem::path directory;
        std::string metadata_digest;
        std::string compilation_digest;
        std::string language;
        std::string standard;
        std::string compatibility_profile { "none" };
        std::string library;
        artifact::CodeCoverageArtifactIdentity code_coverage;
        std::vector<library::VhdlPackageDependency> vhdl_package_dependencies;
        std::vector<std::string> unit_checksums;
        project::SourceSet source_settings;
    };
    struct MappedLibrary {
        std::string library;
        std::filesystem::path directory;
        std::string metadata_digest;
        std::vector<frontend::StandardRevision> standard_revisions;
        std::vector<library::VhdlPackageDependency> vhdl_package_dependencies;
        std::vector<std::string> unit_checksums;
        std::vector<project::SourceSet> source_settings;
        bool native_accepted { };
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
    SystemVerilogUvmProvenance systemverilog_uvm_provenance;
    std::size_t source_count { };
    /// Relocation-safe trace policy restored from object or mapped-library input.
    std::shared_ptr<const TraceArchiveSnapshot> trace_archive;
};

struct MappedLibraryProvenance {
    std::string library;
    std::string metadata_digest;
    std::vector<std::string> unit_checksums;
    bool native_accepted { };
    std::string native_kind;
    std::string native_fingerprint;
};

/// Source-profile identity retained for each analyzed VHDL library unit. The
/// semantic unit ID is the stable join key used by elaborated hierarchy,
/// debugger, trace, and VHPI publication; compiler package implementations do
/// not become hierarchy objects.
struct VhdlUnitProvenance {
    semantic::UnitId unit { };
    std::string standard;
    std::string predefined_environment;
    std::string compatibility_profile;
    std::vector<library::VhdlPackageDependency> package_dependencies;
};

/// Public owning-unit identity for an elaborated Verilog/SystemVerilog scope.
/// Cache keys and compiler implementation objects are intentionally excluded.
struct VerilogScopeProvenance {
    std::string path;
    semantic::UnitId unit { };
    semantic::SourceSpanId source { };
    semantic::Language language { semantic::Language::system_verilog };
    std::string library;
    std::string unit_name;
    std::string semantic_unit;
    std::string source_path;
    std::uint32_t source_line { };
    std::uint32_t source_column { };
    std::string standard;
    std::string compatibility_profile;
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
    /// Owning VHDL declaration/PSL HIR used by live temporal execution. Durable
    /// artifact restoration is added with the other VHDL/PSL schemas in Change 16.
    semantic::vhdl::Hir vhdl_hir;
    std::string cache_key;
    std::string time_resolution;
    std::filesystem::path cache_path;
    project::Optimization optimization { project::Optimization::o2 };
    /// Dense specialization-ID-indexed provenance keys for native modules.
    std::vector<std::string> specialization_cache_keys;
    std::vector<std::filesystem::path> systemc_plugins;
    std::shared_ptr<systemc::HierarchyRegistry> systemc_hierarchy;
    std::vector<std::uint64_t> systemc_roots;
    std::uint64_t seed { 1 };
    bool entropy_seed { };
    bool cache_hit { };
    /// Manifest directory used as the sandbox root for HDL file operations.
    std::filesystem::path file_root;
    /// Every logical-library-specific SystemC plug-in used by this build.
    std::vector<std::shared_ptr<systemc::HierarchyRegistry>>
        systemc_hierarchies;
    std::vector<MappedLibraryProvenance> mapped_libraries;
    std::vector<CheckedProject::ObjectProvenance> objects;
    SystemVerilogUvmProvenance systemverilog_uvm_provenance;
    /// Content-only identity of a loaded standalone design artifact. Project
    /// builds leave this empty; standalone native-cache keys include it.
    std::string artifact_identity;
    /// Owning class specialization metadata used to construct the shared
    /// simulation heap, static state, and method-dispatch services.
    std::vector<frontend::SystemVerilogClassSpecialization>
        systemverilog_class_specializations;
    /// Owning coverage declarations, mutable state, reports, and observer events.
    frontend::SystemVerilogCoverageState systemverilog_coverage;
    /// Versioned, pointer-free UVM bootstrap state loaded from a portable design.
    std::optional<runtime::SystemVerilogUvmCheckpointArtifact>
        systemverilog_uvm_checkpoint;
    std::vector<VhdlUnitProvenance> vhdl_unit_provenance;
    std::map<std::string, frontend::StandardRevision, std::less<>>
        verilog_unit_revisions;
    std::map<std::string, std::string, std::less<>>
        verilog_unit_compatibility_profiles;
    /// Validated immutable SDF state carried across non-project phase boundaries.
    std::vector<std::shared_ptr<const SdfPhaseArtifact>> sdf_phase_artifacts { };
    /// Versioned trace policy carried across object/design/cache/checkpoint phases.
    std::shared_ptr<const TraceArchiveSnapshot> trace_archive;
    enum class CompiledProcessSelection : std::uint8_t {
        selected,
        all,
    };
    /// Native-process admission policy for this in-memory consumer. Portable
    /// design artifacts intentionally do not persist this cache-local choice.
    CompiledProcessSelection compiled_process_selection {
        CompiledProcessSelection::selected
    };
    /// Immutable opt-in capability. Disabled builds do not create coverage
    /// inventories, counters, callbacks, or runtime-operation checks.
    bool code_coverage_enabled { };
};

[[nodiscard]] bool code_coverage_enabled(
    const project::Config& config) noexcept;

[[nodiscard]] std::vector<VerilogScopeProvenance>
verilog_scope_provenance(const BuiltProject& project);
[[nodiscard]] std::optional<VerilogScopeProvenance>
verilog_scope_provenance(
    const BuiltProject& project,
    std::string_view path);

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

/// Controls whether kernel mutations are continuously mirrored into the live
/// SystemVerilog VPI object model.  API and foreign-interface users retain the
/// live bridge by default; standalone execution can leave the already-published
/// time-zero registry dormant when no VPI consumer is present, or omit design
/// publication entirely for a standalone run that cannot expose VPI services.
enum class SystemVerilogVpiRuntimeUpdates : std::uint8_t {
    enabled,
    disabled,
    omitted,
};

struct NativeCacheStatistics {
    std::uint64_t hits { };
    std::uint64_t misses { };
    std::uint64_t stores { };
    std::uint64_t rejected_entries { };
    std::uint64_t load_failures { };
    std::uint64_t store_failures { };
    std::uint64_t pruned_entries { };
    std::uintmax_t pruned_bytes { };
    std::uint64_t prune_failures { };

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
    runtime::SystemVerilogClassHandle object { };
    ClassRandomizationTraceKind kind { ClassRandomizationTraceKind::property };
    bool enabled { };
    std::uint64_t revision { };
    std::uint64_t stream_seed { };
    std::uint64_t domain_signature { };
    std::uint64_t cycle { };
    std::uint64_t used_values { };
};

enum class ConcurrentAssertionCoverageKind : std::uint8_t {
    assertion,
    assumption,
    cover,
    restriction,
};

struct ConcurrentAssertionCoverage {
    std::string name;
    std::string process;
    ConcurrentAssertionCoverageKind kind {
        ConcurrentAssertionCoverageKind::assertion
    };
    std::uint32_t slot { };
    std::uint64_t attempts { };
    std::uint64_t passes { };
    std::uint64_t failures { };
    std::uint64_t vacuous { };
    std::uint64_t aborted { };
    std::string instance_identity;
    std::uint32_t source_span { };

    friend bool operator==(
        const ConcurrentAssertionCoverage&,
        const ConcurrentAssertionCoverage&) = default;
};

enum class ConcurrentAssertionOutcome : std::uint8_t {
    pass,
    failure,
    disabled,
    vacuous,
    aborted,
};

struct ConcurrentAssertionEvent {
    std::string name;
    std::string process;
    ConcurrentAssertionCoverageKind kind {
        ConcurrentAssertionCoverageKind::assertion
    };
    ConcurrentAssertionOutcome outcome { ConcurrentAssertionOutcome::pass };
    std::uint32_t slot { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    bool action_suppressed { };
    std::string instance_identity;
    std::uint32_t source_span { };

    friend bool operator==(
        const ConcurrentAssertionEvent&,
        const ConcurrentAssertionEvent&) = default;
};

struct UvmDebugLimits {
    std::size_t maximum_records { 4'096 };
    std::size_t maximum_payload_bytes { 1U << 20U };
    std::size_t maximum_formatted_bytes { 1U << 20U };
};

enum class UvmDebugSection : std::uint8_t {
    summary,
    phases,
    objections,
    tlm1,
    tlm2,
    callbacks,
    transactions,
    sequences,
    configuration,
    register_model,
    all,
};

class UvmDebugError final : public std::runtime_error {
public:
    UvmDebugError(std::string code, std::string message);
    [[nodiscard]] std::string_view diagnostic_code() const noexcept
    {
        return code_;
    }

private:
    std::string code_;
};

struct UvmDebugSnapshot {
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::vector<runtime::SystemVerilogUvmDomainSnapshot> domains;
    std::vector<runtime::SystemVerilogUvmPhaseSnapshot> phases;
    std::vector<runtime::SystemVerilogUvmPhaseProcessSnapshot> processes;
    std::vector<runtime::SystemVerilogUvmObjectionSnapshot> objections;
    std::vector<runtime::SystemVerilogUvmDrainSnapshot> drains;
    std::vector<runtime::SystemVerilogUvmTlm1EndpointSnapshot> tlm1_endpoints;
    std::vector<runtime::SystemVerilogUvmTlm1FifoSnapshot> tlm1_fifos;
    std::vector<runtime::SystemVerilogUvmTlm1OperationSnapshot> tlm1_operations;
    std::vector<runtime::SystemVerilogUvmTlm2SocketSnapshot> tlm2_sockets;
    std::vector<runtime::SystemVerilogUvmTlm2TransactionSnapshot>
        tlm2_transactions;
    std::vector<runtime::SystemVerilogUvmCallbackSnapshot> callbacks;
    std::vector<runtime::SystemVerilogUvmCallbackFailure> callback_failures;
    std::vector<runtime::SystemVerilogUvmTransactionSnapshot> transactions;
    std::vector<runtime::SystemVerilogUvmTransactionTraceRecord>
        transaction_trace_records;
    std::vector<runtime::SystemVerilogUvmSequencerSnapshot> sequencers;
    std::vector<runtime::SystemVerilogUvmSequenceSnapshot> sequences;
    std::vector<runtime::SystemVerilogUvmSequenceItemSnapshot> sequence_items;
    std::vector<runtime::SystemVerilogUvmFactoryTraceRecord>
        factory_trace_records;
    std::vector<runtime::SystemVerilogUvmResourceTraceRecord>
        resource_trace_records;
    std::vector<runtime::SystemVerilogUvmConfigTraceRecord>
        config_trace_records;
    std::vector<runtime::SystemVerilogUvmRegisterBlockSnapshot> register_blocks;
    std::vector<runtime::SystemVerilogUvmRegisterMapSnapshot> register_maps;
    std::vector<runtime::SystemVerilogUvmRegisterSnapshot> registers;
    std::vector<runtime::SystemVerilogUvmRegisterFieldSnapshot> register_fields;
    std::vector<runtime::SystemVerilogUvmRegisterMemorySnapshot>
        register_memories;
    std::vector<runtime::SystemVerilogUvmRegisterStandardSequenceSnapshot>
        register_sequences;
    std::vector<runtime::SystemVerilogUvmRegisterCallbackSnapshot>
        register_callbacks;
    std::vector<runtime::SystemVerilogUvmRegisterCoverageSnapshot>
        register_coverage;
};

enum class VhdlDebugDeclarationKind : std::uint8_t {
    signal,
    variable,
    constant,
    file,
    access_type,
    protected_type,
    physical_type,
    other,
};

struct VhdlDebugScope {
    std::string path;
    std::string library;
    std::string unit;
    std::string standard;
    std::string predefined_environment;
    std::string compatibility_profile;
    std::vector<library::VhdlPackageDependency> package_dependencies;
    std::uint32_t source_span { };
    fsim_vhpi_handle_v1 vhpi_handle { };
};

struct VhdlDebugDeclaration {
    std::string path;
    std::string name;
    std::string type;
    std::string value;
    VhdlDebugDeclarationKind kind { VhdlDebugDeclarationKind::other };
    std::uint32_t source_span { };
    std::uint32_t driver_count { };
    bool external_alias { };
    bool live_value { };
    fsim_vhpi_handle_v1 vhpi_handle { };
};

struct VhdlDebugProcess {
    std::string path;
    std::uint32_t source_span { };
    bool postponed { };
    fsim_vhpi_handle_v1 vhpi_handle { };
};

struct VhdlDebugSnapshot {
    std::uint64_t simulation_identity { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::vector<VhdlDebugScope> scopes;
    std::vector<VhdlDebugDeclaration> declarations;
    std::vector<VhdlDebugProcess> processes;
    std::vector<runtime::VhdlPslAttemptSnapshot> psl_attempts;
    std::vector<ConcurrentAssertionCoverage> psl_coverage;
};

struct VhdlDebugLimits {
    std::size_t maximum_records { 16'384U };
    std::size_t maximum_payload_bytes { 8U << 20U };
    std::size_t maximum_formatted_bytes { 8U << 20U };
};

class VhdlDebugError final : public std::runtime_error {
public:
    explicit VhdlDebugError(std::string message)
        : std::runtime_error(std::move(message))
    {
    }
};

[[nodiscard]] std::string format_vhdl_debug_snapshot(
    const VhdlDebugSnapshot& snapshot,
    std::size_t maximum_bytes = 8U << 20U);

[[nodiscard]] std::string format_uvm_debug_snapshot(
    const UvmDebugSnapshot& snapshot,
    UvmDebugSection section = UvmDebugSection::all,
    std::size_t maximum_bytes = 1U << 20U);

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
    using ExecutionPointHook = runtime::simir::Interpreter::ExecutionPointHook;
    using OutputHook = runtime::simir::Interpreter::OutputHook;
    using ReportHook = runtime::simir::Interpreter::ReportHook;
    using SystemCommandHook = runtime::simir::Interpreter::SystemCommandHook;
    using VcdControlHook = runtime::simir::Interpreter::VcdControlHook;
    using ConcurrentAssertionHook = std::function<void(const ConcurrentAssertionEvent&)>;
    using VhdlPslAttemptHook = runtime::VhdlPslCompletionHook;
    using UvmActivityHook = runtime::SystemVerilogUvmActivityService::Observer;
    using UvmTaskPhaseContinuation = runtime::SystemVerilogUvmPhaseService::TaskPhaseCallback;
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
        SimulationEngine engine = SimulationEngine::compiled,
        SystemVerilogVpiRuntimeUpdates vpi_runtime_updates
            = SystemVerilogVpiRuntimeUpdates::enabled);
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
    /// Live process program owned by the interpreter. Process operation
    /// payloads transfer out of the elaborated adapter at simulation setup.
    [[nodiscard]] const runtime::simir::Process& process_program(
        runtime::simir::ProcessId process) const;
    [[nodiscard]] const semantic::design::DesignIr& design_ir() const noexcept;
    [[nodiscard]] const semantic::vhdl::Hir& vhdl_hir() const noexcept;
    [[nodiscard]] const semantic::Model& semantics() const noexcept;
    [[nodiscard]] const std::vector<MappedLibraryProvenance>&
    mapped_libraries() const noexcept;
    [[nodiscard]] const std::vector<VhdlUnitProvenance>&
    vhdl_unit_provenance() const noexcept;
    [[nodiscard]] std::vector<std::string>
    vhdl_provenance_comments() const;
    [[nodiscard]] std::vector<VerilogScopeProvenance>
    verilog_scope_provenance() const;
    [[nodiscard]] std::optional<VerilogScopeProvenance>
    verilog_scope_provenance(std::string_view path) const;
    [[nodiscard]] std::vector<std::string>
    verilog_provenance_comments() const;
    [[nodiscard]] std::string_view time_resolution() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
        std::string_view path) const noexcept;
    [[nodiscard]] const runtime::PackedLogic4& read_signal(
        runtime::simir::SignalId signal) const;
    [[nodiscard]] runtime::SystemVerilogScalarValue read_scalar_signal(
        runtime::simir::SignalId signal) const;
    [[nodiscard]] std::vector<runtime::simir::SystemVerilogScalarSignalSnapshot>
    scalar_signal_snapshots() const;
    [[nodiscard]] runtime::PackedLogic4 read_driver(
        runtime::simir::ProcessId process,
        runtime::simir::SignalId signal) const;
    [[nodiscard]] runtime::simir::DriveStrength read_signal_strength(
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
    [[nodiscard]] runtime::SystemVerilogUvmObjectService&
    uvm_objects() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmObjectService&
    uvm_objects() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmComponentService&
    uvm_components() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmComponentService&
    uvm_components() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmActivityService&
    uvm_activity() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmActivityService&
    uvm_activity() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmForeignService&
    uvm_foreign() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmForeignService&
    uvm_foreign() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmPhaseService&
    uvm_phases() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmPhaseService&
    uvm_phases() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmObjectionService&
    uvm_objections() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmObjectionService&
    uvm_objections() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmTlm1Service&
    uvm_tlm1() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmTlm1Service&
    uvm_tlm1() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmTlm2Service&
    uvm_tlm2() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmTlm2Service&
    uvm_tlm2() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmSequenceService&
    uvm_sequences() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmSequenceService&
    uvm_sequences() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmCallbackService&
    uvm_callbacks() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmCallbackService&
    uvm_callbacks() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmTransactionRecorderService&
    uvm_transactions() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmTransactionRecorderService&
    uvm_transactions() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmRegisterModelService&
    uvm_register_model() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmRegisterModelService&
    uvm_register_model() const noexcept;
    [[nodiscard]] UvmDebugSnapshot uvm_debug_snapshot(
        UvmDebugLimits limits = { }) const;
    [[nodiscard]] VhdlDebugSnapshot vhdl_debug_snapshot(
        VhdlDebugLimits limits = { }) const;
    [[nodiscard]] runtime::VhdlVhpiObjectRegistry&
    vhdl_vhpi_objects();
    [[nodiscard]] const runtime::VhdlVhpiObjectRegistry&
    vhdl_vhpi_objects() const;
    /// Live Verilog/SystemVerilog VPI hierarchy and effective/stored values.
    [[nodiscard]] runtime::SystemVerilogVpiObjectRegistry&
    systemverilog_vpi_objects() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiObjectRegistry&
    systemverilog_vpi_objects() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiTimeService&
    systemverilog_vpi_time() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiTimeService&
    systemverilog_vpi_time() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiCallbackManager&
    systemverilog_vpi_callbacks() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiCallbackManager&
    systemverilog_vpi_callbacks() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiValueControl&
    systemverilog_vpi_values() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiValueControl&
    systemverilog_vpi_values() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiControlService&
    systemverilog_vpi_control() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiControlService&
    systemverilog_vpi_control() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiCoverageService&
    systemverilog_vpi_coverage() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiCoverageService&
    systemverilog_vpi_coverage() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiSystemRegistry&
    systemverilog_vpi_systems() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiSystemRegistry&
    systemverilog_vpi_systems() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmCheckpointCaptureResult
    capture_uvm_checkpoint(
        runtime::SystemVerilogUvmCheckpointLimits limits = { });
    [[nodiscard]] runtime::SystemVerilogUvmRegistryService&
    uvm_registry() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmRegistryService&
    uvm_registry() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmFactoryService&
    uvm_factory() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmFactoryService&
    uvm_factory() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmResourcePoolService&
    uvm_resources() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmResourcePoolService&
    uvm_resources() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmSynchronizationService&
    uvm_synchronization() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmSynchronizationService&
    uvm_synchronization() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmConfigDbService&
    uvm_config_db() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmConfigDbService&
    uvm_config_db() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmCommandLineService&
    uvm_command_line() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmCommandLineService&
    uvm_command_line() const noexcept;
    /// Publish the ordered simulation command line to HDL plusarg queries.
    /// Must be called before simulation execution begins.
    void set_systemverilog_plusargs(
        std::span<const std::string> plusargs);
    [[nodiscard]] runtime::SystemVerilogUvmTestRunnerService&
    uvm_test_runner() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmTestRunnerService&
    uvm_test_runner() const noexcept;
    [[nodiscard]] runtime::SystemVerilogUvmReportService&
    uvm_reports() noexcept;
    [[nodiscard]] const runtime::SystemVerilogUvmReportService&
    uvm_reports() const noexcept;
    /// Apply phase controls immediately and schedule time-qualified UVM report
    /// command-line controls on the simulation scheduler.
    void schedule_uvm_report_settings();
    /// Deterministic packed class-property/static values suitable for trace
    /// declaration or snapshots. Paths and handles contain no host addresses.
    [[nodiscard]] std::vector<ClassPackedTraceValue>
    class_packed_trace_values() const;
    /// Stable randomization provenance snapshots for debugger, callbacks, and
    /// trace backends. Paths and state contain no host addresses or RNG objects.
    [[nodiscard]] std::vector<ClassRandomizationTraceState>
    class_randomization_trace_states() const;
    /// Deterministic per-instance concurrent assertion outcomes suitable for
    /// callbacks, debugger inspection, trace backends, and coverage reports.
    [[nodiscard]] std::vector<ConcurrentAssertionCoverage>
    concurrent_assertion_coverage() const;
    /// Stable sample events for callbacks, debugger inspection, and trace
    /// backends. Disabled samples are retained but do not increment coverage.
    [[nodiscard]] const std::vector<ConcurrentAssertionEvent>&
    concurrent_assertion_events() const noexcept;
    /// Mutable simulation-owned functional coverage state, including exact
    /// arbitrary-width hits, callbacks, trace events, and derived reports.
    [[nodiscard]] const frontend::SystemVerilogCoverageState&
    systemverilog_coverage() const noexcept;
    [[nodiscard]] const std::vector<runtime::VhdlPslAttemptSnapshot>&
    vhdl_psl_attempts() const noexcept;
    [[nodiscard]] std::vector<ConcurrentAssertionCoverage>
    vhdl_psl_coverage() const;
    [[nodiscard]] runtime::SystemVerilogClassHandle allocate_class(
        std::string_view specialization_identity,
        std::string_view declared_type = { });
    [[nodiscard]] runtime::SystemVerilogClassHandle allocate_uvm_object(
        std::string_view specialization_identity,
        std::string name = { },
        std::string_view declared_type = { });
    [[nodiscard]] runtime::SystemVerilogUvmRootHandle create_uvm_root(
        std::string identity);
    [[nodiscard]] runtime::SystemVerilogClassHandle allocate_uvm_component(
        std::string_view specialization_identity,
        std::string name,
        runtime::SystemVerilogClassHandle parent = 0,
        runtime::SystemVerilogUvmRootHandle root = 0,
        std::string_view declared_type = { });
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
    [[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
    execute_uvm_function_phase(runtime::SystemVerilogUvmPhaseHandle phase);
    [[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
    execute_uvm_task_phase(
        runtime::SystemVerilogUvmPhaseHandle phase,
        const UvmTaskPhaseContinuation& continuation = { });
    void schedule_class_method(
        runtime::SimulationTick time,
        runtime::StableOrder stable_order,
        std::string canonical_method,
        runtime::SystemVerilogClassHandle this_handle,
        std::vector<runtime::SystemVerilogClassMethodValue> actuals,
        std::optional<std::uint32_t> virtual_slot = std::nullopt,
        ClassMethodCompletion completion = { });
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
    native_cache_statistics(bool synchronize = true) const noexcept;
    /// Join the cold-start LLVM tier and surface any lowering, codegen, or
    /// cache failure. Larger recurring kernels promote asynchronously only
    /// for runs long enough to amortize compilation.
    void await_native_compilation() const;
    /// Force every selected native tier, join all materialization work, and
    /// surface lowering, codegen, and cache failures before returning.
    void await_all_native_compilation() const;
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
    /// Replace the host executor for IEEE $system calls. The default invokes
    /// C system(); an empty hook makes the service unavailable at execution.
    void set_system_command_hook(SystemCommandHook hook);
    void set_vcd_control_hook(VcdControlHook hook);
    void set_output_hook(OutputHook hook);
    void set_report_hook(ReportHook hook);
    void set_concurrent_assertion_hook(ConcurrentAssertionHook hook);
    void set_vhdl_psl_attempt_hook(VhdlPslAttemptHook hook);
    [[nodiscard]] std::uint64_t add_uvm_activity_hook(UvmActivityHook hook);
    void remove_uvm_activity_hook(std::uint64_t token) noexcept;
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
    [[nodiscard]] std::optional<TraceControlStatus> trace_status() const;
    [[nodiscard]] std::span<const TraceControlReportEntry> trace_report()
        const noexcept;

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

} // namespace fsim::app
