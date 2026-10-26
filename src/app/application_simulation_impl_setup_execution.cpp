// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include <algorithm>
#include <stdexcept>

namespace fsim::app::application_detail {

SystemCProcessExecutor::SystemCProcessExecutor(
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
    const std::uint64_t process,
    std::vector<runtime::simir::Sensitivity> boundary_inputs)
    : hierarchy_(std::move(hierarchy))
    , process_(process)
    , boundary_inputs_(std::move(boundary_inputs))
{
    if (!hierarchy_) {
        throw std::invalid_argument {
            "SystemC process executor requires a hierarchy registry"
        };
    }
}

runtime::simir::ProcessResumeResult SystemCProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    runtime::simir::InstructionIndex)
{
    const auto suspension = hierarchy_->invoke_process(process_, context);
    runtime::simir::ProcessResumeResult result { 0, 1 };
    switch (suspension.kind) {
    case systemc::MethodSuspendKind::halt:
        // The registry knows only the SystemC sensitivity; a process with
        // elaborated boundary inputs (the channels its HDL children drive)
        // waits on them.
        result.external.kind = boundary_inputs_.empty()
            ? runtime::simir::ExternalSuspendKind::halt
            : runtime::simir::ExternalSuspendKind::wait_sensitivity;
        break;
    case systemc::MethodSuspendKind::static_sensitivity:
        result.external.kind = runtime::simir::ExternalSuspendKind::wait_sensitivity;
        break;
    case systemc::MethodSuspendKind::wait_for:
        result.external.kind = suspension.delay_ticks == 0
            ? runtime::simir::ExternalSuspendKind::yield
            : runtime::simir::ExternalSuspendKind::wait_for;
        result.external.delay = suspension.delay_ticks;
        break;
    case systemc::MethodSuspendKind::wait_event:
        if (suspension.boundary_inputs && boundary_inputs_.empty()) {
            result.external.kind = !suspension.timeout_ticks
                ? runtime::simir::ExternalSuspendKind::halt
                : *suspension.timeout_ticks == 0
                ? runtime::simir::ExternalSuspendKind::yield
                : runtime::simir::ExternalSuspendKind::wait_for;
            result.external.delay = suspension.timeout_ticks.value_or(0);
            break;
        }
        result.external.kind = runtime::simir::ExternalSuspendKind::wait_on;
        if (suspension.boundary_inputs) {
            result.external.timeout = suspension.timeout_ticks;
            result.external.sensitivity = boundary_inputs_;
            break;
        }
        result.external.wait_all = suspension.wait_all;
        result.external.timeout = suspension.timeout_ticks;
        result.external.sensitivity.reserve(
            suspension.event_signals.size());
        for (const auto event : suspension.event_signals) {
            result.external.sensitivity.push_back(
                { event, runtime::simir::EdgeKind::any });
        }
        break;
    }
    return result;
}

std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept
{
    switch (severity) {
    case runtime::simir::AssertionSeverity::note:
        return "note";
    case runtime::simir::AssertionSeverity::warning:
        return "warning";
    case runtime::simir::AssertionSeverity::error:
        return "error";
    case runtime::simir::AssertionSeverity::failure:
        return "failure";
    }
    return "error";
}

std::uint64_t entropy_seed()
{
    std::random_device source;
    const auto high = static_cast<std::uint64_t>(source());
    const auto low = static_cast<std::uint64_t>(source());
    return (high << 32U) ^ low;
}

} // namespace fsim::app::application_detail

namespace fsim::app {
void Simulation::Impl::setup_execution(
    const SimulationEngine engine)
{

    // Constructor-body fragment included by application_simulation_setup.tpp.

    const auto has_systemc_process = std::ranges::any_of(
        built.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == semantic::design::BoundaryKind::systemc_process;
        });
    if (has_systemc_process && !built.systemc_hierarchy
        && built.systemc_hierarchies.empty()) {
        throw std::logic_error {
            "SystemC processes require their native hierarchy registry"
        };
    }
    for (const auto& boundary : built.design_ir.boundaries()) {
        if (boundary.kind
                != semantic::design::BoundaryKind::systemc_process
            || !boundary.process) {
            continue;
        }
        const auto process = built.design_ir.processes()[boundary.process->value()].runtime_index;
        auto hierarchy = std::ranges::find_if(
            built.systemc_hierarchies,
            [&](const auto& candidate) {
                return candidate->owns_handle(boundary.native_handle);
            });
        auto owner = hierarchy != built.systemc_hierarchies.end()
            ? *hierarchy
            : built.systemc_hierarchy;
        interpreter->set_process_executor(
            process,
            std::make_unique<SystemCProcessExecutor>(
                std::move(owner),
                boundary.native_handle,
                interpreter->process_program(
                    static_cast<runtime::simir::ProcessId>(process))
                    .static_sensitivity));
    }
    (void)engine;
}

} // namespace fsim::app
