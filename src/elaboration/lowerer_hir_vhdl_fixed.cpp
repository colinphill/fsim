// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string fixed_canonical_name(std::string_view name)
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

bool fixed_subtype_name(const semantic::vhdl::SubtypeIndication& subtype)
{
    const auto name = fixed_canonical_name(subtype.type_mark.spelling);
    return name == "ufixed" || name == "sfixed"
        || name == "unresolved_ufixed" || name == "unresolved_sfixed";
}

bool signed_fixed_subtype(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    const auto name = fixed_canonical_name(subtype.type_mark.spelling);
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

PackedLogic4 one_hot_value(
    const std::size_t bit,
    const std::size_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    result.set(bit, Logic4::one);
    return result.promoted_to_logic9();
}

} // namespace

bool Lowerer::is_hir_vhdl_fixed_function(
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
    const auto name = fixed_canonical_name(source.text);
    if (name != "to_ufixed" && name != "to_sfixed"
        && name != "resize") {
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
    return semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.vhdl_standard_package_member_visible(
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
    const auto name = fixed_canonical_name(source.text);
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
    const auto context_left = constraint->left
        ? constraint->left
        : constraint->left_expression
        ? hir_constant_integer(*constraint->left_expression)
        : std::nullopt;
    const auto context_right = constraint->right
        ? constraint->right
        : constraint->right_expression
        ? hir_constant_integer(*constraint->right_expression)
        : std::nullopt;
    if (context_left && context_right
        && *context_left == *left && *context_right == *right
        && constraint->descending) {
        return true;
    }
    report(
        "FSIM-ELAB-VHFIX-004",
        fixed_canonical_name(source.text)
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
    const auto span = hir_source_span(source.source);
    const auto operand_span = [&](const semantic::ExpressionId operand) {
        const auto record = specialized_hir_unit_->find_expression(operand);
        return record && record->vhdl != nullptr
            ? hir_source_span(record->vhdl->source)
            : span;
    };
    const auto name = fixed_canonical_name(source.text);
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

    if (conversion) {
        const auto integer = hir_constant_integer(source.operands.front());
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
            static_cast<std::size_t>(scale_value),
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
    const auto source_left = source_constraint->left
        ? source_constraint->left
        : source_constraint->left_expression
        ? hir_constant_integer(*source_constraint->left_expression)
        : std::nullopt;
    const auto source_right = source_constraint->right
        ? source_constraint->right
        : source_constraint->right_expression
        ? hir_constant_integer(*source_constraint->right_expression)
        : std::nullopt;
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
            const auto zero = allocate_register(
                result_width, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(LoadConstant {
                zero,
                PackedLogic4(result_width, Logic4::zero)
                    .promoted_to_logic9(),
            });
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
        const auto count = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            count, unsigned_value(amount, 32U) });
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
            const auto zero = allocate_register(
                work_width, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(LoadConstant {
                zero,
                PackedLogic4(work_width, Logic4::zero)
                    .promoted_to_logic9(),
            });
            lowered_source = zero;
        } else {
            const auto count = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                count, unsigned_value(amount, 32U) });
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
