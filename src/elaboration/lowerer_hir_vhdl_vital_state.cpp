// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <cctype>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view vital_state_simple_name(const std::string_view name)
{
    const auto separator = name.find_last_of('.');
    return name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

std::string_view vital_state_name(const semantic::vhdl::Name& name)
{
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

bool same_vital_state_identifier(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs, const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs))
                   == std::tolower(static_cast<unsigned char>(rhs));
           });
}

std::string canonical_vital_state_identifier(const std::string_view name)
{
    auto result = std::string { name };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool has_vital_primitives_qualification(const std::string_view name)
{
    const auto member_separator = name.find_last_of('.');
    if (member_separator == std::string_view::npos) {
        return true;
    }
    const auto owner = name.substr(0U, member_separator);
    const auto package_separator = owner.find_last_of('.');
    if (package_separator == std::string_view::npos) {
        return same_vital_state_identifier(owner, "vital_primitives");
    }
    return same_vital_state_identifier(
               owner.substr(package_separator + 1U), "vital_primitives")
        && same_vital_state_identifier(
            owner.substr(0U, package_separator), "ieee");
}

std::optional<char> vital_state_symbol(
    const semantic::vhdl::Expression& expression)
{
    auto text = expression.decoded_string
        ? std::string_view { *expression.decoded_string }
        : std::string_view { expression.text };
    constexpr std::string_view enum_prefix { "@fsim-enum:" };
    if (text.starts_with(enum_prefix)) {
        text.remove_prefix(enum_prefix.size());
    }
    if (text.size() >= 3U && text.front() == '\'' && text.back() == '\'') {
        text.remove_prefix(1U);
        text.remove_suffix(1U);
    }
    if (text.size() != 1U) {
        return std::nullopt;
    }
    return text.front();
}

} // namespace

bool Lowerer::is_hir_vhdl_vital_state_table_call(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return false;
    }
    const auto& procedure = statement->vhdl->procedure;
    const auto qualified_name = vital_state_name(procedure);
    if (!same_vital_state_identifier(
            vital_state_simple_name(qualified_name), "vitalstatetable")
        || !has_vital_primitives_qualification(qualified_name)) {
        return false;
    }
    if (procedure.selected || !procedure.overloads.empty()) {
        return false;
    }
    semantic::CompiledDesignResolver resolver { *specialized_hir_unit_ };
    return resolver.resolve_vhdl_callable_candidates(
                       procedure, statement->vhdl->scope)
               .candidates.empty()
        && resolver.vhdl_standard_package_member_visible(
            procedure, statement->vhdl->scope);
}

bool Lowerer::lower_hir_vhdl_vital_state_table_call(
    const semantic::StatementId statement_id)
{
    if (!is_hir_vhdl_vital_state_table_call(statement_id)) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto statement_span = hir_source_span(source.source);

    std::vector<semantic::ExpressionId> positional;
    std::map<std::string, semantic::ExpressionId, std::less<>> named;
    bool saw_named { };
    bool valid = true;
    for (const auto& association : source.procedure_arguments) {
        if (association.formal) {
            saw_named = true;
            const auto formal = canonical_vital_state_identifier(
                vital_state_simple_name(vital_state_name(*association.formal)));
            if (!named.emplace(formal, association.actual).second) {
                report("FSIM-ELAB-VITAL-013",
                    "duplicate VitalStateTable actual for formal '"
                        + formal + "'",
                    hir_source_span(association.source));
                valid = false;
            }
        } else {
            if (saw_named) {
                report("FSIM-ELAB-VITAL-013",
                    "a positional VitalStateTable actual cannot follow a named actual",
                    hir_source_span(association.source));
                valid = false;
            }
            positional.push_back(association.actual);
        }
    }
    const auto expression_span = [&](const semantic::ExpressionId id) {
        const auto expression = specialized_hir_unit_->find_expression(id);
        return expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : statement_span;
    };
    for (const auto& [formal, expression_id] : named) {
        if (formal != "result" && formal != "previousdatain"
            && formal != "statetable" && formal != "datain"
            && formal != "numstates") {
            report("FSIM-ELAB-VITAL-013",
                "VitalStateTable has no formal parameter '" + formal + "'",
                expression_span(expression_id));
            valid = false;
        }
    }
    const auto actual = [&](const std::string_view formal,
                            const std::size_t position)
        -> std::optional<semantic::ExpressionId> {
        const auto selected = named.find(formal);
        if (selected != named.end()) {
            return selected->second;
        }
        return position < positional.size()
            ? std::optional { positional[position] }
            : std::nullopt;
    };
    const auto binding = [&](const semantic::ExpressionId expression_id,
                             const bool require_storage)
        -> std::optional<HirRuntimeBinding> {
        if (const auto direct = hir_direct_signal_binding(expression_id)) {
            return direct;
        }
        const auto declaration = hir_target_declaration(expression_id);
        return declaration
            ? hir_runtime_binding(
                  *declaration, hir_process_scope_, require_storage)
            : std::nullopt;
    };

    const auto result_expression = actual("result", 0U);
    const auto result_binding = result_expression
        ? binding(*result_expression, true)
        : std::nullopt;
    if (!result_binding
        || (!result_binding->local && !result_binding->signal)) {
        report("FSIM-ELAB-VITAL-014",
            "VitalStateTable Result requires a writable variable or signal",
            result_expression ? expression_span(*result_expression)
                              : statement_span);
        return true;
    }
    const bool variable_profile = result_binding->local.has_value();
    if (result_binding->domain != frontend::ValueDomain::Logic9
        || result_binding->width == 0U) {
        report("FSIM-ELAB-VITAL-014",
            "VitalStateTable Result must have a concrete standard-logic width",
            expression_span(*result_expression));
        return true;
    }
    const auto result_width = result_binding->width;
    auto result_register = result_binding->local.value_or(RegisterId { });
    if (!variable_profile) {
        result_register = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(
            ReadSignal { result_register, *result_binding->signal });
    }

    const auto previous_expression = variable_profile
        ? actual("previousdatain", 1U)
        : std::nullopt;
    const auto table_expression = actual(
        "statetable", variable_profile ? 2U : 1U);
    const auto data_expression = actual(
        "datain", variable_profile ? 3U : 2U);
    const auto states_expression = actual(
        "numstates", variable_profile ? 4U : 3U);
    const auto maximum_actuals = variable_profile ? 5U : 4U;
    if (positional.size() > maximum_actuals) {
        report("FSIM-ELAB-VITAL-013",
            "VitalStateTable has too many positional actuals",
            statement_span);
        valid = false;
    }
    if (!table_expression || !data_expression
        || (variable_profile && !previous_expression)) {
        report("FSIM-ELAB-VITAL-013",
            "VitalStateTable is missing a required table, data, or state actual",
            statement_span);
        return true;
    }
    const auto table = specialized_hir_unit_->find_expression(
        *table_expression);
    if (!table || table->vhdl == nullptr
        || table->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
        || table->vhdl->operands.empty()) {
        report("FSIM-ELAB-VITAL-015",
            "VitalStateTable requires a nonempty static two-dimensional table",
            expression_span(*table_expression));
        return true;
    }

    std::size_t data_width { };
    std::optional<SignalId> data_signal;
    std::optional<RegisterId> data_register;
    if (variable_profile) {
        const auto data_source = specialized_hir_unit_->find_expression(
            *data_expression);
        const auto null_string = data_source
            && data_source->vhdl != nullptr
            && data_source->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal
            && data_source->vhdl->decoded_string
            && data_source->vhdl->decoded_string->empty();
        const auto width = null_string
            ? std::optional<std::size_t> { 0U }
            : hir_expression_width(*data_expression, hir_process_scope_);
        if (!width) {
            report("FSIM-ELAB-VITAL-014",
                "VitalStateTable DataIn requires a concrete vector width",
                expression_span(*data_expression));
            return true;
        }
        data_width = *width;
        if (data_width != 0U) {
            data_register = lower_hir_expression(
                *data_expression, data_width);
            if (!data_register
                || register_domain(*data_register)
                    != frontend::ValueDomain::Logic9) {
                report("FSIM-ELAB-VITAL-014",
                    "VitalStateTable DataIn must be a standard-logic vector",
                    expression_span(*data_expression));
                return true;
            }
        }
    } else {
        const auto data_binding = binding(*data_expression, false);
        if (!data_binding || !data_binding->signal) {
            report("FSIM-ELAB-VITAL-014",
                "the signal VitalStateTable profile requires a signal DataIn",
                expression_span(*data_expression));
            return true;
        }
        if (data_binding->domain != frontend::ValueDomain::Logic9) {
            report("FSIM-ELAB-VITAL-014",
                "VitalStateTable DataIn must be a standard-logic vector",
                expression_span(*data_expression));
            return true;
        }
        data_width = data_binding->width;
        data_signal = data_binding->signal;
        if (data_width != 0U) {
            data_register = allocate_register(
                data_width, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(
                ReadSignal { *data_register, *data_signal });
        }
        implicit_signal_dependencies_.push_back(*data_signal);
    }

    std::size_t num_states = result_width == 1U ? 1U : 0U;
    if (states_expression) {
        const auto value = hir_constant_integer(*states_expression);
        if (!value || *value < 0) {
            report("FSIM-ELAB-VITAL-015",
                "VitalStateTable NumStates must be a static natural value",
                expression_span(*states_expression));
            valid = false;
        } else {
            num_states = static_cast<std::size_t>(*value);
        }
    } else if (result_width != 1U) {
        report("FSIM-ELAB-VITAL-013",
            "the vector VitalStateTable profile requires NumStates",
            statement_span);
        valid = false;
    }
    if (num_states > result_width) {
        report("FSIM-ELAB-VITAL-015",
            "VitalStateTable NumStates exceeds the Result width",
            statement_span);
        valid = false;
    }

    std::optional<RegisterId> previous_register;
    auto previous_width = data_width;
    if (variable_profile) {
        const auto previous_binding = binding(*previous_expression, true);
        if (!previous_binding || !previous_binding->local
            || previous_binding->domain != frontend::ValueDomain::Logic9
            || previous_binding->width < data_width) {
            report("FSIM-ELAB-VITAL-014",
                "VitalStateTable PreviousDataIn must be a writable "
                "standard-logic vector at least as wide as DataIn",
                expression_span(*previous_expression));
            return true;
        }
        previous_register = previous_binding->local;
        previous_width = previous_binding->width;
    } else if (data_width != 0U) {
        previous_register = allocate_register(
            data_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(
            SignalLastValue { *previous_register, *data_signal });
    }
    if (!valid) {
        return true;
    }

    const auto bool_constant = [&](const bool value) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(LoadConstant {
            result, unsigned_value(value ? 1U : 0U, 1U) });
        return result;
    };
    const auto logic_constant = [&](const runtime::Logic9 value) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        runtime::PackedLogic4 packed(1U);
        packed.fill(value);
        process_.operations.emplace_back(
            LoadConstant { result, std::move(packed) });
        return result;
    };
    const auto is_state = [&](const RegisterId value,
                              const runtime::Logic9 state) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, result, value,
            logic_constant(state) });
        return result;
    };
    const auto combine = [&](const BinaryOperator operation,
                             const RegisterId left,
                             const RegisterId right) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            Binary { operation, result, left, right });
        return result;
    };
    const auto either = [&](const RegisterId left, const RegisterId right) {
        return combine(BinaryOperator::bit_or, left, right);
    };
    const auto both = [&](const RegisterId left, const RegisterId right) {
        return combine(BinaryOperator::bit_and, left, right);
    };
    const auto inverted = [&](const RegisterId value) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(UnaryNot { result, value });
        return result;
    };
    const auto bit = [&](const RegisterId value, const std::size_t width,
                         const std::size_t left_index) {
        const auto result = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Extract { result, value,
            static_cast<std::uint32_t>(width - 1U - left_index), 1U });
        return result;
    };
    const auto x01_classes = [&](const RegisterId value) {
        const auto zero = either(
            is_state(value, runtime::Logic9::zero),
            is_state(value, runtime::Logic9::l));
        const auto one = either(
            is_state(value, runtime::Logic9::one),
            is_state(value, runtime::Logic9::h));
        return std::array<RegisterId, 3U> {
            inverted(either(zero, one)), zero, one
        };
    };
    const auto symbol_match = [&](const char symbol,
                                  const RegisterId previous,
                                  const RegisterId current,
                                  const bool state_column)
        -> std::optional<RegisterId> {
        const auto before = x01_classes(previous);
        const auto after = x01_classes(current);
        if (symbol == '-') {
            return bool_constant(true);
        }
        if (symbol == 'X') {
            return after[0];
        }
        if (symbol == '0') {
            return after[1];
        }
        if (symbol == '1') {
            return after[2];
        }
        if (symbol == 'B') {
            return either(after[1], after[2]);
        }
        if (state_column) {
            return std::nullopt;
        }
        if (symbol == 'S') {
            return either(both(before[1], after[1]),
                both(before[2], after[2]));
        }
        static constexpr std::array<char, 16U> edges {
            '/', '\\', 'P', 'N', 'r', 'f', 'p', 'n',
            'R', 'F', '^', 'v', 'E', 'A', 'D', '*'
        };
        const auto found = std::ranges::find(edges, symbol);
        if (found == edges.end()) {
            return std::nullopt;
        }
        const auto ordinal = static_cast<unsigned>(
            std::distance(edges.begin(), found));
        constexpr std::array<std::array<std::uint16_t, 3U>, 3U> masks { {
            { { 0U, 0xDA08U, 0xB504U } },
            { { 0xA150U, 0U, 0x8145U } },
            { { 0xC2A0U, 0x828AU, 0U } },
        } };
        auto match = bool_constant(false);
        for (std::size_t old_class { }; old_class < 3U; ++old_class) {
            for (std::size_t new_class { }; new_class < 3U; ++new_class) {
                if ((masks[old_class][new_class]
                        & (std::uint16_t { 1U } << ordinal))
                    != 0U) {
                    match = either(match,
                        both(before[old_class], after[new_class]));
                }
            }
        }
        return match;
    };

    std::vector<RegisterId> outputs(
        result_width, logic_constant(runtime::Logic9::x));
    for (auto row_id = table->vhdl->operands.rbegin();
         row_id != table->vhdl->operands.rend(); ++row_id) {
        const auto row = specialized_hir_unit_->find_expression(*row_id);
        const auto required_width = data_width + num_states + result_width;
        if (!row || row->vhdl == nullptr
            || row->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
            || row->vhdl->operands.size() != required_width) {
            report("FSIM-ELAB-VITAL-015",
                "each VitalStateTable row must contain DataIn, "
                "present-state, and Result symbols with exact profile widths",
                row && row->vhdl != nullptr
                    ? hir_source_span(row->vhdl->source)
                    : expression_span(*row_id));
            return true;
        }
        auto row_match = bool_constant(true);
        for (std::size_t column { }; column < data_width; ++column) {
            const auto symbol_expression
                = specialized_hir_unit_->find_expression(
                    row->vhdl->operands[column]);
            const auto symbol = symbol_expression
                    && symbol_expression->vhdl != nullptr
                ? vital_state_symbol(*symbol_expression->vhdl)
                : std::nullopt;
            if (!symbol || *symbol == 'Z') {
                report("FSIM-ELAB-VITAL-016",
                    "a VitalStateTable input column contains an invalid state symbol",
                    expression_span(row->vhdl->operands[column]));
                return true;
            }
            const auto matched = symbol_match(*symbol,
                bit(*previous_register, previous_width, column),
                bit(*data_register, data_width, column), false);
            if (!matched) {
                report("FSIM-ELAB-VITAL-016",
                    "a VitalStateTable input column contains an invalid state symbol",
                    expression_span(row->vhdl->operands[column]));
                return true;
            }
            row_match = both(row_match, *matched);
        }
        for (std::size_t column { }; column < num_states; ++column) {
            const auto table_column = data_width + column;
            const auto symbol_expression
                = specialized_hir_unit_->find_expression(
                    row->vhdl->operands[table_column]);
            const auto symbol = symbol_expression
                    && symbol_expression->vhdl != nullptr
                ? vital_state_symbol(*symbol_expression->vhdl)
                : std::nullopt;
            const auto current = bit(result_register, result_width, column);
            const auto matched = symbol
                ? symbol_match(*symbol, current, current, true)
                : std::nullopt;
            if (!matched) {
                report("FSIM-ELAB-VITAL-016",
                    "a VitalStateTable present-state column requires X, 0, 1, -, or B",
                    expression_span(row->vhdl->operands[table_column]));
                return true;
            }
            row_match = both(row_match, *matched);
        }
        for (std::size_t output { }; output < result_width; ++output) {
            const auto table_column = data_width + num_states + output;
            const auto symbol_expression
                = specialized_hir_unit_->find_expression(
                    row->vhdl->operands[table_column]);
            const auto symbol = symbol_expression
                    && symbol_expression->vhdl != nullptr
                ? vital_state_symbol(*symbol_expression->vhdl)
                : std::nullopt;
            RegisterId value { };
            if (symbol && *symbol == 'X') {
                value = logic_constant(runtime::Logic9::x);
            } else if (symbol && *symbol == '0') {
                value = logic_constant(runtime::Logic9::zero);
            } else if (symbol && *symbol == '1') {
                value = logic_constant(runtime::Logic9::one);
            } else if (symbol && *symbol == 'Z') {
                value = logic_constant(runtime::Logic9::z);
            } else if (symbol && (*symbol == '-' || *symbol == 'S')) {
                value = bit(result_register, result_width, output);
            } else {
                report("FSIM-ELAB-VITAL-016",
                    "a VitalStateTable output column requires X, 0, 1, -, Z, or S",
                    expression_span(row->vhdl->operands[table_column]));
                return true;
            }
            const auto selected = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(ConditionalSelect {
                selected, row_match, value, outputs[output] });
            outputs[output] = selected;
        }
    }

    auto evaluated = outputs.front();
    if (result_width > 1U) {
        evaluated = allocate_register(
            result_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Concatenate { evaluated,
            std::move(outputs), static_cast<std::uint32_t>(result_width) });
    }
    if (variable_profile) {
        process_.operations.emplace_back(
            CopyRegister { result_register, evaluated });
        if (data_width != 0U) {
            process_.operations.emplace_back(Insert { *previous_register,
                *previous_register, *data_register,
                static_cast<std::uint32_t>(previous_width - data_width) });
        }
    } else {
        process_.operations.emplace_back(
            WriteUpdate { *result_binding->signal, evaluated });
    }
    return true;
}

} // namespace fsim::elaboration
