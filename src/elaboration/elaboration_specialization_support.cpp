// SPDX-License-Identifier: Apache-2.0
#include "elaboration_specialization_internal.hpp"

namespace fsim::elaboration::elaboration_detail::specialization_detail {

bool vhdl_composite_constant_type(const frontend::Type& type)
{
    const auto separator = type.spelling.find_last_of('.');
    const auto name = std::string_view { type.spelling }.substr(
        separator == std::string::npos ? 0U : separator + 1U);
    return type.vhdl_array.has_value() || !type.packed_members.empty()
        || name == "bit_vector"
        || name == "std_logic_vector"
        || name == "std_ulogic_vector"
        || name == "signed"
        || name == "unsigned"
        || (!type.named_type.empty()
            && type.enumeration_literals.empty()
            && type.width().value_or(0U) > 1U
            && type.domain != frontend::ValueDomain::Integer
            && type.domain != frontend::ValueDomain::Boolean
            && type.domain != frontend::ValueDomain::Bit2)
        || (type.packed_range.has_value()
            && type.enumeration_literals.empty()
            && !type.vhdl_physical
            && type.domain != frontend::ValueDomain::Integer);
}

bool vhdl_unconstrained_builtin_array(const frontend::Type& type)
{
    const auto separator = type.spelling.find_last_of('.');
    const auto name = std::string_view{type.spelling}.substr(
        separator == std::string::npos ? 0U : separator + 1U);
    return !type.packed_range && !type.vhdl_array
        && (name == "bit_vector" || name == "std_logic_vector"
            || name == "std_ulogic_vector" || name == "signed"
            || name == "unsigned");
}

void annotate_systemverilog_constant_casts(
    frontend::Expression& expression,
    const frontend::Type& destination_type,
    const std::vector<frontend::TypeAliasDeclaration>& type_aliases)
{
    for (auto& operand : expression.operands) {
        annotate_systemverilog_constant_casts(
            operand, destination_type, type_aliases);
    }
    for (auto& choices : expression.aggregate_choice_expressions) {
        for (auto& choice : choices) {
            annotate_systemverilog_constant_casts(
                choice, destination_type, type_aliases);
        }
    }
    if (expression.kind != frontend::ExpressionKind::Call
        || !expression.text.starts_with("@sv-cast:")) {
        return;
    }
    const auto type_name = std::string_view { expression.text }.substr(
        std::string_view { "@sv-cast:" }.size());
    const frontend::Type* cast_type = nullptr;
    if (type_name == destination_type.spelling
        || type_name == destination_type.named_type
        || type_name == destination_type.nominal_type) {
        cast_type = &destination_type;
    } else {
        const auto alias = std::ranges::find_if(
            type_aliases,
            [&](const auto& candidate) {
                return candidate.name == type_name;
            });
        if (alias != type_aliases.end()) {
            cast_type = &alias->type;
        }
    }
    if (cast_type == nullptr) {
        return;
    }
    const auto width = cast_type->width();
    if (!width || *width == 0U) {
        return;
    }
    expression.call_result_width = *width;
    expression.call_result_domain = cast_type->domain;
    expression.call_result_signed = cast_type->is_signed;
    expression.nominal_type = cast_type->nominal_type;
}

std::optional<frontend::Expression> vital_constant_expression(
    const frontend::Expression& expression,
    const frontend::Type& type)
{
    if (expression.kind != frontend::ExpressionKind::Identifier) {
        return std::nullopt;
    }
    const auto separator = expression.text.find_last_of('.');
    const auto name = std::string_view { expression.text }.substr(
        separator == std::string::npos ? 0 : separator + 1);
    std::optional<std::string_view> map;
    if (name == "vitaldefaultoutputmap") {
        map = "UX01ZWLH-";
    } else if (name == "vitaldefaultresultmap") {
        map = "UX01";
    } else if (name == "vitaldefaultresultzmap") {
        map = "UX01Z";
    }
    if (map) {
        return frontend::Expression {
            frontend::ExpressionKind::StringLiteral,
            "\"" + std::string { *map } + "\"", { }, expression.span
        };
    }
    if (name != "vitalzerodelay" && name != "vitalzerodelay01"
        && name != "vitalzerodelay01z"
        && name != "vitalzerodelay01zx" && name != "vitaldefdelay01"
        && name != "vitaldefdelay01z") {
        return std::nullopt;
    }
    if (!type.vhdl_array) {
        return frontend::Expression {
            frontend::ExpressionKind::IntegerLiteral,
            "0", { }, expression.span
        };
    }
    const auto total_width = type.width();
    const auto element_width = type.vhdl_array->element_types.empty()
        ? std::optional<std::uint64_t> { }
        : type.vhdl_array->element_types.front().width();
    if (!total_width || !element_width || *element_width == 0
        || *total_width % *element_width != 0) {
        return std::nullopt;
    }
    const auto count = static_cast<std::size_t>(
        *total_width / *element_width);
    std::vector<frontend::Expression> elements;
    elements.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        elements.emplace_back(
            frontend::ExpressionKind::IntegerLiteral,
            "0", std::vector<frontend::Expression> { }, expression.span);
    }
    return frontend::Expression {
        frontend::ExpressionKind::Aggregate,
        "vhdl-aggregate",
        std::move(elements),
        expression.span,
        std::vector<std::string>(count),
        std::vector<std::vector<frontend::Expression>>(count)
    };
}

std::optional<std::int64_t> packed_vhdl_static_value(
    const frontend::Expression& expression,
    const frontend::Type& type,
    std::string& error)
{
    const bool physical_literal = type.vhdl_physical
        && expression.kind == frontend::ExpressionKind::Call
        && expression.text.starts_with("@vhdl-physical:");
    if ((!type.packed_range && !type.vhdl_physical)
        || (!physical_literal
            && expression.kind != frontend::ExpressionKind::Aggregate
            && expression.kind
                != frontend::ExpressionKind::StringLiteral
            && expression.kind
                != frontend::ExpressionKind::LogicLiteral)) {
        return std::nullopt;
    }
    const auto packed = static_vhdl_value(expression, type, error);
    if (!packed) {
        return std::nullopt;
    }
    for (std::size_t bit = 0; bit < packed->width(); ++bit) {
        const auto digit = packed->get(bit);
        if (digit != Logic4::zero && digit != Logic4::one) {
            error = "the packed aggregate contains an unknown or "
                    "high-impedance element";
            return std::nullopt;
        }
    }
    if (packed->width() > 64) {
        const auto extension = type.is_signed
            ? packed->get(63)
            : Logic4::zero;
        for (std::size_t bit = 64; bit < packed->width(); ++bit) {
            if (packed->get(bit) != extension) {
                error = "the packed aggregate value does not fit its "
                        "portable scalar representation";
                return std::nullopt;
            }
        }
    }
    const auto word = packed->low_word();
    if (type.vhdl_physical) {
        return static_cast<std::int64_t>(
            static_cast<std::int32_t>(
                static_cast<std::uint32_t>(word.aval)));
    }
    return static_cast<std::int64_t>(word.aval);
}

std::string vhdl_value_identity(
    const frontend::Type& type,
    const std::int64_t value)
{
    std::string result = "vhdlconst-v1;domain="
        + std::to_string(static_cast<unsigned>(type.domain))
        + ";width=" + std::to_string(type.width().value_or(0))
        + ";signed=" + (type.is_signed ? "1" : "0")
        + ";type=" + type.spelling
        + ";nominal=" + type.nominal_type;
    if (type.packed_range) {
        result += ";packed="
            + std::to_string(type.packed_range->left) + ":"
            + std::to_string(type.packed_range->right) + ":"
            + (type.packed_range->descending ? "down" : "up");
    }
    if (type.integer_range) {
        result += ";integer="
            + std::to_string(type.integer_range->left) + ":"
            + std::to_string(type.integer_range->right);
    }
    if (type.enumeration_range) {
        result += ";enumeration="
            + std::to_string(type.enumeration_range->left) + ":"
            + std::to_string(type.enumeration_range->right);
    }
    result += ";value=" + std::to_string(value);
    return result;
}

std::array<std::uint64_t, 3> transition_delays(
    const frontend::Delay& delay)
{
    const auto rise = delay.magnitude;
    const auto fall = delay.additional_values.empty()
        ? rise
        : delay.additional_values.front().magnitude;
    const auto turnoff = delay.additional_values.size() < 2
        ? std::min(rise, fall)
        : delay.additional_values[1].magnitude;
    return { rise, fall, turnoff };
}

std::optional<frontend::Delay> combined_delay(
    const frontend::Delay& driver,
    const frontend::Delay& net,
    const frontend::SourceSpan& span,
    std::vector<Diagnostic>& diagnostics)
{
    const auto driver_values = transition_delays(driver);
    const auto net_values = transition_delays(net);
    std::array<std::uint64_t, 3> combined { };
    for (std::size_t index = 0; index < combined.size(); ++index) {
        if (driver_values[index]
            > std::numeric_limits<std::uint64_t>::max()
                - net_values[index]) {
            diagnostics.push_back({ "FSIM-ELAB-SVDELAY-003",
                "combined continuous-assignment and net-declaration delay "
                "overflows the 64-bit simulation time range",
                span });
            return std::nullopt;
        }
        combined[index] = driver_values[index] + net_values[index];
    }
    frontend::Delay result;
    result.magnitude = combined[0];
    result.additional_values.resize(2);
    result.additional_values[0].magnitude = combined[1];
    result.additional_values[1].magnitude = combined[2];
    result.span = span;
    return result;
}

const frontend::Expression* delay_target_base(
    const frontend::Expression& expression)
{
    if ((expression.kind == frontend::ExpressionKind::Index
            || expression.kind == frontend::ExpressionKind::Slice)
        && !expression.operands.empty()) {
        return delay_target_base(expression.operands.front());
    }
    return &expression;
}

void apply_net_delays(
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics)
{
    std::vector<const frontend::SignalDeclaration*> delayed;
    for (const auto& port : unit.ports) {
        if (port.net_delay) {
            delayed.push_back(&port);
        }
    }
    for (const auto& signal : unit.signals) {
        if (signal.net_delay) {
            delayed.push_back(&signal);
        }
    }
    for (auto& statement : unit.concurrent_statements) {
        if (statement.kind != frontend::StatementKind::Assignment
            || statement.assignment_kind
                != frontend::AssignmentKind::Continuous) {
            continue;
        }
        const auto* base = delay_target_base(statement.target);
        if (base->kind != frontend::ExpressionKind::Identifier) {
            continue;
        }
        const frontend::SignalDeclaration* declaration = nullptr;
        for (const auto* candidate : delayed) {
            const bool matches = base->text == candidate->name
                || (base->text.starts_with(candidate->name)
                    && base->text.size() > candidate->name.size()
                    && base->text[candidate->name.size()] == '.');
            if (matches
                && (declaration == nullptr
                    || candidate->name.size()
                        > declaration->name.size())) {
                declaration = candidate;
            }
        }
        if (declaration == nullptr) {
            continue;
        }
        if (!statement.delay) {
            statement.delay = declaration->net_delay;
        } else {
            statement.delay = combined_delay(
                *statement.delay,
                *declaration->net_delay,
                statement.span,
                diagnostics);
        }
    }
}

} // namespace fsim::elaboration::elaboration_detail::specialization_detail
