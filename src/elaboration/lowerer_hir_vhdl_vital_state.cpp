// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "lowerer_vhdl_vital_names.hpp"

#include <cctype>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace vital_detail;

namespace {

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
    const auto qualified_name = procedure.text();
    if (!equal_ignoring_ascii_case(
            vital_simple_name(qualified_name), "vitalstatetable")
        || !has_vital_package_qualification(
            qualified_name, "vital_primitives")) {
        return false;
    }
    return hir_vhdl_vital_intrinsic_visible(procedure, statement->vhdl->scope);
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

    bool valid = true;
    const auto actuals = collect_hir_vital_actuals(source.procedure_arguments,
        "FSIM-ELAB-VITAL-013", "VitalStateTable", valid);
    for (const auto& [formal, expression_id] : actuals.named) {
        if (formal != "result" && formal != "previousdatain"
            && formal != "statetable" && formal != "datain"
            && formal != "numstates") {
            report("FSIM-ELAB-VITAL-013",
                "VitalStateTable has no formal parameter '" + formal + "'",
                hir_expression_span(expression_id, statement_span));
            valid = false;
        }
    }
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

    const auto result_expression = actuals.find("result", 0U);
    const auto result_binding = result_expression
        ? binding(*result_expression, true)
        : std::nullopt;
    if (!result_binding
        || (!result_binding->local && !result_binding->signal)) {
        report("FSIM-ELAB-VITAL-014",
            "VitalStateTable Result requires a writable variable or signal",
            result_expression
                ? hir_expression_span(*result_expression, statement_span)
                : statement_span);
        return true;
    }
    const bool variable_profile = result_binding->local.has_value();
    if (result_binding->domain != frontend::ValueDomain::Logic9
        || result_binding->width == 0U) {
        report("FSIM-ELAB-VITAL-014",
            "VitalStateTable Result must have a concrete standard-logic width",
            hir_expression_span(*result_expression, statement_span));
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
        ? actuals.find("previousdatain", 1U)
        : std::nullopt;
    auto table_expression = actuals.find(
        "statetable", variable_profile ? 2U : 1U);
    const auto data_expression = actuals.find(
        "datain", variable_profile ? 3U : 2U);
    const auto states_expression = actuals.find(
        "numstates", variable_profile ? 4U : 3U);
    const auto maximum_actuals = variable_profile ? 5U : 4U;
    if (actuals.positional.size() > maximum_actuals) {
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
    // A table actual naming a constant stands for its value.
    if (const auto declaration
        = hir_referenced_declaration(*table_expression)) {
        if (const auto initializer = hir_constant_initializer(*declaration)) {
            table_expression = *initializer;
        }
    }
    const auto table = specialized_hir_unit_->find_expression(
        *table_expression);
    if (!table || table->vhdl == nullptr
        || table->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
        || table->vhdl->operands.empty()) {
        report("FSIM-ELAB-VITAL-015",
            "VitalStateTable requires a nonempty static two-dimensional table",
            hir_expression_span(*table_expression, statement_span));
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
                hir_expression_span(*data_expression, statement_span));
            return true;
        }
        data_width = *width;
        if (data_width != 0U) {
            // DataIn is a STD_LOGIC_VECTOR formal; an aggregate actual takes
            // that type (IEEE 1076-2008 9.3.3.1, IEEE 1076.4 7.3.3).
            semantic::vhdl::SubtypeIndication data_type;
            data_type.type_mark.spelling = "std_logic_vector";
            data_type.builtin_type = semantic::vhdl::BuiltinTypeIdentity::
                ieee_std_logic_1164_std_logic_vector;
            data_type.domain = semantic::vhdl::ValueDomain::logic9;
            data_type.unconstrained = true;
            data_type.executable_width = data_width;
            data_register = lower_hir_expression(*data_expression, data_width,
                frontend::SystemVerilogScalarKind::None, &data_type);
            if (!data_register
                || register_domain(*data_register)
                    != frontend::ValueDomain::Logic9) {
                report("FSIM-ELAB-VITAL-014",
                    "VitalStateTable DataIn must be a standard-logic vector",
                    hir_expression_span(*data_expression, statement_span));
                return true;
            }
        }
    } else {
        const auto data_binding = binding(*data_expression, false);
        if (!data_binding || !data_binding->signal) {
            report("FSIM-ELAB-VITAL-014",
                "the signal VitalStateTable profile requires a signal DataIn",
                hir_expression_span(*data_expression, statement_span));
            return true;
        }
        if (data_binding->domain != frontend::ValueDomain::Logic9) {
            report("FSIM-ELAB-VITAL-014",
                "VitalStateTable DataIn must be a standard-logic vector",
                hir_expression_span(*data_expression, statement_span));
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
        record_implicit_signal_dependency(*data_signal);
    }

    std::size_t num_states = result_width == 1U ? 1U : 0U;
    if (states_expression) {
        const auto value = hir_constant_integer(*states_expression);
        if (!value || *value < 0) {
            report("FSIM-ELAB-VITAL-015",
                "VitalStateTable NumStates must be a static natural value",
                hir_expression_span(*states_expression, statement_span));
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
                hir_expression_span(*previous_expression, statement_span));
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
        const auto result = load_constant(unsigned_value(value ? 1U : 0U, 1U),
            frontend::ValueDomain::Boolean);
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
                    : hir_expression_span(*row_id, statement_span));
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
                    "a VitalStateTable input column contains an invalid state "
                    "symbol",
                    hir_expression_span(
                        row->vhdl->operands[column], statement_span));
                return true;
            }
            const auto matched = symbol_match(*symbol,
                bit(*previous_register, previous_width, column),
                bit(*data_register, data_width, column), false);
            if (!matched) {
                report("FSIM-ELAB-VITAL-016",
                    "a VitalStateTable input column contains an invalid state "
                    "symbol",
                    hir_expression_span(
                        row->vhdl->operands[column], statement_span));
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
                    "a VitalStateTable present-state column requires X, 0, 1, "
                    "-, or B",
                    hir_expression_span(
                        row->vhdl->operands[table_column], statement_span));
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
                    "a VitalStateTable output column requires X, 0, 1, -, Z, "
                    "or S",
                    hir_expression_span(
                        row->vhdl->operands[table_column], statement_span));
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
