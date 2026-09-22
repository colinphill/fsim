// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string canonical_vital_name(const std::string_view spelling)
{
    const auto separator = spelling.find_last_of('.');
    auto result = std::string { spelling.substr(
        separator == std::string_view::npos ? 0U : separator + 1U) };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool vital_logic_name(const std::string_view name)
{
    for (const auto prefix : { std::string_view { "vitaland" },
             std::string_view { "vitalor" },
             std::string_view { "vitalxor" },
             std::string_view { "vitalnand" },
             std::string_view { "vitalnor" },
             std::string_view { "vitalxnor" } }) {
        if (name == prefix
            || (name.starts_with(prefix)
                && name.size() == prefix.size() + 1U
                && name.back() >= '2' && name.back() <= '4')) {
            return true;
        }
    }
    return false;
}

bool vital_expression_name(const std::string_view name)
{
    return name == "vitaltimingdatainit"
        || name == "vitalperioddatainit"
        || name == "vitalskewdatainit"
        || name == "vitaldefaultoutputmap"
        || name == "vitaldefaultresultmap"
        || name == "vitaldefaultresultzmap"
        || name == "vitalzerodelay"
        || name == "vitalzerodelay01"
        || name == "vitalzerodelay01z"
        || name == "vitalzerodelay01zx"
        || name == "vitaldefdelay01"
        || name == "vitaldefdelay01z"
        || name == "vitalextendtofilldelay"
        || name == "vitalcalcdelay"
        || name == "vitaltruthtable"
        || name == "vitalmux" || name == "vitalmux2"
        || name == "vitalmux4" || name == "vitalmux8"
        || name == "vitaldecoder" || name == "vitaldecoder2"
        || name == "vitaldecoder4" || name == "vitaldecoder8"
        || name == "vitalbufif0" || name == "vitalbufif1"
        || name == "vitalinvif0" || name == "vitalinvif1"
        || name == "vitalbuf" || name == "vitalinv"
        || name == "vitalident" || vital_logic_name(name);
}

std::optional<std::size_t> fixed_vital_width(
    const std::string_view name)
{
    if (name == "vitaltimingdatainit") {
        return 261U;
    }
    if (name == "vitalperioddatainit") {
        return 130U;
    }
    if (name == "vitalskewdatainit") {
        return 259U;
    }
    if (name == "vitaldefaultoutputmap") {
        return 9U;
    }
    if (name == "vitaldefaultresultmap") {
        return 4U;
    }
    if (name == "vitaldefaultresultzmap") {
        return 5U;
    }
    if (name == "vitalzerodelay") {
        return 64U;
    }
    if (name == "vitalzerodelay01" || name == "vitaldefdelay01") {
        return 128U;
    }
    if (name == "vitalzerodelay01z" || name == "vitaldefdelay01z") {
        return 384U;
    }
    if (name == "vitalzerodelay01zx") {
        return 768U;
    }
    if (name == "vitalcalcdelay") {
        return 64U;
    }
    if (name == "vitaldecoder2") {
        return 2U;
    }
    if (name == "vitaldecoder4") {
        return 4U;
    }
    if (name == "vitaldecoder8") {
        return 8U;
    }
    if (name == "vitalbuf" || name == "vitalinv"
        || name == "vitalident" || vital_logic_name(name)
        || name == "vitalbufif0" || name == "vitalbufif1"
        || name == "vitalinvif0" || name == "vitalinvif1"
        || name == "vitalmux" || name == "vitalmux2"
        || name == "vitalmux4" || name == "vitalmux8") {
        return 1U;
    }
    return std::nullopt;
}

bool compiler_owned_vital_expression(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression_id,
    const std::string_view name,
    const std::span<const semantic::CompiledBindingFrame> binding_frames)
{
    const auto expression = specialization.find_expression(expression_id);
    if (!expression || expression->vhdl == nullptr
        || !expression->vhdl->referenced_name) {
        return false;
    }

    const semantic::CompiledDesignResolver resolver {
        specialization, binding_frames };
    const auto compiler_owned_declaration
        = [&](const semantic::DeclarationId id) {
        const auto declaration = specialization.find_declaration(id);
        if (!declaration || declaration->vhdl == nullptr) {
            return false;
        }
        const auto scope = declaration->vhdl->scope;
        const auto& scopes = specialization.design().semantics.scopes();
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return false;
        }
        const auto unit = specialization.design().find_unit(
            scopes[scope.value()].unit);
        if (!unit || unit->vhdl == nullptr
            || unit->vhdl->kind != semantic::vhdl::UnitKind::package
            || !unit->vhdl->primary_name.empty()
            || canonical_vital_name(unit->vhdl->library) != "ieee"
            || (canonical_vital_name(unit->vhdl->name)
                    != "vital_timing"
                && canonical_vital_name(unit->vhdl->name)
                    != "vital_primitives")) {
            return false;
        }
        return std::ranges::any_of(
            unit->vhdl->standard_package_declarations,
            [&](const std::string& member) {
                return canonical_vital_name(member) == name;
            });
    };
    std::vector<semantic::DeclarationId> resolved;
    if (expression->vhdl->kind
        == semantic::vhdl::ExpressionKind::name) {
        resolved = resolver.resolve_expression_name(
            expression_id).candidates;
    } else {
        resolved = resolver.resolve_vhdl_callable_candidates(
            *expression->vhdl->referenced_name,
            expression->vhdl->scope).candidates;
    }
    if (std::ranges::any_of(
            resolved, [&](const semantic::DeclarationId declaration) {
                return !compiler_owned_declaration(declaration);
            })) {
        return false;
    }
    return resolver.vhdl_standard_package_member_visible(
        *expression->vhdl->referenced_name,
        expression->vhdl->scope);
}

} // namespace

std::optional<VhdlVitalNamedConstant>
resolve_vhdl_vital_named_constant(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression_id,
    const std::size_t contextual_width,
    const std::span<const semantic::CompiledBindingFrame> binding_frames)
{
    const auto expression = specialization.find_expression(expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name
        || !expression->vhdl->referenced_name) {
        return std::nullopt;
    }
    const auto name = canonical_vital_name(expression->vhdl->text);
    std::optional<std::string_view> mapped;
    if (name == "vitaldefaultoutputmap") {
        mapped = "UX01ZWLH-";
    } else if (name == "vitaldefaultresultmap") {
        mapped = "UX01";
    } else if (name == "vitaldefaultresultzmap") {
        mapped = "UX01Z";
    }
    const auto delay = name.starts_with("vitalzero")
        || name.starts_with("vitaldefdelay");
    if (!mapped && !delay) {
        return std::nullopt;
    }
    if (!compiler_owned_vital_expression(
            specialization, expression_id, name, binding_frames)) {
        return std::nullopt;
    }

    VhdlVitalNamedConstant result;
    result.canonical_name = name;
    result.default_width = fixed_vital_width(name).value_or(0U);
    result.domain = mapped
        ? frontend::ValueDomain::Logic9
        : frontend::ValueDomain::Integer;
    result.exact_width = mapped.has_value();
    const auto width = contextual_width != 0U
        ? contextual_width
        : result.default_width;
    if (mapped) {
        if (width == mapped->size()) {
            result.value
                = PackedLogic4::from_logic9_msb_string(*mapped);
        }
    } else if (width != 0U) {
        result.value = PackedLogic4 { width, Logic4::zero };
    }
    return result;
}

std::optional<Lowerer::HirVhdlIntrinsicProfile>
Lowerer::hir_vhdl_vital_expression_profile(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::name
            && expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call)
        || !expression->vhdl->referenced_name) {
        return std::nullopt;
    }
    const auto name = canonical_vital_name(expression->vhdl->text);
    if (!vital_expression_name(name)) {
        return std::nullopt;
    }

    // Compiler-owned VITAL packages retain their declaration surface so HIR
    // resolution preserves selected profiles. User declarations must continue
    // to shadow every intrinsic entry point and named constant.
    if (!compiler_owned_vital_expression(*specialized_hir_unit_,
            expression_id, name, hir_generic_binding_frames_)) {
        return std::nullopt;
    }

    const auto width = fixed_vital_width(name).value_or(0U);
    auto domain = frontend::ValueDomain::Logic9;
    if (name.starts_with("vitalzero")
        || name.starts_with("vitaldefdelay")
        || name == "vitalextendtofilldelay"
        || name == "vitalcalcdelay") {
        domain = frontend::ValueDomain::Integer;
    }
    return HirVhdlIntrinsicProfile { width, domain, false };
}

Lowerer::HirVhdlIntrinsicAttempt
Lowerer::lower_hir_vhdl_vital_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto profile = hir_vhdl_vital_expression_profile(expression_id);
    if (!profile || specialized_hir_unit_ == nullptr) {
        return { false, std::nullopt };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return { true, std::nullopt };
    }
    const auto& source = *expression->vhdl;
    const auto name = canonical_vital_name(source.text);
    const auto span = hir_source_span(source.source);
    const auto width = expected_width != 0U
        ? expected_width
        : profile->width;

    const auto load = [&](const PackedLogic4& value,
                          const frontend::ValueDomain domain) {
        const auto result = allocate_register(value.width(), domain);
        process_.operations.emplace_back(LoadConstant { result, value });
        return result;
    };
    const auto state = [&](const runtime::Logic9 value) {
        PackedLogic4 packed(1U);
        packed.fill(value);
        return load(packed, frontend::ValueDomain::Logic9);
    };
    const auto state_matches = [&](const RegisterId value,
                                   const runtime::Logic9 expected) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, result, value, state(expected) });
        return result;
    };
    const auto either = [&](const RegisterId left,
                            const RegisterId right) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::bit_or, result, left, right });
        return result;
    };
    const auto select = [&](const RegisterId condition,
                            const RegisterId when_true,
                            const RegisterId when_false,
                            const std::size_t selected_width,
                            const frontend::ValueDomain domain) {
        const auto result = allocate_register(selected_width, domain);
        process_.operations.emplace_back(ConditionalSelect {
            result, condition, when_true, when_false });
        return result;
    };
    const auto normalize_ux01 = [&](const RegisterId value) {
        auto normalized = state(runtime::Logic9::x);
        for (const auto& [input, mapped] : {
                 std::pair { runtime::Logic9::u, runtime::Logic9::u },
                 std::pair { runtime::Logic9::zero,
                     runtime::Logic9::zero },
                 std::pair { runtime::Logic9::l,
                     runtime::Logic9::zero },
                 std::pair { runtime::Logic9::one,
                     runtime::Logic9::one },
                 std::pair { runtime::Logic9::h,
                     runtime::Logic9::one } }) {
            normalized = select(state_matches(value, input), state(mapped),
                normalized, 1U, frontend::ValueDomain::Logic9);
        }
        return normalized;
    };
    const auto operand_span = [&](const semantic::ExpressionId id) {
        const auto value = specialized_hir_unit_->find_expression(id);
        return value && value->vhdl != nullptr
            ? hir_source_span(value->vhdl->source)
            : span;
    };
    // VHDL composite generic occurrences retain the formal name in executable
    // HIR. VITAL owns the contextual shape of its delay arrays, so inspect the
    // specialized actual before deciding whether an aggregate can be packed.
    const auto specialized_actual = [&](semantic::ExpressionId id) {
        std::unordered_set<std::uint32_t> visited;
        while (visited.insert(id.value()).second) {
            const auto actual = hir_generic_actual(id);
            if (!actual || !actual->valid()) {
                break;
            }
            id = *actual;
        }
        return id;
    };
    const auto delay_aggregate = [&](const semantic::ExpressionId id)
        -> std::optional<semantic::CompiledExpressionView> {
        const auto actual = specialized_actual(id);
        const auto aggregate_expression
            = specialized_hir_unit_->find_expression(actual);
        if (aggregate_expression
            && aggregate_expression->vhdl != nullptr
            && aggregate_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            return aggregate_expression;
        }
        return std::nullopt;
    };
    const auto lower_delay = [&](const semantic::ExpressionId id,
                                 const std::size_t delay_width)
        -> std::optional<RegisterId> {
        const auto aggregate = delay_aggregate(id);
        if (!aggregate || aggregate->vhdl->associations.empty()
            || aggregate->vhdl->associations.size() * 64U
                != delay_width
            || !std::ranges::all_of(
                aggregate->vhdl->associations,
                [](const semantic::vhdl::AggregateAssociation& association) {
                    return association.choices.empty()
                        && association.choice_spelling.empty();
                })) {
            return lower_hir_expression(id, delay_width);
        }
        std::vector<RegisterId> elements;
        elements.reserve(aggregate->vhdl->associations.size());
        for (const auto& association : aggregate->vhdl->associations) {
            const auto element_id = association.value;
            const auto element
                = specialized_hir_unit_->find_expression(element_id);
            if (element && element->vhdl != nullptr
                && element->vhdl->kind
                    == semantic::vhdl::ExpressionKind::unary
                && element->vhdl->text == "-") {
                report("FSIM-ELAB-VITAL-006",
                    "VITAL delay elements must be nonnegative TIME values",
                    operand_span(element_id));
                return std::nullopt;
            }
            const auto value = lower_hir_expression(element_id, 64U);
            if (!value) {
                return std::nullopt;
            }
            elements.push_back(*value);
        }
        const auto result = allocate_register(
            delay_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(Concatenate { result,
            std::move(elements),
            static_cast<std::uint32_t>(delay_width) });
        return result;
    };
    const auto promote_logic = [&](std::optional<RegisterId> value,
                                   const std::size_t value_width)
        -> std::optional<RegisterId> {
        if (!value) {
            return std::nullopt;
        }
        if (register_domain(*value) == frontend::ValueDomain::Logic9) {
            return value;
        }
        if (register_domain(*value) != frontend::ValueDomain::Bit2
            && register_domain(*value) != frontend::ValueDomain::Logic4) {
            return std::nullopt;
        }
        const auto promoted = allocate_register(
            value_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister { promoted, *value });
        return promoted;
    };
    // Compiler-owned VITAL profiles supply a std_ulogic context that is not
    // represented by an ordinary callable declaration in HIR. Apply that
    // context before generic enumeration-literal resolution: otherwise a
    // character literal such as 'W' can resolve to STANDARD.CHARACTER and its
    // ordinal is truncated to one bit.
    const auto lower_scalar_logic = [&](const semantic::ExpressionId id)
        -> std::optional<RegisterId> {
        const auto actual = specialized_actual(id);
        const auto operand
            = specialized_hir_unit_->find_expression(actual);
        if (operand && operand->vhdl != nullptr
            && operand->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && operand->vhdl->text.size() == 3U
            && operand->vhdl->text.front() == '\''
            && operand->vhdl->text.back() == '\'') {
            if (const auto parsed
                = runtime::parse_logic9(operand->vhdl->text[1])) {
                return state(*parsed);
            }
        }
        return promote_logic(lower_hir_expression(id, 1U), 1U);
    };
    const auto result_map = [&](const RegisterId raw,
                                const std::optional<semantic::ExpressionId> map,
                                const std::size_t map_width)
        -> std::optional<RegisterId> {
        if (!map) {
            return raw;
        }
        auto packed = promote_logic(
            lower_hir_expression(*map, map_width), map_width);
        if (!packed) {
            report("FSIM-ELAB-VITAL-005",
                name + " requires its exact VITAL result-map type",
                operand_span(*map));
            return std::nullopt;
        }
        auto mapped = state(runtime::Logic9::u);
        for (std::size_t ordinal { }; ordinal < map_width; ++ordinal) {
            const auto element = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Extract { element, *packed,
                static_cast<std::uint32_t>(map_width - 1U - ordinal), 1U });
            mapped = select(state_matches(raw,
                                static_cast<runtime::Logic9>(ordinal)),
                element, mapped, 1U, frontend::ValueDomain::Logic9);
        }
        return mapped;
    };

    if (source.kind == semantic::vhdl::ExpressionKind::name) {
        const auto named_constant = resolve_vhdl_vital_named_constant(
            *specialized_hir_unit_, expression_id, width,
            hir_generic_binding_frames_);
        if (named_constant) {
            if (!named_constant->value) {
                report("FSIM-ELAB-VITAL-001",
                    named_constant->canonical_name
                        + (named_constant->exact_width
                                ? " requires its exact VITAL map type"
                                : " has no concrete contextual delay type"),
                    span);
                return { true, std::nullopt };
            }
            return { true, load(
                               *named_constant->value,
                               named_constant->domain) };
        }
    }

    if (name == "vitaltimingdatainit"
        || name == "vitalperioddatainit"
        || name == "vitalskewdatainit") {
        const auto required = fixed_vital_width(name).value_or(0U);
        if (width != required || !source.operands.empty()) {
            report("FSIM-ELAB-VITAL-001",
                name + " requires its exact parameterless record type",
                span);
            return { true, std::nullopt };
        }
        PackedLogic4 value(width);
        value.fill(runtime::Logic9::zero);
        if (name == "vitaltimingdatainit") {
            value.set_logic9(259U, runtime::Logic9::x);
            value.set_logic9(193U, runtime::Logic9::x);
        } else if (name == "vitalperioddatainit") {
            value.set_logic9(129U, runtime::Logic9::x);
        }
        return { true, load(value, frontend::ValueDomain::Logic9) };
    }

    if (name == "vitalextendtofilldelay") {
        if (source.operands.size() != 1U
            || (!source.argument_names.empty()
                && !source.argument_names.front().empty()
                && source.argument_names.front() != "delay")) {
            report("FSIM-ELAB-VITAL-003",
                "vitalextendtofilldelay requires one Delay argument",
                span);
            return { true, std::nullopt };
        }
        const auto source_expression
            = delay_aggregate(source.operands.front());
        auto source_width = hir_expression_width(
            source.operands.front(), hir_process_scope_);
        if (source_expression
            && !source_expression->vhdl->associations.empty()
            && std::ranges::all_of(
                source_expression->vhdl->associations,
                [](const semantic::vhdl::AggregateAssociation& association) {
                    return association.choices.empty()
                        && association.choice_spelling.empty();
                })) {
            source_width
                = source_expression->vhdl->associations.size() * 64U;
        }
        const auto delay_width = [](const std::size_t value) {
            return value == 64U || value == 128U || value == 384U
                || value == 768U;
        };
        if (!source_width || !delay_width(*source_width)
            || !delay_width(width)) {
            report("FSIM-ELAB-VITAL-006",
                "vitalextendtofilldelay requires concrete scalar, 01, 01Z, or 01ZX delay types",
                span);
            return { true, std::nullopt };
        }
        const auto packed_source = lower_delay(
            source.operands.front(), *source_width);
        if (!packed_source) {
            return { true, std::nullopt };
        }
        if (*source_width == width) {
            return { true, packed_source };
        }
        const auto source_count = *source_width / 64U;
        const auto target_count = width / 64U;
        constexpr std::array<std::size_t, 12U> transition_group {
            0U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 1U
        };
        std::vector<RegisterId> values;
        values.reserve(target_count);
        for (std::size_t target { }; target < target_count; ++target) {
            std::size_t source_index { };
            if (source_count == 2U) {
                source_index = transition_group[target];
            } else if (source_count == 6U) {
                source_index = target < 6U
                    ? target : transition_group[target];
            } else if (source_count == 12U) {
                source_index = target;
            }
            const auto part = allocate_register(
                64U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(Extract { part,
                *packed_source,
                static_cast<std::uint32_t>(
                    (source_count - 1U - source_index) * 64U),
                64U });
            values.push_back(part);
        }
        const auto result = allocate_register(
            width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(Concatenate { result,
            std::move(values), static_cast<std::uint32_t>(width) });
        return { true, result };
    }

    if (name == "vitalcalcdelay") {
        std::array<std::optional<semantic::ExpressionId>, 3U> arguments;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            std::optional<std::size_t> target;
            if (argument_name == "newval") {
                target = 0U;
            } else if (argument_name == "oldval") {
                target = 1U;
            } else if (argument_name == "delay") {
                target = 2U;
            } else if (argument_name.empty()
                && positional < arguments.size()) {
                target = positional++;
            }
            if (!target || arguments[*target]) {
                report("FSIM-ELAB-VITAL-003",
                    "vitalcalcdelay has an unknown, duplicate, or misplaced argument",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            arguments[*target] = source.operands[index];
        }
        if (std::ranges::any_of(arguments,
                [](const auto& value) { return !value; })
            || width != 64U) {
            report("FSIM-ELAB-VITAL-003",
                "vitalcalcdelay requires NewVal, OldVal, and Delay and returns TIME",
                span);
            return { true, std::nullopt };
        }
        const auto new_value = lower_scalar_logic(*arguments[0]);
        const auto old_value = lower_scalar_logic(*arguments[1]);
        auto delay_width = hir_expression_width(
            *arguments[2], hir_process_scope_);
        if ((!delay_width || *delay_width == 0U)
            && specialized_hir_unit_->find_expression(*arguments[2])) {
            delay_width = 384U;
        }
        if (!new_value || !old_value || !delay_width
            || (*delay_width != 64U && *delay_width != 128U
                && *delay_width != 384U)) {
            report("FSIM-ELAB-VITAL-006",
                "vitalcalcdelay requires std_ulogic values and a scalar, 01, or 01Z delay",
                span);
            return { true, std::nullopt };
        }
        const auto delay = lower_delay(*arguments[2], *delay_width);
        if (!delay) {
            return { true, std::nullopt };
        }
        const auto count = *delay_width / 64U;
        const auto delay_at = [&](const std::size_t transition) {
            const auto result = allocate_register(
                64U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(Extract { result, *delay,
                static_cast<std::uint32_t>(
                    (count - 1U - transition) * 64U),
                64U });
            return result;
        };
        if (count == 1U) {
            return { true, delay_at(0U) };
        }
        const auto minimum = [&](const RegisterId left,
                                 const RegisterId right) {
            const auto less = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::less_unsigned, less, left, right });
            return select(less, left, right, 64U,
                frontend::ValueDomain::Integer);
        };
        const auto maximum = [&](const RegisterId left,
                                 const RegisterId right) {
            const auto less = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::less_unsigned, less, left, right });
            return select(less, right, left, 64U,
                frontend::ValueDomain::Integer);
        };
        const auto new_zero = either(state_matches(*new_value,
                                         runtime::Logic9::zero),
            state_matches(*new_value, runtime::Logic9::l));
        const auto new_one = either(state_matches(*new_value,
                                        runtime::Logic9::one),
            state_matches(*new_value, runtime::Logic9::h));
        const auto new_z = state_matches(*new_value, runtime::Logic9::z);
        const auto old_zero = either(state_matches(*old_value,
                                         runtime::Logic9::zero),
            state_matches(*old_value, runtime::Logic9::l));
        const auto old_one = either(state_matches(*old_value,
                                        runtime::Logic9::one),
            state_matches(*old_value, runtime::Logic9::h));
        const auto old_z = state_matches(*old_value, runtime::Logic9::z);
        const auto rise = delay_at(0U);
        const auto fall = delay_at(1U);
        if (count == 2U) {
            const auto old_other = maximum(fall, rise);
            const auto unknown_target = select(old_zero, rise,
                select(old_one, fall,
                    select(old_z, minimum(fall, rise), old_other, 64U,
                        frontend::ValueDomain::Integer),
                    64U, frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer);
            const auto z_target = select(old_zero, rise,
                select(old_one, fall, old_other, 64U,
                    frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer);
            return { true, select(new_zero, fall,
                               select(new_one, rise,
                                   select(new_z, z_target, unknown_target,
                                       64U,
                                       frontend::ValueDomain::Integer),
                                   64U, frontend::ValueDomain::Integer),
                               64U, frontend::ValueDomain::Integer) };
        }
        const auto tr0z = delay_at(2U);
        const auto trz1 = delay_at(3U);
        const auto tr1z = delay_at(4U);
        const auto trz0 = delay_at(5U);
        const auto from_unknown = select(new_zero, maximum(fall, trz0),
            select(new_one, maximum(rise, trz1),
                select(new_z, maximum(tr1z, tr0z), maximum(fall, rise),
                    64U, frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer),
            64U, frontend::ValueDomain::Integer);
        const auto from_z = select(new_zero, trz0,
            select(new_one, trz1,
                select(new_z, maximum(tr0z, tr1z), minimum(trz1, trz0),
                    64U, frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer),
            64U, frontend::ValueDomain::Integer);
        const auto from_one = select(new_zero, fall,
            select(new_one, rise,
                select(new_z, tr1z, minimum(fall, tr1z), 64U,
                    frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer),
            64U, frontend::ValueDomain::Integer);
        const auto from_zero = select(new_zero, fall,
            select(new_one, rise,
                select(new_z, tr0z, minimum(rise, tr0z), 64U,
                    frontend::ValueDomain::Integer),
                64U, frontend::ValueDomain::Integer),
            64U, frontend::ValueDomain::Integer);
        return { true, select(old_zero, from_zero,
                           select(old_one, from_one,
                               select(old_z, from_z, from_unknown, 64U,
                                   frontend::ValueDomain::Integer),
                               64U, frontend::ValueDomain::Integer),
                           64U, frontend::ValueDomain::Integer) };
    }

    const bool unary = name == "vitalbuf" || name == "vitalinv"
        || name == "vitalident";
    if (unary || vital_logic_name(name)) {
        if (width != 1U) {
            report("FSIM-ELAB-VITAL-002",
                name + " returns one std_ulogic value", span);
            return { true, std::nullopt };
        }
        std::size_t input_count = unary ? 1U : 0U;
        bool reduction { };
        if (!unary) {
            const auto suffix = name.back();
            if (suffix >= '2' && suffix <= '4') {
                input_count = static_cast<std::size_t>(suffix - '0');
            } else {
                input_count = 1U;
                reduction = true;
            }
        }
        std::vector<std::optional<semantic::ExpressionId>> inputs(
            input_count);
        std::optional<semantic::ExpressionId> map;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            if (argument_name == "resultmap") {
                if (map) {
                    report("FSIM-ELAB-VITAL-003",
                        name + " has duplicate ResultMap associations",
                        operand_span(source.operands[index]));
                    return { true, std::nullopt };
                }
                map = source.operands[index];
                continue;
            }
            std::optional<std::size_t> target;
            if (argument_name.empty()) {
                while (positional < inputs.size() && inputs[positional]) {
                    ++positional;
                }
                if (positional < inputs.size()) {
                    target = positional++;
                } else if (!map) {
                    map = source.operands[index];
                    continue;
                }
            } else if ((unary || reduction) && argument_name == "data") {
                target = 0U;
            } else if (!unary && !reduction
                && argument_name.size() == 1U
                && argument_name.front() >= 'a'
                && argument_name.front()
                    < static_cast<char>('a' + input_count)) {
                target = static_cast<std::size_t>(
                    argument_name.front() - 'a');
            }
            if (!target || inputs[*target]) {
                report("FSIM-ELAB-VITAL-003",
                    name + " has an unknown, duplicate, or misplaced association",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            inputs[*target] = source.operands[index];
        }
        if (std::ranges::any_of(inputs,
                [](const auto& input) { return !input; })) {
            report("FSIM-ELAB-VITAL-003",
                name + " is missing a required input", span);
            return { true, std::nullopt };
        }

        RegisterId raw;
        if (reduction) {
            auto input_width = hir_expression_width(
                *inputs.front(), hir_process_scope_);
            const auto input_expression
                = specialized_hir_unit_->find_expression(*inputs.front());
            if (!input_width && input_expression
                && input_expression->vhdl != nullptr
                && input_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal
                && input_expression->vhdl->decoded_string
                && input_expression->vhdl->decoded_string->empty()) {
                input_width = 0U;
            }
            if (!input_width) {
                report("FSIM-ELAB-VITAL-004",
                    name + " requires a concrete std_logic_vector input",
                    operand_span(*inputs.front()));
                return { true, std::nullopt };
            }
            if (*input_width == 0U) {
                raw = state(name.find("and") != std::string::npos
                        ? runtime::Logic9::one
                        : runtime::Logic9::zero);
            } else {
                const auto input = promote_logic(lower_hir_expression(
                    *inputs.front(), *input_width), *input_width);
                if (!input) {
                    report("FSIM-ELAB-VITAL-004",
                        name + " requires a standard-logic vector",
                        operand_span(*inputs.front()));
                    return { true, std::nullopt };
                }
                raw = allocate_register(
                    1U, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(Reduction {
                    name.starts_with("vitaland")
                            || name.starts_with("vitalnand")
                        ? ReductionOperator::bit_and
                        : name.starts_with("vitalor")
                                || name.starts_with("vitalnor")
                        ? ReductionOperator::bit_or
                        : ReductionOperator::bit_xor,
                    raw, *input });
            }
        } else {
            std::vector<RegisterId> values;
            values.reserve(inputs.size());
            for (const auto input_id : inputs) {
                const auto input = lower_scalar_logic(*input_id);
                if (!input) {
                    report("FSIM-ELAB-VITAL-004",
                        name + " requires std_ulogic inputs",
                        operand_span(*input_id));
                    return { true, std::nullopt };
                }
                values.push_back(*input);
            }
            raw = values.front();
            const auto operation = name.starts_with("vitaland")
                    || name.starts_with("vitalnand")
                ? BinaryOperator::bit_and
                : name.starts_with("vitalor")
                        || name.starts_with("vitalnor")
                ? BinaryOperator::bit_or
                : BinaryOperator::bit_xor;
            for (std::size_t index { 1U }; index < values.size(); ++index) {
                const auto combined = allocate_register(
                    1U, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(Binary {
                    operation, combined, raw, values[index] });
                raw = combined;
            }
        }
        if (name == "vitalinv" || name.starts_with("vitalnand")
            || name.starts_with("vitalnor")
            || name.starts_with("vitalxnor")) {
            const auto inverted = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(UnaryNot { inverted, raw });
            raw = inverted;
        }
        if (name != "vitalident") {
            raw = normalize_ux01(raw);
        }
        return { true, result_map(raw, map,
                           name == "vitalident" ? 9U : 4U) };
    }

    if (name == "vitaltruthtable") {
        std::optional<semantic::ExpressionId> table_expression;
        std::optional<semantic::ExpressionId> data_expression;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            auto* target = argument_name == "truthtable"
                    || (argument_name.empty() && positional == 0U)
                ? &table_expression
                : argument_name == "datain"
                        || (argument_name.empty() && positional == 1U)
                ? &data_expression
                : nullptr;
            if (target == nullptr || *target) {
                report("FSIM-ELAB-VITAL-003",
                    "vitaltruthtable has an unknown, duplicate, or misplaced argument",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            *target = source.operands[index];
            positional += argument_name.empty() ? 1U : 0U;
        }
        const auto data_width = data_expression
            ? hir_expression_width(*data_expression, hir_process_scope_)
            : std::nullopt;
        const auto table = table_expression
            ? specialized_hir_unit_->find_expression(*table_expression)
            : std::nullopt;
        if (!table || table->vhdl == nullptr || !data_expression
            || !data_width || *data_width == 0U || width == 0U
            || table->vhdl->kind
                != semantic::vhdl::ExpressionKind::aggregate
            || table->vhdl->operands.empty()) {
            report("FSIM-ELAB-VITAL-008",
                "vitaltruthtable requires a nonempty static two-dimensional "
                "table, non-null DataIn, and a concrete result width",
                span);
            return { true, std::nullopt };
        }
        const auto data = promote_logic(lower_hir_expression(
            *data_expression, *data_width), *data_width);
        if (!data) {
            report("FSIM-ELAB-VITAL-004",
                "vitaltruthtable DataIn must be a standard-logic vector",
                operand_span(*data_expression));
            return { true, std::nullopt };
        }
        const auto symbol = [&](const semantic::ExpressionId id)
            -> std::optional<char> {
            const auto value = specialized_hir_unit_->find_expression(id);
            if (!value || value->vhdl == nullptr) {
                return std::nullopt;
            }
            auto text = std::string_view { value->vhdl->text };
            constexpr auto enum_prefix
                = std::string_view { "@fsim-enum:" };
            if (text.starts_with(enum_prefix)) {
                text.remove_prefix(enum_prefix.size());
            }
            if (text.size() >= 3U && text.front() == '\''
                && text.back() == '\'') {
                text.remove_prefix(1U);
                text.remove_suffix(1U);
            }
            return text.size() == 1U
                ? std::optional { static_cast<char>(std::toupper(
                      static_cast<unsigned char>(text.front()))) }
                : std::nullopt;
        };
        const auto boolean = [&](const bool value) {
            const auto result = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant { result,
                unsigned_value(value ? 1U : 0U, 1U) });
            return result;
        };
        std::vector<RegisterId> outputs(
            width, state(runtime::Logic9::x));
        for (auto row_id = table->vhdl->operands.rbegin();
             row_id != table->vhdl->operands.rend(); ++row_id) {
            const auto row = specialized_hir_unit_->find_expression(*row_id);
            if (!row || row->vhdl == nullptr
                || row->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate
                || row->vhdl->operands.size() != *data_width + width) {
                report("FSIM-ELAB-VITAL-008",
                    "each vitaltruthtable row must contain exactly "
                    "DataIn'length plus result-width symbols",
                    operand_span(*row_id));
                return { true, std::nullopt };
            }
            auto row_match = boolean(true);
            for (std::size_t column { }; column < *data_width; ++column) {
                const auto table_symbol = symbol(
                    row->vhdl->operands[column]);
                if (!table_symbol || (*table_symbol != 'X'
                        && *table_symbol != '0' && *table_symbol != '1'
                        && *table_symbol != '-' && *table_symbol != 'B')) {
                    report("FSIM-ELAB-VITAL-009",
                        "a vitaltruthtable input symbol must be X, 0, 1, -, or B",
                        operand_span(row->vhdl->operands[column]));
                    return { true, std::nullopt };
                }
                const auto input = allocate_register(
                    1U, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(Extract { input, *data,
                    static_cast<std::uint32_t>(
                        *data_width - 1U - column),
                    1U });
                const auto is_zero = either(state_matches(input,
                                                runtime::Logic9::zero),
                    state_matches(input, runtime::Logic9::l));
                const auto is_one = either(state_matches(input,
                                               runtime::Logic9::one),
                    state_matches(input, runtime::Logic9::h));
                auto matches = boolean(true);
                if (*table_symbol == '0') {
                    matches = is_zero;
                } else if (*table_symbol == '1') {
                    matches = is_one;
                } else if (*table_symbol == 'B') {
                    matches = either(is_zero, is_one);
                } else if (*table_symbol == 'X') {
                    const auto known = either(is_zero, is_one);
                    matches = allocate_register(
                        1U, frontend::ValueDomain::Boolean);
                    process_.operations.emplace_back(
                        UnaryNot { matches, known });
                }
                const auto combined = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::bit_and, combined, row_match,
                    matches });
                row_match = combined;
            }
            for (std::size_t output { }; output < width; ++output) {
                const auto value_id
                    = row->vhdl->operands[*data_width + output];
                const auto table_symbol = symbol(value_id);
                auto value = runtime::Logic9::x;
                if (table_symbol && *table_symbol == '0') {
                    value = runtime::Logic9::zero;
                } else if (table_symbol && *table_symbol == '1') {
                    value = runtime::Logic9::one;
                } else if (table_symbol && *table_symbol == 'Z') {
                    value = runtime::Logic9::z;
                } else if (!table_symbol || (*table_symbol != 'X'
                               && *table_symbol != '-')) {
                    report("FSIM-ELAB-VITAL-009",
                        "a vitaltruthtable output symbol must be X, 0, 1, or Z",
                        operand_span(value_id));
                    return { true, std::nullopt };
                }
                outputs[output] = select(row_match, state(value),
                    outputs[output], 1U, frontend::ValueDomain::Logic9);
            }
        }
        if (width == 1U) {
            return { true, outputs.front() };
        }
        const auto result = allocate_register(
            width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Concatenate { result,
            std::move(outputs), static_cast<std::uint32_t>(width) });
        return { true, result };
    }

    if (name == "vitalmux" || name == "vitalmux2"
        || name == "vitalmux4" || name == "vitalmux8") {
        std::optional<semantic::ExpressionId> data_expression;
        std::optional<semantic::ExpressionId> select_expression;
        std::optional<semantic::ExpressionId> map;
        std::optional<semantic::ExpressionId> data0_expression;
        std::optional<semantic::ExpressionId> data1_expression;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            const auto assign_once = [&](auto& target) {
                if (target) {
                    return false;
                }
                target = source.operands[index];
                return true;
            };
            bool assigned { };
            if (name == "vitalmux2"
                && (argument_name == "data1"
                    || (argument_name.empty() && positional == 0U))) {
                assigned = assign_once(data1_expression);
            } else if (name == "vitalmux2"
                && (argument_name == "data0"
                    || (argument_name.empty() && positional == 1U))) {
                assigned = assign_once(data0_expression);
            } else if (name != "vitalmux2"
                && (argument_name == "data"
                    || (argument_name.empty() && positional == 0U))) {
                assigned = assign_once(data_expression);
            } else if (argument_name == "dselect"
                || (argument_name.empty()
                    && positional == (name == "vitalmux2" ? 2U : 1U))) {
                assigned = assign_once(select_expression);
            } else if (argument_name == "resultmap"
                || argument_name.empty()) {
                assigned = assign_once(map);
            }
            if (!assigned) {
                report("FSIM-ELAB-VITAL-003",
                    name + " has an unknown, duplicate, or misplaced argument",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            positional += argument_name.empty() ? 1U : 0U;
        }
        if (width != 1U || !select_expression
            || (name == "vitalmux2"
                    ? !data0_expression || !data1_expression
                    : !data_expression)) {
            report("FSIM-ELAB-VITAL-003",
                name + " is missing a required data or select input", span);
            return { true, std::nullopt };
        }
        std::size_t data_width { 2U };
        std::size_t select_width { 1U };
        std::optional<RegisterId> data;
        if (name == "vitalmux2") {
            const auto data1 = lower_scalar_logic(*data1_expression);
            const auto data0 = lower_scalar_logic(*data0_expression);
            if (!data1 || !data0) {
                return { true, std::nullopt };
            }
            const auto packed = allocate_register(
                2U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Concatenate {
                packed, { *data1, *data0 }, 2U });
            data = packed;
        } else {
            const auto inferred_data_width = hir_expression_width(
                *data_expression, hir_process_scope_);
            const auto inferred_select_width = hir_expression_width(
                *select_expression, hir_process_scope_);
            if (!inferred_data_width || !inferred_select_width
                || *inferred_data_width == 0U
                || *inferred_select_width == 0U) {
                report("FSIM-ELAB-VITAL-007",
                    name + " requires concrete non-null vectors", span);
                return { true, std::nullopt };
            }
            data_width = *inferred_data_width;
            select_width = *inferred_select_width;
            data = promote_logic(lower_hir_expression(
                *data_expression, data_width), data_width);
        }
        const auto selector = promote_logic(lower_hir_expression(
            *select_expression, select_width), select_width);
        if (!data || !selector) {
            report("FSIM-ELAB-VITAL-004",
                name + " requires standard-logic data and select inputs",
                span);
            return { true, std::nullopt };
        }
        if (select_width >= std::numeric_limits<std::size_t>::digits
            || data_width > (std::size_t { 1U } << select_width)) {
            report("FSIM-ELAB-VITAL-007",
                name + " data width exceeds its select space", span);
            return { true, std::nullopt };
        }
        const auto candidate_count = std::size_t { 1U } << select_width;
        std::vector<RegisterId> candidates;
        candidates.reserve(candidate_count);
        for (std::size_t index { }; index < candidate_count; ++index) {
            if (index >= data_width) {
                candidates.push_back(state(runtime::Logic9::x));
                continue;
            }
            const auto bit = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Extract { bit, *data,
                static_cast<std::uint32_t>(index), 1U });
            candidates.push_back(normalize_ux01(bit));
        }
        for (std::size_t bit { }; bit < select_width; ++bit) {
            const auto selected_bit = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Extract { selected_bit,
                *selector, static_cast<std::uint32_t>(bit), 1U });
            const auto select_zero = either(state_matches(selected_bit,
                                                runtime::Logic9::zero),
                state_matches(selected_bit, runtime::Logic9::l));
            const auto select_one = either(state_matches(selected_bit,
                                               runtime::Logic9::one),
                state_matches(selected_bit, runtime::Logic9::h));
            std::vector<RegisterId> next;
            next.reserve(candidates.size() / 2U);
            for (std::size_t index { }; index < candidates.size();
                 index += 2U) {
                const auto equal = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal, equal,
                    candidates[index], candidates[index + 1U] });
                const auto merged = select(equal, candidates[index],
                    state(runtime::Logic9::x), 1U,
                    frontend::ValueDomain::Logic9);
                next.push_back(select(select_zero, candidates[index],
                    select(select_one, candidates[index + 1U], merged,
                        1U, frontend::ValueDomain::Logic9),
                    1U, frontend::ValueDomain::Logic9));
            }
            candidates = std::move(next);
        }
        return { true, result_map(candidates.front(), map, 4U) };
    }

    if (name == "vitaldecoder" || name == "vitaldecoder2"
        || name == "vitaldecoder4" || name == "vitaldecoder8") {
        std::array<std::optional<semantic::ExpressionId>, 2U> arguments;
        std::optional<semantic::ExpressionId> map;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            std::optional<std::size_t> target;
            if (argument_name == "data") {
                target = 0U;
            } else if (argument_name == "enable") {
                target = 1U;
            } else if (argument_name == "resultmap"
                || (argument_name.empty()
                    && positional >= arguments.size())) {
                if (!map) {
                    map = source.operands[index];
                    continue;
                }
            } else if (argument_name.empty()) {
                target = positional++;
            }
            if (!target || arguments[*target]) {
                report("FSIM-ELAB-VITAL-003",
                    name + " has an unknown, duplicate, or misplaced argument",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            arguments[*target] = source.operands[index];
        }
        if (!arguments[0] || !arguments[1] || width < 2U
            || (width & (width - 1U)) != 0U) {
            report("FSIM-ELAB-VITAL-007",
                name + " requires Data, Enable, and a power-of-two result width",
                span);
            return { true, std::nullopt };
        }
        const auto data_width
            = static_cast<std::size_t>(std::bit_width(width)) - 1U;
        const auto inferred_width = hir_expression_width(
            *arguments[0], hir_process_scope_);
        if (!inferred_width || *inferred_width != data_width) {
            report("FSIM-ELAB-VITAL-007",
                name + " data width does not match its result width",
                operand_span(*arguments[0]));
            return { true, std::nullopt };
        }
        const auto data = promote_logic(lower_hir_expression(
            *arguments[0], data_width), data_width);
        const auto enable = lower_scalar_logic(*arguments[1]);
        if (!data || !enable) {
            report("FSIM-ELAB-VITAL-004",
                name + " requires standard-logic inputs", span);
            return { true, std::nullopt };
        }
        std::vector<RegisterId> outputs(width);
        for (std::size_t output { }; output < width; ++output) {
            auto value = normalize_ux01(*enable);
            for (std::size_t bit { }; bit < data_width; ++bit) {
                const auto source_bit = allocate_register(
                    1U, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(Extract { source_bit,
                    *data, static_cast<std::uint32_t>(bit), 1U });
                auto selected_bit = normalize_ux01(source_bit);
                if (((output >> bit) & 1U) == 0U) {
                    const auto inverted = allocate_register(
                        1U, frontend::ValueDomain::Logic9);
                    process_.operations.emplace_back(
                        UnaryNot { inverted, selected_bit });
                    selected_bit = inverted;
                }
                const auto combined = allocate_register(
                    1U, frontend::ValueDomain::Logic9);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::bit_and, combined, value,
                    selected_bit });
                value = combined;
            }
            const auto mapped = result_map(value, map, 4U);
            if (!mapped) {
                return { true, std::nullopt };
            }
            outputs[output] = *mapped;
        }
        std::ranges::reverse(outputs);
        const auto result = allocate_register(
            width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Concatenate { result,
            std::move(outputs), static_cast<std::uint32_t>(width) });
        return { true, result };
    }

    if (name == "vitalbufif0" || name == "vitalbufif1"
        || name == "vitalinvif0" || name == "vitalinvif1") {
        std::array<std::optional<semantic::ExpressionId>, 2U> arguments;
        std::optional<semantic::ExpressionId> map;
        std::size_t positional { };
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            const auto argument_name = source.argument_names.empty()
                ? std::string_view { }
                : std::string_view { source.argument_names[index] };
            std::optional<std::size_t> target;
            if (argument_name == "data") {
                target = 0U;
            } else if (argument_name == "enable") {
                target = 1U;
            } else if (argument_name == "resultmap" || (argument_name.empty()
                    && positional >= arguments.size())) {
                if (!map) {
                    map = source.operands[index];
                    continue;
                }
            } else if (argument_name.empty()) {
                target = positional++;
            }
            if (!target || arguments[*target]) {
                report("FSIM-ELAB-VITAL-003",
                    name + " has an unknown, duplicate, or misplaced argument",
                    operand_span(source.operands[index]));
                return { true, std::nullopt };
            }
            arguments[*target] = source.operands[index];
        }
        if (width != 1U || !arguments[0] || !arguments[1]) {
            report("FSIM-ELAB-VITAL-003",
                name + " requires Data and Enable std_ulogic inputs", span);
            return { true, std::nullopt };
        }
        auto data = lower_scalar_logic(*arguments[0]);
        const auto enable = lower_scalar_logic(*arguments[1]);
        if (!data || !enable) {
            report("FSIM-ELAB-VITAL-004",
                name + " requires std_ulogic inputs", span);
            return { true, std::nullopt };
        }
        data = normalize_ux01(*data);
        if (name.starts_with("vitalinv")) {
            const auto inverted = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(UnaryNot { inverted, *data });
            data = inverted;
        }
        const auto low = either(state_matches(*enable,
                                    runtime::Logic9::zero),
            state_matches(*enable, runtime::Logic9::l));
        const auto high = either(state_matches(*enable,
                                     runtime::Logic9::one),
            state_matches(*enable, runtime::Logic9::h));
        const bool active_high = name.ends_with('1');
        const auto raw = select(active_high ? high : low, *data,
            select(active_high ? low : high, state(runtime::Logic9::z),
                state(runtime::Logic9::x), 1U,
                frontend::ValueDomain::Logic9),
            1U, frontend::ValueDomain::Logic9);
        return { true, result_map(raw, map, 5U) };
    }

    return { false, std::nullopt };
}

} // namespace fsim::elaboration
