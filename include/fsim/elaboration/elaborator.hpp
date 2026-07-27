// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/systemc_abi.h"

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
    std::string target;
    std::optional<std::string> resolver;
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
};

struct ForeignChild {
    std::string name;
    std::vector<std::pair<std::string, std::int64_t>>
        construction_actuals;
    std::vector<ForeignPort> ports;
};

struct ExternalSensitivity {
    std::uint64_t object{};
    fsim_sc_edge_kind_v1 edge{FSIM_SC_ANY_EDGE};
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
};

/// Immutable, ABI-neutral description produced by one constructed SystemC
/// elaboration factory. `path` is the full DesignIR instance path and `target`
/// is the canonical `systemc:plugin.factory` manifest spelling.
struct SystemCInstanceDescription {
    std::string path;
    std::string target;
    std::uint64_t handle{};
    std::uint64_t parent{};
    std::vector<ExternalPort> ports;
    std::vector<ForeignChild> foreign_children;
    std::vector<ExternalProcess> processes;
    std::vector<ExternalEvent> events;
    std::vector<ExternalPrimitiveChannel> primitive_channels;
    std::vector<ExternalInternalSignal> internal_signals;
    std::vector<ExternalExport> exports;
    std::vector<SystemCInstanceDescription> native_children;
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

struct Diagnostic {
    std::string code;
    std::string message;
    frontend::SourceSpan span;
};

struct SignalInfo {
    runtime::simir::SignalId id{};
    std::string name;
    std::size_t width{};
    frontend::ValueDomain source_domain{frontend::ValueDomain::Unknown};
    bool is_signed{};
    std::optional<frontend::PackedRange> packed_range;
    bool is_port{};
    frontend::PortDirection direction{frontend::PortDirection::Unknown};
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
};

struct SystemCInstanceInfo {
    std::uint32_t id{};
    std::string target;
    std::string instance;
    std::uint64_t native_handle{};
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

class ElaboratedDesign final {
public:
    ElaboratedDesign() = default;

    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<SignalInfo>& signals() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::Process>& processes() const noexcept;
    [[nodiscard]] const std::vector<SpecializationInfo>&
    specializations() const noexcept;
    [[nodiscard]] const std::vector<SystemCInstanceInfo>&
    systemc_instances() const noexcept;
    [[nodiscard]] const std::vector<SystemCProcessInfo>&
    systemc_processes() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
        std::string_view name) const noexcept;
    /// Return every debug-visible signal path in lexical order. Boundary-port
    /// aliases may refer to the same dense signal ID as their connected
    /// parent signal.
    [[nodiscard]] std::vector<std::pair<std::string, runtime::simir::SignalId>>
    signal_paths() const;

    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options = {}) const;

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

    std::string top_;
    std::vector<SignalInfo> signal_info_;
    std::vector<runtime::simir::Signal> signals_;
    std::vector<runtime::simir::Process> processes_;
    std::vector<SpecializationInfo> specializations_;
    std::vector<SystemCInstanceInfo> systemc_instances_;
    std::vector<SystemCProcessInfo> systemc_processes_;
    std::unordered_map<std::string, runtime::simir::SignalId> signal_by_name_;
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
