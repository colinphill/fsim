// SPDX-License-Identifier: Apache-2.0
#pragma once

// The checked and built project a simulation runs: the analysed sources and
// objects, the elaborated design with its HIR and provenance, and the calls
// that produce them.

#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/systemverilog_coverage_state.hpp"
#include "fsim/runtime/uvm_checkpoint.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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

class SdfPhaseArtifact;
class SdfSessionPublication;

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

} // namespace fsim::app
