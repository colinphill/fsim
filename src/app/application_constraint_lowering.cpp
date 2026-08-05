// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <bit>
#include <charconv>

namespace fsim::app::application_detail {
namespace {

namespace sv = semantic::sv;
using ExpressionId = runtime::SystemVerilogConstraintExpressionId;
using Operator = runtime::SystemVerilogConstraintExpressionOperator;

[[nodiscard]] std::optional<std::uint64_t> decimal(
    const std::string_view text) {
  std::uint64_t value{};
  const auto parsed = std::from_chars(
      text.data(), text.data() + text.size(), value, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] runtime::PackedLogic4 low_value(
    const std::size_t width,
    const std::uint64_t value) {
  if (width <= 64U) {
    return runtime::PackedLogic4::from_aval_bval(width, value, 0);
  }
  runtime::PackedLogic4 result(width, runtime::Logic4::zero);
  for (std::size_t index = 0; index < 64U; ++index) {
    if ((value & (UINT64_C(1) << index)) != 0) {
      result.set(index, runtime::Logic4::one);
    }
  }
  return result;
}

[[nodiscard]] std::optional<std::pair<
    runtime::PackedLogic4,
    runtime::SystemVerilogConstraintVariableProfile>>
literal(const sv::ConstraintExpression& expression) {
  if (expression.kind == sv::ConstraintExpressionKind::boolean_literal) {
    const auto true_value = expression.text == "true"
        || expression.text == "1";
    return std::pair{
        runtime::PackedLogic4(
            1, true_value ? runtime::Logic4::one : runtime::Logic4::zero),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            1, false, "bit", false}};
  }
  if (expression.kind == sv::ConstraintExpressionKind::integer_literal) {
    const auto value = decimal(expression.text);
    if (!value) return std::nullopt;
    const auto width = std::max<std::size_t>(
        32U, static_cast<std::size_t>(std::bit_width(*value)) + 1U);
    return std::pair{
        low_value(width, *value),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::Integer,
            width, true, "integer", true}};
  }
  if (expression.kind != sv::ConstraintExpressionKind::logic_literal) {
    return std::nullopt;
  }
  auto spelling = expression.text;
  spelling.erase(std::remove(spelling.begin(), spelling.end(), '_'),
                 spelling.end());
  const auto apostrophe = spelling.find('\'');
  if (apostrophe == std::string::npos) return std::nullopt;
  auto suffix = std::string_view{spelling}.substr(apostrophe + 1U);
  if (apostrophe == 0U && suffix.size() == 1U) {
    auto digit = static_cast<char>(
        std::toupper(static_cast<unsigned char>(suffix.front())));
    if (digit != '0' && digit != '1' && digit != 'X' && digit != 'Z') {
      return std::nullopt;
    }
    return std::pair{
        runtime::PackedLogic4::from_msb_string(std::string{digit}),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            1, false, "logic", true}};
  }
  const auto width_value = decimal(
      std::string_view{spelling}.substr(0, apostrophe));
  if (!width_value || *width_value == 0
      || *width_value > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  bool signed_literal = false;
  if (!suffix.empty() && (suffix.front() == 's' || suffix.front() == 'S')) {
    signed_literal = true;
    suffix.remove_prefix(1U);
  }
  if (suffix.size() < 2U) return std::nullopt;
  const auto base = static_cast<char>(
      std::tolower(static_cast<unsigned char>(suffix.front())));
  suffix.remove_prefix(1U);
  if (base == 'd') {
    const auto value = decimal(suffix);
    if (!value) return std::nullopt;
    return std::pair{
        low_value(static_cast<std::size_t>(*width_value), *value),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            static_cast<std::size_t>(*width_value),
            signed_literal,
            signed_literal ? "logic signed" : "logic",
            true}};
  }
  const auto digit_width = base == 'b' ? 1U : base == 'o' ? 3U
      : base == 'h' ? 4U : 0U;
  if (digit_width == 0U) return std::nullopt;
  std::string bits;
  bits.reserve(suffix.size() * digit_width);
  constexpr std::string_view binary_digits{
      "0000000100100011010001010110011110001001101010111100110111101111"};
  for (const auto source_digit : suffix) {
    const auto digit = static_cast<char>(
        std::toupper(static_cast<unsigned char>(source_digit)));
    if (digit == 'X' || digit == 'Z') {
      bits.append(digit_width, digit);
      continue;
    }
    const auto numeric = digit >= '0' && digit <= '9'
        ? static_cast<unsigned>(digit - '0')
        : digit >= 'A' && digit <= 'F'
            ? static_cast<unsigned>(digit - 'A' + 10) : 16U;
    const auto limit = 1U << digit_width;
    if (numeric >= limit) return std::nullopt;
    bits.append(binary_digits.substr(numeric * 4U + 4U - digit_width,
                                     digit_width));
  }
  if (bits.size() > *width_value) {
    bits.erase(0, bits.size() - static_cast<std::size_t>(*width_value));
  } else if (bits.size() < *width_value) {
    bits.insert(0, static_cast<std::size_t>(*width_value) - bits.size(), '0');
  }
  try {
    return std::pair{
        runtime::PackedLogic4::from_msb_string(bits),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            static_cast<std::size_t>(*width_value),
            signed_literal,
            signed_literal ? "logic signed" : "logic",
            true}};
  } catch (const std::invalid_argument&) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<Operator> unary_operator(
    const std::string_view spelling) noexcept {
  if (spelling == "+") return Operator::UnaryPlus;
  if (spelling == "-") return Operator::UnaryMinus;
  if (spelling == "~") return Operator::BitwiseNot;
  if (spelling == "!") return Operator::LogicalNot;
  return std::nullopt;
}

[[nodiscard]] std::optional<Operator> binary_operator(
    const std::string_view spelling) noexcept {
  if (spelling == "+") return Operator::Add;
  if (spelling == "-") return Operator::Subtract;
  if (spelling == "*") return Operator::Multiply;
  if (spelling == "**") return Operator::Power;
  if (spelling == "/") return Operator::Divide;
  if (spelling == "%") return Operator::Modulo;
  if (spelling == "&") return Operator::BitwiseAnd;
  if (spelling == "|") return Operator::BitwiseOr;
  if (spelling == "^") return Operator::BitwiseXor;
  if (spelling == "==") return Operator::Equal;
  if (spelling == "!=") return Operator::NotEqual;
  if (spelling == "===") return Operator::CaseEqual;
  if (spelling == "!==") return Operator::CaseNotEqual;
  if (spelling == "<") return Operator::Less;
  if (spelling == "<=") return Operator::LessEqual;
  if (spelling == ">") return Operator::Greater;
  if (spelling == ">=") return Operator::GreaterEqual;
  if (spelling == "&&") return Operator::LogicalAnd;
  if (spelling == "||") return Operator::LogicalOr;
  return std::nullopt;
}

class Lowerer final {
 public:
  Lowerer(
      const std::string_view specialization,
      const std::map<std::string,
                     runtime::SystemVerilogConstraintVariableId>& variables,
      const std::map<std::string, std::vector<
          runtime::SystemVerilogConstraintVariableId>>& container_variables,
      std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies,
      std::string& error)
      : specialization_(specialization), variables_(variables),
        container_variables_(container_variables),
        dependencies_(dependencies), error_(error) {}

  [[nodiscard]] std::optional<ExpressionId> lower(
      const sv::ConstraintExpression& expression) {
    if (const auto retained_literal = literal(expression)) {
      return builder_.constant(
          retained_literal->first, retained_literal->second);
    }
    if (expression.kind == sv::ConstraintExpressionKind::name) {
      if (foreach_element_ && expression.text == foreach_iterator_) {
        return builder_.constant(
            low_value(32, foreach_index_),
            runtime::SystemVerilogConstraintVariableProfile{
                runtime::SystemVerilogConstraintDomainKind::Integer,
                32, true, "integer", true});
      }
      const auto binding = std::ranges::find(
          expression.bindings, specialization_,
          &sv::ConstraintBinding::specialization_identity);
      if (binding == expression.bindings.end()) {
        error_ = "constraint name has no binding for specialization '"
            + std::string{specialization_} + "'";
        return std::nullopt;
      }
      if (!binding->constant_value.empty()) {
        const auto value = decimal(binding->constant_value);
        if (!value) {
          error_ = "constraint parameter value is not a bounded integer";
          return std::nullopt;
        }
        const auto profile = systemverilog_constraint_profile(*binding);
        return builder_.constant(
            low_value(profile.width, *value), profile);
      }
      const auto variable = variables_.find(binding->canonical_identity);
      if (variable == variables_.end()) {
        error_ = "constraint property '" + binding->canonical_identity
            + "' has no solver variable";
        return std::nullopt;
      }
      if (std::ranges::find(dependencies_, variable->second)
          == dependencies_.end()) {
        dependencies_.push_back(variable->second);
      }
      return builder_.variable(
          variable->second, systemverilog_constraint_profile(*binding));
    }
    if (expression.kind == sv::ConstraintExpressionKind::index
        && expression.operands.size() == 2U
        && expression.operands[0].kind
            == sv::ConstraintExpressionKind::name) {
      const auto& base = expression.operands[0];
      const auto binding = std::ranges::find(
          base.bindings, specialization_,
          &sv::ConstraintBinding::specialization_identity);
      if (binding == base.bindings.end()) {
        error_ = "constraint element selection has no exact binding";
        return std::nullopt;
      }
      const auto elements = container_variables_.find(
          binding->canonical_identity);
      if (elements == container_variables_.end()) {
        error_ = "constraint container '" + binding->canonical_identity
            + "' has no element solver variables";
        return std::nullopt;
      }
      std::optional<std::size_t> index;
      if (foreach_element_
          && expression.operands[1].text == foreach_iterator_) {
        index = foreach_index_;
      } else if (const auto retained = literal(expression.operands[1]);
                 retained && retained->first.width() <= 64U
                 && !std::ranges::any_of(
                     retained->first.bval_words(), [](const auto word) {
                       return word != 0;
                     })) {
        index = static_cast<std::size_t>(retained->first.low_word().aval);
      }
      if (!index || *index >= elements->second.size()) {
        error_ = "constraint container index is outside its materialized bounds";
        return std::nullopt;
      }
      const auto element = elements->second[*index];
      if (std::ranges::find(dependencies_, element)
          == dependencies_.end()) {
        dependencies_.push_back(element);
      }
      return builder_.variable(
          element, systemverilog_constraint_profile(*binding));
    }
    if (expression.kind == sv::ConstraintExpressionKind::soft
        && expression.operands.size() == 1U) {
      return lower(expression.operands.front());
    }
    if (expression.kind == sv::ConstraintExpressionKind::inside_set
        && expression.operands.size() >= 2U) {
      const auto subject = lower(expression.operands.front());
      if (!subject) return std::nullopt;
      std::optional<ExpressionId> membership;
      for (std::size_t index = 1; index < expression.operands.size(); ++index) {
        const auto& item = expression.operands[index];
        std::optional<ExpressionId> selected;
        if (item.kind == sv::ConstraintExpressionKind::inside_range
            && item.operands.size() == 2U) {
          const auto low = lower(item.operands[0]);
          const auto high = lower(item.operands[1]);
          if (!low || !high) return std::nullopt;
          const auto above_low = builder_.binary(
              Operator::GreaterEqual, *subject, *low);
          const auto below_high = builder_.binary(
              Operator::LessEqual, *subject, *high);
          selected = builder_.binary(
              Operator::LogicalAnd, above_low, below_high);
        } else {
          const auto choice = lower(item);
          if (!choice) return std::nullopt;
          selected = builder_.binary(
              Operator::Equal, *subject, *choice);
        }
        membership = membership
            ? std::optional{builder_.binary(
                Operator::LogicalOr, *membership, *selected)}
            : selected;
      }
      return membership;
    }
    if (expression.kind == sv::ConstraintExpressionKind::foreach_constraint
        && expression.operands.size() == 2U) {
      const auto& selection = expression.operands[0];
      if (selection.kind != sv::ConstraintExpressionKind::index
          || selection.operands.size() != 2U
          || selection.operands[0].kind
              != sv::ConstraintExpressionKind::name
          || selection.operands[1].kind
              != sv::ConstraintExpressionKind::name) {
        error_ = "constraint foreach requires one named container index";
        return std::nullopt;
      }
      const auto binding = std::ranges::find(
          selection.operands[0].bindings, specialization_,
          &sv::ConstraintBinding::specialization_identity);
      if (binding == selection.operands[0].bindings.end()) {
        error_ = "constraint foreach container has no exact binding";
        return std::nullopt;
      }
      const auto elements = container_variables_.find(
          binding->canonical_identity);
      if (elements == container_variables_.end()) {
        error_ = "constraint foreach container has no materialized elements";
        return std::nullopt;
      }
      const auto saved_iterator = foreach_iterator_;
      const auto saved_index = foreach_index_;
      const auto saved_element = foreach_element_;
      foreach_iterator_ = selection.operands[1].text;
      std::vector<ExpressionId> predicates;
      predicates.reserve(elements->second.size());
      for (std::size_t index = 0; index < elements->second.size(); ++index) {
        foreach_index_ = index;
        foreach_element_ = elements->second[index];
        const auto predicate = lower(expression.operands[1]);
        if (!predicate) return std::nullopt;
        predicates.push_back(*predicate);
      }
      foreach_iterator_ = saved_iterator;
      foreach_index_ = saved_index;
      foreach_element_ = saved_element;
      return builder_.conjunction(predicates);
    }
    std::vector<ExpressionId> operands;
    operands.reserve(expression.operands.size());
    for (const auto& operand : expression.operands) {
      const auto lowered = lower(operand);
      if (!lowered) return std::nullopt;
      operands.push_back(*lowered);
    }
    if (expression.kind == sv::ConstraintExpressionKind::unary
        && operands.size() == 1U) {
      const auto operation = unary_operator(expression.text);
      if (operation) return builder_.unary(*operation, operands[0]);
    }
    if (expression.kind == sv::ConstraintExpressionKind::binary
        && operands.size() == 2U) {
      const auto operation = binary_operator(expression.text);
      if (operation) {
        return builder_.binary(*operation, operands[0], operands[1]);
      }
    }
    if (expression.kind == sv::ConstraintExpressionKind::conditional
        && operands.size() == 3U) {
      return builder_.conditional(operands[0], operands[1], operands[2]);
    }
    if (expression.kind == sv::ConstraintExpressionKind::constraint_block) {
      return builder_.conjunction(operands);
    }
    if (expression.kind == sv::ConstraintExpressionKind::implication
        && operands.size() == 2U) {
      return builder_.implication(operands[0], operands[1]);
    }
    if (expression.kind
            == sv::ConstraintExpressionKind::conditional_constraint
        && (operands.size() == 2U || operands.size() == 3U)) {
      return builder_.conditional_constraint(
          operands[0], operands[1],
          operands.size() == 3U
              ? std::optional{operands[2]} : std::nullopt);
    }
    error_ = "unsupported constraint expression operator '"
        + expression.text + "'";
    return std::nullopt;
  }

  [[nodiscard]] runtime::SystemVerilogConstraintExpression finish(
      const ExpressionId root) {
    return std::move(builder_).finish(root);
  }

 private:
  std::string_view specialization_;
  const std::map<std::string,
                 runtime::SystemVerilogConstraintVariableId>& variables_;
  const std::map<std::string, std::vector<
      runtime::SystemVerilogConstraintVariableId>>& container_variables_;
  std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies_;
  std::string& error_;
  std::string foreach_iterator_;
  std::size_t foreach_index_{};
  std::optional<runtime::SystemVerilogConstraintVariableId> foreach_element_;
  runtime::SystemVerilogConstraintExpressionBuilder builder_;
};

}  // namespace

runtime::SystemVerilogConstraintVariableProfile
systemverilog_constraint_profile(
    const semantic::sv::ConstraintBinding& binding) {
  runtime::SystemVerilogConstraintVariableProfile profile;
  profile.kind = binding.type.value_form
          == semantic::sv::TypeForm::enumeration
      ? runtime::SystemVerilogConstraintDomainKind::Enumeration
      : binding.type.target.spelling.find("int") != std::string::npos
          ? runtime::SystemVerilogConstraintDomainKind::Integer
          : runtime::SystemVerilogConstraintDomainKind::BitVector;
  profile.width = static_cast<std::size_t>(
      binding.type.executable_width.value_or(
          profile.kind == runtime::SystemVerilogConstraintDomainKind::Integer
              ? 32U : 1U));
  profile.signed_value = binding.type.signed_value;
  profile.nominal_type = binding.type.target.spelling.empty()
      ? "$constraint-value" : binding.type.target.spelling;
  profile.four_state = binding.type.four_state;
  return profile;
}

std::optional<runtime::SystemVerilogConstraintExpression>
lower_systemverilog_constraint_expression(
    const semantic::sv::ConstraintExpression& expression,
    const std::string_view specialization_identity,
    const std::map<std::string,
                   runtime::SystemVerilogConstraintVariableId>& variables,
    std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies,
    std::string& error,
    const std::map<std::string, std::vector<
        runtime::SystemVerilogConstraintVariableId>>& container_variables) {
  dependencies.clear();
  error.clear();
  Lowerer lowerer{
      specialization_identity, variables, container_variables,
      dependencies, error};
  const auto root = lowerer.lower(expression);
  if (!root) {
    dependencies.clear();
    return std::nullopt;
  }
  return lowerer.finish(*root);
}

std::optional<runtime::SystemVerilogConstraintDistribution>
lower_systemverilog_constraint_distribution(
    const semantic::sv::ConstraintExpression& expression,
    const std::string_view specialization_identity,
    const std::map<std::string,
                   runtime::SystemVerilogConstraintVariableId>& variables,
    const std::span<const runtime::SystemVerilogConstraintVariable>
        solver_variables,
    std::string canonical_identity,
    std::string& error) {
  error.clear();
  if (expression.kind != semantic::sv::ConstraintExpressionKind::distribution
      || expression.operands.size() < 2U
      || expression.operands.front().kind
          != semantic::sv::ConstraintExpressionKind::name) {
    error = "a dist constraint requires a bound property and weighted items";
    return std::nullopt;
  }
  const auto& subject = expression.operands.front();
  const auto binding = std::ranges::find(
      subject.bindings, specialization_identity,
      &semantic::sv::ConstraintBinding::specialization_identity);
  if (binding == subject.bindings.end()) {
    error = "dist property has no exact specialization binding";
    return std::nullopt;
  }
  const auto variable = variables.find(binding->canonical_identity);
  if (variable == variables.end() || variable->second >= solver_variables.size()) {
    error = "dist property has no solver variable";
    return std::nullopt;
  }
  const auto& profile = solver_variables[variable->second].profile;
  const auto retained_constant = [&](
      const semantic::sv::ConstraintExpression& item)
      -> std::optional<std::pair<
          runtime::PackedLogic4,
          runtime::SystemVerilogConstraintVariableProfile>> {
    if (const auto retained = literal(item)) return retained;
    if (item.kind != semantic::sv::ConstraintExpressionKind::name) {
      return std::nullopt;
    }
    const auto item_binding = std::ranges::find(
        item.bindings, specialization_identity,
        &semantic::sv::ConstraintBinding::specialization_identity);
    if (item_binding == item.bindings.end()
        || item_binding->constant_value.empty()) {
      return std::nullopt;
    }
    const auto value = decimal(item_binding->constant_value);
    if (!value) return std::nullopt;
    auto item_profile = systemverilog_constraint_profile(*item_binding);
    return std::pair{
        low_value(item_profile.width, *value), std::move(item_profile)};
  };
  const auto resize_literal = [&](const semantic::sv::ConstraintExpression& item)
      -> std::optional<runtime::PackedLogic4> {
    const auto retained = retained_constant(item);
    if (!retained) return std::nullopt;
    const auto& value = retained->first;
    const auto extension = retained->second.signed_value && !value.empty()
        ? value.get(value.width() - 1U) : runtime::Logic4::zero;
    runtime::PackedLogic4 result(profile.width, extension);
    for (std::size_t index = 0;
         index < std::min(profile.width, value.width()); ++index) {
      result.set(index, value.get(index));
    }
    return result;
  };
  runtime::SystemVerilogConstraintDistribution distribution;
  distribution.canonical_identity = std::move(canonical_identity);
  distribution.variable = variable->second;
  for (std::size_t index = 1; index < expression.operands.size(); ++index) {
    const auto& item = expression.operands[index];
    if (item.kind
            != semantic::sv::ConstraintExpressionKind::distribution_item
        || item.operands.size() != 2U) {
      error = "dist list contains an invalid weighted item";
      return std::nullopt;
    }
    const auto weight = retained_constant(item.operands[1]);
    if (!weight || weight->first.width() > 64U
        || std::ranges::any_of(
            weight->first.bval_words(), [](const auto word) {
              return word != 0;
            })) {
      error = "dist weight is not a known bounded integer";
      return std::nullopt;
    }
    runtime::SystemVerilogConstraintDistributionEntry retained;
    retained.weight = weight->first.low_word().aval;
    retained.weight_kind = item.text == "@dist-:/"
        ? runtime::SystemVerilogConstraintDistributionWeight::AcrossRange
        : runtime::SystemVerilogConstraintDistributionWeight::PerValue;
    if (item.operands[0].kind
            == semantic::sv::ConstraintExpressionKind::inside_range
        && item.operands[0].operands.size() == 2U) {
      const auto low = resize_literal(item.operands[0].operands[0]);
      const auto high = resize_literal(item.operands[0].operands[1]);
      if (!low || !high) {
        error = "dist range bounds are not bounded constants";
        return std::nullopt;
      }
      retained.low = *low;
      retained.high = *high;
    } else {
      const auto choice = resize_literal(item.operands[0]);
      if (!choice) {
        error = "dist choice is not a bounded constant";
        return std::nullopt;
      }
      retained.low = *choice;
      retained.high = *choice;
    }
    distribution.entries.push_back(std::move(retained));
  }
  return distribution;
}

std::optional<std::vector<std::pair<
    runtime::SystemVerilogConstraintVariableId,
    runtime::SystemVerilogConstraintVariableId>>>
lower_systemverilog_solve_before(
    const semantic::sv::ConstraintExpression& expression,
    const std::string_view specialization_identity,
    const std::map<std::string,
                   runtime::SystemVerilogConstraintVariableId>& variables,
    std::string& error) {
  error.clear();
  if (expression.kind != semantic::sv::ConstraintExpressionKind::solve_before
      || expression.operands.size() != 2U
      || expression.operands[0].kind
          != semantic::sv::ConstraintExpressionKind::solve_list
      || expression.operands[1].kind
          != semantic::sv::ConstraintExpressionKind::solve_list
      || expression.operands[0].operands.empty()
      || expression.operands[1].operands.empty()) {
    error = "solve-before requires two nonempty variable lists";
    return std::nullopt;
  }
  const auto lower_list = [&](const auto& list)
      -> std::optional<std::vector<
          runtime::SystemVerilogConstraintVariableId>> {
    std::vector<runtime::SystemVerilogConstraintVariableId> result;
    result.reserve(list.operands.size());
    for (const auto& item : list.operands) {
      if (item.kind != semantic::sv::ConstraintExpressionKind::name) {
        return std::nullopt;
      }
      const auto binding = std::ranges::find(
          item.bindings, specialization_identity,
          &semantic::sv::ConstraintBinding::specialization_identity);
      if (binding == item.bindings.end()) return std::nullopt;
      const auto variable = variables.find(binding->canonical_identity);
      if (variable == variables.end()) return std::nullopt;
      result.push_back(variable->second);
    }
    return result;
  };
  const auto earlier = lower_list(expression.operands[0]);
  const auto later = lower_list(expression.operands[1]);
  if (!earlier || !later) {
    error = "solve-before item has no exact solver variable binding";
    return std::nullopt;
  }
  std::vector<std::pair<
      runtime::SystemVerilogConstraintVariableId,
      runtime::SystemVerilogConstraintVariableId>> result;
  for (const auto left : *earlier) {
    for (const auto right : *later) result.emplace_back(left, right);
  }
  return result;
}

void configure_systemverilog_class_constraints(
    runtime::SystemVerilogConstraintSolver& solver,
    const runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::Hir& hir,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::function<bool(std::string_view)>& constraint_enabled) {
  const auto declaration = std::ranges::find(
      hir.classes(), specialization.declaration_identity,
      &semantic::sv::ClassDeclaration::canonical_identity);
  if (declaration == hir.classes().end()) return;
  const auto find_constraint = [&](const std::string_view identity)
      -> const semantic::sv::ClassConstraint* {
    for (const auto& candidate_class : hir.classes()) {
      const auto found = std::ranges::find(
          candidate_class.constraints, identity,
          &semantic::sv::ClassConstraint::canonical_identity);
      if (found != candidate_class.constraints.end()) return &*found;
    }
    return nullptr;
  };
  for (const auto& composed : declaration->composed_constraints) {
    if (!composed.mode_enabled || !composed.override_legal
        || (constraint_enabled
            && !constraint_enabled(composed.selected_identity))) {
      continue;
    }
    const auto* constraint = find_constraint(composed.selected_identity);
    if (constraint == nullptr || !constraint->defined
        || constraint->pure || constraint->external) {
      continue;
    }
    for (std::size_t index = 0; index < constraint->expressions.size();
         ++index) {
      const auto& expression = constraint->expressions[index];
      const auto identity = constraint->canonical_identity + "::"
          + std::to_string(index);
      std::string error;
      if (expression.kind
          == semantic::sv::ConstraintExpressionKind::distribution) {
        auto distribution = lower_systemverilog_constraint_distribution(
            expression, specialization.specialization_identity, variables,
            solver.variables(), identity, error);
        if (!distribution) {
          throw std::invalid_argument{
              "cannot lower class distribution '" + identity
              + "': " + error};
        }
        solver.add_distribution(std::move(*distribution));
        continue;
      }
      if (expression.kind
          == semantic::sv::ConstraintExpressionKind::solve_before) {
        auto edges = lower_systemverilog_solve_before(
            expression, specialization.specialization_identity,
            variables, error);
        if (!edges) {
          throw std::invalid_argument{
              "cannot lower class solve-before '" + identity
              + "': " + error};
        }
        for (const auto& [earlier, later] : *edges) {
          solver.add_solve_before(earlier, later);
        }
        continue;
      }
      std::vector<runtime::SystemVerilogConstraintVariableId> dependencies;
      auto lowered = lower_systemverilog_constraint_expression(
          expression, specialization.specialization_identity, variables,
          dependencies, error);
      if (!lowered) {
        throw std::invalid_argument{
            "cannot lower class constraint '" + identity
            + "': " + error};
      }
      auto clause = runtime::systemverilog_constraint_expression_clause(
          identity, std::move(dependencies), std::move(*lowered));
      clause.soft = expression.kind
          == semantic::sv::ConstraintExpressionKind::soft;
      solver.add_clause(std::move(clause));
    }
  }
}

}  // namespace fsim::app::application_detail
