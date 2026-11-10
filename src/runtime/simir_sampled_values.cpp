// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <map>
#include <span>
#include <stdexcept>
#include <string>

namespace fsim::runtime::simir {

namespace {

constexpr std::size_t maximum_retained_samples { 4097U };

[[nodiscard]] std::size_t sampled_history_capacity(
    const ReadSignal& operation)
{
    if (operation.kind == SignalReadKind::past) {
        if (operation.ticks >= maximum_retained_samples - 1U) {
            return maximum_retained_samples;
        }
        return static_cast<std::size_t>(operation.ticks) + 1U;
    }
    if (operation.kind == SignalReadKind::rose
        || operation.kind == SignalReadKind::fell
        || operation.kind == SignalReadKind::stable
        || operation.kind == SignalReadKind::changed) {
        return 2U;
    }
    return 1U;
}

[[nodiscard]] bool sampled_history_kind(const SignalReadKind kind)
{
    return kind == SignalReadKind::sampled
        || kind == SignalReadKind::rose
        || kind == SignalReadKind::fell
        || kind == SignalReadKind::stable
        || kind == SignalReadKind::changed
        || kind == SignalReadKind::past;
}

} // namespace

std::size_t Interpreter::Impl::sampled_ring_size(
    const std::size_t width, const bool logic9, const std::size_t capacity)
{
    const auto planes = logic9 ? std::size_t { 4U } : std::size_t { 2U };
    return SampledRing::header_words
        + capacity * planes * ((width + 63U) / 64U);
}

void Interpreter::Impl::init_sampled_ring(std::uint64_t* const ring,
    const PackedLogic4& shape, const std::size_t capacity)
{
    const auto planes = shape.is_logic9() ? std::size_t { 4U } : std::size_t { 2U };
    std::fill_n(ring, sampled_ring_size(shape.width(), shape.is_logic9(), capacity), 0U);
    ring[SampledRing::capacity] = capacity;
    ring[SampledRing::value_words] = planes * ((shape.width() + 63U) / 64U);
    ring[SampledRing::width] = shape.width();
    ring[SampledRing::shape] = shape.is_logic9() ? 1U : 0U;
}

PackedLogic4 Interpreter::Impl::sampled_ring_value(
    const std::uint64_t* const ring, const std::size_t index)
{
    if (index >= ring[SampledRing::count]) {
        throw std::logic_error { "sampled history index is out of range" };
    }
    const auto width = static_cast<std::size_t>(ring[SampledRing::width]);
    const auto words = (width + 63U) / 64U;
    const auto slot = (ring[SampledRing::first] + index)
        % ring[SampledRing::capacity];
    const auto* const value = ring + SampledRing::header_words
        + slot * ring[SampledRing::value_words];
    if (ring[SampledRing::shape] == 0U) {
        return PackedLogic4::from_word_planes(width, { value, words },
            { value + words, words });
    }
    return PackedLogic4::from_logic9_word_planes(width, { value, words },
        { value + words, words }, { value + 2U * words, words },
        { value + 3U * words, words });
}

Interpreter::Impl::SampledHistorySlot
Interpreter::Impl::make_sampled_history_slot(
    const SimulationTick time,
    const ProcessSchedulingDomain process_domain,
    const SignalChangeOrigin origin,
    const std::uint64_t generic_delta)
{
    auto slot = SampledHistorySlot {
        time,
        process_domain,
        process_domain,
        0U
    };
    if (process_domain != ProcessSchedulingDomain::systemverilog) {
        slot.event_domain = origin.process_domain;
        slot.generic_delta = generic_delta;
    }
    return slot;
}

bool Interpreter::Impl::same_sampled_history_slot(
    const std::uint64_t* const ring, const SampledHistorySlot& current)
{
    if (ring[SampledRing::slot_valid] == 0U) {
        return false;
    }
    const auto same_time = ring[SampledRing::slot_time] == current.time
        && ring[SampledRing::slot_process_domain]
            == static_cast<std::uint64_t>(current.process_domain);
    if (current.process_domain == ProcessSchedulingDomain::systemverilog) {
        return same_time;
    }
    return same_time
        && ring[SampledRing::slot_event_domain]
            == static_cast<std::uint64_t>(current.event_domain)
        && ring[SampledRing::slot_generic_delta] == current.generic_delta;
}

void Interpreter::Impl::append_sampled_ring(std::uint64_t* const ring,
    const SampledHistorySlot& slot, const PackedLogic4& value)
{
    const auto capacity = ring[SampledRing::capacity];
    if (capacity == 0U) {
        throw std::logic_error {
            "sampled history storage was not prepared before publication"
        };
    }
    const bool logic9 = ring[SampledRing::shape] != 0U;
    if (value.width() != ring[SampledRing::width]
        || value.is_logic9() != logic9) {
        throw std::logic_error {
            "sampled history changed value shape after preallocation"
        };
    }
    std::uint64_t next { };
    if (ring[SampledRing::count] < capacity) {
        next = (ring[SampledRing::first] + ring[SampledRing::count]) % capacity;
        ++ring[SampledRing::count];
    } else {
        next = ring[SampledRing::first];
        ring[SampledRing::first] = (next + 1U) % capacity;
    }
    const auto words = static_cast<std::size_t>(
        (ring[SampledRing::width] + 63U) / 64U);
    auto* const destination = ring + SampledRing::header_words
        + next * ring[SampledRing::value_words];
    const auto copy_plane = [&](const std::span<const std::uint64_t> plane,
                                const std::size_t index) {
        auto* const out = destination + index * words;
        for (std::size_t word = 0; word < words; ++word) {
            out[word] = word < plane.size() ? plane[word] : 0U;
        }
    };
    if (!logic9) {
        copy_plane(value.aval_words(), 0U);
        copy_plane(value.bval_words(), 1U);
    } else {
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            copy_plane(value.logic9_plane_words(plane), plane);
        }
    }
    ring[SampledRing::slot_valid] = 1U;
    ring[SampledRing::slot_time] = slot.time;
    ring[SampledRing::slot_process_domain]
        = static_cast<std::uint64_t>(slot.process_domain);
    ring[SampledRing::slot_event_domain]
        = static_cast<std::uint64_t>(slot.event_domain);
    ring[SampledRing::slot_generic_delta] = slot.generic_delta;
}

std::uint64_t Interpreter::Impl::ensure_sampled_ring(
    const SampledHistoryKey& key, const std::size_t capacity,
    const PackedLogic4& shape)
{
    const auto found = sampled_ring_offsets.find(key);
    if (found != sampled_ring_offsets.end()
        && sampled_ring_words[found->second + SampledRing::capacity]
            >= capacity) {
        return found->second;
    }
    // A new key, or a deeper history than the plan scan saw: a block at the
    // end takes the values (allocation only; reads never reach here once the
    // scan has seen the read). Once a compiled kernel has baked the ring
    // starts, a block added during the run would leave it reading stale
    // words, so a scan gap is an error, not a silent move.
    if (sampled_ring_layout_baked && !sampled_ring_layout_building) {
        throw std::logic_error {
            "sampled-value ring for signal " + std::to_string(key.signal)
            + " is missing from the ring plan the compiled kernel uses"
        };
    }
    const auto offset = sampled_ring_words.size();
    sampled_ring_words.resize(
        offset + sampled_ring_size(shape.width(), shape.is_logic9(), capacity));
    init_sampled_ring(sampled_ring_words.data() + offset, shape, capacity);
    if (found != sampled_ring_offsets.end()) {
        const auto* const old = sampled_ring_words.data() + found->second;
        auto* const fresh = sampled_ring_words.data() + offset;
        for (std::size_t index = 0; index < old[SampledRing::count]; ++index) {
            const auto value = sampled_ring_value(old, index);
            SampledHistorySlot slot { };
            append_sampled_ring(fresh, slot, value);
        }
        fresh[SampledRing::slot_valid] = old[SampledRing::slot_valid];
        fresh[SampledRing::slot_time] = old[SampledRing::slot_time];
        fresh[SampledRing::slot_process_domain]
            = old[SampledRing::slot_process_domain];
        fresh[SampledRing::slot_event_domain]
            = old[SampledRing::slot_event_domain];
        fresh[SampledRing::slot_generic_delta]
            = old[SampledRing::slot_generic_delta];
        found->second = offset;
        // The clock index names rings by start.
        for (auto& rings : sampled_history_keys_by_clock) {
            for (auto& entry : rings) {
                if (entry.key == key) {
                    entry.ring = offset;
                }
            }
        }
        return offset;
    }
    sampled_ring_offsets.emplace(key, offset);
    return offset;
}

std::vector<Interpreter::Impl::SampledRingPlanEntry>
Interpreter::Impl::sampled_ring_plan() const
{
    // The deepest history each key needs, in the order the programs name
    // the keys, then each ring's start: the layout is a function of the
    // design alone, so the compiled kernel bakes a ring's start into its
    // instructions (KOp::sva_sampled_load) before the rings exist.
    std::map<SampledHistoryKey, std::size_t> capacities;
    std::vector<SampledHistoryKey> order;
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (processes.is_compact_constant(id)) {
            // Every compact constant shape is read-free; startup-write bank
            // rows therefore contribute no sampled clock history.
            continue;
        }
        const auto program = processes.program_view(id);
        // Scan the full instruction array. Fork branches refer to PCs in this
        // same program, so their statically present reads are indexed too.
        // Every domain gets its rings here, so a kernel member never
        // allocates one while it runs; only the SystemVerilog keys are
        // captured at clock publication.
        for (std::size_t instruction = 0U;
             instruction < program.operations().size();
             ++instruction) {
            const auto expanded = program.operations().expanded(instruction);
            const auto* read = operation_get_if<ReadSignal>(&expanded);
            // An unclocked history read (no clock in scope) keeps a ring
            // too: it samples on every evaluation, and a ring added while
            // the run goes on would move the rings under the kernel.
            if (read == nullptr
                || !sampled_history_kind(read->kind)
                || (read->kind == SignalReadKind::sampled && !read->clock)
                || read->signal >= signals.size()
                || (read->clock && *read->clock >= signals.size())
                || (read->gate && *read->gate >= signals.size())) {
                continue;
            }
            const SampledHistoryKey key {
                read->signal,
                read->clock,
                read->clock_edge,
                read->gate,
                program.scheduling_domain()
            };
            const auto [entry, inserted] = capacities.try_emplace(key, 0U);
            entry->second = std::max(entry->second, sampled_history_capacity(*read));
            if (inserted) {
                order.push_back(key);
            }
        }
    }
    std::vector<SampledRingPlanEntry> plan;
    plan.reserve(order.size());
    std::uint64_t offset = 0U;
    for (const auto& key : order) {
        // The ring's shape is the signal's: its width and value kind never
        // change (sampled_defaults takes the value at start).
        const auto& shape = get_signal(key.signal).initial_value;
        const auto capacity = capacities.at(key);
        plan.push_back({ key, capacity, offset });
        offset += sampled_ring_size(shape.width(), shape.is_logic9(), capacity);
    }
    return plan;
}

std::optional<std::uint64_t> Interpreter::Impl::planned_sampled_ring_offset(
    const SampledHistoryKey& key) const
{
    for (const auto& entry : sampled_ring_plan()) {
        if (entry.key == key) {
            return entry.offset;
        }
    }
    return std::nullopt;
}

void Interpreter::Impl::build_sampled_history_clock_index()
{
    sampled_ring_words.clear();
    sampled_ring_offsets.clear();
    sampled_history_keys_by_clock.clear();
    sampled_ring_layout_building = true;
    for (const auto& entry : sampled_ring_plan()) {
        const auto& key = entry.key;
        if (key.signal >= sampled_defaults.size()) {
            continue;
        }
        const auto offset = ensure_sampled_ring(
            key, entry.capacity, sampled_defaults[key.signal]);
        if (offset != entry.offset) {
            throw std::logic_error {
                "sampled-value ring layout differs from its plan"
            };
        }
        if (key.clock
            && key.process_domain == ProcessSchedulingDomain::systemverilog) {
            if (sampled_history_keys_by_clock.empty()) {
                sampled_history_keys_by_clock.resize(signals.size());
            }
            sampled_history_keys_by_clock[*key.clock].push_back(
                { key, sampled_ring_offsets.at(key) });
        }
    }
    sampled_ring_layout_building = false;
}

void Interpreter::Impl::ensure_sampled_value_word_layout()
{
    // The layout is a function of the design (the masked signals in order,
    // two planes of ceil(width / 64) words each) and fixed once made: the
    // compiled kernel bakes the offsets into its instructions before the
    // values exist. Signals the mask gains later are appended.
    if (sampled_value_word_offsets.size() < signals.size()) {
        sampled_value_word_offsets.resize(signals.size(), no_sampled_value_words);
    }
    for (std::size_t signal = 0U; signal < signals.size(); ++signal) {
        if (sampled_value_word_offsets[signal] != no_sampled_value_words
            || (!sampled_value_dependencies_unknown
                && (signal >= sampled_value_dependency_mask.size()
                    || sampled_value_dependency_mask[signal] == 0U))) {
            continue;
        }
        const auto width = get_signal(static_cast<SignalId>(signal))
                               .initial_value.width();
        if (width == 0U) {
            continue;
        }
        const auto words = (width + 63U) / 64U;
        sampled_value_word_offsets[signal]
            = static_cast<std::uint32_t>(sampled_value_words.size());
        sampled_value_words.resize(sampled_value_words.size() + 2U * words, 0U);
    }
}

void Interpreter::Impl::refresh_sampled_value_words()
{
    ensure_sampled_value_word_layout();
    for (std::size_t signal = 0U; signal < signals.size(); ++signal) {
        store_sampled_value_words(static_cast<SignalId>(signal));
    }
}

void Interpreter::Impl::store_sampled_value_words(const SignalId signal) noexcept
{
    if (signal >= sampled_value_word_offsets.size()
        || sampled_value_word_offsets[signal] == no_sampled_value_words
        || signal >= sampled_values.size()) {
        return;
    }
    // Logic4 words: a Logic9 value reads as its Logic4 counterpart, as a
    // SystemVerilog read of it does.
    const auto value = coerce_value_kind(sampled_values[signal], ValueKind::logic4);
    const auto words = (value.width() + 63U) / 64U;
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    auto* const out = sampled_value_words.data() + sampled_value_word_offsets[signal];
    if (sampled_value_word_offsets[signal] + 2U * words > sampled_value_words.size()) {
        return;
    }
    for (std::size_t word = 0U; word < words; ++word) {
        out[word] = word < aval.size() ? aval[word] : 0U;
        out[words + word] = word < bval.size() ? bval[word] : 0U;
    }
}

void Interpreter::Impl::capture_sampled_history_clock(
    const SignalId clock,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    if (clock >= sampled_history_keys_by_clock.size()
        || sampled_history_keys_by_clock[clock].empty()
        || clock >= signal_last_values.size()
        || clock >= signals.size()) {
        return;
    }
    const auto& previous_clock = logical_signal_last_value(clock);
    const auto& current_clock = logical_signal_value(clock);
    if (previous_clock.width() == 0U || current_clock.width() == 0U) {
        return;
    }
    if (previous_clock == current_clock) {
        return;
    }
    for (const auto& entry : sampled_history_keys_by_clock[clock]) {
        const auto& key = entry.key;
        const auto edge_matches_clock = key.edge == SampledClockEdge::any
            ? true
            : edge_matches(
                key.edge == SampledClockEdge::positive
                    ? EdgeKind::posedge
                    : EdgeKind::negedge,
                previous_clock.get(0U),
                current_clock.get(0U));
        if (!edge_matches_clock
            || key.signal >= sampled_values.size()
            || key.signal >= sampled_defaults.size()
            || (key.gate
                && (*key.gate >= sampled_values.size()
                    || sampled_values[*key.gate].width() == 0U))) {
            continue;
        }
        if (key.gate
            && sampled_values[*key.gate].get(0U) != Logic4::one) {
            continue;
        }
        auto* const ring = sampled_ring_words.data() + entry.ring;
        const auto slot = make_sampled_history_slot(
            scheduler.now(), key.process_domain, origin, generic_delta);
        if (same_sampled_history_slot(ring, slot)) {
            continue;
        }
        append_sampled_ring(ring, slot, sampled_values[key.signal]);
    }
}

void Interpreter::Impl::run_sampled_read(
    AssertionStorage& storage,
    const ReadSignal& operation)
{
    (void)get_signal(operation.signal);
    if (operation.kind == SignalReadKind::current) {
        storage.fail("current signal read reached the sampled-read service");
    }
    const auto has_sampled_value = [this](const SignalId signal) {
        return signal < sampled_values.size()
            && signal < sampled_defaults.size()
            && (sampled_value_dependencies_unknown
                || (signal < sampled_value_dependency_mask.size()
                    && sampled_value_dependency_mask[signal] != 0U));
    };
    if (!has_sampled_value(operation.signal)) {
        storage.fail("sampled signal state is unavailable");
    }
    const auto& slot_value = sampled_values[operation.signal];
    // A SystemVerilog reader sees a std_logic value through the Logic4
    // mapping (IEEE 1800-2023 21.2.1.3): 'H' is 1, 'L' is 0, 'U', 'W' and '-'
    // are x. The history keeps the nine codes, which the other domains'
    // readers (PSL) compare; a SystemVerilog value change function compares
    // the values it reads (16.9.3: strength is not part of the value).
    const auto reader_value = [&](const PackedLogic4& value) {
        return value.is_logic9()
                && storage.domain() == ProcessSchedulingDomain::systemverilog
            ? coerce_value_kind(value, ValueKind::logic4)
            : value;
    };
    if (operation.kind == SignalReadKind::future
        || operation.kind == SignalReadKind::rising
        || operation.kind == SignalReadKind::falling
        || operation.kind == SignalReadKind::steady
        || operation.kind == SignalReadKind::changing) {
        const auto& future = logical_signal_value(operation.signal);
        if (operation.kind == SignalReadKind::future) {
            storage.write(operation.destination, reader_value(future));
            return;
        }
        // The comparing forms compare the values the reader reads, as the
        // value change functions do (16.9.4 defines them by $sampled and
        // $future_gclk): a SystemVerilog reader's through the Logic4 mapping.
        const auto future_read = reader_value(future);
        const auto sampled_value_read = reader_value(slot_value);
        const auto equal = future_read == sampled_value_read;
        const auto future_lsb = future_read.get(0U);
        const auto sampled_lsb = sampled_value_read.get(0U);
        const auto value = operation.kind == SignalReadKind::rising
            ? future_lsb == Logic4::one && sampled_lsb != Logic4::one
            : operation.kind == SignalReadKind::falling
            ? future_lsb == Logic4::zero && sampled_lsb != Logic4::zero
            : operation.kind == SignalReadKind::steady
            ? equal
            : !equal;
        storage.write(operation.destination,
            PackedLogic4(1U, value ? Logic4::one : Logic4::zero));
        return;
    }
    if (operation.kind == SignalReadKind::sampled && !operation.clock) {
        storage.write(operation.destination, reader_value(slot_value));
        return;
    }
    if (operation.ticks == 0U) {
        storage.fail("sampled history depth is zero");
    }
    if (operation.clock) {
        if (!has_sampled_value(*operation.clock)
            || *operation.clock >= signal_events.size()
            || *operation.clock >= signal_event_scheduling_stamps.size()) {
            storage.fail("sampled clock state is unavailable");
        }
        if (logical_signal_value(*operation.clock).width() == 0U
            || logical_signal_last_value(*operation.clock).width() == 0U) {
            storage.fail("sampled clock must have a nonzero width");
        }
    }
    if (operation.gate) {
        if (!has_sampled_value(*operation.gate)) {
            storage.fail("sampled gating state is unavailable");
        }
        if (sampled_values[*operation.gate].width() == 0U) {
            storage.fail("sampled gate must have a nonzero width");
        }
    }
    const SampledHistoryKey key {
        operation.signal,
        operation.clock,
        operation.clock_edge,
        operation.gate,
        storage.domain()
    };
    const auto ring_offset = ensure_sampled_ring(key,
        sampled_history_capacity(operation),
        sampled_defaults[operation.signal]);
    const auto process_domain = storage.domain();
    auto slot = make_sampled_history_slot(
        scheduler.now(), process_domain, SignalChangeOrigin { }, 0U);
    bool sample = !operation.clock;
    if (operation.clock) {
        const auto& event = signal_events[*operation.clock];
        const auto& stamp
            = signal_event_scheduling_stamps[*operation.clock];
        const bool systemverilog_history
            = process_domain == ProcessSchedulingDomain::systemverilog;
        const bool same_systemverilog_domain
            = systemverilog_history
            && stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog;
        sample = event && event->first == scheduler.now()
            && (same_systemverilog_domain
                    || event->second == scheduler.delta());
        slot = make_sampled_history_slot(
            scheduler.now(),
            process_domain,
            stamp.origin,
            event ? event->second : 0U);
        if (sample && operation.clock_edge != SampledClockEdge::any) {
            const auto edge = operation.clock_edge == SampledClockEdge::positive
                ? EdgeKind::posedge
                : EdgeKind::negedge;
            sample = edge_matches(
                edge,
                logical_signal_last_value(*operation.clock).get(0U),
                logical_signal_value(*operation.clock).get(0U));
        }
    }
    if (sample && operation.gate) {
        sample = sampled_values[*operation.gate].get(0U) == Logic4::one;
    }
    auto* const ring = sampled_ring_words.data() + ring_offset;
    const bool same_sampled_time_step = same_sampled_history_slot(ring, slot);
    if (sample && !same_sampled_time_step) {
        append_sampled_ring(ring, slot, slot_value);
    }
    const bool current_time_step_has_sample
        = sample || same_sampled_time_step;
    const auto count = static_cast<std::size_t>(ring[SampledRing::count]);
    const PackedLogic4 last_sample = count == 0U
        ? sampled_defaults[operation.signal]
        : sampled_ring_value(ring, count - 1U);
    if (operation.kind == SignalReadKind::sampled) {
        storage.write(operation.destination, reader_value(last_sample));
        return;
    }
    // A value change function compares the sampled value of this time step
    // with the sample of the most recent strictly prior time step in which
    // the clocking event occurred, or with the default before the first one
    // (IEEE 1800-2023 16.9.3). In a time step of a tick the ring's last
    // entry is this step's sample and the one before it the prior tick's; in
    // any other SystemVerilog time step (procedural code, an assertion of
    // another clock) the time step's sampled value is compared with the
    // ring's last entry. The other domains (PSL) keep the clock's samples.
    const bool between_ticks = operation.clock && !current_time_step_has_sample
        && process_domain == ProcessSchedulingDomain::systemverilog;
    const PackedLogic4 current
        = reader_value(between_ticks ? slot_value : last_sample);
    const PackedLogic4 previous = reader_value(between_ticks ? last_sample
            : count < 2U ? sampled_defaults[operation.signal]
                         : sampled_ring_value(ring, count - 2U));
    PackedLogic4 result;
    if (operation.kind == SignalReadKind::past) {
        const auto current_sample_count
            = current_time_step_has_sample ? std::size_t { 1U } : std::size_t { };
        const auto prior_sample_count = count - current_sample_count;
        result = prior_sample_count >= operation.ticks
            ? sampled_ring_value(ring, prior_sample_count - operation.ticks)
            : sampled_defaults[operation.signal];
    } else {
        const auto equal = current == previous;
        const auto current_lsb = current.get(0U);
        const auto previous_lsb = previous.get(0U);
        const auto value = operation.kind == SignalReadKind::rose
            ? current_lsb == Logic4::one
                && previous_lsb != Logic4::one
            : operation.kind == SignalReadKind::fell
            ? current_lsb == Logic4::zero
                && previous_lsb != Logic4::zero
            : operation.kind == SignalReadKind::stable
            ? equal
            : !equal;
        result = PackedLogic4(1U, value ? Logic4::one : Logic4::zero);
    }
    storage.write(operation.destination, reader_value(result));
}

} // namespace fsim::runtime::simir
