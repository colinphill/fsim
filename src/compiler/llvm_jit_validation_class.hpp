// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::compiler::llvm_detail {

inline void validate_constraint_templates(
    const runtime::simir::Process& process,
    const std::size_t index,
    const std::span<const runtime::SystemVerilogConstraintTemplate> expressions)
{
    using Kind = runtime::SystemVerilogConstraintTemplateKind;
    std::vector<const runtime::SystemVerilogConstraintTemplate*> pending;
    pending.reserve(expressions.size());
    for (const auto& expression : expressions)
        pending.push_back(&expression);
    while (!pending.empty()) {
        const auto* expression = pending.back();
        pending.pop_back();
        const auto operand_count = expression->operands.size();
        const auto valid_shape = [&] {
            switch (expression->kind) {
            case Kind::Name:
                return !expression->text.empty() && operand_count == 0;
            case Kind::Constant:
                return !expression->constant.empty()
                    && expression->profile.width == expression->constant.width()
                    && operand_count == 0;
            case Kind::Unary:
            case Kind::Soft:
                return !expression->text.empty() && operand_count == 1;
            case Kind::Binary:
            case Kind::InsideRange:
            case Kind::DistributionItem:
            case Kind::Implication:
            case Kind::SolveBefore:
                return !expression->text.empty() && operand_count == 2;
            case Kind::Conditional:
                return operand_count == 3;
            case Kind::InsideSet:
            case Kind::Distribution:
                return !expression->text.empty() && operand_count >= 2;
            case Kind::Block:
                return true;
            case Kind::ConditionalConstraint:
                return operand_count == 2 || operand_count == 3;
            case Kind::SolveList:
                return !expression->operands.empty();
            }
            return false;
        }();
        if (!valid_shape) {
            reject(process, index, "inline constraint template is malformed");
        }
        if (pending.size()
            > std::numeric_limits<std::size_t>::max() - operand_count) {
            reject(process, index, "inline constraint template graph overflows");
        }
        for (const auto& operand : expression->operands)
            pending.push_back(&operand);
    }
}

template <typename OperationType, typename RecordUse,
    typename RecordDefinition, typename ConstrainWidth,
    typename ValidateStringRegister>
void validate_class_operation(
    const runtime::simir::Process& process,
    const std::size_t index,
    const OperationType& operation,
    RecordUse&& record_use,
    RecordDefinition&& record_definition,
    ConstrainWidth&& constrain_width,
    ValidateStringRegister&& validate_string_register)
{
    using namespace runtime::simir;
    if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
        if (operation.specialization_identity.empty()
            || operation.declared_type.empty()) {
            reject(
                process, index,
                "ClassAllocate requires specialization and declared types");
        }
        if (!operation.constructor_actual_names.empty()
            && operation.constructor_actual_names.size()
                != operation.constructor_actuals.size()) {
            reject(
                process, index,
                "ClassAllocate actual names must align with actual registers");
        }
        if (!operation.constructor_actual_kinds.empty()
            && operation.constructor_actual_kinds.size()
                != operation.constructor_actuals.size()) {
            reject(
                process, index,
                "ClassAllocate actual kinds must align with actual registers");
        }
        for (std::size_t actual_index = 0;
            actual_index < operation.constructor_actuals.size();
            ++actual_index) {
            const auto kind = operation.constructor_actual_kinds.empty()
                ? 0U
                : operation.constructor_actual_kinds[actual_index];
            if (kind > 1U) {
                reject(process, index, "ClassAllocate actual kind is invalid");
            }
            if (kind == 0U) {
                record_use(operation.constructor_actuals[actual_index], index);
            }
        }
        record_definition(operation.destination, index);
        constrain_width(operation.destination, 64U, index);
    } else if constexpr (std::is_same_v<OperationType, ClassPropertyRead>) {
        if (operation.property_identity.empty() || operation.width == 0) {
            reject(process, index, "ClassPropertyRead requires an identity");
        }
        record_use(operation.receiver, index);
        constrain_width(operation.receiver, 64U, index);
        record_definition(operation.destination, index);
        constrain_width(operation.destination, operation.width, index);
    } else if constexpr (std::is_same_v<OperationType, ClassPropertyWrite>) {
        if (operation.property_identity.empty()) {
            reject(process, index, "ClassPropertyWrite requires an identity");
        }
        record_use(operation.receiver, index);
        constrain_width(operation.receiver, 64U, index);
        record_use(operation.source, index);
    } else if constexpr (std::is_same_v<OperationType, ClassMethodCall>) {
        if (operation.method_identity.empty() || operation.result_width == 0
            || operation.actual_names.size() != operation.actuals.size()
            || operation.actual_directions.size() != operation.actuals.size()
            || (!operation.actual_kinds.empty()
                && operation.actual_kinds.size() != operation.actuals.size())) {
            reject(
                process, index,
                "ClassMethodCall requires aligned method actual metadata");
        }
        validate_constraint_templates(
            process, index, operation.inline_constraints);
        record_use(operation.receiver, index);
        constrain_width(operation.receiver, 64U, index);
        for (std::size_t actual_index = 0;
            actual_index < operation.actuals.size(); ++actual_index) {
            const auto kind = operation.actual_kinds.empty()
                ? 0U
                : operation.actual_kinds[actual_index];
            if (kind > 1U) {
                reject(process, index, "ClassMethodCall actual kind is invalid");
            }
            if (kind == 1U) {
                validate_string_register(
                    operation.actuals[actual_index], index, "class actual");
            } else {
                record_use(operation.actuals[actual_index], index);
            }
        }
        record_definition(operation.destination, index);
        constrain_width(operation.destination, operation.result_width, index);
    } else if constexpr (
        std::is_same_v<OperationType, ClassStaticPropertyRead>) {
        if (operation.property_identity.empty() || operation.width == 0) {
            reject(
                process, index,
                "ClassStaticPropertyRead requires an identity and width");
        }
        record_definition(operation.destination, index);
        constrain_width(operation.destination, operation.width, index);
    } else if constexpr (
        std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
        if (operation.property_identity.empty()) {
            reject(process, index, "ClassStaticPropertyWrite requires an identity");
        }
        record_use(operation.source, index);
    } else if constexpr (std::is_same_v<OperationType, ClassStaticMethodCall>) {
        if (operation.method_identity.empty() || operation.result_width == 0
            || operation.actual_names.size() != operation.actuals.size()
            || operation.actual_directions.size() != operation.actuals.size()
            || (!operation.actual_kinds.empty()
                && operation.actual_kinds.size() != operation.actuals.size())) {
            reject(
                process, index,
                "ClassStaticMethodCall requires aligned method metadata");
        }
        for (std::size_t actual_index = 0;
            actual_index < operation.actuals.size(); ++actual_index) {
            const auto kind = operation.actual_kinds.empty()
                ? 0U
                : operation.actual_kinds[actual_index];
            if (kind > 1U) {
                reject(
                    process, index,
                    "ClassStaticMethodCall actual kind is invalid");
            }
            if (kind == 1U) {
                validate_string_register(
                    operation.actuals[actual_index], index, "class static actual");
            } else {
                record_use(operation.actuals[actual_index], index);
            }
        }
        record_definition(operation.destination, index);
        constrain_width(operation.destination, operation.result_width, index);
    }
}

} // namespace fsim::compiler::llvm_detail
