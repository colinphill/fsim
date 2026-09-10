// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <bit>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

[[maybe_unused]] constexpr std::string_view
    kRetiredLossyCastDiagnostic = "FSIM-ELAB-SVCAST-004";
[[maybe_unused]] constexpr std::string_view
    kRetiredLossyAggregateDiagnostic = "FSIM-ELAB-SVAGG-005";

Lowerer::ExpressionAttempt::ExpressionAttempt() = default;

Lowerer::ExpressionAttempt::ExpressionAttempt(
    const RegisterId result)
    : handled(true)
    , value(result)
{
}

Lowerer::ExpressionAttempt::ExpressionAttempt(
    std::optional<RegisterId> result)
    : handled(true)
    , value(std::move(result))
{
}

Lowerer::ExpressionAttempt::ExpressionAttempt(
    const std::nullopt_t)
    : handled(true)
{
}

std::optional<RegisterId> Lowerer::lower_sv_packed_pattern(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type& expected_type)
{
    const auto aggregate_width = expected_type.width();
    if (!aggregate_width || *aggregate_width != expected_width
        || expected_width == 0
        || expression.aggregate_choices.size()
            != expression.operands.size()
        || expression.aggregate_choice_expressions.size()
            != expression.operands.size()) {
        report(
            "FSIM-ELAB-SVAGG-001",
            "packed assignment-pattern metadata or contextual layout is "
            "inconsistent",
            expression.span);
        return std::nullopt;
    }
    const bool is_union = expected_type.packed_aggregate
            == frontend::PackedAggregateKind::Union
        || expected_type.packed_aggregate
            == frontend::PackedAggregateKind::TaggedUnion;
    const bool tagged_union = expected_type.packed_aggregate
        == frontend::PackedAggregateKind::TaggedUnion;
    const auto tag_width = tagged_union
      ? std::max<std::size_t>(
              1U,
              static_cast<std::size_t>(std::bit_width(
                  expected_type.packed_members.size() - 1U)))
        : 0U;
    const auto destination = allocate_register(expected_width, expected_type.domain);
    process_.operations.emplace_back(LoadConstant {
        destination,
        is_union
            ? PackedLogic4(expected_width, Logic4::zero)
            : default_packed_value(expected_type, expected_width) });
    std::vector<bool> assigned(expected_type.packed_members.size());
    std::optional<std::size_t> default_index;
    std::size_t positional_index = 0;
    bool valid = true;
    const auto insert_member =
        [&](const std::size_t member_index,
            const Expression& value,
            const frontend::SourceSpan& span) {
            if (member_index >= expected_type.packed_members.size()
                || assigned[member_index]) {
                report(
                    "FSIM-ELAB-SVAGG-003",
                    member_index < expected_type.packed_members.size()
                        ? "packed assignment pattern assigns member '"
                            + expected_type.packed_members[member_index].name
                            + "' more than once"
                        : "packed assignment pattern has too many positional "
                          "members",
                    span);
                valid = false;
                return;
            }
            const auto& member = expected_type.packed_members[member_index];
            const auto width = member.width();
            if (!width || *width == 0
                || *width > std::numeric_limits<std::size_t>::max()
                || member.lsb_offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVAGG-001",
                    "packed member '" + member.name
                        + "' has no executable layout",
                    span);
                valid = false;
                return;
            }
            frontend::Type scalar_type;
            const frontend::Type* member_type = &scalar_type;
            if (!member.nested_types.empty()) {
                member_type = &member.nested_types.front();
            } else {
                scalar_type.domain = member.domain;
                scalar_type.spelling = member.spelling;
                scalar_type.packed_range = member.packed_range;
                scalar_type.packed_range_expression = member.packed_range_expression;
                scalar_type.is_signed = member.is_signed;
            }
            if (!validate_sv_nominal_assignment(member_type, value)) {
                valid = false;
                return;
            }
            auto lowered = lower_expression(
                value,
                static_cast<std::size_t>(*width),
                member_type);
            if (!lowered) {
                valid = false;
                return;
            }
            if (register_width(*lowered) != *width) {
                report(
                    "FSIM-ELAB-SVAGG-004",
                    "packed member '" + member.name + "' expects "
                        + std::to_string(*width) + " bits but its value has "
                        + std::to_string(register_width(*lowered)) + " bits",
                    span);
                valid = false;
                return;
            }
            if (is_two_state_domain(member_type->domain)
                && !is_two_state_domain(register_domain(*lowered))) {
                *lowered = convert_to_two_state(*lowered);
            }
            process_.operations.emplace_back(Insert {
                destination,
                destination,
                *lowered,
                static_cast<std::uint32_t>(member.lsb_offset) });
            if (tagged_union) {
                const auto tag = allocate_register(
                    tag_width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    tag,
                    unsigned_value(member_index, tag_width) });
                process_.operations.emplace_back(Insert {
                    destination,
                    destination,
                    tag,
                    static_cast<std::uint32_t>(
                        expected_width - tag_width) });
            }
            assigned[member_index] = true;
        };
    for (std::size_t index = 0;
        index < expression.operands.size(); ++index) {
        const auto& choice = expression.aggregate_choices[index];
        const auto& choices = expression.aggregate_choice_expressions[index];
        if (choice.empty()) {
            insert_member(
                positional_index++,
                expression.operands[index],
                expression.operands[index].span);
            continue;
        }
        if (choice == "default") {
            if (default_index) {
                report(
                    "FSIM-ELAB-SVAGG-003",
                    "packed assignment pattern has more than one default",
                    expression.operands[index].span);
                valid = false;
            } else {
                default_index = index;
            }
            continue;
        }
        if (choice != "@key" || choices.size() != 1
            || choices.front().kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVAGG-002",
                "packed assignment-pattern keys must name direct members",
                expression.operands[index].span);
            valid = false;
            continue;
        }
        const auto member = std::ranges::find_if(
            expected_type.packed_members,
            [&](const frontend::PackedMember& candidate) {
                return candidate.name == choices.front().text;
            });
        if (member == expected_type.packed_members.end()) {
            report(
                "FSIM-ELAB-SVAGG-002",
                "packed assignment pattern names unknown member '"
                    + choices.front().text + "'",
                choices.front().span);
            valid = false;
            continue;
        }
        insert_member(
            static_cast<std::size_t>(std::distance(
                expected_type.packed_members.begin(), member)),
            expression.operands[index],
            expression.operands[index].span);
    }
    if (is_union && default_index) {
        report(
            "FSIM-ELAB-SVAGG-006",
            "a packed union assignment pattern cannot use default",
            expression.operands[*default_index].span);
        valid = false;
    } else if (default_index) {
        for (std::size_t member = 0; member < assigned.size(); ++member) {
            if (!assigned[member]) {
                insert_member(
                    member,
                    expression.operands[*default_index],
                    expression.operands[*default_index].span);
            }
        }
    }
    const auto assigned_count = static_cast<std::size_t>(
        std::ranges::count(assigned, true));
    if (is_union) {
        if (assigned_count != 1) {
            report(
                "FSIM-ELAB-SVAGG-006",
                "a packed union assignment pattern requires exactly one member",
                expression.span);
            valid = false;
        }
    } else {
        for (std::size_t member = 0; member < assigned.size(); ++member) {
            if (!assigned[member]) {
                report(
                    "FSIM-ELAB-SVAGG-003",
                    "packed assignment pattern is missing member '"
                        + expected_type.packed_members[member].name + "'",
                    expression.span);
                valid = false;
            }
        }
    }
    return valid
        ? std::optional<RegisterId> { destination }
        : std::nullopt;
}

std::optional<RegisterId> Lowerer::lower_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    auto attempt = lower_class_expression(
        expression, expected_width, expected_type);
    if (!attempt.handled && language_ == frontend::Language::Vhdl2008) {
        attempt = lower_vhdl_simulator_function_expression(
            expression, expected_width, expected_type);
    }
    if (!attempt.handled) {
        attempt = expression.kind == ExpressionKind::Update
            ? ExpressionAttempt { lower_procedural_update_expression(
                  expression, expected_width, expected_type) }
            : lower_membership_expression(expression);
    }
    if (!attempt.handled) {
        attempt = lower_primary_expression(
            expression, expected_width, expected_type);
    }
    if (!attempt.handled) {
        attempt = lower_unary_attribute_expression(
            expression, expected_width, expected_type);
    }
    if (!attempt.handled) {
        attempt = lower_system_function_expression(
            expression, expected_width, expected_type);
    }
    if (!attempt.handled) {
        attempt = lower_binary_expression(
            expression, expected_width, expected_type);
    }
    if (!attempt.value) {
        return attempt.value;
    }

    const bool scalar_result = expression.kind == ExpressionKind::Binary
        && (expression.text == "=="
            || expression.text == "!="
            || expression.text == "==="
            || expression.text == "!=="
            || expression.text == "==?"
            || expression.text == "!=?"
            || expression.text == "<"
            || expression.text == "<="
            || expression.text == ">"
            || expression.text == ">="
            || expression.text == "&&"
            || expression.text == "||");
    const bool context_determined =
        (language_ == frontend::Language::Vhdl2008
         && expression.kind == ExpressionKind::Conditional)
        || (language_ != frontend::Language::Vhdl2008
        && ((expression.kind == ExpressionKind::Binary
                && !scalar_result)
            || (expression.kind == ExpressionKind::Unary
                && (expression.text == "+"
                    || expression.text == "-"
                    || expression.text == "~"))
            || expression.kind == ExpressionKind::Update
            || (expression.kind == ExpressionKind::Call
                && expression.text == "?:")));
    const auto domain = register_domain(*attempt.value);
    auto profile_domain = ExpressionValueDomain::four_state;
    if (domain == frontend::ValueDomain::Bit2) {
        profile_domain = ExpressionValueDomain::two_state;
    } else if (domain == frontend::ValueDomain::Logic9) {
        profile_domain = ExpressionValueDomain::nine_state;
    } else if (domain == frontend::ValueDomain::Integer) {
        profile_domain = ExpressionValueDomain::integer;
    } else if (domain == frontend::ValueDomain::Boolean) {
        profile_domain = ExpressionValueDomain::boolean;
    }
    process_.expression_profiles.push_back(ExpressionProfile {
        SourceLocation {
            expression.span.source_name.str(),
            static_cast<std::uint32_t>(expression.span.begin.line),
            static_cast<std::uint32_t>(expression.span.begin.column) },
        static_cast<std::uint32_t>(
            register_width(*attempt.value)),
        is_signed_expression(expression),
        context_determined
            ? ExpressionSizingKind::context_determined
            : ExpressionSizingKind::self_determined,
        profile_domain });
    return attempt.value;
}

Lowerer::ExpressionAttempt Lowerer::lower_primary_cast_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (expression.kind == ExpressionKind::Call
        && expression.text == "@vhdl-external") {
        if (expression.operands.size() != 2
            || expression.operands[0].kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHEXTERNAL-001",
                "a VHDL external signal name has malformed semantic HIR",
                expression.span);
            return std::nullopt;
        }
        const auto& target = expression.operands[0];
        const auto& declared = expression.operands[1];
        const auto* actual = object_type(target.text);
        const auto actual_width = actual == nullptr ? std::optional<std::uint64_t> { }
                                                    : actual->width();
        const bool width_mismatch = declared.call_result_width != 0
            && (!actual_width
                || *actual_width != declared.call_result_width);
        const bool domain_mismatch = actual != nullptr
            && declared.call_result_domain
                != frontend::ValueDomain::Unknown
            && actual->domain != declared.call_result_domain;
        const bool signed_mismatch = actual != nullptr
            && actual->is_signed != declared.call_result_signed;
        const bool nominal_mismatch = actual != nullptr
            && !declared.nominal_type.empty()
            && actual->nominal_type != declared.nominal_type;
        if (actual == nullptr || width_mismatch || domain_mismatch
            || signed_mismatch || nominal_mismatch) {
            report(
                "FSIM-ELAB-VHEXTERNAL-001",
                "external signal subtype does not match target '"
                    + target.text + "'",
                expression.span);
            return std::nullopt;
        }
        return ExpressionAttempt { lower_expression(
            target, expected_width, expected_type) };
    }

    if (expression.kind == ExpressionKind::Call
        && expression.text == "@sv-type") {
        report(
            "FSIM-ELAB-SVTYPE-006",
            "the SystemVerilog type operator is only a value inside a type "
            "equality comparison",
            expression.span);
        return std::nullopt;
    }

    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with("@sv-cast:")) {
        if (expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVCAST-001",
                "a SystemVerilog type cast requires one expression",
                expression.span);
            return std::nullopt;
        }
        if (expression.operands.front().kind
                == ExpressionKind::Aggregate
            && expression.operands.front().text == "sv-pattern"
            && systemverilog_standard_
                != frontend::StandardRevision::SystemVerilog2023) {
            report(
                "FSIM-ELAB-SVCAST-005",
                "a static cast does not provide assignment-pattern context "
                "before SystemVerilog-2023",
                expression.span);
            return std::nullopt;
        }
        const auto type_name = std::string_view { expression.text }
                                   .substr(std::string_view { "@sv-cast:" }.size());
        const auto* cast_type = visible_type_mark(type_name);
        frontend::Type builtin;
        if (cast_type == nullptr) {
            builtin.spelling = std::string { type_name };
            if (expression.call_result_width != 0U
                && expression.call_result_domain
                    != frontend::ValueDomain::Unknown) {
                builtin.domain = expression.call_result_domain;
                builtin.is_signed = expression.call_result_signed;
                builtin.packed_range = frontend::PackedRange {
                    static_cast<std::int64_t>(
                        expression.call_result_width - 1U),
                    0,
                    true
                };
            } else if (type_name == "bit") {
                builtin.domain = frontend::ValueDomain::Bit2;
                builtin.is_signed = false;
            } else if (type_name == "logic"
                || type_name == "reg") {
                builtin.domain = frontend::ValueDomain::Logic4;
                builtin.is_signed = false;
            } else if (type_name == "byte"
                || type_name == "shortint"
                || type_name == "int"
                || type_name == "longint") {
                builtin.domain = frontend::ValueDomain::Integer;
                builtin.is_signed = true;
                const auto width = type_name == "byte" ? 8
                    : type_name == "shortint"          ? 16
                    : type_name == "int"               ? 32
                                                       : 64;
                builtin.packed_range = frontend::PackedRange {
                    width - 1, 0, true
                };
            } else if (type_name == "integer") {
                builtin.domain = frontend::ValueDomain::Logic4;
                builtin.is_signed = true;
                builtin.packed_range = frontend::PackedRange {
                    31, 0, true
                };
            } else {
                report(
                    "FSIM-ELAB-SVCAST-002",
                    "SystemVerilog cast type '" + std::string { type_name }
                        + "' is not visible",
                    expression.span);
                return std::nullopt;
            }
            cast_type = &builtin;
        }
        const auto cast_width = cast_type->width();
        if (!cast_width || *cast_width == 0
            || *cast_width > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-SVCAST-003",
                "SystemVerilog cast type '" + std::string { type_name }
                    + "' has no host-addressable executable width",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = cast_type->systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None
            ? *cast_width
            : infer_width(expression.operands.front()).value_or(*cast_width);
        auto source = lower_expression(
            expression.operands.front(), source_width, cast_type);
        if (!source) {
            return std::nullopt;
        }
        if (register_width(*source) != *cast_width) {
            *source = resize_register(
                *source, *cast_width,
                is_signed_expression(expression.operands.front()));
        }
        if (is_two_state_domain(cast_type->domain)
            && !is_two_state_domain(register_domain(*source))) {
            *source = convert_to_two_state(*source);
        }
        if (register_domain(*source) == cast_type->domain) {
            return source;
        }
        const auto destination = allocate_register(
            *cast_width, cast_type->domain);
        process_.operations.emplace_back(
            CopyRegister { destination, *source });
        return destination;
    }
    return {};
}

} // namespace fsim::elaboration
