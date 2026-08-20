// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "vhdl_array_boundary.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] bool is_composite(const frontend::Type& type)
    {
        return type.vhdl_array.has_value() || !type.packed_members.empty();
    }

    [[nodiscard]] bool same_nominal_base(
        const frontend::Type& left,
        const frontend::Type& right)
    {
        if (left.nominal_type.empty() && right.nominal_type.empty()) {
            return true;
        }
        return !left.nominal_type.empty()
            && left.nominal_type == right.nominal_type;
    }

    [[nodiscard]] std::optional<std::uint64_t> dimension_length(
        const frontend::VhdlArrayDimension& dimension)
    {
        if (dimension.null) {
            return std::uint64_t { 0 };
        }
        if (!dimension.range || dimension.unconstrained) {
            return std::nullopt;
        }
        const auto left = static_cast<std::uint64_t>(
            dimension.range->left);
        const auto right = static_cast<std::uint64_t>(
            dimension.range->right);
        const auto distance = dimension.range->left >= dimension.range->right
            ? left - right
            : right - left;
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        return distance + 1U;
    }

    [[nodiscard]] bool common_composite_type(
        const frontend::Type& left,
        const frontend::Type& right)
    {
        if (!is_composite(left) || !is_composite(right)
            || left.domain != right.domain
            || !same_nominal_base(left, right)) {
            return false;
        }
        if (left.vhdl_array || right.vhdl_array) {
            if (!left.vhdl_array || !right.vhdl_array
                || left.vhdl_array->dimensions.size()
                    != right.vhdl_array->dimensions.size()
                || left.vhdl_array->element_types.size()
                    != right.vhdl_array->element_types.size()) {
                return false;
            }
            for (std::size_t index = 0;
                index < left.vhdl_array->element_types.size(); ++index) {
                if (!vhdl_array_element_profile_matches(
                        left.vhdl_array->element_types[index],
                        right.vhdl_array->element_types[index])) {
                    return false;
                }
            }
            return true;
        }
        return !left.packed_members.empty()
            && !right.packed_members.empty();
    }

    [[nodiscard]] bool assignment_compatible(
        const frontend::Type& target,
        const frontend::Type& source)
    {
        if (!common_composite_type(target, source)) {
            return false;
        }
        if (!target.vhdl_array) {
            return target.width() == source.width();
        }
        for (std::size_t index = 0;
            index < target.vhdl_array->dimensions.size(); ++index) {
            const auto target_length = dimension_length(
                target.vhdl_array->dimensions[index]);
            const auto source_length = dimension_length(
                source.vhdl_array->dimensions[index]);
            if (!target_length || !source_length
                || *target_length != *source_length) {
                return false;
            }
        }
        return target.width() == source.width();
    }

    [[nodiscard]] bool matching_array_type(const frontend::Type& type)
    {
        if (!type.vhdl_array
            || type.vhdl_array->dimensions.size() != 1
            || type.vhdl_array->element_types.size() != 1) {
            return false;
        }
        const auto& element = type.vhdl_array->element_types.front();
        return !element.vhdl_array && element.packed_members.empty()
            && element.width() == 1
            && (element.domain == frontend::ValueDomain::Bit2
                || element.domain == frontend::ValueDomain::Logic9);
    }

    [[nodiscard]] std::optional<frontend::Type> bounded_array_context(
        const frontend::Type* source)
    {
        if (source == nullptr || source->vhdl_array
            || !source->packed_range || !source->packed_members.empty()
            || (source->domain != frontend::ValueDomain::Bit2
                && source->domain != frontend::ValueDomain::Logic9)) {
            return std::nullopt;
        }
        auto result = *source;
        const auto& packed = *source->packed_range;
        frontend::VhdlArrayDimension dimension;
        dimension.index_subtype = "integer";
        dimension.range = frontend::IntegerRange {
            packed.left, packed.right, packed.descending
        };
        dimension.null = packed.descending
            ? packed.left < packed.right
            : packed.left > packed.right;
        dimension.stride = 1;
        frontend::Type element;
        element.domain = source->domain;
        element.spelling = source->domain == frontend::ValueDomain::Logic9
            ? "std_logic"
            : "bit";
        element.nominal_type = source->domain == frontend::ValueDomain::Logic9
            ? "std.standard.std_logic"
            : "std.standard.bit";
        element.enumeration_literals = source->enumeration_literals;
        element.enumeration_range = source->enumeration_range;
        element.enumeration_range_expression =
            source->enumeration_range_expression;
        element.enumeration_base_range = source->enumeration_base_range;
        element.enumeration_base_range_expression =
            source->enumeration_base_range_expression;
        frontend::VhdlArrayInfo array;
        array.index_subtype = "integer";
        array.element_spelling = element.spelling;
        array.element_domain = element.domain;
        array.flat_width = source->width();
        array.dimensions.push_back(std::move(dimension));
        array.element_types.push_back(std::move(element));
        result.vhdl_array = std::move(array);
        result.enumeration_literals.clear();
        result.enumeration_range.reset();
        result.enumeration_range_expression.reset();
        result.enumeration_base_range.reset();
        result.enumeration_base_range_expression.reset();
        return result;
    }

} // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_composite_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (language_ != frontend::Language::Vhdl2008
        || expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2) {
        return { };
    }

    if (expression.text == "&") {
        std::vector<const Expression*> concatenands;
        const auto collect = [&](const auto& self,
                                 const Expression& candidate) -> void {
            if (candidate.kind == ExpressionKind::Binary
                && candidate.text == "&"
                && candidate.operands.size() == 2) {
                self(self, candidate.operands[0]);
                self(self, candidate.operands[1]);
            } else {
                concatenands.push_back(&candidate);
            }
        };
        collect(collect, expression);
        std::optional<frontend::Type> contextual_target = bounded_array_context(expected_type);
        std::optional<frontend::Type> inferred_target;
        const frontend::Type* target = expected_type != nullptr && expected_type->vhdl_array
            ? expected_type
            : contextual_target ? &*contextual_target
                                : nullptr;
        if (target == nullptr) {
            for (const auto* operand : concatenands) {
                auto operand_type = vhdl_expression_type(*operand);
                if (operand_type && operand_type->vhdl_array) {
                    inferred_target = std::move(*operand_type);
                    target = &*inferred_target;
                    break;
                }
            }
        }
        if (target == nullptr || !target->vhdl_array
            || target->vhdl_array->dimensions.size() != 1
            || target->vhdl_array->element_types.size() != 1
            || target->vhdl_array->unconstrained) {
            report(
                "FSIM-ELAB-VHCOMPOP-003",
                "bounded VHDL concatenation requires a constrained "
                "one-dimensional array context or operand",
                expression.span);
            return std::nullopt;
        }
        const auto target_width = target->width();
        const auto& element = target->vhdl_array->element_types.front();
        const auto element_width = element.width();
        constexpr auto maximum_concatenation_width = std::numeric_limits<std::uint32_t>::max();
        if (!target_width || !element_width || *element_width == 0
            || *target_width > maximum_concatenation_width) {
            report(
                "FSIM-ELAB-VHCOMPOP-003",
                "VHDL concatenation context has no bounded executable "
                "element and result width",
                expression.span);
            return std::nullopt;
        }

        std::vector<RegisterId> operands;
        std::size_t result_width = 0;
        for (const auto* operand_pointer : concatenands) {
            const auto& operand_expression = *operand_pointer;
            auto operand_storage = vhdl_expression_type(operand_expression);
            auto operand_array = bounded_array_context(
                operand_storage ? &*operand_storage : nullptr);
            const frontend::Type* operand_type = operand_storage
                    && operand_storage->vhdl_array
                ? &*operand_storage
                : operand_array ? &*operand_array : nullptr;
            std::size_t operand_width = static_cast<std::size_t>(*element_width);
            const frontend::Type* operand_context = &element;
            if (operand_type != nullptr && operand_type->vhdl_array) {
                if (!common_composite_type(*target, *operand_type)
                    || operand_type->vhdl_array->dimensions.size() != 1) {
                    report(
                        "FSIM-ELAB-VHCOMPOP-003",
                        "VHDL concatenation array operands must share the "
                        "contextual array base and element profile",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto width = operand_type->width();
                if (!width || *width > maximum_concatenation_width) {
                    report(
                        "FSIM-ELAB-VHCOMPOP-003",
                        "a VHDL concatenation operand has no bounded executable "
                        "width",
                        operand_expression.span);
                    return std::nullopt;
                }
                operand_width = static_cast<std::size_t>(*width);
                operand_context = operand_type;
            } else {
                const auto inferred_width = infer_width(operand_expression);
                const bool array_value = inferred_width
                    && *inferred_width != *element_width;
                if (array_value) {
                    if (*inferred_width > maximum_concatenation_width
                        || *inferred_width % *element_width != 0
                        || !vhdl_expression_matches_type(
                            operand_expression, *target)) {
                        report(
                            "FSIM-ELAB-VHCOMPOP-003",
                            "a VHDL concatenation array operand must have the "
                            "contextual element profile and a bounded length",
                            operand_expression.span);
                        return std::nullopt;
                    }
                    operand_width = *inferred_width;
                    operand_context = nullptr;
                } else if (!vhdl_expression_matches_type(
                               operand_expression, element)) {
                    report(
                        "FSIM-ELAB-VHCOMPOP-003",
                        "a scalar VHDL concatenation operand must match the "
                        "contextual array element type",
                        operand_expression.span);
                    return std::nullopt;
                }
            }
            if (result_width > maximum_concatenation_width
                || operand_width > maximum_concatenation_width - result_width) {
                report(
                    "FSIM-ELAB-VHCOMPOP-003",
                    "VHDL concatenation result exceeds the SimIR width metadata "
                    "capacity",
                    expression.span);
                return std::nullopt;
            }
            auto value = lower_expression(
                operand_expression, operand_width, operand_context);
            if (!value || register_width(*value) != operand_width) {
                return std::nullopt;
            }
            result_width += operand_width;
            if (operand_width != 0) {
                operands.push_back(*value);
            }
        }
        if (result_width != *target_width
            || (expected_type != nullptr
                && result_width != expected_width)) {
            report(
                "FSIM-ELAB-VHCOMPOP-003",
                "VHDL concatenation result length does not match its "
                "constrained array context",
                expression.span);
            return std::nullopt;
        }
        if (operands.empty()) {
            const auto destination = allocate_register(0, target->domain);
            process_.operations.emplace_back(
                LoadConstant { destination, PackedLogic4 { } });
            return destination;
        }
        if (operands.size() == 1) {
            if (register_domain(operands.front()) != target->domain) {
                report(
                    "FSIM-ELAB-VHCOMPOP-003",
                    "VHDL concatenation would change the contextual state "
                    "domain",
                    expression.span);
                return std::nullopt;
            }
            return operands.front();
        }
        const auto destination = allocate_register(
            result_width, target->domain);
        process_.operations.emplace_back(Concatenate {
            destination, std::move(operands),
            static_cast<std::uint32_t>(result_width) });
        return destination;
    }

    const bool ordinary = expression.text == "="
        || expression.text == "/=";
    const bool matching = expression.text == "?="
        || expression.text == "?/=";
    if (!ordinary && !matching) {
        return { };
    }
    auto lhs_storage = vhdl_expression_type(expression.operands[0]);
    auto rhs_storage = vhdl_expression_type(expression.operands[1]);
    const bool lhs_composite = lhs_storage && is_composite(*lhs_storage);
    const bool rhs_composite = rhs_storage && is_composite(*rhs_storage);
    if (!lhs_composite && !rhs_composite) {
        return { };
    }
    const frontend::Type* context = lhs_composite
        ? &*lhs_storage
        : &*rhs_storage;
    if ((lhs_storage
            && (!lhs_composite
                || !common_composite_type(*context, *lhs_storage)))
        || (rhs_storage
            && (!rhs_composite
                || !common_composite_type(*context, *rhs_storage)))
        || (!lhs_storage
            && !vhdl_expression_matches_type(
                expression.operands[0], *context))
        || (!rhs_storage
            && !vhdl_expression_matches_type(
                expression.operands[1], *context))) {
        report(
            context->vhdl_array ? "FSIM-ELAB-VHARRAY-006"
                                : "FSIM-ELAB-VHCOMPOP-001",
            "VHDL composite comparison operands must share one nominal "
            "record or array base and element profile",
            expression.span);
        return std::nullopt;
    }
    if (matching && !matching_array_type(*context)) {
        report(
            "FSIM-ELAB-VHDLMATCH-004",
            "VHDL matching equality operands must be bit, std_ulogic, or "
            "one-dimensional arrays of those element types",
            expression.span);
        return std::nullopt;
    }

    const auto lhs_width = lhs_storage
        ? lhs_storage->width()
        : context->width();
    const auto rhs_width = rhs_storage
        ? rhs_storage->width()
        : context->width();
    if (!lhs_width || !rhs_width) {
        report(
            "FSIM-ELAB-VHCOMPOP-001",
            "VHDL composite comparison operands require bounded executable "
            "widths",
            expression.span);
        return std::nullopt;
    }
    const auto lhs = lower_expression(
        expression.operands[0], static_cast<std::size_t>(*lhs_width),
        lhs_storage ? &*lhs_storage : context);
    const auto rhs = lower_expression(
        expression.operands[1], static_cast<std::size_t>(*rhs_width),
        rhs_storage ? &*rhs_storage : context);
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    const bool invert = expression.text == "/="
        || expression.text == "?/=";
    if (*lhs_width == 0 && *rhs_width == 0) {
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4 { 1, invert ? Logic4::zero : Logic4::one } });
        return destination;
    }
    if (*lhs_width != *rhs_width) {
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4 { 1, invert ? Logic4::one : Logic4::zero } });
        return destination;
    }
    const auto compared = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary {
        matching ? BinaryOperator::vhdl_match_equal
                 : BinaryOperator::case_equal,
        compared, *lhs, *rhs });
    if (!invert) {
        return compared;
    }
    const auto destination = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(UnaryNot { destination, compared });
    return destination;
}

bool Lowerer::validate_vhdl_composite_assignment(
    const frontend::Type* target,
    const Expression& value)
{
    if (language_ != frontend::Language::Vhdl2008 || target == nullptr) {
        return true;
    }
    if (target->vhdl_access) {
        if (value.kind == ExpressionKind::Call
            && (value.text == "@vhdl-null"
                || value.text == "@vhdl-new"
                || value.text == "@vhdl-new-qualified")) {
            return true;
        }
        const auto source = vhdl_expression_type(value);
        if (source && source->vhdl_access
            && !target->nominal_type.empty()
            && target->nominal_type == source->nominal_type) {
            return true;
        }
        report(
            "FSIM-ELAB-VHACCESS-020",
            "VHDL access assignment requires the same nominal access type "
            "or null",
            value.span);
        return false;
    }
    if (!is_composite(*target)) {
        return true;
    }
    if (value.kind == ExpressionKind::Aggregate
        || value.kind == ExpressionKind::StringLiteral
        || (value.kind == ExpressionKind::Binary
            && value.text == "&")) {
        return true;
    }
    if (value.kind == ExpressionKind::Call && value.text == "?:"
        && value.operands.size() == 3) {
        return validate_vhdl_composite_assignment(target, value.operands[1])
            && validate_vhdl_composite_assignment(target, value.operands[2]);
    }
    if (value.kind == ExpressionKind::Call) {
        return true;
    }
    const auto source = vhdl_expression_type(value);
    const auto source_array = source
        ? bounded_array_context(&*source)
        : std::nullopt;
    const auto* comparable_source = source_array
        ? &*source_array
        : source ? &*source : nullptr;
    if ((comparable_source
            && assignment_compatible(*target, *comparable_source))
        || (!comparable_source
            && vhdl_expression_matches_type(value, *target))) {
        return true;
    }
    report(
        target->vhdl_array ? "FSIM-ELAB-VHARRAY-006"
                           : "FSIM-ELAB-VHCOMPOP-002",
        "assignment to VHDL composite type '" + target->spelling
            + "' requires the same nominal base, element profile, rank, "
              "and dimension lengths",
        value.span);
    return false;
}

} // namespace fsim::elaboration
