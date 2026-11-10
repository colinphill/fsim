// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"
#include "simir_static_kernel.hpp"
#include "simir_static_kernel_state.hpp"

#include "fsim/runtime/simir_shared_body_scan.hpp"
#include "fsim/semantic/sva_plan.hpp"

namespace fsim::runtime::simir {

// A reference process's registers for the assertion operations.
struct Interpreter::Impl::ProcessAssertionStorage final : AssertionStorage {
    ProcessAssertionStorage(Impl& owner_value, ProcessState& state_value) noexcept
        : owner { owner_value }, state { state_value }
    {
    }
    [[nodiscard]] ProcessId process() const noexcept override
    {
        return state.id;
    }
    [[nodiscard]] ProcessId design_process() const noexcept override
    {
        return state.cold().design_process;
    }
    [[nodiscard]] ProcessSchedulingDomain domain() const noexcept override
    {
        return state.program().scheduling_domain();
    }
    [[nodiscard]] PackedLogic4 read(const RegisterId id,
        const std::size_t width) override
    {
        return state.executor ? state.executor->read_register(id, width)
                              : owner.get_register(state, id);
    }
    void write(const RegisterId id, const PackedLogic4& value) override
    {
        owner.write_process_register(state, id, value);
    }
    [[noreturn]] void fail(const std::string& message) override
    {
        owner.fail(state, message);
    }
    Impl& owner;
    ProcessState& state;
};

void Interpreter::Impl::execute_sva(
    ProcessState& process,
    const SvaOperation& operation)
{
    ProcessAssertionStorage storage { *this, process };
    run_sva(storage, operation);
}

void Interpreter::Impl::plan_assertion_controls(
    const std::optional<ProcessId> host, const Process* const host_program)
{
    if (assertion_controls_planned) {
        return;
    }
    assertion_controls_planned = true;
    // Each body several processes share is scanned once; a process's
    // sites follow its base in the order of their numbers (the lowering
    // numbers them in the order of their first read).
    assertion_control_base.assign(processes.size(), no_assertion_control);
    SharedBodyOperationScan scan { [](const Operation& operation) {
        const auto* const sva = operation_get_if<SvaOperation>(&operation);
        return sva != nullptr && sva->kind == SvaKind::control;
    } };
    for (std::size_t index = 0U; index < processes.size(); ++index) {
        const auto id = static_cast<ProcessId>(index);
        const bool stubbed_host = host_program != nullptr && host == id;
        if (!stubbed_host && processes.is_compact_constant(id)) {
            continue;
        }
        const auto base
            = static_cast<std::uint32_t>(assertion_control_sites.size());
        std::uint32_t count { };
        // The static kernel's host runs a stub in place of its member's
        // program; the member's reads are in the original.
        const auto program = stubbed_host
            ? ProcessProgramView { *host_program }
            : processes.program_view(id);
        scan.visit_stored(program.operations(),
            [&](const std::size_t, const Operation& operation) {
                const auto& read = operation_get<SvaOperation>(operation);
                for (; count <= read.site; ++count) {
                    assertion_control_sites.push_back({ id, count, { } });
                }
                auto& site = assertion_control_sites[base + read.site];
                if (site.description.empty()) {
                    site.description = read.plan;
                }
            });
        if (count != 0U) {
            assertion_control_base[index] = base;
        }
    }
    assertion_controls.assign(
        assertion_control_sites.size(), semantic::sva::control_default);
}

std::int64_t Interpreter::Impl::assertion_control_word(
    const ProcessId design_process, const std::uint32_t site) const noexcept
{
    if (design_process >= assertion_control_base.size()
        || assertion_control_base[design_process] == no_assertion_control) {
        return semantic::sva::control_default;
    }
    const auto index
        = std::size_t { assertion_control_base[design_process] } + site;
    return index < assertion_controls.size()
        ? assertion_controls[index]
        : semantic::sva::control_default;
}

const std::vector<Interpreter::AssertionControlSite>&
Interpreter::assertion_control_sites()
{
    impl_->plan_assertion_controls();
    return impl_->assertion_control_sites;
}

void Interpreter::set_assertion_control(
    const std::size_t index, const std::int64_t word)
{
    impl_->plan_assertion_controls();
    if (index < impl_->assertion_controls.size()) {
        impl_->assertion_controls[index] = word;
    }
}

void Interpreter::Impl::execute_sampled_read(
    ProcessState& process,
    const ReadSignal& operation)
{
    ProcessAssertionStorage storage { *this, process };
    run_sampled_read(storage, operation);
}

sva::Monitor* Interpreter::Impl::sva_monitor_find(const ProcessId process,
    const std::uint32_t site) noexcept
{
    const auto found = sva_monitors.find({ process, site });
    return found == sva_monitors.end() ? nullptr : found->second.get();
}

sva::Monitor& Interpreter::Impl::sva_monitor_bind(AssertionStorage& storage,
    const SvaOperation& operation)
{
    auto& active_monitor = sva_monitors[{ storage.process(), operation.site }];
    if (active_monitor) {
        return *active_monitor;
    }
    auto plan = semantic::sva::decode(operation.plan);
    if (!plan || plan->bounds != operation.bounds.size()
        || plan->guards != operation.guard_count) {
        storage.fail("concurrent property plan is malformed");
    }
    std::vector<std::int64_t> bounds;
    for (const auto bound : operation.bounds) {
        const auto value = storage.read(bound, 64U).known_signed_value();
        if (!value || *value < 0) {
            storage.fail(
                "concurrent property delay and repetition bounds "
                "must be known nonnegative constants");
        }
        bounds.push_back(*value);
    }
    std::string error;
    try {
        active_monitor
            = std::make_unique<sva::Monitor>(operation.plan, std::move(bounds));
    } catch (const std::runtime_error& failure) {
        error = failure.what();
    }
    if (!active_monitor) {
        storage.fail(error);
    }
    // The action controls in force (20.11).
    if (sva_action_filter) {
        active_monitor->set_actions_off(
            sva_action_filter(storage.design_process()));
    }
    // The disable net disables the monitor's attempts between the ticks
    // too (16.12).
    if (operation.disable_signal) {
        const auto net = *operation.disable_signal;
        if (sva_disable_watched.size() <= net) {
            sva_disable_watched.resize(std::size_t { net } + 1U, 0U);
        }
        sva_disable_watched[net] = 1U;
        auto& watches = sva_disable_watches[net];
        if (std::ranges::none_of(watches, [&](const SvaDisableWatch& watch) {
                return watch.process == storage.process()
                    && watch.site == operation.site;
            })) {
            watches.push_back(SvaDisableWatch { storage.process(),
                operation.site, storage.design_process(),
                (operation.monitor_flags & semantic::sva::monitor_expect)
                    != 0U });
        }
    }
    // The wake bit of a monitor that also evaluates at time steps without
    // a tick of its clock, and the abort net whose changes wake it
    // (16.12.14; wake_sva_monitors).
    if (operation.wake_signal) {
        const auto index = static_cast<std::uint32_t>(sva_wakes.size());
        const bool every_step
            = (operation.monitor_flags & semantic::sva::monitor_every_step) != 0U;
        sva_wakes.push_back(SvaWake { storage.process(), operation.site,
            *operation.wake_signal, every_step, false });
        sva_wakes_due = sva_wakes_due || every_step;
        if (operation.abort_signal) {
            const auto net = *operation.abort_signal;
            if (sva_abort_watched.size() <= net) {
                sva_abort_watched.resize(std::size_t { net } + 1U, 0U);
            }
            sva_abort_watched[net] = 1U;
            sva_abort_watches[net].push_back(index);
        }
    }
    // The step requests a library made for this assertion's attempts
    // before the monitor existed (39.5.2); they name attempts that have
    // not started, so none is refused.
    if (const auto requests = sva_step_requests.find(storage.design_process());
        requests != sva_step_requests.end()) {
        for (const auto start : requests->second) {
            (void)active_monitor->request_step(start, true);
        }
    }
    return *active_monitor;
}

void Interpreter::Impl::disable_sva_attempts(const SignalId net)
{
    // The disable condition became true (16.12): every attempt in progress
    // is disabled now, in the time step and region of the change, between
    // the clock ticks too. The attempts reach the disabled state here
    // (39.4.2 cbAssertionDisabledEvaluation) and produce no outcome, since
    // a disabled evaluation runs no action block. An attempt that starts
    // while the net is 1 is disabled by its tick.
    if (net >= signals.size() || logical_signal_value(net).get(0U) != Logic4::one
        || logical_signal_last_value(net).get(0U) == Logic4::one) {
        return;
    }
    const auto found = sva_disable_watches.find(net);
    if (found == sva_disable_watches.end()) {
        return;
    }
    for (const auto& watch : found->second) {
        auto* const active_monitor = sva_monitor_find(watch.process, watch.site);
        if (active_monitor == nullptr) {
            continue;
        }
        const auto starts = active_monitor->disable_running(watch.outcomes);
        if (sva_disabled_hook) {
            for (const auto start : starts) {
                sva_disabled_hook(watch.design_process, start);
            }
        }
    }
}

void Interpreter::Impl::sva_abort_changed(const SignalId net)
{
    // An asynchronous abort condition changed value (16.12.14). Its
    // sampled value changes at the next time step: the monitors it belongs
    // to evaluate at the start of the next time step that exists, where
    // their ticks read the conditions' sampled values; no time step is made
    // for it.
    const auto found = sva_abort_watches.find(net);
    if (found == sva_abort_watches.end()) {
        return;
    }
    for (const auto index : found->second) {
        if (index < sva_wakes.size()) {
            sva_wakes[index].pending = true;
            sva_wakes_due = true;
        }
    }
}

void Interpreter::Impl::wake_sva_monitors()
{
    // The start of a time step (before any of its regions): each monitor
    // with an attempt in progress whose abort net changed in an earlier
    // time step, or whose disable or abort condition no net follows, has
    // its wake bit toggled, which resumes its process; the process's tick
    // reads its clock's tick event to tell this evaluation from a tick
    // (16.12, 16.12.14). A monitor without an attempt has nothing to
    // abort or disable: an attempt that starts later reads the conditions
    // at its own ticks.
    bool due = false;
    for (auto& wake : sva_wakes) {
        if (!wake.pending && !wake.every_step) {
            continue;
        }
        wake.pending = false;
        due = due || wake.every_step;
        const auto* const active_monitor = sva_monitor_find(wake.process, wake.site);
        if (active_monitor == nullptr || active_monitor->running_attempts() == 0U
            || wake.signal >= signals.size()) {
            continue;
        }
        const bool high = logical_signal_value(wake.signal).get(0U) == Logic4::one;
        commit(wake.signal, PackedLogic4 { 1U, high ? Logic4::zero : Logic4::one },
            SignalChangeOrigin { ProcessSchedulingDomain::generic,
                SchedulerPhase::active });
        if (static_kernel && started) {
            static_kernel->host_deposit(wake.signal);
        }
    }
    sva_wakes_due = due;
}

std::size_t Interpreter::Impl::sva_tick_core(AssertionStorage& storage,
    sva::Monitor& active_monitor, const std::vector<bool>& guards, bool start,
    const bool disabled)
{
    // $assertoff and its VPI form stop new attempts; attempts already
    // running complete (IEEE 1800-2017 20.12).
    if (start && fork_spawn_filter) {
        start = sva_attempt_allowed(storage);
    }
    std::string error;
    try {
        active_monitor.tick(guards, start, disabled,
            static_cast<std::int64_t>(scheduler.now()));
    } catch (const std::runtime_error& failure) {
        error = failure.what();
    }
    if (!error.empty()) {
        storage.fail(error);
    }
    sva_tick_reports(storage, active_monitor);
    // A tick that needs values of local-variable expressions gives minus
    // the number of requests (16.10; the attempt state is unchanged).
    if (const auto requests = active_monitor.pending_requests(); requests != 0U) {
        return static_cast<std::size_t>(-static_cast<std::int64_t>(requests));
    }
    return active_monitor.pending_outcomes();
}

bool Interpreter::Impl::sva_attempt_allowed(AssertionStorage& storage)
{
    const auto filter = fork_spawn_filter;
    if (!filter) {
        return true;
    }
    if (fork_spawn_filter_observes) {
        prepare_callback_observation();
    }
    return (*filter)(storage.design_process());
}

void Interpreter::Impl::sva_tick_reports(
    AssertionStorage& storage, sva::Monitor& active_monitor)
{
    const auto attempt_starts = active_monitor.take_started();
    if (sva_attempt_hook) {
        for (const auto when : attempt_starts) {
            sva_attempt_hook(storage.design_process(), when);
        }
    }
    // The tick produced step events only for attempts stepping is on for.
    if (active_monitor.has_step_events()) {
        const auto events = active_monitor.take_step_events();
        if (sva_step_hook) {
            for (const auto& event : events) {
                sva_step_hook(storage.design_process(), event);
            }
        }
    }
}

void Interpreter::Impl::run_sva(
    AssertionStorage& storage,
    const SvaOperation& operation)
{
    const auto known_one = [](const PackedLogic4& value,
                               const std::size_t bit) {
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        const auto word = bit / 64U;
        const auto mask = std::uint64_t { 1 } << (bit % 64U);
        return word < aval.size() && (aval[word] & mask) != 0U
            && (word >= bval.size() || (bval[word] & mask) == 0U);
    };
    const auto write_result = [&](const std::size_t value) {
        storage.write(operation.result,
            PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(value), 0));
    };
    if (operation.kind == SvaKind::control) {
        // The assertion control word of the site (20.11), as generated
        // code loads it (KOp::sva_sampled_load, aux 8).
        plan_assertion_controls();
        storage.write(operation.result,
            PackedLogic4::from_aval_bval(32U,
                static_cast<std::uint32_t>(assertion_control_word(
                    storage.design_process(), operation.site)),
                0U));
        return;
    }
    if (operation.kind == SvaKind::outcome) {
        // The same pop as the compiled kernel's inline KOp::sva_outcome;
        // the start and fail position of the outcome stay in the block.
        auto* const active_monitor
            = sva_monitor_find(storage.process(), operation.site);
        if (active_monitor == nullptr) {
            write_result(0U);
            return;
        }
        const auto outcome = active_monitor->next_outcome(++sva_pop_serial);
        write_result(static_cast<std::size_t>(
            active_monitor->reported_code(outcome)));
        return;
    }
    if (operation.kind == SvaKind::call) {
        // The next queued subroutine call of the last tick (16.11).
        auto* const active_monitor
            = sva_monitor_find(storage.process(), operation.site);
        write_result(active_monitor == nullptr ? 0U : active_monitor->take_call());
        return;
    }
    if (operation.kind == SvaKind::request || operation.kind == SvaKind::local
        || operation.kind == SvaKind::answer) {
        // A request of the monitor's last tick (16.10): the expression it
        // asks for, a local of its valuation, or its value.
        auto* const active_monitor
            = sva_monitor_find(storage.process(), operation.site);
        const auto request = storage.read(operation.start, 32U).known_unsigned_value();
        const bool call_local = operation.kind == SvaKind::local && request
            && *request == sva::Monitor::call_request;
        if (active_monitor == nullptr || !request
            || (!call_local && *request >= active_monitor->pending_requests())) {
            storage.fail("concurrent property local-variable request is malformed");
            write_result(0U);
            return;
        }
        const auto index = static_cast<std::size_t>(*request);
        if (operation.kind == SvaKind::request) {
            write_result(active_monitor->request_expression(index));
            return;
        }
        if (operation.kind == SvaKind::local) {
            const auto width = active_monitor->local_width(operation.guard_count);
            const auto words = active_monitor->request_local(index, operation.guard_count);
            const auto half = words.size() / 2U;
            std::vector<std::uint64_t> aval(half);
            std::vector<std::uint64_t> bval(half);
            for (std::size_t word = 0; word < half; ++word) {
                aval[word] = static_cast<std::uint64_t>(words[word]);
                bval[word] = static_cast<std::uint64_t>(words[half + word]);
            }
            storage.write(operation.result,
                PackedLogic4::from_word_planes(width, aval, bval));
            return;
        }
        const auto value = storage.read(operation.guards, operation.guard_count);
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        const auto chunks = (std::max<std::size_t>(operation.guard_count, 1U) + 63U) / 64U;
        std::vector<std::int64_t> words(2U * chunks, 0);
        for (std::size_t word = 0; word < chunks; ++word) {
            words[word] = word < aval.size() ? static_cast<std::int64_t>(aval[word]) : 0;
            words[chunks + word]
                = word < bval.size() ? static_cast<std::int64_t>(bval[word]) : 0;
        }
        active_monitor->answer(index, words);
        write_result(0U);
        return;
    }
    auto& active_monitor = sva_monitor_bind(storage, operation);
    std::vector<bool> guards(operation.guard_count);
    if (operation.guard_count != 0U) {
        const auto values = storage.read(operation.guards, operation.guard_count);
        for (std::size_t index = 0; index < guards.size(); ++index) {
            guards[index] = known_one(values, index);
        }
    }
    const bool start = known_one(storage.read(operation.start, 1U), 0U);
    const bool disabled = known_one(storage.read(operation.disabled, 1U), 0U);
    write_result(sva_tick_core(storage, active_monitor, guards, start, disabled));
}

} // namespace fsim::runtime::simir
