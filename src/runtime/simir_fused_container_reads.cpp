// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_fused_container_reads.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace fsim::runtime::simir {
namespace {

bool valid_binding(const FusedMaskedContainerRead& binding)
{
    if (binding.type == nullptr) {
        return false;
    }
    const auto& type = *binding.type;
    if (!type.fixed || type.queue || type.associative || type.string_indices
        || type.two_state || type.dimensions.size() != 1U
        || type.element_kind != ContainerElementKind::Packed
        || !type.element_types.empty() || type.element_width == 0U
        || type.index_left != type.dimensions[0].first
        || type.index_right != type.dimensions[0].second) {
        return false;
    }
    const auto left = static_cast<std::int64_t>(type.index_left);
    const auto right = static_cast<std::int64_t>(type.index_right);
    const auto extent = static_cast<std::uint64_t>(
        std::max(left, right) - std::min(left, right)) + 1U;
    return extent * type.element_width == binding.signal_width;
}

} // namespace

std::optional<Process> normalize_fused_container_reads(
    const Process& original,
    const std::span<const FusedMaskedContainerRead> bindings)
{
    if (original.container_register_count == 0U
        || !original.debug_container_locals.empty()
        || original.operations.size() < 2U
        || !operation_holds<WaitSensitivity>(
            original.operations[original.operations.size() - 2U])) {
        return std::nullopt;
    }
    const auto* loop = operation_get_if<Jump>(
        &original.operations[original.operations.size() - 1U]);
    if (loop == nullptr || loop->target != 0U) {
        return std::nullopt;
    }
    struct ContainerSource {
        const FusedMaskedContainerRead* binding { };
        RegisterId packed { };
    };
    auto result = original;
    auto constants = std::vector<std::optional<PackedLogic4>>(
        original.register_count);
    auto sources = std::vector<ContainerSource>(
        original.container_register_count);
    auto operations = std::vector<Operation> { };
    operations.reserve(original.operations.size());
    std::size_t rewritten { };
    for (std::size_t position { }; position < original.operations.size();
         ++position) {
        auto operation = original.operations.expanded(position);
        bool valid = true;
        visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, ReadContainerObject>) {
                const FusedMaskedContainerRead* binding { };
                for (const auto& candidate : bindings) {
                    if (candidate.object == value.object) {
                        if (binding != nullptr) {
                            valid = false;
                            return;
                        }
                        binding = &candidate;
                    }
                }
                if (binding == nullptr || !valid_binding(*binding)
                    || value.destination >= sources.size()
                    || value.destination >= original.container_register_types.size()
                    || original.container_register_types[value.destination]
                        != *binding->type
                    || result.register_count >= std::numeric_limits<RegisterId>::max()
                    || !std::ranges::any_of(original.static_sensitivity,
                        [&](const Sensitivity& entry) {
                            return entry.signal == binding->signal
                                && entry.edge == EdgeKind::any;
                        })) {
                    valid = false;
                    return;
                }
                const auto packed = static_cast<RegisterId>(result.register_count++);
                sources[value.destination] = { binding, packed };
                operation = ReadSignal { packed, binding->signal };
            } else if constexpr (std::is_same_v<Type, ContainerRead>) {
                if (value.source >= sources.size()
                    || sources[value.source].binding == nullptr
                    || value.index >= constants.size() || !constants[value.index]
                    || value.destination >= constants.size()
                    || value.linear_index || value.string_index) {
                    valid = false;
                    return;
                }
                const auto& source = sources[value.source];
                const auto& type = *source.binding->type;
                const auto index = constants[value.index]->known_signed_value();
                const auto low = std::min(type.index_left, type.index_right);
                const auto high = std::max(type.index_left, type.index_right);
                if (!index || *index < low || *index > high) {
                    valid = false;
                    return;
                }
                const auto ordinal = static_cast<std::uint64_t>(
                    type.index_left >= type.index_right
                        ? type.index_left - *index : *index - type.index_left);
                const auto offset = source.binding->signal_width
                    - (ordinal + 1U) * type.element_width;
                constants[value.destination].reset();
                operation = Extract { value.destination, source.packed,
                    static_cast<std::uint32_t>(offset), type.element_width };
                ++rewritten;
            } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                valid = value.destination < constants.size();
                if (valid) {
                    constants[value.destination] = value.value;
                }
            } else if constexpr (std::is_same_v<Type, CopyRegister>) {
                valid = value.destination < constants.size()
                    && value.source < constants.size();
                if (valid) {
                    constants[value.destination] = constants[value.source];
                }
            } else if constexpr (std::is_same_v<Type, Extract>) {
                valid = value.destination < constants.size()
                    && value.source < constants.size();
                if (valid) {
                    const auto& source = constants[value.source];
                    auto extracted = std::optional<PackedLogic4> { };
                    if (source && value.width != 0U
                        && value.offset <= source->width()
                        && value.width <= source->width() - value.offset) {
                        extracted = source->extract_bits(value.offset, value.width);
                    }
                    constants[value.destination] = std::move(extracted);
                }
            } else if constexpr (std::is_same_v<Type, ReadSignal>
                || std::is_same_v<Type, Binary>
                || std::is_same_v<Type, Reduction>
                || std::is_same_v<Type, ConditionalSelect>
                || std::is_same_v<Type, Concatenate>) {
                valid = value.destination < constants.size();
                if (valid) {
                    constants[value.destination].reset();
                }
            } else if constexpr (std::is_same_v<Type, WaitSensitivity>) {
                valid = position + 2U == original.operations.size();
            } else if constexpr (std::is_same_v<Type, Jump>) {
                valid = position + 1U == original.operations.size()
                    && value.target == 0U;
            } else if constexpr (!std::is_same_v<Type, DebugPoint>
                && !std::is_same_v<Type, WriteUpdate>
                && !std::is_same_v<Type, WriteUpdateSlice>
                && !std::is_same_v<Type, WriteProjected>) {
                valid = false;
            }
        }, operation);
        if (!valid) {
            return std::nullopt;
        }
        operations.push_back(std::move(operation));
    }
    if (rewritten == 0U) {
        return std::nullopt;
    }
    if (!result.register_value_kinds.empty()) {
        result.register_value_kinds.resize(result.register_count, ValueKind::logic4);
    }
    result.operations = std::move(operations);
    result.container_register_count = 0U;
    result.container_register_types.clear();
    return result;
}

} // namespace fsim::runtime::simir
