// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/app/application.hpp"
#include "application_compiled_source.hpp"
#include "application_direct_read_map_cache.hpp"
#include "../runtime/simir_process_program.hpp"
#include "../runtime/simir_builtin_process_executor_capability.hpp"

#include "tcl.hpp"

#include "fsim/compiler/object_cache.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/frontend/class_specialization.hpp"
#include "fsim/frontend/coverage_persistence.hpp"
#include "fsim/frontend/coverage_percentage.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/preprocessor.hpp"
#include "fsim/library/source_mapping.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/incremental.hpp"
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::runtime {
class FstWriter;
}

namespace fsim::semantic {
struct CompiledLinkResult;
}

namespace fsim::app::application_detail {

namespace systemverilog_hir_detail {
class SystemVerilogHirBuilder;
}

enum class BinaryPayloadReadFailure {
    none,
    open,
    size,
    read,
};

struct BinaryPayloadReadResult {
    std::optional<std::string> bytes;
    BinaryPayloadReadFailure failure { BinaryPayloadReadFailure::none };
    bool budget_exceeded { };
};

[[nodiscard]] BinaryPayloadReadResult read_binary_payload(
    const std::filesystem::path& path,
    std::optional<std::uintmax_t> maximum_bytes = std::nullopt);

class VhdlPslExecution {
public:
    using SignalReader = std::function<runtime::PackedLogic4(
        runtime::simir::SignalId)>;

    VhdlPslExecution(
        const semantic::vhdl::Hir& hir,
        const semantic::Model& semantics,
        const elaboration::ElaboratedDesign& design,
        const semantic::design::DesignIr& design_ir,
        SignalReader reader);
    ~VhdlPslExecution();
    VhdlPslExecution(const VhdlPslExecution&) = delete;
    VhdlPslExecution& operator=(const VhdlPslExecution&) = delete;

    void observe(runtime::SimulationTick time, std::uint64_t delta);
    void finish(runtime::SimulationTick time, std::uint64_t delta);
    /// The design has PSL directives to evaluate.
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::vector<runtime::VhdlPslAttemptSnapshot>&
    attempts() const noexcept;
    [[nodiscard]] std::vector<ConcurrentAssertionCoverage> coverage() const;
    [[nodiscard]] bool apply_api(
        runtime::simir::VhdlPslApiKind kind,
        std::optional<bool> enable = std::nullopt);
    void set_completion_hook(runtime::VhdlPslCompletionHook hook);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<runtime::VhdlVhpiObjectRegistry>
make_vhdl_debug_registry(const BuiltProject& project);

struct SystemVerilogVpiDriverBinding {
    fsim_vpi_handle_v1 handle { };
    runtime::simir::ProcessId process { };
    std::optional<runtime::SystemVerilogVpiDriveStrength> strength;
};

// A published unpacked array whose word count follows the live container: a
// dynamic array or a queue grows and shrinks at run time, and its words
// are created and released to match (IEEE 1800-2023 7.5, 7.10, 37.17).
struct SystemVerilogVpiLiveMemory {
    fsim_vpi_handle_v1 memory { };
    std::string path;
    runtime::SystemVerilogVpiTypeInfo word_type;
    std::vector<fsim_vpi_handle_v1> words;
};

struct SystemVerilogVpiPublishedDesign {
    std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry> registry;
    std::map<runtime::simir::SignalId, std::vector<fsim_vpi_handle_v1>>
        signals;
    std::map<fsim_vpi_handle_v1, runtime::simir::SignalId> handles;
    std::map<runtime::simir::SignalId,
        std::vector<SystemVerilogVpiDriverBinding>>
        drivers;
    std::map<runtime::simir::SignalId, std::vector<fsim_vpi_handle_v1>> events;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> assertions;
    std::map<runtime::simir::SignalId, runtime::SystemVerilogScalarKind>
        scalar_kinds;
    std::map<runtime::simir::SignalId,
        runtime::SystemVerilogVpiValueCategory>
        categories;
    std::map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<fsim_vpi_handle_v1, std::size_t>>>
        container_words;
    std::set<fsim_vpi_handle_v1> read_only_container_words;
    std::map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ContainerObjectId, std::size_t>>
        word_handles;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogScalarKind>
        container_scalar_kinds;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogVpiValueCategory>
        container_categories;
    std::map<runtime::simir::ContainerObjectId, SystemVerilogVpiLiveMemory>
        live_memories;
};

// The word of a live memory at `ordinal`, created under its memory with the
// word name `[ordinal]`.
[[nodiscard]] fsim_vpi_handle_v1 create_systemverilog_vpi_live_word(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const SystemVerilogVpiLiveMemory& memory, std::size_t ordinal);

// The stored VPI value of one element of a container value: a string, or
// packed bits under the container's scalar profile.
[[nodiscard]] runtime::SystemVerilogVpiStoredValue
systemverilog_vpi_container_word_value(
    const runtime::simir::ContainerValue& value, std::size_t ordinal,
    runtime::SystemVerilogScalarKind scalar_kind,
    runtime::SystemVerilogVpiValueCategory category);

[[nodiscard]] SystemVerilogVpiPublishedDesign
make_systemverilog_vpi_design(
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter);

[[nodiscard]] std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry>
make_empty_systemverilog_vpi_registry();

[[nodiscard]] runtime::SystemVerilogVpiTimeProfile
systemverilog_vpi_time_profile(std::string_view resolution);

[[nodiscard]] runtime::SystemVerilogVpiStoredValue
systemverilog_vpi_signal_value(
    runtime::PackedLogic4 value,
    runtime::SystemVerilogScalarKind scalar_kind,
    runtime::SystemVerilogVpiValueCategory category);

[[nodiscard]] runtime::PackedLogic4 systemverilog_vpi_packed_value(
    const runtime::SystemVerilogVpiStoredValue& value,
    runtime::SystemVerilogScalarKind scalar_kind,
    runtime::SystemVerilogVpiValueCategory category);

[[nodiscard]] std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept;

using runtime::PackedLogic4;
using runtime::SimulationTick;
using runtime::simir::SignalId;

[[nodiscard]] runtime::SystemVerilogClassPropertyDescriptor
class_property_descriptor(
    const semantic::sv::SpecializedClassProperty& property,
    const semantic::sv::Hir& hir,
    bool qualified_name = false);

[[nodiscard]] std::uint64_t entropy_seed();

[[nodiscard]] semantic::Model build_semantic_model(
    const frontend::ParsedDesign& parsed,
    std::span<const CheckedSource> hdl_sources,
    std::span<const CheckedSource> systemc_sources,
    std::span<const CheckedSource> standard_sources);

[[nodiscard]] semantic::SourceSpanId intern_semantic_span(
    semantic::Model& model,
    const frontend::SourceSpan& source);

[[nodiscard]] semantic::vhdl::Hir build_vhdl_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics);

void compose_vhdl_mode_views(semantic::vhdl::Hir& hir);

bool validate_vhdl_mode_view_hir(
    const semantic::vhdl::Hir& hir,
    diagnostic::Engine& diagnostics);

void complete_vhdl_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::vhdl::Hir& hir);

[[nodiscard]] semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    std::span<frontend::SystemVerilogClassSpecialization>
        class_specializations = { });

/// Non-owning functional-coverage inventory over the compiled SystemVerilog
/// HIR. Declarations remain owned by their unit or class and mutable instance
/// state remains owned by Hir; the inventory only supplies deterministic
/// dispatch for application/runtime consumers.
struct SystemVerilogCoverageHirInventory {
    std::vector<const semantic::sv::CovergroupDeclaration*> declarations;
    std::span<const semantic::sv::CovergroupInstance> instances;
};

[[nodiscard]] SystemVerilogCoverageHirInventory
systemverilog_coverage_hir_inventory(
    const semantic::sv::Hir& hir);

[[nodiscard]] const semantic::sv::CovergroupDeclaration*
find_systemverilog_covergroup_declaration(
    const SystemVerilogCoverageHirInventory& inventory,
    std::string_view canonical_identity) noexcept;

[[nodiscard]] const semantic::sv::CovergroupDeclaration*
find_systemverilog_covergroup_declaration(
    const semantic::sv::Hir& hir,
    std::string_view canonical_identity) noexcept;

[[nodiscard]] const semantic::sv::CovergroupInstance*
find_systemverilog_covergroup_instance(
    const SystemVerilogCoverageHirInventory& inventory,
    std::string_view runtime_identity) noexcept;

[[nodiscard]] const semantic::sv::CovergroupInstance*
find_systemverilog_covergroup_instance(
    const semantic::sv::Hir& hir,
    std::string_view runtime_identity) noexcept;

[[nodiscard]] semantic::sv::CovergroupInstance*
find_systemverilog_covergroup_instance(
    semantic::sv::Hir& hir,
    std::string_view runtime_identity) noexcept;

/// Copy mutable sampling results from the temporary frontend execution
/// adapter into HIR-native runtime state. Declaration and constructor state
/// are immutable and are deliberately not duplicated here.
void synchronize_systemverilog_covergroup_runtime_state(
    semantic::sv::CovergroupInstance& destination,
    const frontend::SystemVerilogCovergroupInstance& source,
    semantic::Model& semantics);

[[nodiscard]] runtime::SystemVerilogCoverageState
make_systemverilog_coverage_state(
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics);

[[nodiscard]] frontend::SystemVerilogCovergroupDeclaration
project_systemverilog_covergroup_declaration(
    const semantic::sv::CovergroupDeclaration& declaration,
    const semantic::Model& semantics);

[[nodiscard]] frontend::SystemVerilogCovergroupInstance
project_systemverilog_covergroup_instance(
    const semantic::sv::CovergroupInstance& instance,
    const semantic::Model& semantics);

[[nodiscard]] frontend::SystemVerilogCoverageState
project_systemverilog_coverage_state(
    const runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics);

[[nodiscard]] bool restore_systemverilog_coverage_state(
    runtime::SystemVerilogCoverageState& destination,
    const runtime::SystemVerilogCoverageState& source,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    std::string& error);

void refresh_systemverilog_coverage_reports(
    runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics);

[[nodiscard]] bool merge_systemverilog_coverage_state(
    runtime::SystemVerilogCoverageState& destination,
    const runtime::SystemVerilogCoverageState& source,
    const semantic::sv::Hir& hir,
    semantic::Model& semantics,
    std::string& error);

[[nodiscard]] runtime::SystemVerilogConstraintVariableProfile
systemverilog_constraint_profile(
    const semantic::sv::ConstraintBinding& binding);

[[nodiscard]] std::optional<runtime::SystemVerilogConstraintExpression>
lower_systemverilog_constraint_expression(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies,
    std::string& error,
    const std::map<std::string, std::vector<runtime::SystemVerilogConstraintVariableId>>& container_variables = { });

[[nodiscard]] std::optional<runtime::SystemVerilogConstraintDistribution>
lower_systemverilog_constraint_distribution(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::span<const runtime::SystemVerilogConstraintVariable>
        solver_variables,
    std::string canonical_identity,
    std::string& error);

[[nodiscard]] std::optional<std::vector<std::pair<
    runtime::SystemVerilogConstraintVariableId,
    runtime::SystemVerilogConstraintVariableId>>>
lower_systemverilog_solve_before(
    const semantic::sv::ConstraintExpression& expression,
    std::string_view specialization_identity,
    const std::map<std::string,
        runtime::SystemVerilogConstraintVariableId>& variables,
    std::string& error);

void configure_systemverilog_class_constraints(
    runtime::SystemVerilogConstraintSolver& solver,
    const runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::Hir& hir,
    const semantic::sv::ClassSpecialization& specialization,
    const std::function<bool(std::string_view)>& constraint_enabled = { },
    std::string_view identity_prefix = { },
    const std::map<std::string, std::string, std::less<>>& string_state = { });

void configure_systemverilog_randomize_selection(
    runtime::SystemVerilogClassRandomizeRequest& request,
    std::span<const std::string> selected_names);


[[nodiscard]] runtime::PackedLogic4
invoke_systemverilog_randomization_mode(
    runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogClassHandle handle,
    std::string_view method,
    std::span<const runtime::PackedLogic4> actuals);

void complete_systemverilog_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::sv::Hir& hir,
    systemverilog_hir_detail::SystemVerilogHirBuilder& type_builder);

/// Owned, sorted runtime path snapshots for one DesignIR build and validation
/// pass. Consumers borrow these views while the elaborated design is immutable.
struct RuntimePathViews {
    std::vector<std::pair<std::string, runtime::simir::SignalId>>
        signal_paths;
    std::vector<std::pair<std::string, runtime::simir::StringObjectId>>
        string_paths;
    std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
        container_paths;
};

[[nodiscard]] RuntimePathViews make_runtime_path_views(
    const elaboration::ElaboratedDesign& elaborated);

[[nodiscard]] semantic::design::DesignIr build_design_ir(
    CheckedProject& checked,
    const elaboration::ElaboratedDesign& elaborated,
    const RuntimePathViews& runtime_paths);

[[nodiscard]] bool design_object_is_signal_bearing(
    const semantic::design::Object& object) noexcept;

[[nodiscard]] bool valid_runtime_projection(
    const semantic::design::DesignIr& design,
    const elaboration::ElaboratedDesign& runtime,
    const RuntimePathViews& runtime_paths);

[[nodiscard]] bool valid_runtime_projection(
    const semantic::design::DesignIr& design,
    const elaboration::ElaboratedDesign& runtime);

class SystemCProcessExecutor final
    : public runtime::simir::ProcessExecutor {
public:
    SystemCProcessExecutor(
        std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
        const std::uint64_t process);

    [[nodiscard]] runtime::simir::ProcessResumeResult resume(
        runtime::simir::ProcessExecutionContext& context,
        runtime::simir::InstructionIndex) override;

private:
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy_;
    std::uint64_t process_ { };
};

struct DebugCodeCoverageObservation {
    runtime::simir::InstructionIndex instruction { };
    runtime::CodeCoveragePointId point;
    runtime::CodeCoverageMetric metric {
        runtime::CodeCoverageMetric::Statement
    };
    runtime::CodeCoverageCounterId counter;
    std::uint64_t hits { };

    friend bool operator==(
        const DebugCodeCoverageObservation&,
        const DebugCodeCoverageObservation&)
        = default;
};

enum class DebugCodeCoverageSnapshotError : std::uint8_t {
    none,
    resource_limit,
    invalid_point,
    invalid_metric,
    duplicate_point,
    counter_out_of_range,
};

struct DebugCodeCoverageSnapshotResult {
    std::vector<DebugCodeCoverageObservation> observations;
    DebugCodeCoverageSnapshotError error {
        DebugCodeCoverageSnapshotError::none
    };
    runtime::simir::InstructionIndex instruction { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == DebugCodeCoverageSnapshotError::none;
    }
};

[[nodiscard]] DebugCodeCoverageSnapshotResult
debug_code_coverage_snapshot(
    const runtime::simir::Process& process,
    std::span<const std::uint64_t> counters,
    std::size_t maximum_points = 1U << 20U) noexcept;


extern std::atomic_bool interrupt_requested;
static_assert(
    std::atomic_bool::is_always_lock_free,
    "the supported Ctrl-C path requires a lock-free atomic flag");

extern "C" void handle_interrupt(int);

class InterruptSignalGuard final {
public:
    InterruptSignalGuard() noexcept;

    ~InterruptSignalGuard();

    InterruptSignalGuard(const InterruptSignalGuard&) = delete;
    InterruptSignalGuard& operator=(const InterruptSignalGuard&) = delete;

private:
    using Handler = void (*)(int);
    Handler previous_ { SIG_ERR };
};

diagnostic::SourcePosition position(const frontend::SourceLocation& source);

diagnostic::SourceSpan span(const frontend::SourceSpan& source);

void import_diagnostic(
    diagnostic::Engine& output,
    const frontend::Diagnostic& input);

frontend::Language frontend_language(const project::Language language);
frontend::VhdlStandard frontend_vhdl_standard(
    project::VhdlStandard standard);
frontend::StandardRevision frontend_standard_revision(
    project::Language language,
    std::string_view standard);
void stamp_vhdl_predefined_context(frontend::ParsedDesign& parsed);
std::string_view vhdl_compatibility_profile() noexcept;
std::string vhdl_compatibility_profile(std::string_view source_profile);
std::optional<std::string_view> vhdl_source_compatibility_from_profile(
    std::string_view profile) noexcept;

struct ParseInput {
    std::filesystem::path path;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    frontend::VhdlStandard vhdl_standard {
        frontend::VhdlStandard::Vhdl2008
    };
    frontend::StandardRevision standard_revision {
        frontend::StandardRevision::SystemVerilog2017
    };
    std::string standard { "2017" };
    std::string library { "work" };
    std::size_t source_order { };
};

struct ParseGroup {
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    frontend::StandardRevision standard_revision {
        frontend::StandardRevision::SystemVerilog2017
    };
    std::string standard;
    std::string compatibility_profile { "none" };
    std::vector<ParseInput> inputs;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> defines;
    std::filesystem::path base_directory;
    frontend::SystemVerilogPackageMemberLookup package_member_lookup;
};

struct ParsedSnapshot {
    frontend::ParseResult result;
    std::vector<CheckedSource> sources;
    std::vector<std::size_t> unit_source_orders;
    std::vector<std::size_t> udp_source_orders;
    std::vector<std::size_t> class_source_orders;
    std::vector<std::size_t> class_method_source_orders;
};

/// Compile-local parser storage. Public compilation results expose only the
/// CompiledDesign-owning CheckedProject base; moving that base out destroys
/// this syntax workspace before the compilation API returns.
struct CompilationWorkspace final : CheckedProject {
    frontend::ParsedDesign parsed;
    std::vector<frontend::SystemVerilogClassSpecialization>
        systemverilog_class_specializations;
    std::vector<semantic::CompiledDesign> mapped_compiled_designs;
    std::vector<semantic::UnitId> source_units;
    std::vector<std::filesystem::path> source_unit_paths;
    std::vector<std::string> source_class_identities;
    std::vector<std::filesystem::path> source_class_paths;
    std::vector<semantic::CompiledUdpIdentity> source_udp_identities;
    std::vector<std::filesystem::path> source_udp_paths;
};

[[nodiscard]] CheckedProject release_compiled_project(
    CompilationWorkspace&& workspace);

[[nodiscard]] bool relocate_compiled_design_sources(
    semantic::CompiledDesign& design,
    std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

/// Project compiled-HIR source names without modifying the source strings.
[[nodiscard]] std::optional<std::vector<std::string>>
project_compiled_design_source_names(
    std::span<const std::string_view> names,
    std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

/// Project semantic source-name fields into a serialization-owned record set.
[[nodiscard]] std::optional<semantic::ModelRecords>
project_compiled_semantic_source_names(
    const semantic::Model& semantics,
    std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

/// Cache-only semantic serializer; standalone artifact validation is unchanged.
[[nodiscard]] std::optional<std::string>
serialize_cache_semantic_state_with_source_projection(
    const semantic::Model& semantics,
    std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

/// Serialize a cache bundle with source-bearing fields projected into the
/// cache namespace, without mutating the source bundle.
[[nodiscard]] std::optional<std::string>
serialize_cache_compiled_hir_bundle_with_source_projection(
    const semantic::CompiledDesign& design,
    std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool normalize_compiled_design_source_paths(
    CheckedProject& checked,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::string stable_cache_source_name(
    const std::filesystem::path& path,
    const std::filesystem::path& base_directory);

void add_compiled_hir_cache_key_identity(
    compiler::CacheKeyBuilder& key);

void add_compiled_hir_cache_key_identity(
    compiler::CacheKeyBuilder& key, std::uint32_t producer_revision);

[[nodiscard]] std::optional<std::vector<library::SourceNameMapping>>
compiled_cache_source_mappings(
    const CheckedProject& checked,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] semantic::CompiledLinkResult install_linked_compiled_design(
    CheckedProject& checked,
    std::vector<semantic::CompiledDesign> inputs);

[[nodiscard]] std::optional<CompilationWorkspace> check_project_workspace(
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    bool complete_vhdl_environment = false);

[[nodiscard]] std::optional<CompilationWorkspace> load_object_workspace(
    std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics,
    bool validate_uvm_surface = true);

bool same_source_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right);

[[nodiscard]] std::string source_path_key(
    const std::filesystem::path& path);

std::string compilation_unit_digest(
    const std::vector<frontend::PreprocessedRoot>& roots,
    const std::vector<frontend::PreprocessedDependency>& inputs,
    std::string_view language,
    std::string_view standard,
    std::string_view compatibility_profile,
    const std::filesystem::path& base_directory);

ParsedSnapshot parse_group_snapshot(const ParseGroup& group);

bool load_required_mapped_libraries(
    const project::Config& config,
    CompilationWorkspace& checked,
    diagnostic::Engine& diagnostics);

std::optional<systemc::PluginCompileRequest> systemc_request(
    const project::Config& config);

struct SystemCLibraryCompileRequest {
    std::string library;
    systemc::PluginCompileRequest request;
};

std::vector<SystemCLibraryCompileRequest> systemc_requests(
    const project::Config& config);

std::shared_ptr<systemc::HierarchyRegistry> load_systemc_plugin(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics);

struct SystemCLibraryRegistry {
    std::string library;
    std::shared_ptr<systemc::HierarchyRegistry> registry;
};

std::string unit_key(const frontend::DesignUnit& unit);

[[nodiscard]] std::vector<library::UnitIndexEntry>
compiled_unit_metadata_entries(
    const semantic::CompiledDesign& design,
    std::string_view expected_library);

[[nodiscard]] std::string compiled_unit_metadata_key(
    const library::UnitIndexEntry& entry,
    std::string_view library);

std::string compiled_udp_key(
    const semantic::sv::UdpDeclaration& declaration);

std::string_view compiled_udp_standard(
    const semantic::sv::UdpDeclaration& declaration);

bool compiled_udp_metadata_matches(
    const library::UnitIndexEntry& entry,
    const semantic::sv::UdpDeclaration& declaration,
    std::string_view expected_library);

const semantic::sv::UdpDeclaration* find_compiled_udp(
    const semantic::CompiledDesign& design,
    std::string_view library,
    std::string_view name);

[[nodiscard]] const semantic::sv::Unit* compiled_class_owner(
    const semantic::CompiledDesign& design,
    const semantic::sv::ClassDeclaration& declaration);

[[nodiscard]] const semantic::sv::ClassDeclaration* find_compiled_class(
    const semantic::CompiledDesign& design,
    std::string_view library,
    std::string_view canonical_identity);

[[nodiscard]] std::optional<library::UnitIndexEntry>
compiled_class_metadata_entry(
    const semantic::CompiledDesign& design,
    const semantic::sv::ClassDeclaration& declaration,
    std::string_view expected_library);

[[nodiscard]] bool compiled_class_metadata_matches(
    const library::UnitIndexEntry& entry,
    const semantic::CompiledDesign& design,
    const semantic::sv::ClassDeclaration& declaration,
    std::string_view expected_library);

void install_compiled_class_specializations(CheckedProject& checked);

void report_vhdl_duplicate_design_unit(
    const frontend::DesignUnit& unit,
    diagnostic::Engine& diagnostics);

struct CompiledUnitAvailability {
    std::string library;
    fsim::library::UnitIndexEntry unit;
};

void validate_vhdl_analysis_order(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics,
    bool allow_external_architecture_primary = false,
    const semantic::CompiledDesign* imported = nullptr,
    std::span<const CompiledUnitAvailability> available_units = { });

void validate_vhdl_source_visibility(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics);

void validate_vhdl_simulator_api(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void validate_vhdl_mode_view_interfaces(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

void validate_vhdl_package_declarations(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics,
    bool allow_missing_package_bodies = false);

// IEEE 1076-1993 1.1.1.2: a port of the enclosing entity associated as the
// actual of a component or entity port must have a compatible mode.
void validate_vhdl_port_association_modes(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

// IEEE 1076-2008 4.2.1: a subprogram declared in a package body has its
// body in that package body.
void validate_vhdl_package_body_subprograms(
    std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_vhdl_profile_compatibility(
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics);

void inject_vhdl_standard_libraries(
    CompilationWorkspace& checked,
    diagnostic::Engine& diagnostics,
    bool complete_environment = false);

[[nodiscard]] std::vector<library::VhdlPackageDependency>
vhdl_package_dependencies(const CheckedProject& checked);

[[nodiscard]] bool validate_vhdl_package_dependencies(
    std::span<const library::VhdlPackageDependency> archived,
    std::string_view artifact,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool compiled_vhdl_package_dependencies_match(
    const semantic::CompiledDesign& design,
    std::span<const library::VhdlPackageDependency> archived,
    std::string_view artifact,
    diagnostic::Engine& diagnostics);

std::vector<project::ProjectSection::TopLevel> selected_tops(
    const project::Config& config,
    const semantic::CompiledDesign& compiled,
    diagnostic::Engine& diagnostics);

struct BindingTarget {
    std::string language;
    std::string qualifier;
    std::string unit;
    bool unit_extended { };
    std::string architecture;
    bool architecture_extended { };
};

std::optional<BindingTarget> parse_binding_target(std::string_view target);

frontend::PortDirection systemc_direction(
    const fsim_sc_port_direction_v1 direction);

elaboration::PackedTypeMetadata systemc_type(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width);

PackedLogic4 systemc_value(
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const std::span<const std::uint8_t> storage);

elaboration::SystemCInstanceDescription systemc_description(
    const std::string_view path,
    const std::string_view target,
    const systemc::ModuleDescription& module);

std::optional<std::vector<elaboration::SystemCInstanceDescription>>
construct_systemc_instances(
    std::span<const project::ProjectSection::TopLevel> tops,
    std::span<const SystemCLibraryRegistry> registries,
    diagnostic::Engine& diagnostics);

class ApplicationSystemCFactoryProvider final
    : public elaboration::SystemCFactoryProvider {
public:
    ApplicationSystemCFactoryProvider(
        std::span<const SystemCLibraryRegistry> registries,
        const std::span<
            const elaboration::SystemCInstanceDescription>
            eager_instances,
        std::vector<std::uint64_t>& lifecycle_roots);

    [[nodiscard]] std::shared_ptr<systemc::HierarchyRegistry>
    registry_for_handle(std::uint64_t handle) const;

    std::vector<elaboration::SystemCFactoryCandidate>
    candidates() const override;

    std::vector<std::string> libraries() const override;

    std::optional<std::vector<
        elaboration::SystemCConstructionParameter>>
    schema(
        const std::string_view target,
        std::string& error) override;

    std::optional<elaboration::SystemCInstanceDescription>
    instantiate(
        const std::string_view path,
        const std::string_view target,
        const std::span<
            const std::pair<std::string, std::int64_t>>
            construction_values,
        std::string& error) override;

private:
    void record_handles(
        const std::string& path,
        const elaboration::SystemCInstanceDescription& description,
        systemc::HierarchyRegistry* registry);

    void record_handles(
        const std::string& path,
        const systemc::ModuleDescription& description,
        systemc::HierarchyRegistry* registry);

    struct HandleOwner {
        fsim_sc_handle_v1 handle { };
        systemc::HierarchyRegistry* registry { };
    };

    std::vector<SystemCLibraryRegistry> registries_;
    std::vector<std::uint64_t>& lifecycle_roots_;
    std::map<std::string, HandleOwner> handles_;
};

void validate_bindings(
    const project::Config& config,
    const semantic::CompiledDesign& compiled,
    std::span<const SystemCLibraryRegistry> systemc_registries,
    diagnostic::Engine& diagnostics);

std::string target_name();

std::string make_cache_key(
    const project::Config& config,
    const CheckedProject& checked,
    std::span<const project::ProjectSection::TopLevel> tops,
    const std::string_view resolution,
    const std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics);

void add_systemverilog_standard_package_identities(
    compiler::CacheKeyBuilder& key,
    const CheckedProject& checked);

std::optional<std::vector<std::string>>
make_specialization_cache_keys(
    const project::Config& config,
    const CheckedProject& checked,
    const semantic::design::DesignIr& design,
    std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics);

bool wildcard_match(std::string_view pattern, std::string_view text);

bool trace_selected(
    const std::vector<std::string>& filters,
    const std::string_view name);

struct VcdScale {
    std::string timescale;
    SimulationTick tick_multiplier { 1 };
};

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

class TraceObservationRecorder;
class TraceSelectionControl;

struct TraceState;

struct TraceCallbackLifetime {
    TraceState* state { };
};

enum class TraceTerminalStatus : std::uint8_t {
    open,
    complete,
    failed
};

struct TraceState {
    ~TraceState();

    std::ofstream stream;
    std::unique_ptr<runtime::VcdWriter> writer;
    std::unique_ptr<runtime::FstWriter> fst_writer;
    std::unique_ptr<runtime::TraceDeclarationModel> declarations;
    std::unique_ptr<TraceObservationRecorder> observations;
    std::unique_ptr<TraceSelectionControl> selection;
    std::shared_ptr<TraceCallbackLifetime> callback_lifetime;
    std::shared_ptr<const TraceControlApplication> control;
    std::vector<std::vector<runtime::VcdSignal>> handles;
    std::vector<runtime::VcdSignal> declaration_handles;
    std::vector<runtime::TraceSignalId> signal_trace_ids;
    std::vector<runtime::SystemVerilogScalarKind> scalar_kinds;
    std::array<runtime::TraceSignalId, 7> uvm_activity_trace_ids { };
    Simulation* simulation { };
    diagnostic::Engine* diagnostics { };
    project::TraceFormat format { project::TraceFormat::vcd };
    std::filesystem::path output_path;
    std::filesystem::path lock_directory;
    std::filesystem::path staging_path;
    std::filesystem::path backup_path;
    std::uint64_t observation_writer_observer { };
    std::uint64_t signal_change_observer { };
    std::uint64_t uvm_activity_observer { };
    SimulationTick tick_multiplier { 1 };
    TraceTerminalStatus terminal_status { TraceTerminalStatus::open };
    std::string terminal_diagnostic;
    bool collecting_fst_initial_values { true };
    bool terminal_diagnostic_reported { };
};

std::optional<project::TraceFormat> resolve_trace_format(
    const project::RunSection& run,
    diagnostic::Engine& diagnostics);

std::optional<std::int8_t> fst_timescale_exponent(
    std::string_view timescale,
    diagnostic::Engine& diagnostics);

bool prepare_trace_output(
    TraceState& state,
    const std::filesystem::path& output,
    diagnostic::Engine& diagnostics);

void fail_trace(
    TraceState& state,
    std::string_view message,
    diagnostic::Engine* diagnostics) noexcept;

bool flush_trace(TraceState& state, diagnostic::Engine& diagnostics);
bool finish_trace(TraceState& state, diagnostic::Engine& diagnostics);

struct HdlVcdState {
    ~HdlVcdState();
    std::ofstream stream;
    std::unique_ptr<runtime::VcdWriter> writer;
    std::filesystem::path file_root;
    std::filesystem::path path;
    Simulation* simulation { };
    std::function<void()> activate_observer;
    std::function<void(std::uint64_t)> remove_observer;
    std::function<SimulationTick()> current_time;
    std::vector<std::vector<runtime::VcdSignal>> handles;
    std::vector<bool> selected;
    std::vector<runtime::SystemVerilogScalarKind> scalar_kinds;
    std::vector<std::pair<std::string, runtime::simir::SignalId>> objects;
    std::vector<std::string> instances;
    std::uint64_t observer { };
    SimulationTick tick_multiplier { 1 };
    std::optional<SimulationTick> dumpvars_time;
    std::optional<std::uint64_t> byte_limit;
    bool inventory_initialized { };
    bool begun { };
    bool enabled { true };
    bool limit_reached { };

    struct ExtendedPort {
        std::string scope;
        std::string reference;
        runtime::simir::SignalId signal { };
        semantic::design::Direction direction {
            semantic::design::Direction::unknown
        };
        std::optional<std::pair<std::int64_t, std::int64_t>> range;
    };

    struct ExtendedFile {
        std::filesystem::path path;
        std::ofstream stream;
        std::vector<std::size_t> ports;
        SimulationTick tick_multiplier { 1 };
        std::uint64_t bytes_written { };
        std::optional<std::uint64_t> byte_limit;
        std::optional<SimulationTick> last_time;
        bool begun { };
        bool enabled { true };
        bool limit_reached { };
        bool closed { };
    };

    std::vector<ExtendedPort> extended_ports;
    std::vector<std::unique_ptr<ExtendedFile>> extended_files;
    std::set<std::string, std::less<>> extended_scopes;
    std::set<std::filesystem::path> extended_paths;
    std::optional<SimulationTick> dumpports_time;
};

void attach_hdl_vcd_control(
    Simulation& simulation,
    HdlVcdState& state,
    const std::filesystem::path& file_root);

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection = false,
    std::shared_ptr<const TraceControlApplication> configured = { });

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

void install_interrupt_hook(Simulation& simulation);

bool apply_uvm_command_line(
    Simulation& simulation,
    std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics);

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int handle_build(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int handle_run(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&);

int handle_run_stdio(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

enum class SimulationOutputSink {
    callback,
    builtin_stdout
};

/// Engine v4 is asked for: `--engine v4`, or FSIM_STATIC_KERNEL=1 with any
/// engine. The kernel then runs whenever cone fusion is on.
[[nodiscard]] bool static_kernel_requested(SimulationEngine engine) noexcept;

/// The static-kernel access plan of a simulation client that cone fusion
/// does not serve (docs/engine-v4-semantics.md S19, S21): `foreign` for a
/// client that may read or write any design object (VPI, VHPI, DPI, a Tcl
/// session), `observe_all` for one that records any signal (tracing, a Tcl
/// session). The debugger also makes every member a stoppable thread.
/// FSIM_STATIC_KERNEL_ACCESS_PLAN=0 restores the gates; FSIM_DEBUG_KERNEL=0
/// runs the debugger on the reference interpreter.
void plan_kernel_access(BuiltProject& built, SimulationEngine engine,
    bool foreign, bool observe_all);

int run_built_project(
    BuiltProject project,
    SimulationEngine engine,
    const project::Config& config,
    std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    SimulationOutputSink sink = SimulationOutputSink::callback);

bool compile_object(
    const project::Config& config,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

std::optional<CompilationWorkspace> check_project_for_object(
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const semantic::CompiledDesign* imported = nullptr,
    std::span<const CompiledUnitAvailability> available_units = { });

int handle_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_elaborate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_simulate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_simulate_stdio(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_systemc_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_systemc_link(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_coverage_merge(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

int handle_coverage_report(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error);

enum class DebugBreakpointKind {
    time,
    signal,
    watch,
    source,
    phase,
    uvm,
};

struct DebugBreakpoint {
    std::uint64_t id { };
    DebugBreakpointKind kind { DebugBreakpointKind::time };
    SimulationTick time { };
    SignalId signal { };
    std::string path;
    std::uint32_t line { };
    std::optional<PackedLogic4> signal_condition;
    bool signal_condition_equal { true };
};

struct DebugBreakpointHit {
    std::uint64_t id { };
    std::string description;
};

class DebuggerSession final {
public:
    DebuggerSession(
        Simulation& simulation,
        std::ostream& output,
        std::ostream& error,
        TraceState* trace = nullptr);

    ~DebuggerSession();

    DebuggerSession(const DebuggerSession&) = delete;
    DebuggerSession& operator=(const DebuggerSession&) = delete;

    void execute(const std::vector<std::string>& command);

    [[nodiscard]] DebuggerStatus status() const;
    void step(std::string_view kind);
    void continue_run(std::optional<std::string_view> duration);
    [[nodiscard]] DebuggerBreakpointInfo add_breakpoint(
        std::string_view kind,
        std::string_view location,
        std::optional<std::string_view> comparison,
        std::optional<std::string_view> value);
    [[nodiscard]] DebuggerBreakpointInfo add_watch(
        std::string_view signal,
        std::optional<std::string_view> comparison,
        std::optional<std::string_view> value);
    [[nodiscard]] std::vector<DebuggerBreakpointInfo> breakpoints() const;
    [[nodiscard]] std::vector<DebuggerBreakpointInfo> watches() const;
    void delete_breakpoint(std::uint64_t id);
    void delete_watch(std::uint64_t id);
    void clear_breakpoints();
    [[nodiscard]] std::vector<DebuggerFrameInfo> frames() const;
    [[nodiscard]] DebuggerFrameInfo frame(std::size_t index) const;
    [[nodiscard]] DebuggerScopeInfo scope(
        std::optional<std::string_view> path);
    [[nodiscard]] DebuggerInspection inspect(std::string_view path) const;
    [[nodiscard]] std::vector<DebuggerSourceProvenance> provenance(
        std::optional<std::string_view> path) const;

private:
    struct ExecutionGuard {
        bool& executing;
        ~ExecutionGuard();
    };

    [[nodiscard]] static std::string format_value(
        const PackedLogic4& value,
        const std::vector<std::string>& enumeration_literals);

    [[nodiscard]] bool canonical_path(const std::string_view path) const;

    [[nodiscard]] std::vector<std::string> lexical_paths(
        std::string_view name) const;

    [[nodiscard]] std::optional<std::string> resolve_scope(
        const std::string_view requested) const;

    [[nodiscard]] std::optional<std::pair<std::string, SignalId>>
    resolve_signal(const std::string_view name, bool report_error = true);

    [[nodiscard]] std::optional<std::pair<
        std::string, runtime::simir::StringObjectId>>
    resolve_string_object(std::string_view name) const;
    [[nodiscard]] std::optional<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
    resolve_container_object(std::string_view name) const;

    [[nodiscard]] std::optional<SimulationTick> command_time(
        const std::string_view text);

    void scope_command(const std::vector<std::string>& command);

    void scopes_command(const std::vector<std::string>& command);

    void signals_command(const std::vector<std::string>& command);

    void modify_signal(const std::vector<std::string>& command);

    void show_locals();

    void uvm_command(const std::vector<std::string>& command);

    void vhdl_command(const std::vector<std::string>& command);

    [[nodiscard]] std::vector<std::pair<
        std::string, runtime::SystemVerilogUvmPhaseState>>
    phase_states() const;

    void trace_command(const std::vector<std::string>& command);

    void set_trace_enabled(const SignalId signal, const bool enable);

    void add_breakpoint(const std::vector<std::string>& command);

    void list_breakpoints() const;

    void delete_breakpoint(const std::string_view id_text);

    [[nodiscard]] std::optional<DebugBreakpoint> earliest_time_breakpoint(
        const SimulationTick start,
        const std::optional<SimulationTick> requested_limit) const;

    [[nodiscard]] static bool source_path_matches(
        const std::string_view requested,
        const std::string_view actual);

    [[nodiscard]] static bool is_statement_point(
        const runtime::simir::ExecutionPointKind kind) noexcept;

    void install_execution_hook(
        const std::optional<DebugBreakpoint>& time_breakpoint,
        const std::function<bool(runtime::Scheduler&, runtime::SchedulerPhase)>&
            additional_stop = { },
        const std::function<bool(const runtime::simir::ExecutionPoint&)>&
            additional_execution_stop = { });

    void report_execution_point();

    void report_result(const runtime::RunResult& result);

    [[nodiscard]] bool can_execute();

    void run(const std::optional<SimulationTick> requested_limit);

    void step(const bool delta_step);

    void step_phase();

    void step_execution(const bool process_step);

    [[nodiscard]] std::vector<std::string> child_scopes(
        std::string_view base) const;
    [[nodiscard]] DebuggerBreakpointInfo breakpoint_info(
        const DebugBreakpoint& breakpoint) const;
    void require_executable();

    Simulation& simulation_;
    std::ostream& output_;
    std::ostream& error_;
    TraceState* trace_ { };
    std::string scope_;
    std::vector<std::pair<std::string, SignalId>> signal_paths_;
    std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
        container_paths_;
    std::vector<std::string> execution_scope_paths_;
    std::vector<DebugBreakpoint> breakpoints_;
    std::optional<DebugBreakpointHit> hit_;
    std::optional<runtime::simir::ExecutionPoint> current_execution_point_;
    std::optional<std::string> structured_execution_error_;
    std::vector<std::pair<
        std::string, runtime::SystemVerilogUvmPhaseState>>
        phase_states_;
    std::optional<std::string> phase_transition_;
    std::uint64_t next_breakpoint_ { 1 };
    std::uint64_t observer_ { };
    std::uint64_t uvm_observer_ { };
    bool executing_ { };
    bool stop_on_phase_transition_ { };
};

struct ParsedMagnitude {
    std::uint64_t magnitude { };
    std::string unit;
};

std::optional<ParsedMagnitude> magnitude_and_unit(std::string_view text);

std::optional<std::uint64_t> unit_femtoseconds(std::string_view unit);

template <typename Function>
void visit_delay(frontend::Delay& delay, Function& function);

template <typename Function>
void visit_delays(
    std::vector<frontend::Statement>& statements,
    Function&& function);

void validate_vhdl_rejection_limits(
    const std::vector<frontend::Statement>& statements,
    diagnostic::Engine& diagnostics,
    bool& valid);

std::string effective_resolution(
    const project::Config& config,
    const semantic::CompiledDesign& compiled);

bool normalize_delays(
    frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

bool normalize_delays(
    semantic::CompiledDesign& compiled,
    std::string_view resolution,
    diagnostic::Engine& diagnostics);

void select_delay_alternatives(
    frontend::ParsedDesign& parsed,
    const project::DelayMode mode);

void select_delay_alternatives(
    semantic::CompiledDesign& compiled,
    project::DelayMode mode);

bool validate_declared_time_precisions(
    const semantic::CompiledDesign& compiled,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::application_detail
