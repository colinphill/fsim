// SPDX-License-Identifier: Apache-2.0
#include "scheduler_internal.hpp"

namespace fsim::runtime {

ScheduledTaskHandle::operator bool() const noexcept
{
    const auto owner = owner_.lock();
    return owner && owner->active({ slot_, generation_ });
}

const char* phase_name(SchedulerPhase phase) noexcept
{
    switch (phase) {
    case SchedulerPhase::active:
        return "active";
    case SchedulerPhase::inactive:
        return "inactive";
    case SchedulerPhase::update:
        return "update";
    case SchedulerPhase::observed:
        return "observed";
    case SchedulerPhase::reactive:
        return "reactive";
    case SchedulerPhase::re_inactive:
        return "re-inactive";
    case SchedulerPhase::re_update:
        return "re-update";
    case SchedulerPhase::postponed:
        return "postponed";
    }
    return "unknown";
}

DeltaCycleLimitError::DeltaCycleLimitError(
    SimulationTick time, std::uint64_t limit,
    std::vector<StableOrder> pending_orders,
    std::vector<RuntimeSignalId> recent_signals)
    : std::runtime_error(delta_error_message(time, limit))
    , time_(time)
    , limit_(limit)
    , pending_orders_(std::move(pending_orders))
    , recent_signals_(std::move(recent_signals))
{
}

Scheduler::Scheduler(SchedulerOptions options)
    : impl_(std::make_unique<Impl>(options))
{
}

Scheduler::~Scheduler()
{
    if (impl_)
        impl_->discard_queued_and_notify();
}
Scheduler::Scheduler(Scheduler&& other)
{
    if (other.impl_
        && (other.impl_->in_run || other.impl_->discarding)) {
        throw std::logic_error(
            "cannot move a running or releasing scheduler");
    }
    if (other.impl_ && !other.impl_->discard_hooks.empty()) {
        other.impl_->discard_queued_and_notify();
        other.impl_->discard_hooks.clear();
    }
    impl_ = std::move(other.impl_);
}
Scheduler& Scheduler::operator=(Scheduler&& other) noexcept
{
    if (this == &other || (impl_ && (impl_->discarding || impl_->in_run))
        || (other.impl_ && (other.impl_->discarding || other.impl_->in_run)))
        return *this;
    std::vector<Impl::DiscardHookEntry> retained_hooks;
    DiscardHookToken next_discard_hook_token { 1U };
    if (impl_) {
        impl_->discard_queued_and_notify();
        retained_hooks = std::move(impl_->discard_hooks);
        next_discard_hook_token = impl_->next_discard_hook_token;
    }
    if (other.impl_ && !other.impl_->discard_hooks.empty()) {
        other.impl_->discard_queued_and_notify();
        other.impl_->discard_hooks.clear();
    }
    impl_ = std::move(other.impl_);
    if (impl_) {
        impl_->discard_hooks = std::move(retained_hooks);
        impl_->next_discard_hook_token = next_discard_hook_token;
    }
    return *this;
}

void Scheduler::schedule_at(SimulationTick time, SchedulerPhase phase,
    StableOrder stable_order, Task task)
{
    if (impl_->discarding)
        return;
    if (time < impl_->now) {
        throw std::invalid_argument("cannot schedule an event in the past");
    }
    auto entry = impl_->make_entry(stable_order, std::move(task));
    impl_->enqueue_at(time, phase, std::move(entry));
}

void Scheduler::schedule_after(SimulationTick delay, SchedulerPhase phase,
    StableOrder stable_order, Task task)
{
    if (impl_->discarding)
        return;
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now) {
        throw std::overflow_error("simulation time overflow while scheduling event");
    }
    schedule_at(impl_->now + delay, phase, stable_order, std::move(task));
}

ScheduledTaskHandle Scheduler::schedule_after_cancelable(
    const SimulationTick delay,
    const SchedulerPhase phase,
    const StableOrder stable_order,
    Task task)
{
    if (impl_->discarding)
        return { };
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now) {
        throw std::overflow_error("simulation time overflow while scheduling event");
    }
    const auto time = impl_->now + delay;
    const auto index = phase_index(phase);
    if (index >= phase_count) {
        throw std::invalid_argument("invalid scheduler phase");
    }
    if (!task) {
        throw std::invalid_argument("cannot schedule an empty task");
    }
    if (impl_->next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("scheduler insertion sequence overflow");
    }
    const auto sequence = impl_->next_sequence++;
    const auto cancellation = impl_->cancel_slots->acquire(std::move(task));
    // A moved-from std::function may still retain its target. Keep only the
    // cancellation slot's callback; the source parameter dies after enqueue.
    Entry entry { stable_order, sequence, EntryTask { std::monostate { } },
        impl_->cancel_slots.get(), cancellation };

    try {
        impl_->enqueue_at(time, phase, std::move(entry), false);
    } catch (...) {
        impl_->cancel_slots->release(cancellation);
        throw;
    }
    return ScheduledTaskHandle {
        impl_->cancel_slots, cancellation.slot, cancellation.generation };
}

void Scheduler::cancel(const ScheduledTaskHandle& handle) noexcept
{
    if (!impl_)
        return;
    const auto owner = handle.owner_.lock();
    const CancellationSlots::Identity identity {
        handle.slot_, handle.generation_ };
    if (owner && owner.get() == impl_->cancel_slots.get()
        && impl_->cancel_slots->active(identity)) {
        impl_->cancel_slots->release(identity);
        impl_->note_cancellation();
    }
}

void Scheduler::schedule(SchedulerPhase phase, StableOrder stable_order,
    Task task)
{
    schedule_at(impl_->now, phase, stable_order, std::move(task));
}

void Scheduler::schedule_next_delta(SchedulerPhase phase,
    StableOrder stable_order, Task task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_entry(stable_order, std::move(task));
    impl_->enqueue_next_delta(phase, std::move(entry));
}

void Scheduler::schedule_next_delta_batchable(
    const SchedulerPhase phase,
    const StableOrder stable_order,
    SchedulerBatchTask& batch_task,
    const std::uint64_t batch_payload,
    Task fallback_task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_batch_entry(
        stable_order, batch_task, batch_payload, std::move(fallback_task));
    impl_->enqueue_next_delta(phase, std::move(entry));
}

SchedulerOrderKey Scheduler::reserve_order_key(
    const StableOrder stable_order)
{
    if (impl_->next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("scheduler insertion sequence overflow");
    }
    return SchedulerOrderKey {
        stable_order, impl_->next_sequence++, impl_.get(),
        impl_->reservation_epoch
    };
}

void Scheduler::schedule_reserved_next_delta(
    const SchedulerPhase phase, const SchedulerOrderKey key, Task task)
{
    if (impl_->discarding) {
        return;
    }
    if (!task || key.owner_ != impl_.get()
        || key.epoch_ != impl_->reservation_epoch
        || key.sequence >= impl_->next_sequence) {
        throw std::invalid_argument("invalid reserved scheduler task");
    }
    auto entry = Entry {
        key.order, key.sequence, EntryTask { std::move(task) },
        nullptr, { }
    };
    impl_->enqueue_next_delta(phase, std::move(entry));
}

ScheduledTaskHandle Scheduler::schedule_reserved_next_delta_cancelable(
    const SchedulerPhase phase, const SchedulerOrderKey key, Task task)
{
    if (impl_->discarding) {
        return { };
    }
    if (!task || key.owner_ != impl_.get()
        || key.epoch_ != impl_->reservation_epoch
        || key.sequence >= impl_->next_sequence) {
        throw std::invalid_argument("invalid reserved scheduler task");
    }
    const auto cancellation = impl_->cancel_slots->acquire(std::move(task));
    auto entry = Entry {
        key.order, key.sequence, EntryTask { std::monostate { } },
        impl_->cancel_slots.get(), cancellation
    };
    try {
        impl_->enqueue_next_delta(phase, std::move(entry));
    } catch (...) {
        impl_->cancel_slots->release(cancellation);
        throw;
    }
    return ScheduledTaskHandle {
        impl_->cancel_slots, cancellation.slot, cancellation.generation
    };
}

void Scheduler::schedule_reserved_current(
    const SchedulerPhase phase, const SchedulerOrderKey key, Task task)
{
    if (impl_->discarding) {
        return;
    }
    if (!impl_->in_callback || !impl_->batch_entries.empty()
        || !impl_->current
        || impl_->current->systemverilog_phase
        || impl_->current->executing_end_of_slot
        || impl_->current->phase >= phase_count
        || phase != static_cast<SchedulerPhase>(impl_->current->phase)) {
        throw std::logic_error(
            "reserved current task requires its ordinary callback phase");
    }
    if (!task || key.owner_ != impl_.get()
        || key.epoch_ != impl_->reservation_epoch
        || key.sequence >= impl_->next_sequence) {
        throw std::invalid_argument("invalid reserved scheduler task");
    }
    auto entry = Entry {
        key.order, key.sequence, EntryTask { std::move(task) },
        nullptr, { }
    };
    impl_->enqueue_at(impl_->now, phase, std::move(entry));
}

ScheduledTaskHandle Scheduler::schedule_reserved_current_cancelable(
    const SchedulerPhase phase, const SchedulerOrderKey key, Task task)
{
    if (impl_->discarding) {
        return { };
    }
    if (!impl_->in_callback || !impl_->batch_entries.empty()
        || !impl_->current
        || impl_->current->systemverilog_phase
        || impl_->current->executing_end_of_slot
        || impl_->current->phase >= phase_count
        || phase != static_cast<SchedulerPhase>(impl_->current->phase)) {
        throw std::logic_error(
            "reserved current task requires its ordinary callback phase");
    }
    if (!task || key.owner_ != impl_.get()
        || key.epoch_ != impl_->reservation_epoch
        || key.sequence >= impl_->next_sequence) {
        throw std::invalid_argument("invalid reserved scheduler task");
    }
    const auto cancellation = impl_->cancel_slots->acquire(std::move(task));
    auto entry = Entry {
        key.order, key.sequence, EntryTask { std::monostate { } },
        impl_->cancel_slots.get(), cancellation
    };
    try {
        impl_->enqueue_at(impl_->now, phase, std::move(entry));
    } catch (...) {
        impl_->cancel_slots->release(cancellation);
        throw;
    }
    return ScheduledTaskHandle {
        impl_->cancel_slots, cancellation.slot, cancellation.generation
    };
}

std::optional<SchedulerOrderKey>
Scheduler::next_current_order_key() const
{
    if (!impl_->in_callback || !impl_->batch_entries.empty()
        || !impl_->current
        || impl_->current->systemverilog_phase
        || impl_->current->executing_end_of_slot
        || impl_->current->phase >= phase_count) {
        throw std::logic_error(
            "next current order key requires an ordinary scheduler callback");
    }
    auto& queue = impl_->current->current.queues[impl_->current->phase];
    const auto* const next = queue.next();
    return next == nullptr
        ? std::nullopt
        : std::optional<SchedulerOrderKey> {
              SchedulerOrderKey {
                  next->order, next->sequence, impl_.get(),
                  impl_->reservation_epoch
              }
          };
}

void Scheduler::schedule_internal_at(
    const SimulationTick time,
    const SchedulerPhase phase,
    const StableOrder stable_order,
    detail::SchedulerTaskDescriptor task)
{
    if (impl_->discarding)
        return;
    if (time < impl_->now) {
        throw std::invalid_argument("cannot schedule an event in the past");
    }
    auto entry = impl_->make_internal_entry(stable_order, std::move(task));
    impl_->enqueue_at(time, phase, std::move(entry));
}

void Scheduler::schedule_internal(
    const SchedulerPhase phase,
    const StableOrder stable_order,
    detail::SchedulerTaskDescriptor task)
{
    schedule_internal_at(
        impl_->now, phase, stable_order, std::move(task));
}

void Scheduler::schedule_internal_next_delta(
    const SchedulerPhase phase,
    const StableOrder stable_order,
    detail::SchedulerTaskDescriptor task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_internal_entry(stable_order, std::move(task));
    impl_->enqueue_next_delta(phase, std::move(entry));
}

void Scheduler::note_signal_change(RuntimeSignalId signal)
{
    impl_->trace(SchedulerTraceKind::signal_change, nullptr, 0, signal);
    const auto capacity = impl_->options.recent_signal_capacity;
    if (capacity == 0) {
        return;
    }
    if (impl_->recent_signals.size() < capacity) {
        impl_->recent_signals.push_back(signal);
        return;
    }
    impl_->recent_signals[impl_->recent_signal_cursor] = signal;
    impl_->recent_signal_cursor = (impl_->recent_signal_cursor + 1) % impl_->recent_signals.size();
}

void Scheduler::note_signal_transaction(RuntimeSignalId signal) noexcept
{
    impl_->trace(SchedulerTraceKind::signal_transaction, nullptr, 0, signal);
}

void Scheduler::set_trace_hook(void* context, TraceHook hook) noexcept
{
    impl_->trace_context = context;
    impl_->trace_hook = hook;
}

void Scheduler::request_stop() noexcept
{
    impl_->stop.store(true, std::memory_order_relaxed);
}

void Scheduler::clear_stop() noexcept
{
    impl_->stop.store(false, std::memory_order_relaxed);
}

bool Scheduler::stop_requested() const noexcept
{
    return impl_->stop.load(std::memory_order_relaxed);
}

void Scheduler::discard_pending()
{
    if (!impl_ || impl_->discarding)
        return;
    if (impl_->in_run || impl_->in_callback) {
        throw std::logic_error(
            "cannot discard scheduler work while it is running");
    }
    impl_->discard_queued_and_notify();
    ++impl_->reservation_epoch;
}

void Scheduler::reset()
{
    if (!impl_ || impl_->discarding)
        return;
    discard_pending();
    impl_->now = 0;
    impl_->callbacks = 0;
    impl_->next_sequence = 0;
    impl_->recent_signals.clear();
    impl_->recent_signal_cursor = 0;
    impl_->stop.store(false, std::memory_order_relaxed);
}

bool Scheduler::has_pending() const noexcept
{
    if (impl_->current) {
        return true;
    }
    return std::any_of(
        impl_->future.begin(),
        impl_->future.end(),
        [](const auto& entry) { return !entry.second.empty(); });
}

std::optional<SimulationTick>
Scheduler::next_pending_time() const noexcept
{
    for (const auto& [time, bucket] : impl_->future) {
        if (!bucket.empty()) {
            return time;
        }
    }
    return std::nullopt;
}

bool Scheduler::running() const noexcept { return impl_->in_run; }
bool Scheduler::at_safe_point() const noexcept { return impl_->in_safe_point; }
bool Scheduler::at_runtime_slot_quiet_point() const noexcept
{
    return impl_->in_runtime_slot_quiet_point;
}
SimulationTick Scheduler::now() const noexcept { return impl_->now; }
std::uint64_t Scheduler::delta() const noexcept
{
    return impl_->current ? impl_->current->delta : 0;
}

std::optional<SchedulerPhase> Scheduler::current_phase() const noexcept
{
    return impl_->executing_phase();
}

std::uint64_t Scheduler::current_phase_revision() const noexcept
{
    return impl_->current_phase_revision;
}

void Scheduler::set_safe_point_hook(SafePointHook hook)
{
    std::shared_ptr<const SafePointHook> replacement;
    if (hook) {
        replacement = std::make_shared<const SafePointHook>(std::move(hook));
    }
    auto previous = std::move(impl_->safe_point_hook);
    impl_->safe_point_hook = std::move(replacement);
    previous.reset();
}

Scheduler::SafePointHookToken Scheduler::add_safe_point_hook(
    SafePointHook hook)
{
    if (!hook) {
        throw std::invalid_argument("safe-point observer cannot be empty");
    }
    if (impl_->next_safe_point_hook_token == 0U) {
        throw std::overflow_error("safe-point observer token space exhausted");
    }
    const auto token = impl_->next_safe_point_hook_token;
    auto callback = std::make_shared<const SafePointHook>(std::move(hook));
    impl_->safe_point_hooks.emplace_back(token, std::move(callback));
    ++impl_->next_safe_point_hook_token;
    return token;
}

void Scheduler::remove_safe_point_hook(const SafePointHookToken token) noexcept
{
    if (token == 0U) {
        return;
    }
    const auto found = std::find_if(
        impl_->safe_point_hooks.begin(),
        impl_->safe_point_hooks.end(),
        [token](const auto& entry) { return entry.first == token; });
    if (found == impl_->safe_point_hooks.end()) {
        return;
    }
    auto removed = std::move(found->second);
    impl_->safe_point_hooks.erase(found);
    removed.reset();
}

Scheduler::DiscardHookToken Scheduler::add_discard_hook(
    void* context, const DiscardHook hook)
{
    if (hook == nullptr) {
        throw std::invalid_argument("discard observer cannot be empty");
    }
    if (!impl_ || impl_->next_discard_hook_token == 0U) {
        throw std::overflow_error("discard observer token space exhausted");
    }
    const auto token = impl_->next_discard_hook_token;
    impl_->discard_hooks.push_back({ token, context, hook });
    ++impl_->next_discard_hook_token;
    return token;
}

void Scheduler::remove_discard_hook(
    const DiscardHookToken token) noexcept
{
    if (token == 0U || !impl_) {
        return;
    }
    const auto found = std::find_if(
        impl_->discard_hooks.begin(), impl_->discard_hooks.end(),
        [token](const auto& entry) { return entry.token == token; });
    if (found != impl_->discard_hooks.end()) {
        impl_->discard_hooks.erase(found);
    }
}

void Scheduler::set_slot_start_hook(SlotStartHook hook)
{
    impl_->slot_start_hook = std::move(hook);
}

void Scheduler::set_runtime_slot_quiet_hook(
    void* context, const RuntimeSlotQuietHook hook)
{
    if (impl_->in_run || impl_->in_callback) {
        throw std::logic_error {
            "cannot replace the runtime slot-quiet hook while running"
        };
    }
    if ((context == nullptr) != (hook == nullptr)) {
        throw std::invalid_argument {
            "runtime slot-quiet hook context and callback must be paired"
        };
    }
    impl_->runtime_slot_quiet_context = context;
    impl_->runtime_slot_quiet_hook = hook;
}

} // namespace fsim::runtime
