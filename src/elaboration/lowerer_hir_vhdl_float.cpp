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

bool float_type_name(const std::string_view spelling)
{
    const auto name = lowercase_vhdl_simple_name(spelling);
    return name == "float" || name == "unresolved_float"
        || name == "u_float" || name == "float32"
        || name == "unresolved_float32" || name == "u_float32";
}

bool float_subtype_name(const semantic::vhdl::SubtypeIndication& subtype)
{
    return float_type_name(subtype.type_mark.spelling);
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

bool float_arithmetic_operator(const std::string_view text)
{
    return text == "+" || text == "-" || text == "*" || text == "/";
}

bool float_relational_operator(const std::string_view text)
{
    return text == "=" || text == "/=" || text == "<" || text == "<="
        || text == ">" || text == ">=";
}

} // namespace

bool Lowerer::hir_vhdl_intrinsic_member_present(
    const std::string_view member) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto& design = specialized_hir_unit_->design();
    auto& cached = hir_vhdl_intrinsic_member_presence_[std::string { member }];
    if (cached.first != &design) {
        cached = { &design,
            std::ranges::any_of(design.vhdl_units(),
                [&](const semantic::vhdl::Unit& unit) {
                    return std::ranges::find(
                               unit.standard_package_declarations, member)
                        != unit.standard_package_declarations.end();
                }) };
    }
    return cached.second;
}

bool Lowerer::hir_vhdl_float_valued(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::binary) {
        return float_arithmetic_operator(source.text)
            && is_hir_vhdl_float_operator(expression_id);
    }
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && float_result_name(lowercase_vhdl_simple_name(source.text))
        && is_hir_vhdl_float_function(expression_id)) {
        return true;
    }
    auto subtype = hir_vhdl_expression_subtype(expression_id);
    // A user subtype of a float type (`subtype t is float32`) is one too.
    const auto float_subtype = [&](const semantic::vhdl::SubtypeIndication& value) {
        return float_subtype_name(value)
            || float_type_name(
                hir_vhdl_subtype_chain(value, source.scope).spelling);
    };
    if (subtype && !float_subtype(*subtype)) {
        subtype = hir_effective_vhdl_subtype(*subtype);
    }
    return subtype && float_subtype(*subtype)
        && hir_expression_width(expression_id, hir_process_scope_)
            == std::optional<std::size_t> { 32U };
}

bool Lowerer::is_hir_vhdl_float_operator(
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
        || (!float_arithmetic_operator(expression->vhdl->text)
            && !float_relational_operator(expression->vhdl->text))) {
        return false;
    }
    if (!hir_vhdl_intrinsic_member_present("float32")) {
        return false;
    }
    const auto& source = *expression->vhdl;
    // A user operator overload keeps its own meaning; one whose formals
    // are not floats (`"+"(a, b : rec)`) is not this operator.
    if (source.referenced_name && source.referenced_name->selected) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *source.referenced_name->selected);
        if (declaration && declaration->vhdl != nullptr) {
            static const std::vector<semantic::DeclarationId> none;
            const auto& formals = declaration->vhdl->callable
                ? declaration->vhdl->callable->formals
                : none;
            const bool float_formal = std::ranges::any_of(formals,
                [&](const semantic::DeclarationId formal_id) {
                    const auto formal
                        = specialized_hir_unit_->find_declaration(formal_id);
                    return formal && formal->vhdl != nullptr
                        && formal->vhdl->subtype
                        && (float_subtype_name(*formal->vhdl->subtype)
                            || float_type_name(hir_vhdl_subtype_chain(
                                *formal->vhdl->subtype,
                                formal->vhdl->scope).spelling));
                });
            if (formals.empty() || float_formal) {
                return false;
            }
        }
    }
    const auto lhs = hir_vhdl_float_valued(source.operands[0]);
    const auto rhs = hir_vhdl_float_valued(source.operands[1]);
    if (!lhs && !rhs) {
        return false;
    }
    const auto other = lhs ? source.operands[1] : source.operands[0];
    return (lhs && rhs) || hir_vhdl_expression_is_real(other)
        || hir_expression_domain(other, hir_process_scope_)
            == std::optional { frontend::ValueDomain::Integer };
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_float_as_real(
    const semantic::ExpressionId expression_id)
{
    using Kind = frontend::SystemVerilogScalarKind;
    if (hir_vhdl_float_valued(expression_id)) {
        const auto bits = lower_hir_expression(expression_id, 32U);
        if (!bits) {
            return std::nullopt;
        }
        const auto payload = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CopyRegister { payload, *bits });
        const auto real = allocate_register(64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            runtime::SystemVerilogScalarBinaryOperator::Convert, real,
            payload, payload, Kind::ShortReal, Kind::ShortReal, Kind::Real });
        return real;
    }
    if (hir_vhdl_expression_is_real(expression_id)) {
        return lower_hir_expression(expression_id, 64U, Kind::Real);
    }
    if (hir_expression_domain(expression_id, hir_process_scope_)
        == std::optional { frontend::ValueDomain::Integer }) {
        const auto width = hir_expression_width(
            expression_id, hir_process_scope_).value_or(32U);
        const auto integer = lower_hir_expression(expression_id, width);
        if (!integer) {
            return std::nullopt;
        }
        const auto real = allocate_register(64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogMath {
            .function = runtime::SystemVerilogMathFunction::Itor,
            .destination = real,
            .first = *integer,
            .first_width = static_cast<std::uint32_t>(
                register_width(*integer)),
            .first_signed = true,
        });
        return real;
    }
    return std::nullopt;
}

RegisterId Lowerer::hir_vhdl_real_to_float_bits(const RegisterId real)
{
    using Kind = frontend::SystemVerilogScalarKind;
    const auto payload = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogScalarBinary {
        runtime::SystemVerilogScalarBinaryOperator::Convert, payload, real,
        real, Kind::Real, Kind::Real, Kind::ShortReal });
    const auto bits = allocate_register(32U, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(CopyRegister { bits, payload });
    return bits;
}

bool Lowerer::is_hir_vhdl_float_function(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (is_hir_vhdl_float_operator(expression_id)) {
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
    if (!float_result_name(name) && !boolean_result_name(name)
        && !packed_result_name(name) && name != "to_integer"
        && name != "to_real") {
        return false;
    }
    if (!hir_resolver().vhdl_standard_package_member_visible(
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
    return hir_vhdl_float_valued(source.operands.front());
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
    if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::binary) {
        return float_relational_operator(expression->vhdl->text)
            ? HirVhdlIntrinsicProfile {
                  1U, frontend::ValueDomain::Boolean, false }
            : HirVhdlIntrinsicProfile {
                  32U, frontend::ValueDomain::Logic9, false };
    }
    const auto name = lowercase_vhdl_simple_name(expression->vhdl->text);
    if (name == "to_real") {
        return HirVhdlIntrinsicProfile {
            64U, frontend::ValueDomain::Bit2, false };
    }
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
    const auto name = lowercase_vhdl_simple_name(expression->vhdl->text);
    if (expression->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || !float_result_name(name)) {
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(context)
        .value_or(context);
    auto valid = float_subtype_name(effective)
        || float_type_name(
            hir_vhdl_subtype_chain(context, expression->vhdl->scope).spelling);
    if (valid && !effective.constraints.empty()) {
        const auto& range = effective.constraints.front();
        const auto left = hir_range_left(range);
        const auto right = hir_range_right(range);
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
    const auto root_name = lowercase_vhdl_simple_name(source.text);
    const auto span = hir_source_span(source.source);
    using ScalarKind = frontend::SystemVerilogScalarKind;
    using ScalarOperator = runtime::SystemVerilogScalarBinaryOperator;
    // binary32 operations are exact in REAL and rounded once to binary32
    // (round_nearest, IEEE 1076-2019 G.5).
    const auto real_operation = [&](const ScalarOperator operation,
                                    const RegisterId lhs,
                                    const RegisterId rhs) {
        const auto compare = operation >= ScalarOperator::Equal;
        const auto destination = allocate_register(
            compare ? 1U : 64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            operation, destination, lhs, rhs, ScalarKind::Real,
            ScalarKind::Real,
            compare ? ScalarKind::None : ScalarKind::Real });
        if (!compare) {
            return destination;
        }
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(CopyRegister { result, destination });
        return result;
    };
    const auto scalar_operator = [](const std::string_view text)
        -> std::optional<ScalarOperator> {
        if (text == "+" || text == "add") {
            return ScalarOperator::Add;
        }
        if (text == "-" || text == "subtract") {
            return ScalarOperator::Subtract;
        }
        if (text == "*" || text == "multiply") {
            return ScalarOperator::Multiply;
        }
        if (text == "/" || text == "divide") {
            return ScalarOperator::Divide;
        }
        if (text == "=" || text == "eq") {
            return ScalarOperator::Equal;
        }
        if (text == "/=" || text == "ne") {
            return ScalarOperator::NotEqual;
        }
        if (text == "<" || text == "lt") {
            return ScalarOperator::Less;
        }
        if (text == "<=" || text == "le") {
            return ScalarOperator::LessEqual;
        }
        if (text == ">" || text == "gt") {
            return ScalarOperator::Greater;
        }
        if (text == ">=" || text == "ge") {
            return ScalarOperator::GreaterEqual;
        }
        return std::nullopt;
    };
    const auto runtime_binary = [&](const std::string_view text)
        -> std::optional<RegisterId> {
        const auto operation = scalar_operator(text);
        if (!operation || source.operands.size() < 2U) {
            return std::nullopt;
        }
        const auto lhs = lower_hir_vhdl_float_as_real(source.operands[0]);
        const auto rhs = lhs
            ? lower_hir_vhdl_float_as_real(source.operands[1])
            : std::nullopt;
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto value = real_operation(*operation, *lhs, *rhs);
        return *operation >= ScalarOperator::Equal
            ? value
            : hir_vhdl_real_to_float_bits(value);
    };
    if (source.kind == semantic::vhdl::ExpressionKind::binary) {
        const auto value = runtime_binary(source.text);
        if (!value) {
            report("FSIM-ELAB-VHFLT-003",
                "binary32 operator '" + source.text
                    + "' requires FLOAT, REAL or INTEGER operands",
                span);
        }
        return { true, value };
    }
    if (root_name == "to_real") {
        const auto value = source.operands.size() == 1U
            ? lower_hir_vhdl_float_as_real(source.operands.front())
            : std::nullopt;
        if (!value) {
            report("FSIM-ELAB-VHFLT-003",
                "to_real requires one binary32 FLOAT operand", span);
        }
        return { true, value };
    }

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
        const auto name = lowercase_vhdl_simple_name(call.text);
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

    // TO_FLOAT of a run-time REAL or INTEGER with the binary32 sizes.
    const auto runtime_float = [&]() -> std::optional<RegisterId> {
        if (root_name == "to_float" && !source.operands.empty()) {
            const auto sizes = source.operands.size() == 1U
                || (source.operands.size() == 2U
                    && hir_vhdl_float_valued(source.operands[1]))
                || (source.operands.size() >= 3U
                    && hir_constant_integer(source.operands[1])
                        == std::optional<std::int64_t> { 8 }
                    && hir_constant_integer(source.operands[2])
                        == std::optional<std::int64_t> { 23 });
            const auto argument = source.operands.front();
            if (!sizes
                || (!hir_vhdl_expression_is_real(argument)
                    && hir_expression_domain(argument, hir_process_scope_)
                        != std::optional {
                            frontend::ValueDomain::Integer })) {
                return std::nullopt;
            }
            const auto real = lower_hir_vhdl_float_as_real(argument);
            return real ? std::optional { hir_vhdl_real_to_float_bits(*real) }
                        : std::nullopt;
        }
        if (root_name == "add" || root_name == "subtract"
            || root_name == "multiply" || root_name == "divide") {
            return runtime_binary(root_name);
        }
        if (root_name == "abs" && source.operands.size() == 1U) {
            const auto real = lower_hir_vhdl_float_as_real(
                source.operands.front());
            if (!real) {
                return std::nullopt;
            }
            const auto zero = load_constant(
                unsigned_value(std::bit_cast<std::uint64_t>(0.0), 64U),
                frontend::ValueDomain::Bit2);
            const auto negative = real_operation(
                ScalarOperator::Less, *real, zero);
            const auto negated = real_operation(
                ScalarOperator::Subtract, zero, *real);
            const auto magnitude = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ConditionalSelect {
                magnitude, negative, negated, *real });
            return hir_vhdl_real_to_float_bits(magnitude);
        }
        return std::nullopt;
    };
    if (float_result_name(root_name) && !evaluate(expression_id)) {
        if (const auto value = runtime_float()) {
            return { true, value };
        }
    }
    if (boolean_result_name(root_name) && scalar_operator(root_name)
        && (!evaluate(source.operands.front())
            || (source.operands.size() > 1U
                && !evaluate(source.operands[1])))) {
        if (const auto value = runtime_binary(root_name)) {
            return { true, value };
        }
    }
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
        const auto result = load_integer(static_cast<std::int64_t>(rounded),
            integer_width, frontend::ValueDomain::Integer);
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
    const auto result
        = load_constant(unsigned_value(result_value ? 1U : 0U, 1U),
            frontend::ValueDomain::Boolean);
    return { true, result };
}

} // namespace fsim::elaboration
