// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view vital_state_simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0U : separator + 1U);
}

std::optional<char> vital_state_symbol(const Expression& expression) {
  auto text = std::string_view{expression.text};
  constexpr std::string_view enum_prefix{"@fsim-enum:"};
  if (text.starts_with(enum_prefix)) text.remove_prefix(enum_prefix.size());
  if (text.size() >= 3U && text.front() == '\'' && text.back() == '\'') {
    text.remove_prefix(1U);
    text.remove_suffix(1U);
  }
  if (text.size() != 1U) return std::nullopt;
  return text.front();
}

}  // namespace

bool Lowerer::lower_vhdl_vital_state_table_call(
    const Statement& statement) {
  if (vital_state_simple_name(statement.procedure_name)
      != "vitalstatetable") {
    return false;
  }

  std::vector<const Expression*> positional;
  std::map<std::string, const Expression*, std::less<>> named;
  bool saw_named{};
  bool valid = true;
  for (const auto& association : statement.procedure_arguments) {
    if (association.formal) {
      saw_named = true;
      if (!named.emplace(*association.formal, &association.value).second) {
        report(
            "FSIM-ELAB-VITAL-013",
            "duplicate VitalStateTable actual for formal '"
                + *association.formal + "'",
            association.span);
        valid = false;
      }
    } else {
      if (saw_named) {
        report(
            "FSIM-ELAB-VITAL-013",
            "a positional VitalStateTable actual cannot follow a named actual",
            association.span);
        valid = false;
      }
      positional.push_back(&association.value);
    }
  }
  for (const auto& [formal, expression] : named) {
    if (formal != "result" && formal != "previousdatain"
        && formal != "statetable" && formal != "datain"
        && formal != "numstates") {
      report(
          "FSIM-ELAB-VITAL-013",
          "VitalStateTable has no formal parameter '" + formal + "'",
          expression->span);
      valid = false;
    }
  }
  const auto named_actual = [&](const std::string_view formal)
      -> const Expression* {
    const auto found = named.find(formal);
    return found == named.end() ? nullptr : found->second;
  };
  const Expression* result_expression = named_actual("result");
  if (result_expression == nullptr && !positional.empty()) {
    result_expression = positional.front();
  }
  if (result_expression == nullptr
      || result_expression->kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-VITAL-014",
        "VitalStateTable Result requires a writable variable or signal",
        result_expression == nullptr ? statement.span : result_expression->span);
    return true;
  }

  const auto result_local = locals_.find(result_expression->text);
  const auto result_signal = signals_.find(result_expression->text);
  const bool variable_profile = result_local != locals_.end();
  if (!variable_profile && result_signal == signals_.end()) {
    report(
        "FSIM-ELAB-VITAL-014",
        "VitalStateTable Result is not a visible writable object",
        result_expression->span);
    return true;
  }
  const auto actual = [&](const std::string_view formal,
                          const std::size_t position) -> const Expression* {
    if (const auto* selected = named_actual(formal)) return selected;
    return position < positional.size() ? positional[position] : nullptr;
  };
  const auto* previous_expression = variable_profile
      ? actual("previousdatain", 1U) : nullptr;
  const auto* table_expression = actual(
      "statetable", variable_profile ? 2U : 1U);
  const auto* data_expression = actual(
      "datain", variable_profile ? 3U : 2U);
  const auto* states_expression = actual(
      "numstates", variable_profile ? 4U : 3U);
  const auto maximum_actuals = variable_profile ? 5U : 4U;
  if (positional.size() > maximum_actuals) {
    report(
        "FSIM-ELAB-VITAL-013",
        "VitalStateTable has too many positional actuals",
        statement.span);
    valid = false;
  }
  if (table_expression == nullptr || data_expression == nullptr
      || (variable_profile && previous_expression == nullptr)) {
    report(
        "FSIM-ELAB-VITAL-013",
        "VitalStateTable is missing a required table, data, or state actual",
        statement.span);
    return true;
  }
  if (table_expression->kind != ExpressionKind::Aggregate
      || table_expression->operands.empty()) {
    report(
        "FSIM-ELAB-VITAL-015",
        "VitalStateTable requires a nonempty static two-dimensional table",
        table_expression->span);
    return true;
  }

  RegisterId result_register{};
  std::size_t result_width{};
  std::optional<SignalId> result_signal_id;
  if (variable_profile) {
    const auto* result_type = object_type(result_expression->text);
    if (result_type == nullptr
        || result_type->domain != frontend::ValueDomain::Logic9
        || !result_type->width() || *result_type->width() == 0U) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable Result must have a concrete standard-logic width",
          result_expression->span);
      return true;
    }
    result_register = result_local->second;
    result_width = *result_type->width();
  } else {
    const auto& info = design_.signal_info_[result_signal->second];
    if (info.source_domain != frontend::ValueDomain::Logic9
        || info.width == 0U) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable Result must have a concrete standard-logic width",
          result_expression->span);
      return true;
    }
    result_width = info.width;
    result_signal_id = result_signal->second;
    result_register = allocate_register(
        result_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(
        ReadSignal{result_register, *result_signal_id});
  }

  std::size_t data_width{};
  std::optional<SignalId> data_signal_id;
  std::optional<RegisterId> data_register;
  if (!variable_profile) {
    if (data_expression->kind != ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-VITAL-014",
          "the signal VitalStateTable profile requires a signal DataIn",
          data_expression->span);
      return true;
    }
    const auto found = signals_.find(data_expression->text);
    if (found == signals_.end()) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable DataIn is not a visible signal",
          data_expression->span);
      return true;
    }
    const auto& info = design_.signal_info_[found->second];
    if (info.source_domain != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable DataIn must be a standard-logic vector",
          data_expression->span);
      return true;
    }
    data_width = info.width;
    data_signal_id = found->second;
    if (data_width != 0U) {
      data_register = allocate_register(
          data_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(
          ReadSignal{*data_register, *data_signal_id});
    }
  } else {
    const auto width = infer_width(*data_expression);
    if (!width) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable DataIn requires a concrete vector width",
          data_expression->span);
      return true;
    }
    data_width = *width;
    if (data_width != 0U) {
      data_register = lower_expression(*data_expression, data_width);
      if (!data_register
          || register_domain(*data_register)
              != frontend::ValueDomain::Logic9) {
        report(
            "FSIM-ELAB-VITAL-014",
            "VitalStateTable DataIn must be a standard-logic vector",
            data_expression->span);
        return true;
      }
    }
  }

  std::size_t num_states = result_width == 1U ? 1U : 0U;
  if (states_expression != nullptr) {
    std::string error;
    const auto value = evaluate_constant_expression(
        *states_expression, {}, error);
    if (!value || *value < 0) {
      report(
          "FSIM-ELAB-VITAL-015",
          "VitalStateTable NumStates must be a static natural value",
          states_expression->span);
      valid = false;
    } else {
      num_states = static_cast<std::size_t>(*value);
    }
  } else if (result_width != 1U) {
    report(
        "FSIM-ELAB-VITAL-013",
        "the vector VitalStateTable profile requires NumStates",
        statement.span);
    valid = false;
  }
  if (num_states > result_width) {
    report(
        "FSIM-ELAB-VITAL-015",
        "VitalStateTable NumStates exceeds the Result width",
        statement.span);
    valid = false;
  }

  std::optional<RegisterId> previous_register;
  std::size_t previous_width = data_width;
  if (variable_profile) {
    if (previous_expression->kind != ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable PreviousDataIn requires a writable variable",
          previous_expression->span);
      return true;
    }
    const auto found = locals_.find(previous_expression->text);
    const auto* type = object_type(previous_expression->text);
    if (found == locals_.end() || type == nullptr
        || type->domain != frontend::ValueDomain::Logic9
        || !type->width() || *type->width() < data_width) {
      report(
          "FSIM-ELAB-VITAL-014",
          "VitalStateTable PreviousDataIn must be a writable standard-logic "
          "vector at least as wide as DataIn",
          previous_expression->span);
      return true;
    }
    previous_register = found->second;
    previous_width = *type->width();
  } else if (data_width != 0U) {
    previous_register = allocate_register(
        data_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(
        SignalLastValue{*previous_register, *data_signal_id});
  }
  if (!valid) return true;

  const auto bool_constant = [&](const bool value) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(LoadConstant{
        result, unsigned_value(value ? 1U : 0U, 1U)});
    return result;
  };
  const auto logic_constant = [&](const runtime::Logic9 value) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Logic9);
    runtime::PackedLogic4 packed(1U);
    packed.fill(value);
    process_.operations.emplace_back(
        LoadConstant{result, std::move(packed)});
    return result;
  };
  const auto is_state = [&](const RegisterId value,
                            const runtime::Logic9 state) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, result, value, logic_constant(state)});
    return result;
  };
  const auto combine = [&](const BinaryOperator operation,
                           const RegisterId lhs,
                           const RegisterId rhs) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{operation, result, lhs, rhs});
    return result;
  };
  const auto either = [&](const RegisterId lhs, const RegisterId rhs) {
    return combine(BinaryOperator::bit_or, lhs, rhs);
  };
  const auto both = [&](const RegisterId lhs, const RegisterId rhs) {
    return combine(BinaryOperator::bit_and, lhs, rhs);
  };
  const auto inverted = [&](const RegisterId value) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(UnaryNot{result, value});
    return result;
  };
  const auto bit = [&](const RegisterId value,
                       const std::size_t width,
                       const std::size_t left_index) {
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Extract{
        result, value,
        static_cast<std::uint32_t>(width - 1U - left_index), 1U});
    return result;
  };
  const auto x01_classes = [&](const RegisterId value) {
    const auto zero = either(
        is_state(value, runtime::Logic9::zero),
        is_state(value, runtime::Logic9::l));
    const auto one = either(
        is_state(value, runtime::Logic9::one),
        is_state(value, runtime::Logic9::h));
    return std::array<RegisterId, 3>{
        inverted(either(zero, one)), zero, one};
  };
  const auto symbol_match = [&](const char symbol,
                                const RegisterId previous,
                                const RegisterId current,
                                const bool state_column)
      -> std::optional<RegisterId> {
    const auto before = x01_classes(previous);
    const auto after = x01_classes(current);
    if (symbol == '-') return bool_constant(true);
    if (symbol == 'X') return after[0];
    if (symbol == '0') return after[1];
    if (symbol == '1') return after[2];
    if (symbol == 'B') return either(after[1], after[2]);
    if (state_column) return std::nullopt;
    if (symbol == 'S') {
      return either(
          both(before[1], after[1]), both(before[2], after[2]));
    }
    static constexpr std::array<char, 16> edges{
        '/', '\\', 'P', 'N', 'r', 'f', 'p', 'n',
        'R', 'F', '^', 'v', 'E', 'A', 'D', '*'};
    const auto found = std::ranges::find(edges, symbol);
    if (found == edges.end()) return std::nullopt;
    const auto ordinal = static_cast<unsigned>(
        std::distance(edges.begin(), found));
    constexpr std::array<std::array<std::uint16_t, 3>, 3> masks{{
        {{0U, 0xDA08U, 0xB504U}},
        {{0xA150U, 0U, 0x8145U}},
        {{0xC2A0U, 0x828AU, 0U}},
    }};
    auto match = bool_constant(false);
    for (std::size_t old_class = 0; old_class < 3U; ++old_class) {
      for (std::size_t new_class = 0; new_class < 3U; ++new_class) {
        if ((masks[old_class][new_class]
             & (std::uint16_t{1U} << ordinal)) != 0U) {
          match = either(
              match, both(before[old_class], after[new_class]));
        }
      }
    }
    return match;
  };

  std::vector<RegisterId> outputs(
      result_width, logic_constant(runtime::Logic9::x));
  for (auto row = table_expression->operands.rbegin();
       row != table_expression->operands.rend(); ++row) {
    const auto required_width = data_width + num_states + result_width;
    if (row->kind != ExpressionKind::Aggregate
        || row->operands.size() != required_width) {
      report(
          "FSIM-ELAB-VITAL-015",
          "each VitalStateTable row must contain DataIn, present-state, and "
          "Result symbols with exact profile widths",
          row->span);
      return true;
    }
    auto row_match = bool_constant(true);
    for (std::size_t column = 0; column < data_width; ++column) {
      const auto symbol = vital_state_symbol(row->operands[column]);
      if (!symbol || *symbol == 'Z') {
        report(
            "FSIM-ELAB-VITAL-016",
            "a VitalStateTable input column contains an invalid state symbol",
            row->operands[column].span);
        return true;
      }
      const auto matched = symbol_match(
          *symbol,
          bit(*previous_register, previous_width, column),
          bit(*data_register, data_width, column), false);
      if (!matched) {
        report(
            "FSIM-ELAB-VITAL-016",
            "a VitalStateTable input column contains an invalid state symbol",
            row->operands[column].span);
        return true;
      }
      row_match = both(row_match, *matched);
    }
    for (std::size_t column = 0; column < num_states; ++column) {
      const auto table_column = data_width + column;
      const auto symbol = vital_state_symbol(row->operands[table_column]);
      const auto current = bit(result_register, result_width, column);
      const auto matched = symbol
          ? symbol_match(*symbol, current, current, true)
          : std::optional<RegisterId>{};
      if (!matched) {
        report(
            "FSIM-ELAB-VITAL-016",
            "a VitalStateTable present-state column requires X, 0, 1, -, or B",
            row->operands[table_column].span);
        return true;
      }
      row_match = both(row_match, *matched);
    }
    for (std::size_t output = 0; output < result_width; ++output) {
      const auto table_column = data_width + num_states + output;
      const auto symbol = vital_state_symbol(row->operands[table_column]);
      RegisterId value{};
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
        report(
            "FSIM-ELAB-VITAL-016",
            "a VitalStateTable output column requires X, 0, 1, -, Z, or S",
            row->operands[table_column].span);
        return true;
      }
      const auto selected = allocate_register(
          1U, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(ConditionalSelect{
          selected, row_match, value, outputs[output]});
      outputs[output] = selected;
    }
  }

  RegisterId evaluated = outputs.front();
  if (result_width > 1U) {
    evaluated = allocate_register(
        result_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Concatenate{
        evaluated, std::move(outputs),
        static_cast<std::uint32_t>(result_width)});
  }
  if (variable_profile) {
    process_.operations.emplace_back(
        CopyRegister{result_register, evaluated});
    if (data_width != 0U) {
      process_.operations.emplace_back(Insert{
          *previous_register, *previous_register, *data_register,
          static_cast<std::uint32_t>(previous_width - data_width)});
    }
  } else {
    process_.operations.emplace_back(
        WriteUpdate{*result_signal_id, evaluated});
  }
  return true;
}

}  // namespace fsim::elaboration
