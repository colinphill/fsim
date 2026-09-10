// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_validation_class.hpp"

#include <limits>
#include <vector>

namespace fsim::compiler::llvm_detail {

void validate_constraint_templates(
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
            case Kind::Unique:
                return !expression->text.empty()
                    && !expression->operands.empty();
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


} // namespace fsim::compiler::llvm_detail
