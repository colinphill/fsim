// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/systemc_abi.h"

#include <algorithm>
#include <cstdint>
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

struct ExternalPort {
    std::uint64_t handle{};
    std::string name;
    frontend::Type type;
    frontend::PortDirection direction{
        frontend::PortDirection::Unknown};
    std::uint64_t bound_object{};
};

struct ForeignPort {
    std::string name;
    frontend::Type type;
    frontend::PortDirection direction{
        frontend::PortDirection::Unknown};
    std::uint64_t object{};
    std::uint64_t handle{};
};

struct ForeignChild {
    std::uint64_t handle{};
    std::string name;
    std::vector<std::pair<std::string, std::int64_t>>
        construction_actuals;
    std::vector<ForeignPort> ports;
    bool module_facade{};
    std::string implementation;
};

struct ExternalSensitivity {
    std::uint64_t object{};
    // Store untrusted ABI metadata as an integer so validation can inspect an
    // out-of-range value without first performing an undefined C++ enum load.
    std::uint32_t edge{FSIM_SC_ANY_EDGE};
};

struct ExternalProcess {
    std::uint64_t handle{};
    std::string name;
    fsim_sc_process_kind_v1 kind{FSIM_SC_METHOD};
    fsim_sc_process_entry_v1 entry{};
    void* user{};
    std::vector<ExternalSensitivity> sensitivity;
    bool initialize{true};
};

struct ExternalEvent {
    std::uint64_t handle{};
    std::string name;
};

struct ExternalPrimitiveChannel {
    std::uint64_t handle{};
    std::string name;
    std::string kind{"sc_prim_channel"};
};

struct ExternalMetadataObject {
    std::uint64_t handle{};
    std::string name;
    fsim_sc_metadata_category_v1 category{FSIM_SC_METADATA_PORT};
    std::string kind;
};

struct ExternalInternalSignal {
    std::uint64_t handle{};
    std::string name;
    frontend::Type type;
    runtime::PackedLogic4 initial_value;
};

struct ExternalExport {
    std::uint64_t handle{};
    std::string name;
    frontend::Type type;
    std::uint64_t bound_object{};
    bool writable{true};
};

/// Immutable, ABI-neutral description produced by one constructed SystemC
/// elaboration factory. `path` is the full DesignIR instance path and `target`
/// is the canonical `systemc:plugin.factory` manifest spelling.
struct SystemCInstanceDescription {
    std::string path;
    std::string target;
    std::uint64_t handle{};
    std::uint64_t parent{};
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<ExternalPort> ports;
    std::vector<ForeignChild> foreign_children;
    std::vector<ExternalProcess> processes;
    std::vector<ExternalEvent> events;
    std::vector<ExternalPrimitiveChannel> primitive_channels;
    std::vector<ExternalInternalSignal> internal_signals;
    std::vector<ExternalExport> exports;
    std::vector<ExternalMetadataObject> metadata_objects;
    std::vector<SystemCInstanceDescription> native_children;
    // Canonical typed construction values supplied by the authoritative HDL
    // specialization path. Kept trailing for source compatibility with
    // aggregate descriptions produced by existing plug-in bridges.
    std::vector<std::pair<std::string, std::string>>
        construction_identity_values;
};

struct SystemCConstructionParameter {
    std::string name;
    fsim_sc_construction_type_v1 type{
        FSIM_SC_CONSTRUCTION_INTEGER};
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
    [[nodiscard]] virtual std::vector<std::string> libraries() const {
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

[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed, std::string_view top);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    std::string_view top,
    std::span<const Binding> bindings);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    std::string_view top,
    std::span<const Binding> bindings,
    std::span<const SystemCInstanceDescription> systemc_instances);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    std::string_view top,
    std::span<const Binding> bindings,
    std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    std::string_view top,
    std::span<const Binding> bindings,
    std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    // The parent library is prepended and duplicates are removed at their
    // first occurrence for each lazy unqualified lookup.
    std::span<const std::string> search_libraries);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
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

struct SignalInfo {
    runtime::simir::SignalId id{};
    std::string name;
    std::size_t width{};
    std::string type_name;
    frontend::ValueDomain source_domain{frontend::ValueDomain::Unknown};
    bool is_signed{};
    std::optional<frontend::PackedRange> packed_range;
    std::optional<frontend::VhdlArrayInfo> vhdl_array;
    std::optional<frontend::VhdlAccessInfo> vhdl_access;
    std::optional<frontend::VhdlPhysicalInfo> vhdl_physical;
    std::vector<frontend::PackedMember> packed_members;
    std::optional<frontend::IntegerRange> integer_range;
    std::string nominal_type;
    std::vector<std::string> enumeration_literals;
    std::optional<frontend::EnumerationRange> enumeration_range;
    bool is_port{};
    frontend::PortDirection direction{frontend::PortDirection::Unknown};
    frontend::SourceSpan declaration_span;
    runtime::simir::ResolutionKind resolution{
        runtime::simir::ResolutionKind::none};
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
    BoundaryConversionKind kind{BoundaryConversionKind::ordinal_alias};
    std::string path;
    runtime::simir::SignalId formal_signal{};
    runtime::simir::SignalId actual_signal{};
    std::optional<runtime::simir::ProcessId> process;
    frontend::PortDirection direction{frontend::PortDirection::Unknown};
    std::size_t formal_width{};
    std::size_t actual_width{};
    frontend::ValueDomain formal_domain{frontend::ValueDomain::Unknown};
    frontend::ValueDomain actual_domain{frontend::ValueDomain::Unknown};
    bool formal_signed{};
    bool actual_signed{};
    bool state_domain_changed{};
    std::optional<frontend::PackedRange> formal_range;
    std::optional<frontend::PackedRange> actual_range;
    std::optional<frontend::IntegerRange> formal_integer_range;
    std::optional<frontend::IntegerRange> actual_integer_range;
    frontend::SourceSpan connection_span;
    frontend::SourceSpan formal_span;
    frontend::SourceSpan actual_span;
};

struct StringObjectInfo {
    runtime::simir::StringObjectId id{};
    std::string name;
    frontend::SourceSpan declaration_span;
    bool is_port{};
    frontend::PortDirection direction{
        frontend::PortDirection::Unknown};
};

struct ContainerObjectInfo {
    runtime::simir::ContainerObjectId id{};
    std::string name;
    runtime::simir::ContainerType type;
    frontend::SourceSpan declaration_span;
    bool is_port{};
    frontend::PortDirection direction{
        frontend::PortDirection::Unknown};
    std::optional<runtime::simir::ContainerSliceAlias> slice_alias;
};

using VhdlProtectedObjectId = std::uint32_t;

struct VhdlProtectedMemberInfo {
    std::string name;
    frontend::Type type;
    std::size_t offset{};
    std::size_t width{};
    runtime::simir::ContainerObjectId storage{};
    frontend::SourceSpan declaration_span;
};

/// One constructed VHDL shared variable of protected type. Private members
/// use independently addressable global container objects, while this record
/// preserves their common object identity and declaration-ordered layout.
struct VhdlProtectedObjectInfo {
    VhdlProtectedObjectId id{};
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
    SpecializationId id{};
    std::string unit;
    std::string instance;
    std::vector<runtime::simir::ProcessId> processes;
    std::string source;
    // Additional source roots that define this specialization's interface,
    // such as a VHDL entity paired with an architecture in another file.
    std::vector<std::string> source_dependencies;
    frontend::Language language{frontend::Language::SystemVerilog2017};
    std::string library{"work"};
    bool is_cell{};
    // Canonical name/value pairs after frontend generic/parameter evaluation.
    // Empty for unparameterized units.
    std::vector<std::pair<std::string, std::string>> parameter_values;
    // Versioned semantic identities used by the native-object cache when a
    // display value alone would lose width, signedness, state, or type
    // metadata. Empty entries fall back to parameter_values.
    std::vector<std::pair<std::string, std::string>>
        parameter_identity_values;
};

struct SystemCPortInfo {
    std::string name;
    std::uint64_t native_handle{};
    runtime::simir::SignalId signal{};
};

struct SystemCEventInfo {
    std::string name;
    std::uint64_t native_handle{};
    runtime::simir::SignalId signal{};
};

struct SystemCPrimitiveChannelInfo {
    std::string name;
    std::uint64_t native_handle{};
};

struct SystemCSignalInfo {
    std::string name;
    std::uint64_t native_handle{};
    runtime::simir::SignalId signal{};
};

struct SystemCExportInfo {
    std::string name;
    std::uint64_t native_handle{};
    runtime::simir::SignalId signal{};
    bool writable{true};
};

struct SystemCInstanceInfo {
    std::uint32_t id{};
    std::string target;
    std::string instance;
    std::uint64_t native_handle{};
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
    runtime::simir::ProcessId process{};
    std::uint64_t native_handle{};
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
    SystemCNamedObjectKind kind{SystemCNamedObjectKind::module};
    std::uint64_t native_handle{};
    std::string name;
    std::string parent;
    std::string type_name;
    std::optional<runtime::simir::SignalId> signal;
    std::optional<runtime::simir::ProcessId> process;
    runtime::simir::SourceLocation source;
};

class ElaboratedDesign final {
public:
    ElaboratedDesign() = default;

    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;
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
    [[nodiscard]] const std::vector<SystemCInstanceInfo>&
    systemc_instances() const noexcept;
    [[nodiscard]] const std::vector<SystemCProcessInfo>&
    systemc_processes() const noexcept;
    [[nodiscard]] const std::vector<SystemCNamedObjectInfo>&
    systemc_objects() const noexcept;
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
        runtime::SchedulerOptions options = {},
        std::uint64_t seed = 1) const;

private:
    friend struct ElaborationResult;
    friend class Lowerer;
    friend class HierarchyBuilder;
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&, std::string_view);
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&,
        std::string_view,
        std::span<const Binding>);
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&,
        std::string_view,
        std::span<const Binding>,
        std::span<const SystemCInstanceDescription>);
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&,
        std::string_view,
        std::span<const Binding>,
        std::span<const SystemCInstanceDescription>,
        SystemCFactoryProvider*);
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&,
        std::string_view,
        std::span<const Binding>,
        std::span<const SystemCInstanceDescription>,
        SystemCFactoryProvider*,
        std::span<const std::string>);
    friend ElaborationResult elaborate(
        const frontend::ParsedDesign&,
        std::span<const Root>,
        std::span<const Binding>,
        std::span<const SystemCInstanceDescription>,
        SystemCFactoryProvider*,
        std::span<const std::string>);

    std::string top_;
    std::vector<std::string> roots_;
    std::vector<SignalInfo> signal_info_;
    std::vector<BoundaryConversionInfo> boundary_conversions_;
    std::vector<runtime::simir::Signal> signals_;
    std::vector<StringObjectInfo> string_object_info_;
    std::vector<runtime::simir::StringObject> string_objects_;
    std::vector<ContainerObjectInfo> container_object_info_;
    std::vector<runtime::simir::ContainerObject> container_objects_;
    std::vector<VhdlProtectedObjectInfo> vhdl_protected_object_info_;
    std::vector<runtime::simir::Process> processes_;
    std::vector<SpecializationInfo> specializations_;
    std::vector<SystemCInstanceInfo> systemc_instances_;
    std::vector<SystemCProcessInfo> systemc_processes_;
    std::vector<SystemCNamedObjectInfo> systemc_objects_;
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

    [[nodiscard]] bool ok() const noexcept {
        return design.has_value() && diagnostics.empty();
    }
};

/// Elaborate one parsed VHDL entity/architecture or Verilog module.
///
/// VHDL names are already canonicalized by the frontend. `top` accepts a
/// simple unit name or a qualified manifest spelling such as
/// `sv:work.counter` or `vhdl:work.counter(rtl)`.
} // namespace fsim::elaboration
