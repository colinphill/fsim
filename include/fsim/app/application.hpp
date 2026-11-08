// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/elaboration/sdf_net_delay_split.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"
#include "fsim/runtime/systemverilog_coverage_state.hpp"
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
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/runtime/vpi_coverage.hpp"
#include "fsim/runtime/vpi_data_read.hpp"
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
class SdfSessionPublication;
class SdfReannotationApplication;
struct SdfReannotationCommitResult;

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
    /// Coverage facts recorded with a compiled object. When present, code
    /// coverage takes the source's identity and controls from them and never
    /// reads the source.
    std::optional<artifact::ObjectSourceCoverage> coverage;
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

struct CheckedProject : semantic::CompiledDesign {
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
    /// Parser-independent class specializations owned with the compiled HIR.
    std::vector<semantic::sv::ClassSpecialization>
        compiled_systemverilog_class_specializations;
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
    struct Input {
        std::string language;
        std::string standard;
        std::string compatibility_profile { "none" };
    };
    std::string library;
    std::string metadata_digest;
    std::vector<std::string> unit_checksums;
    bool native_accepted { };
    std::string native_kind;
    std::string native_fingerprint;
    /// The language, standard and profile of the library's units.
    std::vector<Input> inputs;
    std::vector<library::VhdlPackageDependency> vhdl_package_dependencies;
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
    /// Linked .fsimscplugin artifacts, or the shared library of a plug-in
    /// built from a mapped library's sources.
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
    /// Parser-independent specialization records retained with their owning
    /// semantic model and SystemVerilog HIR. No syntax-backed class payload is
    /// retained after compilation.
    std::vector<semantic::sv::ClassSpecialization>
        compiled_systemverilog_class_specializations;
    /// Mutable ID-keyed coverage state. Declaration structure remains owned
    /// exactly once by `systemverilog_hir`.
    runtime::SystemVerilogCoverageState systemverilog_coverage;
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
    /// Retained for source compatibility only: the per-process JIT was
    /// removed in the engine-v4 migration, so this choice has no effect.
    CompiledProcessSelection compiled_process_selection {
        CompiledProcessSelection::selected
    };
    /// Immutable opt-in capability. Disabled builds do not create coverage
    /// inventories, counters, callbacks, or runtime-operation checks.
    bool code_coverage_enabled { };
    /// Opt-in A2 combinational cone fusion for this in-memory consumer. Set
    /// only when no observer outside SimIR processes (trace output, debugger,
    /// coverage) can inspect hidden internal nets; hidden nets are still
    /// materialized before any runtime observation.
    bool cone_fusion { };
    /// What replaces the whole-design static-kernel gates for this
    /// in-memory consumer (foreign libraries, trace output, coverage): the
    /// planner keeps the signals those clients can reach as host signals.
    /// `admit` lets the kernel run although cone fusion is off.
    elaboration::StaticKernelAccessPlan kernel_access { };
    /// Validated file inputs awaiting the design digest assigned at publication.
    std::vector<std::shared_ptr<const SdfSessionPublication>>
        sdf_session_publications { };
    /// Restored from a trusted design artifact (checksummed, from a matching
    /// build): derived state it carries is not re-derived and cross-checked.
    /// FSIM_VERIFY_ARTIFACT=1 keeps the checks.
    bool trusted_artifact { };
    /// The build annotated the VITAL timing generics of the SDF files it was
    /// given (IEEE 1076.4-2000 5.1.1), so the SDF session leaves VHDL cells
    /// to it.
    bool sdf_vital_applied { };
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
    /// An alias of `v4` (normalized when a Simulation is constructed). The
    /// per-process LLVM JIT it once named was removed.
    compiled,
    /// The reference SimIR evaluator with debugger execution-point hooks;
    /// no native code is used.
    debug,
    /// Engine v4 (docs/simulation-engine-v4-plan.md): the static kernel owns
    /// every member process; the reference evaluator runs the rest. The
    /// per-process JIT is never started. A design the kernel refuses runs
    /// wholly on the reference evaluator, with a reason (plan §9, D5).
    v4,
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

/// In-memory execution-path telemetry for executable SystemVerilog class
/// bodies. A nonzero frontend_adapter_fallbacks count identifies an attempted
/// path which has not yet been implemented by the compiled-HIR evaluator.
struct SystemVerilogClassExecutionStatistics {
    std::size_t hir_constructors { };
    std::size_t hir_functions { };
    std::size_t hir_tasks { };
    std::size_t frontend_adapter_fallbacks { };
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
    // The tick the attempt that reached this outcome started at.
    runtime::SimulationTick attempt_start { };
    // The source position of the expression that rejected a failed attempt
    // (failExpr, IEEE 1800-2023 39.4.2); line 0 when unknown.
    std::uint32_t fail_line { };
    std::uint32_t fail_column { };

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

/// Archived identity for a SystemC plug-in used by a loaded design.
struct SystemCPluginProvenance {
    std::string logical_library;
    std::string input_digest;
    std::string link_digest;
    std::string compiler_fingerprint;
    std::string library_checksum;
    std::vector<std::string> factories;
};

struct DebuggerSourceProvenance;

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
    using SystemVerilogDpiFunctionHook
        = runtime::simir::Interpreter::DpiFunctionCallHook;
    using SystemVerilogDpiSiteHooks
        = runtime::simir::Interpreter::DpiSiteHooks;
    using VcdControlHook = runtime::simir::Interpreter::VcdControlHook;
    using ConcurrentAssertionHook = std::function<void(const ConcurrentAssertionEvent&)>;
    /// An attempt of the VPI assertion object (a handle) started at the tick
    /// (IEEE 1800-2023 39.4.2 cbAssertionStart).
    using AssertionAttemptHook
        = std::function<void(std::uint64_t, runtime::SimulationTick)>;
    /// A step of an attempt of the VPI assertion object (a handle), produced
    /// only for attempts stepping is enabled for (IEEE 1800-2023 39.4.2
    /// cbAssertionStepSuccess / cbAssertionStepFailure, 39.5.2).
    using AssertionStepHook
        = std::function<void(std::uint64_t, const runtime::sva::StepRecord&)>;
    using VhdlPslAttemptHook = runtime::VhdlPslCompletionHook;
    using UvmActivityHook = runtime::SystemVerilogUvmActivityService::Observer;
    using UvmTaskPhaseContinuation = runtime::SystemVerilogUvmPhaseService::TaskPhaseCallback;
    using SafePointHook = runtime::Scheduler::SafePointHook;
    using ForceStatementHook
        = runtime::simir::Interpreter::ForceStatementHook;
    using SignalTransactionHook
        = runtime::simir::Interpreter::SignalTransactionHook;
    using DriverTransactionHook
        = runtime::simir::Interpreter::DriverTransactionHook;
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
    [[nodiscard]] const semantic::sv::Hir& systemverilog_hir() const noexcept;
    [[nodiscard]] const semantic::Model& semantics() const noexcept;
    [[nodiscard]] const std::vector<MappedLibraryProvenance>&
    mapped_libraries() const noexcept;
    [[nodiscard]] std::vector<SystemCPluginProvenance>
    systemc_plugin_provenance() const;
    [[nodiscard]] std::vector<DebuggerSourceProvenance>
    structured_provenance(
        std::optional<std::string_view> path = std::nullopt) const;
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
    /// The hierarchical path `find_signal` takes for a signal, if it has one.
    [[nodiscard]] std::optional<std::string> signal_path(
        runtime::simir::SignalId signal) const;
    /// Return a live reference backed by public storage for its lifetime.
    [[nodiscard]] const runtime::PackedLogic4& read_signal(
        runtime::simir::SignalId signal) const;
    /// Return a value snapshot without permanently pinning the signal.
    [[nodiscard]] runtime::PackedLogic4 read_signal_snapshot(
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
    /// `newer_activations` reads an outer activation of a recursive callable
    /// (VHPI stack frames, IEEE 1076-2019 19.10).
    [[nodiscard]] runtime::PackedLogic4 read_process_local(
        runtime::simir::ProcessId process, std::size_t local_index,
        std::size_t newer_activations = 0U) const;
    /// The value slot `slot` of the process program's call-result table holds
    /// (`Process::call_results`, IEEE 1800-2023 37.42, 38.15).
    [[nodiscard]] runtime::PackedLogic4 read_process_call_result(
        runtime::simir::ProcessId process, std::size_t slot) const;
    /// Deposit a value into a variable of a process (VHPI 22.5.2).
    void write_process_local(runtime::simir::ProcessId process,
        std::size_t local_index, const runtime::PackedLogic4& value);
    [[nodiscard]] runtime::SystemVerilogScalarValue read_process_scalar_local(
        runtime::simir::ProcessId process, std::size_t local_index) const;
    [[nodiscard]] std::string read_process_string_local(
        runtime::simir::ProcessId process, std::size_t local_index) const;
    void write_process_string_local(runtime::simir::ProcessId process,
        std::size_t local_index, std::string_view value);
    void write_process_container_local(runtime::simir::ProcessId process,
        std::size_t local_index, const runtime::simir::ContainerValue& value);
    [[nodiscard]] runtime::simir::ContainerValue
    read_process_container_local(
        runtime::simir::ProcessId process, std::size_t local_index) const;
    [[nodiscard]] const std::string& read_string_object(
        runtime::simir::StringObjectId object) const;
    [[nodiscard]] const runtime::simir::ContainerValue&
    read_container_object(
        runtime::simir::ContainerObjectId object) const;
    [[nodiscard]] const std::vector<
        semantic::sv::ClassSpecialization>&
    class_specializations() const noexcept;
    // A class specialization by identity: a design-wide one, or the class of
    // a generated scope or design element instance, which is rebuilt from
    // its identity on first use (IEEE 1800-2023 6.22, 27.4); null when none.
    [[nodiscard]] const semantic::sv::ClassSpecialization*
    class_specialization(std::string_view identity) const;
    [[nodiscard]] SystemVerilogClassExecutionStatistics
    class_execution_statistics() const noexcept;
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
    /// Time unit and precision exponents of the Verilog/SystemVerilog
    /// instance at `path` (IEEE 1800-2023 3.14.2), if it is one.
    [[nodiscard]] std::optional<std::pair<int, int>>
    systemverilog_instance_time_scale(std::string_view path) const;
    /// Registry handles that publish `signal` (a net, port, or alias of it).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> vpi_signal_object_handles(
        runtime::simir::SignalId signal) const;
    /// The driver objects beneath the net or variable `object`, each with the
    /// kernel process that drives it (IEEE 1800-2023 37.16); materializes
    /// them first.
    [[nodiscard]] std::vector<
        std::pair<fsim_vpi_handle_v1, runtime::simir::ProcessId>>
    vpi_driver_objects(fsim_vpi_handle_v1 object);
    /// A procedural assign of the register `object` to `value`, or its
    /// deassign when `value` is empty (IEEE 1364-2005 9.3.1, ACC
    /// acc_set_value accAssignFlag and accDeassignFlag 23.96). Only a
    /// signal-bound register takes it.
    [[nodiscard]] runtime::SystemVerilogVpiValueError assign_vpi_value(
        fsim_vpi_handle_v1 object,
        std::optional<runtime::SystemVerilogVpiStoredValue> value);
    /// The ports of the published instances, each instance's in port order.
    [[nodiscard]] const std::vector<runtime::SystemVerilogVpiPort>&
    systemverilog_vpi_ports() const noexcept;
    /// Live delay table of the elaborated Verilog module path `path`, in
    /// scheduler ticks (IEEE 1800-2023 38.11).
    [[nodiscard]] std::vector<runtime::SimulationTick>
    verilog_module_path_delays(std::uint32_t path) const;
    /// Replace that table (IEEE 1800-2023 38.34). Returns false, changing
    /// nothing, when the scheduler is running outside a safe point; the
    /// caller retries from `add_safe_point_hook`.
    [[nodiscard]] bool set_verilog_module_path_delays(
        std::uint32_t path, std::span<const runtime::SimulationTick> delays);
    /// Live limits of the elaborated Verilog timing check `check`, in
    /// scheduler ticks (IEEE 1800-2023 38.10).
    [[nodiscard]] std::vector<std::int64_t> verilog_timing_check_limits(
        std::uint32_t check) const;
    /// Replace that path's delay table and, when `reject` and `error` are not
    /// empty, its pulse limit tables (IEEE 1800-2023 38.32). Returns false,
    /// changing nothing, outside a safe point; throws std::invalid_argument
    /// for tables the path cannot take.
    [[nodiscard]] bool set_verilog_module_path_timing(std::uint32_t path,
        std::span<const runtime::SimulationTick> delays,
        std::span<const runtime::SimulationTick> reject,
        std::span<const runtime::SimulationTick> error);
    /// Replace that check's limits (IEEE 1800-2023 38.32). Returns false,
    /// changing nothing, outside a safe point as for the path delays; throws
    /// std::invalid_argument for limits the check cannot take.
    [[nodiscard]] bool set_verilog_timing_check_limits(
        std::uint32_t check, std::span<const std::int64_t> limits);
    /// Give a driver process new transition delays for its delayed writes
    /// (IEEE 1800-2023 38.32); legal at any time. False when the driver has
    /// no slotted write (the run had no foreign library).
    [[nodiscard]] bool set_verilog_driver_delays(
        runtime::simir::ProcessId process, runtime::simir::TransitionDelays delays);
    /// The transition delays a driver process's delayed writes use now: the
    /// ones set_verilog_driver_delays gave it, else its first slotted write's.
    [[nodiscard]] std::optional<runtime::simir::TransitionDelays>
    verilog_driver_delays(runtime::simir::ProcessId process) const;
    /// Give a driver process the twelve transition delays and pulse limits
    /// of its interconnect-delay writes (IEEE 1364-2005 16.2.4); legal at any
    /// time. False when the driver has no such write.
    [[nodiscard]] bool set_verilog_net_delay_table(
        runtime::simir::ProcessId process,
        const runtime::simir::NetDelayTable& table);
    /// The table a driver process's interconnect-delay writes use now: the
    /// one set_verilog_net_delay_table gave it, else its first write's.
    [[nodiscard]] std::optional<runtime::simir::NetDelayTable>
    verilog_net_delay_table(runtime::simir::ProcessId process) const;
    /// Asks for the split of a load port that no SDF annotation split (a PORT
    /// `record` of the port, or of the bits a select names, with the twelve
    /// delays a foreign vpi_put_delays, acc_replace_delays or
    /// acc_append_delays gave): the port's net gets a delayed copy that the
    /// instance behind the port reads, written by a $mipd_driver process, as
    /// annotation does at elaboration (split_sdf_net_delays). The split is
    /// made at the next scheduler safe point, with the net's value then, and
    /// pending changes keep their time; a request for the same port before
    /// that replaces the first one's delays. A run that never asks pays
    /// nothing: no signal, process or hook exists until then. After the split
    /// the port's record in design().sdf_net_delays() is delayed, and
    /// set_verilog_net_delay_table retimes the copy.
    void request_net_delay_split(elaboration::SdfNetDelayInfo record);
    /// The result of put_gate_driver_delays.
    enum class GateDelayPut : std::uint8_t {
        /// The driver took the delays now (a slotted write, or a gate already
        /// split).
        applied,
        /// The driver writes without a delay: it is split into a delay slot at
        /// the next safe point (owner decision 2026-11-07; a gate declared
        /// without a delay is never turned into a #0 write), with these
        /// delays; gate_delay_split_refusals says if the engine cannot.
        split_requested,
        refused,
    };
    /// Gives a gate's driver process the delays of a vpi_put_delays or
    /// acc_replace_delays (IEEE 1800-2023 38.32), splitting a delay-free
    /// driver into a delay slot the first time (Simulation::GateDelayPut).
    [[nodiscard]] GateDelayPut put_gate_driver_delays(
        runtime::simir::ProcessId driver, runtime::simir::TransitionDelays delays);
    /// Whether put_gate_driver_delays would take these delays now, changing
    /// nothing; when not, `reason` says why.
    [[nodiscard]] bool gate_delays_accepted(runtime::simir::ProcessId driver,
        runtime::simir::TransitionDelays delays, std::string& reason);
    /// The processes that write the output of the gate (or switch) whose full
    /// hierarchical name is given with a delay-free continuous write: the
    /// ones put_gate_driver_delays can split.
    [[nodiscard]] std::vector<runtime::simir::ProcessId>
    undelayed_gate_drivers(std::string_view full_name) const;
    /// Every process that drives an output of the gate whose full name is
    /// given (`g`, `g$one`, `g$output<n>`), delayed or not: the driver
    /// objects of IEEE 1364-2001 Table 88 (reason_paramdrc) for its
    /// output terminals.
    [[nodiscard]] std::vector<runtime::simir::ProcessId>
    gate_driver_processes(std::string_view full_name) const;
    /// Why each gate split that was asked for was not made, in request order.
    /// The turn-on and turn-off delays of a tranif put by vpi_put_delays
    /// (IEEE 1800-2023 28.16, 38.32), as the rise and fall of its control
    /// (`delays` already swapped for a tranif0). A switch declared without a
    /// delay gets the delayed copy of its control the first time, at the next
    /// safe point (the lazy split of a gate); false with
    /// gate_delay_error() set when the switch cannot take it.
    [[nodiscard]] bool put_tranif_control_delays(std::string_view full_name,
        runtime::simir::TransitionDelays delays);
    /// Why the latest put_gate_driver_delays was refused.
    [[nodiscard]] const std::string& gate_delay_error() const noexcept;
    [[nodiscard]] const std::vector<std::string>& gate_delay_split_refusals()
        const noexcept;
    /// The deferred splits that failed at their safe point after the put
    /// returned success (FSIM-VPI-SPLIT-001), each already worded as the
    /// error; the run ends with a failure status. take_ hands each to
    /// vpi_chk_error once.
    [[nodiscard]] const std::vector<std::string>& deferred_split_errors()
        const noexcept;
    [[nodiscard]] std::optional<std::string> take_deferred_split_error();
    /// Whether a split asked for is waiting for its safe point.
    [[nodiscard]] bool net_delay_split_pending() const noexcept;
    /// Why each split that was asked for was not made (the engine cannot take
    /// it, or the split does not cover the port), in request order.
    [[nodiscard]] const std::vector<std::string>& net_delay_split_refusals()
        const noexcept;
    /// Run `hook` with a timing check's index when it is violated; an empty
    /// function removes it (IEEE 1800-2023 38.36.3).
    void set_verilog_timing_violation_hook(
        std::function<void(std::uint32_t)> hook);
    /// Run `hook` with a dynamic array's or queue's VPI object and its old
    /// and new size when the size changes (cbSizeChange, IEEE 1800-2023
    /// 37.17 detail 14, 38.36.1); an empty function removes it.
    void set_vpi_size_change_hook(
        std::function<void(fsim_vpi_handle_v1, std::size_t, std::size_t)> hook);
    /// Run `hook` once at the end of the simulation, before the end of
    /// simulation callbacks, so the live class objects can be reclaimed
    /// (cbReclaimObj, IEEE 1800-2023 38.36.1); an empty function removes it.
    void set_class_reclaim_hook(std::function<void()> hook);
    /// Compare the length of a string variable's text at each safe point so
    /// the size hook hears of its changes (cbSizeChange on a string var,
    /// IEEE 1800-2023 37.17 detail 14).
    void watch_vpi_string_size(fsim_vpi_handle_v1 handle);
    /// Tell `hook` of each class object whose constructor has completed
    /// (cbCreateObj, IEEE 1800-2023 38.36.1); only a run with the execution
    /// probes does. An empty function removes it.
    void set_class_created_hook(
        std::function<void(runtime::SystemVerilogClassHandle)> hook);
    /// The object whose lowered constructor just completed (the
    /// object_created execution point); 0 when none is pending.
    [[nodiscard]] runtime::SystemVerilogClassHandle
    take_constructed_class_object() noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiTimeService&
    systemverilog_vpi_time() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiTimeService&
    systemverilog_vpi_time() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiDataReadService&
    systemverilog_vpi_data_read() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiDataReadService&
    systemverilog_vpi_data_read() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiCallbackManager&
    systemverilog_vpi_callbacks() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiCallbackManager&
    systemverilog_vpi_callbacks() const noexcept;
    /// Publish the compiled kernel's changed outputs now, so the value
    /// changes a running process made so far are observed before a foreign
    /// routine it called acts on them (an ACC link deleted after a change,
    /// IEEE 1364-2001 23.97).
    void publish_kernel_outputs();
    [[nodiscard]] runtime::SystemVerilogVpiValueControl&
    systemverilog_vpi_values() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiValueControl&
    systemverilog_vpi_values() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiControlService&
    systemverilog_vpi_control() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiControlService&
    systemverilog_vpi_control() const noexcept;
    [[nodiscard]] runtime::SystemVerilogVpiAssertionApi&
    systemverilog_vpi_assertions() noexcept;
    [[nodiscard]] const runtime::SystemVerilogVpiAssertionApi&
    systemverilog_vpi_assertions() const noexcept;
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
    /// Mutable simulation-owned functional coverage state, including exact
    /// arbitrary-width hits, callbacks, trace events, and derived reports.
    [[nodiscard]] const runtime::SystemVerilogCoverageState&
    systemverilog_coverage() const noexcept;
    /// Atomically write this run's code-coverage database, merged with any
    /// coverage history the run loaded, to `path`. Returns the failure
    /// reason, or nothing once written.
    [[nodiscard]] std::optional<std::string> save_code_coverage_database(
        const std::filesystem::path& path);
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
    /// The VPI multichannel descriptor routines on the table $fopen uses
    /// (IEEE 1800-2023 38.24-38.29).
    [[nodiscard]] std::uint32_t mcd_open(std::string_view name);
    [[nodiscard]] std::uint32_t mcd_close(std::uint32_t descriptor);
    [[nodiscard]] std::optional<std::string> mcd_name(
        std::uint32_t descriptor) const;
    [[nodiscard]] bool mcd_write(
        std::uint32_t descriptor, std::string_view text);
    [[nodiscard]] bool mcd_flush(std::uint32_t descriptor);
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
    /// Schedule a transaction on the driver that `process`, the placeholder
    /// process of a foreign VHDL architecture, has for the `value.width()`
    /// bits of `signal` from `offset` (VHPI vhpi_schedule_transaction,
    /// IEEE 1076-2019 22.6, 23.34). The transaction time is the current
    /// time plus `delay` ticks; `rejection` is the pulse rejection limit of
    /// an inertial transaction.
    void schedule_foreign_transaction(
        runtime::simir::ProcessId process,
        runtime::simir::SignalId signal,
        runtime::PackedLogic4 value,
        std::size_t offset,
        runtime::SimulationTick delay,
        runtime::SimulationTick rejection,
        bool transport);
    /// Schedule the null transaction of the driver `process` has for
    /// `signal`, `delay` ticks from now (VHPI 23.34, IEEE 1076-2019 10.5.2.2);
    /// false when the signal is not guarded.
    [[nodiscard]] bool disconnect_foreign_driver(
        runtime::simir::ProcessId process, runtime::simir::SignalId signal,
        runtime::SimulationTick delay);
    /// Force the driver of a foreign architecture's placeholder process
    /// (VHPI 22.5.4), at once or in the next update phase.
    void force_foreign_driver(
        runtime::simir::ProcessId process,
        runtime::simir::SignalId signal,
        runtime::PackedLogic4 value,
        std::size_t offset,
        bool propagate);
    /// Deposit into the driver of a foreign architecture's placeholder
    /// process at once (VHPI vhpiDeposit, 22.5.4).
    void deposit_foreign_driver(
        runtime::simir::ProcessId process,
        runtime::simir::SignalId signal,
        runtime::PackedLogic4 value,
        std::size_t offset);
    /// The pending transactions of a driver's projected output waveform for
    /// the bits [offset, offset + width) of `signal`, in time order (VHPI
    /// vhpiTransactions, IEEE 1076-2019 14.7.2); see
    /// Interpreter::driver_transactions.
    [[nodiscard]] std::vector<runtime::simir::DriverTransaction>
    foreign_driver_transactions(runtime::simir::ProcessId process,
        runtime::simir::SignalId signal, std::size_t offset,
        std::size_t width) const;
    /// Release a forced driver of a foreign architecture's placeholder
    /// process (VHPI 22.5.4).
    void release_foreign_driver(
        runtime::simir::ProcessId process,
        runtime::simir::SignalId signal,
        std::size_t offset,
        std::size_t width);
    /// A new owner of foreign drivers in the instance whose placeholder
    /// process is `placeholder`: one more process vhpi_create makes there,
    /// or a driver it makes with no process (IEEE 1076-2019 23.6).
    /// Gives `process` a driver of `signal` at its initial value, unless it
    /// has one (a driver vhpi_create makes, IEEE 1076-2019 23.6).
    /// Records structure a foreign interface made (a created process, a host
    /// driver): the run can no longer be reset (reset_refusal). Only the first
    /// description is kept.
    void note_foreign_structure(std::string_view what);
    void add_foreign_driver(
        runtime::simir::ProcessId process, runtime::simir::SignalId signal);
    [[nodiscard]] runtime::simir::ProcessId add_foreign_driver_owner(
        runtime::simir::ProcessId placeholder);
    /// Run `task` in the next signal-update phase of the current time, as
    /// VHPI vhpiDepositPropagate and vhpiForcePropagate require for their
    /// updates (IEEE 1076-2019 22.5.3).
    void schedule_update_task(std::function<void()> task);
    /// Run `task` after the other work of the current time's generic
    /// active phase, so the scheduler reaches the safe point that follows
    /// it even when no process runs at this time.
    void schedule_active_task(std::function<void()> task);
    void force_signal(
        runtime::simir::SignalId signal,
        runtime::PackedLogic4 value);
    void force_scalar_signal(
        runtime::simir::SignalId signal,
        runtime::SystemVerilogScalarValue value);
    void release_signal(runtime::simir::SignalId signal);
    [[nodiscard]] bool signal_is_forced(
        runtime::simir::SignalId signal) const;
    /// Held by a procedural continuous assign (IEEE 1364-2005 9.3.1); a
    /// forced report excludes bits only an assign holds.
    [[nodiscard]] bool signal_is_assigned(
        runtime::simir::SignalId signal) const;

    void start();
    [[nodiscard]] runtime::RunResult run(
        std::optional<runtime::SimulationTick> until = std::nullopt);
    void request_stop() noexcept;
    void clear_stop() noexcept;

    /// Points of the run a foreign interface hears of beyond its start and
    /// end: the save, restart and reset phases (IEEE 1800-2023 38.36.3,
    /// IEEE 1076-2019 20.7-20.9). A reset fires its events here; the
    /// save and restart events are fired by the save/restart driver.
    enum class LifecycleEvent : std::uint8_t {
        start_of_save,
        end_of_save,
        start_of_restart,
        end_of_restart,
        start_of_reset,
        end_of_reset,
        /// A front end that accepts commands takes control of the run, and
        /// gives it back (IEEE 1800-2023 38.36.3 cbEnterInteractive,
        /// cbExitInteractive; IEEE 1076-2019 21.3.7.8, 21.3.7.9). The
        /// debugger session fires them around each command that resumes
        /// the run. A batch run never enters interactive mode, so a $stop
        /// there fires neither.
        enter_interactive,
        exit_interactive,
        /// The interactive scope changed (IEEE 1800-2023 38.4
        /// vpiSetInteractiveScope, 38.36.3 cbInteractiveScopeChange).
        scope_change,
    };
    /// `location` names the save/restart location for the save and restart
    /// events and the hierarchical name of the new scope for scope_change,
    /// and is empty for the others. Hooks run in registration order.
    using LifecycleHook
        = std::function<void(LifecycleEvent, std::string_view location)>;
    void add_lifecycle_hook(LifecycleHook hook);
    void notify_lifecycle(
        LifecycleEvent event, std::string_view location = {});

    /// The arguments of $reset (IEEE 1800-2023 D.8, 38.4 vpiReset).
    struct ResetRequest {
        /// Zero enters interactive mode after the reset; nonzero begins
        /// processing at once.
        std::int32_t stop_value { 1 };
        std::int32_t reset_value { };
        std::int32_t diagnostics_value { };
    };
    /// True when the engine restores the time-zero state of this run.
    [[nodiscard]] bool reset_supported() const noexcept;
    /// Why this run cannot reset, as a diagnostic with its FSIM-SAVE code; empty
    /// when reset_supported(). A design with a SystemC part never resets: the
    /// SystemC kernel (IEEE 1666) has no operation that returns its modules,
    /// channels and suspended processes to their elaborated state, so $reset
    /// cannot meet IEEE 1800-2023 D.8 ("All variables and nets contain their
    /// initial values") for it. vpiReset (38.4), $reset and vhpiReset (IEEE
    /// 1076-2019 23.5: a tool "may" implement it, 20.9) are refused with this
    /// text instead of resetting part of the design.
    [[nodiscard]] std::string reset_refusal() const;
    /// Installs the factory a reset takes the time-zero state from (IEEE
    /// 1800-2023 D.8): it returns a fresh interpreter of the same design,
    /// built from the artifact and configured as the run's was before start.
    /// Without one reset_supported() is false.
    void set_reset_source(
        std::function<std::unique_ptr<runtime::simir::Interpreter>()> source);
    /// Installs the reset source from a loader of the same design artifact:
    /// each reset reloads the design with `reload` and builds the twin as
    /// this run was built (seed, access plan and roots are taken from the
    /// running design). `max_deltas` and `engine` are the constructor's.
    void set_reset_design_source(
        std::function<std::optional<BuiltProject>()> reload,
        std::uint64_t max_deltas, SimulationEngine engine);
    /// Ask for a reset once the caller's routine returns: the run stops at
    /// the next safe point, the foreign interfaces hear start-of-reset,
    /// time and every value return to their time-zero state, they hear
    /// end-of-reset, and run() goes on. False, with nothing queued, when the
    /// engine cannot reset.
    [[nodiscard]] bool request_reset(ResetRequest request);
    /// The resets performed (IEEE 1800-2023 D.8 $reset_count) and the
    /// reset_value of the last one ($reset_value).
    [[nodiscard]] std::uint32_t reset_count() const noexcept;
    [[nodiscard]] std::int32_t reset_value() const noexcept;

    /// Run-state files: $save, $incsave and $restart (IEEE 1800-2023 D.9),
    /// `simulate --save-at/--save-file/--restart`, and the Tcl commands
    /// `save` and `restart`. A file holds the whole run state in versioned,
    /// checksummed sections (runtime/run_state_image.hpp).
    struct StateFileResult {
        bool ok { true };
        /// "FSIM-SAVE-nnnn: ..." when !ok.
        std::string error;
    };
    /// Whether a process of the design runs $save, $incsave, $restart or
    /// $reset. Such a run keeps the host's time steps (no kernel-owned
    /// time), so the end of a time slot is where the request sees it.
    [[nodiscard]] bool uses_simulation_control() const noexcept;
    /// What a restart must repeat besides the design: the command line and
    /// libraries of the run (IEEE 1800-2023 D.9 requires the same design;
    /// the same libraries keep their callback registrations valid). Included
    /// in the identity a saved file records; set before the run starts.
    void set_state_file_identity(std::string text);
    /// The sections the foreign interfaces add to a run-state file
    /// ("foreign.vpi", "foreign.vhpi": the data sets by save/restart id and
    /// the restart callbacks; IEEE 1800-2023 38.31, 38.36.3, IEEE 1076-2019
    /// 20.7). They are written after the end-of-save callbacks ran, so that
    /// data put from those callbacks is in the file, and read before the
    /// start-of-restart callbacks fire, so that the data can be read and the
    /// restart callbacks exist. The registry's owners must outlive the run.
    void set_foreign_run_state_sections(runtime::RunStateSectionRegistry sections);
    /// True while a restore sets the run up (before start-of-restart): a
    /// fresh-process restart fires no start-of-simulation callback of any
    /// interface, only start- and end-of-restart.
    [[nodiscard]] bool restoring_state() const noexcept;
    /// Writes the run state to `file` (written beside it and renamed, so a
    /// failed save leaves an older file). At the end of a time slot with the
    /// slot's events drained, or between run() calls. `incremental` writes
    /// only what changed since the last full save of this run ($incsave,
    /// D.9); it fails when no full save came before.
    [[nodiscard]] StateFileResult save_state(
        const std::filesystem::path& file, bool incremental = false);
    /// Restores the run state from `file`, a full file or an incremental one
    /// whose full base file is still present and unchanged. The foreign
    /// interfaces hear start-of-restart before and end-of-restart after;
    /// the run then continues from the saved time without a second
    /// start-of-simulation. Fails (nothing partly restored is usable) with
    /// FSIM-SAVE-0009, 0010, 0015, 0016 or 0017 when the file is not one of this
    /// run's design.
    [[nodiscard]] StateFileResult restore_state(const std::filesystem::path& file);
    /// Queue a save or a restart for the next safe point of run(), as the
    /// system tasks do; the file is written at the end of the current time
    /// slot once nothing is pending in it (D.9), and a failure ends run() with
    /// the error.
    void request_save(std::filesystem::path file, bool incremental = false);
    void request_restart(std::filesystem::path file);
    [[nodiscard]] runtime::SimulationTick now() const noexcept;
    [[nodiscard]] std::uint64_t delta() const noexcept;
    /// Simulation cycles started (IEEE 1076-2019 23.18).
    [[nodiscard]] std::uint64_t cycles() const noexcept;
    [[nodiscard]] bool has_pending() const noexcept;
    /// The earliest tick with pending activity, the current tick for a
    /// pending delta cycle; nothing when no activity remains.
    /// The future time steps that hold work, in increasing order (IEEE
    /// 1800-2023 37.81).
    [[nodiscard]] std::vector<runtime::SimulationTick> pending_times()
        const;
    [[nodiscard]] std::optional<runtime::SimulationTick> next_pending_time()
        const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    /// End the simulation for the foreign interfaces when a batch run stops
    /// for good without finishing (a time limit, a batch `$stop`, a runtime
    /// error): VPI cbEndOfSimulation callbacks run once, here, if they have
    /// not yet run (IEEE 1800-2023 38.36.3). Callback failures are ignored.
    void end_foreign_simulation() noexcept;
    /// True once a final procedure has executed `$stop`.
    [[nodiscard]] bool paused_in_final() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    /// Stub kept for API compatibility: always zero. The per-process LLVM
    /// executor was removed in the engine-v4 migration.
    [[nodiscard]] std::size_t compiled_process_count() const noexcept;
    /// Stub kept for API compatibility: always zero.
    [[nodiscard]] std::size_t compiled_module_count() const noexcept;
    /// Stub kept for API compatibility: all counters are zero (there is no
    /// per-process native-object cache any more).
    [[nodiscard]] NativeCacheStatistics
    native_cache_statistics(bool synchronize = true) const noexcept;
    /// Stub kept for API compatibility: no-op (no background compilation).
    void await_native_compilation() const;
    /// Stub kept for API compatibility: no-op (no background compilation).
    void await_all_native_compilation() const;
    void set_signal_change_hook(SignalChangeHook hook);
    /// Signal change observers see every later change of these signals,
    /// also of those the v4 kernel owns (docs/engine-v4-semantics.md S19).
    /// A watch lasts for the rest of the run.
    void watch_signal_changes(std::span<const runtime::simir::SignalId> signals);
    /// watch_signal_changes for every signal (an observer of any change).
    void watch_all_signal_changes();
    /// watch_signal_changes, and signal transaction hooks also hear the
    /// assignments that leave these signals' values unchanged
    /// (Interpreter::watch_signal_transactions).
    void watch_signal_transactions(
        std::span<const runtime::simir::SignalId> signals);
    /// A client observes delta cycles or time steps from outside the design
    /// (Interpreter::open_external_observation).
    void open_external_observation();
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
    /// Observe each process force or release statement as it executes.
    [[nodiscard]] std::uint64_t add_force_statement_hook(
        ForceStatementHook hook);
    void remove_force_statement_hook(std::uint64_t token) noexcept;
    /// Observe every signal transaction, with or without an event.
    [[nodiscard]] std::uint64_t add_signal_transaction_hook(
        SignalTransactionHook hook);
    void remove_signal_transaction_hook(std::uint64_t token) noexcept;
    /// Observe every driver update by the process that holds the driver,
    /// with or without a change of the driver's value.
    [[nodiscard]] std::uint64_t add_driver_transaction_hook(
        DriverTransactionHook hook);
    void remove_driver_transaction_hook(std::uint64_t token) noexcept;
    /// Commits a live SDF reannotation: the module path and timing check
    /// tables, and, when `annotated` (the session's design with its
    /// interconnect and port delays split, plan_sdf_session_inputs) keeps the
    /// split the simulation runs, the records and the delay tables of the
    /// delayed copies of the load ports (sdf_net_delay_retiming): later
    /// transitions use the new tables, pending ones keep their time. A
    /// reannotation that changes which loads are split splits a load that had
    /// no delay at this safe point (request_net_delay_split) and gives one
    /// that loses its delay a zero table. `device_gates` are the DEVICE
    /// delays of outputs that no module path ends at (SdfSessionPlan::
    /// device_gates, IEEE 1497-2001 5.4.1): the gates that drive each take
    /// its delays (put_gate_driver_delays), a delay-free gate by the split at
    /// the next safe point.
    [[nodiscard]] SdfReannotationCommitResult commit_sdf_reannotation(
        const SdfReannotationApplication& application,
        const elaboration::ElaboratedDesign* annotated = nullptr,
        std::span<const elaboration::SdfDeviceGateDelay> device_gates = {});
    [[nodiscard]] bool at_sdf_reannotation_safe_point() const noexcept;
    /// Counts the live SDF reannotations that replaced the delay tables of
    /// the delayed port copies (sdf_net_delay_retiming): a value that
    /// vpi_put_delays stored for such a record is stale once this changes.
    [[nodiscard]] std::uint64_t sdf_net_delay_epoch() const noexcept;
    void set_execution_point_hook(ExecutionPointHook hook);
    /// Add an execution-point observer beside the debugger's hook. The
    /// static kernel reports points only when it keeps its debug points
    /// (`--engine debug`, or `foreign_probes`).
    [[nodiscard]] std::uint64_t add_execution_point_hook(
        ExecutionPointHook hook);
    void remove_execution_point_hook(std::uint64_t token) noexcept;
    /// The run asked for statement, call and suspend probes for its foreign
    /// libraries (`simulate --foreign-probes`).
    void set_foreign_probes(bool enabled) noexcept;
    [[nodiscard]] bool foreign_probes() const noexcept;
    /// VHPI timed callbacks registered at the end of a time step are
    /// accepted (`simulate --vhpi-end-of-step-timers`).
    void set_vhpi_end_of_step_timers(bool enabled) noexcept;
    [[nodiscard]] bool vhpi_end_of_step_timers() const noexcept;
    /// Replace the host executor for IEEE $system calls. The default invokes
    /// C system(); an empty hook makes the service unavailable at execution.
    void set_system_command_hook(SystemCommandHook hook);
    /// Install the imported DPI function boundary used by compiled-HIR calls.
    /// The callback receives the C linkage name and mutable copy-out actuals.
    void set_systemverilog_dpi_function_hook(
        SystemVerilogDpiFunctionHook hook);
    /// Install the bound call sites of the imported DPI functions: calls the
    /// service binds run through their site instead of the identity hook.
    void set_systemverilog_dpi_site_hooks(SystemVerilogDpiSiteHooks hooks);
    /// The installed DPI function boundary, for a service that serves some
    /// identities itself and passes the others on.
    [[nodiscard]] SystemVerilogDpiFunctionHook
    systemverilog_dpi_function_hook() const;
    void set_vcd_control_hook(VcdControlHook hook);
    void set_output_hook(OutputHook hook);
    /// Use the executable's concrete process-stdout sink for ordinary HDL
    /// text. Reserved output controls still prepare full observation.
    void set_builtin_stdout_output();
    void set_report_hook(ReportHook hook);
    /// Install a report sink that only formats the supplied message and
    /// metadata, without inspecting simulation state. Unlike
    /// set_report_hook(), reports reach it without a full signal-observation
    /// barrier.
    void set_trusted_text_report_hook(ReportHook hook);
    void set_concurrent_assertion_hook(ConcurrentAssertionHook hook);
    void set_assertion_attempt_hook(AssertionAttemptHook hook);
    void set_assertion_step_hook(AssertionStepHook hook);
    /// vpiAssertionEnableStep / vpiAssertionDisableStep for the attempt of
    /// the VPI assertion object that starts at the tick (IEEE 1800-2023
    /// 39.5.2). `attempt_started`: the attempt has already started, so its
    /// stepping mode cannot change any more (39.5.2) and nothing changed.
    enum class AssertionStepResult : std::uint8_t {
        done,
        no_assertion,
        attempt_started,
    };
    [[nodiscard]] AssertionStepResult set_assertion_step(std::uint64_t assertion,
        runtime::SimulationTick start, bool enable);
    /// Discard the running attempts of the VPI assertion object that started
    /// at the tick, leaving it enabled (vpiAssertionKill, IEEE 1800-2023
    /// 39.5.2); true when one was running.
    bool kill_assertion_attempt(
        std::uint64_t assertion, runtime::SimulationTick start);
    void set_vhdl_psl_attempt_hook(VhdlPslAttemptHook hook);
    [[nodiscard]] std::uint64_t add_uvm_activity_hook(UvmActivityHook hook);
    void remove_uvm_activity_hook(std::uint64_t token) noexcept;
    void set_class_property_change_hook(ClassPropertyChangeHook hook);
    void set_class_static_property_change_hook(
        ClassStaticPropertyChangeHook hook);

private:
    friend struct ::fsim::runtime::simir::NativeRegionAllocationTestAccess;
    friend struct ::fsim::runtime::simir::SystemCBridgeTestAccess;
    friend struct ::fsim::runtime::simir::RunStateTestAccess;
    struct Impl;
    void install_execution_point_hooks();
    std::unique_ptr<Impl> impl_;
};

/// Stateful command adapter over the same debugger engine used by the CLI.
///
/// Embedders supply already-tokenized commands and own the streams for the
/// adapter lifetime. This retains breakpoints, scope, and selected process
/// state across calls without exposing debugger implementation layouts.
struct DebuggerStatus;
struct DebuggerBreakpointInfo;
struct DebuggerFrameInfo;
struct DebuggerScopeInfo;
struct DebuggerInspection;
struct DebuggerSourceProvenance;

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

    /// Structured state exposed to native clients such as the Tcl debugger.
    /// String fields are owned copies of archived or live DesignIR data.
    [[nodiscard]] DebuggerStatus status() const;
    void step(std::string_view kind);
    void continue_run(
        std::optional<std::string_view> duration = std::nullopt);
    [[nodiscard]] DebuggerBreakpointInfo add_breakpoint(
        std::string_view kind,
        std::string_view location,
        std::optional<std::string_view> comparison = std::nullopt,
        std::optional<std::string_view> value = std::nullopt);
    [[nodiscard]] DebuggerBreakpointInfo add_watch(
        std::string_view signal,
        std::optional<std::string_view> comparison = std::nullopt,
        std::optional<std::string_view> value = std::nullopt);
    [[nodiscard]] std::vector<DebuggerBreakpointInfo> breakpoints() const;
    [[nodiscard]] std::vector<DebuggerBreakpointInfo> watches() const;
    void delete_breakpoint(std::uint64_t id);
    void delete_watch(std::uint64_t id);
    void clear_breakpoints();
    [[nodiscard]] std::vector<DebuggerFrameInfo> frames() const;
    [[nodiscard]] DebuggerFrameInfo frame(std::size_t index) const;
    [[nodiscard]] DebuggerScopeInfo scope(
        std::optional<std::string_view> path = std::nullopt);
    [[nodiscard]] DebuggerInspection inspect(
        std::string_view path) const;
    [[nodiscard]] std::vector<DebuggerSourceProvenance> provenance(
        std::optional<std::string_view> path = std::nullopt) const;
    [[nodiscard]] std::optional<TraceControlStatus> trace_status() const;
    [[nodiscard]] std::span<const TraceControlReportEntry> trace_report()
        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct DebuggerStatus {
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::string scope;
    bool finished { };
    bool poisoned { };
    std::optional<std::uint64_t> breakpoint_id;
    std::string stop_reason;
};

struct DebuggerBreakpointInfo {
    std::uint64_t id { };
    std::string kind;
    std::string path;
    std::optional<runtime::SimulationTick> time;
    std::optional<std::uint32_t> line;
    std::string comparison;
    std::string value;
};

struct DebuggerLocalInfo {
    std::string name;
    std::string type;
    std::string value;
};

struct DebuggerFrameInfo {
    std::size_t index { };
    std::uint64_t process_id { };
    std::string process;
    std::string scope;
    std::string source_path;
    std::uint32_t source_line { };
    std::uint32_t source_column { };
    std::string point_kind;
    std::vector<DebuggerLocalInfo> locals;
};

struct DebuggerScopeInfo {
    std::string path;
    std::vector<std::string> children;
};

struct DebuggerInspection {
    std::string path;
    std::string name;
    std::string kind;
    std::string type;
    std::uint64_t width { };
    std::string value;
    bool forced { };
    std::vector<std::string> children;
};

struct DebuggerSourceProvenance {
    std::string path;
    std::string language;
    std::string library;
    std::string unit;
    std::string source_path;
    std::string source_identity;
    std::optional<std::uint64_t> unit_id;
    std::optional<std::uint64_t> source_id;
    std::uint32_t source_line { };
    std::uint32_t source_column { };
    std::string standard;
    std::string compatibility_profile;
    std::string instance;
    std::string plugin_identity;
    std::string plugin_input_digest;
    std::string plugin_link_digest;
    std::string plugin_compiler_fingerprint;
    std::string plugin_library_checksum;
};

[[nodiscard]] std::vector<DebuggerSourceProvenance> structured_provenance(
    const BuiltProject& project,
    std::optional<std::string_view> path = std::nullopt);

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
/// Executable-only standard-output services. Stream-injected CLI calls still
/// use the ordinary handlers and retain full callback observation.
[[nodiscard]] cli::Services make_stdio_cli_services();
/// Stream-injectable variant used by embedders and non-interactive tests.
/// The input stream must outlive the returned services object.
[[nodiscard]] cli::Services make_cli_services(std::istream& input);

} // namespace fsim::app
