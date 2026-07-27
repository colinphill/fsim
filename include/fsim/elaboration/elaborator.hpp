// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/runtime/simir.hpp"

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

[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed, std::string_view top);
[[nodiscard]] ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    std::string_view top,
    std::span<const Binding> bindings);

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
/// The current frontend subset has no generic/parameter actuals, so each
/// instance is its own specialization record. The dense ID and explicit
/// process membership provide the stable grouping contract that later
/// parameterized specialization can refine without recovering ownership from
/// process-name strings.
struct SpecializationInfo {
    SpecializationId id{};
    std::string unit;
    std::string instance;
    std::vector<runtime::simir::ProcessId> processes;
    std::string source;
    frontend::Language language{frontend::Language::SystemVerilog2017};
    std::string library{"work"};
    // Canonical name/value pairs once frontend parameters and generics are
    // represented. Empty for the current bounded frontend subset.
    std::vector<std::pair<std::string, std::string>> parameter_values;
};

class ElaboratedDesign final {
public:
    ElaboratedDesign() = default;

    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<SignalInfo>& signals() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::Process>& processes() const noexcept;
    [[nodiscard]] const std::vector<SpecializationInfo>&
    specializations() const noexcept;
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

    std::string top_;
    std::vector<SignalInfo> signal_info_;
    std::vector<runtime::simir::Signal> signals_;
    std::vector<runtime::simir::Process> processes_;
    std::vector<SpecializationInfo> specializations_;
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
