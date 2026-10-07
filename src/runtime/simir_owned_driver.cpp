// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace fsim::runtime::simir {
namespace {

template <typename Vector>
void reserve_one_more(Vector& entries)
{
    if (entries.size() == entries.max_size()) {
        throw std::length_error { "fused update staging exceeds capacity" };
    }
    const auto required = entries.size() + 1U;
    if (entries.capacity() >= required) {
        return;
    }
    const auto maximum = entries.max_size();
    const auto current = entries.capacity();
    const auto grown = current > maximum / 2U
        ? maximum : std::max<std::size_t>(1U, current * 2U);
    entries.reserve(std::max(required, grown));
}

} // namespace

bool Interpreter::Impl::owned_driver_active(const SignalId signal) const noexcept
{
    return signal < owned_driver_composites.size()
        && owned_driver_composites[signal].active;
}

void Interpreter::Impl::build_owned_driver_composites()
{
    owned_driver_composites.resize(signals.size());
    owned_driver_spans.resize(processes.size());
    if (driver_change_hook) {
        return;
    }
    auto switch_signals = std::vector<std::uint8_t>(signals.size(), 0U);
    // Static kernel members have no switch endpoints (the planner refuses
    // them) and never drive host signals, and the host never resolves the
    // signals the kernel owns: neither needs a composite.
    const bool kernel = static_cast<bool>(static_kernel);
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (kernel && id < fusion_dormant_process.size()
            && fusion_dormant_process[id] != 0U) {
            continue;
        }
        const auto program = processes.program_view(id);
        for (const auto signal : {
                program.switch_source(),
                program.switch_target(),
                program.switch_control() }) {
            if (signal && *signal < switch_signals.size()) {
                switch_signals[*signal] = 1U;
            }
        }
    }
    for (SignalId signal_id = 0U; signal_id < signals.size(); ++signal_id) {
        if (kernel && signal_id < static_kernel_owned_signal.size()
            && static_kernel_owned_signal[signal_id] != 0U) {
            continue;
        }
        const auto& signal = get_signal(signal_id);
        const auto width = signal.initial_value.width();
        const auto& table = driver_values[signal_id];
        const auto* const authoritative_state
            = a4_wide_disjoint_owner_commit_enabled
            ? region_authoritative_state_for_signal(signal_id) : nullptr;
        const bool has_disjoint_authoritative_slots
            = authoritative_state != nullptr
            && authoritative_state->values().layout().contains(signal_id)
            && authoritative_state->values().layout().signal(signal_id)
                    .storage_class
                == SignalDriverStorageClass::disjoint_owner
            && authoritative_state->values()
                .packed_signal_slots_bound(signal_id);
        if (has_disjoint_authoritative_slots) {
            // The versioned sidecar is the sole owner/value authority for
            // this complete disjoint partition. Do not construct the older
            // committed/phase composite over the same driver records.
            continue;
        }
        if (signal.resolution != ResolutionKind::sv_wire
            || signal.value_kind != ValueKind::logic4
            || signal.systemverilog_scalar != SystemVerilogScalarKind::None
            || has_container_signal_alias(signal_id)
            || width == 0U || table.size() < 2U
            || switch_signals[signal_id] != 0U
            || signal.has_implicit_driver || signal.has_charge_strength
            || external_driver_values[signal_id]
            || forced_values[signal_id] || forced_driver_values[signal_id]
            || (has_bidirectional_switches
                && (signal_id >= switch_endpoint_adjacency.size()
                    || signal_id >= switch_control_adjacency.size()
                    || !switch_endpoint_adjacency[signal_id].empty()
                    || !switch_control_adjacency[signal_id].empty()))
            || (!module_paths.empty()
                && (native_signal_dependencies_unknown
                    || signal_id >= module_path_destination_mask.size()
                    || module_path_destination_mask[signal_id] != 0U))) {
            continue;
        }

        auto composite = PackedLogic4 { width, Logic4::z };
        auto covered = std::vector<std::uint8_t>(width, 0U);
        auto spans = std::vector<std::pair<ProcessId, OwnedDriverSpan>> { };
        bool eligible = true;
        table.for_each_in_process_order([&](const DriverRecord& record) {
            if (!eligible) {
                return;
            }
            if (record.process >= processes.size()
                || record.strength != DriveStrength { }
                || record.value.is_logic9() || record.value.width() != width) {
                eligible = false;
                return;
            }
            const auto& program = processes.program_view(record.process);
            if (program.switch_bidirectional() || program.switch_resistive()
                || program.switch_source() || program.switch_target()
                || program.switch_control()
                || program.drive_strength() != DriveStrength { }
                || program.driver_regions().empty()
                || std::ranges::any_of(program.driver_regions(),
                    [&](const Process::DriverRegion& region) {
                        return region.signal != signal_id;
                    })) {
                eligible = false;
                return;
            }
            auto matching = std::optional<Process::DriverRegion> { };
            for (const auto& region : program.driver_regions()) {
                if (region.signal == signal_id) {
                    if (matching) {
                        eligible = false;
                        return;
                    }
                    matching = region;
                }
            }
            if (!matching || matching->whole || matching->width == 0U
                || matching->offset > width
                || matching->width > width - matching->offset) {
                eligible = false;
                return;
            }
            const auto begin = static_cast<std::size_t>(matching->offset);
            const auto end = begin + matching->width;
            for (std::size_t bit = 0U; bit < width; ++bit) {
                if (bit >= begin && bit < end) {
                    if (covered[bit]) {
                        eligible = false;
                        return;
                    }
                    covered[bit] = 1U;
                } else if (record.value.get(bit) != Logic4::z) {
                    eligible = false;
                    return;
                }
            }
            composite.insert_bits(
                record.value.extract_bits(begin, matching->width), begin);
            spans.emplace_back(record.process, OwnedDriverSpan {
                signal_id, matching->offset, matching->width });
        });
        if (!eligible || std::ranges::any_of(covered,
                [](const auto bit) { return bit == 0U; })) {
            continue;
        }
        auto& owned = owned_driver_composites[signal_id];
        owned.committed = std::move(composite);
        owned.active = true;
        for (const auto& [process, span] : spans) {
            owned_driver_spans[process] = span;
        }
    }
}

PackedLogic4 Interpreter::Impl::owned_driver_value(
    const ProcessId process, const SignalId signal) const
{
    const auto& span = owned_driver_spans.at(process);
    if (!owned_driver_active(signal) || span.signal != signal) {
        throw std::logic_error { "uncertified owned driver" };
    }
    auto result = PackedLogic4 {
        owned_driver_composites[signal].committed.width(), Logic4::z
    };
    result.insert_bits(owned_driver_composites[signal]
        .committed.extract_bits(span.offset, span.width), span.offset);
    return result;
}

void Interpreter::Impl::demote_owned_driver(const SignalId signal)
{
    prepare_region_authoritative_write(signal);
    if (!owned_driver_active(signal)) {
        return;
    }
    auto& owned = owned_driver_composites[signal];
    if (signal >= driver_update_scratch.size()) {
        driver_update_scratch.resize(signals.size());
    }
    auto& staged = driver_update_scratch[signal];
    static_assert(std::is_nothrow_move_assignable_v<PackedLogic4>);
    static_assert(noexcept(staged.swap(staged)));
    std::vector<std::pair<DriverRecord*, PackedLogic4>> committed_drivers;
    committed_drivers.reserve(driver_values[signal].size());
    driver_values[signal].for_each_in_process_order(
        [&](DriverRecord& record) {
            committed_drivers.emplace_back(
                &record, owned_driver_value(record.process, signal));
        });
    std::vector<PendingDriverCommit> materialized;
    materialized.reserve(staged.size());
    for (const auto& update : staged) {
        if (!update.owned_composite) {
            materialized.push_back(update);
            continue;
        }
        if (update.fused_cohort) {
            const auto& plan = fused_static_cohorts.at(*update.fused_cohort);
            const auto& phase = owned.phase_active
                ? owned.phase : owned.committed;
            for (const auto id : plan.candidate.members) {
                const auto& span = owned_driver_spans.at(id);
                if (span.signal != signal) {
                    continue;
                }
                const auto old_bits = owned.committed.extract_bits(
                    span.offset, span.width);
                const auto new_bits = phase.extract_bits(
                    span.offset, span.width);
                if (old_bits == new_bits) {
                    continue;
                }
                auto value = PackedLogic4 {
                    owned.committed.width(), Logic4::z
                };
                value.insert_bits(new_bits, span.offset);
                materialized.push_back(PendingDriverCommit {
                    id, std::move(value), false
                });
            }
            continue;
        }
        if (!update.driver) {
            throw std::logic_error { "owned update has no process" };
        }
        const auto& span = owned_driver_spans.at(*update.driver);
        auto value = PackedLogic4 {
            owned.committed.width(), Logic4::z
        };
        value.insert_bits(owned.phase.extract_bits(
            span.offset, span.width), span.offset);
        materialized.push_back(PendingDriverCommit {
            update.driver, std::move(value), false
        });
    }
    // Everything that can allocate or validate has completed. Publish the
    // materialized driver table and ordinary pending suffix with no-throw
    // moves so a failed preparation leaves the owned and staged state intact.
    for (auto& [record, value] : committed_drivers) {
        record->value = std::move(value);
    }
    staged.swap(materialized);
    owned.active = false;
    owned.phase_active = false;
}

void Interpreter::Impl::demote_all_owned_drivers()
{
    for (SignalId signal = 0U;
        signal < owned_driver_composites.size(); ++signal) {
        demote_owned_driver(signal);
    }
}

std::optional<PreparedOwnedUpdateSlot>
Interpreter::Impl::prepare_owned_update_slot(
    const ProcessUpdateSlotBatch& batch) const
{
    if (batch.slots.size() != 1U || batch.active_words.size() != 1U
        || batch.active_words[0] != 0U || !module_paths.empty()
        || has_bidirectional_switches || process_profile_enabled
        || update_profile_enabled || batch.process >= owned_driver_spans.size()) {
        return std::nullopt;
    }
    const auto& slot = batch.slots[0];
    const auto signal = slot.signal;
    if (signal >= signals.size() || slot.width == 0U
        || has_container_signal_alias(signal)
        || slot.width > 128U
        || slot.word_count == 0U || slot.word_count > 2U
        || slot.word_count != (slot.width + 63U) / 64U
        || slot.active == nullptr || slot.aval == nullptr
        || slot.bval == nullptr || slot.mask == nullptr
        || *slot.active != 0U || !owned_driver_active(signal)
        || direct_single_driver_record(signal) != nullptr
        || signals[signal].systemverilog_scalar
            != SystemVerilogScalarKind::None
        || signals[signal].value_kind == ValueKind::logic9
        || signals[signal].resolution == ResolutionKind::none
        || signal_transaction_observed[signal]
        || owned_driver_spans[batch.process].signal != signal
        || owned_driver_composites[signal].committed.width()
            != slot.width) {
        return std::nullopt;
    }
    const auto& span = owned_driver_spans[batch.process];
    PreparedOwnedUpdateSlot prepared {
        this, batch.process, signal, slot.width, slot.word_count, { }
    };
    for (std::uint32_t word = 0U; word < slot.word_count; ++word) {
        if (slot.mask[word] != 0U) {
            return std::nullopt;
        }
        const auto begin = static_cast<std::size_t>(word) * 64U;
        const auto size = std::min<std::size_t>(64U, slot.width - begin);
        const auto own_begin = std::max<std::size_t>(begin, span.offset);
        const auto own_end = std::min<std::size_t>(
            begin + size,
            static_cast<std::size_t>(span.offset) + span.width);
        if (own_begin >= own_end) {
            continue;
        }
        const auto count = own_end - own_begin;
        prepared.own_masks[word] = count == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : ((UINT64_C(1) << count) - UINT64_C(1))
                << (own_begin - begin);
    }
    return prepared;
}

Interpreter::Impl::OwnedDriverStage
Interpreter::Impl::stage_owned_driver_slot(
    const ProcessId process, const ProcessUpdateSlotView& slot)
{
    return stage_owned_driver_slot_impl(process, slot, nullptr);
}

Interpreter::Impl::OwnedDriverStage
Interpreter::Impl::stage_prepared_owned_update_slot(
    const PreparedOwnedUpdateSlot& prepared,
    const ProcessUpdateSlotView& slot)
{
    return stage_owned_driver_slot_impl(prepared.process, slot, &prepared);
}

Interpreter::Impl::OwnedDriverStage
Interpreter::Impl::stage_owned_driver_slot_impl(
    const ProcessId process, const ProcessUpdateSlotView& slot,
    const PreparedOwnedUpdateSlot* prepared)
{
    const auto signal = slot.signal;
    if (has_container_signal_alias(signal)
        || !owned_driver_active(signal)) {
        return OwnedDriverStage::unsupported;
    }
    if ((prepared == nullptr
            && (process >= owned_driver_spans.size()
                || owned_driver_spans[process].signal != signal))
        || slot.width != owned_driver_composites[signal].committed.width()
        || signal_transaction_observed[signal]) {
        demote_owned_driver(signal);
        return OwnedDriverStage::unsupported;
    }
    auto& owned = owned_driver_composites[signal];
    const auto& current = owned.phase_active
        ? owned.phase : owned.committed;
    bool equal = true;
    for (std::uint32_t word = 0U; word < slot.word_count; ++word) {
        const auto word_begin = static_cast<std::size_t>(word) * 64U;
        const auto remaining = slot.width - word_begin;
        const auto word_size = std::min<std::size_t>(64U, remaining);
        const auto valid_mask = word_size == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << word_size) - UINT64_C(1);
        const auto mask = slot.mask[word] & valid_mask;
        std::uint64_t own_mask { };
        if (prepared != nullptr) {
            own_mask = prepared->own_masks[word];
        } else {
            const auto& span = owned_driver_spans[process];
            const auto own_begin = std::max<std::size_t>(
                word_begin, span.offset);
            const auto own_end = std::min<std::size_t>(
                word_begin + word_size,
                static_cast<std::size_t>(span.offset) + span.width);
            if (own_begin < own_end) {
                const auto count = own_end - own_begin;
                own_mask = count == 64U
                    ? std::numeric_limits<std::uint64_t>::max()
                    : ((UINT64_C(1) << count) - UINT64_C(1))
                        << (own_begin - word_begin);
            }
        }
        if ((mask & ~own_mask) != 0U) {
            demote_owned_driver(signal);
            return OwnedDriverStage::unsupported;
        }
        if (!current.matches_masked_word(Logic4Word {
                word_size, slot.aval[word], slot.bval[word]
            }, mask, word_begin)) {
            equal = false;
        }
    }
    if (equal) {
        return OwnedDriverStage::unchanged;
    }
    if (!owned.phase_active) {
        owned.phase = owned.committed;
        owned.phase_active = true;
    }
    for (std::uint32_t word = 0U; word < slot.word_count; ++word) {
        const auto word_begin = static_cast<std::size_t>(word) * 64U;
        const auto word_size = std::min<std::size_t>(
            64U, slot.width - word_begin);
        const auto valid_mask = word_size == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << word_size) - UINT64_C(1);
        owned.phase.insert_masked_word(Logic4Word {
            word_size, slot.aval[word], slot.bval[word]
        }, slot.mask[word] & valid_mask, word_begin);
    }
    auto& staged = driver_update_scratch[signal];
    const auto found = std::ranges::find(staged,
        std::optional<ProcessId> { process },
        &PendingDriverCommit::driver);
    if (found == staged.end()) {
        if (staged.empty()) {
            driver_update_signals.push_back(signal);
        }
        staged.push_back(PendingDriverCommit {
            process, PackedLogic4 { }, true
        });
    }
    return OwnedDriverStage::changed;
}

bool Interpreter::Impl::stage_owned_driver_pending(PendingUpdate& pending)
{
    const auto signal = pending.signal;
    if (!owned_driver_active(signal)) {
        return false;
    }
    if (!pending.driver_present()
        || !pending.offset_present()
        || pending.driver >= owned_driver_spans.size()
        || owned_driver_spans[pending.driver].signal != signal) {
        demote_owned_driver(signal);
        return false;
    }
    const auto& span = owned_driver_spans[pending.driver];
    const auto width = pending.packed_value_present()
        ? pending_update_values.at(pending.packed_value).width()
        : pending.word.width;
    const auto offset = static_cast<std::size_t>(pending.offset);
    if (offset < span.offset || width > span.width
        || offset - span.offset > span.width - width) {
        demote_owned_driver(signal);
        return false;
    }
    auto& owned = owned_driver_composites[signal];
    if (!owned.phase_active) {
        owned.phase = owned.committed;
        owned.phase_active = true;
    }
    if (pending.packed_value_present()) {
        owned.phase.insert_bits(
            pending_update_values.at(pending.packed_value), offset);
    } else {
        owned.phase.insert_word(pending.word, offset);
    }
    auto& staged = driver_update_scratch[signal];
    const auto found = std::ranges::find(staged,
        std::optional<ProcessId> { pending.driver },
        &PendingDriverCommit::driver);
    if (found == staged.end()) {
        if (staged.empty()) {
            driver_update_signals.push_back(signal);
        }
        staged.push_back(PendingDriverCommit {
            pending.driver, PackedLogic4 { }, true
        });
    }
    return true;
}

void Interpreter::Impl::commit_owned_driver(const SignalId signal)
{
    prepare_region_authoritative_write(signal);
    auto& owned = owned_driver_composites[signal];
    if (!owned.active || !owned.phase_active) {
        throw std::logic_error { "owned driver lost its staged phase" };
    }
    owned.committed = std::move(owned.phase);
    owned.phase_active = false;
}

} // namespace fsim::runtime::simir
