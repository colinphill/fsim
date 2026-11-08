// SPDX-License-Identifier: Apache-2.0
// Scheduler event-kind registry and run-state save/restore of pending events
// (#128). Nothing here runs on the scheduling or dispatch path.
#include "fsim/runtime/scheduler_events.hpp"

#include "scheduler_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <tuple>

namespace fsim::runtime {

const char* scheduler_event_class_name(
    const SchedulerEventClass klass) noexcept
{
    switch (klass) {
    case SchedulerEventClass::data:
        return "data";
    case SchedulerEventClass::derived:
        return "derived";
    case SchedulerEventClass::foreign_callback:
        return "foreign_callback";
    case SchedulerEventClass::host_closure:
        return "host_closure";
    }
    return "unknown";
}

// ---------------------------------------------------------------- owners

void SchedulerEventOwners::bind(
    const std::string_view domain, void* const owner)
{
    for (auto& entry : owners_) {
        if (entry.first == domain) {
            entry.second = owner;
            return;
        }
    }
    owners_.emplace_back(std::string(domain), owner);
}

void* SchedulerEventOwners::find(
    const std::string_view domain) const noexcept
{
    for (const auto& entry : owners_) {
        if (entry.first == domain)
            return entry.second;
    }
    return nullptr;
}

// ------------------------------------------------------- writer and reader

void SchedulerEventWriter::put(
    const std::uint64_t value, const unsigned bytes)
{
    if (!ok())
        return;
    for (unsigned index = 0U; index < bytes; ++index) {
        out_->push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>(value >> (8U * index))));
    }
}

void SchedulerEventWriter::owner(const void* const pointer)
{
    if (!ok())
        return;
    if (pointer != nullptr
        && (domain_.empty() || pointer != owners_->find(domain_))) {
        fail("payload points outside the owner domain '"
            + std::string(domain_) + "'");
        return;
    }
    // A null owner is kept distinct so an unbound payload stays unbound.
    put(pointer == nullptr ? 0U : 1U, 1U);
}

void SchedulerEventWriter::fail(std::string message)
{
    if (error_.empty())
        error_ = message.empty() ? std::string("event encode failed")
                                 : std::move(message);
}

std::uint64_t SchedulerEventReader::get(const unsigned bytes)
{
    if (!ok())
        return 0U;
    if (in_.size() - position_ < bytes) {
        fail("saved event payload is truncated");
        return 0U;
    }
    std::uint64_t value { };
    for (unsigned index = 0U; index < bytes; ++index) {
        value |= static_cast<std::uint64_t>(
                     static_cast<std::uint8_t>(in_[position_ + index]))
            << (8U * index);
    }
    position_ += bytes;
    return value;
}

void* SchedulerEventReader::find_owner()
{
    if (!ok())
        return nullptr;
    const auto present = get(1U);
    if (!ok() || present == 0U)
        return nullptr;
    void* const found = domain_.empty() ? nullptr : owners_->find(domain_);
    if (found == nullptr)
        fail("owner domain '" + std::string(domain_) + "' is not bound");
    return found;
}

void SchedulerEventReader::fail(std::string message)
{
    if (error_.empty())
        error_ = message.empty() ? std::string("event decode failed")
                                 : std::move(message);
}

// --------------------------------------------------------------- registry

namespace {

std::vector<std::unique_ptr<SchedulerEventKind>>& kinds()
{
    // Built on first use: registrations run from static initializers of many
    // translation units.
    static std::vector<std::unique_ptr<SchedulerEventKind>> registry;
    return registry;
}

} // namespace

void add_scheduler_event_kind(SchedulerEventKind kind)
{
    auto& registry = kinds();
    for (const auto& existing : registry) {
        if (existing->name == kind.name || existing->invoke == kind.invoke) {
            std::fprintf(stderr,
                "fsim: scheduler event kind '%s' duplicates '%s'\n",
                kind.name.c_str(), existing->name.c_str());
            std::abort();
        }
    }
    if (kind.name.empty() || kind.invoke == nullptr || kind.encode == nullptr
        || kind.decode == nullptr) {
        std::fprintf(stderr, "fsim: incomplete scheduler event kind '%s'\n",
            kind.name.c_str());
        std::abort();
    }
    registry.push_back(std::make_unique<SchedulerEventKind>(std::move(kind)));
}

const SchedulerEventKind* find_scheduler_event_kind(
    const std::string_view name) noexcept
{
    for (const auto& kind : kinds()) {
        if (kind->name == name)
            return kind.get();
    }
    return nullptr;
}

const SchedulerEventKind* find_scheduler_event_kind(
    const detail::SchedulerTaskDescriptor::Invoke invoke) noexcept
{
    if (invoke == nullptr)
        return nullptr;
    for (const auto& kind : kinds()) {
        if (kind->invoke == invoke)
            return kind.get();
    }
    return nullptr;
}

std::vector<const SchedulerEventKind*> scheduler_event_kinds()
{
    std::vector<const SchedulerEventKind*> result;
    for (const auto& kind : kinds())
        result.push_back(kind.get());
    return result;
}

void register_derived_scheduler_event_kind(const std::string_view name,
    const detail::SchedulerTaskDescriptor::Invoke invoke)
{
    // Two dispatch thunks the linker folded into one are one kind: the first
    // name stays.
    if (find_scheduler_event_kind(invoke) != nullptr)
        return;
    SchedulerEventKind kind;
    kind.name = std::string(name);
    kind.klass = SchedulerEventClass::derived;
    kind.invoke = invoke;
    // A derived event is never written or read, so its codec does nothing.
    kind.encode = +[](const std::byte*, SchedulerEventWriter&) { return true; };
    kind.decode = +[](SchedulerEventReader& reader, std::byte*) {
        reader.fail("a derived event kind has no saved payload");
        return false;
    };
    add_scheduler_event_kind(std::move(kind));
}

// ------------------------------------------------------------ save point

bool Scheduler::at_quiescent() const noexcept
{
    const auto& impl = *impl_;
    if (impl.in_runtime_slot_quiet_point)
        return true;
    if (impl.at_cycle_end()) {
        // Pending SystemVerilog work would resume in a different order than it
        // was saved in: a mixed design saves at the slot end instead.
        return impl.current->systemverilog.empty()
            && impl.current->systemverilog_running.empty();
    }
    return !impl.in_run && !impl.in_callback && !impl.current;
}

void Scheduler::request_quiet_point(const SchedulerQuietPoint point,
    void* const context, const SchedulerQuietPointCallback callback)
{
    if (callback == nullptr) {
        throw std::invalid_argument("quiet-point request needs a callback");
    }
    if (point == SchedulerQuietPoint::slot_end) {
        impl_->quiet_request_context = context;
        impl_->quiet_request = callback;
        return;
    }
    if (impl_->cycle_end_request_token != 0U) {
        remove_safe_point_hook(impl_->cycle_end_request_token);
        impl_->cycle_end_request_token = 0U;
    }
    Impl* const impl = impl_.get();
    // A one-shot observer: the end of a simulation cycle is the safe point
    // after the last phase of the delta (IEEE 1076-2019 20.7).
    impl_->cycle_end_request_token = add_safe_point_hook(
        [impl, context, callback](Scheduler& scheduler, SchedulerPhase) {
            if (!impl->at_cycle_end())
                return;
            const auto token = std::exchange(impl->cycle_end_request_token, 0U);
            scheduler.remove_safe_point_hook(token);
            callback(context, scheduler);
        });
}

// ------------------------------------------------------------------- save

namespace {

struct SaveCollector {
    const SchedulerEventOwners& owners;
    SchedulerSaveResult& result;
    SimulationTick now { };
    bool all_deferrable { true };

    void refuse(std::string message, const bool slot_local)
    {
        result.refusals.push_back(std::move(message));
        all_deferrable = all_deferrable && slot_local;
    }

    [[nodiscard]] static std::string where(const SimulationTick time,
        const SchedulerSavedRegion region, const SchedulerPhase phase,
        const Entry& entry)
    {
        std::ostringstream text;
        text << "event at time " << time << ' '
             << (region == SchedulerSavedRegion::generic ? "generic"
                     : region == SchedulerSavedRegion::systemverilog
                     ? "systemverilog" : "end-of-slot")
             << ' ' << phase_name(phase) << " order " << entry.order
             << " sequence " << entry.sequence;
        return text.str();
    }

    // What an entry holds: a registered-event descriptor, or a closure.
    struct Held {
        const detail::SchedulerTaskDescriptor* descriptor { };
        const Scheduler::Task* task { };
    };

    [[nodiscard]] static Held held_by(const Entry& entry)
    {
        Held held;
        if (const auto* const direct
            = std::get_if<detail::SchedulerTaskDescriptor>(&entry.task)) {
            held.descriptor = direct;
            return held;
        }
        if (const auto* const batch = std::get_if<BatchEntry>(&entry.task)) {
            // A batch member stands for its fallback: the batch only fuses
            // several of them into one call, so a restart re-creates the
            // plain event.
            if (batch->fallback_descriptor.invoke != nullptr)
                held.descriptor = &batch->fallback_descriptor;
            else if (batch->fallback)
                held.task = &batch->fallback;
            return held;
        }
        const Scheduler::Task* task = nullptr;
        if (entry.cancel_slots != nullptr) {
            task = entry.cancel_slots->peek(entry.cancellation);
        } else {
            task = std::get_if<Scheduler::Task>(&entry.task);
        }
        if (task != nullptr) {
            held.descriptor = task->target<detail::SchedulerTaskDescriptor>();
            if (held.descriptor == nullptr)
                held.task = task;
        }
        return held;
    }

    void visit(const SimulationTick time, const SchedulerSavedRegion region,
        const SchedulerPhase phase, const Entry& entry)
    {
        if (entry.is_cancelled())
            return;
        const auto site = [&] { return where(time, region, phase, entry); };
        // Work of the time slot being run ends before the slot does.
        const bool slot_local = time == now;
        const auto held = held_by(entry);
        if (held.descriptor == nullptr) {
            if (held.task != nullptr) {
                if (const auto* const host
                    = held.task->target<SchedulerHostClosure>()) {
                    refuse("FSIM-SAVE-0004: " + site()
                            + " is an embedder closure '" + host->what
                            + "' (host-closure)",
                        slot_local);
                    return;
                }
                if (held.task->target<SchedulerForeignCallback>() != nullptr) {
                    ++result.foreign_dropped;
                    return;
                }
                refuse("FSIM-SAVE-0002: " + site()
                        + " is an unregistered closure",
                    slot_local);
            } else {
                refuse("FSIM-SAVE-0003: " + site()
                        + " is a batch or ticket entry",
                    slot_local);
            }
            return;
        }
        const auto* const descriptor = held.descriptor;
        const auto* const kind
            = find_scheduler_event_kind(descriptor->invoke);
        if (kind == nullptr) {
            refuse("FSIM-SAVE-0003: " + site()
                    + " has an unregistered event kind",
                slot_local);
            return;
        }
        switch (kind->klass) {
        case SchedulerEventClass::derived:
            ++result.derived;
            return;
        case SchedulerEventClass::foreign_callback:
            ++result.foreign_dropped;
            return;
        case SchedulerEventClass::host_closure:
            refuse("FSIM-SAVE-0004: " + site() + " is of host-closure kind '"
                    + kind->name + "'",
                slot_local);
            return;
        case SchedulerEventClass::data:
            break;
        }
        SchedulerSavedEvent saved;
        saved.time = time;
        saved.region = region;
        saved.phase = phase;
        saved.order = entry.order;
        saved.sequence = entry.sequence;
        saved.kind = kind->name;
        if (entry.cancel_slots != nullptr) {
            if (kind->cancel_key == nullptr
                || !kind->cancel_key(
                    descriptor->payload.data(), saved.cancel_key)) {
                refuse("FSIM-SAVE-0006: " + site() + " of kind '" + kind->name
                        + "' is cancelable but has no cancel key",
                    slot_local);
                return;
            }
            saved.cancelable = true;
        }
        SchedulerEventWriter writer { saved.payload, owners,
            kind->owner_domain };
        if (!kind->encode(descriptor->payload.data(), writer)) {
            refuse("FSIM-SAVE-0005: " + site() + " of kind '" + kind->name
                    + "' cannot be saved: " + writer.error(),
                slot_local);
            return;
        }
        result.state.events.push_back(std::move(saved));
    }

    void queue(const WorkQueue& queue, const SimulationTick time,
        const SchedulerSavedRegion region, const SchedulerPhase phase)
    {
        // A queue whose batch is mid-dispatch has taken members out that the
        // save cannot see (#185); the quiet point is outside any dispatch.
        FSIM_RS_IDLE("sched.work_queue", dispatching_ticket);
        FSIM_RS_IDLE("sched.work_queue", dispatching_ticket_count);
        if (queue.dispatching_ticket != nullptr
            || queue.dispatching_ticket_count != 0U) {
            refuse("FSIM-SAVE-0011: the scheduler's batch dispatch of the "
                   "queue at time " + std::to_string(time)
                    + " is not at rest at the save point",
                true);
        }
        for (auto index = queue.cursor; index < queue.entries.size(); ++index)
            visit(time, region, phase, queue.entries[index]);
        for (const auto& entry : queue.current_insertions)
            visit(time, region, phase, entry);
    }

    void bucket(const Bucket& bucket, const SimulationTick time,
        const SchedulerSavedRegion region)
    {
        for (std::size_t phase = 0U; phase < phase_count; ++phase) {
            queue(bucket.queues[phase], time, region,
                static_cast<SchedulerPhase>(phase));
        }
    }
};

} // namespace

void Scheduler::Impl::audit_rest_state(std::vector<std::string>& messages) const
{
    // The members of the table's class S rows that are not part of an event
    // (#185). A save at the end of a time slot, at the end of a simulation
    // cycle or while the scheduler is idle (at_quiescent) finds each of them
    // at rest; one that is not names the work the file would lose.
    const auto busy = [&](const bool condition, const char* what) {
        if (condition) {
            messages.push_back(std::string { "FSIM-SAVE-0011: the scheduler's " }
                + what + " is not at rest at the save point");
        }
    };
    FSIM_RS_IDLE("sched.impl", discarding);
    busy(discarding, "discard of pending work");
    // The run flags follow the save point's kind: a slot-end request runs in
    // a callback of the run, a cycle-end save in a safe point of the run, an
    // idle save outside both.
    FSIM_RS_IDLE("sched.impl", in_run);
    FSIM_RS_IDLE("sched.impl", in_callback);
    FSIM_RS_IDLE("sched.impl", in_safe_point);
    FSIM_RS_IDLE("sched.impl", in_runtime_slot_quiet_point);
    if (in_runtime_slot_quiet_point) {
        busy(!in_run || !in_callback, "run flags at a slot-end save point");
    } else if (at_cycle_end()) {
        busy(!in_run, "run flags at a cycle-end save point");
    } else {
        busy(in_run || in_callback || in_safe_point,
            "run flags outside a run");
    }
    // The slot being run is consumed up to its last phase, with no
    // SystemVerilog region executing.
    FSIM_RS_IDLE("sched.current_slot", phase);
    FSIM_RS_IDLE("sched.current_slot", systemverilog_phase);
    busy(current && (current->phase != phase_count
                        || current->systemverilog_phase.has_value()),
        "slot position");
    // A SystemVerilog group-batch reservation lasts one batch.
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_reservation_id);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_queue);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_ticket);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_task);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_ordered_task);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_count);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_previous_count);
    FSIM_RS_IDLE("sched.impl", active_systemverilog_group_batch_new_ticket);
    busy(active_systemverilog_group_batch_reservation_id != 0U
            || active_systemverilog_group_batch_queue != nullptr
            || active_systemverilog_group_batch_ticket != nullptr
            || active_systemverilog_group_batch_task != nullptr
            || active_systemverilog_group_batch_ordered_task != nullptr
            || active_systemverilog_group_batch_count != 0U
            || active_systemverilog_group_batch_previous_count != 0U
            || active_systemverilog_group_batch_new_ticket,
        "group-batch reservation");
    FSIM_RS_IDLE("sched.impl", batch_entries);
    FSIM_RS_IDLE("sched.impl", batch_payloads);
    busy(!batch_entries.empty() || !batch_payloads.empty(),
        "batch dispatch lists");
    FSIM_RS_IDLE("sched.impl", systemverilog_batch_tickets);
    FSIM_RS_IDLE("sched.impl", systemverilog_batch_ticket_members);
    busy(!systemverilog_batch_tickets.empty()
            || !systemverilog_batch_ticket_members.empty(),
        "SystemVerilog batch tickets");
    // A readiness ticket below the reach is live while it still holds
    // members (an exhausted one is recycled lazily).
    const auto live = [](const BatchTicket& ticket) {
        return ticket.active && ticket.cursor < ticket.count;
    };
    FSIM_RS_IDLE("sched.impl", systemverilog_readiness_ticket_reach);
    bool live_ticket = false;
    for (std::size_t index = 0U; index < systemverilog_readiness_ticket_reach
         && index < systemverilog_readiness_ticket_capacity(); ++index) {
        live_ticket = live_ticket
            || live(index < default_readiness_ticket_count
                    ? systemverilog_readiness_tickets[index]
                    : systemverilog_readiness_ticket_overflow[
                          index - default_readiness_ticket_count]);
    }
    busy(live_ticket, "SystemVerilog readiness tickets");
    FSIM_RS_IDLE("sched.impl", generic_readiness_ticket_reach);
    live_ticket = false;
    for (std::size_t index = 0U; index < generic_readiness_ticket_reach
         && index < generic_readiness_ticket_capacity(); ++index) {
        live_ticket = live_ticket
            || live(index < default_readiness_ticket_count
                    ? generic_readiness_tickets[index]
                    : generic_readiness_ticket_overflow[
                          index - default_readiness_ticket_count]);
    }
    busy(live_ticket, "generic readiness tickets");
}

SchedulerSaveResult Scheduler::save_future(
    const SchedulerEventOwners& owners) const
{
    SchedulerSaveResult result;
    if (!at_quiescent()) {
        result.refusals.push_back(
            "FSIM-SAVE-0001: the scheduler is not at a save point: save at "
            "the end of the time slot or of the simulation cycle");
        result.deferrable = true;
        return result;
    }
    const auto& impl = *impl_;
    impl.audit_rest_state(result.refusals);
    result.state.now = impl.now;
    result.state.cycles = impl.cycles;
    result.state.callbacks = impl.callbacks;
    result.state.next_sequence = impl.next_sequence;
    result.state.delta = impl.current ? impl.current->delta : 0U;

    SaveCollector collector { owners, result, impl.now };
    for (const auto& [time, slot] : impl.future) {
        collector.bucket(slot.generic, time, SchedulerSavedRegion::generic);
        collector.bucket(
            slot.systemverilog, time, SchedulerSavedRegion::systemverilog);
        collector.queue(slot.end_of_slot, time,
            SchedulerSavedRegion::end_of_slot, SchedulerPhase::postponed);
    }
    if (impl.current) {
        // A cycle-end save: what the cycle left for the next delta, and the
        // work waiting for the end of the slot.
        const auto time = impl.current->time;
        collector.bucket(
            impl.current->current, time, SchedulerSavedRegion::generic);
        collector.bucket(
            impl.current->next_delta, time, SchedulerSavedRegion::generic);
        collector.bucket(impl.current->systemverilog, time,
            SchedulerSavedRegion::systemverilog);
        collector.queue(impl.current->end_of_slot, time,
            SchedulerSavedRegion::end_of_slot, SchedulerPhase::postponed);
    }
    std::sort(result.state.events.begin(), result.state.events.end(),
        [](const SchedulerSavedEvent& left, const SchedulerSavedEvent& right) {
            return std::tuple { left.time, left.region, left.phase, left.order,
                       left.sequence }
                < std::tuple { right.time, right.region, right.phase,
                      right.order, right.sequence };
        });
    // The owner re-creates each derived event on restore and that takes a
    // fresh sequence, so the saved counter leaves their sequences out: the
    // restored run then records the same counter it was saved with.
    result.state.next_sequence -= std::min<std::uint64_t>(
        static_cast<std::uint64_t>(result.derived),
        result.state.next_sequence);
    result.ok = result.refusals.empty();
    if (!result.ok) {
        result.state.events.clear();
        result.deferrable = collector.all_deferrable;
    }
    return result;
}

// ---------------------------------------------------------------- restore

SchedulerRestoreResult Scheduler::restore_future(
    const SchedulerSavedState& state, const SchedulerEventOwners& owners,
    const SchedulerCancelBinder& bind)
{
    SchedulerRestoreResult result;
    auto& impl = *impl_;
    if (impl.in_run || impl.in_callback || impl.discarding) {
        result.errors.push_back("cannot restore while the scheduler runs");
        return result;
    }
    struct Built {
        const SchedulerSavedEvent* saved { };
        const SchedulerEventKind* kind { };
        detail::SchedulerTaskDescriptor descriptor;
    };
    std::vector<Built> built;
    built.reserve(state.events.size());
    std::uint64_t highest_sequence { };
    bool any_event { };
    for (const auto& saved : state.events) {
        const auto* const kind = find_scheduler_event_kind(saved.kind);
        if (kind == nullptr) {
            result.errors.push_back(
                "saved event kind '" + saved.kind + "' is not registered");
            continue;
        }
        if (kind->klass != SchedulerEventClass::data) {
            result.errors.push_back("saved event kind '" + saved.kind
                + "' is of class " + scheduler_event_class_name(kind->klass)
                + ", which a save never records");
            continue;
        }
        if (saved.time < state.now) {
            result.errors.push_back(
                "saved event of kind '" + saved.kind + "' is in the past");
            continue;
        }
        if (saved.cancelable && kind->cancel_key == nullptr) {
            result.errors.push_back("saved event of kind '" + saved.kind
                + "' is cancelable but the kind has no cancel key");
            continue;
        }
        SchedulerEventReader reader { saved.payload, owners,
            kind->owner_domain };
        Built entry;
        entry.saved = &saved;
        entry.kind = kind;
        entry.descriptor.invoke = kind->invoke;
        if (!kind->decode(reader, entry.descriptor.payload.data())
            || !reader.exhausted()) {
            result.errors.push_back("saved event of kind '" + saved.kind
                + "' cannot be restored: "
                + (reader.ok() ? std::string("trailing payload bytes")
                               : reader.error()));
            continue;
        }
        any_event = true;
        highest_sequence = std::max(highest_sequence, saved.sequence);
        built.push_back(std::move(entry));
    }
    if (!result.errors.empty())
        return result;

    if (impl.current || has_pending())
        discard_pending();
    impl.now = state.now;
    impl.cycles = state.cycles;
    impl.callbacks = state.callbacks;
    impl.next_sequence = state.next_sequence;
    if (any_event && impl.next_sequence <= highest_sequence)
        impl.next_sequence = highest_sequence + 1U;

    std::vector<std::pair<const Built*, ScheduledTaskHandle>> handles;
    for (const auto& item : built) {
        const auto& saved = *item.saved;
        auto& slot = impl.future_slot_at(saved.time);
        WorkQueue* queue = nullptr;
        switch (saved.region) {
        case SchedulerSavedRegion::generic:
            queue = &slot.generic.queues[phase_index(saved.phase)];
            break;
        case SchedulerSavedRegion::systemverilog:
            queue = &slot.systemverilog.queues[phase_index(saved.phase)];
            break;
        case SchedulerSavedRegion::end_of_slot:
            queue = &slot.end_of_slot;
            break;
        }
        Entry entry { saved.order, saved.sequence,
            EntryTask { item.descriptor }, nullptr, { } };
        if (saved.cancelable) {
            // The handle is rebuilt with the same descriptor in a cancel
            // slot, exactly as a cancelable schedule call would hold it.
            const auto cancellation = impl.cancel_slots->acquire(
                Scheduler::Task { item.descriptor });
            entry = Entry { saved.order, saved.sequence,
                EntryTask { std::monostate { } }, impl.cancel_slots.get(),
                cancellation };
            handles.emplace_back(&item,
                ScheduledTaskHandle { impl.cancel_slots, cancellation.slot,
                    cancellation.generation });
        }
        impl.push_entry(*queue, std::move(entry));
        ++result.restored;
    }
    if (bind) {
        for (const auto& [item, handle] : handles) {
            bind(item->saved->kind, item->saved->cancel_key, handle);
        }
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------- serialization

namespace {

constexpr std::uint32_t state_magic = 0x48435346U; // "FSCH"
constexpr std::uint32_t state_version = 1U;

void put64(std::vector<std::byte>& out, const std::uint64_t value)
{
    for (unsigned index = 0U; index < 8U; ++index) {
        out.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>(value >> (8U * index))));
    }
}

void put32(std::vector<std::byte>& out, const std::uint32_t value)
{
    for (unsigned index = 0U; index < 4U; ++index) {
        out.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>(value >> (8U * index))));
    }
}

class ByteCursor {
public:
    explicit ByteCursor(const std::span<const std::byte> bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] bool take(const std::size_t count) noexcept
    {
        if (bytes_.size() - position_ < count) {
            failed_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::uint64_t u64(const unsigned width)
    {
        if (failed_ || !take(width))
            return 0U;
        std::uint64_t value { };
        for (unsigned index = 0U; index < width; ++index) {
            value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(
                         bytes_[position_ + index])) << (8U * index);
        }
        position_ += width;
        return value;
    }

    [[nodiscard]] std::span<const std::byte> block(const std::size_t count)
    {
        if (failed_ || !take(count))
            return { };
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool done() const noexcept
    {
        return position_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ { };
    bool failed_ { };
};

} // namespace

std::vector<std::byte> serialize_scheduler_state(
    const SchedulerSavedState& state)
{
    std::vector<std::byte> out;
    put32(out, state_magic);
    put32(out, state_version);
    put64(out, state.now);
    put64(out, state.cycles);
    put64(out, state.callbacks);
    put64(out, state.next_sequence);
    put64(out, state.delta);
    put64(out, state.events.size());
    for (const auto& event : state.events) {
        put64(out, event.time);
        out.push_back(static_cast<std::byte>(event.region));
        out.push_back(static_cast<std::byte>(event.phase));
        put64(out, event.order);
        put64(out, event.sequence);
        put32(out, static_cast<std::uint32_t>(event.kind.size()));
        for (const char letter : event.kind)
            out.push_back(static_cast<std::byte>(letter));
        out.push_back(static_cast<std::byte>(event.cancelable ? 1U : 0U));
        put64(out, event.cancel_key);
        put32(out, static_cast<std::uint32_t>(event.payload.size()));
        out.insert(out.end(), event.payload.begin(), event.payload.end());
    }
    return out;
}

bool deserialize_scheduler_state(const std::span<const std::byte> bytes,
    SchedulerSavedState& state, std::string& error)
{
    ByteCursor cursor { bytes };
    state = { };
    if (cursor.u64(4U) != state_magic) {
        error = "not a scheduler state section";
        return false;
    }
    if (cursor.u64(4U) != state_version) {
        error = "unsupported scheduler state version";
        return false;
    }
    state.now = cursor.u64(8U);
    state.cycles = cursor.u64(8U);
    state.callbacks = cursor.u64(8U);
    state.next_sequence = cursor.u64(8U);
    state.delta = cursor.u64(8U);
    const auto count = cursor.u64(8U);
    // Each event needs at least its fixed fields: bound the allocation by the
    // input size so a corrupt count cannot exhaust memory.
    if (cursor.failed() || count > bytes.size()) {
        error = "scheduler state section is truncated";
        return false;
    }
    state.events.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        SchedulerSavedEvent event;
        event.time = cursor.u64(8U);
        const auto region = cursor.u64(1U);
        const auto phase = cursor.u64(1U);
        event.order = cursor.u64(8U);
        event.sequence = cursor.u64(8U);
        const auto kind_size = cursor.u64(4U);
        if (cursor.failed() || region > 2U || phase >= phase_count) {
            error = "scheduler state section is malformed";
            return false;
        }
        event.region = static_cast<SchedulerSavedRegion>(region);
        event.phase = static_cast<SchedulerPhase>(phase);
        const auto kind = cursor.block(static_cast<std::size_t>(kind_size));
        event.kind.assign(
            reinterpret_cast<const char*>(kind.data()), kind.size());
        event.cancelable = cursor.u64(1U) != 0U;
        event.cancel_key = cursor.u64(8U);
        const auto payload_size = cursor.u64(4U);
        const auto payload
            = cursor.block(static_cast<std::size_t>(payload_size));
        event.payload.assign(payload.begin(), payload.end());
        if (cursor.failed()) {
            error = "scheduler state section is truncated";
            return false;
        }
        state.events.push_back(std::move(event));
    }
    if (!cursor.done()) {
        error = "scheduler state section has trailing bytes";
        return false;
    }
    return true;
}

} // namespace fsim::runtime
