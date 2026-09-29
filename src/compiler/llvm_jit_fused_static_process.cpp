// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/fused_static_process.hpp"
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace fsim::compiler {

using namespace runtime::simir;

std::optional<FusedStaticProcess> fuse_static_processes(
    const std::span<const Process* const> members,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const ProcessId fused_id)
{
    if (members.size() < 2U || members.front() == nullptr) {
        return std::nullopt;
    }
    const auto& first = *members.front();
    if (first.static_sensitivity.empty() || first.observed || first.reactive
        || first.postponed || first.final) {
        return std::nullopt;
    }
    auto sensitivity = first.static_sensitivity;
    std::ranges::sort(sensitivity, { }, [](const Sensitivity& entry) {
        return std::pair { entry.signal, entry.edge };
    });
    FusedStaticProcess result;
    result.process.id = fused_id;
    result.process.name = "fused_static_" + std::to_string(fused_id);
    result.process.initialize = false;
    // Expression profiles describe source spans and value sizing. The fused
    // body retains validated operations, so it does not need to copy this
    // per-original source metadata into the synthetic process.
    result.process.static_sensitivity = sensitivity;
    std::map<SignalId, std::vector<std::uint8_t>> owner_masks;
    std::set<SignalId> update_signals;

    for (const auto* member : members) {
        if (member == nullptr || member->final
            || member->observed || member->reactive || member->postponed
            || member->string_register_count != 0U
            || member->container_register_count != 0U
            || !member->debug_locals.empty()
            || !member->debug_string_locals.empty()
            || !member->debug_container_locals.empty()
            || !member->static_trigger_regions.empty()
            || member->switch_source || member->switch_target
            || member->switch_control || member->switch_bidirectional
            || member->switch_resistive
            || member->drive_strength != DriveStrength { }
            || member->register_count == 0U
            || member->register_count
                > std::numeric_limits<RegisterId>::max()
                    - result.process.register_count) {
            return std::nullopt;
        }
        auto current_sensitivity = member->static_sensitivity;
        std::ranges::sort(current_sensitivity,
            { }, [](const Sensitivity& entry) {
                return std::pair { entry.signal, entry.edge };
            });
        if (current_sensitivity != sensitivity) {
            return std::nullopt;
        }
        const auto operation_count = member->operations.size();
        if (operation_count < 2U
            || !operation_holds<WaitSensitivity>(
                member->operations[operation_count - 2U])
            || !operation_holds<Jump>(
                member->operations[operation_count - 1U])
            || operation_get<Jump>(
                member->operations[operation_count - 1U]).target != 0U) {
            return std::nullopt;
        }

        llvm_detail::ValidatedProcess validated;
        try {
            validated = llvm_detail::validate_process(
                *member, signal_widths, signal_value_kinds);
        } catch (const LlvmJitError&) {
            return std::nullopt;
        }
        if (validated.uses_write_after || validated.uses_write_inertial
            || validated.uses_write_projected_waveform
            || validated.uses_exact_signal_operation) {
            return std::nullopt;
        }
        const auto base = static_cast<RegisterId>(
            result.process.register_count);
        std::vector<bool> defined(member->register_count, false);
        std::map<SignalId, std::vector<std::uint8_t>> member_masks;
        const auto readable = [&](const RegisterId id) {
            return id < defined.size() && defined[id];
        };
        const auto define = [&](const RegisterId id) {
            if (id >= defined.size()) {
                return false;
            }
            defined[id] = true;
            return true;
        };
        const auto mark_write = [&](const SignalId signal,
                                    const std::uint32_t offset,
                                    const RegisterId source) {
            if (!readable(source) || signal >= signal_widths.size()
                || source >= validated.register_widths.size()) {
                return false;
            }
            const auto width = validated.register_widths[source];
            if (width == 0U || offset > signal_widths[signal]
                || width > signal_widths[signal] - offset) {
                return false;
            }
            auto& mask = member_masks[signal];
            mask.resize(signal_widths[signal]);
            std::fill(mask.begin() + offset, mask.begin() + offset + width,
                std::uint8_t { 1U });
            ++result.original_updates;
            update_signals.insert(signal);
            return true;
        };

        for (std::size_t index = 0U; index + 2U < operation_count;
             ++index) {
            auto operation = member->operations.expanded(index);
            bool accepted = false;
            visit_operation([&](auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, DebugPoint>) {
                    accepted = true;
                    return;
                } else if constexpr (std::is_same_v<Type, LoadConstant>
                    || std::is_same_v<Type, ReadSignal>) {
                    if constexpr (std::is_same_v<Type, ReadSignal>) {
                        if (value.kind != SignalReadKind::current
                            || value.ticks != 1U
                            || value.clock || value.gate) {
                            return;
                        }
                    }
                    accepted = define(value.destination);
                    if (accepted) {
                        value.destination += base;
                    }
                } else if constexpr (std::is_same_v<Type, CopyRegister>
                    || std::is_same_v<Type, Extract>
                    || std::is_same_v<Type, Reduction>) {
                    if constexpr (std::is_same_v<Type, Reduction>) {
                        if (validated.uses_logic9
                            || (value.operation != ReductionOperator::bit_and
                            && value.operation != ReductionOperator::bit_or
                            && value.operation != ReductionOperator::bit_xor)) {
                            return;
                        }
                    }
                    accepted = readable(value.source)
                        && define(value.destination);
                    if (accepted) {
                        value.source += base;
                        value.destination += base;
                    }
                } else if constexpr (std::is_same_v<Type, Binary>) {
                    if (value.operation != BinaryOperator::bit_and
                        && value.operation != BinaryOperator::bit_or
                        && value.operation != BinaryOperator::bit_xor) {
                        return;
                    }
                    accepted = readable(value.lhs) && readable(value.rhs)
                        && define(value.destination);
                    if (accepted) {
                        value.lhs += base;
                        value.rhs += base;
                        value.destination += base;
                    }
                } else if constexpr (std::is_same_v<Type, Concatenate>) {
                    accepted = std::ranges::all_of(
                        value.operands, readable)
                        && define(value.destination);
                    if (accepted) {
                        for (auto& operand : value.operands) {
                            operand += base;
                        }
                        value.destination += base;
                    }
                } else if constexpr (std::is_same_v<Type, WriteUpdate>) {
                    accepted = mark_write(value.signal, 0U, value.source);
                    if (accepted) {
                        value.source += base;
                    }
                } else if constexpr (std::is_same_v<Type,
                                         WriteUpdateSlice>) {
                    accepted = mark_write(
                        value.signal, value.offset, value.source);
                    if (accepted) {
                        value.source += base;
                    }
                } else if constexpr (std::is_same_v<Type,
                                         WriteProjected>) {
                    accepted = value.delay == 0U
                        && value.rejection == 0U
                        && value.mode == ProjectedDelayMode::inertial
                        && mark_write(value.signal, 0U, value.source);
                    if (accepted) {
                        value.source += base;
                    }
                }
            }, operation);
            if (!accepted) {
                return std::nullopt;
            }
            if (!operation_holds<DebugPoint>(operation)) {
                result.process.operations.push_back(std::move(operation));
            }
        }
        if (member_masks.empty()) {
            return std::nullopt;
        }
        for (const auto& [signal, member_mask] : member_masks) {
            for (std::size_t bit = 0U; bit < member_mask.size(); ++bit) {
                if (member_mask[bit] == 0U) {
                    continue;
                }
                const auto covered = std::ranges::any_of(
                    member->driver_regions,
                    [&](const Process::DriverRegion& region) {
                        return region.signal == signal
                            && (region.whole
                                || (bit >= region.offset
                                    && bit - region.offset < region.width));
                    });
                if (!covered) {
                    return std::nullopt;
                }
            }
        }
        for (const auto& [signal, member_mask] : member_masks) {
            auto& owned = owner_masks[signal];
            owned.resize(member_mask.size());
            for (std::size_t bit = 0U; bit < member_mask.size(); ++bit) {
                if (member_mask[bit] != 0U && owned[bit] != 0U) {
                    return std::nullopt;
                }
                owned[bit] |= member_mask[bit];
            }
        }
        for (const auto& region : member->driver_regions) {
            result.process.driver_regions.push_back(region);
        }
        result.members.push_back(member->id);
        result.process.register_count += member->register_count;
        if (!member->register_value_kinds.empty()) {
            if (result.process.register_value_kinds.empty()
                && base != 0U) {
                result.process.register_value_kinds.resize(
                    base, ValueKind::logic4);
            }
            result.process.register_value_kinds.insert(
                result.process.register_value_kinds.end(),
                member->register_value_kinds.begin(),
                member->register_value_kinds.end());
        } else if (!result.process.register_value_kinds.empty()) {
            result.process.register_value_kinds.insert(
                result.process.register_value_kinds.end(),
                member->register_count, ValueKind::logic4);
        }
    }
    result.process.operations.push_back(WaitSensitivity { });
    result.process.operations.push_back(Jump { 0U });
    result.aggregate_update_signals = update_signals.size();
    try {
        static_cast<void>(llvm_detail::validate_process(
            result.process, signal_widths, signal_value_kinds));
    } catch (const LlvmJitError&) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::compiler
