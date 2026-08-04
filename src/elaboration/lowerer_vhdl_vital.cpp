// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0 : separator + 1);
}

bool is_vital_logic_name(const std::string_view name) {
  for (const std::string_view prefix : {
           "vitaland", "vitalor", "vitalxor", "vitalnand",
           "vitalnor", "vitalxnor"}) {
    if (name == prefix
        || (name.starts_with(prefix) && name.size() == prefix.size() + 1U
            && name.back() >= '2' && name.back() <= '4')) {
      return true;
    }
  }
  return false;
}

constexpr std::size_t vital_x01_ordinal(const runtime::Logic9 value) {
  if (value == runtime::Logic9::zero || value == runtime::Logic9::l) return 1;
  if (value == runtime::Logic9::one || value == runtime::Logic9::h) return 2;
  return 0;
}

constexpr std::uint16_t vital_edge_symbol_mask(
    const runtime::Logic9 previous,
    const runtime::Logic9 current) {
  // VitalEdgeSymbolType declaration order is
  // /, \, P, N, r, f, p, n, R, F, ^, v, E, A, D, *.
  constexpr std::array<std::array<std::uint16_t, 3>, 3> masks{{
      {{0, 0xDA08U, 0xB504U}},
      {{0xA150U, 0, 0x8145U}},
      {{0xC2A0U, 0x828AU, 0}},
  }};
  return masks[vital_x01_ordinal(previous)][vital_x01_ordinal(current)];
}

constexpr bool vital_edge_matches(
    const runtime::Logic9 previous,
    const runtime::Logic9 current,
    const std::size_t symbol_ordinal) {
  return symbol_ordinal < 16U
      && (vital_edge_symbol_mask(previous, current)
          & (std::uint16_t{1} << symbol_ordinal)) != 0;
}

static_assert(vital_edge_matches(runtime::Logic9::zero, runtime::Logic9::one, 0));
static_assert(vital_edge_matches(runtime::Logic9::one, runtime::Logic9::zero, 1));
static_assert(vital_edge_matches(runtime::Logic9::x, runtime::Logic9::one, 12));
static_assert(vital_edge_matches(runtime::Logic9::l, runtime::Logic9::h, 15));
static_assert(!vital_edge_matches(runtime::Logic9::zero, runtime::Logic9::zero, 15));

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_vital_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || (expression.kind != ExpressionKind::Identifier
          && expression.kind != ExpressionKind::Call)) {
    return ExpressionAttempt{};
  }
  auto memory = lower_vhdl_vital_memory_expression(
      expression, expected_width, expected_type);
  if (memory.handled) return memory;
  const auto name = simple_name(expression.text);
  if (name == "vitaltimingdatainit"
      || name == "vitalperioddatainit"
      || name == "vitalskewdatainit") {
    const auto required_width = name == "vitaltimingdatainit"
        ? 261U
        : name == "vitalperioddatainit" ? 130U : 259U;
    if (expected_width != required_width
        || (expression.kind == ExpressionKind::Call
            && !expression.operands.empty())) {
      report(
          "FSIM-ELAB-VITAL-001",
          std::string{name} + " requires its exact parameterless record type",
          expression.span);
      return std::nullopt;
    }
    runtime::PackedLogic4 value(expected_width);
    value.fill(runtime::Logic9::zero);
    if (name == "vitaltimingdatainit") {
      value.set_logic9(259, runtime::Logic9::x);
      value.set_logic9(193, runtime::Logic9::x);
    } else if (name == "vitalperioddatainit") {
      value.set_logic9(129, runtime::Logic9::x);
    }
    const auto result = allocate_register(
        expected_width,
        expected_type != nullptr
            ? expected_type->domain
            : frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(
        LoadConstant{result, std::move(value)});
    return result;
  }
  if (expression.kind == ExpressionKind::Identifier) {
    std::optional<std::string_view> mapped;
    if (name == "vitaldefaultoutputmap") {
      mapped = "UX01ZWLH-";
    } else if (name == "vitaldefaultresultmap") {
      mapped = "UX01";
    } else if (name == "vitaldefaultresultzmap") {
      mapped = "UX01Z";
    }
    if (mapped) {
      if (expected_width != mapped->size()) {
        report(
            "FSIM-ELAB-VITAL-001",
            std::string{name} + " requires its exact VITAL map type",
            expression.span);
        return std::nullopt;
      }
      const auto result = allocate_register(
          expected_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(LoadConstant{
          result, runtime::PackedLogic4::from_logic9_msb_string(*mapped)});
      return result;
    }
    if (name == "vitalzerodelay" || name == "vitalzerodelay01"
        || name == "vitalzerodelay01z" || name == "vitalzerodelay01zx"
        || name == "vitaldefdelay01" || name == "vitaldefdelay01z") {
      if (expected_width == 0) {
        report(
            "FSIM-ELAB-VITAL-001",
            std::string{name} + " has no concrete contextual delay type",
            expression.span);
        return std::nullopt;
      }
      const auto result = allocate_register(
          expected_width,
          expected_type != nullptr
              ? expected_type->domain
              : frontend::ValueDomain::Integer);
      process_.operations.emplace_back(LoadConstant{
          result, unsigned_value(0, expected_width)});
      return result;
    }
    return ExpressionAttempt{};
  }

  const auto make_state = [&](const runtime::Logic9 state) {
    const auto result = allocate_register(1, frontend::ValueDomain::Logic9);
    runtime::PackedLogic4 value(1);
    value.fill(state);
    process_.operations.emplace_back(LoadConstant{result, std::move(value)});
    return result;
  };
  const auto state_matches = [&](const RegisterId value,
                                 const runtime::Logic9 state) {
    const auto result = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, result, value, make_state(state)});
    return result;
  };
  const auto either = [&](const RegisterId lhs, const RegisterId rhs) {
    const auto result = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::bit_or, result, lhs, rhs});
    return result;
  };
  const auto select = [&](const RegisterId condition,
                          const RegisterId when_true,
                          const RegisterId when_false,
                          const std::size_t width,
                          const frontend::ValueDomain domain) {
    const auto result = allocate_register(width, domain);
    process_.operations.emplace_back(ConditionalSelect{
        result, condition, when_true, when_false});
    return result;
  };
  const auto normalize_ux01 = [&](const RegisterId value) {
    auto normalized = make_state(runtime::Logic9::x);
    for (const auto& [state, mapped] : {
             std::pair{runtime::Logic9::u, runtime::Logic9::u},
             std::pair{runtime::Logic9::zero, runtime::Logic9::zero},
             std::pair{runtime::Logic9::l, runtime::Logic9::zero},
             std::pair{runtime::Logic9::one, runtime::Logic9::one},
             std::pair{runtime::Logic9::h, runtime::Logic9::one}}) {
      normalized = select(
          state_matches(value, state), make_state(mapped), normalized,
          1, frontend::ValueDomain::Logic9);
    }
    return normalized;
  };
  const auto apply_result_map = [&](const RegisterId raw,
                                    const Expression* map_expression)
      -> std::optional<RegisterId> {
    if (map_expression == nullptr) {
      return raw;
    }
    const auto map = lower_expression(*map_expression, 4);
    if (!map || register_domain(*map) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-005",
          "a VITAL logic primitive requires a VitalResultMapType map",
          map_expression->span);
      return std::nullopt;
    }
    auto mapped = make_state(runtime::Logic9::u);
    for (std::size_t ordinal = 0; ordinal < 4; ++ordinal) {
      const auto element = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Extract{
          element, *map, static_cast<std::uint32_t>(3U - ordinal), 1});
      mapped = select(
          state_matches(raw, static_cast<runtime::Logic9>(ordinal)),
          element, mapped, 1, frontend::ValueDomain::Logic9);
    }
    return mapped;
  };

  if (name == "vitalextendtofilldelay") {
    if (expression.operands.size() != 1
        || (!expression.call_argument_names.empty()
            && !expression.call_argument_names.front().empty()
            && expression.call_argument_names.front() != "delay")) {
      report(
          "FSIM-ELAB-VITAL-003",
          "vitalextendtofilldelay requires one Delay argument",
          expression.span);
      return std::nullopt;
    }
    auto source_width = infer_width(expression.operands.front());
    if (expression.operands.front().kind == ExpressionKind::Aggregate
        && !expression.operands.front().operands.empty()) {
      source_width = expression.operands.front().operands.size() * 64U;
    }
    if (!source_width
        || (*source_width != 64 && *source_width != 128
            && *source_width != 384 && *source_width != 768)
        || (expected_width != 64 && expected_width != 128
            && expected_width != 384 && expected_width != 768)) {
      report(
          "FSIM-ELAB-VITAL-006",
          "vitalextendtofilldelay requires concrete scalar, 01, 01Z, or "
          "01ZX delay types",
          expression.span);
      return std::nullopt;
    }
    std::optional<RegisterId> source;
    if (expression.operands.front().kind == ExpressionKind::Aggregate) {
      std::vector<RegisterId> elements;
      elements.reserve(expression.operands.front().operands.size());
      for (const auto& element : expression.operands.front().operands) {
        if (element.kind == ExpressionKind::Unary
            && element.text == "-") {
          report(
              "FSIM-ELAB-VITAL-006",
              "VITAL delay elements must be nonnegative TIME values",
              element.span);
          return std::nullopt;
        }
        const auto inferred_type = vhdl_expression_type(element);
        const auto* element_type = inferred_type
            ? &*inferred_type
            : expected_type != nullptr && expected_type->vhdl_array
                    && !expected_type->vhdl_array->element_types.empty()
                ? &expected_type->vhdl_array->element_types.front()
                : visible_type_mark("time");
        if (element_type == nullptr) {
          report(
              "FSIM-ELAB-VITAL-006",
              "vitalextendtofilldelay cannot determine a TIME element type",
              element.span);
          return std::nullopt;
        }
        const auto value = lower_expression(
            element, 64, element_type);
        if (!value) {
          return std::nullopt;
        }
        elements.push_back(*value);
      }
      const auto aggregate = allocate_register(
          *source_width, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(Concatenate{
          aggregate, std::move(elements),
          static_cast<std::uint32_t>(*source_width)});
      source = aggregate;
    } else {
      source = lower_expression(
          expression.operands.front(), *source_width);
    }
    if (!source) {
      return std::nullopt;
    }
    if (*source_width == expected_width) {
      return source;
    }
    const auto source_count = *source_width / 64U;
    const auto target_count = expected_width / 64U;
    const std::array<std::size_t, 12> transition_group{
        0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    std::vector<RegisterId> values;
    values.reserve(target_count);
    for (std::size_t target = 0; target < target_count; ++target) {
      std::size_t source_index = 0;
      if (source_count == 2) {
        source_index = transition_group[target];
      } else if (source_count == 6) {
        source_index = target < 6U ? target : transition_group[target];
      } else if (source_count == 12) {
        source_index = target;
      }
      const auto part = allocate_register(
          64, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(Extract{
          part, *source,
          static_cast<std::uint32_t>((source_count - 1U - source_index) * 64U),
          64});
      values.push_back(part);
    }
    const auto result = allocate_register(
        expected_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(Concatenate{
        result, std::move(values), static_cast<std::uint32_t>(expected_width)});
    return result;
  }

  if (name == "vitalcalcdelay") {
    std::array<const Expression*, 3> arguments{};
    std::size_t positional = 0;
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      std::optional<std::size_t> target;
      const auto argument_name = expression.call_argument_names.empty()
          ? std::string_view{}
          : std::string_view{expression.call_argument_names[index]};
      if (argument_name == "newval") {
        target = 0;
      } else if (argument_name == "oldval") {
        target = 1;
      } else if (argument_name == "delay") {
        target = 2;
      } else if (argument_name.empty() && positional < arguments.size()) {
        target = positional++;
      }
      if (!target || arguments[*target] != nullptr) {
        report(
            "FSIM-ELAB-VITAL-003",
            "vitalcalcdelay has an unknown, duplicate, or misplaced argument",
            expression.operands[index].span);
        return std::nullopt;
      }
      arguments[*target] = &expression.operands[index];
    }
    if (std::ranges::any_of(arguments, [](const auto* value) {
          return value == nullptr;
        }) || expected_width != 64) {
      report(
          "FSIM-ELAB-VITAL-003",
          "vitalcalcdelay requires NewVal, OldVal, and Delay and returns TIME",
          expression.span);
      return std::nullopt;
    }
    auto new_value = lower_expression(*arguments[0], 1);
    auto old_value = lower_expression(*arguments[1], 1);
    const auto delay_width = infer_width(*arguments[2]);
    if (!new_value || !old_value || !delay_width
        || (*delay_width != 64 && *delay_width != 128
            && *delay_width != 384)) {
      report(
          "FSIM-ELAB-VITAL-006",
          "vitalcalcdelay requires std_ulogic values and a scalar, 01, or "
          "01Z delay",
          expression.span);
      return std::nullopt;
    }
    const auto* delay_type = *delay_width == 384
        ? visible_type_mark("vitaldelaytype01z")
        : *delay_width == 128
            ? visible_type_mark("vitaldelaytype01")
            : visible_type_mark("vitaldelaytype");
    const auto delay = lower_expression(
        *arguments[2], *delay_width, delay_type);
    if (!delay) {
      return std::nullopt;
    }
    const auto delay_count = *delay_width / 64U;
    const auto delay_at = [&](const std::size_t transition) {
      const auto result = allocate_register(
          64, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(Extract{
          result, *delay,
          static_cast<std::uint32_t>(
              (delay_count - 1U - transition) * 64U),
          64});
      return result;
    };
    if (delay_count == 1) {
      return delay_at(0);
    }
    const auto minimum = [&](const RegisterId lhs, const RegisterId rhs) {
      const auto less = allocate_register(
          1, frontend::ValueDomain::Boolean);
      process_.operations.emplace_back(Binary{
          BinaryOperator::less_unsigned, less, lhs, rhs});
      return select(
          less, lhs, rhs, 64, frontend::ValueDomain::Integer);
    };
    const auto maximum = [&](const RegisterId lhs, const RegisterId rhs) {
      const auto less = allocate_register(
          1, frontend::ValueDomain::Boolean);
      process_.operations.emplace_back(Binary{
          BinaryOperator::less_unsigned, less, lhs, rhs});
      return select(
          less, rhs, lhs, 64, frontend::ValueDomain::Integer);
    };
    const auto new_zero = either(
        state_matches(*new_value, runtime::Logic9::zero),
        state_matches(*new_value, runtime::Logic9::l));
    const auto new_one = either(
        state_matches(*new_value, runtime::Logic9::one),
        state_matches(*new_value, runtime::Logic9::h));
    const auto new_z = state_matches(*new_value, runtime::Logic9::z);
    const auto old_zero = either(
        state_matches(*old_value, runtime::Logic9::zero),
        state_matches(*old_value, runtime::Logic9::l));
    const auto old_one = either(
        state_matches(*old_value, runtime::Logic9::one),
        state_matches(*old_value, runtime::Logic9::h));
    const auto old_z = state_matches(*old_value, runtime::Logic9::z);
    const auto rise = delay_at(0);
    const auto fall = delay_at(1);
    if (delay_count == 2) {
      const auto old_other = maximum(fall, rise);
      const auto unknown_target = select(
          old_zero, rise,
          select(old_one, fall,
                 select(old_z, minimum(fall, rise), old_other, 64,
                        frontend::ValueDomain::Integer),
                 64, frontend::ValueDomain::Integer),
          64, frontend::ValueDomain::Integer);
      const auto z_target = select(
          old_zero, rise,
          select(old_one, fall, old_other, 64,
                 frontend::ValueDomain::Integer),
          64, frontend::ValueDomain::Integer);
      return select(
          new_zero, fall,
          select(new_one, rise,
                 select(new_z, z_target, unknown_target, 64,
                        frontend::ValueDomain::Integer),
                 64, frontend::ValueDomain::Integer),
          64, frontend::ValueDomain::Integer);
    }
    const auto tr0z = delay_at(2);
    const auto trz1 = delay_at(3);
    const auto tr1z = delay_at(4);
    const auto trz0 = delay_at(5);
    const auto from_unknown = select(
        new_zero, maximum(fall, trz0),
        select(new_one, maximum(rise, trz1),
               select(new_z, maximum(tr1z, tr0z), maximum(fall, rise), 64,
                      frontend::ValueDomain::Integer),
               64, frontend::ValueDomain::Integer),
        64, frontend::ValueDomain::Integer);
    const auto from_z = select(
        new_zero, trz0,
        select(new_one, trz1,
               select(new_z, maximum(tr0z, tr1z), minimum(trz1, trz0), 64,
                      frontend::ValueDomain::Integer),
               64, frontend::ValueDomain::Integer),
        64, frontend::ValueDomain::Integer);
    const auto from_one = select(
        new_zero, fall,
        select(new_one, rise,
               select(new_z, tr1z, minimum(fall, tr1z), 64,
                      frontend::ValueDomain::Integer),
               64, frontend::ValueDomain::Integer),
        64, frontend::ValueDomain::Integer);
    const auto from_zero = select(
        new_zero, fall,
        select(new_one, rise,
               select(new_z, tr0z, minimum(rise, tr0z), 64,
                      frontend::ValueDomain::Integer),
               64, frontend::ValueDomain::Integer),
        64, frontend::ValueDomain::Integer);
    return select(
        old_zero, from_zero,
        select(old_one, from_one,
               select(old_z, from_z, from_unknown, 64,
                      frontend::ValueDomain::Integer),
               64, frontend::ValueDomain::Integer),
        64, frontend::ValueDomain::Integer);
  }

  if (name == "vitaltruthtable") {
    const Expression* table_expression = nullptr;
    const Expression* data_expression = nullptr;
    std::size_t positional = 0;
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      const auto argument_name = expression.call_argument_names.empty()
          ? std::string_view{}
          : std::string_view{expression.call_argument_names[index]};
      const Expression** target = nullptr;
      if (argument_name == "truthtable"
          || (argument_name.empty() && positional == 0)) {
        target = &table_expression;
      } else if (argument_name == "datain"
                 || (argument_name.empty() && positional == 1)) {
        target = &data_expression;
      }
      if (target == nullptr || *target != nullptr) {
        report(
            "FSIM-ELAB-VITAL-003",
            "vitaltruthtable has an unknown, duplicate, or misplaced argument",
            expression.operands[index].span);
        return std::nullopt;
      }
      *target = &expression.operands[index];
      if (argument_name.empty()) {
        ++positional;
      }
    }
    const auto data_width = data_expression != nullptr
        ? infer_width(*data_expression)
        : std::optional<std::size_t>{};
    if (table_expression == nullptr || data_expression == nullptr
        || !data_width || *data_width == 0 || expected_width == 0
        || table_expression->kind != ExpressionKind::Aggregate
        || table_expression->operands.empty()) {
      report(
          "FSIM-ELAB-VITAL-008",
          "vitaltruthtable requires a nonempty static two-dimensional table, "
          "non-null DataIn, and a concrete result width",
          expression.span);
      return std::nullopt;
    }
    auto data = lower_expression(*data_expression, *data_width);
    if (!data) {
      return std::nullopt;
    }
    if (register_domain(*data) != frontend::ValueDomain::Logic9) {
      if (register_domain(*data) == frontend::ValueDomain::Bit2
          || register_domain(*data) == frontend::ValueDomain::Logic4) {
        const auto promoted = allocate_register(
            *data_width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister{promoted, *data});
        data = promoted;
      } else {
        report(
            "FSIM-ELAB-VITAL-004",
            "vitaltruthtable DataIn must be a standard-logic vector",
            data_expression->span);
        return std::nullopt;
      }
    }
    const auto symbol = [](const Expression& value)
        -> std::optional<char> {
      auto text = std::string_view{value.text};
      constexpr std::string_view enum_prefix{"@fsim-enum:"};
      if (text.starts_with(enum_prefix)) {
        text.remove_prefix(enum_prefix.size());
      }
      if (text.size() >= 3 && text.front() == '\'' && text.back() == '\'') {
        text.remove_prefix(1);
        text.remove_suffix(1);
      }
      if (text.size() != 1) {
        return std::nullopt;
      }
      return static_cast<char>(std::toupper(
          static_cast<unsigned char>(text.front())));
    };
    const auto bool_constant = [&](const bool value) {
      const auto result = allocate_register(
          1, frontend::ValueDomain::Boolean);
      process_.operations.emplace_back(LoadConstant{
          result, unsigned_value(value ? 1U : 0U, 1)});
      return result;
    };
    std::vector<RegisterId> outputs(
        expected_width, make_state(runtime::Logic9::x));
    for (auto row = table_expression->operands.rbegin();
         row != table_expression->operands.rend(); ++row) {
      if (row->kind != ExpressionKind::Aggregate
          || row->operands.size() != *data_width + expected_width) {
        report(
            "FSIM-ELAB-VITAL-008",
            "each vitaltruthtable row must contain exactly DataIn'length plus "
            "result-width symbols",
            row->span);
        return std::nullopt;
      }
      auto row_match = bool_constant(true);
      for (std::size_t column = 0; column < *data_width; ++column) {
        const auto table_symbol = symbol(row->operands[column]);
        if (!table_symbol || (*table_symbol != 'X' && *table_symbol != '0'
                              && *table_symbol != '1'
                              && *table_symbol != '-'
                              && *table_symbol != 'B')) {
          report(
              "FSIM-ELAB-VITAL-009",
              "a vitaltruthtable input symbol must be X, 0, 1, -, or B",
              row->operands[column].span);
          return std::nullopt;
        }
        const auto input = allocate_register(
            1, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Extract{
            input, *data,
            static_cast<std::uint32_t>(*data_width - 1U - column), 1});
        const auto is_zero = either(
            state_matches(input, runtime::Logic9::zero),
            state_matches(input, runtime::Logic9::l));
        const auto is_one = either(
            state_matches(input, runtime::Logic9::one),
            state_matches(input, runtime::Logic9::h));
        RegisterId matches = bool_constant(true);
        if (*table_symbol == '0') {
          matches = is_zero;
        } else if (*table_symbol == '1') {
          matches = is_one;
        } else if (*table_symbol == 'B') {
          matches = either(is_zero, is_one);
        } else if (*table_symbol == 'X') {
          const auto known = either(is_zero, is_one);
          const auto inverted = allocate_register(
              1, frontend::ValueDomain::Boolean);
          process_.operations.emplace_back(UnaryNot{inverted, known});
          matches = inverted;
        }
        const auto combined = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary{
            BinaryOperator::bit_and, combined, row_match, matches});
        row_match = combined;
      }
      for (std::size_t output = 0; output < expected_width; ++output) {
        const auto table_symbol = symbol(
            row->operands[*data_width + output]);
        runtime::Logic9 value = runtime::Logic9::x;
        if (table_symbol && *table_symbol == '0') {
          value = runtime::Logic9::zero;
        } else if (table_symbol && *table_symbol == '1') {
          value = runtime::Logic9::one;
        } else if (table_symbol && *table_symbol == 'Z') {
          value = runtime::Logic9::z;
        } else if (!table_symbol || (*table_symbol != 'X'
                                     && *table_symbol != '-')) {
          report(
              "FSIM-ELAB-VITAL-009",
              "a vitaltruthtable output symbol must be X, 0, 1, or Z",
              row->operands[*data_width + output].span);
          return std::nullopt;
        }
        outputs[output] = select(
            row_match, make_state(value), outputs[output],
            1, frontend::ValueDomain::Logic9);
      }
    }
    if (expected_width == 1) {
      return outputs.front();
    }
    const auto result = allocate_register(
        expected_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Concatenate{
        result, std::move(outputs), static_cast<std::uint32_t>(expected_width)});
    return result;
  }

  const bool mux = name == "vitalmux" || name == "vitalmux2"
      || name == "vitalmux4" || name == "vitalmux8";
  if (mux) {
    const Expression* data_expression = nullptr;
    const Expression* select_expression = nullptr;
    const Expression* result_map = nullptr;
    const Expression* data0_expression = nullptr;
    const Expression* data1_expression = nullptr;
    std::size_t positional = 0;
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      const auto argument_name = expression.call_argument_names.empty()
          ? std::string_view{}
          : std::string_view{expression.call_argument_names[index]};
      const auto assign_once = [&](const Expression*& target) {
        if (target != nullptr) {
          return false;
        }
        target = &expression.operands[index];
        return true;
      };
      bool assigned = false;
      if (name == "vitalmux2"
          && (argument_name == "data1"
              || (argument_name.empty() && positional == 0))) {
        assigned = assign_once(data1_expression);
      } else if (name == "vitalmux2"
                 && (argument_name == "data0"
                     || (argument_name.empty() && positional == 1))) {
        assigned = assign_once(data0_expression);
      } else if (name != "vitalmux2"
                 && (argument_name == "data"
                     || (argument_name.empty() && positional == 0))) {
        assigned = assign_once(data_expression);
      } else if (argument_name == "dselect"
                 || (argument_name.empty()
                     && positional == (name == "vitalmux2" ? 2U : 1U))) {
        assigned = assign_once(select_expression);
      } else if (argument_name == "resultmap"
                 || argument_name.empty()) {
        assigned = assign_once(result_map);
      }
      if (!assigned) {
        report(
            "FSIM-ELAB-VITAL-003",
            std::string{name}
                + " has an unknown, duplicate, or misplaced argument",
            expression.operands[index].span);
        return std::nullopt;
      }
      if (argument_name.empty()) {
        ++positional;
      }
    }
    if (expected_width != 1 || select_expression == nullptr
        || (name == "vitalmux2"
                ? data0_expression == nullptr || data1_expression == nullptr
                : data_expression == nullptr)) {
      report(
          "FSIM-ELAB-VITAL-003",
          std::string{name} + " is missing a required data or select input",
          expression.span);
      return std::nullopt;
    }
    std::size_t data_width = 2;
    std::size_t select_width = 1;
    std::optional<RegisterId> data;
    if (name == "vitalmux2") {
      auto data1 = lower_expression(*data1_expression, 1);
      auto data0 = lower_expression(*data0_expression, 1);
      if (!data1 || !data0) {
        return std::nullopt;
      }
      const auto packed = allocate_register(
          2, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Concatenate{
          packed, {*data1, *data0}, 2});
      data = packed;
    } else {
      const auto inferred_data_width = infer_width(*data_expression);
      const auto inferred_select_width = infer_width(*select_expression);
      if (!inferred_data_width || !inferred_select_width
          || *inferred_data_width == 0 || *inferred_select_width == 0) {
        report(
            "FSIM-ELAB-VITAL-007",
            std::string{name} + " requires concrete non-null vectors",
            expression.span);
        return std::nullopt;
      }
      data_width = *inferred_data_width;
      select_width = *inferred_select_width;
      data = lower_expression(*data_expression, data_width);
    }
    auto selector = lower_expression(
        *select_expression, select_width);
    const auto promote_vector = [&](std::optional<RegisterId>& value,
                                    const std::size_t width) {
      if (value && register_domain(*value)
              != frontend::ValueDomain::Logic9
          && (register_domain(*value) == frontend::ValueDomain::Bit2
              || register_domain(*value)
                  == frontend::ValueDomain::Logic4)) {
        const auto promoted = allocate_register(
            width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister{promoted, *value});
        value = promoted;
      }
    };
    promote_vector(data, data_width);
    promote_vector(selector, select_width);
    if (!data || !selector
        || register_domain(*data) != frontend::ValueDomain::Logic9
        || register_domain(*selector) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-004",
          std::string{name} + " requires standard-logic data and select inputs",
          expression.span);
      return std::nullopt;
    }
    if (select_width >= std::numeric_limits<std::size_t>::digits
        || data_width > (std::size_t{1} << select_width)) {
      report(
          "FSIM-ELAB-VITAL-007",
          std::string{name} + " data width exceeds its select space",
          expression.span);
      return std::nullopt;
    }
    const auto candidate_count = std::size_t{1} << select_width;
    std::vector<RegisterId> candidates;
    candidates.reserve(candidate_count);
    for (std::size_t index = 0; index < candidate_count; ++index) {
      if (index >= data_width) {
        candidates.push_back(make_state(runtime::Logic9::x));
        continue;
      }
      const auto bit = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Extract{
          bit, *data, static_cast<std::uint32_t>(index), 1});
      candidates.push_back(normalize_ux01(bit));
    }
    for (std::size_t bit = 0; bit < select_width; ++bit) {
      const auto select_bit = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Extract{
          select_bit, *selector, static_cast<std::uint32_t>(bit), 1});
      const auto select_zero = either(
          state_matches(select_bit, runtime::Logic9::zero),
          state_matches(select_bit, runtime::Logic9::l));
      const auto select_one = either(
          state_matches(select_bit, runtime::Logic9::one),
          state_matches(select_bit, runtime::Logic9::h));
      std::vector<RegisterId> next;
      next.reserve(candidates.size() / 2U);
      for (std::size_t index = 0; index < candidates.size(); index += 2U) {
        const auto equal = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary{
            BinaryOperator::case_equal, equal,
            candidates[index], candidates[index + 1U]});
        const auto merged = select(
            equal, candidates[index], make_state(runtime::Logic9::x),
            1, frontend::ValueDomain::Logic9);
        next.push_back(select(
            select_zero, candidates[index],
            select(select_one, candidates[index + 1U], merged, 1,
                   frontend::ValueDomain::Logic9),
            1, frontend::ValueDomain::Logic9));
      }
      candidates = std::move(next);
    }
    return apply_result_map(candidates.front(), result_map);
  }

  const bool decoder = name == "vitaldecoder" || name == "vitaldecoder2"
      || name == "vitaldecoder4" || name == "vitaldecoder8";
  if (decoder) {
    std::array<const Expression*, 2> arguments{};
    const Expression* result_map = nullptr;
    std::size_t positional = 0;
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      const auto argument_name = expression.call_argument_names.empty()
          ? std::string_view{}
          : std::string_view{expression.call_argument_names[index]};
      std::optional<std::size_t> target;
      if (argument_name == "data") {
        target = 0;
      } else if (argument_name == "enable") {
        target = 1;
      } else if (argument_name == "resultmap") {
        if (result_map == nullptr) {
          result_map = &expression.operands[index];
          continue;
        }
      } else if (argument_name.empty() && positional < arguments.size()) {
        target = positional++;
      } else if (argument_name.empty() && result_map == nullptr) {
        result_map = &expression.operands[index];
        continue;
      }
      if (!target || arguments[*target] != nullptr) {
        report(
            "FSIM-ELAB-VITAL-003",
            std::string{name}
                + " has an unknown, duplicate, or misplaced argument",
            expression.operands[index].span);
        return std::nullopt;
      }
      arguments[*target] = &expression.operands[index];
    }
    if (std::ranges::any_of(arguments, [](const auto* value) {
          return value == nullptr;
        }) || expected_width < 2
        || (expected_width & (expected_width - 1U)) != 0) {
      report(
          "FSIM-ELAB-VITAL-007",
          std::string{name}
              + " requires Data, Enable, and a power-of-two result width",
          expression.span);
      return std::nullopt;
    }
    const auto data_width = std::bit_width(expected_width) - 1U;
    const auto inferred_data_width = infer_width(*arguments[0]);
    if (!inferred_data_width || *inferred_data_width != data_width) {
      report(
          "FSIM-ELAB-VITAL-007",
          std::string{name} + " data width does not match its result width",
          arguments[0]->span);
      return std::nullopt;
    }
    auto data = lower_expression(*arguments[0], data_width);
    auto enable = lower_expression(*arguments[1], 1);
    if (data && register_domain(*data)
            != frontend::ValueDomain::Logic9
        && (register_domain(*data) == frontend::ValueDomain::Bit2
            || register_domain(*data)
                == frontend::ValueDomain::Logic4)) {
      const auto promoted = allocate_register(
          data_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(CopyRegister{promoted, *data});
      data = promoted;
    }
    if (!data || !enable
        || register_domain(*data) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-004",
          std::string{name} + " requires standard-logic inputs",
          expression.span);
      return std::nullopt;
    }
    if (register_domain(*enable) != frontend::ValueDomain::Logic9) {
      const auto promoted = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(CopyRegister{promoted, *enable});
      enable = promoted;
    }
    std::vector<RegisterId> outputs(expected_width);
    for (std::size_t output = 0; output < expected_width; ++output) {
      auto value = normalize_ux01(*enable);
      for (std::size_t bit = 0; bit < data_width; ++bit) {
        const auto source_bit = allocate_register(
            1, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Extract{
            source_bit, *data, static_cast<std::uint32_t>(bit), 1});
        auto selected_bit = normalize_ux01(source_bit);
        if (((output >> bit) & 1U) == 0) {
          const auto inverted = allocate_register(
              1, frontend::ValueDomain::Logic9);
          process_.operations.emplace_back(UnaryNot{
              inverted, selected_bit});
          selected_bit = inverted;
        }
        const auto combined = allocate_register(
            1, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Binary{
            BinaryOperator::bit_and, combined, value, selected_bit});
        value = combined;
      }
      const auto mapped = apply_result_map(value, result_map);
      if (!mapped) {
        return std::nullopt;
      }
      outputs[output] = *mapped;
    }
    std::ranges::reverse(outputs);
    const auto result = allocate_register(
        expected_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Concatenate{
        result, std::move(outputs), static_cast<std::uint32_t>(expected_width)});
    return result;
  }

  const bool tri_state = name == "vitalbufif0" || name == "vitalbufif1"
      || name == "vitalinvif0" || name == "vitalinvif1";
  if (tri_state) {
    std::array<const Expression*, 2> arguments{};
    const Expression* result_map = nullptr;
    std::size_t positional = 0;
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      const auto argument_name = expression.call_argument_names.empty()
          ? std::string_view{}
          : std::string_view{expression.call_argument_names[index]};
      std::optional<std::size_t> target;
      if (argument_name == "data") {
        target = 0;
      } else if (argument_name == "enable") {
        target = 1;
      } else if (argument_name == "resultmap") {
        if (result_map != nullptr) {
          target.reset();
        } else {
          result_map = &expression.operands[index];
          continue;
        }
      } else if (argument_name.empty() && positional < arguments.size()) {
        target = positional++;
      } else if (argument_name.empty() && result_map == nullptr) {
        result_map = &expression.operands[index];
        continue;
      }
      if (!target || arguments[*target] != nullptr) {
        report(
            "FSIM-ELAB-VITAL-003",
            std::string{name}
                + " has an unknown, duplicate, or misplaced argument",
            expression.operands[index].span);
        return std::nullopt;
      }
      arguments[*target] = &expression.operands[index];
    }
    if (expected_width != 1
        || std::ranges::any_of(arguments, [](const auto* value) {
             return value == nullptr;
           })) {
      report(
          "FSIM-ELAB-VITAL-003",
          std::string{name} + " requires Data and Enable std_ulogic inputs",
          expression.span);
      return std::nullopt;
    }
    auto data = lower_expression(*arguments[0], 1);
    auto enable = lower_expression(*arguments[1], 1);
    const auto promote_logic = [&](std::optional<RegisterId>& value) {
      if (value && register_domain(*value)
              != frontend::ValueDomain::Logic9
          && (register_domain(*value) == frontend::ValueDomain::Bit2
              || register_domain(*value)
                  == frontend::ValueDomain::Logic4)) {
        const auto promoted = allocate_register(
            1, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister{promoted, *value});
        value = promoted;
      }
    };
    promote_logic(data);
    promote_logic(enable);
    if (!data || !enable
        || register_domain(*data) != frontend::ValueDomain::Logic9
        || register_domain(*enable) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-004",
          std::string{name} + " requires std_ulogic inputs",
          expression.span);
      return std::nullopt;
    }
    auto normalized_data = make_state(runtime::Logic9::x);
    for (const auto& [state, mapped] : {
             std::pair{runtime::Logic9::u, runtime::Logic9::u},
             std::pair{runtime::Logic9::zero, runtime::Logic9::zero},
             std::pair{runtime::Logic9::l, runtime::Logic9::zero},
             std::pair{runtime::Logic9::one, runtime::Logic9::one},
             std::pair{runtime::Logic9::h, runtime::Logic9::one}}) {
      normalized_data = select(
          state_matches(*data, state), make_state(mapped), normalized_data,
          1, frontend::ValueDomain::Logic9);
    }
    if (name.starts_with("vitalinv")) {
      const auto inverted = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(UnaryNot{inverted, normalized_data});
      normalized_data = inverted;
    }
    const auto enabled_low = either(
        state_matches(*enable, runtime::Logic9::zero),
        state_matches(*enable, runtime::Logic9::l));
    const auto enabled_high = either(
        state_matches(*enable, runtime::Logic9::one),
        state_matches(*enable, runtime::Logic9::h));
    const bool active_high = name.ends_with('1');
    auto raw = select(
        active_high ? enabled_high : enabled_low,
        normalized_data,
        select(active_high ? enabled_low : enabled_high,
               make_state(runtime::Logic9::z),
               make_state(runtime::Logic9::x), 1,
               frontend::ValueDomain::Logic9),
        1, frontend::ValueDomain::Logic9);
    if (result_map == nullptr) {
      return raw;
    }
    const auto map = lower_expression(*result_map, 5);
    if (!map || register_domain(*map) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-005",
          std::string{name} + " requires a VitalResultZMapType map",
          result_map->span);
      return std::nullopt;
    }
    auto mapped = make_state(runtime::Logic9::u);
    for (std::size_t ordinal = 0; ordinal < 5; ++ordinal) {
      const auto element = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Extract{
          element, *map, static_cast<std::uint32_t>(4U - ordinal), 1});
      mapped = select(
          state_matches(raw, static_cast<runtime::Logic9>(ordinal)),
          element, mapped, 1, frontend::ValueDomain::Logic9);
    }
    return mapped;
  }

  const bool unary = name == "vitalbuf" || name == "vitalinv"
      || name == "vitalident";
  if (!unary && !is_vital_logic_name(name)) {
    return ExpressionAttempt{};
  }
  if (expected_width != 1) {
    report(
        "FSIM-ELAB-VITAL-002",
        std::string{name} + " returns one std_ulogic value",
        expression.span);
    return std::nullopt;
  }

  std::size_t input_count = unary ? 1U : 0U;
  bool vector_reduction = false;
  if (!unary) {
    const auto suffix = name.back();
    if (suffix >= '2' && suffix <= '4') {
      input_count = static_cast<std::size_t>(suffix - '0');
    } else {
      input_count = 1;
      vector_reduction = true;
    }
  }
  if (expression.operands.size() < input_count
      || expression.operands.size() > input_count + 1U) {
    report(
        "FSIM-ELAB-VITAL-003",
        std::string{name} + " has an unsupported argument count",
        expression.span);
    return std::nullopt;
  }

  std::vector<const Expression*> inputs(input_count);
  const Expression* result_map = nullptr;
  std::size_t positional = 0;
  for (std::size_t index = 0; index < expression.operands.size(); ++index) {
    const auto argument_name = expression.call_argument_names.empty()
        ? std::string_view{}
        : std::string_view{expression.call_argument_names[index]};
    if (argument_name == "resultmap") {
      if (result_map != nullptr) {
        report(
            "FSIM-ELAB-VITAL-003",
            std::string{name} + " has duplicate ResultMap associations",
            expression.operands[index].span);
        return std::nullopt;
      }
      result_map = &expression.operands[index];
      continue;
    }
    std::optional<std::size_t> target;
    if (!argument_name.empty()) {
      if (vector_reduction || unary) {
        if (argument_name == "data") {
          target = 0;
        }
      } else if (argument_name.size() == 1
                 && argument_name.front() >= 'a'
                 && argument_name.front()
                     < static_cast<char>('a' + input_count)) {
        target = static_cast<std::size_t>(argument_name.front() - 'a');
      }
      if (!target) {
        report(
            "FSIM-ELAB-VITAL-003",
            std::string{name} + " has unknown named association '"
                + std::string{argument_name} + "'",
            expression.operands[index].span);
        return std::nullopt;
      }
    } else {
      while (positional < inputs.size() && inputs[positional] != nullptr) {
        ++positional;
      }
      if (positional < inputs.size()) {
        target = positional++;
      } else if (result_map == nullptr) {
        result_map = &expression.operands[index];
        continue;
      }
    }
    if (!target || inputs[*target] != nullptr) {
      report(
          "FSIM-ELAB-VITAL-003",
          std::string{name} + " has duplicate or misplaced associations",
          expression.operands[index].span);
      return std::nullopt;
    }
    inputs[*target] = &expression.operands[index];
  }
  if (std::ranges::any_of(inputs, [](const auto* value) {
        return value == nullptr;
      })) {
    report(
        "FSIM-ELAB-VITAL-003",
        std::string{name} + " is missing a required input",
        expression.span);
    return std::nullopt;
  }

  RegisterId raw{};
  if (vector_reduction) {
    const auto width = infer_width(*inputs.front());
    if (!width) {
      report(
          "FSIM-ELAB-VITAL-004",
          std::string{name}
              + " requires a concrete std_logic_vector input",
          inputs.front()->span);
      return std::nullopt;
    }
    if (*width == 0) {
      const bool identity_one =
          name.find("and") != std::string_view::npos;
      raw = make_state(
          identity_one ? runtime::Logic9::one : runtime::Logic9::zero);
    } else {
    const auto source = lower_expression(*inputs.front(), *width);
    if (!source
        || register_domain(*source) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-004",
          std::string{name} + " requires a standard-logic vector",
          inputs.front()->span);
      return std::nullopt;
    }
    raw = allocate_register(1, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Reduction{
        name.starts_with("vitaland") || name.starts_with("vitalnand")
            ? ReductionOperator::bit_and
            : name.starts_with("vitalor") || name.starts_with("vitalnor")
              ? ReductionOperator::bit_or
              : ReductionOperator::bit_xor,
        raw, *source});
    }
  } else {
    std::vector<RegisterId> values;
    values.reserve(inputs.size());
    for (const auto* input : inputs) {
      auto value = lower_expression(*input, 1);
      if (!value) {
        report(
            "FSIM-ELAB-VITAL-004",
            std::string{name} + " requires std_ulogic inputs",
            input->span);
        return std::nullopt;
      }
      if (register_domain(*value) == frontend::ValueDomain::Bit2) {
        const auto converted = allocate_register(
            1, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister{converted, *value});
        value = converted;
      }
      if (register_domain(*value) != frontend::ValueDomain::Logic9) {
        report(
            "FSIM-ELAB-VITAL-004",
            std::string{name} + " requires std_ulogic inputs",
            input->span);
        return std::nullopt;
      }
      values.push_back(*value);
    }
    raw = values.front();
    const auto operation =
        name.starts_with("vitaland") || name.starts_with("vitalnand")
        ? BinaryOperator::bit_and
        : name.starts_with("vitalor") || name.starts_with("vitalnor")
          ? BinaryOperator::bit_or
          : BinaryOperator::bit_xor;
    for (std::size_t index = 1; index < values.size(); ++index) {
      const auto combined = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Binary{
          operation, combined, raw, values[index]});
      raw = combined;
    }
  }
  const bool invert = name == "vitalinv" || name.starts_with("vitalnand")
      || name.starts_with("vitalnor") || name.starts_with("vitalxnor");
  if (invert) {
    const auto inverted = allocate_register(
        1, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(UnaryNot{inverted, raw});
    raw = inverted;
  }
  const auto state_constant = [&](const runtime::Logic9 state) {
    const auto result = allocate_register(1, frontend::ValueDomain::Logic9);
    runtime::PackedLogic4 value(1);
    value.fill(state);
    process_.operations.emplace_back(LoadConstant{result, std::move(value)});
    return result;
  };
  const auto exact_state = [&](const runtime::Logic9 state) {
    const auto match = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, match, raw, state_constant(state)});
    return match;
  };
  if (name != "vitalident") {
    auto normalized = state_constant(runtime::Logic9::x);
    for (const auto& [state, mapped_state] : {
             std::pair{runtime::Logic9::u, runtime::Logic9::u},
             std::pair{runtime::Logic9::zero, runtime::Logic9::zero},
             std::pair{runtime::Logic9::l, runtime::Logic9::zero},
             std::pair{runtime::Logic9::one, runtime::Logic9::one},
             std::pair{runtime::Logic9::h, runtime::Logic9::one}}) {
      const auto selected = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(ConditionalSelect{
          selected, exact_state(state), state_constant(mapped_state),
          normalized});
      normalized = selected;
    }
    raw = normalized;
  }
  if (result_map == nullptr) {
    return raw;
  }
  const auto map_width = name == "vitalident" ? 9U : 4U;
  const auto map = lower_expression(*result_map, map_width);
  if (!map || register_domain(*map) != frontend::ValueDomain::Logic9) {
    report(
        "FSIM-ELAB-VITAL-005",
        std::string{name} + " requires a VitalResultMapType map",
        result_map->span);
    return std::nullopt;
  }
  const auto map_value = [&](const std::size_t ordinal) {
    const auto result = allocate_register(1, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Extract{
        result, *map,
        static_cast<std::uint32_t>(map_width - 1U - ordinal), 1});
    return result;
  };
  auto mapped = map_value(0);
  for (std::size_t ordinal = 1; ordinal < map_width; ++ordinal) {
    const auto literal = state_constant(static_cast<runtime::Logic9>(ordinal));
    const auto match = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, match, raw, literal});
    const auto selected = allocate_register(
        1, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(ConditionalSelect{
        selected, match, map_value(ordinal), mapped});
    mapped = selected;
  }
  return mapped;
}

}  // namespace fsim::elaboration
