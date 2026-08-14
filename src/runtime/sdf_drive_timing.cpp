// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_drive_timing.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool overlaps(
        const runtime::simir::Process::DriverRegion& region,
        const elaboration::VerilogSpecifyTerminalInfo& terminal) noexcept
    {
        if (region.signal != terminal.signal)
            return false;
        if (region.whole)
            return true;
        const auto region_end = static_cast<std::size_t>(region.offset)
            + region.width;
        const auto terminal_end = static_cast<std::size_t>(terminal.offset)
            + terminal.width;
        return region.offset < terminal_end && terminal.offset < region_end;
    }

    [[nodiscard]] std::string binding_identity(
        const SdfDriveTimingBinding& binding)
    {
        std::string result = "sdf-drive-timing-binding-v1";
        append_field(result, binding.path_identity);
        append_field(result, std::to_string(binding.process));
        append_field(result, std::to_string(binding.signal));
        append_field(result, std::to_string(binding.offset));
        append_field(result, std::to_string(binding.width));
        append_field(result,
            std::to_string(static_cast<unsigned>(binding.resolution)));
        append_field(result,
            std::to_string(static_cast<unsigned>(binding.strength.zero)));
        append_field(result,
            std::to_string(static_cast<unsigned>(binding.strength.one)));
        append_field(result,
            std::to_string(static_cast<unsigned>(binding.role)));
        return result;
    }

    struct BindingState {
        const SdfDriveTimingLimits& limits;
        std::vector<SdfDriveTimingBinding>& bindings;
        std::vector<Diagnostic>& diagnostics;
        std::size_t identity_bytes { };
    };

    [[nodiscard]] bool add_binding(
        SdfDriveTimingBinding binding, BindingState& state)
    {
        if (state.bindings.size() >= state.limits.max_bindings) {
            diagnose(state.diagnostics, "FSIM-SDF-DRIVE-004",
                "SDF drive timing exceeds its configured binding limit");
            return false;
        }
        binding.canonical_identity = binding_identity(binding);
        if (binding.canonical_identity.size()
                > state.limits.max_identity_bytes
            || state.identity_bytes > state.limits.max_identity_bytes
                    - binding.canonical_identity.size()) {
            diagnose(state.diagnostics, "FSIM-SDF-DRIVE-004",
                "SDF drive timing exceeds its identity-byte limit");
            return false;
        }
        state.identity_bytes += binding.canonical_identity.size();
        state.bindings.push_back(std::move(binding));
        return true;
    }

    [[nodiscard]] bool bind_path_driver(
        const elaboration::ElaboratedDesign& design,
        const elaboration::VerilogSpecifyPathInfo& path,
        const runtime::simir::ProcessId driver,
        BindingState& state)
    {
        if (driver >= design.processes().size()) {
            diagnose(state.diagnostics, "FSIM-SDF-DRIVE-002",
                "SDF drive timing references a missing path driver");
            return false;
        }
        const auto& process = design.processes()[driver];
        for (const auto& destination : path.destinations) {
            if (destination.signal >= design.signals().size()
                || !std::ranges::any_of(process.driver_regions,
                    [&](const auto& region) {
                        return overlaps(region, destination);
                    })) {
                diagnose(state.diagnostics, "FSIM-SDF-DRIVE-002",
                    "SDF path driver does not own its destination region");
                return false;
            }
            SdfDriveTimingBinding binding;
            binding.path_identity = path.identity;
            binding.process = driver;
            binding.signal = destination.signal;
            binding.offset = destination.offset;
            binding.width = destination.width;
            binding.resolution
                = design.signals()[destination.signal].resolution;
            binding.strength = process.drive_strength;
            if (!add_binding(std::move(binding), state))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool bind_switches(
        const elaboration::ElaboratedDesign& design,
        const std::set<runtime::simir::SignalId>& timed_signals,
        BindingState& state)
    {
        for (const auto& process : design.processes()) {
            if (!process.switch_source || !process.switch_target
                || (!timed_signals.contains(*process.switch_source)
                    && !timed_signals.contains(*process.switch_target))) {
                continue;
            }
            for (const auto signal : {
                     *process.switch_source, *process.switch_target }) {
                if (signal >= design.signals().size()) {
                    diagnose(state.diagnostics, "FSIM-SDF-DRIVE-003",
                        "SDF drive timing found an invalid switch endpoint");
                    return false;
                }
                SdfDriveTimingBinding binding;
                binding.path_identity = "switch:" + process.name;
                binding.process = process.id;
                binding.signal = signal;
                binding.offset = signal == *process.switch_source
                    ? process.switch_source_offset
                    : process.switch_target_offset;
                binding.width = process.switch_width;
                binding.resolution = design.signals()[signal].resolution;
                binding.strength = process.drive_strength;
                binding.role = process.switch_bidirectional
                    ? SdfDriveTimingBindingRole::switch_bidirectional
                    : SdfDriveTimingBindingRole::switch_unidirectional;
                if (!add_binding(std::move(binding), state))
                    return false;
            }
        }
        return true;
    }

    using DriverFanout = std::map<runtime::simir::SignalId,
        std::vector<const runtime::simir::Process*>>;

    [[nodiscard]] DriverFanout make_driver_fanout(
        const elaboration::ElaboratedDesign& design)
    {
        DriverFanout fanout;
        for (const auto& process : design.processes()) {
            if (process.switch_bidirectional)
                continue;
            for (const auto& sensitivity : process.static_sensitivity)
                fanout[sensitivity.signal].push_back(&process);
        }
        return fanout;
    }

    [[nodiscard]] bool bind_propagated_process(
        const elaboration::ElaboratedDesign& design,
        const runtime::simir::Process& process,
        std::set<runtime::simir::SignalId>& timed_signals,
        std::deque<runtime::simir::SignalId>& pending, BindingState& state)
    {
        for (const auto& region : process.driver_regions) {
            if (region.signal >= design.signals().size()
                || (!region.whole
                    && (region.width == 0U
                        || region.offset > design.signals()[region.signal].width
                        || region.width > design.signals()[region.signal].width
                                - region.offset))) {
                diagnose(state.diagnostics, "FSIM-SDF-DRIVE-002",
                    "SDF propagated driver owns an invalid region");
                return false;
            }
            SdfDriveTimingBinding binding;
            binding.path_identity = "propagated:" + process.name;
            binding.process = process.id;
            binding.signal = region.signal;
            binding.offset = region.offset;
            binding.width = region.whole
                ? design.signals()[region.signal].width
                : region.width;
            binding.resolution = design.signals()[region.signal].resolution;
            binding.strength = process.drive_strength;
            binding.role = SdfDriveTimingBindingRole::propagated_driver;
            if (!add_binding(std::move(binding), state))
                return false;
            if (timed_signals.insert(region.signal).second)
                pending.push_back(region.signal);
        }
        return true;
    }

    [[nodiscard]] bool bind_propagated_drivers(
        const elaboration::ElaboratedDesign& design,
        std::set<runtime::simir::SignalId>& timed_signals,
        BindingState& state)
    {
        auto fanout = make_driver_fanout(design);
        std::deque<runtime::simir::SignalId> pending(
            timed_signals.begin(), timed_signals.end());
        std::set<runtime::simir::ProcessId> visited;
        while (!pending.empty()) {
            const auto signal = pending.front();
            pending.pop_front();
            for (const auto* process : fanout[signal]) {
                if (!visited.insert(process->id).second)
                    continue;
                if (!bind_propagated_process(
                        design, *process, timed_signals, pending, state))
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string application_identity(
        const SdfSchedulingApplication& scheduling,
        const std::vector<SdfDriveTimingBinding>& bindings)
    {
        std::string result = "sdf-drive-timing-application-v1";
        append_field(result, scheduling.semantic_identity());
        for (const auto& binding : bindings)
            append_field(result, binding.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfSchedulingApplication>&
SdfDriveTimingApplication::scheduling() const noexcept
{
    return scheduling_;
}

std::span<const SdfDriveTimingBinding> SdfDriveTimingApplication::bindings()
    const noexcept
{
    return bindings_;
}

std::string_view SdfDriveTimingApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfDriveTimingApplication::SdfDriveTimingApplication(
    std::shared_ptr<const SdfSchedulingApplication> scheduling,
    std::vector<SdfDriveTimingBinding> bindings,
    std::string semantic_identity)
    : scheduling_(std::move(scheduling))
    , bindings_(std::move(bindings))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfDriveTimingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfDriveTimingResult apply_sdf_drive_timing(
    std::shared_ptr<const SdfSchedulingApplication> scheduling,
    const SdfDriveTimingLimits limits)
{
    SdfDriveTimingResult result;
    if (!scheduling || scheduling->semantic_identity().empty()
        || limits.max_bindings == 0U || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-DRIVE-001",
            "SDF drive timing requires complete scheduling and nonzero limits");
        return result;
    }
    const auto& design = scheduling->design();
    std::vector<SdfDriveTimingBinding> bindings;
    BindingState state { limits, bindings, result.diagnostics };
    std::set<runtime::simir::SignalId> timed_signals;
    for (const auto& path : design.verilog_specify_paths()) {
        for (const auto& destination : path.destinations)
            timed_signals.insert(destination.signal);
        for (const auto driver : path.drivers) {
            if (!bind_path_driver(design, path, driver, state))
                return result;
        }
    }
    if (!bind_propagated_drivers(design, timed_signals, state))
        return result;
    if (!bind_switches(design, timed_signals, state))
        return result;
    std::ranges::sort(bindings, [](const auto& left, const auto& right) {
        return std::tie(left.path_identity, left.process, left.signal,
                   left.offset, left.width)
            < std::tie(right.path_identity, right.process, right.signal,
                right.offset, right.width);
    });
    const auto duplicate = std::ranges::adjacent_find(
        bindings, [](const auto& left, const auto& right) {
            return std::tie(left.path_identity, left.process, left.signal,
                       left.offset, left.width)
                == std::tie(right.path_identity, right.process, right.signal,
                    right.offset, right.width);
        });
    if (duplicate != bindings.end()) {
        diagnose(result.diagnostics, "FSIM-SDF-DRIVE-003",
            "SDF drive timing contains duplicate driver ownership");
        return result;
    }
    const auto identity = application_identity(*scheduling, bindings);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-DRIVE-004",
            "SDF drive timing semantic identity exceeds its configured limit");
        return result;
    }
    result.application = std::make_shared<const SdfDriveTimingApplication>(
        std::move(scheduling), std::move(bindings), identity);
    return result;
}

} // namespace fsim::app
