// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/fused_masked_process.hpp"
#include "fsim/runtime/simir_fused_branch_safety.hpp"
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace fsim::compiler {
namespace {
using namespace runtime::simir;

struct DefiniteState {
    std::vector<bool> registers;
    std::set<std::size_t> writes;
};

void merge_state(std::optional<DefiniteState>& target,
    const DefiniteState& source)
{
    if (!target) {
        target = source;
        return;
    }
    for (std::size_t id = 0U; id < target->registers.size(); ++id) {
        target->registers[id] = target->registers[id] && source.registers[id];
    }
    std::erase_if(target->writes,
        [&](const auto index) { return !source.writes.contains(index); });
}

bool supported_member(const Process& process)
{
    const auto count = process.operations.size();
    return count >= 3U && !process.static_sensitivity.empty()
        && !process.final && !process.observed && !process.reactive
        && !process.postponed && !process.switch_source
        && !process.switch_target && !process.switch_control
        && !process.switch_bidirectional && !process.switch_resistive
        && process.drive_strength == DriveStrength { }
        && process.register_count != 0U
        && process.string_register_count == 0U
        && process.container_register_count == 0U
        && process.debug_locals.empty() && process.debug_string_locals.empty()
        && process.debug_container_locals.empty()
        && process.static_trigger_regions.empty()
        && operation_holds<WaitSensitivity>(process.operations[count - 2U])
        && operation_holds<Jump>(process.operations[count - 1U])
        && operation_get<Jump>(process.operations[count - 1U]).target == 0U;
}

bool append_member(FusedMaskedProcess& result, const Process& member,
    const std::span<const std::uint32_t> widths,
    const std::span<const ValueKind> kinds,
    std::map<SignalId, std::vector<bool>>& occupied)
{
    if (!supported_member(member)
        || member.register_count > std::numeric_limits<RegisterId>::max()
                - result.process.register_count) {
        return false;
    }
    llvm_detail::ValidatedProcess validated;
    try {
        validated = llvm_detail::validate_process(member, widths, kinds);
    } catch (const LlvmJitError&) {
        return false;
    }
    if (!runtime::simir::detail::masked_branch_conditions_are_proven_known(
            member.operations, member.register_count)) {
        return false;
    }
    const auto count = member.operations.size() - 2U;
    if (count > std::numeric_limits<InstructionIndex>::max()
            - result.process.operations.size() - 2U) {
        return false;
    }
    const auto base = static_cast<RegisterId>(result.process.register_count);
    const auto begin = static_cast<InstructionIndex>(result.process.operations.size());
    std::vector<std::optional<DefiniteState>> incoming(count + 1U);
    incoming[0] = DefiniteState { std::vector<bool>(member.register_count), { } };
    std::set<std::size_t> writes;
    std::map<SignalId, std::vector<bool>> masks;
    std::size_t projected_count { };
    for (std::size_t index = 0U; index < count; ++index) {
        if (!incoming[index]) {
            return false;
        }
        auto state = *incoming[index];
        for (const auto id : validated.instruction_uses[index]) {
            if (id >= state.registers.size() || !state.registers[id]) {
                return false;
            }
        }
        for (const auto id : validated.instruction_definitions[index]) {
            if (id >= state.registers.size()) {
                return false;
            }
            state.registers[id] = true;
        }
        auto operation = member.operations.expanded(index);
        bool accepted = false;
        std::vector<std::size_t> successors { index + 1U };
        const auto write = [&](const SignalId signal, const RegisterId source,
                               const std::uint32_t offset) {
            if (signal >= widths.size() || source >= validated.register_widths.size()) {
                return false;
            }
            const auto width = validated.register_widths[source];
            if (width == 0U || offset > widths[signal]
                || width > widths[signal] - offset) {
                return false;
            }
            auto& mask = masks[signal];
            mask.resize(widths[signal]);
            std::fill(mask.begin() + offset, mask.begin() + offset + width, true);
            writes.insert(index);
            state.writes.insert(index);
            return true;
        };
        visit_operation([&](auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, DebugPoint>) {
                // Instrumentation is disabled for masked kernels. Keep the
                // instruction index, but remove instance-specific provenance.
                value = DebugPoint { };
                accepted = true;
            } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                value.destination += base;
                accepted = true;
            } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                accepted = value.kind == SignalReadKind::current
                    && value.ticks == 1U && !value.clock && !value.gate;
                value.destination += base;
            } else if constexpr (std::is_same_v<Type, CopyRegister>
                || std::is_same_v<Type, Extract> || std::is_same_v<Type, Reduction>) {
                accepted = true;
                if constexpr (std::is_same_v<Type, Reduction>) {
                    accepted = !validated.uses_logic9
                        && (value.operation == ReductionOperator::bit_and
                            || value.operation == ReductionOperator::bit_or
                            || value.operation == ReductionOperator::bit_xor);
                }
                value.source += base;
                value.destination += base;
            } else if constexpr (std::is_same_v<Type, Binary>) {
                accepted = value.operation == BinaryOperator::bit_and
                    || value.operation == BinaryOperator::bit_or
                    || value.operation == BinaryOperator::bit_xor
                    || value.operation == BinaryOperator::equal
                    || value.operation == BinaryOperator::case_equal;
                value.lhs += base;
                value.rhs += base;
                value.destination += base;
            } else if constexpr (std::is_same_v<Type, ConditionalSelect>) {
                value.condition += base;
                value.when_true += base;
                value.when_false += base;
                value.destination += base;
                accepted = true;
            } else if constexpr (std::is_same_v<Type, Concatenate>) {
                for (auto& operand : value.operands) {
                    operand += base;
                }
                value.destination += base;
                accepted = true;
            } else if constexpr (std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteProjected>) {
                std::uint32_t offset { };
                if constexpr (std::is_same_v<Type, WriteUpdateSlice>) {
                    offset = value.offset;
                }
                accepted = write(value.signal, value.source, offset);
                if constexpr (std::is_same_v<Type, WriteProjected>) {
                    accepted = accepted && value.delay == 0U
                        && value.rejection == 0U
                        && value.mode == ProjectedDelayMode::inertial
                        && validated.register_widths[value.source] == widths[value.signal];
                    ++projected_count;
                }
                value.source += base;
            } else if constexpr (std::is_same_v<Type, Jump>) {
                successors = { value.target };
                value.target += begin;
                accepted = true;
            } else if constexpr (std::is_same_v<Type, Branch>) {
                successors = { value.when_true, value.when_false };
                value.condition += base;
                value.when_true += begin;
                value.when_false += begin;
                accepted = true;
            }
        }, operation);
        if (!accepted) {
            return false;
        }
        for (const auto target : successors) {
            if (target <= index || target > count) {
                return false;
            }
            merge_state(incoming[target], state);
        }
        result.process.operations.push_back(std::move(operation));
    }
    if (writes.empty() || !incoming[count] || incoming[count]->writes != writes
        || (projected_count != 0U && (projected_count != 1U || writes.size() != 1U))) {
        return false;
    }
    auto& member_writes = result.writes.emplace_back();
    member_writes.original_id = member.id;
    for (const auto& [signal, mask] : masks) {
        auto& previous = occupied[signal];
        previous.resize(mask.size());
        for (std::size_t bit = 0U; bit < mask.size(); ++bit) {
            if (!mask[bit]) {
                continue;
            }
            if (previous[bit] || !std::ranges::any_of(member.driver_regions,
                    [&](const auto& region) {
                        return region.signal == signal && (region.whole
                            || (bit >= region.offset && bit - region.offset < region.width));
                    })) {
                return false;
            }
            previous[bit] = true;
        }
        for (std::size_t bit = 0U; bit < mask.size();) {
            if (!mask[bit]) {
                ++bit;
                continue;
            }
            const auto first = bit;
            while (bit < mask.size() && mask[bit]) {
                ++bit;
            }
            member_writes.regions.push_back({ signal,
                static_cast<std::uint32_t>(first),
                static_cast<std::uint32_t>(bit - first), first == 0U && bit == mask.size() });
        }
    }
    result.process.driver_regions.insert(result.process.driver_regions.end(),
        member_writes.regions.begin(), member_writes.regions.end());
    result.gates.push_back({ member.id, begin,
        static_cast<InstructionIndex>(result.process.operations.size()),
        static_cast<std::uint32_t>(result.gates.size()) });
    result.process.register_count += member.register_count;
    result.process.register_value_kinds.resize(base, ValueKind::logic4);
    const auto member_register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(member.register_value_kinds);
    if (member_register_value_kinds.empty()) {
        result.process.register_value_kinds.resize(result.process.register_count, ValueKind::logic4);
    } else {
        result.process.register_value_kinds.insert(result.process.register_value_kinds.end(),
            member_register_value_kinds.begin(), member_register_value_kinds.end());
    }
    return true;
}
} // namespace

std::optional<FusedMaskedProcess> fuse_masked_processes(
    const std::span<const runtime::simir::Process* const> ordered_members,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const runtime::simir::ProcessId synthetic_id)
{
    if (ordered_members.empty()
        || ordered_members.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    FusedMaskedProcess result;
    result.process.id = synthetic_id;
    result.process.name = "fused_masked_" + std::to_string(synthetic_id);
    result.process.initialize = false;
    std::map<SignalId, std::vector<bool>> occupied;
    std::set<ProcessId> identities;
    for (const auto* member : ordered_members) {
        if (member == nullptr || !identities.insert(member->id).second
            || !append_member(result, *member, signal_widths, signal_value_kinds, occupied)) {
            return std::nullopt;
        }
        for (const auto& sensitivity : member->static_sensitivity) {
            if (std::ranges::find(result.process.static_sensitivity, sensitivity)
                == result.process.static_sensitivity.end()) {
                result.process.static_sensitivity.push_back(sensitivity);
            }
        }
    }
    result.process.operations.push_back(WaitSensitivity { });
    result.process.operations.push_back(Jump { 0U });
    try {
        static_cast<void>(llvm_detail::validate_process(
            result.process, signal_widths, signal_value_kinds));
    } catch (const LlvmJitError&) {
        return std::nullopt;
    }
    return result;
}
} // namespace fsim::compiler
