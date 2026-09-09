// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] bool systemverilog_same_type(
        const frontend::Type& lhs,
        const frontend::Type& rhs) noexcept
    {
        return frontend::systemverilog_types_equivalent(lhs, rhs);
    }

} // namespace

Lowerer::ExpressionAttempt Lowerer::lower_binary_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (language_ == frontend::Language::Vhdl2008) {
        auto environment = lower_vhdl_environment_binary_expression(
            expression, expected_type);
        if (environment.handled) {
            return environment;
        }
    }
    if (language_ == frontend::Language::SystemVerilog2017
        && expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2U
        && (expression.text == "=="
            || expression.text == "!="
            || expression.text == "==="
            || expression.text == "!==")
        && (expression.operands[0].text == "@sv-type"
            || expression.operands[1].text == "@sv-type")) {
        const auto& lhs = expression.operands[0];
        const auto& rhs = expression.operands[1];
        if (lhs.kind != ExpressionKind::Call
            || lhs.text != "@sv-type"
            || lhs.operands.size() != 1U
            || rhs.kind != ExpressionKind::Call
            || rhs.text != "@sv-type"
            || rhs.operands.size() != 1U) {
            report(
                "FSIM-ELAB-SVTYPE-006",
                "type equality requires type(expression) on both sides",
                expression.span);
            return std::nullopt;
        }
        const auto* lhs_type = systemverilog_expression_type(
            lhs.operands.front());
        const auto* rhs_type = systemverilog_expression_type(
            rhs.operands.front());
        if (lhs_type == nullptr || rhs_type == nullptr) {
            report(
                "FSIM-ELAB-SVTYPE-006",
                "type equality requires statically typed operands",
                expression.span);
            return std::nullopt;
        }
        auto equal = systemverilog_same_type(*lhs_type, *rhs_type);
        if (expression.text == "!=" || expression.text == "!==") {
            equal = !equal;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(equal ? 1U : 0U, 1U) });
        return destination;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2) {
        const auto found = function_indices_.find(expression.text);
        if (found != function_indices_.end()) {
            Expression call {
                ExpressionKind::Call,
                expression.text,
                expression.operands,
                expression.span
            };
            const bool matching_user_operator = std::ranges::any_of(
                found->second,
                [&](const std::size_t index) {
                    return vhdl_function_profile_matches(
                        call,
                        *function_frames_[index].source,
                        expected_type);
                });
            if (matching_user_operator) {
                return lower_user_function_expression(
                    call, expected_width, expected_type);
            }
        }
    }
    auto vhdl_composite = lower_vhdl_composite_expression(
        expression, expected_width, expected_type);
    if (vhdl_composite.handled) {
        return vhdl_composite;
    }
    const bool string_lhs = expression.operands.size() == 2
        && is_string_expression(expression.operands[0]);
    const bool string_rhs = expression.operands.size() == 2
        && is_string_expression(expression.operands[1]);
    const bool string_equality = language_ == frontend::Language::Vhdl2008
        ? string_lhs && string_rhs
        : string_lhs || string_rhs;
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2
        && (is_container_expression(expression.operands[0])
            || is_container_expression(expression.operands[1]))) {
        const bool supported = expression.text == "=="
            || expression.text == "!="
            || expression.text == "==="
            || expression.text == "!==";
        if (language_ != frontend::Language::SystemVerilog2017
            || !supported) {
            report(
                "FSIM-ELAB-SVEQUAL-001",
                "bounded whole-container comparison supports only "
                "SystemVerilog ==, !=, ===, and !==",
                expression.span);
            return std::nullopt;
        }
        if (!is_container_expression(expression.operands[0])
            || !is_container_expression(expression.operands[1])) {
            report(
                "FSIM-ELAB-SVEQUAL-002",
                "whole-container equality requires two typed "
                "container operands",
                expression.span);
            return std::nullopt;
        }
        const auto lhs_type = container_expression_runtime_type(
            expression.operands[0]);
        const auto rhs_type = container_expression_runtime_type(
            expression.operands[1]);
        if (!lhs_type || !rhs_type) {
            return std::nullopt;
        }
        if (*lhs_type != *rhs_type) {
            report(
                "FSIM-ELAB-SVEQUAL-003",
                "whole-container equality requires an exactly "
                "compatible kind and profile",
                expression.span);
            return std::nullopt;
        }
        const auto lhs = lower_container_expression(
            expression.operands[0]);
        const auto rhs = lower_container_expression(
            expression.operands[1]);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const bool case_equal = expression.text == "==="
            || expression.text == "!==";
        const auto result_domain = case_equal || lhs_type->two_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        const auto destination = allocate_register(1, result_domain);
        process_.operations.emplace_back(
            CompareContainers {
                destination, *lhs, *rhs, case_equal });
        if (expression.text == "!="
            || expression.text == "!==") {
            const auto inverted = allocate_register(1, result_domain);
            process_.operations.emplace_back(
                UnaryNot { inverted, destination });
            return inverted;
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2
        && (expression.text == "=="
            || expression.text == "!="
            || (language_ == frontend::Language::Vhdl2008
                && (expression.text == "="
                    || expression.text == "/=")))
        && string_equality) {
        if (!string_lhs || !string_rhs) {
            report(
                "FSIM-ELAB-SVSTRING-012",
                "string equality requires two string operands",
                expression.span);
            return std::nullopt;
        }
        const auto lhs = lower_string_expression(expression.operands[0]);
        const auto rhs = lower_string_expression(expression.operands[1]);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            CompareStrings {
                destination,
                *lhs,
                *rhs,
                expression.text == "!=" || expression.text == "/=" });
        return destination;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2
        && (expression.text == "&&"
            || expression.text == "||")) {
        const auto lhs_width = infer_width(expression.operands[0])
                                   .value_or(expected_width);
        const auto rhs_width = infer_width(expression.operands[1])
                                   .value_or(expected_width);
        const auto lhs = lower_expression(expression.operands[0], lhs_width);
        if (!lhs) {
            return std::nullopt;
        }

        // A definite controlling value bypasses the complete right-hand
        // graph. An ambiguous X/Z truth value follows the right-hand path
        // so LogicalBinary can retain the language's four-state merge.
        const auto truth_domain = is_two_state_domain(register_domain(*lhs))
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        auto controlling = allocate_register(1, truth_domain);
        process_.operations.emplace_back(
            LogicalNot { controlling, *lhs });
        if (expression.text == "||") {
            const auto normalized = allocate_register(1, truth_domain);
            process_.operations.emplace_back(
                LogicalNot { normalized, controlling });
            controlling = normalized;
        }
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            controlling,
            0,
            branch + 1,
            UnknownBranchPolicy::when_false });

        const auto rhs = lower_expression(expression.operands[1], rhs_width);
        if (!rhs) {
            return std::nullopt;
        }
        const auto result_domain = is_two_state_domain(register_domain(*lhs))
                && is_two_state_domain(register_domain(*rhs))
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        const auto destination = allocate_register(1, result_domain);
        process_.operations.emplace_back(LogicalBinary {
            expression.text == "&&"
                ? LogicalBinaryOperator::logical_and
                : LogicalBinaryOperator::logical_or,
            destination,
            *lhs,
            *rhs });
        const auto exit = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0 });
        const auto controlled = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(
                expression.text == "||" ? 1U : 0U, 1) });
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            controlling,
            controlled,
            branch + 1,
            UnknownBranchPolicy::when_false
        };
        process_.operations[exit] = Jump { end };
        return destination;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2
        && (expression.text == "<<"
            || expression.text == ">>"
            || expression.text == "<<<"
            || expression.text == ">>>"
            || expression.text == "sll"
            || expression.text == "srl"
            || expression.text == "sla"
            || expression.text == "sra"
            || expression.text == "rol"
            || expression.text == "ror")) {
        auto effective_operator = expression.text;
        std::optional<std::uint64_t> static_amount;
        if (language_ == frontend::Language::Vhdl2008) {
            const auto count = constant_index(expression.operands[1]);
            if (count) {
                static_amount = index_distance(*count, 0);
            }
            if (count && *count < 0) {
                if (effective_operator == "sll") {
                    effective_operator = "srl";
                } else if (effective_operator == "srl") {
                    effective_operator = "sll";
                } else if (effective_operator == "sla") {
                    effective_operator = "sra";
                } else if (effective_operator == "sra") {
                    effective_operator = "sla";
                } else if (effective_operator == "rol") {
                    effective_operator = "ror";
                } else if (effective_operator == "ror") {
                    effective_operator = "rol";
                }
            }
        }
        const auto value_width = language_ == frontend::Language::SystemVerilog2017
            ? std::max(
                  expected_width,
                  infer_width(expression.operands[0])
                      .value_or(expected_width))
            : infer_width(expression.operands[0])
                  .value_or(expected_width);
        auto amount_width = infer_width(expression.operands[1])
                                .value_or(expected_width);
        if (static_amount) {
            auto magnitude = *static_amount;
            std::size_t required_width = 1;
            while (magnitude > 1) {
                ++required_width;
                magnitude >>= 1U;
            }
            amount_width = std::max(amount_width, required_width);
        }
        auto value = lower_expression(
            expression.operands[0], value_width);
        std::optional<RegisterId> amount;
        if (static_amount) {
            amount = allocate_register(
                amount_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                *amount,
                unsigned_value(*static_amount, amount_width) });
        } else {
            amount = lower_expression(
                expression.operands[1], amount_width);
        }
        if (!value || !amount) {
            return std::nullopt;
        }
        if (language_
                == frontend::Language::SystemVerilog2017
            && register_width(*value) != value_width) {
            *value = resize_register(
                *value,
                value_width,
                is_signed_expression(expression.operands[0]));
        }
        const bool signed_amount = language_ == frontend::Language::Vhdl2008
            && !static_amount.has_value();
        if (signed_amount
            && register_domain(*amount)
                != frontend::ValueDomain::Integer) {
            report(
                "FSIM-ELAB-070",
                "a dynamic VHDL packed shift or rotate count must "
                "have the base integer subtype",
                expression.operands[1].span);
            return std::nullopt;
        }
        const auto value_domain = register_domain(*value);
        const auto result_domain = value_domain == frontend::ValueDomain::Logic9
            ? frontend::ValueDomain::Logic9
            : is_two_state_domain(value_domain)
                && is_two_state_domain(register_domain(*amount))
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        const auto destination = allocate_register(
            register_width(*value), result_domain);
        process_.operations.emplace_back(Shift {
            effective_operator == ">>"
                    || effective_operator == "srl"
                    || (effective_operator == ">>>"
                        && !is_signed_expression(
                            expression.operands[0]))
                ? ShiftOperator::logical_right
                : effective_operator == ">>>"
                    || effective_operator == "sra"
                ? ShiftOperator::arithmetic_right
                : effective_operator == "sla"
                ? ShiftOperator::arithmetic_left
                : effective_operator == "rol"
                ? ShiftOperator::rotate_left
                : effective_operator == "ror"
                ? ShiftOperator::rotate_right
                : ShiftOperator::logical_left,
            destination,
            *value,
            *amount,
            signed_amount });
        return destination;
    }
    if (expression.kind == ExpressionKind::Binary && expression.operands.size() == 2) {
        const frontend::Type* binary_context_type = nullptr;
        const auto expression_object_type =
            [&](const Expression& operand)
            -> const frontend::Type* {
            if (operand.kind == ExpressionKind::Identifier) {
                return object_type(operand.text);
            }
            if ((operand.kind == ExpressionKind::Index
                    || operand.kind == ExpressionKind::Slice)
                && !operand.operands.empty()
                && operand.operands[0].kind
                    == ExpressionKind::Identifier) {
                return object_type(
                    operand.operands[0].text);
            }
            return nullptr;
        };
        const frontend::Type* lhs_object_type = expression_object_type(
            expression.operands[0]);
        const frontend::Type* rhs_object_type = expression_object_type(
            expression.operands[1]);
        auto lhs_vhdl_type = language_ == frontend::Language::Vhdl2008
            ? vhdl_expression_type(expression.operands[0])
            : std::optional<frontend::Type>{};
        auto rhs_vhdl_type = language_ == frontend::Language::Vhdl2008
            ? vhdl_expression_type(expression.operands[1])
            : std::optional<frontend::Type>{};
        const auto constrain_builtin_result = [&](
            std::optional<frontend::Type>& type,
            const Expression& operand,
            const std::optional<std::size_t> contextual_width) {
          if (!type || type->vhdl_array || type->packed_range) {
            return;
          }
          const auto separator = type->spelling.find_last_of('.');
          const auto name = std::string_view{type->spelling}.substr(
              separator == std::string::npos ? 0U : separator + 1U);
          const bool builtin_array = name == "bit_vector"
              || name == "std_logic_vector"
              || name == "std_ulogic_vector" || name == "signed"
              || name == "unsigned";
          const auto width = builtin_array
              ? contextual_width
                    ? contextual_width
                    : infer_width(operand)
              : std::nullopt;
          if (width && *width != 0U
              && *width - 1U
                  <= static_cast<std::size_t>(
                      std::numeric_limits<std::int64_t>::max())) {
            type->packed_range = frontend::PackedRange{
                static_cast<std::int64_t>(*width - 1U), 0, true};
          }
        };
        constrain_builtin_result(
            lhs_vhdl_type,
            expression.operands[0],
            expression.operands[1].kind == ExpressionKind::Aggregate
                ? infer_width(expression.operands[1])
                : std::nullopt);
        constrain_builtin_result(
            rhs_vhdl_type,
            expression.operands[1],
            expression.operands[0].kind == ExpressionKind::Aggregate
                ? infer_width(expression.operands[0])
                : std::nullopt);
        const auto* lhs_value_type = lhs_object_type != nullptr
            ? lhs_object_type
            : lhs_vhdl_type ? &*lhs_vhdl_type : nullptr;
        const auto* rhs_value_type = rhs_object_type != nullptr
            ? rhs_object_type
            : rhs_vhdl_type ? &*rhs_vhdl_type : nullptr;
        const auto* lhs_nominal_type = systemverilog_expression_type(
            expression.operands[0]);
        const auto* rhs_nominal_type = systemverilog_expression_type(
            expression.operands[1]);
        const auto scalar_type = [&](
                                     const Expression& operand,
                                     frontend::Type& inferred)
            -> const frontend::Type* {
            if (const auto* direct = expression_object_type(operand);
                direct != nullptr
                && direct->systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None) {
                return direct;
            }
            if (operand.kind == ExpressionKind::Call) {
                const auto* function = visible_function(operand.text);
                if (function != nullptr
                    && function->return_type.systemverilog_scalar
                        != frontend::SystemVerilogScalarKind::None) {
                    return &function->return_type;
                }
            }
            if (operand.systemverilog_scalar_kind
                != frontend::SystemVerilogScalarKind::None) {
                const auto width = operand.systemverilog_scalar_kind
                        == frontend::SystemVerilogScalarKind::ShortReal
                    ? std::int64_t { 32 }
                    : std::int64_t { 64 };
                inferred.spelling = operand.systemverilog_scalar_kind
                        == frontend::SystemVerilogScalarKind::ShortReal
                    ? "shortreal"
                    : operand.systemverilog_scalar_kind
                        == frontend::SystemVerilogScalarKind::Realtime
                    ? "realtime"
                    : "real";
                inferred.domain = frontend::ValueDomain::Bit2;
                inferred.packed_range = frontend::PackedRange {
                    width - 1, 0, true
                };
                inferred.systemverilog_scalar
                    = operand.systemverilog_scalar_kind;
                return &inferred;
            }
            if (language_ == frontend::Language::Vhdl2008) {
                const auto type = vhdl_expression_type(operand);
                if (type && type->systemverilog_scalar
                        != frontend::SystemVerilogScalarKind::None) {
                    inferred = *type;
                    return &inferred;
                }
            }
            return nullptr;
        };
        frontend::Type inferred_lhs_scalar_type;
        frontend::Type inferred_rhs_scalar_type;
        const auto* lhs_scalar_type = scalar_type(
            expression.operands[0], inferred_lhs_scalar_type);
        const auto* rhs_scalar_type = scalar_type(
            expression.operands[1], inferred_rhs_scalar_type);
        const auto* scalar_context = expected_type != nullptr
                && expected_type->systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None
            ? expected_type
            : lhs_scalar_type != nullptr
            ? lhs_scalar_type
            : rhs_scalar_type;
        if ((language_ == frontend::Language::SystemVerilog2017
                || language_ == frontend::Language::Vhdl2008)
            && scalar_context != nullptr) {
            using ScalarOperator = runtime::SystemVerilogScalarBinaryOperator;
            std::optional<ScalarOperator> scalar_operation;
            if (expression.text == "+")
                scalar_operation = ScalarOperator::Add;
            else if (expression.text == "-")
                scalar_operation = ScalarOperator::Subtract;
            else if (expression.text == "*")
                scalar_operation = ScalarOperator::Multiply;
            else if (expression.text == "/")
                scalar_operation = ScalarOperator::Divide;
            else if (expression.text == "==" || expression.text == "==="
                || expression.text == "=") {
                scalar_operation = ScalarOperator::Equal;
            } else if (expression.text == "!=" || expression.text == "!=="
                || expression.text == "/=") {
                scalar_operation = ScalarOperator::NotEqual;
            } else if (expression.text == "<")
                scalar_operation = ScalarOperator::Less;
            else if (expression.text == "<=")
                scalar_operation = ScalarOperator::LessEqual;
            else if (expression.text == ">")
                scalar_operation = ScalarOperator::Greater;
            else if (expression.text == ">=")
                scalar_operation = ScalarOperator::GreaterEqual;
            if (!scalar_operation) {
                report(
                    "FSIM-ELAB-SVSCALAR-002",
                    "runtime scalar operator '" + expression.text
                        + "' is outside the executable arithmetic and "
                          "comparison subset",
                    expression.span);
                return std::nullopt;
            }
            const auto kind = [&](const Expression& operand,
                                  const frontend::Type* type) {
                return type != nullptr
                    ? type->systemverilog_scalar
                    : operand.systemverilog_scalar_kind
                        != frontend::SystemVerilogScalarKind::None
                    ? operand.systemverilog_scalar_kind
                    : scalar_context->systemverilog_scalar;
            };
            const auto lhs_kind = kind(
                expression.operands[0], lhs_scalar_type);
            const auto rhs_kind = kind(
                expression.operands[1], rhs_scalar_type);
            const auto width = [](const auto selected) {
                return selected == frontend::SystemVerilogScalarKind::ShortReal
                    ? std::size_t { 32 }
                    : std::size_t { 64 };
            };
            auto lhs = lower_expression(
                expression.operands[0], width(lhs_kind),
                lhs_scalar_type != nullptr
                    ? lhs_scalar_type
                    : scalar_context);
            auto rhs = lower_expression(
                expression.operands[1], width(rhs_kind),
                rhs_scalar_type != nullptr
                    ? rhs_scalar_type
                    : scalar_context);
            if (!lhs || !rhs)
                return std::nullopt;
            const bool comparison = *scalar_operation >= ScalarOperator::Equal;
            auto result_kind = expression.systemverilog_scalar_kind;
            if (!comparison
                && result_kind == frontend::SystemVerilogScalarKind::None) {
                result_kind = scalar_context->systemverilog_scalar;
            }
            const auto destination = allocate_register(
                comparison ? 1U : width(result_kind),
                comparison
                        && language_ == frontend::Language::Vhdl2008
                    ? frontend::ValueDomain::Boolean
                    : frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                *scalar_operation, destination, *lhs, *rhs,
                lhs_kind, rhs_kind,
                comparison
                    ? frontend::SystemVerilogScalarKind::None
                    : result_kind });
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const auto* physical_context = expected_type != nullptr
                    && expected_type->vhdl_physical
                ? expected_type
                : lhs_object_type != nullptr
                    && lhs_object_type->vhdl_physical
                ? lhs_object_type
                : rhs_object_type != nullptr
                    && rhs_object_type->vhdl_physical
                ? rhs_object_type
                : nullptr;
            if (physical_context != nullptr) {
                const bool supported = expression.text == "+"
                    || expression.text == "-"
                    || expression.text == "*"
                    || expression.text == "/"
                    || expression.text == "="
                    || expression.text == "/="
                    || expression.text == "<"
                    || expression.text == "<="
                    || expression.text == ">"
                    || expression.text == ">=";
                if (!supported) {
                    report(
                        "FSIM-ELAB-VHPHYSICAL-007",
                        "operator '" + expression.text
                            + "' is not supported for bounded physical "
                              "values",
                        expression.span);
                    return std::nullopt;
                }
                if (lhs_object_type != nullptr
                    && lhs_object_type->vhdl_physical
                    && rhs_object_type != nullptr
                    && rhs_object_type->vhdl_physical
                    && lhs_object_type->nominal_type
                        != rhs_object_type->nominal_type) {
                    report(
                        "FSIM-ELAB-VHPHYSICAL-008",
                        "physical operands require the same nominal "
                        "type",
                        expression.span);
                    return std::nullopt;
                }
                binary_context_type = physical_context;
            }
        }
        if (language_ == frontend::Language::SystemVerilog2017
            && (expression.text == "=="
                || expression.text == "!="
                || expression.text == "==="
                || expression.text == "!==")
            && ((lhs_nominal_type != nullptr
                    && is_systemverilog_nominal_packed_type(
                        *lhs_nominal_type))
                || (rhs_nominal_type != nullptr
                    && is_systemverilog_nominal_packed_type(
                        *rhs_nominal_type)))
            && (lhs_nominal_type == nullptr
                || rhs_nominal_type == nullptr
                || !is_systemverilog_nominal_packed_type(
                    *lhs_nominal_type)
                || !is_systemverilog_nominal_packed_type(
                    *rhs_nominal_type)
                || lhs_nominal_type->nominal_type
                    != rhs_nominal_type->nominal_type)) {
            report(
                "FSIM-ELAB-SVTYPE-005",
                "nominal packed equality requires two values of the same "
                "nominal SystemVerilog type",
                expression.span);
            return std::nullopt;
        }
        const frontend::Type* lhs_enumeration_type = enumeration_expression_type(
            expression.operands[0]);
        const frontend::Type* rhs_enumeration_type = enumeration_expression_type(
            expression.operands[1]);
        if (lhs_enumeration_type == nullptr && lhs_vhdl_type
            && !lhs_vhdl_type->enumeration_literals.empty()) {
            lhs_enumeration_type = &*lhs_vhdl_type;
        }
        if (rhs_enumeration_type == nullptr && rhs_vhdl_type
            && !rhs_vhdl_type->enumeration_literals.empty()) {
            rhs_enumeration_type = &*rhs_vhdl_type;
        }
        if (language_ == frontend::Language::Vhdl2008
            && (expression.text == "="
                || expression.text == "/="
                || expression.text == "?="
                || expression.text == "?/="
                || expression.text == "<"
                || expression.text == "<="
                || expression.text == ">"
                || expression.text == ">=")) {
            if (lhs_enumeration_type != nullptr) {
                binary_context_type = lhs_enumeration_type;
            } else if (rhs_enumeration_type != nullptr) {
                binary_context_type = rhs_enumeration_type;
            } else if (
                expression.text == "="
                || expression.text == "/="
                || expression.text == "?="
                || expression.text == "?/=") {
                if (expression.operands[0].kind
                    == ExpressionKind::Aggregate) {
                    binary_context_type = rhs_value_type;
                } else if (
                    expression.operands[1].kind
                    == ExpressionKind::Aggregate) {
                    binary_context_type = lhs_value_type;
                }
                if (binary_context_type != nullptr
                    && binary_context_type->packed_members.empty()
                    && !binary_context_type->vhdl_array
                    && !binary_context_type->packed_range) {
                    binary_context_type = nullptr;
                }
            }
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const bool lhs_array = lhs_object_type != nullptr
                && lhs_object_type->vhdl_array.has_value();
            const bool rhs_array = rhs_object_type != nullptr
                && rhs_object_type->vhdl_array.has_value();
            if (lhs_array || rhs_array) {
                if (expression.text != "="
                    && expression.text != "/="
                    && expression.text != "?=") {
                    report(
                        "FSIM-ELAB-VHARRAY-007",
                        "operator '" + expression.text
                            + "' is not implemented for VHDL array "
                              "values",
                        expression.span);
                    return std::nullopt;
                }
                if (lhs_object_type != nullptr
                    && rhs_object_type != nullptr
                    && (!lhs_array || !rhs_array
                        || lhs_object_type->nominal_type
                            != rhs_object_type->nominal_type)) {
                    report(
                        "FSIM-ELAB-VHARRAY-006",
                        "VHDL array values require the same nominal "
                        "type in equality expressions",
                        expression.span);
                    return std::nullopt;
                }
                binary_context_type = lhs_array
                    ? lhs_object_type
                    : rhs_object_type;
                const auto lhs_type = vhdl_expression_type(expression.operands[0]);
                const auto rhs_type = vhdl_expression_type(expression.operands[1]);
                const auto lhs_width = lhs_type
                    ? lhs_type->width()
                    : std::nullopt;
                const auto rhs_width = rhs_type
                    ? rhs_type->width()
                    : std::nullopt;
                if (lhs_width && rhs_width
                    && (*lhs_width == 0 || *rhs_width == 0)) {
                    const bool equal = *lhs_width == 0 && *rhs_width == 0;
                    const bool result = expression.text == "/="
                        ? !equal
                        : equal;
                    const auto destination = allocate_register(
                        1, frontend::ValueDomain::Boolean);
                    process_.operations.emplace_back(LoadConstant {
                        destination,
                        PackedLogic4 {
                            1,
                            result ? Logic4::one
                                   : Logic4::zero } });
                    return destination;
                }
            }
        }
        const bool lhs_enumeration = lhs_enumeration_type != nullptr;
        const bool rhs_enumeration = rhs_enumeration_type != nullptr;
        const bool comparison = expression.text == "="
            || expression.text == "/="
            || expression.text == "?="
            || expression.text == "?/="
            || expression.text == "<"
            || expression.text == "<="
            || expression.text == ">"
            || expression.text == ">=";
        if (language_ == frontend::Language::Vhdl2008
            && (lhs_enumeration || rhs_enumeration)
            && !comparison) {
            report(
                "FSIM-ELAB-VHENUM-003",
                "operator '" + expression.text
                    + "' is not defined for VHDL enumeration values",
                expression.span);
            return std::nullopt;
        }
        const auto inferred_lhs_width = infer_width(expression.operands[0])
                                            .value_or(expected_width);
        const auto inferred_rhs_width = infer_width(expression.operands[1])
                                            .value_or(expected_width);
        const bool scalar_result_operator = expression.text == "="
            || expression.text == "/="
            || expression.text == "?="
            || expression.text == "?/="
            || expression.text == "=="
            || expression.text == "!="
            || expression.text == "==="
            || expression.text == "!=="
            || expression.text == "==?"
            || expression.text == "!=?"
            || expression.text == "<"
            || expression.text == "<="
            || expression.text == ">"
            || expression.text == ">=";
        const bool systemverilog_power = language_
                == frontend::Language::SystemVerilog2017
            && expression.text == "**";
        const auto inferred_width = language_ == frontend::Language::Vhdl2008
            ? infer_width(expression).value_or(expected_width)
            : systemverilog_power
            ? std::max(expected_width, inferred_lhs_width)
            : std::max(
                  scalar_result_operator
                      ? std::size_t { 0 }
                      : expected_width,
                  std::max(
                      inferred_lhs_width,
                      inferred_rhs_width));
        auto width = inferred_width;
        if (binary_context_type != nullptr) {
            const auto contextual_width = binary_context_type->width();
            if (contextual_width.has_value()) {
                width = static_cast<std::size_t>(
                    contextual_width.value());
            }
        }
        const auto operation_width = systemverilog_power
            ? std::max(width, inferred_rhs_width)
            : width;
        if (language_ == frontend::Language::Vhdl2008
            && expression.text == "**") {
            const auto exponent = constant_index(expression.operands[1]);
            if (!exponent || *exponent < 0) {
                report(
                    "FSIM-ELAB-091",
                    "bounded VHDL integer exponentiation requires "
                    "a locally static nonnegative exponent",
                    expression.operands[1].span);
                return std::nullopt;
            }
        }
        auto lhs = lower_expression(
            expression.operands[0],
            width,
            binary_context_type);
        auto rhs = lower_expression(
            expression.operands[1],
            systemverilog_power
                ? inferred_rhs_width
                : width,
            binary_context_type);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto direct_nonmatching_domain =
            [](const Expression& operand,
                const frontend::Type* type) {
                return operand.kind == ExpressionKind::IntegerLiteral
                    || operand.kind == ExpressionKind::BooleanLiteral
                    || (type != nullptr
                        && type->domain
                            != frontend::ValueDomain::Bit2
                        && type->domain
                            != frontend::ValueDomain::Logic9);
            };
        if (language_ == frontend::Language::Vhdl2008
            && (expression.text == "?="
                || expression.text == "?/=")
            && (direct_nonmatching_domain(
                    expression.operands[0], lhs_object_type)
                || direct_nonmatching_domain(
                    expression.operands[1], rhs_object_type)
                || (register_domain(*lhs)
                        != frontend::ValueDomain::Bit2
                    && register_domain(*lhs)
                        != frontend::ValueDomain::Logic9)
                || (register_domain(*rhs)
                        != frontend::ValueDomain::Bit2
                    && register_domain(*rhs)
                        != frontend::ValueDomain::Logic9))) {
            report(
                "FSIM-ELAB-VHDLMATCH-004",
                "VHDL matching equality operands must be bit, "
                "std_ulogic, or one-dimensional arrays of those "
                "element types",
                expression.span);
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const bool lhs_integer = is_integer_expression(
                expression.operands[0]);
            const bool rhs_integer = is_integer_expression(
                expression.operands[1]);
            const auto contextualize_integer =
                [&](std::optional<RegisterId>& integer,
                    const RegisterId vector,
                    const Expression& vector_expression) {
                  const auto target_width = register_width(vector);
                  *integer = resize_register(
                      *integer,
                      target_width,
                      is_signed_expression(vector_expression));
                  const auto target_domain = register_domain(vector);
                  if (register_domain(*integer) != target_domain) {
                      const auto converted = allocate_register(
                          target_width, target_domain);
                      process_.operations.emplace_back(
                          CopyRegister{converted, *integer});
                      *integer = converted;
                  }
                };
            if (lhs_integer && !rhs_integer) {
                contextualize_integer(
                    lhs, *rhs, expression.operands[1]);
            } else if (rhs_integer && !lhs_integer) {
                contextualize_integer(
                    rhs, *lhs, expression.operands[0]);
            }
        }
        if (language_ != frontend::Language::Vhdl2008) {
            const bool common_signed = is_signed_expression(expression.operands[0])
                && is_signed_expression(expression.operands[1]);
            *lhs = resize_register(
                *lhs, operation_width, common_signed);
            *rhs = resize_register(
                *rhs, operation_width, common_signed);
        }
        if (register_width(*lhs) != register_width(*rhs)) {
            report(
                "FSIM-ELAB-049",
                "binary operator operands have different widths ("
                    + std::to_string(register_width(*lhs)) + " and "
                    + std::to_string(register_width(*rhs))
                    + "); implicit sizing is not executable in this slice",
                expression.span);
            return std::nullopt;
        }
        std::optional<BinaryOperator> operation;
        bool invert_result = false;
        if (expression.text == "&" || expression.text == "and"
            || expression.text == "nand") {
            operation = BinaryOperator::bit_and;
            invert_result = expression.text == "nand";
        } else if (expression.text == "|" || expression.text == "or"
            || expression.text == "nor") {
            operation = BinaryOperator::bit_or;
            invert_result = expression.text == "nor";
        } else if (expression.text == "^" || expression.text == "xor"
            || expression.text == "xnor"
            || expression.text == "~^"
            || expression.text == "^~") {
            operation = BinaryOperator::bit_xor;
            invert_result = expression.text == "xnor"
                || expression.text == "~^"
                || expression.text == "^~";
        } else if (expression.text == "+") {
            operation = BinaryOperator::add_unsigned;
        } else if (expression.text == "-") {
            operation = BinaryOperator::subtract_unsigned;
        } else if (expression.text == "*") {
            operation = BinaryOperator::multiply_unsigned;
        } else if (expression.text == "**") {
            operation = BinaryOperator::power_unsigned;
        } else if (expression.text == "/") {
            operation = BinaryOperator::divide_unsigned;
        } else if (
            language_ != frontend::Language::Vhdl2008
            && expression.text == "%") {
            operation = BinaryOperator::modulo_unsigned;
        } else if (
            language_ == frontend::Language::Vhdl2008
            && (expression.text == "mod"
                || expression.text == "rem")) {
            operation = BinaryOperator::modulo_unsigned;
        } else if (
            expression.text == "=" || expression.text == "==") {
            operation = language_ == frontend::Language::Vhdl2008
                ? BinaryOperator::case_equal
                : BinaryOperator::equal;
        } else if (
            language_ != frontend::Language::Vhdl2008
            && (expression.text == "==="
                || expression.text == "!==")) {
            operation = BinaryOperator::case_equal;
            invert_result = expression.text == "!==";
        } else if (
            language_ == frontend::Language::SystemVerilog2017
            && (expression.text == "==?"
                || expression.text == "!=?")) {
            operation = BinaryOperator::wildcard_equal;
            invert_result = expression.text == "!=?";
        } else if (
            language_ == frontend::Language::Vhdl2008
            && (expression.text == "?="
                || expression.text == "?/=")) {
            operation = BinaryOperator::vhdl_match_equal;
            invert_result = expression.text == "?/=";
        } else if (
            language_ != frontend::Language::Vhdl2008
            && expression.text == "!=") {
            operation = BinaryOperator::not_equal;
        } else if (
            language_ == frontend::Language::Vhdl2008
            && expression.text == "/=") {
            operation = BinaryOperator::case_equal;
            invert_result = true;
        } else if (expression.text == "<") {
            operation = BinaryOperator::less_unsigned;
        } else if (expression.text == "<=") {
            operation = BinaryOperator::less_equal_unsigned;
        } else if (expression.text == ">") {
            operation = BinaryOperator::greater_unsigned;
        } else if (expression.text == ">=") {
            operation = BinaryOperator::greater_equal_unsigned;
        }
        if (!operation) {
            report(
                "FSIM-ELAB-042",
                "operator '" + expression.text + "' is parsed but not executable yet",
                expression.span);
            return std::nullopt;
        }
        const auto relational = *operation == BinaryOperator::less_unsigned
            || *operation
                == BinaryOperator::less_equal_unsigned
            || *operation == BinaryOperator::greater_unsigned
            || *operation
                == BinaryOperator::greater_equal_unsigned;
        const auto numeric_array_type = [](const frontend::Type* type) {
          if (type == nullptr) {
            return false;
          }
          const auto separator = type->spelling.find_last_of('.');
          const auto name = std::string_view{type->spelling}.substr(
              separator == std::string::npos ? 0U : separator + 1U);
          return name == "signed" || name == "unsigned";
        };
        const bool vhdl_numeric_relational =
            language_ == frontend::Language::Vhdl2008
            && relational
            && (numeric_array_type(lhs_value_type)
                || numeric_array_type(rhs_value_type));
        const auto arithmetic = *operation == BinaryOperator::add_unsigned
            || *operation == BinaryOperator::subtract_unsigned
            || *operation == BinaryOperator::multiply_unsigned
            || *operation == BinaryOperator::power_unsigned
            || *operation == BinaryOperator::divide_unsigned
            || *operation == BinaryOperator::modulo_unsigned;
        const bool synopsys_overload = arithmetic || relational
            || expression.text == "=" || expression.text == "/=";
        if (language_ == frontend::Language::Vhdl2008
            && vhdl_synopsys_signed_visible_
            && vhdl_synopsys_unsigned_visible_ && synopsys_overload
            && (is_synopsys_std_logic_vector_expression(
                    expression.operands[0])
                || is_synopsys_std_logic_vector_expression(
                    expression.operands[1]))) {
            report(
                "FSIM-ELAB-VHSYN-001",
                "std_logic_signed and std_logic_unsigned expose conflicting "
                "std_logic_vector overloads; remove one use clause or add "
                "an explicit numeric conversion",
                expression.span);
            return std::nullopt;
        }
        const bool lhs_signed = is_signed_expression(expression.operands[0]);
        const bool rhs_signed = is_signed_expression(expression.operands[1]);
        const bool signed_operation = lhs_signed && rhs_signed;
        const bool contextual_integer = is_integer_expression(
                expression.operands[0])
            || is_integer_expression(expression.operands[1]);
        if (language_ == frontend::Language::Vhdl2008
            && (arithmetic || relational)
            && lhs_signed != rhs_signed
            && !contextual_integer) {
            report(
                relational ? "FSIM-ELAB-066"
                           : "FSIM-ELAB-067",
                "mixed signed/unsigned VHDL operands require an "
                "explicit conversion",
                expression.span);
            return std::nullopt;
        }
        if (signed_operation) {
            if (*operation == BinaryOperator::add_unsigned) {
                operation = BinaryOperator::add_signed;
            } else if (
                *operation
                == BinaryOperator::subtract_unsigned) {
                operation = BinaryOperator::subtract_signed;
            } else if (
                *operation
                == BinaryOperator::multiply_unsigned) {
                operation = BinaryOperator::multiply_signed;
            } else if (
                *operation
                == BinaryOperator::power_unsigned) {
                operation = BinaryOperator::power_signed;
            } else if (
                *operation
                == BinaryOperator::divide_unsigned) {
                operation = BinaryOperator::divide_signed;
            } else if (
                *operation
                == BinaryOperator::modulo_unsigned) {
                operation = language_
                            == frontend::Language::Vhdl2008
                        && expression.text == "mod"
                    ? BinaryOperator::modulo_signed
                    : BinaryOperator::remainder_signed;
            } else if (
                *operation
                == BinaryOperator::less_unsigned) {
                operation = BinaryOperator::less_signed;
            } else if (
                *operation
                == BinaryOperator::less_equal_unsigned) {
                operation = BinaryOperator::less_equal_signed;
            } else if (
                *operation
                == BinaryOperator::greater_unsigned) {
                operation = BinaryOperator::greater_signed;
            } else if (
                *operation
                == BinaryOperator::greater_equal_unsigned) {
                operation = BinaryOperator::greater_equal_signed;
            }
        }
        const auto scalar_result = *operation == BinaryOperator::equal
            || *operation == BinaryOperator::case_equal
            || *operation == BinaryOperator::wildcard_equal
            || *operation == BinaryOperator::vhdl_match_equal
            || *operation == BinaryOperator::not_equal
            || *operation == BinaryOperator::less_unsigned
            || *operation
                == BinaryOperator::less_equal_unsigned
            || *operation == BinaryOperator::greater_unsigned
            || *operation
                == BinaryOperator::greater_equal_unsigned
            || *operation == BinaryOperator::less_signed
            || *operation
                == BinaryOperator::less_equal_signed
            || *operation == BinaryOperator::greater_signed
            || *operation
                == BinaryOperator::greater_equal_signed;
        const auto result_width = scalar_result ? std::size_t { 1 }
                                                : register_width(*lhs);
        auto result_domain = scalar_result
                && language_
                    == frontend::Language::Vhdl2008
            ? frontend::ValueDomain::Boolean
            : language_
                    == frontend::Language::Vhdl2008
                && register_domain(*lhs)
                    == frontend::ValueDomain::Boolean
                && register_domain(*rhs)
                    == frontend::ValueDomain::Boolean
            ? frontend::ValueDomain::Boolean
            : is_two_state_domain(register_domain(*lhs))
                && is_two_state_domain(register_domain(*rhs))
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        if (!scalar_result
            && language_ == frontend::Language::Vhdl2008
            && (register_domain(*lhs)
                    == frontend::ValueDomain::Integer
                || register_domain(*rhs)
                    == frontend::ValueDomain::Integer)) {
            result_domain = frontend::ValueDomain::Integer;
        }
        if (!scalar_result
            && (register_domain(*lhs)
                    == frontend::ValueDomain::Logic9
                || register_domain(*rhs)
                    == frontend::ValueDomain::Logic9)) {
            result_domain = frontend::ValueDomain::Logic9;
        }
        const auto destination = allocate_register(result_width, result_domain);
        if (result_domain == frontend::ValueDomain::Integer
            && arithmetic) {
            if (!validate_static_integer_assignment(
                    expression.operands[0],
                    std::nullopt,
                    expression.operands[0].span)
                || !validate_static_integer_assignment(
                    expression.operands[1],
                    std::nullopt,
                    expression.operands[1].span)) {
                return std::nullopt;
            }
            auto integer_operation = IntegerBinaryOperator::add;
            switch (*operation) {
            case BinaryOperator::add_signed:
            case BinaryOperator::add_unsigned:
                integer_operation = IntegerBinaryOperator::add;
                break;
            case BinaryOperator::subtract_signed:
            case BinaryOperator::subtract_unsigned:
                integer_operation = IntegerBinaryOperator::subtract;
                break;
            case BinaryOperator::multiply_signed:
            case BinaryOperator::multiply_unsigned:
                integer_operation = IntegerBinaryOperator::multiply;
                break;
            case BinaryOperator::power_signed:
            case BinaryOperator::power_unsigned:
                integer_operation = IntegerBinaryOperator::power;
                break;
            case BinaryOperator::divide_signed:
            case BinaryOperator::divide_unsigned:
                integer_operation = IntegerBinaryOperator::divide;
                break;
            case BinaryOperator::remainder_signed:
                integer_operation = IntegerBinaryOperator::remainder;
                break;
            case BinaryOperator::modulo_signed:
            case BinaryOperator::modulo_unsigned:
                integer_operation = expression.text == "rem"
                    ? IntegerBinaryOperator::remainder
                    : IntegerBinaryOperator::modulo;
                break;
            default:
                break;
            }
            process_.operations.emplace_back(IntegerBinary {
                integer_operation,
                destination,
                *lhs,
                *rhs });
        } else {
            process_.operations.emplace_back(
                Binary { *operation, destination, *lhs, *rhs });
        }
        auto normalized_destination = destination;
        if (vhdl_numeric_relational) {
            const auto one = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant {
                one, PackedLogic4(1, Logic4::one) });
            normalized_destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                normalized_destination,
                destination,
                one });
        }
        if (invert_result) {
            const auto inverted = allocate_register(result_width, result_domain);
            process_.operations.emplace_back(
                UnaryNot { inverted, normalized_destination });
            return inverted;
        }
        if (systemverilog_power
            && result_width != width) {
            return resize_register(
                destination, width, signed_operation);
        }
        return normalized_destination;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call) {
        report(
            "FSIM-ELAB-VHNAME-001",
            "VHDL function or type mark '" + expression.text
                + "' is not visible in this expression context",
            expression.span);
    } else {
        report(
            "FSIM-ELAB-043",
            "expression form is parsed but not executable yet",
            expression.span);
    }
    return std::nullopt;
}

} // namespace fsim::elaboration
