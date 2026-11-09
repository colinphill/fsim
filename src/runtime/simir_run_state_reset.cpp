// SPDX-License-Identifier: Apache-2.0
//
// Reset and restore-mode start of the interpreter (#128; IEEE 1800-2023 D.8
// $reset, IEEE 1076-2019 20.9 reset phase).
//
// There is no pre-start image: a run that never resets pays nothing. At a reset
// the embedder's reset source builds a second interpreter of the same design,
// configured as this one was before start; that twin is started without
// running user code and its state, which no process has touched, is the
// time-zero state. The running interpreter then takes that state through the
// restart path (restore_run_state), so the reset and the restart cannot
// disagree about what a run state is. $reset_count and $reset_value are the
// only state a reset keeps (D.8: "the count of resets and the reset value
// survive").
#include "simir_run_state.hpp"

#include "fsim/runtime/run_state_image.hpp"

#include <exception>
#include <utility>

namespace fsim::runtime::simir {

void Interpreter::set_reset_source(
    std::function<std::unique_ptr<Interpreter>()> source)
{
    impl_->reset_source = std::move(source);
}

void Interpreter::set_run_state_event_host(RunStateEventHost host)
{
    impl_->run_state_event_host = std::move(host);
}

const std::string& Interpreter::reset_failure() const noexcept
{
    return impl_->reset_failure;
}

void Interpreter::set_plan_replay(const bool replaying) noexcept
{
    impl_->restoring_plan = replaying;
}

bool Interpreter::replay_plan_onto(
    Interpreter& target, const bool after_start, std::string& reason) const
{
    struct Replaying {
        bool& flag;
        explicit Replaying(bool& value) : flag(value) { flag = true; }
        ~Replaying() { flag = false; }
    } replaying { target.impl_->replaying_plan };
    for (const auto& step : impl_->plan_journal) {
        if (step.after_start != after_start) {
            continue;
        }
        using Kind = Impl::PlanStep::Kind;
        switch (step.kind) {
        case Kind::watch_changes:
            target.watch_signal_changes(step.signals);
            break;
        case Kind::watch_transactions:
            target.watch_signal_transactions(step.signals);
            break;
        case Kind::open_boundary:
            target.open_external_observation();
            break;
        case Kind::external_write:
            for (const auto signal : step.signals) {
                target.note_external_signal_write(signal);
            }
            break;
        case Kind::graft_net_delay: {
            // The added processes take the ids that follow the twin's own;
            // the run gave them the ones the journal holds, and a run that
            // had grown (fork children, foreign drivers) before the graft
            // gave them later ones, which the twin cannot reproduce.
            auto graft = *step.graft;
            if (!target.graft_net_delay_planned(graft, reason)) {
                reason = "FSIM-SAVE-0010: a late net-delay split of the run "
                         "cannot be replayed for the reset: " + reason;
                return false;
            }
            for (std::size_t index = 0U; index < graft.processes.size(); ++index) {
                if (graft.processes[index].id != step.graft->processes[index].id) {
                    reason = "FSIM-SAVE-0010: a late net-delay split of the "
                             "run was made after processes the design does not "
                             "have (fork children or foreign drivers), so the "
                             "reset cannot give its drivers the same ids";
                    return false;
                }
            }
            break;
        }
        case Kind::split_gate_delay: {
            // The twin's driver is the run's: a gate driver is a process of
            // the design, so it has the same id, and its original program is
            // the one the split rewrote.
            auto program = *step.gate_program;
            if (!target.split_gate_delay(
                    step.gate_process, std::move(program), reason, false)) {
                reason = "FSIM-SAVE-0010: a gate delay split of the run "
                         "cannot be replayed for the reset: " + reason;
                return false;
            }
            break;
        }
        }
    }
    return true;
}

void Interpreter::set_reset_twin_class_hooks(const bool share) noexcept
{
    impl_->reset_twin_class_hooks = share;
}

bool Interpreter::can_reset_run_state() const noexcept
{
    const auto& impl = *impl_;
    if (!impl.reset_source || impl.validation_only) {
        return false;
    }
    // An executor that keeps its own registers (a SystemC or foreign model)
    // cannot be put back to its time-zero state from outside.
    for (ProcessId id = 0U; id < impl.processes.size(); ++id) {
        const auto* const state = impl.processes.full_state_if_present(id);
        if (state != nullptr && state->executor && !state->halted
            && !state->killed && !state->executor->run_state_free()) {
            return false;
        }
    }
    return true;
}

bool Interpreter::reset_run_state()
{
    if (!can_reset_run_state() || !impl_->started) {
        return false;
    }
    auto& impl = *impl_;
    impl.reset_failure.clear();
    std::unique_ptr<Interpreter> twin;
    try {
        twin = impl.reset_source();
        if (!twin) {
            impl.reset_failure = "FSIM-SAVE-0018: the design cannot be "
                                 "rebuilt for the reset";
            return false;
        }
        if (impl.reset_twin_class_hooks) {
            // Its package initializers allocate and call through this run's
            // class services (the embedder cleared them for the reset).
            auto& target = *twin->impl_;
            target.class_allocate_context_hook = impl.class_allocate_context_hook;
            target.class_property_read_hook = impl.class_property_read_hook;
            target.class_property_write_hook = impl.class_property_write_hook;
            target.class_property_container_hook
                = impl.class_property_container_hook;
            target.class_method_call_context_hook
                = impl.class_method_call_context_hook;
            target.class_static_property_read_hook
                = impl.class_static_property_read_hook;
            target.class_static_property_write_hook
                = impl.class_static_property_write_hook;
            target.class_static_method_call_context_hook
                = impl.class_static_method_call_context_hook;
        }
        // The twin has the plan this run has: what the foreign interfaces
        // asked before start, then start without running anything, then what
        // they asked after it (D.8: the time-zero state of this run's design).
        std::string reason;
        if (!replay_plan_onto(*twin, false, reason)) {
            impl.reset_failure = std::move(reason);
            return false;
        }
        // A full start: the package and class static initializers run on
        // the twin and every process is armed for time zero, but nothing
        // runs yet (a restore from a file skips both, its state replaces
        // them).
        twin->impl_->reset_twin = true;
        twin->start();
        if (!replay_plan_onto(*twin, true, reason)) {
            impl.reset_failure = std::move(reason);
            return false;
        }
    } catch (const std::exception& error) {
        impl.reset_failure = std::string { "FSIM-SAVE-0018: the design cannot "
                                           "be rebuilt for the reset: " }
            + error.what();
        return false;
    }
    RunStateImage image { "reset" };
    RunStateReport report;
    if (!twin->record_run_state(image, report)) {
        impl.reset_failure = report.messages.empty()
            ? std::string { "FSIM-SAVE-0018: the time-zero state cannot be "
                            "written" }
            : report.messages.front();
        return false;
    }
    // The twin held the whole state; its processes, kernel and signal tables
    // are the design's at time zero, with the processes queued to start.
    const auto count = impl.reset_count_result;
    const auto value = impl.reset_value_result;
    twin.reset();
    // The finals of the old run did not run, and a stopped run runs again.
    report.messages.clear();
    if (!restore_run_state(image, report)) {
        impl.reset_failure = report.messages.empty()
            ? std::string { "FSIM-SAVE-0010: the time-zero state does not fit "
                            "the run" }
            : report.messages.front();
        return false;
    }
    set_reset_values(count, value);
    return true;
}

}  // namespace fsim::runtime::simir
