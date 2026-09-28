// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

namespace fsim::runtime::simir {

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
    for (const auto& process : processes) {
        const auto& program = process.program();
        for (const auto signal : {
                program.switch_source,
                program.switch_target,
                program.switch_control }) {
            if (signal && *signal < switch_signals.size()) {
                switch_signals[*signal] = 1U;
            }
        }
    }
    for (SignalId signal_id = 0U; signal_id < signals.size(); ++signal_id) {
        const auto& signal = get_signal(signal_id);
        const auto width = signal.initial_value.width();
        const auto& table = driver_values[signal_id];
        if (signal.resolution != ResolutionKind::sv_wire
            || signal.value_kind != ValueKind::logic4
            || signal.systemverilog_scalar != SystemVerilogScalarKind::None
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
            const auto& program = get_process(record.process).program();
            if (program.switch_bidirectional || program.switch_resistive
                || program.switch_source || program.switch_target
                || program.switch_control
                || program.drive_strength != DriveStrength { }
                || program.driver_regions.empty()
                || std::ranges::any_of(program.driver_regions,
                    [&](const Process::DriverRegion& region) {
                        return region.signal != signal_id;
                    })) {
                eligible = false;
                return;
            }
            auto matching = std::optional<Process::DriverRegion> { };
            for (const auto& region : program.driver_regions) {
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
    if (!owned_driver_active(signal)) {
        return;
    }
    auto& owned = owned_driver_composites[signal];
    if (signal >= driver_update_scratch.size()) {
        driver_update_scratch.resize(signals.size());
    }
    auto& staged = driver_update_scratch[signal];
    driver_values[signal].for_each_in_process_order(
        [&](DriverRecord& record) {
            record.value = owned_driver_value(record.process, signal);
        });
    for (auto& update : staged) {
        if (!update.owned_composite) {
            continue;
        }
        if (!update.driver) {
            throw std::logic_error { "owned update has no process" };
        }
        const auto& span = owned_driver_spans.at(*update.driver);
        update.value = PackedLogic4 {
            owned.committed.width(), Logic4::z
        };
        update.value.insert_bits(owned.phase.extract_bits(
            span.offset, span.width), span.offset);
        update.owned_composite = false;
    }
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

Interpreter::Impl::OwnedDriverStage
Interpreter::Impl::stage_owned_driver_slot(
    const ProcessId process, const ProcessUpdateSlotView& slot)
{
    const auto signal = slot.signal;
    if (!owned_driver_active(signal)) {
        return OwnedDriverStage::unsupported;
    }
    if (process >= owned_driver_spans.size()
        || owned_driver_spans[process].signal != signal
        || slot.width != owned_driver_composites[signal].committed.width()
        || signal_transaction_observed[signal]) {
        demote_owned_driver(signal);
        return OwnedDriverStage::unsupported;
    }
    const auto& span = owned_driver_spans[process];
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
        const auto own_begin = std::max<std::size_t>(word_begin, span.offset);
        const auto own_end = std::min<std::size_t>(
            word_begin + word_size,
            static_cast<std::size_t>(span.offset) + span.width);
        std::uint64_t own_mask { };
        if (own_begin < own_end) {
            const auto count = own_end - own_begin;
            own_mask = count == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : ((UINT64_C(1) << count) - UINT64_C(1))
                    << (own_begin - word_begin);
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
    auto& owned = owned_driver_composites[signal];
    if (!owned.active || !owned.phase_active) {
        throw std::logic_error { "owned driver lost its staged phase" };
    }
    owned.committed = std::move(owned.phase);
    owned.phase_active = false;
}

} // namespace fsim::runtime::simir
