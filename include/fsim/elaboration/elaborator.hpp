// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/hierarchy_path.hpp"
#include "fsim/systemc_abi.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::elaboration {

struct ElaborationResult;
class Lowerer;
class HierarchyBuilder;

struct Binding {
    std::string instance;
    std::optional<std::string> target;
    std::optional<std::string> resolver;
};

/// One independently selected simulation root. `target` follows the same
/// qualified or inferred spelling as the legacy singular top; `alias` is the
/// stable first hierarchy component visible to tracing and debugging.
struct Root {
    std::string target;
    std::string alias;
};

/// Parser-independent identity of a class made visible by one selected
/// SystemVerilog generate branch. The CompiledDesign supplied to elaboration
/// owns the declaration and every executable member referenced by this
/// record.
struct SelectedSystemVerilogClass {
    semantic::UnitId unit;
    semantic::DeclarationId generate_owner;
    semantic::ScopeId declaration_scope;
    semantic::OriginId origin;
    std::string declaration_identity;

    friend bool operator==(
        const SelectedSystemVerilogClass&,
        const SelectedSystemVerilogClass&) = default;
};

struct PackedTypeMetadata;

struct PackedMemberMetadata {
    std::string name;
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    std::string spelling;
    std::optional<frontend::PackedRange> packed_range;
    bool is_signed { };
    std::uint64_t lsb_offset { };
    frontend::SourceSpan span;
    std::vector<PackedTypeMetadata> nested_types;

    [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

struct VhdlArrayDimensionMetadata {
    std::string index_subtype;
    frontend::SourceSpan index_span;
    std::optional<frontend::IntegerRange> index_base_range;
    std::optional<frontend::IntegerRange> range;
    bool null { };
    std::uint64_t stride { };
    bool unconstrained { };
};

struct VhdlArrayMetadata {
    std::string index_subtype;
    frontend::SourceSpan index_span;
    std::optional<frontend::IntegerRange> index_base_range;
    std::string element_spelling;
    std::string element_named_type;
    frontend::SourceSpan element_span;
    frontend::ValueDomain element_domain { frontend::ValueDomain::Unknown };
    bool unconstrained { };
    std::optional<std::uint64_t> flat_width;
    std::vector<VhdlArrayDimensionMetadata> dimensions;
    std::vector<PackedTypeMetadata> element_types;
};

struct VhdlAccessMetadata {
    std::vector<PackedTypeMetadata> designated_types;
    frontend::SourceSpan designated_span;
    std::uint32_t handle_width { 32U };
    std::uint32_t maximum_objects {
        std::numeric_limits<std::uint32_t>::max()
    };
    bool nullable { true };
    bool owns_designated_object { true };
    bool deallocate_releases_storage { true };
    bool reclaim_when_unreachable { };
    bool simulation_lifetime { true };
};

struct VhdlPhysicalUnitMetadata {
    std::string name;
    std::optional<std::int64_t> scale_factor;
    frontend::SourceSpan span;
};

struct VhdlPhysicalMetadata {
    std::vector<VhdlPhysicalUnitMetadata> units;
    std::optional<frontend::IntegerRange> resolved_range;
};

/// Concrete type metadata retained across elaboration and DesignIR
/// construction. It contains recursive layout and source identity, but cannot
/// own syntax expressions, statements, or parser type nodes.
struct PackedTypeMetadata {
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    std::string spelling;
    frontend::SystemVerilogScalarKind systemverilog_scalar {
        frontend::SystemVerilogScalarKind::None
    };
    std::string systemverilog_net_type;
    std::string systemverilog_resolution_function;
    std::optional<frontend::PackedRange> packed_range;
    bool is_signed { };
    std::string named_type;
    frontend::SourceSpan named_type_span;
    std::optional<frontend::IntegerRange> integer_range;
    std::optional<frontend::IntegerRange> integer_base_range;
    std::uint8_t vhdl_integer_storage_width { };
    std::string nominal_type;
    std::string vhdl_type_declaration;
    std::string vhdl_resolution_function;
    std::vector<std::string> enumeration_literals;
    std::optional<frontend::EnumerationRange> enumeration_range;
    std::optional<frontend::EnumerationRange> enumeration_base_range;
    std::shared_ptr<VhdlArrayMetadata> vhdl_array;
    std::shared_ptr<VhdlAccessMetadata> vhdl_access;
    std::shared_ptr<VhdlPhysicalMetadata> vhdl_physical;
    std::vector<PackedMemberMetadata> packed_members;
    frontend::PackedAggregateKind packed_aggregate {
        frontend::PackedAggregateKind::None
    };

    PackedTypeMetadata() = default;
    PackedTypeMetadata(
        const frontend::ValueDomain domain_value,
        std::string spelling_value,
        std::optional<frontend::PackedRange> range_value,
        const bool signed_value)
        : domain(domain_value)
        , spelling(std::move(spelling_value))
        , packed_range(std::move(range_value))
        , is_signed(signed_value)
    {
    }

    [[nodiscard]] std::optional<std::uint64_t> width() const noexcept
    {
        if (systemverilog_scalar
            == frontend::SystemVerilogScalarKind::ShortReal) {
            return 32U;
        }
        if (systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Real
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Realtime
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Time
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Chandle) {
            return 64U;
        }
        if (vhdl_array && vhdl_array->flat_width) {
            return vhdl_array->flat_width;
        }
        if (packed_range) {
            return packed_range->width();
        }
        if (vhdl_array || !packed_members.empty()) {
            return std::nullopt;
        }
        switch (domain) {
        case frontend::ValueDomain::Bit2:
        case frontend::ValueDomain::Logic4:
        case frontend::ValueDomain::Logic9:
        case frontend::ValueDomain::Boolean:
            return 1U;
        case frontend::ValueDomain::Integer:
            return vhdl_integer_storage_width != 0U
                ? std::optional<std::uint64_t> {
                      vhdl_integer_storage_width }
                : std::optional<std::uint64_t> { 32U };
        case frontend::ValueDomain::String:
        case frontend::ValueDomain::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }
};

inline std::optional<std::uint64_t>
PackedMemberMetadata::width() const noexcept
{
    if (!nested_types.empty()) {
        return nested_types.front().width();
    }
    if (packed_range) {
        return packed_range->width();
    }
    switch (domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Logic4:
    case frontend::ValueDomain::Logic9:
    case frontend::ValueDomain::Boolean:
        return 1U;
    case frontend::ValueDomain::Integer:
        return 32U;
    case frontend::ValueDomain::String:
    case frontend::ValueDomain::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

struct ExternalPort {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
    std::uint64_t bound_object { };
};

struct ExternalSensitivity {
    std::uint64_t object { };
    // Store untrusted ABI metadata as an integer so validation can inspect an
    // out-of-range value without first performing an undefined C++ enum load.
    std::uint32_t edge { FSIM_SC_ANY_EDGE };
};

struct ExternalProcess {
    std::uint64_t handle { };
    std::string name;
    fsim_sc_process_entry_v1 entry { };
    void* user { };
    std::vector<ExternalSensitivity> sensitivity;
};

struct ExternalInternalSignal {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    runtime::PackedLogic4 initial_value;
};

struct ExternalExport {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    std::uint64_t bound_object { };
    bool writable { true };
};

/// Immutable, ABI-neutral description produced by one constructed SystemC
/// elaboration factory. `path` is the full DesignIR instance path and `target`
/// is the canonical `systemc:plugin.factory` manifest spelling.
struct SystemCInstanceDescription {
    std::string path;
    std::string target;
    std::uint64_t handle { };
    std::uint64_t parent { };
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<ExternalPort> ports;
    std::vector<ExternalProcess> processes;
    std::vector<ExternalInternalSignal> internal_signals;
    std::vector<ExternalExport> exports;
    std::vector<SystemCInstanceDescription> native_children;
    // Canonical typed construction values supplied by the authoritative HDL
    // specialization path. Kept trailing for source compatibility with
    // aggregate descriptions produced by existing plug-in bridges.
    std::vector<std::pair<std::string, std::string>>
        construction_identity_values;
};

struct SystemCConstructionParameter {
    std::string name;
    fsim_sc_construction_type_v1 type {
        FSIM_SC_CONSTRUCTION_INTEGER
    };
    std::optional<std::int64_t> default_value;
};

struct SystemCFactoryCandidate {
    std::string library;
    std::string name;
    std::string target;
};

/// Application-owned bridge used by the authoritative hierarchy walk to
/// inspect a registered factory schema and construct an HDL-bound SystemC
/// instance only after source-language actuals have been canonicalized.
class SystemCFactoryProvider {
public:
    virtual ~SystemCFactoryProvider() = default;

    [[nodiscard]] virtual std::vector<SystemCFactoryCandidate>
    candidates() const = 0;

    // Logical-library availability is distinct from exporting at least one
    // factory. The default preserves source compatibility for providers which
    // expose only candidate records.
    [[nodiscard]] virtual std::vector<std::string> libraries() const
    {
        std::vector<std::string> result;
        for (const auto& candidate : candidates()) {
            if (std::ranges::find(result, candidate.library)
                == result.end()) {
                result.push_back(candidate.library);
            }
        }
        return result;
    }

    [[nodiscard]] virtual std::optional<
        std::vector<SystemCConstructionParameter>>
    schema(std::string_view target, std::string& error) = 0;

    [[nodiscard]] virtual std::optional<SystemCInstanceDescription>
    instantiate(
        std::string_view path,
        std::string_view target,
        std::span<const std::pair<std::string, std::int64_t>>
            construction_values,
        std::string& error) = 0;
};

/// Parser-free compiled-HIR elaboration. Unsupported residual structures are
/// diagnosed instead of being reconstructed as syntax nodes.
[[nodiscard]] ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    std::string_view top);
[[nodiscard]] ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    std::span<const Root> roots,
    std::span<const Binding> bindings,
    std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    std::span<const std::string> search_libraries);

struct Diagnostic {
    std::string code;
    std::string message;
    frontend::SourceSpan span;
};

struct VhdlModeViewElementBinding {
    std::string formal_path;
    std::string actual_path;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::SourceSpan source;
    // Explicit packed-storage endpoint after every record selection and
    // concrete array index has been elaborated. A view never creates separate
    // storage: each leaf addresses the associated signal directly.
    runtime::simir::SignalId signal { };
    std::uint64_t lsb_offset { };
    std::uint64_t width { };
};

struct VhdlModeViewBinding {
    std::string formal;
    std::string view;
    frontend::VhdlModeViewIndicationKind kind {
        frontend::VhdlModeViewIndicationKind::record
    };
    std::vector<VhdlModeViewElementBinding> elements;
    frontend::SourceSpan source;
};

struct SignalInfo {
    runtime::simir::SignalId id { };
    std::string name;
    std::size_t width { };
    std::string type_name;
    frontend::ValueDomain source_domain { frontend::ValueDomain::Unknown };
    frontend::SystemVerilogScalarKind systemverilog_scalar {
        frontend::SystemVerilogScalarKind::None
    };
    std::string systemverilog_net_type;
    bool is_signed { };
    std::optional<frontend::PackedRange> packed_range;
    // VHDL-only type metadata is large (physical-type metadata alone exceeds
    // one KiB) and absent from the overwhelmingly common scalar/SV signal.
    // Box it so every SignalInfo does not reserve storage for inactive
    // language-specific alternatives.
    std::shared_ptr<VhdlArrayMetadata> vhdl_array;
    std::shared_ptr<VhdlAccessMetadata> vhdl_access;
    std::shared_ptr<VhdlPhysicalMetadata> vhdl_physical;
    std::vector<PackedMemberMetadata> packed_members;
    std::optional<frontend::IntegerRange> integer_range;
    std::string nominal_type;
    std::vector<std::string> enumeration_literals;
    std::optional<frontend::EnumerationRange> enumeration_range;
    bool is_port { };
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::SourceSpan declaration_span;
    // A composite signal can cross several view-based VHDL boundaries. Each
    // binding retains the declaration-order formal-to-actual leaf map; Batch
    // 184 materializes these leaves as directional runtime endpoints.
    std::vector<VhdlModeViewBinding> vhdl_mode_view_bindings;
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none
    };
};

/// One explicit cross-language scalar or vector boundary conversion.
///
/// Packed values store the leftmost declared element at the most-significant
/// ordinal independently of source-language index spelling. Compatible
/// ordinal and Logic4/Logic9 boundaries share one scheduler signal. Width,
/// signedness, Boolean, and integer conversions use a separately owned formal
/// signal and deterministic adapter process. This record retains both
/// language-local profiles and the connection's ownership and source identity
/// for diagnostics, debugging, and cache projection.
enum class BoundaryConversionKind : std::uint8_t {
    ordinal_alias,
    width_adapter,
    signedness_adapter,
    width_signedness_adapter,
    boolean_adapter,
    integer_adapter,
    state_domain_alias,
};

struct BoundaryConversionInfo {
    BoundaryConversionKind kind { BoundaryConversionKind::ordinal_alias };
    std::string path;
    runtime::simir::SignalId formal_signal { };
    runtime::simir::SignalId actual_signal { };
    std::optional<runtime::simir::ProcessId> process;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    std::size_t formal_width { };
    std::size_t actual_width { };
    frontend::ValueDomain formal_domain { frontend::ValueDomain::Unknown };
    frontend::ValueDomain actual_domain { frontend::ValueDomain::Unknown };
    bool formal_signed { };
    bool actual_signed { };
    bool state_domain_changed { };
    std::optional<frontend::PackedRange> formal_range;
    std::optional<frontend::PackedRange> actual_range;
    std::optional<frontend::IntegerRange> formal_integer_range;
    std::optional<frontend::IntegerRange> actual_integer_range;
    frontend::SourceSpan connection_span;
    frontend::SourceSpan formal_span;
    frontend::SourceSpan actual_span;
};

struct StringObjectInfo {
    runtime::simir::StringObjectId id { };
    std::string name;
    frontend::SourceSpan declaration_span;
    bool is_port { };
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
};

struct ContainerObjectInfo {
    runtime::simir::ContainerObjectId id { };
    std::string name;
    runtime::simir::ContainerType type;
    frontend::SourceSpan declaration_span;
    bool is_port { };
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
    std::optional<runtime::simir::ContainerSliceAlias> slice_alias;
};

using VhdlProtectedObjectId = std::uint32_t;

struct VhdlProtectedMemberInfo {
    std::string name;
    bool is_signed { };
    std::size_t offset { };
    std::size_t width { };
    runtime::simir::ContainerObjectId storage { };
    frontend::SourceSpan declaration_span;
};

/// One constructed VHDL shared variable of protected type. Private members
/// use independently addressable global container objects, while this record
/// preserves their common object identity and declaration-ordered layout.
struct VhdlProtectedObjectInfo {
    VhdlProtectedObjectId id { };
    std::string name;
    std::string type_name;
    std::string nominal_type;
    std::vector<VhdlProtectedMemberInfo> members;
    frontend::SourceSpan declaration_span;
};

using SpecializationId = std::uint32_t;

/// One elaborated design-unit occurrence and its directly owned processes.
///
/// Parameterized VHDL and Verilog/SystemVerilog occurrences carry canonical
/// generic/parameter values after constant evaluation. Each occurrence remains
/// an explicit specialization record so process ownership and cache identity
/// never depend on recovering hierarchy from process-name strings.
struct SpecializationInfo {
    SpecializationId id { };
    std::string unit;
    std::string instance;
    // Exact semantic provenance for compiled-HIR hierarchy occurrences.
    // Transitional syntax-only callers may leave these unset while their
    // lowering path is ported.
    std::optional<semantic::UnitId> source_unit;
    std::optional<semantic::InstanceId> source_instance;
    std::optional<semantic::SourceSpanId> source_span;
    std::optional<semantic::OriginId> origin;
    std::vector<runtime::simir::ProcessId> processes;
    std::string source;
    // Additional source roots that define this specialization's interface,
    // such as a VHDL entity paired with an architecture in another file.
    std::vector<std::string> source_dependencies;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    std::string library { "work" };
    bool is_cell { };
    // Canonical name/value pairs after frontend generic/parameter evaluation.
    // Empty for unparameterized units.
    std::vector<std::pair<std::string, std::string>> parameter_values;
    // Versioned semantic identities used by the native-object cache when a
    // display value alone would lose width, signedness, state, or type
    // metadata. Empty entries fall back to parameter_values.
    std::vector<std::pair<std::string, std::string>>
        parameter_identity_values;
    // Direct provenance for processes lowered from language HIR. Synthetic
    // concurrent, boundary, timing, and foreign processes intentionally do
    // not appear here; callers must not infer semantic ownership by position
    // in `processes`. Kept trailing for source compatibility with existing
    // aggregate descriptions.
    std::vector<std::pair<runtime::simir::ProcessId, semantic::ProcessId>>
        semantic_processes;
};

using UdpTableId = std::uint32_t;

/// One declaration-normalized Verilog user-defined primitive table.
///
/// The table is stored once per canonical logical-library identity. Every UDP
/// instance references its stable ID/digest through specialization provenance.
struct UdpTableInfo {
    UdpTableId id { };
    std::string identity;
    std::string digest;
    bool sequential { };
    std::optional<frontend::VerilogUdpOutputSymbol> initial_output;
    std::vector<std::string> terminals;
    std::vector<frontend::VerilogUdpTableRow> rows;
};

struct SystemCPortInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCEventInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCPrimitiveChannelInfo {
    std::string name;
    std::uint64_t native_handle { };
};

struct SystemCSignalInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCExportInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
    bool writable { true };
};

struct SystemCInstanceInfo {
    std::uint32_t id { };
    std::string target;
    std::string instance;
    std::uint64_t native_handle { };
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<std::pair<std::string, std::string>>
        construction_identity_values;
    std::vector<SystemCPortInfo> ports;
    std::vector<SystemCEventInfo> events;
    std::vector<SystemCPrimitiveChannelInfo> primitive_channels;
    std::vector<SystemCSignalInfo> internal_signals;
    std::vector<SystemCExportInfo> exports;
};

struct SystemCProcessInfo {
    runtime::simir::ProcessId process { };
    std::uint64_t native_handle { };
};

enum class SystemCNamedObjectKind : std::uint8_t {
    module,
    port,
    foreign_child,
    process,
    event,
    primitive_channel,
    signal,
    export_object,
};

/// One debug-visible object from a compiled SystemC hierarchy. Names and
/// parent names are common DesignIR paths; value-bearing aliases retain their
/// distinct object identity while sharing the referenced dense signal.
struct SystemCNamedObjectInfo {
    SystemCNamedObjectKind kind { SystemCNamedObjectKind::module };
    std::uint64_t native_handle { };
    std::string name;
    std::string parent;
    std::string type_name;
    std::optional<runtime::simir::SignalId> signal;
    std::optional<runtime::simir::ProcessId> process;
    runtime::simir::SourceLocation source;
};

using VerilogSpecifyPathId = std::uint32_t;

struct VerilogSpecifyTerminalInfo {
    runtime::simir::SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    friend bool operator==(
        const VerilogSpecifyTerminalInfo&,
        const VerilogSpecifyTerminalInfo&) = default;
};

/// One instance-local, specialization-normalized Verilog module path.
/// Conditions and destination data sources are retained only as
/// parser-independent scheduler programs after specialization.
struct VerilogSpecifyPathInfo {
    VerilogSpecifyPathId id { };
    std::string identity;
    std::string instance;
    std::vector<VerilogSpecifyTerminalInfo> sources;
    std::vector<VerilogSpecifyTerminalInfo> destinations;
    std::vector<runtime::simir::ProcessId> drivers;
    runtime::simir::ModulePathExpression condition_program;
    runtime::simir::ModulePathExpression data_source_program;
    std::uint32_t selection_group { };
    frontend::VerilogModulePathKind kind {
        frontend::VerilogModulePathKind::Parallel
    };
    frontend::VerilogSpecifyEdge source_edge {
        frontend::VerilogSpecifyEdge::None
    };
    frontend::VerilogPathPolarity polarity {
        frontend::VerilogPathPolarity::None
    };
    frontend::VerilogPulseStyle pulse_style {
        frontend::VerilogPulseStyle::Onevent
    };
    bool show_cancelled { };
    std::optional<runtime::SimulationTick> pulse_reject_limit;
    std::optional<runtime::SimulationTick> pulse_error_limit;
    std::vector<runtime::SimulationTick> pulse_reject_delays;
    std::vector<runtime::SimulationTick> pulse_error_delays;
    std::vector<runtime::SimulationTick> retain_delays;
    bool conditional { };
    bool ifnone { };
    std::vector<runtime::SimulationTick> delays;
    frontend::SourceSpan source;
};

struct ElaboratedDesignState {
    std::string top;
    std::vector<std::string> roots;
    std::vector<SignalInfo> signal_info;
    std::vector<BoundaryConversionInfo> boundary_conversions;
    std::vector<runtime::simir::Signal> signals;
    std::vector<StringObjectInfo> string_object_info;
    std::vector<runtime::simir::StringObject> string_objects;
    std::vector<ContainerObjectInfo> container_object_info;
    std::vector<runtime::simir::ContainerObject> container_objects;
    std::vector<runtime::simir::ContainerSignalAlias>
        container_signal_aliases;
    std::vector<VhdlProtectedObjectInfo> vhdl_protected_object_info;
    std::vector<runtime::simir::Process> processes;
    std::vector<SpecializationInfo> specializations;
    std::vector<UdpTableInfo> udp_tables;
    std::vector<VerilogSpecifyPathInfo> verilog_specify_paths;
    std::vector<runtime::simir::ModuleTimingCheck> verilog_timing_checks;
    std::vector<SystemCInstanceInfo> systemc_instances;
    std::vector<SystemCProcessInfo> systemc_processes;
    std::vector<SystemCNamedObjectInfo> systemc_objects;
    std::vector<std::pair<std::string, runtime::simir::SignalId>> signal_names;
    std::vector<std::pair<std::string, runtime::simir::StringObjectId>>
        string_names;
    std::vector<std::pair<std::string, runtime::simir::ContainerObjectId>>
        container_names;
    std::optional<CodeCoverageInventory> code_coverage_inventory;
};

class ElaboratedDesign final {
public:
    ElaboratedDesign() = default;
    explicit ElaboratedDesign(const std::span<const Root> roots)
    {
        roots_.reserve(roots.size());
        for (const auto& root : roots) {
            roots_.push_back(root.alias);
        }
        if (!roots_.empty()) {
            top_ = roots_.front();
        }
    }

    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;
    [[nodiscard]] const semantic::HierarchyPathTable&
    hierarchy_paths() const noexcept;
    /// Accept an extended table only when every existing ID still names the
    /// same path. DesignIR may append derived paths after elaboration.
    [[nodiscard]] bool rebind_path_table(
        semantic::HierarchyPathTable paths);
    /// Reassign path-keyed indexes when the destination table has the same
    /// spellings under different IDs, as in a canonical artifact table.
    [[nodiscard]] bool remap_path_table(
        semantic::HierarchyPathTable paths);
    [[nodiscard]] const std::vector<SignalInfo>& signals() const noexcept;
    [[nodiscard]] const std::vector<BoundaryConversionInfo>&
    boundary_conversions() const noexcept;
    [[nodiscard]] const std::vector<StringObjectInfo>&
    string_objects() const noexcept;
    [[nodiscard]] const std::vector<ContainerObjectInfo>&
    container_objects() const noexcept;
    [[nodiscard]] const std::vector<VhdlProtectedObjectInfo>&
    vhdl_protected_objects() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::Process>& processes() const noexcept;
    [[nodiscard]] const std::vector<SpecializationInfo>&
    specializations() const noexcept;
    [[nodiscard]] const std::vector<UdpTableInfo>&
    udp_tables() const noexcept;
    [[nodiscard]] const std::vector<VerilogSpecifyPathInfo>&
    verilog_specify_paths() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::ModuleTimingCheck>&
    verilog_timing_checks() const noexcept;
    [[nodiscard]] const std::vector<SystemCInstanceInfo>&
    systemc_instances() const noexcept;
    [[nodiscard]] const std::vector<SystemCProcessInfo>&
    systemc_processes() const noexcept;
    [[nodiscard]] const std::vector<SystemCNamedObjectInfo>&
    systemc_objects() const noexcept;
    [[nodiscard]] const std::optional<CodeCoverageInventory>&
    code_coverage_inventory() const noexcept;
    [[nodiscard]] CoverageInventoryValidationResult
    attach_code_coverage_inventory(
        std::span<const CoverageInventorySource> sources,
        std::span<const CoverageInstanceInventoryDraft> instances,
        CoverageInventoryLimits limits = { }) noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
        std::string_view name) const noexcept;
    /// Return every debug-visible signal path in lexical order. Boundary-port
    /// aliases may refer to the same dense signal ID as their connected
    /// parent signal.
    [[nodiscard]] std::vector<std::pair<std::string, runtime::simir::SignalId>>
    signal_paths() const;
    [[nodiscard]] std::optional<runtime::simir::ContainerObjectId>
    find_container(std::string_view name) const noexcept;
    /// Return every debug-visible container path in lexical order. Container
    /// port aliases may refer to the same bounded object ID as their connected
    /// parent object.
    [[nodiscard]] std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
    container_paths() const;

    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options = { },
        std::uint64_t seed = 1) const &;
    /// Consume the immutable SimIR process programs when the elaborated design
    /// is no longer needed as their owner. Other design metadata remains
    /// available for runtime services and native-executor setup.
    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options = { },
        std::uint64_t seed = 1) &&;

    [[nodiscard]] ElaboratedDesignState state() const &;
    /// Transfer the complete portable state out of a design that has reached
    /// its final ownership boundary. This avoids duplicating every process
    /// program while publishing a standalone design artifact.
    [[nodiscard]] ElaboratedDesignState state() &&;
    [[nodiscard]] static std::optional<ElaboratedDesign> from_state(
        ElaboratedDesignState state);

private:
    friend struct ElaborationResult;
    friend class Lowerer;

    void populate_interpreter(
        runtime::simir::Interpreter* interpreter,
        bool validation_only,
        std::vector<runtime::simir::Process>* consumed_processes = nullptr) const;
    friend class HierarchyBuilder;

    struct HierarchyPathHash final {
        [[nodiscard]] std::size_t operator()(
            semantic::HierarchyPathId id) const noexcept
        {
            return id.value();
        }
    };

    void freeze_hierarchy_paths();

    std::string top_;
    std::vector<std::string> roots_;
    std::vector<SignalInfo> signal_info_;
    std::vector<BoundaryConversionInfo> boundary_conversions_;
    std::vector<runtime::simir::Signal> signals_;
    std::vector<StringObjectInfo> string_object_info_;
    std::vector<runtime::simir::StringObject> string_objects_;
    std::vector<ContainerObjectInfo> container_object_info_;
    std::vector<runtime::simir::ContainerObject> container_objects_;
    std::vector<runtime::simir::ContainerSignalAlias>
        container_signal_aliases_;
    std::vector<VhdlProtectedObjectInfo> vhdl_protected_object_info_;
    std::vector<runtime::simir::Process> processes_;
    std::vector<SpecializationInfo> specializations_;
    std::vector<UdpTableInfo> udp_tables_;
    std::vector<VerilogSpecifyPathInfo> verilog_specify_paths_;
    std::vector<runtime::simir::ModuleTimingCheck> verilog_timing_checks_;
    std::vector<SystemCInstanceInfo> systemc_instances_;
    std::vector<SystemCProcessInfo> systemc_processes_;
    std::vector<SystemCNamedObjectInfo> systemc_objects_;
    std::optional<CodeCoverageInventory> code_coverage_inventory_;
    semantic::HierarchyPathTable hierarchy_paths_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::SignalId, HierarchyPathHash> signal_by_path_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::StringObjectId, HierarchyPathHash> string_by_path_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::ContainerObjectId, HierarchyPathHash>
        container_by_path_;
    // Construction drafts. Cleared before the design is published.
    std::unordered_map<std::string, runtime::simir::SignalId> signal_by_name_;
    std::unordered_map<
        std::string, runtime::simir::StringObjectId>
        string_by_name_;
    std::unordered_map<
        std::string, runtime::simir::ContainerObjectId>
        container_by_name_;
};

struct ElaborationResult {
    std::optional<ElaboratedDesign> design;
    std::vector<Diagnostic> diagnostics;
    // Non-failing elaboration-time language messages remain ordered beside
    // ordinary errors without making a successful design unavailable.
    std::vector<frontend::Diagnostic> messages;
    // Generate declarations are selected only after parameters and hierarchy
    // are known. Return stable compiled-HIR identities; syntax declarations
    // never cross the elaboration boundary.
    std::vector<SelectedSystemVerilogClass>
        selected_systemverilog_classes;

    [[nodiscard]] bool ok() const noexcept
    {
        return design.has_value() && diagnostics.empty();
    }
};

/// Elaborate one compiled VHDL entity/architecture or Verilog module.
///
/// VHDL names are already canonicalized in the compiled HIR. `top` accepts a
/// simple unit name or a qualified manifest spelling such as
/// `sv:work.counter` or `vhdl:work.counter(rtl)`.
} // namespace fsim::elaboration
