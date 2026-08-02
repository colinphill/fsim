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

}  // namespace

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_logic_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call) {
    return ExpressionAttempt{};
  }
  const auto name = simple_name(expression.text);
  const bool bit_conversion = name == "to_bit" || name == "to_bitvector"
      || name == "to_bit_vector" || name == "to_bv";
  const bool logic_conversion = name == "to_stdulogic"
      || name == "to_stdlogicvector" || name == "to_std_logic_vector"
      || name == "to_slv" || name == "to_stdulogicvector"
      || name == "to_std_ulogic_vector" || name == "to_sulv";
  const bool mapping = name == "to_01" || name == "to_x01"
      || name == "to_x01z" || name == "to_ux01";
  const bool predicate = name == "is_x";
  if (!bit_conversion && !logic_conversion && !mapping && !predicate) {
    return ExpressionAttempt{};
  }
  const auto minimum_arity = bit_conversion || name == "to_01" ? 1U : 1U;
  const auto maximum_arity = bit_conversion || name == "to_01" ? 2U : 1U;
  if (expression.operands.size() < minimum_arity
      || expression.operands.size() > maximum_arity) {
    report(
        "FSIM-ELAB-VHLOGIC-001",
        std::string{name} + " has an unsupported argument count",
        expression.span);
    return std::nullopt;
  }
  const auto source_width = infer_width(expression.operands.front());
  if (!source_width || *source_width == 0 || *source_width > 64) {
    report(
        "FSIM-ELAB-VHLOGIC-002",
        std::string{name} + " requires a packed operand of width 1 through 64",
        expression.operands.front().span);
    return std::nullopt;
  }
  if ((name == "to_bit" || name == "to_stdulogic")
      && *source_width != 1) {
    report(
        "FSIM-ELAB-VHLOGIC-002",
        std::string{name} + " requires a scalar operand",
        expression.operands.front().span);
    return std::nullopt;
  }
  if (!predicate && expected_width != *source_width) {
    report(
        "FSIM-ELAB-VHLOGIC-002",
        std::string{name} + " result width does not match its context",
        expression.span);
    return std::nullopt;
  }
  auto source = lower_expression(
      expression.operands.front(), *source_width);
  if (!source) {
    return std::nullopt;
  }

  if (logic_conversion) {
    if (register_domain(*source) != frontend::ValueDomain::Bit2
        && register_domain(*source) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VHLOGIC-002",
          std::string{name} + " requires a bit or standard-logic operand",
          expression.operands.front().span);
      return std::nullopt;
    }
    if (register_domain(*source) == frontend::ValueDomain::Logic9) {
      return *source;
    }
    const auto result = allocate_register(
        *source_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(CopyRegister{result, *source});
    return result;
  }
  if (register_domain(*source) != frontend::ValueDomain::Logic9) {
    report(
        "FSIM-ELAB-VHLOGIC-002",
        std::string{name} + " requires a standard-logic operand",
        expression.operands.front().span);
    return std::nullopt;
  }

  const auto constant = [&](const frontend::ValueDomain domain,
                            const runtime::Logic9 state) {
    const auto result = allocate_register(1, domain);
    if (domain == frontend::ValueDomain::Logic9) {
      runtime::PackedLogic4 value(1);
      value.fill(state);
      process_.operations.emplace_back(LoadConstant{
          result, std::move(value)});
    } else {
      const auto known = state == runtime::Logic9::one;
      process_.operations.emplace_back(LoadConstant{
          result, unsigned_value(known ? 1 : 0, 1)});
    }
    return result;
  };
  const auto exact = [&](const RegisterId scalar,
                         const runtime::Logic9 state) {
    const auto literal = constant(frontend::ValueDomain::Logic9, state);
    const auto result = allocate_register(1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, result, scalar, literal});
    return result;
  };
  const auto scalar_at = [&](const std::size_t bit) {
    if (*source_width == 1) {
      return *source;
    }
    const auto result = allocate_register(1, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Extract{
        result, *source, static_cast<std::uint32_t>(bit), 1});
    return result;
  };

  if (predicate) {
    auto any_unknown = constant(
        frontend::ValueDomain::Boolean, runtime::Logic9::zero);
    for (std::size_t bit = 0; bit < *source_width; ++bit) {
      const auto scalar = scalar_at(bit);
      auto known = exact(scalar, runtime::Logic9::zero);
      for (const auto state : {
               runtime::Logic9::one,
               runtime::Logic9::l,
               runtime::Logic9::h}) {
        const auto match = exact(scalar, state);
        const auto combined = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary{
            BinaryOperator::bit_or, combined, known, match});
        known = combined;
      }
      const auto unknown = allocate_register(
          1, frontend::ValueDomain::Boolean);
      process_.operations.emplace_back(LogicalNot{unknown, known});
      const auto combined = allocate_register(
          1, frontend::ValueDomain::Boolean);
      process_.operations.emplace_back(Binary{
          BinaryOperator::bit_or, combined, any_unknown, unknown});
      any_unknown = combined;
    }
    return any_unknown;
  }

  runtime::Logic9 xmap = runtime::Logic9::zero;
  if (expression.operands.size() == 2) {
    const auto value = literal_value(
        expression.operands[1], 1, frontend::Language::Vhdl2008);
    if (!value || value->value.is_logic9()
        || (value->value.get(0) != runtime::Logic4::zero
            && value->value.get(0) != runtime::Logic4::one)) {
      report(
          "FSIM-ELAB-VHLOGIC-003",
          std::string{name} + " supports only a static bit xmap",
          expression.operands[1].span);
      return std::nullopt;
    }
    xmap = value->value.get(0) == runtime::Logic4::one
        ? runtime::Logic9::one : runtime::Logic9::zero;
  }

  std::array<runtime::Logic9, 9> table{};
  if (bit_conversion || name == "to_01") {
    table.fill(xmap);
  } else {
    table.fill(runtime::Logic9::x);
    if (name == "to_x01z") {
      table[static_cast<std::size_t>(runtime::Logic9::z)] =
          runtime::Logic9::z;
    } else if (name == "to_ux01") {
      table[static_cast<std::size_t>(runtime::Logic9::u)] =
          runtime::Logic9::u;
    }
  }
  table[static_cast<std::size_t>(runtime::Logic9::zero)] =
      runtime::Logic9::zero;
  table[static_cast<std::size_t>(runtime::Logic9::l)] =
      runtime::Logic9::zero;
  table[static_cast<std::size_t>(runtime::Logic9::one)] =
      runtime::Logic9::one;
  table[static_cast<std::size_t>(runtime::Logic9::h)] =
      runtime::Logic9::one;
  const auto result_domain = bit_conversion
      ? frontend::ValueDomain::Bit2
      : expected_type != nullptr
            && expected_type->domain == frontend::ValueDomain::Logic9
          ? expected_type->domain : frontend::ValueDomain::Logic9;
  std::vector<RegisterId> bits(*source_width);
  for (std::size_t bit = 0; bit < *source_width; ++bit) {
    const auto scalar = scalar_at(bit);
    auto mapped = constant(result_domain, table.front());
    for (std::size_t state = 1; state < table.size(); ++state) {
      const auto condition = exact(
          scalar, static_cast<runtime::Logic9>(state));
      const auto value = constant(result_domain, table[state]);
      const auto selected = allocate_register(1, result_domain);
      process_.operations.emplace_back(ConditionalSelect{
          selected, condition, value, mapped});
      mapped = selected;
    }
    bits[*source_width - bit - 1] = mapped;
  }
  if (*source_width == 1) {
    return bits.front();
  }
  const auto result = allocate_register(*source_width, result_domain);
  process_.operations.emplace_back(Concatenate{
      result, std::move(bits), static_cast<std::uint32_t>(*source_width)});
  return result;
}

}  // namespace fsim::elaboration
