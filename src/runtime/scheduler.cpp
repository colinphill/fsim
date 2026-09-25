// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <sstream>
#include <utility>
#include <variant>

namespace fsim::runtime {

class CancellationSlots {
public:
    struct Identity {
        std::size_t slot;
        std::uint64_t generation;
    };

    [[nodiscard]] Identity acquire(Scheduler::Task task)
    {
        if (free_head_ != no_slot) {
            const auto index = free_head_;
            auto& slot = slots_[index];
            free_head_ = slot.next_free;
            ++slot.generation;
            slot.task = std::move(task);
            slot.active = true;
            return { index, slot.generation };
        }
        slots_.push_back({ 1U, true, no_slot, std::move(task) });
        return { slots_.size() - 1U, 1U };
    }

    [[nodiscard]] bool active(Identity identity) const noexcept
    {
        return identity.slot < slots_.size()
            && slots_[identity.slot].generation == identity.generation
            && slots_[identity.slot].active;
    }

    void release(Identity identity) noexcept
    {
        if (!active(identity))
            return;
        auto& slot = slots_[identity.slot];
        Scheduler::Task released_task;
        released_task.swap(slot.task);
        slot.active = false;
        if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
            slot.next_free = free_head_;
            free_head_ = identity.slot;
        }
        // A captured payload can reenter Scheduler as it is destroyed. Finish
        // all slot mutations before invoking any of its destructors.
        released_task = nullptr;
    }

    [[nodiscard]] Scheduler::Task take(Identity identity) noexcept
    {
        Scheduler::Task task;
        slots_[identity.slot].task.swap(task);
        release(identity);
        return task;
    }

private:
    static constexpr std::size_t no_slot = std::numeric_limits<std::size_t>::max();

    struct Slot {
        std::uint64_t generation;
        bool active;
        std::size_t next_free;
        Scheduler::Task task;
    };

    std::vector<Slot> slots_;
    std::size_t free_head_ = no_slot;
};

ScheduledTaskHandle::operator bool() const noexcept
{
    const auto owner = owner_.lock();
    return owner && owner->active({ slot_, generation_ });
}

namespace {

    constexpr std::size_t phase_count = 8;

    [[nodiscard]] constexpr std::size_t phase_index(SchedulerPhase phase)
    {
        return static_cast<std::size_t>(phase);
    }

    struct BatchEntry {
        SchedulerBatchTask* task { };
        std::uint64_t payload { };
        Scheduler::Task fallback;
    };

    using EntryTask = std::variant<Scheduler::Task, BatchEntry,
        detail::SchedulerTaskDescriptor, std::monostate>;

    struct Entry {
        StableOrder order { };
        std::uint64_t sequence { };
        EntryTask task;
        CancellationSlots* cancel_slots { };
        CancellationSlots::Identity cancellation { };

        [[nodiscard]] bool is_cancelled() const noexcept
        {
            return cancel_slots && !cancel_slots->active(cancellation);
        }

        [[nodiscard]] SchedulerBatchTask* batch_task() const noexcept
        {
            const auto* batch = std::get_if<BatchEntry>(&task);
            return batch == nullptr ? nullptr : batch->task;
        }

        [[nodiscard]] std::uint64_t batch_payload() const noexcept
        {
            return std::get<BatchEntry>(task).payload;
        }

        void complete() noexcept
        {
            if (cancel_slots)
                cancel_slots->release(cancellation);
        }

        [[nodiscard]] Scheduler::Task take_task() noexcept
        {
            if (cancel_slots)
                return cancel_slots->take(cancellation);
            return std::move(std::get<Scheduler::Task>(task));
        }

        [[nodiscard]] Scheduler::Task take_batch_fallback() noexcept
        {
            return std::move(std::get<BatchEntry>(task).fallback);
        }

        [[nodiscard]] detail::SchedulerTaskDescriptor take_descriptor() noexcept
        {
            return std::move(
                std::get<detail::SchedulerTaskDescriptor>(task));
        }
    };

    struct WorkQueue {
        std::vector<Entry> entries;
        std::size_t cursor { };
        std::size_t pushes_since_compaction { };
        bool needs_sort { };
        bool has_cancelable { };

        void compact() noexcept
        {
            entries.erase(entries.begin(),
                entries.begin() + static_cast<std::ptrdiff_t>(cursor));
            cursor = 0;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                [](const Entry& entry) { return entry.is_cancelled(); }),
                entries.end());
            pushes_since_compaction = 0;
            has_cancelable = std::any_of(entries.begin(), entries.end(),
                [](const Entry& entry) { return entry.cancel_slots != nullptr; });
            if (entries.empty())
                needs_sort = false;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return std::none_of(
                entries.begin() + static_cast<std::ptrdiff_t>(cursor),
                entries.end(),
                [](const Entry& entry) { return !entry.is_cancelled(); });
        }

        void push(Entry entry)
        {
            // A cancelled task has already released its payload. Reclaim its
            // queue entry after enough pushes to amortize a scan of the
            // remaining bucket.
            if (has_cancelable && entries.size() >= 64U
                && pushes_since_compaction >= entries.size() / 2U)
                compact();
            if (!needs_sort && cursor < entries.size()) {
                const auto& last = entries.back();
                needs_sort = entry.order < last.order
                    || (entry.order == last.order
                        && entry.sequence < last.sequence);
            }
            const bool cancelable = entry.cancel_slots != nullptr;
            entries.push_back(std::move(entry));
            has_cancelable |= cancelable;
            ++pushes_since_compaction;
        }

        void prepare()
        {
            if (needs_sort) {
                std::sort(entries.begin() + static_cast<std::ptrdiff_t>(cursor),
                    entries.end(), [](const Entry& lhs, const Entry& rhs) {
                        if (lhs.order != rhs.order) {
                            return lhs.order < rhs.order;
                        }
                        return lhs.sequence < rhs.sequence;
                    });
                needs_sort = false;
            }
            while (cursor < entries.size() && entries[cursor].is_cancelled()) {
                ++cursor;
            }
            if (has_cancelable && cursor >= 64U
                && cursor >= entries.size() / 2U)
                compact();
        }

        [[nodiscard]] const Entry* next()
        {
            prepare();
            return cursor < entries.size() ? &entries[cursor] : nullptr;
        }

        Entry pop()
        {
            prepare();
            return std::move(entries.at(cursor++));
        }

        void clear_consumed()
        {
            if (empty()) {
                entries.clear();
                cursor = 0;
                pushes_since_compaction = 0;
                needs_sort = false;
                has_cancelable = false;
            }
        }

        void cancel_pending() noexcept
        {
            for (auto index = cursor; index < entries.size(); ++index) {
                entries[index].complete();
            }
        }

        [[nodiscard]] std::vector<StableOrder> pending_orders() const
        {
            std::vector<StableOrder> result;
            result.reserve(
                entries.size() - cursor);
            for (auto index = cursor; index < entries.size(); ++index) {
                if (!entries[index].is_cancelled()) {
                    result.push_back(entries[index].order);
                }
            }
            return result;
        }
    };

    struct Bucket {
        std::array<WorkQueue, phase_count> queues;

        [[nodiscard]] std::size_t compact_cancelled() noexcept
        {
            std::size_t retained = 0;
            for (auto& queue : queues) {
                queue.compact();
                retained += queue.entries.size();
            }
            return retained;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return std::all_of(queues.begin(), queues.end(),
                [](const WorkQueue& queue) { return queue.empty(); });
        }

        void cancel_pending() noexcept
        {
            for (auto& queue : queues)
                queue.cancel_pending();
        }
    };

    struct CurrentSlot {
        SimulationTick time { };
        std::uint64_t delta { };
        std::size_t phase { };
        Bucket current;
        Bucket next_delta;
    };

    [[nodiscard]] std::string delta_error_message(SimulationTick time,
        std::uint64_t limit)
    {
        std::ostringstream message;
        message << "maximum delta-cycle count (" << limit
                << ") exceeded at simulation tick " << time;
        return message.str();
    }

} // namespace

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

struct Scheduler::Impl {
    explicit Impl(SchedulerOptions scheduler_options)
        : options(scheduler_options)
    {
        if (options.max_delta_cycles == 0) {
            throw std::invalid_argument("max_delta_cycles must be greater than zero");
        }
    }

    SchedulerOptions options;
    std::map<SimulationTick, Bucket> future;
    std::optional<CurrentSlot> current;
    SimulationTick now { };
    std::uint64_t callbacks { };
    std::uint64_t next_sequence { };
    std::uint64_t current_phase_revision { };
    bool in_run { };
    bool in_callback { };
    bool in_safe_point { };
    bool discarding { };
    std::atomic_bool stop { false };
    std::shared_ptr<CancellationSlots> cancel_slots
        = std::make_shared<CancellationSlots>();
    SlotStartHook slot_start_hook;
    std::shared_ptr<const SafePointHook> safe_point_hook;
    using SafePointHookEntry
        = std::pair<SafePointHookToken, std::shared_ptr<const SafePointHook>>;
    std::vector<SafePointHookEntry> safe_point_hooks;
    std::vector<std::shared_ptr<const SafePointHook>> safe_point_hook_snapshot;
    SafePointHookToken next_safe_point_hook_token { 1U };
    struct DiscardHookEntry {
        DiscardHookToken token { };
        void* context { };
        DiscardHook callback { };
    };
    std::vector<DiscardHookEntry> discard_hooks;
    DiscardHookToken next_discard_hook_token { 1U };
    std::vector<Entry> batch_entries;
    std::vector<std::uint64_t> batch_payloads;
    std::vector<RuntimeSignalId> recent_signals;
    std::size_t recent_signal_cursor { };
    std::size_t cancellations_since_compaction { };
    std::size_t cancellation_compaction_threshold { 64U };

    void note_cancellation() noexcept
    {
        ++cancellations_since_compaction;
        if (cancellations_since_compaction < cancellation_compaction_threshold)
            return;

        std::size_t retained = 0;
        if (current) {
            retained += current->current.compact_cancelled();
            retained += current->next_delta.compact_cancelled();
        }
        for (auto bucket = future.begin(); bucket != future.end();) {
            retained += bucket->second.compact_cancelled();
            if (bucket->second.empty())
                bucket = future.erase(bucket);
            else
                ++bucket;
        }
        cancellations_since_compaction = 0;
        cancellation_compaction_threshold = std::max<std::size_t>(
            64U, retained / 2U);
    }

    void discard_queued() noexcept
    {
        const auto was_discarding = discarding;
        discarding = true;
        clear_batch_scratch();
        auto discarded_current = std::move(current);
        current.reset();
        std::map<SimulationTick, Bucket> discarded_future;
        discarded_future.swap(future);

        if (discarded_current) {
            discarded_current->current.cancel_pending();
            discarded_current->next_delta.cancel_pending();
        }
        for (auto& [time, bucket] : discarded_future) {
            (void)time;
            bucket.cancel_pending();
        }
        discarded_current.reset();
        discarded_future.clear();
        cancellations_since_compaction = 0;
        cancellation_compaction_threshold = 64U;
        discarding = was_discarding;
    }

    void notify_discard_hooks() noexcept
    {
        for (const auto& hook : discard_hooks)
            hook.callback(hook.context);
    }

    void discard_queued_and_notify() noexcept
    {
        const auto was_discarding = discarding;
        discarding = true;
        discard_queued();
        notify_discard_hooks();
        discarding = was_discarding;
    }

    void clear_batch_scratch() noexcept
    {
        batch_entries.clear();
        batch_payloads.clear();
    }

    [[nodiscard]] Entry make_entry(
        StableOrder order,
        Task task,
        CancellationSlots* cancellation_slots = nullptr,
        const CancellationSlots::Identity cancellation = { })
    {
        if (!task) {
            throw std::invalid_argument("cannot schedule an empty task");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        return Entry {
            order, next_sequence++, EntryTask { std::move(task) },
            cancellation_slots, cancellation
        };
    }

    [[nodiscard]] Entry make_batch_entry(
        StableOrder order,
        SchedulerBatchTask& batch_task,
        const std::uint64_t batch_payload,
        Task fallback_task)
    {
        if (!fallback_task) {
            throw std::invalid_argument(
                "cannot schedule a batch with an empty fallback task");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        return Entry {
            order, next_sequence++,
            EntryTask { BatchEntry {
                &batch_task, batch_payload, std::move(fallback_task) } },
            nullptr, { }
        };
    }

    [[nodiscard]] Entry make_internal_entry(
        StableOrder order,
        detail::SchedulerTaskDescriptor task)
    {
        if (task.invoke == nullptr) {
            throw std::invalid_argument(
                "cannot schedule an empty task descriptor");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        return Entry {
            order, next_sequence++, EntryTask { std::move(task) }, nullptr, { }
        };
    }

    void enqueue_at(
        const SimulationTick time,
        SchedulerPhase phase,
        Entry entry,
        const bool adjust_reactive_regions = true)
    {
        if (time < now) {
            throw std::invalid_argument("cannot schedule an event in the past");
        }
        if (adjust_reactive_regions && current && time == current->time
            && current->phase < phase_count) {
            const auto current_phase
                = static_cast<SchedulerPhase>(current->phase);
            if (phase == SchedulerPhase::inactive
                && current_phase >= SchedulerPhase::reactive
                && current_phase <= SchedulerPhase::re_update) {
                phase = SchedulerPhase::re_inactive;
            } else if (phase == SchedulerPhase::update
                && current_phase >= SchedulerPhase::reactive
                && current_phase <= SchedulerPhase::re_update) {
                phase = SchedulerPhase::re_update;
            }
        }
        const auto index = phase_index(phase);
        if (index >= phase_count) {
            throw std::invalid_argument("invalid scheduler phase");
        }

        if (current && time == current->time) {
            const bool phase_finished = index < current->phase;
            auto& bucket = phase_finished ? current->next_delta : current->current;
            bucket.queues[index].push(std::move(entry));
            if (!phase_finished && index == current->phase) {
                ++current_phase_revision;
            }
            return;
        }
        future[time].queues[index].push(std::move(entry));
    }

    void enqueue_next_delta(
        const SchedulerPhase phase,
        Entry entry)
    {
        const auto index = phase_index(phase);
        if (index >= phase_count) {
            throw std::invalid_argument("invalid scheduler phase");
        }
        if (current) {
            current->next_delta.queues[index].push(std::move(entry));
            return;
        }
        // Before a run begins, "next delta" means the initial delta at now.
        future[now].queues[index].push(std::move(entry));
    }

    void load_next_slot()
    {
        auto first = future.begin();
        CurrentSlot slot;
        slot.time = first->first;
        slot.current = std::move(first->second);
        future.erase(first);
        now = slot.time;
        current.emplace(std::move(slot));
    }

    [[nodiscard]] std::vector<StableOrder> pending_next_orders() const
    {
        std::vector<StableOrder> result;
        if (!current) {
            return result;
        }
        for (const auto& queue : current->next_delta.queues) {
            auto orders = queue.pending_orders();
            result.insert(result.end(), orders.begin(), orders.end());
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
};

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

RunResult Scheduler::run(std::optional<SimulationTick> until)
{
    if (impl_->in_run) {
        throw std::logic_error("Scheduler::run is not reentrant");
    }
    if (until && *until < impl_->now) {
        throw std::invalid_argument("run time limit is before the current time");
    }

    struct RunGuard {
        bool& running;
        ~RunGuard() { running = false; }
    };
    impl_->in_run = true;
    RunGuard guard { impl_->in_run };
    const auto initial_callbacks = impl_->callbacks;
    auto& batch_entries = impl_->batch_entries;
    auto& batch_payloads = impl_->batch_payloads;
    struct RunScratchGuard {
        std::vector<Entry>& entries;
        std::vector<std::uint64_t>& payloads;

        ~RunScratchGuard()
        {
            entries.clear();
            payloads.clear();
        }
    } scratch_guard { batch_entries, batch_payloads };

    auto result = [&](RunStatus status) {
        if (!impl_->current && impl_->future.empty()) {
            impl_->cancellations_since_compaction = 0;
            impl_->cancellation_compaction_threshold = 64U;
        }
        return RunResult { status,
            impl_->now,
            impl_->current ? impl_->current->delta : 0,
            impl_->callbacks - initial_callbacks,
            std::nullopt };
    };

    while (true) {
        if (!impl_->current) {
            if (impl_->stop.load(std::memory_order_relaxed)) {
                return result(RunStatus::stopped);
            }
            while (!impl_->future.empty()
                && impl_->future.begin()->second.empty()) {
                impl_->future.erase(impl_->future.begin());
            }
            if (impl_->future.empty()) {
                if (until && impl_->now < *until) {
                    impl_->now = *until;
                    return result(RunStatus::time_limit);
                }
                return result(RunStatus::completed);
            }
            if (until && impl_->future.begin()->first > *until) {
                impl_->now = *until;
                return result(RunStatus::time_limit);
            }
            impl_->load_next_slot();
            if (impl_->slot_start_hook) {
                impl_->slot_start_hook(*this);
            }
        }

        auto& slot = *impl_->current;
        if (slot.phase == phase_count) {
            if (impl_->stop.load(std::memory_order_relaxed)) {
                return result(RunStatus::stopped);
            }
            if (slot.next_delta.empty()) {
                impl_->current.reset();
                continue;
            }
            if (slot.delta + 1 >= impl_->options.max_delta_cycles) {
                throw DeltaCycleLimitError(
                    slot.time, impl_->options.max_delta_cycles,
                    impl_->pending_next_orders(), impl_->recent_signals);
            }
            std::swap(slot.current, slot.next_delta);
            ++slot.delta;
            slot.phase = 0;
            continue;
        }

        auto& queue = slot.current.queues[slot.phase];
        const auto phase = static_cast<SchedulerPhase>(slot.phase);
        if (queue.empty()
            && impl_->stop.load(std::memory_order_relaxed)) {
            return result(RunStatus::stopped);
        }
        while (!queue.empty()) {
            if (impl_->stop.load(std::memory_order_relaxed)) {
                return result(RunStatus::stopped);
            }
            auto entry = queue.pop();
            if (entry.batch_task() != nullptr) {
                batch_entries.clear();
                batch_payloads.clear();
                auto* const batch_task = entry.batch_task();
                batch_entries.push_back(std::move(entry));
                while (const auto* next = queue.next()) {
                    if (next->batch_task() != batch_task) {
                        break;
                    }
                    batch_entries.push_back(queue.pop());
                }
                batch_payloads.reserve(batch_entries.size());
                for (const auto& candidate : batch_entries) {
                    batch_payloads.push_back(candidate.batch_payload());
                }

                impl_->in_callback = true;
                SchedulerBatchResult batch_result;
                try {
                    batch_result = batch_task->execute(
                        *this, batch_payloads);
                } catch (...) {
                    for (auto& candidate : batch_entries) {
                        queue.push(std::move(candidate));
                    }
                    impl_->clear_batch_scratch();
                    impl_->in_callback = false;
                    throw;
                }
                if (batch_result.executed > batch_entries.size()) {
                    for (auto& candidate : batch_entries) {
                        queue.push(std::move(candidate));
                    }
                    impl_->clear_batch_scratch();
                    impl_->in_callback = false;
                    throw std::logic_error(
                        "scheduler batch consumed an invalid task count");
                }

                const auto consumed = batch_result.executed;
                if (consumed == 0U && !batch_result.failure) {
                    auto fallback = std::move(batch_entries.front());
                    for (std::size_t index = 1U;
                        index < batch_entries.size(); ++index) {
                        queue.push(std::move(batch_entries[index]));
                    }
                    impl_->clear_batch_scratch();
                    fallback.complete();
                    try {
                        auto task = fallback.take_batch_fallback();
                        task(*this);
                    } catch (...) {
                        impl_->in_callback = false;
                        throw;
                    }
                    impl_->in_callback = false;
                    ++impl_->callbacks;
                    continue;
                }

                for (std::size_t index = 0U; index < consumed; ++index) {
                    batch_entries[index].complete();
                }
                for (auto index = consumed;
                    index < batch_entries.size(); ++index) {
                    queue.push(std::move(batch_entries[index]));
                }
                impl_->clear_batch_scratch();
                impl_->callbacks += consumed;
                impl_->in_callback = false;
                if (batch_result.failure) {
                    std::rethrow_exception(batch_result.failure);
                }
                continue;
            }
            impl_->in_callback = true;
            try {
                if (std::holds_alternative<
                        detail::SchedulerTaskDescriptor>(entry.task)) {
                    auto descriptor = entry.take_descriptor();
                    descriptor(*this);
                } else {
                    auto task = entry.take_task();
                    task(*this);
                }
            } catch (...) {
                impl_->in_callback = false;
                throw;
            }
            impl_->in_callback = false;
            ++impl_->callbacks;
        }
        queue.clear_consumed();

        ++slot.phase;
        if (impl_->safe_point_hook || !impl_->safe_point_hooks.empty()) {
            const auto safe_point_hook = impl_->safe_point_hook;
            auto& safe_point_hook_snapshot
                = impl_->safe_point_hook_snapshot;
            safe_point_hook_snapshot.clear();
            if (safe_point_hook_snapshot.capacity()
                < impl_->safe_point_hooks.size()) {
                safe_point_hook_snapshot.reserve(
                    impl_->safe_point_hooks.size());
            }
            for (const auto& [token, hook] : impl_->safe_point_hooks) {
                (void)token;
                safe_point_hook_snapshot.push_back(hook);
            }
            // Advance the phase cursor before exposing the safe point. A callback
            // that schedules into the just-completed phase must enter the next
            // delta rather than an already-consumed queue.
            impl_->in_safe_point = true;
            try {
                if (safe_point_hook) {
                    (*safe_point_hook)(*this, phase);
                }
                for (const auto& hook : safe_point_hook_snapshot) {
                    (*hook)(*this, phase);
                }
            } catch (...) {
                safe_point_hook_snapshot.clear();
                impl_->in_safe_point = false;
                throw;
            }
            safe_point_hook_snapshot.clear();
            impl_->in_safe_point = false;
        }
    }
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
SimulationTick Scheduler::now() const noexcept { return impl_->now; }
std::uint64_t Scheduler::delta() const noexcept
{
    return impl_->current ? impl_->current->delta : 0;
}

std::optional<SchedulerPhase> Scheduler::current_phase() const noexcept
{
    if (!impl_->current || impl_->current->phase >= phase_count) {
        return std::nullopt;
    }
    return static_cast<SchedulerPhase>(impl_->current->phase);
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

} // namespace fsim::runtime
