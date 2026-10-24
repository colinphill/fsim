// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <cmath>
#include <limits>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

bool fixed_subtype_name(const semantic::vhdl::SubtypeIndication& subtype)
{
    const auto name = lowercase_vhdl_simple_name(subtype.type_mark.spelling);
    return name == "ufixed" || name == "sfixed"
        || name == "unresolved_ufixed" || name == "unresolved_sfixed";
}

bool signed_fixed_subtype(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    const auto name = lowercase_vhdl_simple_name(subtype.type_mark.spelling);
    return subtype.signed_value || name == "sfixed"
        || name == "unresolved_sfixed";
}

PackedLogic4 fixed_integer_value(
    const std::int64_t integer,
    const std::size_t width,
    const std::size_t scale,
    const bool signed_result)
{
    PackedLogic4 result(width, Logic4::zero);
    const auto magnitude = integer < 0
        ? static_cast<std::uint64_t>(-(integer + 1)) + 1U
        : static_cast<std::uint64_t>(integer);
    const auto sign_index = width - 1U;
    if (!signed_result) {
        const auto value_bits = width > scale ? width - scale : 0U;
        const auto overflow = integer > 0 && value_bits < 64U
            && magnitude >= (std::uint64_t { 1 } << value_bits);
        if (overflow) {
            for (std::size_t bit { }; bit < width; ++bit) {
                result.set(bit, Logic4::one);
            }
            return result.promoted_to_logic9();
        }
        if (integer <= 0) {
            return result.promoted_to_logic9();
        }
    } else if (integer > 0) {
        const auto value_bits = sign_index > scale
            ? sign_index - scale
            : 0U;
        const auto overflow = value_bits < 63U
            && magnitude >= (std::uint64_t { 1 } << value_bits);
        if (overflow) {
            for (std::size_t bit { }; bit < sign_index; ++bit) {
                result.set(bit, Logic4::one);
            }
            return result.promoted_to_logic9();
        }
    } else if (integer < 0) {
        const auto value_bits = sign_index >= scale
            ? sign_index - scale
            : 0U;
        const auto overflow = sign_index < scale
            || (value_bits < 64U
                && magnitude > (std::uint64_t { 1 } << value_bits));
        if (overflow) {
            result.set(sign_index, Logic4::one);
            return result.promoted_to_logic9();
        }
    }

    const auto encoded = static_cast<std::uint64_t>(integer);
    for (std::size_t bit = scale; bit < width; ++bit) {
        const auto source_bit = bit - scale;
        const auto one = source_bit < 64U
            ? ((encoded >> source_bit) & 1U) != 0U
            : integer < 0;
        if (one) {
            result.set(bit, Logic4::one);
        }
    }
    return result.promoted_to_logic9();
}

// FROM_STRING and FROM_BSTRING read a binary string whose leftmost
// character is the LEFT_INDEX bit (IEEE 1076-2019 fixed_generic_pkg).
bool fixed_binary_string_function(const std::string_view name)
{
    return name == "from_string" || name == "from_bstring"
        || name == "from_binary_string";
}

PackedLogic4 one_hot_value(
    const std::size_t bit,
    const std::size_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    result.set(bit, Logic4::one);
    return result.promoted_to_logic9();
}

bool fixed_arithmetic_operator(const std::string_view text)
{
    return text == "+" || text == "-" || text == "*" || text == "/";
}

bool fixed_relational_operator(const std::string_view text)
{
    return text == "=" || text == "/=" || text == "<" || text == "<="
        || text == ">" || text == ">=";
}

} // namespace

std::optional<Lowerer::HirVhdlFixedRange> Lowerer::hir_vhdl_fixed_range(
    const semantic::ExpressionId expression_id,
    const std::size_t depth) const
{
    if (specialized_hir_unit_ == nullptr || depth > 64U) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::binary) {
        if (!fixed_arithmetic_operator(source.text)
            || !is_hir_vhdl_fixed_operator(expression_id)) {
            return std::nullopt;
        }
        const auto lhs = hir_vhdl_fixed_range(source.operands[0], depth + 1U);
        const auto rhs = hir_vhdl_fixed_range(source.operands[1], depth + 1U);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        // The result ranges of the fixed_generic_pkg operators.
        const auto signed_value = lhs->signed_value;
        if (source.text == "*") {
            return HirVhdlFixedRange { lhs->left + rhs->left + 1,
                lhs->right + rhs->right, signed_value };
        }
        if (source.text == "/") {
            return signed_value
                ? HirVhdlFixedRange { lhs->left - rhs->right + 1,
                      lhs->right - rhs->left, true }
                : HirVhdlFixedRange { lhs->left - rhs->right,
                      lhs->right - rhs->left - 1, false };
        }
        return HirVhdlFixedRange { std::max(lhs->left, rhs->left) + 1,
            std::min(lhs->right, rhs->right), signed_value };
    }
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.operands.size() == 3U
        && is_hir_vhdl_fixed_function(expression_id)) {
        const auto left = hir_constant_integer(source.operands[1]);
        const auto right = hir_constant_integer(source.operands[2]);
        const auto profile = hir_vhdl_fixed_function_profile(expression_id);
        if (!left || !right || !profile) {
            return std::nullopt;
        }
        return HirVhdlFixedRange { *left, *right, profile->signed_value };
    }
    auto subtype = hir_vhdl_expression_subtype(expression_id);
    if (!subtype) {
        return std::nullopt;
    }
    subtype = hir_effective_vhdl_subtype(*subtype).value_or(*subtype);
    if (!fixed_subtype_name(*subtype)) {
        return std::nullopt;
    }
    const auto constraint = std::ranges::find_if(subtype->constraints,
        [](const semantic::vhdl::RangeConstraint& candidate) {
            return candidate.kind == semantic::vhdl::RangeKind::array_index
                || candidate.kind == semantic::vhdl::RangeKind::discrete;
        });
    if (constraint == subtype->constraints.end()
        || !constraint->descending) {
        return std::nullopt;
    }
    const auto left = hir_range_left(*constraint);
    const auto right = hir_range_right(*constraint);
    if (!left || !right || *left < *right) {
        return std::nullopt;
    }
    return HirVhdlFixedRange { *left, *right,
        signed_fixed_subtype(*subtype) };
}

bool Lowerer::is_hir_vhdl_fixed_operator(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::binary
        || expression->vhdl->operands.size() != 2U
        || (!fixed_arithmetic_operator(expression->vhdl->text)
            && !fixed_relational_operator(expression->vhdl->text))) {
        return false;
    }
    if (!hir_vhdl_intrinsic_member_present("ufixed")) {
        return false;
    }
    const auto& source = *expression->vhdl;
    // A user operator overload keeps its own meaning.
    if (source.referenced_name && source.referenced_name->selected) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *source.referenced_name->selected);
        if (declaration && declaration->vhdl != nullptr) {
            return false;
        }
    }
    const auto lhs = hir_vhdl_fixed_range(source.operands[0]);
    const auto rhs = lhs ? hir_vhdl_fixed_range(source.operands[1])
                         : std::nullopt;
    return lhs && rhs && lhs->signed_value == rhs->signed_value;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_fixed_operator(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    const auto lhs_range = hir_vhdl_fixed_range(source.operands[0]);
    const auto rhs_range = hir_vhdl_fixed_range(source.operands[1]);
    if (!lhs_range || !rhs_range) {
        return std::nullopt;
    }
    const auto signed_value = lhs_range->signed_value;
    const auto width_of = [](const HirVhdlFixedRange& range) {
        return static_cast<std::size_t>(range.left - range.right + 1);
    };
    constexpr std::size_t maximum_width = 1U << 16U;
    if (width_of(*lhs_range) > maximum_width
        || width_of(*rhs_range) > maximum_width) {
        report("FSIM-ELAB-VHFIX-002",
            "fixed-point operand width is not representable by SimIR",
            hir_source_span(source.source));
        return std::nullopt;
    }
    const auto lhs = lower_hir_expression(
        source.operands[0], width_of(*lhs_range));
    const auto rhs = lhs
        ? lower_hir_expression(source.operands[1], width_of(*rhs_range))
        : std::nullopt;
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    const auto shift_left = [&](const RegisterId value,
                                const std::size_t amount) {
        if (amount == 0U) {
            return value;
        }
        const auto count = load_constant(
            unsigned_value(amount, 32U), frontend::ValueDomain::Bit2);
        const auto shifted = allocate_register(
            register_width(value), frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Shift {
            ShiftOperator::logical_left, shifted, value, count, false });
        return shifted;
    };
    // The operand at `width` bits with its binary point at index `right`.
    const auto aligned = [&](const RegisterId value,
                             const HirVhdlFixedRange& range,
                             const std::int64_t right,
                             const std::size_t width) {
        return shift_left(resize_register(value, width, signed_value),
            static_cast<std::size_t>(range.right - right));
    };
    const auto binary = [&](const BinaryOperator operation,
                            const RegisterId left_value,
                            const RegisterId right_value,
                            const std::size_t width,
                            const frontend::ValueDomain domain) {
        const auto destination = allocate_register(width, domain);
        process_.operations.emplace_back(
            Binary { operation, destination, left_value, right_value });
        return destination;
    };
    if (fixed_relational_operator(source.text)) {
        // Both values exactly, at a common binary point.
        const auto right = std::min(lhs_range->right, rhs_range->right);
        const auto left = std::max(lhs_range->left, rhs_range->left) + 1;
        const auto width = static_cast<std::size_t>(left - right + 1);
        const auto a = aligned(*lhs, *lhs_range, right, width);
        const auto b = aligned(*rhs, *rhs_range, right, width);
        const auto operation = source.text == "=" ? BinaryOperator::equal
            : source.text == "/=" ? BinaryOperator::not_equal
            : source.text == "<"
            ? (signed_value ? BinaryOperator::less_signed
                            : BinaryOperator::less_unsigned)
            : source.text == "<="
            ? (signed_value ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned)
            : source.text == ">"
            ? (signed_value ? BinaryOperator::greater_signed
                            : BinaryOperator::greater_unsigned)
            : (signed_value ? BinaryOperator::greater_equal_signed
                            : BinaryOperator::greater_equal_unsigned);
        const auto compared = binary(operation, a, b, 1U,
            frontend::ValueDomain::Logic9);
        const auto result = allocate_register(1U,
            frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(CopyRegister { result, compared });
        return result;
    }
    const auto result_range = hir_vhdl_fixed_range(expression_id);
    if (!result_range) {
        return std::nullopt;
    }
    const auto width = width_of(*result_range);
    if (source.text == "+" || source.text == "-") {
        const auto a = aligned(*lhs, *lhs_range, result_range->right, width);
        const auto b = aligned(*rhs, *rhs_range, result_range->right, width);
        return binary(source.text == "+" ? BinaryOperator::add_unsigned
                                         : BinaryOperator::subtract_unsigned,
            a, b, width, frontend::ValueDomain::Logic9);
    }
    if (source.text == "*") {
        return binary(BinaryOperator::multiply_unsigned,
            resize_register(*lhs, width, signed_value),
            resize_register(*rhs, width, signed_value), width,
            frontend::ValueDomain::Logic9);
    }
    // Division: the quotient of the raw values scaled to the result's
    // binary point, with one extra bit rounded to nearest (fixed_round).
    const auto scale = static_cast<std::size_t>(
        lhs_range->right - rhs_range->right - result_range->right) + 1U;
    const auto work_width = width_of(*lhs_range) + width_of(*rhs_range)
        + scale + 2U;
    const auto numerator = shift_left(
        resize_register(*lhs, work_width, signed_value), scale);
    const auto denominator = resize_register(*rhs, work_width, signed_value);
    const auto quotient = binary(signed_value
            ? BinaryOperator::divide_signed
            : BinaryOperator::divide_unsigned,
        numerator, denominator, work_width, frontend::ValueDomain::Logic9);
    const auto one = load_constant(
        PackedLogic4(1U, Logic4::one).promoted_to_logic9(),
        frontend::ValueDomain::Logic9);
    const auto rounded = binary(BinaryOperator::add_unsigned, quotient,
        resize_register(one, work_width, false), work_width,
        frontend::ValueDomain::Logic9);
    const auto count = load_constant(
        unsigned_value(1U, 32U), frontend::ValueDomain::Bit2);
    const auto halved = allocate_register(work_width,
        frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Shift {
        signed_value ? ShiftOperator::arithmetic_right
                     : ShiftOperator::logical_right,
        halved, rounded, count, false });
    return resize_register(halved, width, signed_value);
}

std::optional<StringRegisterId> Lowerer::lower_hir_vhdl_fixed_to_string(
    const semantic::ExpressionId operand)
{
    const auto range = hir_vhdl_fixed_range(operand);
    if (!range || range->left < 0 || range->right >= 0) {
        return std::nullopt;
    }
    const auto image = lower_hir_vhdl_runtime_image(
        operand, std::nullopt, OutputFormat::binary, false);
    if (!image) {
        return std::nullopt;
    }
    const auto part = [&](const std::int64_t first,
                          const std::int64_t last) {
        StringMethod method;
        method.operation = StringMethodOperator::substr;
        method.source = *image;
        method.string_destination = allocate_string_register();
        method.first = load_integer(first, 32U,
            frontend::ValueDomain::Integer);
        method.second = load_integer(last, 32U,
            frontend::ValueDomain::Integer);
        process_.operations.emplace_back(method);
        return method.string_destination;
    };
    const auto integer_part = part(0, range->left);
    const auto fraction_part = part(range->left + 1,
        range->left - range->right);
    const auto point = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant { point, "." });
    const auto result = allocate_string_register();
    process_.operations.emplace_back(ConcatenateStrings {
        result, { integer_part, point, fraction_part } });
    return result;
}

bool Lowerer::is_hir_vhdl_fixed_function(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (is_hir_vhdl_fixed_operator(expression_id)) {
        return true;
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
    const auto name = lowercase_vhdl_simple_name(source.text);
    if (name != "to_ufixed" && name != "to_sfixed" && name != "resize"
        && !fixed_binary_string_function(name)) {
        return false;
    }
    if (name == "resize" && source.operands.size() != 3U) {
        const auto subtype = source.operands.empty()
            ? std::optional<semantic::vhdl::SubtypeIndication> { }
            : hir_vhdl_expression_subtype(source.operands.front());
        if (!subtype || !fixed_subtype_name(*subtype)) {
            return false;
        }
    }
    return hir_resolver().vhdl_standard_package_member_visible(
        *source.referenced_name, source.scope);
}

std::optional<Lowerer::HirVhdlIntrinsicProfile>
Lowerer::hir_vhdl_fixed_function_profile(
    const semantic::ExpressionId expression_id) const
{
    if (!is_hir_vhdl_fixed_function(expression_id)) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::binary) {
        if (fixed_relational_operator(source.text)) {
            return HirVhdlIntrinsicProfile {
                1U, frontend::ValueDomain::Boolean, false };
        }
        const auto range = hir_vhdl_fixed_range(expression_id);
        if (!range) {
            return std::nullopt;
        }
        return HirVhdlIntrinsicProfile {
            static_cast<std::size_t>(range->left - range->right + 1),
            frontend::ValueDomain::Logic9, range->signed_value };
    }
    if (source.operands.size() != 3U) {
        return std::nullopt;
    }
    const auto left = hir_constant_integer(source.operands[1]);
    const auto right = hir_constant_integer(source.operands[2]);
    if (!left || !right || *left < *right) {
        return std::nullopt;
    }
    const auto distance = index_distance(*left, *right);
    if (distance >= std::numeric_limits<std::uint32_t>::max()
        || distance >= std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    const auto name = lowercase_vhdl_simple_name(source.text);
    auto signed_value = name == "to_sfixed";
    if (name == "resize") {
        const auto subtype = hir_vhdl_expression_subtype(
            source.operands.front());
        if (!subtype || !fixed_subtype_name(*subtype)) {
            return std::nullopt;
        }
        signed_value = signed_fixed_subtype(*subtype);
    }
    return HirVhdlIntrinsicProfile {
        static_cast<std::size_t>(distance + 1U),
        frontend::ValueDomain::Logic9,
        signed_value,
    };
}

bool Lowerer::validate_hir_vhdl_fixed_context(
    const semantic::ExpressionId expression_id,
    const semantic::vhdl::SubtypeIndication& context)
{
    if (!is_hir_vhdl_fixed_function(expression_id)) {
        return true;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    if (source.operands.size() != 3U) {
        return true;
    }
    const auto left = hir_constant_integer(source.operands[1]);
    const auto right = hir_constant_integer(source.operands[2]);
    if (!left || !right || *left < *right) {
        return true;
    }
    const auto distance = index_distance(*left, *right);
    if (distance >= std::numeric_limits<std::uint32_t>::max()
        || distance >= std::numeric_limits<std::size_t>::max()) {
        // Let the intrinsic lowering path issue the resource diagnostic.
        // A contextual mismatch is secondary and must not suppress the
        // required unrepresentable-width error.
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(context)
        .value_or(context);
    if (!fixed_subtype_name(effective)) {
        return true;
    }
    const auto constraint = std::ranges::find_if(
        effective.constraints,
        [](const semantic::vhdl::RangeConstraint& candidate) {
            return candidate.kind
                    == semantic::vhdl::RangeKind::array_index
                || candidate.kind
                    == semantic::vhdl::RangeKind::discrete;
        });
    if (constraint == effective.constraints.end()) {
        return true;
    }
    const auto context_left = hir_range_left(*constraint);
    const auto context_right = hir_range_right(*constraint);
    if (context_left && context_right
        && *context_left == *left && *context_right == *right
        && constraint->descending) {
        return true;
    }
    report(
        "FSIM-ELAB-VHFIX-004",
        lowercase_vhdl_simple_name(source.text)
            + " bounds do not match the contextual fixed-point range",
        hir_source_span(source.source));
    return false;
}

Lowerer::HirVhdlIntrinsicAttempt Lowerer::lower_hir_vhdl_fixed_function(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (!is_hir_vhdl_fixed_function(expression_id)) {
        return { };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::binary) {
        const auto profile = hir_vhdl_fixed_function_profile(expression_id);
        auto value = lower_hir_vhdl_fixed_operator(expression_id);
        if (value && profile && expected_width != 0U
            && profile->domain != frontend::ValueDomain::Boolean
            && register_width(*value) != expected_width) {
            value = resize_register(
                *value, expected_width, profile->signed_value);
        }
        return { true, value };
    }
    const auto span = hir_source_span(source.source);
    const auto operand_span = [&](const semantic::ExpressionId operand) {
        const auto record = specialized_hir_unit_->find_expression(operand);
        return record && record->vhdl != nullptr
            ? hir_source_span(record->vhdl->source)
            : span;
    };
    const auto name = lowercase_vhdl_simple_name(source.text);
    const auto conversion = name == "to_ufixed" || name == "to_sfixed";
    if (source.operands.size() != 3U) {
        report(
            "FSIM-ELAB-VHFIX-001",
            name
                + " requires a value and locally static left and right bounds",
            span);
        return { true, std::nullopt };
    }
    const auto left = hir_constant_integer(source.operands[1]);
    const auto right = hir_constant_integer(source.operands[2]);
    if (!left || !right || *left < *right) {
        report(
            "FSIM-ELAB-VHFIX-002",
            name
                + " requires a locally static descending fixed-point range",
            span);
        return { true, std::nullopt };
    }
    const auto distance = index_distance(*left, *right);
    if (distance >= std::numeric_limits<std::uint32_t>::max()
        || distance >= std::numeric_limits<std::size_t>::max()) {
        report(
            "FSIM-ELAB-VHFIX-002",
            "fixed-point result width is not representable by SimIR",
            span);
        return { true, std::nullopt };
    }
    const auto result_width = static_cast<std::size_t>(distance + 1U);
    if (expected_width != 0U && expected_width != result_width) {
        report(
            "FSIM-ELAB-VHFIX-004",
            name
                + " bounds do not match the contextual fixed-point range",
            span);
        return { true, std::nullopt };
    }

    if (fixed_binary_string_function(name)) {
        const auto text = specialized_hir_unit_->evaluate_string_expression(
            source.operands.front());
        if (!text || text->size() != result_width
            || !std::ranges::all_of(*text, [](const char character) {
                   return character == '0' || character == '1';
               })) {
            report(
                "FSIM-ELAB-VHFIX-001",
                name
                    + " requires a locally static binary string of the "
                      "fixed-point width in this bounded profile",
                operand_span(source.operands.front()));
            return { true, std::nullopt };
        }
        PackedLogic4 value(result_width, Logic4::zero);
        for (std::size_t index { }; index < result_width; ++index) {
            if ((*text)[index] == '1') {
                value.set(result_width - 1U - index, Logic4::one);
            }
        }
        const auto result = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(
            LoadConstant { result, value.promoted_to_logic9() });
        return { true, result };
    }
    if (conversion) {
        auto integer = hir_constant_integer(source.operands.front());
        auto scale_applied = false;
        if (!integer && *right <= 0 && -*right < 62) {
            // A REAL value rounds to the nearest representable value
            // (fixed_round, IEEE 1076-2019 G.4).
            if (const auto real = hir_vhdl_constant_real(
                    source.operands.front())) {
                const auto scaled = std::nearbyint(
                    std::ldexp(*real, static_cast<int>(-*right)));
                if (std::isfinite(scaled) && std::fabs(scaled) < 0x1p62) {
                    integer = static_cast<std::int64_t>(scaled);
                    scale_applied = true;
                }
            }
        }
        // A run-time binary32 FLOAT rounds to the nearest value of the
        // range and saturates (fixed_round, fixed_saturate; IEEE 1076-2019
        // G.4).
        if (!integer && *right <= 0 && -*right < 62
            && hir_vhdl_float_valued(source.operands.front())) {
            const auto real = lower_hir_vhdl_float_as_real(
                source.operands.front());
            if (!real) {
                return { true, std::nullopt };
            }
            using ScalarKind = frontend::SystemVerilogScalarKind;
            const auto scale = load_constant(
                unsigned_value(std::bit_cast<std::uint64_t>(
                                   std::ldexp(1.0, static_cast<int>(-*right))),
                    64U),
                frontend::ValueDomain::Bit2);
            const auto scaled = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                runtime::SystemVerilogScalarBinaryOperator::Multiply, scaled,
                *real, scale, ScalarKind::Real, ScalarKind::Real,
                ScalarKind::Real });
            const auto signed_result = name == "to_sfixed";
            const auto value_bits = signed_result ? result_width - 1U
                                                  : result_width;
            const auto limit = value_bits >= 62U
                ? (std::int64_t { 1 } << 62)
                : (std::int64_t { 1 } << value_bits);
            const auto bound = [&](const std::int64_t value) {
                return load_constant(
                    unsigned_value(std::bit_cast<std::uint64_t>(
                                       static_cast<double>(value)),
                        64U),
                    frontend::ValueDomain::Bit2);
            };
            const auto clamp = [&](const RegisterId value,
                                   const runtime::SystemVerilogScalarBinaryOperator
                                       relation,
                                   const RegisterId limit_value) {
                const auto outside = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(SystemVerilogScalarBinary {
                    relation, outside, value, limit_value, ScalarKind::Real,
                    ScalarKind::Real, ScalarKind::None });
                const auto selected = allocate_register(
                    64U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ConditionalSelect {
                    selected, outside, limit_value, value });
                return selected;
            };
            auto clamped = clamp(scaled,
                runtime::SystemVerilogScalarBinaryOperator::Greater,
                bound(limit - 1));
            clamped = clamp(clamped,
                runtime::SystemVerilogScalarBinaryOperator::Less,
                bound(signed_result ? -limit : 0));
            const auto rounded = lower_hir_vhdl_real_to_integer(
                clamped, 64U, std::nullopt);
            auto value = resize_register(rounded, result_width, true);
            if (register_domain(value) != frontend::ValueDomain::Logic9) {
                const auto promoted = allocate_register(
                    result_width, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(
                    CopyRegister { promoted, value });
                value = promoted;
            }
            return { true, value };
        }
        if (!integer) {
            report(
                "FSIM-ELAB-VHFIX-001",
                name + " requires an integer value in this bounded profile",
                operand_span(source.operands.front()));
            return { true, std::nullopt };
        }
        if (*right > 0) {
            report(
                "FSIM-ELAB-VHFIX-003",
                name
                    + " requires a locally static integer and a nonpositive "
                      "binary-point index",
                span);
            return { true, std::nullopt };
        }
        const auto scale_value = index_distance(0, *right);
        if (scale_value > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-VHFIX-003",
                "fixed-point binary-point distance exceeds host address space",
                span);
            return { true, std::nullopt };
        }
        auto value = fixed_integer_value(
            *integer,
            result_width,
            scale_applied ? 0U : static_cast<std::size_t>(scale_value),
            name == "to_sfixed");
        const auto result = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(
            LoadConstant { result, std::move(value) });
        return { true, result };
    }

    auto source_subtype = hir_vhdl_expression_subtype(
        source.operands.front());
    if (source_subtype) {
        source_subtype = hir_effective_vhdl_subtype(*source_subtype)
            .value_or(*source_subtype);
    }
    const semantic::vhdl::RangeConstraint* source_constraint { };
    if (source_subtype) {
        const auto found = std::ranges::find_if(
            source_subtype->constraints,
            [](const semantic::vhdl::RangeConstraint& candidate) {
                return candidate.kind
                        == semantic::vhdl::RangeKind::array_index
                    || candidate.kind
                        == semantic::vhdl::RangeKind::discrete;
            });
        if (found != source_subtype->constraints.end()) {
            source_constraint = &*found;
        }
    }
    const auto constrained_source = source_subtype
        && fixed_subtype_name(*source_subtype)
        && source_constraint != nullptr
        && source_constraint->descending;
    if (!constrained_source) {
        report(
            "FSIM-ELAB-VHFIX-001",
            "fixed-point resize requires a constrained ufixed or sfixed operand",
            operand_span(source.operands.front()));
        return { true, std::nullopt };
    }
    const auto source_left = hir_range_left(*source_constraint);
    const auto source_right = hir_range_right(*source_constraint);
    if (!source_left || !source_right || *source_left < *source_right) {
        report(
            "FSIM-ELAB-VHFIX-001",
            "fixed-point resize requires a constrained ufixed or sfixed operand",
            operand_span(source.operands.front()));
        return { true, std::nullopt };
    }
    const auto source_distance = index_distance(*source_left, *source_right);
    if (source_distance >= std::numeric_limits<std::uint32_t>::max()
        || source_distance >= std::numeric_limits<std::size_t>::max()) {
        report(
            "FSIM-ELAB-VHFIX-002",
            "fixed-point resize source width is not representable by SimIR",
            span);
        return { true, std::nullopt };
    }
    const auto source_width = static_cast<std::size_t>(
        source_distance + 1U);
    auto lowered_source = lower_hir_expression(
        source.operands.front(), source_width);
    if (!lowered_source) {
        return { true, std::nullopt };
    }
    const auto signed_source = signed_fixed_subtype(*source_subtype);
    if (*source_right < *right) {
        const auto amount_value = index_distance(*right, *source_right);
        if (amount_value > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-VHFIX-003",
                "fixed-point resize distance exceeds host address space",
                span);
            return { true, std::nullopt };
        }
        const auto amount = static_cast<std::size_t>(amount_value);
        if (signed_source) {
            report(
                "FSIM-ELAB-VHFIX-003",
                "rounded resize does not yet support signed fractional narrowing",
                span);
            return { true, std::nullopt };
        }
        if (amount > source_width) {
            const auto zero = load_constant(
                PackedLogic4(result_width, Logic4::zero).promoted_to_logic9(),
                frontend::ValueDomain::Logic9);
            return { true, zero };
        }
        if (source_width == std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHFIX-003",
                "rounded resize requires an intermediate width representable by SimIR",
                span);
            return { true, std::nullopt };
        }
        const auto work_width = source_width + 1U;
        lowered_source = resize_register(
            *lowered_source, work_width, false);
        const auto half = allocate_register(
            work_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            half, one_hot_value(amount - 1U, work_width) });
        const auto rounded = allocate_register(
            work_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, rounded, *lowered_source, half });
        const auto count = load_constant(
            unsigned_value(amount, 32U), frontend::ValueDomain::Bit2);
        const auto shifted = allocate_register(
            work_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Shift {
            ShiftOperator::logical_right,
            shifted,
            rounded,
            count,
            false,
        });
        lowered_source = shifted;
    } else if (*source_right > *right) {
        const auto amount_value = index_distance(*source_right, *right);
        if (amount_value > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-VHFIX-003",
                "fixed-point resize distance exceeds host address space",
                span);
            return { true, std::nullopt };
        }
        const auto amount = static_cast<std::size_t>(amount_value);
        const auto work_width = result_width;
        lowered_source = resize_register(
            *lowered_source, work_width, signed_source);
        if (amount >= work_width) {
            const auto zero = load_constant(
                PackedLogic4(work_width, Logic4::zero).promoted_to_logic9(),
                frontend::ValueDomain::Logic9);
            lowered_source = zero;
        } else {
            const auto count = load_constant(
                unsigned_value(amount, 32U), frontend::ValueDomain::Bit2);
            const auto shifted = allocate_register(
                work_width, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Shift {
                ShiftOperator::logical_left,
                shifted,
                *lowered_source,
                count,
                false,
            });
            lowered_source = shifted;
        }
    }
    auto resized = resize_register(
        *lowered_source, result_width, signed_source);
    if (register_domain(resized) != frontend::ValueDomain::Logic9) {
        const auto promoted = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(
            CopyRegister { promoted, resized });
        resized = promoted;
    }
    return { true, resized };
}

} // namespace fsim::elaboration
