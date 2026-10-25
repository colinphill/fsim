// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "fsim/support/vhdl_fixed_bounds.hpp"

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

// A value of `width` bits whose low `count` bits are one.
PackedLogic4 low_ones_value(const std::size_t count, const std::size_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    for (std::size_t bit { }; bit < count && bit < width; ++bit) {
        result.set(bit, Logic4::one);
    }
    return result.promoted_to_logic9();
}

bool fixed_shift_function(const std::string_view name)
{
    return name == "shift_left" || name == "shift_right";
}

// UFIXED_HIGH, SFIXED_LOW and their relatives give the index bounds of an
// operation's result (IEEE 1076-2019 fixed_generic_pkg).
bool fixed_bound_function(const std::string_view name)
{
    return name == "ufixed_high" || name == "ufixed_low"
        || name == "sfixed_high" || name == "sfixed_low";
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
        const auto lhs = hir_vhdl_fixed_operand_range(
            source.operands[0], source.operands[1], depth + 1U);
        const auto rhs = hir_vhdl_fixed_operand_range(
            source.operands[1], source.operands[0], depth + 1U);
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
        && is_hir_vhdl_fixed_function(expression_id)) {
        return hir_vhdl_fixed_call_range(expression_id, depth + 1U);
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
        && source.kind == semantic::vhdl::ExpressionKind::call) {
        // A user function with an unconstrained fixed result has the range
        // its body proves for this call.
        const auto shape = hir_vhdl_function_result_shape(
            expression_id, hir_process_scope_);
        if (shape && !shape->record && !shape->null_range
            && shape->range.descending
            && shape->range.left >= shape->range.right) {
            return HirVhdlFixedRange { shape->range.left,
                shape->range.right, signed_fixed_subtype(*subtype) };
        }
        return std::nullopt;
    }
    if (constraint == subtype->constraints.end()) {
        // An object of an unconstrained fixed subtype, such as an element of
        // a record formal, has the range of the value it holds.
        const auto shape = source.kind != semantic::vhdl::ExpressionKind::call
            ? hir_vhdl_shape(expression_id)
            : std::nullopt;
        if (shape && shape->ranges.size() == 1U && shape->ranges.front()
            && shape->ranges.front()->descending
            && shape->ranges.front()->left >= shape->ranges.front()->right) {
            return HirVhdlFixedRange { shape->ranges.front()->left,
                shape->ranges.front()->right, signed_fixed_subtype(*subtype) };
        }
        return std::nullopt;
    }
    if (!constraint->descending) {
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

bool Lowerer::hir_vhdl_fixed_subtype(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    return fixed_subtype_name(subtype);
}

std::optional<Lowerer::HirVhdlFixedRange>
Lowerer::hir_vhdl_fixed_call_range(
    const semantic::ExpressionId expression_id,
    const std::size_t depth) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr || depth > 64U
        || expression->vhdl->operands.empty()) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto name = lowercase_vhdl_simple_name(source.text);
    if (fixed_shift_function(name)) {
        return hir_vhdl_fixed_range(source.operands.front(), depth + 1U);
    }
    if (name != "to_ufixed" && name != "to_sfixed" && name != "resize"
        && !fixed_binary_string_function(name)) {
        return std::nullopt;
    }
    auto signed_value = name == "to_sfixed";
    if (name == "resize") {
        // The overload, and so the result's signedness, follows the
        // operand's type; its bounds come from the size arguments alone.
        if (const auto operand = hir_vhdl_fixed_range(
                source.operands.front(), depth + 1U)) {
            signed_value = operand->signed_value;
        } else {
            auto subtype = hir_vhdl_expression_subtype(
                source.operands.front());
            if (subtype) {
                subtype = hir_effective_vhdl_subtype(*subtype).value_or(
                    *subtype);
            }
            if (!subtype || !fixed_subtype_name(*subtype)) {
                return std::nullopt;
            }
            signed_value = signed_fixed_subtype(*subtype);
        }
    }
    if (source.operands.size() == 3U) {
        const auto left = hir_constant_integer(source.operands[1]);
        const auto right = hir_constant_integer(source.operands[2]);
        if (!left || !right || *left < *right) {
            return std::nullopt;
        }
        return HirVhdlFixedRange { *left, *right, signed_value };
    }
    if (source.operands.size() != 2U || fixed_binary_string_function(name)) {
        return std::nullopt;
    }
    // A size_res argument gives its own range; an integer one is the left
    // index of a result whose right index defaults to zero.
    if (const auto size = hir_vhdl_fixed_range(
            source.operands[1], depth + 1U)) {
        return HirVhdlFixedRange { size->left, size->right, signed_value };
    }
    if (name != "resize") {
        if (const auto left = hir_constant_integer(source.operands[1]);
            left && *left >= 0) {
            return HirVhdlFixedRange { *left, 0, signed_value };
        }
    }
    return std::nullopt;
}

std::optional<Lowerer::HirVhdlFixedRange>
Lowerer::hir_vhdl_fixed_operand_range(
    const semantic::ExpressionId operand,
    const semantic::ExpressionId other,
    const std::size_t depth) const
{
    if (auto range = hir_vhdl_fixed_range(operand, depth)) {
        return range;
    }
    const auto partner = hir_vhdl_fixed_range(other, depth);
    if (!partner) {
        return std::nullopt;
    }
    // "+" (L : SFIXED; R : REAL) is L + TO_SFIXED (R, L'HIGH, L'LOW), and
    // with an INTEGER, L + TO_SFIXED (R, L'HIGH, 0).
    if (hir_vhdl_expression_is_real(operand)) {
        return partner;
    }
    if (hir_expression_domain(operand, hir_process_scope_)
        == std::optional { frontend::ValueDomain::Integer }) {
        return HirVhdlFixedRange { std::max<std::int64_t>(partner->left, 0),
            0, partner->signed_value };
    }
    return std::nullopt;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_fixed_operand(
    const semantic::ExpressionId operand,
    const HirVhdlFixedRange& range)
{
    const auto width = static_cast<std::size_t>(range.left - range.right + 1);
    if (hir_vhdl_fixed_range(operand)) {
        return lower_hir_expression(operand, width);
    }
    const auto real = lower_hir_vhdl_float_as_real(operand);
    return real ? lower_hir_vhdl_real_to_fixed(*real, range) : std::nullopt;
}

RegisterId Lowerer::lower_hir_vhdl_fixed_resize(
    const RegisterId value,
    const HirVhdlFixedRange& from,
    const std::int64_t left,
    const std::int64_t right)
{
    const bool signed_value = from.signed_value;
    const auto result_width = static_cast<std::size_t>(left - right + 1);
    const auto logic = frontend::ValueDomain::Logic9;
    const auto as_logic = [&](const RegisterId candidate) {
        if (register_domain(candidate) == logic) {
            return candidate;
        }
        const auto promoted = allocate_register(
            register_width(candidate), logic);
        process_.operations.emplace_back(CopyRegister { promoted, candidate });
        return promoted;
    };
    // Every bit of the value lies below the result's least significant
    // index: the result is the value's sign.
    if (right > from.left + 1) {
        if (!signed_value) {
            return load_constant(
                PackedLogic4(result_width, Logic4::zero).promoted_to_logic9(),
                logic);
        }
        const auto sign = allocate_register(1U, logic);
        process_.operations.emplace_back(Extract { sign, value,
            static_cast<std::uint32_t>(from.left - from.right), 1U });
        return as_logic(resize_register(sign, result_width, true));
    }
    // The value at its exact binary point, with room for the rounding
    // carry and the saturation bounds.
    const auto low = std::min(from.right, right);
    const auto work_width = static_cast<std::size_t>(
        std::max(from.left, left) - low + 3);
    const auto constant = [&](PackedLogic4 bits) {
        return load_constant(std::move(bits), logic);
    };
    const auto binary = [&](const BinaryOperator operation,
                            const RegisterId lhs, const RegisterId rhs,
                            const std::size_t width) {
        const auto destination = allocate_register(width, logic);
        process_.operations.emplace_back(
            Binary { operation, destination, lhs, rhs });
        return destination;
    };
    const auto shift = [&](const ShiftOperator operation,
                           const RegisterId operand,
                           const std::size_t amount) {
        const auto count = load_constant(
            unsigned_value(amount, 32U), frontend::ValueDomain::Bit2);
        const auto shifted = allocate_register(register_width(operand), logic);
        process_.operations.emplace_back(
            Shift { operation, shifted, operand, count, false });
        return shifted;
    };
    auto work = as_logic(resize_register(value, work_width, signed_value));
    if (from.right > low) {
        work = shift(ShiftOperator::logical_left, work,
            static_cast<std::size_t>(from.right - low));
    }
    if (right > low) {
        // fixed_round: to nearest, a tie to the even result.
        const auto amount = static_cast<std::size_t>(right - low);
        const auto quotient = shift(signed_value
                ? ShiftOperator::arithmetic_right
                : ShiftOperator::logical_right,
            work, amount);
        const auto remainder = binary(BinaryOperator::bit_and, work,
            constant(low_ones_value(amount, work_width)), work_width);
        const auto half = constant(one_hot_value(amount - 1U, work_width));
        const auto above = binary(BinaryOperator::greater_unsigned,
            remainder, half, 1U);
        const auto tie = binary(BinaryOperator::equal, remainder, half, 1U);
        const auto odd = allocate_register(1U, logic);
        process_.operations.emplace_back(
            Extract { odd, quotient, 0U, 1U });
        const auto rounds = binary(BinaryOperator::bit_or, above,
            binary(BinaryOperator::bit_and, tie, odd, 1U), 1U);
        work = binary(BinaryOperator::add_unsigned, quotient,
            as_logic(resize_register(rounds, work_width, false)),
            work_width);
    }
    // fixed_saturate: a value beyond the result's range takes its bound.
    const auto magnitude_bits = signed_value ? result_width - 1U
                                             : result_width;
    const auto maximum = constant(low_ones_value(magnitude_bits, work_width));
    const auto above = binary(signed_value
            ? BinaryOperator::greater_signed
            : BinaryOperator::greater_unsigned,
        work, maximum, 1U);
    const auto saturated_high = allocate_register(work_width, logic);
    process_.operations.emplace_back(
        ConditionalSelect { saturated_high, above, maximum, work });
    work = saturated_high;
    if (signed_value) {
        auto minimum_bits = PackedLogic4(work_width, Logic4::zero);
        for (auto bit = magnitude_bits; bit < work_width; ++bit) {
            minimum_bits.set(bit, Logic4::one);
        }
        const auto minimum = constant(minimum_bits.promoted_to_logic9());
        const auto below = binary(BinaryOperator::less_signed, work,
            minimum, 1U);
        const auto saturated_low = allocate_register(work_width, logic);
        process_.operations.emplace_back(
            ConditionalSelect { saturated_low, below, minimum, work });
        work = saturated_low;
    }
    return as_logic(resize_register(work, result_width, signed_value));
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_real_to_fixed(
    const RegisterId real,
    const HirVhdlFixedRange& range)
{
    using ScalarKind = frontend::SystemVerilogScalarKind;
    const auto result_width = static_cast<std::size_t>(
        range.left - range.right + 1);
    if (range.right > 0 || -range.right >= 62) {
        return std::nullopt;
    }
    const auto scale = load_constant(
        unsigned_value(std::bit_cast<std::uint64_t>(
                           std::ldexp(1.0, static_cast<int>(-range.right))),
            64U),
        frontend::ValueDomain::Bit2);
    const auto scaled = allocate_register(64U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogScalarBinary {
        runtime::SystemVerilogScalarBinaryOperator::Multiply, scaled, real,
        scale, ScalarKind::Real, ScalarKind::Real, ScalarKind::Real });
    const auto value_bits = range.signed_value ? result_width - 1U
                                               : result_width;
    const auto limit = value_bits >= 62U
        ? (std::int64_t { 1 } << 62)
        : (std::int64_t { 1 } << value_bits);
    const auto bound = [&](const std::int64_t value) {
        return load_constant(
            unsigned_value(
                std::bit_cast<std::uint64_t>(static_cast<double>(value)), 64U),
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
        bound(range.signed_value ? -limit : 0));
    const auto rounded = lower_hir_vhdl_real_to_integer(
        clamped, 64U, std::nullopt);
    auto value = resize_register(rounded, result_width, true);
    if (register_domain(value) != frontend::ValueDomain::Logic9) {
        const auto promoted = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister { promoted, value });
        value = promoted;
    }
    return value;
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
    // A user operator overload keeps its own meaning. A generic formal
    // operator denotes its actual: the fixed package's operator when bound
    // to it or left to the visible default (IEEE 1076-2008 6.5.6.2).
    if (source.referenced_name && source.referenced_name->selected) {
        const auto formal = *source.referenced_name->selected;
        const auto declaration = specialized_hir_unit_->find_declaration(
            formal);
        const auto& design = specialized_hir_unit_->design();
        const auto fixed_package = [&](const semantic::ScopeId scope) {
            return design.vhdl_scope_in_package(scope, "ieee", "fixed_pkg")
                || design.vhdl_scope_in_package(
                    scope, "ieee", "fixed_generic_pkg");
        };
        if (declaration && declaration->vhdl != nullptr
            && !fixed_package(declaration->vhdl->scope)
            && declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::generic_function) {
            // Analysis can retain the only listed user overload even when
            // the operands match no profile of it; the package operator is
            // then the one denoted (IEEE 1076-2008 12.5).
            if (resolve_hir_vhdl_function_call(
                    expression_id, hir_process_scope_, 0U)) {
                return false;
            }
        } else if (declaration && declaration->vhdl != nullptr
            && !fixed_package(declaration->vhdl->scope)) {
            std::optional<semantic::DeclarationId> actual;
            bool bound { };
            for (auto frame = hir_generic_binding_frames_.rbegin();
                 frame != hir_generic_binding_frames_.rend() && !bound;
                 ++frame) {
                const auto binding = std::ranges::find(
                    *frame, formal, &HirGenericBinding::formal);
                if (binding != frame->end()) {
                    bound = true;
                    actual = binding->actual_declaration;
                }
            }
            if (!bound) {
                const auto& actuals
                    = specialized_hir_unit_->specialization().actual_identities;
                const auto identity = std::ranges::find(actuals, formal,
                    &semantic::SpecializedHirActualIdentity::declaration);
                if (identity != actuals.end()) {
                    actual = identity->actual_declaration;
                }
            }
            const auto actual_declaration = actual
                ? specialized_hir_unit_->find_declaration(*actual)
                : std::nullopt;
            if (actual
                && (!actual_declaration || actual_declaration->vhdl == nullptr
                    || !fixed_package(actual_declaration->vhdl->scope))) {
                return false;
            }
        }
    }
    const auto lhs = hir_vhdl_fixed_operand_range(
        source.operands[0], source.operands[1]);
    const auto rhs = lhs
        ? hir_vhdl_fixed_operand_range(source.operands[1], source.operands[0])
        : std::nullopt;
    return lhs && rhs && lhs->signed_value == rhs->signed_value;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_fixed_operator(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto& source = *expression->vhdl;
    const auto lhs_range = hir_vhdl_fixed_operand_range(
        source.operands[0], source.operands[1]);
    const auto rhs_range = hir_vhdl_fixed_operand_range(
        source.operands[1], source.operands[0]);
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
    const auto lhs = lower_hir_vhdl_fixed_operand(
        source.operands[0], *lhs_range);
    const auto rhs = lhs
        ? lower_hir_vhdl_fixed_operand(source.operands[1], *rhs_range)
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
    const bool fixed_operand = name == "to_real" || name == "to_integer"
        || fixed_shift_function(name)
        || (name == "resize" && source.operands.size() != 3U);
    if (name != "to_ufixed" && name != "to_sfixed" && name != "resize"
        && !fixed_binary_string_function(name) && !fixed_operand
        && !fixed_bound_function(name)) {
        return false;
    }
    // TO_REAL, TO_INTEGER, the shifts and a size_res RESIZE take a fixed
    // value; other packages overload these names.
    if (fixed_operand
        && (source.operands.empty()
            || !hir_vhdl_fixed_range(source.operands.front()))) {
        return false;
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
    const auto name = lowercase_vhdl_simple_name(source.text);
    if (fixed_bound_function(name) || name == "to_integer") {
        return HirVhdlIntrinsicProfile {
            static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_)),
            frontend::ValueDomain::Integer,
            true,
        };
    }
    if (name == "to_real") {
        return HirVhdlIntrinsicProfile {
            64U, frontend::ValueDomain::Bit2, false };
    }
    const auto range = hir_vhdl_fixed_call_range(expression_id, 0U);
    if (!range) {
        return std::nullopt;
    }
    const auto distance = index_distance(range->left, range->right);
    if (distance >= std::numeric_limits<std::uint32_t>::max()
        || distance >= std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return HirVhdlIntrinsicProfile {
        static_cast<std::size_t>(distance + 1U),
        frontend::ValueDomain::Logic9,
        range->signed_value,
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
    const auto integer_width = static_cast<std::size_t>(
        frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
    if (fixed_bound_function(name)) {
        // (LEFT, RIGHT [, OPERATION [, LEFT2, RIGHT2]]) or
        // (SIZE_RES [, OPERATION [, SIZE_RES2]]).
        const auto& operands = source.operands;
        const auto first = operands.empty()
            ? std::optional<HirVhdlFixedRange> { }
            : hir_vhdl_fixed_range(operands.front());
        const auto bounds_at = [&](const std::size_t index)
            -> std::optional<std::pair<std::int64_t, std::int64_t>> {
            if (first) {
                if (index >= operands.size()) {
                    return std::pair<std::int64_t, std::int64_t> { 0, 0 };
                }
                const auto range = hir_vhdl_fixed_range(operands[index]);
                return range ? std::optional { std::pair {
                                   range->left, range->right } }
                             : std::nullopt;
            }
            if (index + 1U >= operands.size()) {
                return index >= operands.size()
                    ? std::optional { std::pair<std::int64_t, std::int64_t> {
                          0, 0 } }
                    : std::nullopt;
            }
            const auto left = hir_constant_integer(operands[index]);
            const auto right = hir_constant_integer(operands[index + 1U]);
            return left && right
                ? std::optional { std::pair { *left, *right } }
                : std::nullopt;
        };
        const auto operation_index = first ? 1U : 2U;
        auto operation = 'X';
        if (operation_index < operands.size()) {
            const auto literal = specialized_hir_unit_->find_expression(
                operands[operation_index]);
            if (!literal || literal->vhdl == nullptr
                || literal->vhdl->text.size() != 3U
                || literal->vhdl->text.front() != '\'') {
                report("FSIM-ELAB-VHFIX-001",
                    name + " requires a character literal operation", span);
                return { true, std::nullopt };
            }
            operation = literal->vhdl->text[1];
        }
        const auto primary = bounds_at(0U);
        const auto secondary = bounds_at(operation_index + 1U);
        if (!primary || !secondary) {
            report("FSIM-ELAB-VHFIX-001",
                name + " requires locally static index bounds", span);
            return { true, std::nullopt };
        }
        const auto value = support::vhdl_fixed_bound(name.ends_with("_high"),
            name.starts_with("sfixed"), primary->first, primary->second,
            operation, secondary->first, secondary->second);
        return { true, load_integer(value, integer_width,
                           frontend::ValueDomain::Integer) };
    }
    if (name == "to_real" || name == "to_integer"
        || fixed_shift_function(name)) {
        const auto range = hir_vhdl_fixed_range(source.operands.front());
        const auto width = static_cast<std::size_t>(
            range->left - range->right + 1);
        if (fixed_shift_function(name)) {
            if (source.operands.size() != 2U) {
                report("FSIM-ELAB-VHFIX-001",
                    name + " requires a value and a shift count", span);
                return { true, std::nullopt };
            }
            const auto value = lower_hir_expression(
                source.operands.front(), width);
            const auto count = value
                ? lower_hir_expression(source.operands[1], 32U)
                : std::nullopt;
            if (!value || !count) {
                return { true, std::nullopt };
            }
            const auto shifted = allocate_register(
                width, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Shift {
                name == "shift_left" ? ShiftOperator::logical_left
                    : range->signed_value ? ShiftOperator::arithmetic_right
                                          : ShiftOperator::logical_right,
                shifted, *value, *count, false });
            return { true, shifted };
        }
        if (width > 64U) {
            report("FSIM-ELAB-VHFIX-002",
                name + " of a fixed value wider than 64 bits is not "
                       "representable",
                span);
            return { true, std::nullopt };
        }
        // A metavalue converts to zero (fixed_generic_pkg TO_REAL,
        // TO_INTEGER).
        auto value = lower_hir_expression(source.operands.front(), width);
        if (!value) {
            return { true, std::nullopt };
        }
        if (name == "to_integer") {
            // RESIZE to (LEFT+1 downto 0), or (LEFT downto 0) for UFIXED.
            const auto left = range->left + (range->signed_value ? 1 : 0);
            if (left < 0) {
                return { true, load_integer(0, integer_width,
                                   frontend::ValueDomain::Integer) };
            }
            value = lower_hir_vhdl_fixed_resize(*value, *range, left, 0);
        }
        const auto parity = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Reduction {
            ReductionOperator::bit_xor, parity, *value });
        const auto metavalue = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, metavalue, parity,
            load_constant(PackedLogic4(1U, Logic4::x),
                frontend::ValueDomain::Logic4) });
        const auto value_width = register_width(*value);
        const auto known = allocate_register(
            value_width, register_domain(*value));
        process_.operations.emplace_back(ConditionalSelect { known,
            metavalue,
            load_constant(PackedLogic4(value_width, Logic4::zero),
                register_domain(*value)),
            *value });
        if (name == "to_integer") {
            const auto widened = resize_register(
                known, integer_width, range->signed_value);
            const auto integer = allocate_register(
                integer_width, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(
                CopyRegister { integer, widened });
            return { true, integer };
        }
        const auto payload = allocate_register(
            width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CopyRegister { payload, known });
        const auto integer = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogMath {
            .function = runtime::SystemVerilogMathFunction::Itor,
            .destination = integer,
            .first = payload,
            .first_width = static_cast<std::uint32_t>(width),
            .first_signed = range->signed_value,
        });
        const auto scale = load_constant(
            unsigned_value(std::bit_cast<std::uint64_t>(std::ldexp(
                               1.0, static_cast<int>(range->right))),
                64U),
            frontend::ValueDomain::Bit2);
        const auto real = allocate_register(64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            runtime::SystemVerilogScalarBinaryOperator::Multiply, real,
            integer, scale, frontend::SystemVerilogScalarKind::Real,
            frontend::SystemVerilogScalarKind::Real,
            frontend::SystemVerilogScalarKind::Real });
        return { true, real };
    }
    const auto target = hir_vhdl_fixed_call_range(expression_id, 0U);
    if (!target) {
        report(
            "FSIM-ELAB-VHFIX-001",
            name
                + " requires a value and locally static left and right "
                  "bounds or a constrained size_res",
            span);
        return { true, std::nullopt };
    }
    const auto left = std::optional { target->left };
    const auto right = std::optional { target->right };
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
        // A run-time REAL, INTEGER or binary32 FLOAT rounds to the nearest
        // value of the range and saturates (fixed_round, fixed_saturate;
        // IEEE 1076-2019 G.4).
        const auto operand = source.operands.front();
        if (!integer && *right <= 0 && -*right < 62
            && (hir_vhdl_float_valued(operand)
                || hir_vhdl_expression_is_real(operand)
                || hir_expression_domain(operand, hir_process_scope_)
                    == std::optional { frontend::ValueDomain::Integer })) {
            const auto real = lower_hir_vhdl_float_as_real(operand);
            if (!real) {
                return { true, std::nullopt };
            }
            return { true, lower_hir_vhdl_real_to_fixed(*real,
                               HirVhdlFixedRange { *left, *right,
                                   name == "to_sfixed" }) };
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

    const auto source_range = hir_vhdl_fixed_range(source.operands.front());
    if (!source_range) {
        report(
            "FSIM-ELAB-VHFIX-001",
            "fixed-point resize requires a constrained ufixed or sfixed operand",
            operand_span(source.operands.front()));
        return { true, std::nullopt };
    }
    const auto source_distance = index_distance(
        source_range->left, source_range->right);
    if (source_distance >= std::numeric_limits<std::uint32_t>::max()
        || source_distance >= std::numeric_limits<std::size_t>::max()
        || std::max(source_range->left, *left)
                - std::min(source_range->right, *right)
            >= (std::int64_t { 1 } << 20)) {
        report(
            "FSIM-ELAB-VHFIX-002",
            "fixed-point resize source width is not representable by SimIR",
            span);
        return { true, std::nullopt };
    }
    const auto lowered_source = lower_hir_expression(source.operands.front(),
        static_cast<std::size_t>(source_distance + 1U));
    if (!lowered_source) {
        return { true, std::nullopt };
    }
    return { true,
        lower_hir_vhdl_fixed_resize(*lowered_source, *source_range, *left,
            *right) };
}

} // namespace fsim::elaboration
