// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <functional>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string float_canonical_name(std::string_view name)
{
    const auto separator = name.find_last_of('.');
    if (separator != std::string_view::npos) {
        name.remove_prefix(separator + 1U);
    }
    std::string result { name };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool float_subtype_name(const semantic::vhdl::SubtypeIndication& subtype)
{
    const auto name = float_canonical_name(subtype.type_mark.spelling);
    return name == "float" || name == "unresolved_float"
        || name == "u_float" || name == "float32"
        || name == "unresolved_float32" || name == "u_float32";
}

bool float_result_name(const std::string_view name)
{
    return name == "to_float" || name == "zerofp"
        || name == "neg_zerofp" || name == "nanfp"
        || name == "qnanfp" || name == "pos_inffp"
        || name == "neg_inffp" || name == "add"
        || name == "subtract" || name == "multiply"
        || name == "divide" || name == "sqrt" || name == "abs";
}

bool boolean_result_name(const std::string_view name)
{
    return name == "finite" || name == "isnan"
        || name == "unordered" || name == "is_negative"
        || name == "negative" || name == "eq" || name == "ne"
        || name == "lt" || name == "le" || name == "gt"
        || name == "ge";
}

bool packed_result_name(const std::string_view name)
{
    return name == "to_slv" || name == "to_stdlogicvector"
        || name == "to_std_logic_vector";
}

} // namespace

bool Lowerer::is_hir_vhdl_float_function(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call
        || !expression->vhdl->referenced_name) {
        return false;
    }
    const auto& source = *expression->vhdl;
    const auto name = float_canonical_name(source.text);
    if (!float_result_name(name) && !boolean_result_name(name)
        && !packed_result_name(name) && name != "to_integer") {
        return false;
    }
    if (!semantic::CompiledDesignResolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        }.vhdl_standard_package_member_visible(
            *source.referenced_name, source.scope)) {
        return false;
    }
    if (name == "to_float" || name == "zerofp"
        || name == "neg_zerofp" || name == "nanfp"
        || name == "qnanfp" || name == "pos_inffp"
        || name == "neg_inffp") {
        return true;
    }
    if (source.operands.empty()) {
        return true;
    }
    const auto first = source.operands.front();
    const auto subtype = hir_vhdl_expression_subtype(first);
    return (subtype && float_subtype_name(*subtype))
        || is_hir_vhdl_float_function(first);
}

std::optional<Lowerer::HirVhdlIntrinsicProfile>
Lowerer::hir_vhdl_float_function_profile(
    const semantic::ExpressionId expression_id) const
{
    if (!is_hir_vhdl_float_function(expression_id)) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto name = float_canonical_name(expression->vhdl->text);
    if (float_result_name(name)) {
        return HirVhdlIntrinsicProfile {
            32U, frontend::ValueDomain::Logic9, false };
    }
    if (boolean_result_name(name)) {
        return HirVhdlIntrinsicProfile {
            1U, frontend::ValueDomain::Boolean, false };
    }
    if (packed_result_name(name)) {
        return HirVhdlIntrinsicProfile {
            32U, frontend::ValueDomain::Logic9, false };
    }
    return HirVhdlIntrinsicProfile {
        static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(vhdl_standard_)),
        frontend::ValueDomain::Integer,
        true,
    };
}

bool Lowerer::validate_hir_vhdl_float_context(
    const semantic::ExpressionId expression_id,
    const semantic::vhdl::SubtypeIndication& context)
{
    if (!is_hir_vhdl_float_function(expression_id)) {
        return true;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto name = float_canonical_name(expression->vhdl->text);
    if (!float_result_name(name)) {
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(context)
        .value_or(context);
    auto valid = float_subtype_name(effective);
    if (valid && !effective.constraints.empty()) {
        const auto& range = effective.constraints.front();
        const auto left = range.left
            ? range.left
            : range.left_expression
            ? hir_constant_integer(*range.left_expression)
            : std::nullopt;
        const auto right = range.right
            ? range.right
            : range.right_expression
            ? hir_constant_integer(*range.right_expression)
            : std::nullopt;
        valid = left == std::optional<std::int64_t> { 8 }
            && right == std::optional<std::int64_t> { -23 }
            && range.descending;
    }
    if (valid && (!effective.executable_width
            || *effective.executable_width == 32U)) {
        return true;
    }
    report(
        "FSIM-ELAB-VHFLT-002",
        "the bounded floating profile requires float(8 downto -23) context for "
            + name,
        hir_source_span(expression->vhdl->source));
    return false;
}

Lowerer::HirVhdlIntrinsicAttempt Lowerer::lower_hir_vhdl_float_function(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (!is_hir_vhdl_float_function(expression_id)) {
        return { };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    const auto root_name = float_canonical_name(source.text);
    const auto span = hir_source_span(source.source);

    const auto valid_dimensions = [&](const semantic::vhdl::Expression& call) {
        if (call.operands.size() != 2U) {
            return false;
        }
        return hir_constant_integer(call.operands[0])
                == std::optional<std::int64_t> { 8 }
            && hir_constant_integer(call.operands[1])
                == std::optional<std::int64_t> { 23 };
    };
    std::function<std::optional<std::uint32_t>(semantic::ExpressionId)>
        evaluate;
    evaluate = [&](const semantic::ExpressionId candidate_id)
        -> std::optional<std::uint32_t> {
        const auto candidate = specialized_hir_unit_->find_expression(
            candidate_id);
        if (!candidate || candidate->vhdl == nullptr
            || candidate->vhdl->kind
                != semantic::vhdl::ExpressionKind::call
            || !is_hir_vhdl_float_function(candidate_id)) {
            return std::nullopt;
        }
        const auto& call = *candidate->vhdl;
        const auto name = float_canonical_name(call.text);
        if (name == "zerofp" || name == "neg_zerofp"
            || name == "nanfp" || name == "qnanfp"
            || name == "pos_inffp" || name == "neg_inffp") {
            if (!valid_dimensions(call)) {
                return std::nullopt;
            }
            if (name == "neg_zerofp") {
                return UINT32_C(0x80000000);
            }
            if (name == "nanfp") {
                return UINT32_C(0x7f800001);
            }
            if (name == "qnanfp") {
                return UINT32_C(0x7fc00000);
            }
            if (name == "pos_inffp") {
                return UINT32_C(0x7f800000);
            }
            if (name == "neg_inffp") {
                return UINT32_C(0xff800000);
            }
            return UINT32_C(0);
        }
        if (name == "to_float") {
            if (call.operands.size() != 3U
                || hir_constant_integer(call.operands[1])
                    != std::optional<std::int64_t> { 8 }
                || hir_constant_integer(call.operands[2])
                    != std::optional<std::int64_t> { 23 }) {
                return std::nullopt;
            }
            const auto integer = hir_constant_integer(call.operands[0]);
            return integer
                ? std::optional<std::uint32_t> {
                      std::bit_cast<std::uint32_t>(
                          static_cast<float>(*integer)) }
                : std::nullopt;
        }
        const auto binary = name == "add" || name == "subtract"
            || name == "multiply" || name == "divide";
        if (binary) {
            if (call.operands.size() != 2U) {
                return std::nullopt;
            }
            const auto lhs = evaluate(call.operands[0]);
            const auto rhs = evaluate(call.operands[1]);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            const auto left = std::bit_cast<float>(*lhs);
            const auto right = std::bit_cast<float>(*rhs);
            const auto result = name == "add" ? left + right
                : name == "subtract"            ? left - right
                : name == "multiply"            ? left * right
                                                 : left / right;
            return std::bit_cast<std::uint32_t>(result);
        }
        if ((name == "sqrt" || name == "abs")
            && call.operands.size() == 1U) {
            const auto bits = evaluate(call.operands.front());
            if (!bits) {
                return std::nullopt;
            }
            const auto value = std::bit_cast<float>(*bits);
            return std::bit_cast<std::uint32_t>(
                name == "sqrt" ? std::sqrt(value) : std::fabs(value));
        }
        return std::nullopt;
    };

    if (float_result_name(root_name)) {
        const auto bits = evaluate(expression_id);
        if (!bits) {
            report(
                root_name == "to_float" ? "FSIM-ELAB-VHFLT-001"
                                        : "FSIM-ELAB-VHFLT-003",
                "the bounded floating profile requires a locally static binary32 operation",
                span);
            return { true, std::nullopt };
        }
        if (expected_width != 0U && expected_width != 32U) {
            report(
                "FSIM-ELAB-VHFLT-002",
                "the bounded floating profile requires float(8 downto -23) context for "
                    + root_name + " (received width "
                    + std::to_string(expected_width) + ")",
                span);
            return { true, std::nullopt };
        }
        const auto result = allocate_register(
            32U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            result, unsigned_value(*bits, 32U).promoted_to_logic9() });
        return { true, result };
    }

    const auto operand_bits = [&](const std::size_t index) {
        return index < source.operands.size()
            ? evaluate(source.operands[index])
            : std::optional<std::uint32_t> { };
    };
    if (packed_result_name(root_name)) {
        const auto bits = operand_bits(0U);
        if (source.operands.size() != 1U || !bits
            || (expected_width != 0U && expected_width != 32U)) {
            report(
                "FSIM-ELAB-VHFLT-003",
                "binary32 vector conversion requires one locally static float operand",
                span);
            return { true, std::nullopt };
        }
        const auto result = allocate_register(
            32U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            result, unsigned_value(*bits, 32U).promoted_to_logic9() });
        return { true, result };
    }
    if (root_name == "to_integer") {
        const auto bits = operand_bits(0U);
        if (source.operands.size() != 1U || !bits) {
            report(
                "FSIM-ELAB-VHFLT-003",
                "floating to_integer requires one locally static binary32 operand",
                span);
            return { true, std::nullopt };
        }
        const auto value = std::bit_cast<float>(*bits);
        const auto integer_width = static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
        const auto rounded = std::nearbyint(static_cast<double>(value));
        const auto minimum = integer_width == 64U
            ? -9223372036854775808.0
            : -2147483648.0;
        const auto exclusive_maximum = integer_width == 64U
            ? 9223372036854775808.0
            : 2147483648.0;
        if (!std::isfinite(rounded) || rounded < minimum
            || rounded >= exclusive_maximum) {
            report(
                "FSIM-ELAB-VHFLT-004",
                "floating to_integer requires a finite result in the predefined integer range",
                span);
            return { true, std::nullopt };
        }
        const auto result = allocate_register(
            integer_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            result,
            integer_value(
                static_cast<std::int64_t>(rounded), integer_width),
        });
        return { true, result };
    }

    const auto lhs = operand_bits(0U);
    const auto rhs = source.operands.size() > 1U
        ? operand_bits(1U)
        : lhs;
    const auto unary = root_name == "finite" || root_name == "isnan"
        || root_name == "is_negative" || root_name == "negative";
    if (!lhs || !rhs || source.operands.size() != (unary ? 1U : 2U)) {
        report(
            "FSIM-ELAB-VHFLT-003",
            "the bounded floating predicate requires locally static binary32 operands",
            span);
        return { true, std::nullopt };
    }
    const auto left = std::bit_cast<float>(*lhs);
    const auto right = std::bit_cast<float>(*rhs);
    const auto result_value = root_name == "finite" ? std::isfinite(left)
        : root_name == "isnan"                      ? std::isnan(left)
        : root_name == "is_negative"
                || root_name == "negative"         ? std::signbit(left)
        : root_name == "unordered"                 ? std::isunordered(left, right)
        : root_name == "eq"                        ? left == right
        : root_name == "ne"                        ? left != right
        : root_name == "lt"                        ? left < right
        : root_name == "le"                        ? left <= right
        : root_name == "gt"                        ? left > right
                                                    : left >= right;
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(LoadConstant {
        result, unsigned_value(result_value ? 1U : 0U, 1U) });
    return { true, result };
}

} // namespace fsim::elaboration
