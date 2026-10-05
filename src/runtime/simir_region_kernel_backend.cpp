// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_kernel_backend.hpp"

#include <algorithm>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] bool same_supported_operation(
    const Operation& left, const Operation& right)
{
    if (left.storage.index() != right.storage.index()) {
        return false;
    }
    if (const auto* value = operation_get_if<LoadConstant>(&left)) {
        const auto* other = operation_get_if<LoadConstant>(&right);
        return other && value->destination == other->destination
            && value->value == other->value;
    }
    if (const auto* value = operation_get_if<CopyRegister>(&left)) {
        const auto* other = operation_get_if<CopyRegister>(&right);
        return other && value->destination == other->destination
            && value->source == other->source;
    }
    if (const auto* value = operation_get_if<UnaryNot>(&left)) {
        const auto* other = operation_get_if<UnaryNot>(&right);
        return other && value->destination == other->destination
            && value->source == other->source;
    }
    if (const auto* value = operation_get_if<Binary>(&left)) {
        const auto* other = operation_get_if<Binary>(&right);
        return other && value->operation == other->operation
            && value->destination == other->destination
            && value->lhs == other->lhs && value->rhs == other->rhs;
    }
    if (const auto* value = operation_get_if<Reduction>(&left)) {
        const auto* other = operation_get_if<Reduction>(&right);
        return other && value->operation == other->operation
            && value->destination == other->destination
            && value->source == other->source;
    }
    if (const auto* value = operation_get_if<Extract>(&left)) {
        const auto* other = operation_get_if<Extract>(&right);
        return other && value->destination == other->destination
            && value->source == other->source
            && value->offset == other->offset
            && value->width == other->width;
    }
    if (const auto* value = operation_get_if<Concatenate>(&left)) {
        const auto* other = operation_get_if<Concatenate>(&right);
        return other && value->destination == other->destination
            && value->operands == other->operands
            && value->width == other->width;
    }
    if (const auto* value = operation_get_if<Branch>(&left)) {
        const auto* other = operation_get_if<Branch>(&right);
        return other && value->condition == other->condition
            && value->when_true == other->when_true
            && value->when_false == other->when_false
            && value->unknown_policy == other->unknown_policy;
    }
    if (const auto* value = operation_get_if<DebugPoint>(&left)) {
        const auto* other = operation_get_if<DebugPoint>(&right);
        return other && value->kind == other->kind
            && value->source == other->source
            && value->scope == other->scope;
    }
    if (const auto* value = operation_get_if<Halt>(&left)) {
        const auto* other = operation_get_if<Halt>(&right);
        return other && value->program_exit == other->program_exit;
    }
    return false;
}

[[nodiscard]] bool same_supported_program(const Process& left,
    const Process& right)
{
    const auto auxiliary_metadata_is_empty = [](const Process& process) {
        return process.string_register_count == 0U
            && process.container_register_count == 0U
            && process.debug_locals.empty()
            && process.debug_string_locals.empty()
            && process.debug_container_locals.empty()
            && process.container_register_types.empty()
            && process.static_sensitivity.empty()
            && process.static_trigger_regions.empty()
            && process.driver_regions.empty()
            && process.drive_strength == DriveStrength { }
            && !process.switch_source && !process.switch_target
            && !process.switch_control
            && process.switch_source_offset == 0U
            && process.switch_target_offset == 0U
            && process.switch_width == 0U
            && process.switch_active_high
            && !process.switch_bidirectional
            && !process.switch_resistive
            && !process.observed && !process.reactive
            && !process.program_owner && !process.postponed
            && !process.final && process.expression_profiles.empty();
    };
    if (!auxiliary_metadata_is_empty(left)
        || !auxiliary_metadata_is_empty(right)
        || left.id != right.id || left.name != right.name
        || left.language_standard != right.language_standard
        || left.compatibility_profile != right.compatibility_profile
        || left.register_count != right.register_count
        || left.register_value_kinds != right.register_value_kinds
        || left.initialize != right.initialize
        || left.scheduling_domain != right.scheduling_domain
        || left.operations.size() != right.operations.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.operations.size(); ++index) {
        const auto& left_operation = left.operations[index];
        const auto& right_operation = right.operations[index];
        const auto* left_debug_point =
            operation_get_if<DebugPoint>(&left_operation);
        const auto* right_debug_point =
            operation_get_if<DebugPoint>(&right_operation);
        if (left_debug_point != nullptr || right_debug_point != nullptr) {
            if (left_debug_point == nullptr || right_debug_point == nullptr) {
                return false;
            }
            const auto& left_effective =
                left.operations.debug_point(index, *left_debug_point);
            const auto& right_effective =
                right.operations.debug_point(index, *right_debug_point);
            if (left_effective.kind != right_effective.kind
                || left_effective.source != right_effective.source
                || left.operations.debug_scope(left_effective.scope)
                    != right.operations.debug_scope(right_effective.scope)) {
                return false;
            }
        } else if (!same_supported_operation(
                       left_operation, right_operation)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool same_region_kernel_mapping(
    const RegionConeActivationKernel& left,
    const RegionConeActivationKernel& right)
{
    return same_supported_program(left.program, right.program)
        && left.member_execution_order == right.member_execution_order
        && left.inputs == right.inputs
        && left.members == right.members
        && left.outputs == right.outputs
        && left.internal_signals == right.internal_signals
        && left.constant_inputs == right.constant_inputs;
}

} // namespace fsim::runtime::simir
