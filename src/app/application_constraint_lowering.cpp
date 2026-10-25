// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <bit>
#include <charconv>
#include <limits>

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

// A value of `width` bits with only bit `bit` set.
[[nodiscard]] runtime::PackedLogic4 bit_value(
    const std::size_t width, const std::size_t bit) {
  runtime::PackedLogic4 result(width, runtime::Logic4::zero);
  result.set(bit, runtime::Logic4::one);
  return result;
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
  // An unsized based literal (`'hDEADBEEF`) is at least 32 bits wide
  // (IEEE 1800-2017 5.7.1).
  auto width_value = apostrophe == 0U
      ? std::optional<std::uint64_t>{32U}
      : decimal(std::string_view{spelling}.substr(0, apostrophe));
  if (apostrophe == 0U && suffix.size() >= 2U) {
    const auto base_index = suffix.front() == 's' || suffix.front() == 'S'
        ? 1U : 0U;
    if (base_index < suffix.size()) {
      const auto base = static_cast<char>(std::tolower(
          static_cast<unsigned char>(suffix[base_index])));
      const auto digits = suffix.size() - base_index - 1U;
      const std::uint64_t bits = base == 'b' ? digits
          : base == 'o' ? digits * 3U : base == 'h' ? digits * 4U : 0U;
      if (bits > 32U) width_value = bits;
    }
  }
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
  if (spelling == "<<" || spelling == "<<<") return Operator::ShiftLeft;
  if (spelling == ">>") return Operator::ShiftRight;
  if (spelling == ">>>") return Operator::ArithmeticShiftRight;
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
      std::string& error,
      std::span<const runtime::SystemVerilogConstraintVariable>
          solver_variables = {})
      : specialization_(specialization), variables_(variables),
        container_variables_(container_variables),
        dependencies_(dependencies), error_(error),
        solver_variables_(solver_variables) {}

  [[nodiscard]] std::optional<ExpressionId> lower(
      const sv::ConstraintExpression& expression) {
    if (const auto retained_literal = literal(expression)) {
      return builder_.constant(
          retained_literal->first, retained_literal->second);
    }
    if (expression.kind == sv::ConstraintExpressionKind::name) {
      if (const auto* frame = iterator(expression.text)) {
        // A foreach loop variable is its index; a with-clause item is its
        // element (IEEE 1800-2017 18.5.8.1, 7.12.4).
        if (frame->string_key) {
          error_ = "a string-keyed foreach index is used as a value";
          return std::nullopt;
        }
        if (!frame->item) {
          return builder_.constant(
              low_value(32, frame->index), index_profile());
        }
        if (!frame->element) {
          error_ = "a structure element is used as a constraint value";
          return std::nullopt;
        }
        use(*frame->element);
        return variable_node(*frame->element, frame->profile);
      }
      if (const auto dot = expression.text.rfind('.');
          dot != std::string::npos
          && expression.text.substr(dot) == ".index") {
        if (const auto* frame = iterator(
                std::string_view{expression.text}.substr(0, dot));
            frame != nullptr && frame->item) {
          return builder_.constant(
              low_value(32, frame->index), index_profile());
        }
      }
      // `item.kind` of a with-clause item that is a structure selects its
      // member (IEEE 1800-2017 7.12.4).
      if (const auto dot = expression.text.find('.');
          dot != std::string::npos) {
        if (const auto* frame = iterator(
                std::string_view{expression.text}.substr(0, dot));
            frame != nullptr && frame->item && !frame->path.empty()) {
          sv::ConstraintExpression selection;
          selection.kind = sv::ConstraintExpressionKind::name;
          selection.text = expression.text.substr(0, dot);
          selection.source = expression.source;
          auto rest = std::string_view{expression.text}.substr(dot + 1U);
          while (!rest.empty()) {
            const auto next = rest.find('.');
            sv::ConstraintExpression member;
            member.kind = sv::ConstraintExpressionKind::call;
            member.text = std::string{member_prefix}
                + std::string{rest.substr(0, next)};
            member.source = expression.source;
            member.operands.push_back(std::move(selection));
            selection = std::move(member);
            rest = next == std::string_view::npos
                ? std::string_view{} : rest.substr(next + 1U);
          }
          return lower(selection);
        }
      }
      const auto* binding = exact_binding(expression);
      if (binding == nullptr) {
        if (auto value = specialization_parameter(expression)) return value;
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
        const auto profile = binding_profile(*binding);
        return builder_.constant(
            low_value(profile.width, *value), profile);
      }
      const auto variable = variables_.find(binding->canonical_identity);
      if (variable == variables_.end()) {
        (void)container(binding->canonical_identity);
        error_ = "constraint property '" + binding->canonical_identity
            + "' has no solver variable";
        return std::nullopt;
      }
      use(variable->second);
      return variable_node(
          variable->second, binding_profile(*binding));
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text == ".size" && expression.operands.size() == 1U) {
      if (expression.operands.front().kind
          == sv::ConstraintExpressionKind::index) {
        const auto path = aggregate_path(expression.operands.front());
        if (!path) {
          error_ = "constraint size() receiver is not a container";
          return std::nullopt;
        }
        return builder_.constant(
            low_value(32, node_size(*path).value_or(0U)), index_profile());
      }
      if (const auto& locator = expression.operands.front();
          locator.kind == sv::ConstraintExpressionKind::call
          && is_locator(locator.text) && locator.operands.size() >= 2U) {
        return locator_size(locator);
      }
      const auto* binding = exact_binding(expression.operands.front());
      if (binding == nullptr) {
        error_ = "constraint size() receiver has no exact binding";
        return std::nullopt;
      }
      // While sizes are solved a random container's size is a variable
      // (IEEE 1800-2017 18.4), even when its current elements are known.
      const auto size = variables_.find(
          binding->canonical_identity + ".size()");
      if (size == variables_.end()) {
        // A materialized count is the size; an array of handles has
        // elements past its old size that no object joins yet.
        if (const auto count = variables_.find(
                binding->canonical_identity + "[]");
            count != variables_.end()
            && count->second < solver_variables_.size()
            && solver_variables_[count->second].domain.size() == 1U) {
          return builder_.constant(low_value(32,
              solver_variables_[count->second].domain.front()
                  .low_word().aval), index_profile());
        }
        if (nested_ != nullptr
            && nested_->contains(binding->canonical_identity)) {
          return builder_.constant(low_value(32,
              nested_->at(binding->canonical_identity).size()),
              index_profile());
        }
        if (const auto* elements = container(binding->canonical_identity)) {
          return builder_.constant(
              low_value(32, elements->size()), index_profile());
        }
        error_ = "constraint container '" + binding->canonical_identity
            + "' has no size";
        return std::nullopt;
      }
      use(size->second);
      return variable_node(size->second, size_profile());
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && (expression.text == ".sum" || expression.text == ".product"
            || expression.text == ".and" || expression.text == ".or"
            || expression.text == ".xor")
        && !expression.operands.empty() && expression.operands.size() <= 3U) {
      // Array reduction methods, optionally through a with-clause (IEEE
      // 1800-2017 7.12.3, 18.5.8.2).
      const auto* binding = exact_binding(expression.operands.front());
      if (binding == nullptr) {
        error_ = "constraint reduction receiver has no exact binding";
        return std::nullopt;
      }
      const auto profile = binding_profile(*binding);
      const auto operation = expression.text == ".sum" ? Operator::Add
          : expression.text == ".product" ? Operator::Multiply
          : expression.text == ".and" ? Operator::BitwiseAnd
          : expression.text == ".or" ? Operator::BitwiseOr
          : Operator::BitwiseXor;
      std::optional<ExpressionId> result;
      const auto visited = each_item(expression, *binding,
          [&](const std::optional<ExpressionId> value) {
            result = result
                ? builder_.binary(operation, *result, *value) : *value;
          });
      if (!visited) return std::nullopt;
      if (result) return result;
      // An empty array reduces to the operation's identity.
      runtime::PackedLogic4 identity(profile.width,
          operation == Operator::BitwiseAnd ? runtime::Logic4::one
                                            : runtime::Logic4::zero);
      if (operation == Operator::Multiply) identity.set(0, runtime::Logic4::one);
      return builder_.constant(std::move(identity), profile);
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text.starts_with("@sv-cast:")
        && expression.operands.size() == 1U) {
      const auto operand = lower(expression.operands.front());
      if (!operand) return std::nullopt;
      const auto target = std::string_view{expression.text}.substr(9U);
      auto profile = builder_.profile(*operand);
      const auto separator = target.find(':');
      if (separator != std::string_view::npos
          && (target.substr(0, separator) == "signed"
              || target.substr(0, separator) == "unsigned")) {
        // A typedef cast resolved to its signedness and width.
        const auto width = decimal(target.substr(separator + 1U));
        if (!width || *width == 0U || *width > 4096U) {
          error_ = "constraint cast width is out of range";
          return std::nullopt;
        }
        profile.signed_value = target.substr(0, separator) == "signed";
        profile.width = static_cast<std::size_t>(*width);
      } else if (target == "signed" || target == "unsigned") {
        profile.signed_value = target == "signed";
      } else if (const auto width = decimal(target)) {
        if (*width == 0U || *width > 4096U) {
          error_ = "constraint cast width is out of range";
          return std::nullopt;
        }
        profile.width = static_cast<std::size_t>(*width);
      } else if (const auto scalar = integral_cast(target)) {
        profile = *scalar;
      } else {
        error_ = "unsupported constraint cast '" + std::string{target} + "'";
        return std::nullopt;
      }
      return builder_.resize(*operand, profile);
    }
    if (expression.kind == sv::ConstraintExpressionKind::unary
        && expression.operands.size() == 1U
        && (expression.text == "&" || expression.text == "|"
            || expression.text == "^" || expression.text == "~&"
            || expression.text == "~|" || expression.text == "~^"
            || expression.text == "^~")) {
      // Reduction operators (IEEE 1800-2017 11.4.9).
      const auto operand = lower(expression.operands.front());
      if (!operand) return std::nullopt;
      auto profile = builder_.profile(*operand);
      if (profile.width == 0U) {
        error_ = "constraint reduction requires a sized operand";
        return std::nullopt;
      }
      profile.signed_value = false;
      const auto value = builder_.resize(*operand, profile);
      const auto width = profile.width;
      ExpressionId reduced{};
      if (expression.text == "|" || expression.text == "~|") {
        reduced = builder_.binary(Operator::NotEqual, value,
            builder_.constant(
                runtime::PackedLogic4(width, runtime::Logic4::zero), profile));
      } else if (expression.text == "&" || expression.text == "~&") {
        reduced = builder_.binary(Operator::Equal, value,
            builder_.constant(
                runtime::PackedLogic4(width, runtime::Logic4::one), profile));
      } else {
        const auto count = count_ones(value, profile);
        reduced = builder_.binary(Operator::NotEqual,
            builder_.binary(Operator::BitwiseAnd, count,
                builder_.constant(low_value(32, 1), index_profile())),
            builder_.constant(low_value(32, 0), index_profile()));
      }
      return expression.text.starts_with('~') || expression.text == "^~"
          ? builder_.unary(Operator::LogicalNot, reduced) : reduced;
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text == "$countbits" && expression.operands.size() >= 2U) {
      // $countbits counts the bits equal to any control bit (20.9); a
      // two-state variable has only '0 and '1 bits.
      const auto operand = lower(expression.operands.front());
      if (!operand) return std::nullopt;
      auto profile = builder_.profile(*operand);
      if (profile.width == 0U) {
        error_ = "constraint bit count requires a sized operand";
        return std::nullopt;
      }
      profile.signed_value = false;
      bool ones{};
      bool zeros{};
      for (std::size_t index = 1; index < expression.operands.size(); ++index) {
        const auto control = literal(expression.operands[index]);
        if (!control || control->first.width() == 0U) {
          error_ = "constraint $countbits control is not a constant bit";
          return std::nullopt;
        }
        const auto bit = control->first.get(0);
        ones = ones || bit == runtime::Logic4::one;
        zeros = zeros || bit == runtime::Logic4::zero;
      }
      const auto value = builder_.resize(*operand, profile);
      const auto set = count_ones(value, profile);
      if (ones && zeros) {
        return builder_.constant(low_value(32, profile.width), index_profile());
      }
      if (ones) return set;
      if (zeros) {
        return builder_.binary(Operator::Subtract,
            builder_.constant(low_value(32, profile.width), index_profile()),
            set);
      }
      return builder_.constant(low_value(32, 0), index_profile());
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text == "$clog2" && expression.operands.size() == 1U) {
      // $clog2 counts the powers of two below its argument (20.8.1).
      const auto operand = lower(expression.operands.front());
      if (!operand) return std::nullopt;
      auto profile = builder_.profile(*operand);
      if (profile.width == 0U || profile.width > 64U) {
        error_ = "constraint $clog2 requires a 1- to 64-bit operand";
        return std::nullopt;
      }
      profile.signed_value = false;
      const auto value = builder_.resize(*operand, profile);
      std::optional<ExpressionId> count;
      for (std::size_t bit = 0; bit < profile.width; ++bit) {
        const auto above = builder_.resize(builder_.binary(Operator::Greater,
            value, builder_.constant(low_value(profile.width,
                std::uint64_t{1} << bit), profile)), index_profile());
        count = count ? builder_.binary(Operator::Add, *count, above) : above;
      }
      return count;
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && (expression.text == "$countones" || expression.text == "$onehot"
            || expression.text == "$onehot0")
        && expression.operands.size() == 1U) {
      // Bit-count functions (IEEE 1800-2017 20.9).
      const auto operand = lower(expression.operands.front());
      if (!operand) return std::nullopt;
      const auto width = builder_.profile(*operand).width;
      if (width == 0U || width > 64U) {
        error_ = "constraint bit count requires a 1- to 64-bit operand";
        return std::nullopt;
      }
      auto bit_profile = builder_.profile(*operand);
      bit_profile.signed_value = false;
      const auto unsigned_operand = builder_.resize(*operand, bit_profile);
      std::optional<ExpressionId> count;
      for (std::size_t bit = 0; bit < width; ++bit) {
        const auto mask = builder_.constant(
            low_value(width, std::uint64_t{1} << bit), bit_profile);
        const auto set = builder_.binary(Operator::NotEqual,
            builder_.binary(Operator::BitwiseAnd, unsigned_operand, mask),
            builder_.constant(low_value(width, 0), bit_profile));
        const auto counted = builder_.resize(set, index_profile());
        count = count ? builder_.binary(Operator::Add, *count, counted)
                      : counted;
      }
      if (expression.text == "$countones") return count;
      return builder_.binary(
          expression.text == "$onehot" ? Operator::Equal : Operator::LessEqual,
          *count, builder_.constant(low_value(32, 1), index_profile()));
    }
    if (expression.kind == sv::ConstraintExpressionKind::concatenation
        && !expression.operands.empty()) {
      // {a, b} is a * 2^width(b) + b over the concatenated width.
      std::vector<ExpressionId> parts;
      std::size_t total{};
      for (const auto& operand : expression.operands) {
        const auto part = lower(operand);
        if (!part) return std::nullopt;
        parts.push_back(*part);
        total += builder_.profile(*part).width;
      }
      if (total == 0U || total > 64U) {
        error_ = "constraint concatenation requires a 1- to 64-bit result";
        return std::nullopt;
      }
      const runtime::SystemVerilogConstraintVariableProfile result_profile{
          runtime::SystemVerilogConstraintDomainKind::BitVector,
          total, false, "logic", false};
      std::optional<ExpressionId> result;
      for (const auto part : parts) {
        auto part_profile = builder_.profile(part);
        part_profile.signed_value = false;
        const auto widened = builder_.resize(
            builder_.resize(part, part_profile), result_profile);
        if (result) {
          const auto shift = builder_.profile(part).width;
          const auto scale = builder_.constant(
              low_value(total, shift >= 64U ? 0U : std::uint64_t{1} << shift),
              result_profile);
          result = builder_.binary(Operator::Add,
              builder_.binary(Operator::Multiply, *result, scale), widened);
        } else {
          result = widened;
        }
      }
      return result;
    }
    if ((expression.kind == sv::ConstraintExpressionKind::index
            && expression.operands.size() == 2U)
        || (expression.kind == sv::ConstraintExpressionKind::slice
            && expression.operands.size() == 3U)) {
      // A bit- or part-select of a packed value (IEEE 1800-2017 11.5.1).
      const auto* binding = exact_binding(expression.operands[0]);
      const bool element_of_container = expression.operands[0].kind
              == sv::ConstraintExpressionKind::index
          && names_subarray(expression.operands[0]);
      // `a[i].m[j]` selects an element of an array member.
      const bool member_array = expression.operands[0].kind
              == sv::ConstraintExpressionKind::call
          && expression.operands[0].text.starts_with(member_prefix)
          && [&] {
               const auto prior_error = error_;
               const auto path = aggregate_path(expression.operands[0]);
               error_ = prior_error;
               return path.has_value();
             }();
      const bool packed = !element_of_container && !member_array
          && (binding == nullptr
              || (!container_variables_.contains(binding->canonical_identity)
                  && !variables_.contains(
                      binding->canonical_identity + ".size()")));
      if (packed) {
        if (const auto selected = packed_select(expression)) return selected;
        if (!error_.empty()) return std::nullopt;
      }
    }
    if (expression.kind == sv::ConstraintExpressionKind::binary
        && expression.operands.size() == 2U
        && (expression.text == "<<" || expression.text == ">>"
            || expression.text == "<<<" || expression.text == ">>>")) {
      // A shift is a product or quotient by a power of two; the result
      // keeps the shifted operand's width (IEEE 1800-2017 11.4.10).
      const auto value = lower(expression.operands[0]);
      if (!value) return std::nullopt;
      const auto amount = lower(expression.operands[1]);
      if (!amount) return std::nullopt;
      const auto profile = builder_.profile(*value);
      auto unsigned_profile = profile;
      unsigned_profile.signed_value = false;
      if (expression.text == ">>>" && profile.signed_value) {
        error_ = "constraint arithmetic right shift of a signed value is "
            "not supported";
        return std::nullopt;
      }
      runtime::PackedLogic4 two(profile.width, runtime::Logic4::zero);
      if (profile.width > 1U) two.set(1, runtime::Logic4::one);
      const auto factor = builder_.binary(Operator::Power,
          builder_.constant(std::move(two), unsigned_profile),
          builder_.resize(*amount, index_profile()));
      const auto shifted = expression.text.starts_with("<<")
          ? builder_.binary(Operator::Multiply, *value, factor)
          : builder_.binary(Operator::Divide,
                builder_.resize(*value, unsigned_profile), factor);
      return builder_.resize(shifted, profile);
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text.starts_with(member_prefix)
        && expression.operands.size() == 1U) {
      // A member of an element of an array of structures (`a[i].m`) is a
      // variable of its own (IEEE 1800-2017 7.2, 18.4).
      const auto path = member_path(expression);
      if (!path) {
        if (error_.empty()) {
          error_ = "constraint member selection '" + expression.text
              + "' is not of a structure element";
        }
        return std::nullopt;
      }
      if (path->empty()) {
        // Outside the array: guarded from constraining (18.5.8.1).
        return builder_.constant(low_value(32, 0), index_profile());
      }
      auto found = variables_.find(*path);
      if (found == variables_.end()
          || found->second >= solver_variables_.size()) {
        error_ = "constraint member '" + *path
            + "' has no solver variable";
        return std::nullopt;
      }
      use(found->second);
      return variable_node(found->second,
          solver_variables_[found->second].profile);
    }
    // An element of an array member of a structure element (`a[i].m[j]`)
    // is selected from that member (IEEE 1800-2017 7.2, 18.4).
    if (expression.kind == sv::ConstraintExpressionKind::index
        && expression.operands.size() == 2U
        && (expression.operands[0].kind
                == sv::ConstraintExpressionKind::name
            || expression.operands[0].kind
                == sv::ConstraintExpressionKind::index
            || (expression.operands[0].kind
                    == sv::ConstraintExpressionKind::call
                && expression.operands[0].text.starts_with(member_prefix)
                && expression.operands[0].operands.size() == 1U))) {
      const auto select_from = [&](const std::string& path,
                                   const std::string& typed)
          -> std::optional<ExpressionId> {
        const auto profile = element_profile(expression.operands[0], typed);
        const auto outside = [&] {
          // A selection outside the array occurs only where a guard keeps
          // it from constraining (IEEE 1800-2017 18.5.8.1).
          return builder_.constant(
              runtime::PackedLogic4(profile.width, runtime::Logic4::zero),
              profile);
        };
        if (path.empty()) return outside();
        const auto* elements = container(path);
        if (elements == nullptr) {
          error_ = "constraint container '" + path
              + "' has no element solver variables";
          return std::nullopt;
        }
        if (const auto position
            = select_position(path, expression.operands[1])) {
          if (!*position) return outside();
          const auto element = (*elements)[**position];
          use(element);
          return variable_node(element, profile);
        }
        // A random index selects among the elements.
        const auto selector = lower(expression.operands[1]);
        if (!selector) return std::nullopt;
        const auto* held = shape(path);
        if (held != nullptr && held->associative && held->string_keys) {
          error_ = "constraint string-keyed selection needs a constant key";
          return std::nullopt;
        }
        auto result = outside();
        for (std::size_t index = elements->size(); index-- > 0U;) {
          use((*elements)[index]);
          const auto key = held != nullptr && held->associative
              ? builder_.constant(held->keys[index],
                    runtime::SystemVerilogConstraintVariableProfile{
                        runtime::SystemVerilogConstraintDomainKind::BitVector,
                        held->keys[index].width(), false, "bit", false})
              : builder_.constant(low_value(32,
                    static_cast<std::uint64_t>(index_at(path, index))),
                    index_profile());
          result = builder_.conditional(
              builder_.binary(Operator::Equal, *selector, key),
              variable_node((*elements)[index], profile), result);
        }
        return result;
      };
      if (expression.operands[0].kind
          == sv::ConstraintExpressionKind::name) {
        const auto* binding = exact_binding(expression.operands[0]);
        if (binding == nullptr) {
          error_ = "constraint element selection has no exact binding";
          return std::nullopt;
        }
        return select_from(binding->canonical_identity,
            binding->canonical_identity);
      }
      const auto prior_error = error_;
      if (const auto selected = aggregate_path(expression.operands[0])) {
        return select_from(*selected, *selected);
      }
      // A random index of an outer dimension selects among its subarrays
      // (IEEE 1800-2017 7.4.5, 18.3).
      const auto alternatives = aggregate_alternatives(expression.operands[0]);
      if (!alternatives) {
        if (error_.empty()) {
          error_ = "constraint element selection is not of a container";
        }
        return std::nullopt;
      }
      error_ = prior_error;
      // Every alternative is typed by the subarrays' elements.
      std::string typed;
      for (const auto& alternative : *alternatives) {
        if (!alternative.second.empty()) {
          typed = alternative.second;
          break;
        }
      }
      std::optional<ExpressionId> result;
      for (auto at = alternatives->size(); at-- > 0U;) {
        const auto& [guard, path] = (*alternatives)[at];
        const auto value = select_from(path, typed);
        if (!value) return std::nullopt;
        result = result && guard
            ? builder_.conditional(*guard, *value, *result) : *value;
      }
      return result;
    }
    if (expression.kind == sv::ConstraintExpressionKind::soft
        && expression.operands.size() == 1U) {
      return lower(expression.operands.front());
    }
    if (expression.kind == sv::ConstraintExpressionKind::distribution
        && expression.operands.size() >= 2U) {
      // A dist below a guard or loop keeps its hard part: membership in
      // the items of nonzero weight (IEEE 1800-2017 18.5.4).
      sv::ConstraintExpression membership;
      membership.kind = sv::ConstraintExpressionKind::inside_set;
      membership.text = "inside";
      membership.source = expression.source;
      membership.operands.push_back(expression.operands.front());
      for (std::size_t index = 1; index < expression.operands.size(); ++index) {
        const auto& item = expression.operands[index];
        if (item.kind != sv::ConstraintExpressionKind::distribution_item
            || item.operands.size() != 2U) {
          error_ = "dist list contains an invalid weighted item";
          return std::nullopt;
        }
        if (const auto weight = constant_value(item.operands[1]);
            weight && *weight == 0) {
          continue;
        }
        membership.operands.push_back(item.operands[0]);
      }
      if (membership.operands.size() == 1U) {
        return builder_.constant(low_value(1, 0),
            runtime::SystemVerilogConstraintVariableProfile{
                runtime::SystemVerilogConstraintDomainKind::BitVector,
                1, false, "bit", false});
      }
      return lower(membership);
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
        } else if (const auto* binding = item.kind
                           == sv::ConstraintExpressionKind::name
                       ? exact_binding(item) : nullptr;
                   binding != nullptr
                   && container(binding->canonical_identity) != nullptr) {
          // An unpacked array in the set contributes its elements (IEEE
          // 1800-2017 11.4.13).
          const auto profile = binding_profile(*binding);
          for (const auto element : *container(binding->canonical_identity)) {
            use(element);
            const auto equal = builder_.binary(Operator::Equal, *subject,
                variable_node(element, profile));
            selected = selected
                ? builder_.binary(Operator::LogicalOr, *selected, equal)
                : equal;
          }
          if (!selected) {
            selected = builder_.constant(low_value(1, 0),
                runtime::SystemVerilogConstraintVariableProfile{
                    runtime::SystemVerilogConstraintDomainKind::BitVector,
                    1, false, "bit", false});
          }
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
    // A solve-before ordering in a foreach body orders the element
    // variables without changing the solution set (IEEE 1800-2017 18.5.10).
    if (expression.kind == sv::ConstraintExpressionKind::solve_before
        && !iterators_.empty()) {
      return builder_.constant(low_value(1, 1),
          runtime::SystemVerilogConstraintVariableProfile{
              runtime::SystemVerilogConstraintDomainKind::BitVector,
              1, false, "bit", false});
    }
    if (expression.kind == sv::ConstraintExpressionKind::foreach_constraint
        && expression.operands.size() == 2U) {
      const auto& selection = expression.operands[0];
      // The array is named, an element of an outer array, or an array
      // member of a structure element (`foreach (a[i].m[j])`, 7.2).
      if (selection.kind != sv::ConstraintExpressionKind::index
          || selection.operands.size() != 2U
          || (selection.operands[0].kind
                  != sv::ConstraintExpressionKind::name
              && selection.operands[0].kind
                  != sv::ConstraintExpressionKind::index
              && !(selection.operands[0].kind
                      == sv::ConstraintExpressionKind::call
                  && selection.operands[0].text.starts_with(member_prefix)
                  && selection.operands[0].operands.size() == 1U))
          || selection.operands[1].kind
              != sv::ConstraintExpressionKind::name) {
        error_ = "constraint foreach requires one named container index";
        return std::nullopt;
      }
      const auto loop = foreach_loop(selection);
      if (!loop) return std::nullopt;
      std::vector<ExpressionId> predicates;
      predicates.reserve(loop->size());
      for (const auto& frame : *loop) {
        iterators_.push_back(frame);
        const auto predicate = lower(expression.operands[1]);
        iterators_.pop_back();
        if (!predicate) return std::nullopt;
        predicates.push_back(*predicate);
      }
      if (predicates.empty()) {
        return builder_.constant(low_value(1, 1),
            runtime::SystemVerilogConstraintVariableProfile{
                runtime::SystemVerilogConstraintDomainKind::BitVector,
                1, false, "bit", false});
      }
      return builder_.conjunction(predicates);
    }
    if (expression.kind == sv::ConstraintExpressionKind::unique_constraint) {
      std::vector<ExpressionId> values;
      for (const auto& item : expression.operands) {
        if (item.kind != sv::ConstraintExpressionKind::name
            && item.kind != sv::ConstraintExpressionKind::index) {
          error_ = "unique constraint items must be variables or selected "
              "container elements";
          return std::nullopt;
        }
        if (item.kind == sv::ConstraintExpressionKind::name) {
          const auto binding = std::ranges::find(
              item.bindings, specialization_,
              &sv::ConstraintBinding::specialization_identity);
          if (binding == item.bindings.end()) {
            error_ = "unique constraint item has no exact binding";
            return std::nullopt;
          }
          if (const auto* elements = container(binding->canonical_identity)) {
            const auto profile = binding_profile(*binding);
            for (const auto element : *elements) {
              if (std::ranges::find(dependencies_, element)
                  == dependencies_.end()) {
                dependencies_.push_back(element);
              }
              values.push_back(variable_node(element, profile));
            }
            continue;
          }
        }
        const auto value = lower(item);
        if (!value) return std::nullopt;
        values.push_back(*value);
      }
      if (values.size() < 2U) {
        return builder_.constant(
            low_value(1, 1),
            runtime::SystemVerilogConstraintVariableProfile{
                runtime::SystemVerilogConstraintDomainKind::BitVector,
                1, false, "$unique-result", false});
      }
      std::vector<ExpressionId> comparisons;
      for (std::size_t left = 0; left < values.size(); ++left) {
        for (std::size_t right = left + 1U; right < values.size(); ++right) {
          comparisons.push_back(builder_.binary(
              Operator::NotEqual, values[left], values[right]));
        }
      }
      return builder_.conjunction(comparisons);
    }
    if (expression.kind == sv::ConstraintExpressionKind::binary
        && expression.operands.size() == 2U
        && (expression.text == "==" || expression.text == "!=")) {
      // Unpacked arrays are equal when their elements are (IEEE 1800-2017
      // 7.4.5, 11.4.5).
      auto left = aggregate_elements(expression.operands[0]);
      if (!left && !error_.empty()) return std::nullopt;
      auto right = left ? aggregate_elements(expression.operands[1])
                        : std::nullopt;
      if (!right && !error_.empty()) return std::nullopt;
      if (left && right) {
        if (left->size() != right->size()) {
          error_ = "constraint array comparison of unequal sizes";
          return std::nullopt;
        }
        const bool equal = expression.text == "==";
        std::optional<ExpressionId> result;
        for (std::size_t index = 0; index < left->size(); ++index) {
          const auto compared = builder_.binary(
              equal ? Operator::Equal : Operator::NotEqual,
              (*left)[index], (*right)[index]);
          result = result
              ? builder_.binary(equal ? Operator::LogicalAnd
                                      : Operator::LogicalOr,
                    *result, compared)
              : compared;
        }
        if (result) return result;
        return builder_.constant(low_value(1, equal ? 1U : 0U),
            runtime::SystemVerilogConstraintVariableProfile{
                runtime::SystemVerilogConstraintDomainKind::BitVector,
                1, false, "bit", false});
      }
    }
    // Only a parameter given `$` is unbounded (IEEE 1800-2017 20.6.3);
    // a constraint operand never is.
    if (expression.text == "$isunbounded" && expression.operands.size() == 1U) {
      return builder_.constant(
          low_value(1, 0),
          runtime::SystemVerilogConstraintVariableProfile{
              runtime::SystemVerilogConstraintDomainKind::BitVector,
              1, false, "$isunbounded-result", false});
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

  /// The expression names a random container whose size is still being
  /// solved; it constrains the elements once they exist.
  [[nodiscard]] bool deferred() const noexcept { return deferred_; }

  struct Clause {
    runtime::SystemVerilogConstraintExpression expression;
    std::vector<runtime::SystemVerilogConstraintVariableId> dependencies;
    bool soft{};
    /// `disable soft` of `discarded` where `expression` (its guard) holds.
    bool discard{};
    std::vector<runtime::SystemVerilogConstraintVariableId> discarded;
    /// A distribution that applies where `expression` (its guard) holds;
    /// the next clause is its membership for a guard that is not fixed.
    std::optional<runtime::SystemVerilogConstraintDistribution> distribution;
  };

  /// Lower a constraint as separate clauses: the members of a block, the
  /// iterations of a foreach, the pairs of a unique constraint, each under
  /// its guards, so the solver checks each as soon as its own variables
  /// are assigned. A clause naming a container whose size is still being
  /// solved is left out. False reports an error.
  [[nodiscard]] bool lower_clauses(
      const sv::ConstraintExpression& expression,
      std::vector<Clause>& clauses) {
    return split(expression, clauses, false);
  }

  /// The current values of the string properties the constraints read.
  void set_string_state(
      const std::map<std::string, std::string, std::less<>>* state) {
    string_state_ = state;
  }

  /// The containers whose elements are containers, each with the paths of
  /// its elements, and the solver holding the containers' shapes under
  /// `prefix` + path.
  void set_container_shapes(
      const std::map<std::string, std::vector<std::string>>* nested,
      const runtime::SystemVerilogConstraintSolver* shapes,
      const std::string_view prefix) {
    nested_ = nested;
    shapes_ = shapes;
    shape_prefix_ = prefix;
  }

 private:
  struct Guard {
    const sv::ConstraintExpression* condition{};
    bool holds{};
  };

  [[nodiscard]] bool leaf(
      const std::function<std::optional<ExpressionId>()>& body,
      std::vector<Clause>& clauses, const bool soft) {
    builder_ = {};
    dependencies_.clear();
    deferred_ = false;
    auto root = body();
    for (auto guard = guards_.rbegin(); root && guard != guards_.rend();
         ++guard) {
      auto condition = lower(*guard->condition);
      if (!condition) {
        root.reset();
        break;
      }
      if (!guard->holds) {
        condition = builder_.unary(Operator::LogicalNot, *condition);
      }
      root = builder_.implication(*condition, *root);
    }
    if (!root) return deferred_;
    clauses.push_back({std::move(builder_).finish(*root),
        dependencies_, soft, false, {}, std::nullopt});
    builder_ = {};
    return true;
  }

  [[nodiscard]] bool split(const sv::ConstraintExpression& expression,
                           std::vector<Clause>& clauses, const bool soft) {
    using Kind = sv::ConstraintExpressionKind;
    if (expression.kind == Kind::constraint_block) {
      return std::ranges::all_of(expression.operands,
          [&](const auto& item) { return split(item, clauses, soft); });
    }
    if (expression.kind == Kind::soft && expression.operands.size() == 1U) {
      return split(expression.operands.front(), clauses, true);
    }
    if (expression.kind == Kind::implication
        && expression.operands.size() == 2U) {
      guards_.push_back({&expression.operands[0], true});
      const bool lowered = split(expression.operands[1], clauses, soft);
      guards_.pop_back();
      return lowered;
    }
    if (expression.kind == Kind::conditional_constraint
        && (expression.operands.size() == 2U
            || expression.operands.size() == 3U)) {
      guards_.push_back({&expression.operands[0], true});
      bool lowered = split(expression.operands[1], clauses, soft);
      guards_.pop_back();
      if (lowered && expression.operands.size() == 3U) {
        guards_.push_back({&expression.operands[0], false});
        lowered = split(expression.operands[2], clauses, soft);
        guards_.pop_back();
      }
      return lowered;
    }
    if (expression.kind == Kind::foreach_constraint
        && expression.operands.size() == 2U
        && expression.operands[0].kind == Kind::index
        && expression.operands[0].operands.size() == 2U
        && (expression.operands[0].operands[0].kind == Kind::name
            || expression.operands[0].operands[0].kind == Kind::index)
        && expression.operands[0].operands[1].kind == Kind::name) {
      deferred_ = false;
      const auto loop = foreach_loop(expression.operands[0]);
      if (!loop) return deferred_;
      for (const auto& frame : *loop) {
        iterators_.push_back(frame);
        const bool lowered = split(expression.operands[1], clauses, soft);
        iterators_.pop_back();
        if (!lowered) return false;
      }
      return true;
    }
    if (expression.kind == Kind::unique_constraint) {
      // Each pair of the unique items differs (IEEE 1800-2017 18.5.5).
      std::vector<std::pair<const sv::ConstraintExpression*,
          std::optional<std::pair<runtime::SystemVerilogConstraintVariableId,
              runtime::SystemVerilogConstraintVariableProfile>>>> items;
      for (const auto& item : expression.operands) {
        const auto* binding = exact_binding(item);
        deferred_ = false;
        const auto* elements = binding == nullptr
            ? nullptr : container(binding->canonical_identity);
        if (deferred_) return true;
        if (elements == nullptr) {
          items.push_back({&item, std::nullopt});
          continue;
        }
        const auto profile = binding_profile(*binding);
        for (const auto element : *elements) {
          items.push_back({nullptr, std::pair{element, profile}});
        }
      }
      const auto value = [&](const auto& item) -> std::optional<ExpressionId> {
        if (item.second) {
          use(item.second->first);
          return variable_node(item.second->first, item.second->second);
        }
        return lower(*item.first);
      };
      for (std::size_t left = 0; left < items.size(); ++left) {
        for (std::size_t right = left + 1U; right < items.size(); ++right) {
          if (!leaf([&]() -> std::optional<ExpressionId> {
                const auto first = value(items[left]);
                if (!first) return std::nullopt;
                const auto second = value(items[right]);
                if (!second) return std::nullopt;
                return builder_.binary(Operator::NotEqual, *first, *second);
              }, clauses, soft)) {
            return false;
          }
        }
      }
      return true;
    }
    if (expression.kind == Kind::call && expression.text == "@disable-soft"
        && expression.operands.size() == 1U) {
      // `disable soft v` under guards applies where the guards hold (IEEE
      // 1800-2017 18.5.14.2).
      builder_ = {};
      dependencies_.clear();
      deferred_ = false;
      if (!lower(expression.operands.front())) return deferred_;
      auto discarded = dependencies_;
      builder_ = {};
      dependencies_.clear();
      auto holds = builder_.constant(low_value(1, 1),
          runtime::SystemVerilogConstraintVariableProfile{
              runtime::SystemVerilogConstraintDomainKind::BitVector,
              1, false, "bit", false});
      for (const auto& guard : guards_) {
        auto condition = lower(*guard.condition);
        if (!condition) return deferred_;
        if (!guard.holds) {
          condition = builder_.unary(Operator::LogicalNot, *condition);
        }
        holds = builder_.binary(Operator::LogicalAnd, holds, *condition);
      }
      clauses.push_back({std::move(builder_).finish(holds), dependencies_,
          false, true, std::move(discarded), std::nullopt});
      builder_ = {};
      return true;
    }
    if (expression.kind == Kind::distribution
        && expression.operands.size() >= 2U && !soft) {
      // `(c ? a : b) dist D` is `c -> a dist D` and `!c -> b dist D`
      // (IEEE 1800-2023 18.5.3, 11.4.11): each arm's variable is weighted
      // where its condition selects it.
      if (const auto& subject = expression.operands.front();
          subject.kind == Kind::conditional && subject.operands.size() == 3U) {
        for (std::size_t arm = 0U; arm < 2U; ++arm) {
          auto selected = expression;
          selected.operands.front() = subject.operands[arm + 1U];
          guards_.push_back({&subject.operands[0], arm == 0U});
          const bool lowered = split(selected, clauses, soft);
          guards_.pop_back();
          if (!lowered) {
            return false;
          }
        }
        return true;
      }
      // A dist below guards or in a loop weights its variable where the
      // guards hold (IEEE 1800-2017 18.5.4).
      for (auto& weighted : weighted_distributions(expression)) {
        clauses.push_back(std::move(weighted));
      }
    }
    return leaf([&] { return lower(expression); }, clauses, soft);
  }

  /// The weighted distributions of a dist. An item naming a random
  /// variable (`x dist {y :/ 90, 9 :/ 10}`) weights the subject by that
  /// variable's chosen value: one distribution per value, guarded by it
  /// (IEEE 1800-2023 18.5.3).
  [[nodiscard]] std::vector<Clause> weighted_distributions(
      const sv::ConstraintExpression& expression) {
    std::vector<Clause> result;
    if (auto one = guarded_distribution(expression)) {
      result.push_back(std::move(*one));
      return result;
    }
    error_.clear();
    std::optional<runtime::SystemVerilogConstraintVariableId> random;
    const sv::ConstraintExpression* random_source{};
    std::string random_identity;
    const auto note = [&](const sv::ConstraintExpression& value) {
      if (constant_value(value)) return true;
      if (value.kind != sv::ConstraintExpressionKind::name
          || iterator(value.text) != nullptr) {
        return false;
      }
      const auto* binding = exact_binding(value);
      if (binding == nullptr || !binding->constant_value.empty()) return false;
      const auto found = variables_.find(binding->canonical_identity);
      if (found == variables_.end()
          || found->second >= solver_variables_.size()
          || (random && *random != found->second)) {
        return false;
      }
      random = found->second;
      random_source = &value;
      random_identity = binding->canonical_identity;
      return true;
    };
    for (std::size_t index = 1; index < expression.operands.size(); ++index) {
      const auto& item = expression.operands[index];
      if (item.kind != sv::ConstraintExpressionKind::distribution_item
          || item.operands.size() != 2U) {
        return result;
      }
      const auto& choice = item.operands[0];
      const bool known = choice.kind == sv::ConstraintExpressionKind::inside_range
              && choice.operands.size() == 2U
          ? note(choice.operands[0]) && note(choice.operands[1])
          : note(choice);
      if (!known) return result;
    }
    if (!random) return result;
    const auto& held = solver_variables_[*random];
    const auto width = held.profile.width;
    std::vector<runtime::PackedLogic4> values = held.domain;
    if (values.empty() && width != 0U && width <= 8U) {
      for (std::uint64_t value = 0; value < (std::uint64_t{1} << width);
           ++value) {
        values.push_back(low_value(width, value));
      }
    }
    if (values.empty() || values.size() > 256U) return result;
    for (const auto& value : values) {
      if (value.width() == 0U || value.width() > 64U || value.is_logic9()
          || value.low_word().bval != 0U) {
        result.clear();
        return result;
      }
      auto word = value.low_word().aval;
      if (held.profile.signed_value && value.width() < 64U
          && ((word >> (value.width() - 1U)) & 1U) != 0U) {
        word |= ~((std::uint64_t{1} << value.width()) - 1U);
      }
      pinned_ = Pinned{random_identity, static_cast<std::int64_t>(word),
          random_source, value, held.profile};
      auto one = guarded_distribution(expression);
      pinned_.reset();
      if (!one) {
        error_.clear();
        result.clear();
        return result;
      }
      result.push_back(std::move(*one));
    }
    return result;
  }

  [[nodiscard]] std::optional<Clause> guarded_distribution(
      const sv::ConstraintExpression& expression) {
    builder_ = {};
    dependencies_.clear();
    deferred_ = false;
    error_.clear();
    // Only a variable's values are weighted; any other subject is a
    // membership test (IEEE 1800-2017 18.5.4).
    const auto bare = [&](const sv::ConstraintExpression& source) {
      return source.kind == sv::ConstraintExpressionKind::name
          || source.kind == sv::ConstraintExpressionKind::index
          || (source.kind == sv::ConstraintExpressionKind::call
              && source.text.starts_with(member_prefix));
    };
    // `v + c`, `c + v` and `v - c` with `c` a constant weight `v` at the
    // items less (or plus) `c`.
    const auto& whole = expression.operands.front();
    const sv::ConstraintExpression* subject_source = &whole;
    std::optional<std::uint64_t> offset;
    bool subtract = false;
    if (whole.kind == sv::ConstraintExpressionKind::binary
        && whole.operands.size() == 2U
        && (whole.text == "+" || whole.text == "-")) {
      // Only a constant is known here: a random variable's domain may
      // already be narrowed to one candidate.
      const auto fixed = [&](const sv::ConstraintExpression& source)
          -> std::optional<std::uint64_t> {
        if (const auto retained = literal(source)) {
          return retained->first.known_unsigned_value();
        }
        if (source.kind != sv::ConstraintExpressionKind::name
            || iterator(source.text) != nullptr) {
          return std::nullopt;
        }
        const auto* binding = exact_binding(source);
        if (binding == nullptr || binding->constant_value.empty()) {
          return std::nullopt;
        }
        return decimal(binding->constant_value);
      };
      for (std::size_t side = 0; side < 2U && !offset; ++side) {
        if (side == 0U && whole.text == "-") continue;
        const auto& candidate = whole.operands[1U - side];
        if (!bare(candidate)) continue;
        if (const auto value = fixed(whole.operands[side])) {
          subject_source = &candidate;
          offset = *value;
          subtract = whole.text == "-";
        }
      }
      if (!offset) return std::nullopt;
    } else if (!bare(whole)) {
      return std::nullopt;
    }
    const auto subject = lower(*subject_source);
    if (!subject || dependencies_.size() != 1U
        || dependencies_.front() >= solver_variables_.size()) {
      error_.clear();
      return std::nullopt;
    }
    const auto variable = dependencies_.front();
    const auto width = solver_variables_[variable].profile.width;
    if (width == 0U || width > 64U) return std::nullopt;
    const auto mask = width == 64U
        ? ~std::uint64_t{} : (std::uint64_t{1} << width) - 1U;
    if (offset) {
      // The sum keeps the variable's width only when nothing wider takes
      // part (IEEE 1800-2017 11.6.1).
      const auto sum = lower(whole);
      if (!sum || builder_.profile(*sum).width != width) {
        error_.clear();
        return std::nullopt;
      }
    }
    // An item value of the subject, as a value of the variable.
    const auto shifted = [&](const std::uint64_t item) {
      return (subtract ? item + offset.value_or(0U)
                       : item - offset.value_or(0U)) & mask;
    };
    runtime::SystemVerilogConstraintDistribution distribution;
    distribution.variable = variable;
    const auto value = [&](const sv::ConstraintExpression& item)
        -> std::optional<runtime::PackedLogic4> {
      const auto constant = constant_value(item);
      if (!constant) return std::nullopt;
      return low_value(width, shifted(static_cast<std::uint64_t>(*constant)));
    };
    for (std::size_t index = 1; index < expression.operands.size(); ++index) {
      const auto& item = expression.operands[index];
      if (item.kind != sv::ConstraintExpressionKind::distribution_item
          || item.operands.size() != 2U) {
        return std::nullopt;
      }
      const auto weight = constant_value(item.operands[1]);
      if (!weight || *weight < 0) return std::nullopt;
      runtime::SystemVerilogConstraintDistributionEntry entry;
      entry.weight = static_cast<std::uint64_t>(*weight);
      entry.weight_kind = item.text == "@dist-:/"
          ? runtime::SystemVerilogConstraintDistributionWeight::AcrossRange
          : runtime::SystemVerilogConstraintDistributionWeight::PerValue;
      const auto& choice = item.operands[0];
      if (choice.kind == sv::ConstraintExpressionKind::inside_range
          && choice.operands.size() == 2U) {
        auto low = value(choice.operands[0]);
        auto high = value(choice.operands[1]);
        if (!low || !high) return std::nullopt;
        // A range the offset carries across the variable's bounds is two
        // ranges; it is left to membership.
        if (offset && low->known_unsigned_value()
            && high->known_unsigned_value()
            && *low->known_unsigned_value() > *high->known_unsigned_value()) {
          return std::nullopt;
        }
        entry.low = std::move(*low);
        entry.high = std::move(*high);
      } else {
        auto single = value(choice);
        if (!single) return std::nullopt;
        entry.low = *single;
        entry.high = std::move(*single);
      }
      distribution.entries.push_back(std::move(entry));
    }
    // The guard: the conjunction of the enclosing conditions.
    builder_ = {};
    dependencies_.clear();
    auto holds = builder_.constant(low_value(1, 1),
        runtime::SystemVerilogConstraintVariableProfile{
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            1, false, "bit", false});
    for (const auto& guard : guards_) {
      auto condition = lower(*guard.condition);
      if (!condition) {
        error_.clear();
        return std::nullopt;
      }
      if (!guard.holds) {
        condition = builder_.unary(Operator::LogicalNot, *condition);
      }
      holds = builder_.binary(Operator::LogicalAnd, holds, *condition);
    }
    if (pinned_) {
      const auto chosen = lower(*pinned_->source);
      if (!chosen) {
        error_.clear();
        return std::nullopt;
      }
      holds = builder_.binary(Operator::LogicalAnd, holds,
          builder_.binary(Operator::Equal, *chosen,
              builder_.constant(pinned_->value, pinned_->profile)));
    }
    Clause clause{std::move(builder_).finish(holds), dependencies_, false,
        false, {}, std::move(distribution)};
    builder_ = {};
    return clause;
  }

  /// The shape of the container at `path`; none indexes positions.
  [[nodiscard]] const runtime::SystemVerilogConstraintContainerShape*
  shape(const std::string& path) const {
    return shapes_ == nullptr ? nullptr
        : shapes_->container_shape(shape_prefix_ + path);
  }

  /// The number of elements of the container at `path`.
  [[nodiscard]] std::optional<std::size_t> node_size(
      const std::string& path) const {
    if (const auto found = container_variables_.find(path);
        found != container_variables_.end()) {
      return found->second.size();
    }
    if (nested_ != nullptr) {
      if (const auto found = nested_->find(path); found != nested_->end()) {
        return found->second.size();
      }
    }
    return std::nullopt;
  }

  /// The language index of the element at `position` (IEEE 1800-2017
  /// 7.4.2, 7.8): an associative array's key, a fixed-size array's index
  /// counted from its left bound.
  [[nodiscard]] std::int64_t index_at(
      const std::string& path, const std::size_t position) const {
    const auto* held = shape(path);
    const auto at = static_cast<std::int64_t>(position);
    if (held == nullptr) return at;
    if (held->associative) {
      if (!held->string_keys && position < held->keys.size()) {
        if (const auto key = held->keys[position].known_unsigned_value()) {
          return static_cast<std::int64_t>(*key);
        }
      }
      return at;
    }
    if (held->fixed) return held->descending ? held->left - at : held->left + at;
    return at;
  }

  /// The position of the element that `selector` selects in the container
  /// at `path`: nothing when the selector is not constant, an empty
  /// position when it selects no element.
  [[nodiscard]] std::optional<std::optional<std::size_t>> select_position(
      const std::string& path, const sv::ConstraintExpression& selector) {
    const auto size = node_size(path).value_or(0U);
    const auto* held = shape(path);
    if (held != nullptr && held->associative && held->string_keys) {
      // A foreach iterator over a string-keyed array holds its key.
      const auto* frame = selector.kind == sv::ConstraintExpressionKind::name
          ? iterator(selector.text) : nullptr;
      // A string state variable holds its value during the call (IEEE
      // 1800-2017 18.5.1, 7.8).
      const std::string* state = nullptr;
      if (frame == nullptr && string_state_ != nullptr
          && selector.kind == sv::ConstraintExpressionKind::name) {
        if (const auto* binding = exact_binding(selector)) {
          if (const auto held_value
              = string_state_->find(binding->canonical_identity);
              held_value != string_state_->end()) {
            state = &held_value->second;
          }
        }
      }
      if (selector.kind != sv::ConstraintExpressionKind::string_literal
          && state == nullptr && (frame == nullptr || !frame->string_key)) {
        return std::nullopt;
      }
      auto text = frame != nullptr && frame->string_key
          ? std::string_view{*frame->string_key}
          : state != nullptr ? std::string_view{*state}
          : std::string_view{selector.text};
      if (frame == nullptr && state == nullptr && text.size() >= 2U
          && text.front() == '"' && text.back() == '"') {
        text = text.substr(1U, text.size() - 2U);
      }
      const auto found = std::ranges::find(held->string_key_values, text);
      if (found == held->string_key_values.end()) {
        return std::optional<std::size_t>{};
      }
      return std::optional<std::size_t>{static_cast<std::size_t>(
          std::distance(held->string_key_values.begin(), found))};
    }
    if (held != nullptr && held->associative) {
      // A wide key compares by value (IEEE 1800-2017 7.8.4).
      std::optional<runtime::PackedLogic4> value;
      if (const auto retained = literal(selector)) {
        value = retained->first;
      } else if (const auto constant = constant_value(selector)) {
        value = low_value(64, static_cast<std::uint64_t>(*constant));
      } else {
        return std::nullopt;
      }
      for (std::size_t position = 0; position < held->keys.size();
           ++position) {
        const auto& key = held->keys[position];
        const auto width = std::max(key.width(), value->width());
        bool equal = true;
        for (std::size_t bit = 0; equal && bit < width; ++bit) {
          const auto left = bit < key.width()
              ? key.get(bit) : runtime::Logic4::zero;
          const auto right = bit < value->width()
              ? value->get(bit) : runtime::Logic4::zero;
          // A negative constant extends past a narrower key's sign.
          equal = left == right
              || (bit >= key.width() && key.width() > 0U
                  && right == key.get(key.width() - 1U));
        }
        if (equal) return std::optional<std::size_t>{position};
      }
      return std::optional<std::size_t>{};
    }
    const auto index = constant_value(selector);
    if (!index) return std::nullopt;
    auto at = *index;
    if (held != nullptr && held->fixed) {
      at = held->descending ? held->left - *index : *index - held->left;
    }
    if (at < 0 || static_cast<std::uint64_t>(at) >= size) {
      return std::optional<std::size_t>{};
    }
    return std::optional<std::size_t>{static_cast<std::size_t>(at)};
  }

  /// The container an expression names: a container property, or an
  /// element of a nested or multi-dimensional one (`a[1]` of
  /// `int a[2][3]`) selected by a constant or loop index. An empty path is
  /// a selection outside the array. Nothing when it names no container.
  [[nodiscard]] std::optional<std::string> aggregate_path(
      const sv::ConstraintExpression& expression) {
    if (expression.kind == sv::ConstraintExpressionKind::name) {
      if (iterator(expression.text) != nullptr) return std::nullopt;
      const auto* binding = exact_binding(expression);
      if (binding == nullptr) return std::nullopt;
      if (node_size(binding->canonical_identity)) {
        return binding->canonical_identity;
      }
      (void)container(binding->canonical_identity);
      return std::nullopt;
    }
    // An array member of a structure element (`a[i].m`, 7.2).
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text.starts_with(member_prefix)
        && expression.operands.size() == 1U) {
      const auto prior_error = error_;
      auto path = member_path(expression);
      if (!path) {
        error_ = prior_error;
        return std::nullopt;
      }
      if (path->empty() || node_size(*path)) return path;
      return std::nullopt;
    }
    if (expression.kind != sv::ConstraintExpressionKind::index
        || expression.operands.size() != 2U) {
      return std::nullopt;
    }
    const auto base = aggregate_path(expression.operands[0]);
    if (!base || base->empty()) return base;
    if (nested_ == nullptr || !nested_->contains(*base)) return std::nullopt;
    const auto position = select_position(*base, expression.operands[1]);
    if (!position) return std::nullopt;
    if (!*position) return std::string{};
    auto child = *base + "[" + std::to_string(**position) + "]";
    if (!node_size(child)) return std::nullopt;
    return child;
  }

  /// The subarrays a selection of a nested container names when an outer
  /// index is random: each path with the guard under which it is
  /// selected, the first whose guard holds applying, and a final
  /// unguarded selection outside the array. Nothing when it names no
  /// container.
  [[nodiscard]] std::optional<
      std::vector<std::pair<std::optional<ExpressionId>, std::string>>>
  aggregate_alternatives(const sv::ConstraintExpression& expression) {
    using Alternatives
        = std::vector<std::pair<std::optional<ExpressionId>, std::string>>;
    if (const auto path = aggregate_path(expression)) {
      return Alternatives{{std::nullopt, *path}};
    }
    if (expression.kind != sv::ConstraintExpressionKind::index
        || expression.operands.size() != 2U) {
      return std::nullopt;
    }
    const auto bases = aggregate_alternatives(expression.operands[0]);
    if (!bases) return std::nullopt;
    std::optional<ExpressionId> selector;
    Alternatives result;
    for (const auto& [guard, base] : *bases) {
      if (base.empty() || nested_ == nullptr || !nested_->contains(base)) {
        if (!base.empty()) return std::nullopt;
        result.emplace_back(guard, std::string{});
        continue;
      }
      const auto both = [&](const ExpressionId selected) {
        return guard
            ? builder_.binary(Operator::LogicalAnd, *guard, selected)
            : selected;
      };
      if (const auto position = select_position(base, expression.operands[1])) {
        auto child = *position
            ? base + "[" + std::to_string(**position) + "]" : std::string{};
        if (!child.empty() && !node_size(child)) return std::nullopt;
        result.emplace_back(guard, std::move(child));
        continue;
      }
      if (!selector) {
        selector = lower(expression.operands[1]);
        if (!selector) return std::nullopt;
      }
      const auto size = node_size(base).value_or(0U);
      for (std::size_t position = 0; position < size; ++position) {
        auto child = base + "[" + std::to_string(position) + "]";
        if (!node_size(child)) return std::nullopt;
        result.emplace_back(both(builder_.binary(Operator::Equal, *selector,
            builder_.constant(low_value(32, static_cast<std::uint64_t>(
                index_at(base, position))), index_profile()))),
            std::move(child));
      }
      result.emplace_back(guard, std::string{});
    }
    return result;
  }

  /// Whether a selection of a nested container, by any indices, names one
  /// of its subarrays rather than an element.
  [[nodiscard]] bool names_subarray(
      const sv::ConstraintExpression& expression) const {
    std::size_t depth = 0;
    const auto* current = &expression;
    while (current->kind == sv::ConstraintExpressionKind::index
           && !current->operands.empty()) {
      current = &current->operands.front();
      ++depth;
    }
    const auto* root = exact_binding(*current);
    if (root == nullptr || nested_ == nullptr) return false;
    auto path = root->canonical_identity;
    for (; depth > 0U; --depth) {
      if (!nested_->contains(path)) return false;
      path += "[0]";
    }
    return node_size(path).has_value();
  }

  /// The element values of an unpacked array an expression names, which
  /// a random outer index selects among (IEEE 1800-2017 7.4.5). Nothing,
  /// with no error, when it names no container of solver variables.
  [[nodiscard]] std::optional<std::vector<ExpressionId>> aggregate_elements(
      const sv::ConstraintExpression& expression) {
    if (expression.kind != sv::ConstraintExpressionKind::name
        && expression.kind != sv::ConstraintExpressionKind::index) {
      return std::nullopt;
    }
    if (expression.kind == sv::ConstraintExpressionKind::name
        && iterator(expression.text) != nullptr) {
      return std::nullopt;
    }
    const auto* root = root_binding(expression);
    if (root == nullptr || !node_size(root->canonical_identity)) {
      return std::nullopt;
    }
    const auto prior_error = error_;
    auto alternatives = aggregate_alternatives(expression);
    error_ = prior_error;
    if (!alternatives) return std::nullopt;
    std::optional<std::size_t> size;
    std::string typed;
    for (const auto& [guard, path] : *alternatives) {
      if (path.empty()) continue;
      if (nested_ != nullptr && nested_->contains(path)) return std::nullopt;
      const auto* elements = container(path);
      if (elements == nullptr) return std::nullopt;
      if (size && *size != elements->size()) {
        error_ = "constraint array selection has subarrays of unequal sizes";
        return std::nullopt;
      }
      size = elements->size();
      if (typed.empty()) typed = path;
    }
    if (!size) return std::nullopt;
    const auto profile = element_profile(expression, typed);
    std::vector<ExpressionId> values;
    for (std::size_t index = 0; index < *size; ++index) {
      std::optional<ExpressionId> value;
      for (auto at = alternatives->size(); at-- > 0U;) {
        const auto& [guard, path] = (*alternatives)[at];
        ExpressionId chosen;
        if (path.empty()) {
          chosen = builder_.constant(
              runtime::PackedLogic4(profile.width, runtime::Logic4::zero),
              profile);
        } else {
          const auto element = (*container(path))[index];
          use(element);
          chosen = variable_node(element, profile);
        }
        value = value && guard
            ? builder_.conditional(*guard, chosen, *value) : chosen;
      }
      values.push_back(*value);
    }
    return values;
  }

  static constexpr std::string_view member_prefix{"@sv-select:"};

  /// The solver's spelling of a spelled member path (`o.a[0].m`): the
  /// class randomizer names each object a rand handle reaches as
  /// `<root property>.<owner class>::<property>...`, so every segment of
  /// the path is matched against the variable names segment by segment,
  /// a segment naming a property of any class (IEEE 1800-2017 18.5.9,
  /// 8.18). The path itself when no variable name matches.
  [[nodiscard]] std::string solver_path(const std::string& spelled) const {
    if (variables_.contains(spelled)
        || container_variables_.contains(spelled)) {
      return spelled;
    }
    const auto split = [](std::string_view text) {
      std::vector<std::string_view> parts;
      while (true) {
        const auto dot = text.find('.');
        if (dot == std::string_view::npos) break;
        parts.push_back(text.substr(0, dot));
        text.remove_prefix(dot + 1U);
      }
      parts.push_back(text);
      return parts;
    };
    const auto wanted = split(spelled);
    if (wanted.size() < 2U) return spelled;
    const auto strip_index = [](std::string_view segment) {
      const auto open = segment.find('[');
      return open == std::string_view::npos ? segment
                                            : segment.substr(0, open);
    };
    const auto matches = [&](const std::string_view have,
                             const std::string_view want, const bool last) {
      // A container named without an index matches its elements and
      // count variables.
      const auto have_name = last && want.find('[') == std::string_view::npos
          ? strip_index(have) : have;
      return have_name == want
          || (have_name.size() > want.size() + 2U
              && have_name.ends_with(want)
              && have_name.substr(
                     have_name.size() - want.size() - 2U, 2U) == "::");
    };
    const std::string head{wanted.front()};
    for (auto candidate = variables_.lower_bound(head);
         candidate != variables_.end()
         && candidate->first.starts_with(head); ++candidate) {
      const auto have = split(candidate->first);
      if (have.size() < wanted.size()) continue;
      bool match = true;
      for (std::size_t index = 0; match && index < wanted.size(); ++index) {
        match = matches(have[index], wanted[index],
            index + 1U == wanted.size());
      }
      if (!match) continue;
      std::string result;
      for (std::size_t index = 0; index < wanted.size(); ++index) {
        if (index != 0U) result += '.';
        result += index + 1U == wanted.size()
                && wanted[index].find('[') == std::string_view::npos
            ? strip_index(have[index]) : have[index];
      }
      return result;
    }
    return spelled;
  }

  /// The variable a member selection names: `a[i].m` of an array of
  /// structures, whose elements' members the class randomizer
  /// materializes as `a[i].m`. An empty path is a selection outside the
  /// array; nothing when it names no such member.
  [[nodiscard]] std::optional<std::string> member_path(
      const sv::ConstraintExpression& expression) {
    if (expression.kind == sv::ConstraintExpressionKind::name) {
      const auto* frame = iterator(expression.text);
      if (frame == nullptr || !frame->item || frame->path.empty()) {
        return std::nullopt;
      }
      return frame->path + "[" + std::to_string(frame->position) + "]";
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text.starts_with(member_prefix)
        && expression.operands.size() == 1U) {
      const auto base = member_path(expression.operands.front());
      if (!base || base->empty()) return base;
      return solver_path(*base + "."
          + expression.text.substr(member_prefix.size()));
    }
    if (expression.kind != sv::ConstraintExpressionKind::index
        || expression.operands.size() != 2U) {
      return std::nullopt;
    }
    const auto base = aggregate_path(expression.operands[0]);
    if (!base || base->empty()) return base;
    const auto position = select_position(*base, expression.operands[1]);
    if (!position) {
      error_ = "constraint structure element selection needs a constant "
          "or loop index";
      return std::nullopt;
    }
    if (!*position) return std::string{};
    return *base + "[" + std::to_string(**position) + "]";
  }

  /// The name an element or container selection starts from.
  [[nodiscard]] const sv::ConstraintBinding* root_binding(
      const sv::ConstraintExpression& expression) const {
    const auto* current = &expression;
    while (current->kind == sv::ConstraintExpressionKind::index
           && !current->operands.empty()) {
      current = &current->operands.front();
    }
    return exact_binding(*current);
  }

  /// The profile of the elements of the container at `path`.
  [[nodiscard]] runtime::SystemVerilogConstraintVariableProfile
  element_profile(const sv::ConstraintExpression& selection,
                  const std::string& path) const {
    const auto* binding = root_binding(selection);
    if (selection.kind == sv::ConstraintExpressionKind::name
        && binding != nullptr) {
      return binding_profile(*binding);
    }
    if (const auto found = container_variables_.find(path);
        found != container_variables_.end() && !found->second.empty()
        && found->second.front() < solver_variables_.size()) {
      return solver_variables_[found->second.front()].profile;
    }
    return binding != nullptr ? binding_profile(*binding) : index_profile();
  }

  struct Iterator {
    std::string name;
    std::size_t index{};
    std::optional<runtime::SystemVerilogConstraintVariableId> element;
    runtime::SystemVerilogConstraintVariableProfile profile;
    bool item{};
    /// The key of a string-keyed associative array element.
    std::optional<std::string> string_key;
    /// A with-clause item's container and element position, which name its
    /// members (`item.kind` is `items[3].kind`).
    std::string path;
    std::size_t position{};
  };

  /// The iterations of `foreach (a[i])`: each element of the container
  /// `a` names, with `i` its index (IEEE 1800-2017 18.5.8.1). An element
  /// that is itself a container is iterated by an inner loop over `a[i]`.
  [[nodiscard]] std::optional<std::vector<Iterator>> foreach_loop(
      const sv::ConstraintExpression& selection) {
    std::string path;
    if (selection.operands[0].kind == sv::ConstraintExpressionKind::name) {
      const auto* binding = exact_binding(selection.operands[0]);
      if (binding == nullptr) {
        error_ = "constraint foreach container has no exact binding";
        return std::nullopt;
      }
      path = binding->canonical_identity;
    } else {
      const auto selected = aggregate_path(selection.operands[0]);
      if (!selected) {
        error_ = "constraint foreach container has no materialized elements";
        return std::nullopt;
      }
      // A loop over an element outside the array has no iterations.
      if (selected->empty()) return std::vector<Iterator>{};
      path = *selected;
    }
    const auto* held = shape(path);
    // The elements of a string-keyed array are iterated in key order,
    // each iterator holding its key (IEEE 1800-2017 7.8.1, 18.5.8.1).
    const auto string_key = [&](const std::size_t position)
        -> std::optional<std::string> {
      if (held == nullptr || !held->associative || !held->string_keys) {
        return std::nullopt;
      }
      return position < held->string_key_values.size()
          ? std::optional<std::string>{held->string_key_values[position]}
          : std::nullopt;
    };
    std::vector<Iterator> loop;
    const auto& name = selection.operands[1].text;
    if (nested_ != nullptr) {
      if (const auto found = nested_->find(path); found != nested_->end()) {
        for (std::size_t position = 0; position < found->second.size();
             ++position) {
          loop.push_back({name,
              static_cast<std::size_t>(index_at(path, position)),
              std::nullopt, index_profile(), false, string_key(position),
              {}, {}});
        }
        return loop;
      }
    }
    const auto* elements = container(path);
    if (elements == nullptr) {
      error_ = "constraint foreach container has no materialized elements";
      return std::nullopt;
    }
    const auto profile = element_profile(selection.operands[0], path);
    for (std::size_t position = 0; position < elements->size(); ++position) {
      loop.push_back({name, static_cast<std::size_t>(index_at(path, position)),
          (*elements)[position], profile, false, string_key(position),
          {}, {}});
    }
    return loop;
  }

  [[nodiscard]] static runtime::SystemVerilogConstraintVariableProfile
  index_profile() {
    return {runtime::SystemVerilogConstraintDomainKind::Integer,
        32, true, "int", false};
  }

  /// The class randomizer's size variables are two-state `int`s.
  [[nodiscard]] static runtime::SystemVerilogConstraintVariableProfile
  size_profile() {
    return index_profile();
  }

  [[nodiscard]] static std::optional<
      runtime::SystemVerilogConstraintVariableProfile>
  integral_cast(const std::string_view type) {
    using Kind = runtime::SystemVerilogConstraintDomainKind;
    if (type == "int") return {{Kind::Integer, 32, true, "int", false}};
    if (type == "integer") return {{Kind::Integer, 32, true, "integer", true}};
    if (type == "shortint") return {{Kind::Integer, 16, true, "shortint", false}};
    if (type == "longint") return {{Kind::Integer, 64, true, "longint", false}};
    if (type == "byte") return {{Kind::Integer, 8, true, "byte", false}};
    if (type == "bit") return {{Kind::BitVector, 1, false, "bit", false}};
    if (type == "logic" || type == "reg") {
      return {{Kind::BitVector, 1, false, "logic", true}};
    }
    return std::nullopt;
  }

  /// A variable read with the declared profile of its binding. The solver
  /// may hold a state variable with another profile; the read converts it.
  [[nodiscard]] ExpressionId variable_node(
      const runtime::SystemVerilogConstraintVariableId variable,
      const runtime::SystemVerilogConstraintVariableProfile& profile) {
    if (variable < solver_variables_.size()) {
      const auto& held = solver_variables_[variable].profile;
      if (held.kind != profile.kind || held.width != profile.width
          || held.signed_value != profile.signed_value
          || held.nominal_type != profile.nominal_type
          || held.four_state != profile.four_state) {
        return builder_.resize(builder_.variable(variable, held), profile);
      }
    }
    return builder_.variable(variable, profile);
  }

  /// Visits each element of the container a reduction or locator method
  /// names (IEEE 1800-2017 7.12): the value of its with-clause, evaluated
  /// with the item bound to the element, else the element. An element that
  /// is a structure is reached only through its members (`item.kind`).
  template <class Visit>
  [[nodiscard]] bool each_item(const sv::ConstraintExpression& call,
      const sv::ConstraintBinding& binding, Visit&& visit) {
    const auto& path = binding.canonical_identity;
    const auto profile = binding_profile(binding);
    const auto* with = call.operands.size() >= 2U
        ? &call.operands.back() : nullptr;
    const std::string item = call.operands.size() == 3U
        ? call.operands[1].text : std::string{"item"};
    const auto* elements = container(path);
    std::size_t count{};
    if (elements != nullptr) {
      count = elements->size();
    } else if (with != nullptr && node_size(path)) {
      count = *node_size(path);
    } else {
      error_ = "constraint container '" + path
          + "' has no element solver variables";
      return false;
    }
    error_.clear();
    for (std::size_t position = 0; position < count; ++position) {
      const auto element = elements != nullptr
          ? std::optional{(*elements)[position]} : std::nullopt;
      std::optional<ExpressionId> value;
      if (with != nullptr) {
        iterators_.push_back({item,
            static_cast<std::size_t>(index_at(path, position)), element,
            profile, true, std::nullopt, path, position});
        value = lower(*with);
        iterators_.pop_back();
        if (!value) return false;
      } else {
        use(*element);
        value = variable_node(*element, profile);
      }
      visit(value);
    }
    return true;
  }

  [[nodiscard]] static bool is_locator(const std::string_view method) {
    return method == ".find" || method == ".find_index"
        || method == ".find_first" || method == ".find_first_index"
        || method == ".find_last" || method == ".find_last_index";
  }

  /// The size of a locator method's result queue (IEEE 1800-2017 7.12.1):
  /// the number of items whose with-clause holds, at most one for the
  /// first and last forms.
  [[nodiscard]] std::optional<ExpressionId> locator_size(
      const sv::ConstraintExpression& locator) {
    const auto* binding = exact_binding(locator.operands.front());
    if (binding == nullptr) {
      error_ = "constraint locator receiver has no exact binding";
      return std::nullopt;
    }
    const auto one = builder_.constant(low_value(32, 1), index_profile());
    const auto zero = builder_.constant(low_value(32, 0), index_profile());
    std::optional<ExpressionId> count;
    const auto visited = each_item(locator, *binding,
        [&](const std::optional<ExpressionId> holds) {
          const auto indicator = builder_.conditional(*holds, one, zero);
          count = count ? builder_.binary(Operator::Add, *count, indicator)
                        : indicator;
        });
    if (!visited) return std::nullopt;
    if (!count) return zero;
    if (locator.text == ".find" || locator.text == ".find_index") {
      return count;
    }
    return builder_.conditional(
        builder_.binary(Operator::NotEqual, *count, zero), one, zero);
  }

  [[nodiscard]] const Iterator* iterator(const std::string_view name) const {
    for (auto frame = iterators_.rbegin(); frame != iterators_.rend();
         ++frame) {
      if (frame->name == name) return &*frame;
    }
    return nullptr;
  }

  void use(const runtime::SystemVerilogConstraintVariableId variable) {
    if (std::ranges::find(dependencies_, variable) == dependencies_.end()) {
      dependencies_.push_back(variable);
    }
  }

  /// The name's binding in this specialization. A specialization made after
  /// the constraints were bound (`C#(16)`) reuses another specialization's
  /// property binding: a property's identity is the same in all of them,
  /// and its profile comes from the solver (see binding_profile).
  [[nodiscard]] const sv::ConstraintBinding* exact_binding(
      const sv::ConstraintExpression& expression) const {
    if (expression.kind != sv::ConstraintExpressionKind::name) return nullptr;
    const auto binding = std::ranges::find(
        expression.bindings, specialization_,
        &sv::ConstraintBinding::specialization_identity);
    if (binding != expression.bindings.end()) return &*binding;
    const auto property = std::ranges::find(expression.bindings,
        sv::ConstraintReferenceKind::property, &sv::ConstraintBinding::kind);
    return property == expression.bindings.end() ? nullptr : &*property;
  }

  /// A value parameter of a specialization made after the constraints were
  /// bound (`C#(5)`): its value is in the specialization identity
  /// (`C<len:NAME=len:value{integer:N};...>`), its profile in the
  /// parameter's binding of another specialization.
  [[nodiscard]] std::optional<ExpressionId> specialization_parameter(
      const sv::ConstraintExpression& expression) {
    const auto parameter = std::ranges::find_if(expression.bindings,
        [](const sv::ConstraintBinding& binding) {
          return binding.kind == sv::ConstraintReferenceKind::parameter
              && !binding.constant_value.empty();
        });
    if (parameter == expression.bindings.end()) return std::nullopt;
    const auto name = expression.text.substr(expression.text.rfind(':') + 1U);
    const auto key = std::to_string(name.size()) + ":" + name + "=";
    const std::string_view identity{specialization_};
    const auto open = identity.rfind('<');
    if (open == std::string_view::npos) return std::nullopt;
    for (auto at = open + 1U; at < identity.size();) {
      const auto colon = identity.find(':', at);
      if (colon == std::string_view::npos) return std::nullopt;
      const auto length = decimal(identity.substr(at, colon - at));
      if (!length) return std::nullopt;
      const auto value_start = colon + 1U + *length + 1U;
      const auto value_colon = identity.find(':', value_start);
      if (value_colon == std::string_view::npos) return std::nullopt;
      const auto value_length
          = decimal(identity.substr(value_start, value_colon - value_start));
      if (!value_length) return std::nullopt;
      const auto value = identity.substr(value_colon + 1U, *value_length);
      if (identity.substr(at, colon + 1U + *length + 1U - at) == key) {
        constexpr std::string_view prefix{"value{integer:"};
        if (!value.starts_with(prefix) || !value.ends_with('}')) {
          return std::nullopt;
        }
        const auto digits
            = value.substr(prefix.size(), value.size() - prefix.size() - 1U);
        std::int64_t number{};
        const auto parsed = std::from_chars(
            digits.data(), digits.data() + digits.size(), number, 10);
        if (parsed.ec != std::errc{}
            || parsed.ptr != digits.data() + digits.size()) {
          return std::nullopt;
        }
        const auto profile = systemverilog_constraint_profile(*parameter);
        return builder_.constant(low_value(profile.width,
            static_cast<std::uint64_t>(number)), profile);
      }
      at = value_colon + 1U + *value_length + 1U;
    }
    return std::nullopt;
  }

  /// A binding's declared profile; a binding of another specialization
  /// takes the profile of the solver variable it names.
  [[nodiscard]] runtime::SystemVerilogConstraintVariableProfile
  binding_profile(const sv::ConstraintBinding& binding) const {
    if (binding.specialization_identity != specialization_) {
      auto variable = variables_.find(binding.canonical_identity);
      if (variable == variables_.end()) {
        variable = variables_.find(binding.canonical_identity + "[0]");
      }
      if (variable != variables_.end()
          && variable->second < solver_variables_.size()) {
        return solver_variables_[variable->second].profile;
      }
    }
    return systemverilog_constraint_profile(binding);
  }

  /// The element variables of a materialized container. A container whose
  /// size is still a variable defers the expression.
  [[nodiscard]] const std::vector<runtime::SystemVerilogConstraintVariableId>*
  container(const std::string& identity) {
    const auto found = container_variables_.find(identity);
    if (found != container_variables_.end()) return &found->second;
    if (variables_.contains(identity + ".size()")) deferred_ = true;
    return nullptr;
  }

  /// The number of set bits of an unsigned 1- to 64-bit value, as an int.
  [[nodiscard]] ExpressionId count_ones(
      const ExpressionId value,
      const runtime::SystemVerilogConstraintVariableProfile& profile) {
    std::optional<ExpressionId> count;
    for (std::size_t bit = 0; bit < profile.width; ++bit) {
      const auto set = builder_.binary(Operator::NotEqual,
          builder_.binary(Operator::BitwiseAnd, value,
              builder_.constant(bit_value(profile.width, bit), profile)),
          builder_.constant(low_value(profile.width, 0), profile));
      const auto counted = builder_.resize(set, index_profile());
      count = count ? builder_.binary(Operator::Add, *count, counted)
                    : counted;
    }
    return *count;
  }

  /// `width` bits of `value` starting at bit `low`.
  [[nodiscard]] ExpressionId extract(const ExpressionId value,
                                     const std::size_t low,
                                     const std::size_t width) {
    auto profile = builder_.profile(value);
    profile.signed_value = false;
    auto shifted = builder_.resize(value, profile);
    if (low > 0U && low < profile.width) {
      runtime::PackedLogic4 scale(profile.width, runtime::Logic4::zero);
      scale.set(low, runtime::Logic4::one);
      shifted = builder_.binary(Operator::Divide, shifted,
          builder_.constant(std::move(scale), profile));
    }
    auto result = profile;
    result.kind = runtime::SystemVerilogConstraintDomainKind::BitVector;
    result.width = width;
    result.nominal_type = profile.four_state ? "logic" : "bit";
    return builder_.resize(shifted, result);
  }

  /// A constant bit- or part-select of a packed operand.
  [[nodiscard]] std::optional<ExpressionId> packed_select(
      const sv::ConstraintExpression& expression) {
    const auto value = lower(expression.operands[0]);
    if (!value) return std::nullopt;
    const auto width = builder_.profile(*value).width;
    // Bit positions count from the declared range's least significant
    // index.
    std::int64_t right = 0;
    bool descending = true;
    if (const auto* binding = exact_binding(expression.operands[0]);
        binding != nullptr && binding->type.packed_range
        && binding->type.packed_range->left
        && binding->type.packed_range->right) {
      right = *binding->type.packed_range->right;
      descending = *binding->type.packed_range->left
          >= *binding->type.packed_range->right;
    }
    const auto position = [&](const std::int64_t index) {
      return descending ? index - right : right - index;
    };
    std::int64_t low{};
    std::int64_t high{};
    if (expression.kind == sv::ConstraintExpressionKind::index) {
      const auto index = constant_value(expression.operands[1]);
      if (!index) {
        error_ = "constraint bit-select index is not constant";
        return std::nullopt;
      }
      low = high = position(*index);
    } else {
      const auto first = constant_value(expression.operands[1]);
      const auto second = constant_value(expression.operands[2]);
      if (!first || !second) {
        error_ = "constraint part-select bounds are not constant";
        return std::nullopt;
      }
      if (expression.text == "+:" || expression.text == "-:") {
        const auto start = position(*first);
        const auto span = *second;
        if (span <= 0) {
          error_ = "constraint part-select width is not positive";
          return std::nullopt;
        }
        const bool upward = (expression.text == "+:") == descending;
        low = upward ? start : start - span + 1;
        high = upward ? start + span - 1 : start;
      } else {
        low = std::min(position(*first), position(*second));
        high = std::max(position(*first), position(*second));
      }
    }
    if (low < 0 || high < low
        || static_cast<std::uint64_t>(high) >= width) {
      error_ = "constraint select is outside its operand";
      return std::nullopt;
    }
    return extract(*value, static_cast<std::size_t>(low),
        static_cast<std::size_t>(high - low + 1));
  }

  /// The value of an index expression known while lowering: literals,
  /// parameters, loop indices and materialized sizes.
  [[nodiscard]] std::optional<std::int64_t> constant_value(
      const sv::ConstraintExpression& expression) {
    if (const auto retained = literal(expression)) {
      const auto value = retained->first.known_unsigned_value();
      if (!value || *value > static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
      }
      return static_cast<std::int64_t>(*value);
    }
    if (expression.kind == sv::ConstraintExpressionKind::name) {
      if (const auto* frame = iterator(expression.text);
          frame != nullptr && !frame->item) {
        if (frame->string_key) return std::nullopt;
        return static_cast<std::int64_t>(frame->index);
      }
      if (const auto dot = expression.text.rfind('.');
          dot != std::string::npos
          && expression.text.substr(dot) == ".index") {
        if (const auto* frame = iterator(
                std::string_view{expression.text}.substr(0, dot));
            frame != nullptr && frame->item) {
          return static_cast<std::int64_t>(frame->index);
        }
      }
      const auto* binding = exact_binding(expression);
      if (binding == nullptr) return std::nullopt;
      if (pinned_ && binding->canonical_identity == pinned_->identity) {
        return pinned_->number;
      }
      // A state variable holds its value during the call.
      if (binding->constant_value.empty()) {
        const auto variable = variables_.find(binding->canonical_identity);
        if (variable == variables_.end()
            || variable->second >= solver_variables_.size()
            || solver_variables_[variable->second].domain.size() != 1U) {
          return std::nullopt;
        }
        const auto& held = solver_variables_[variable->second];
        const auto& state = held.domain.front();
        if (state.width() == 0U || state.width() > 64U || state.is_logic9()
            || state.low_word().bval != 0U) {
          return std::nullopt;
        }
        auto word = state.low_word().aval;
        if (held.profile.signed_value && state.width() < 64U
            && ((word >> (state.width() - 1U)) & 1U) != 0U) {
          word |= ~((std::uint64_t{1} << state.width()) - 1U);
        }
        return static_cast<std::int64_t>(word);
      }
      const auto value = decimal(binding->constant_value);
      if (!value || *value > static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
      }
      return static_cast<std::int64_t>(*value);
    }
    if (expression.kind == sv::ConstraintExpressionKind::call
        && expression.text == ".size" && expression.operands.size() == 1U) {
      if (expression.operands.front().kind
          == sv::ConstraintExpressionKind::index) {
        const auto path = aggregate_path(expression.operands.front());
        if (!path) return std::nullopt;
        return static_cast<std::int64_t>(node_size(*path).value_or(0U));
      }
      const auto* binding = exact_binding(expression.operands.front());
      if (binding == nullptr) return std::nullopt;
      const auto size = node_size(binding->canonical_identity);
      if (!size) return std::nullopt;
      return static_cast<std::int64_t>(*size);
    }
    if (expression.kind == sv::ConstraintExpressionKind::unary
        && expression.operands.size() == 1U
        && (expression.text == "-" || expression.text == "+")) {
      const auto value = constant_value(expression.operands.front());
      if (!value) return std::nullopt;
      return expression.text == "-" ? -*value : *value;
    }
    if (expression.kind == sv::ConstraintExpressionKind::binary
        && expression.operands.size() == 2U) {
      const auto left = constant_value(expression.operands[0]);
      const auto right = constant_value(expression.operands[1]);
      if (!left || !right) return std::nullopt;
      constexpr std::int64_t bound = std::int64_t{1} << 40;
      if (*left > bound || *left < -bound || *right > bound
          || *right < -bound) {
        return std::nullopt;
      }
      if (expression.text == "+") return *left + *right;
      if (expression.text == "-") return *left - *right;
      if (expression.text == "*") return *left * *right;
      if ((expression.text == "/" || expression.text == "%") && *right != 0) {
        return expression.text == "/" ? *left / *right : *left % *right;
      }
    }
    return std::nullopt;
  }

  std::string_view specialization_;
  const std::map<std::string,
                 runtime::SystemVerilogConstraintVariableId>& variables_;
  const std::map<std::string, std::vector<
      runtime::SystemVerilogConstraintVariableId>>& container_variables_;
  std::vector<runtime::SystemVerilogConstraintVariableId>& dependencies_;
  std::string& error_;
  std::vector<Iterator> iterators_;
  std::vector<Guard> guards_;
  // A random variable a dist item names, at one of its values.
  struct Pinned {
    std::string identity;
    std::int64_t number{};
    const sv::ConstraintExpression* source{};
    runtime::PackedLogic4 value;
    runtime::SystemVerilogConstraintVariableProfile profile;
  };
  std::optional<Pinned> pinned_;
  bool deferred_{};
  std::span<const runtime::SystemVerilogConstraintVariable> solver_variables_;
  const std::map<std::string, std::vector<std::string>>* nested_{};
  const std::map<std::string, std::string, std::less<>>* string_state_{};
  const runtime::SystemVerilogConstraintSolver* shapes_{};
  std::string shape_prefix_;
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
    const auto sampled_weight = weight
        ? weight->first.known_unsigned_value()
        : std::nullopt;
    if (!sampled_weight) {
        error = "dist weight is not a known host-representable sampling integer";
        return std::nullopt;
    }
    // An unpacked array item contributes each element's value with the
    // item's weight (IEEE 1800-2017 18.5.4, 11.4.13).
    if (item.operands[0].kind
            == semantic::sv::ConstraintExpressionKind::name) {
      const auto array_binding = std::ranges::find(
          item.operands[0].bindings, specialization_identity,
          &semantic::sv::ConstraintBinding::specialization_identity);
      if (array_binding != item.operands[0].bindings.end()
          && variables.contains(array_binding->canonical_identity + "[]")) {
        for (std::size_t element = 0;; ++element) {
          const auto found = variables.find(array_binding->canonical_identity
              + "[" + std::to_string(element) + "]");
          if (found == variables.end()) break;
          if (found->second >= solver_variables.size()
              || solver_variables[found->second].domain.size() != 1U) {
            error = "dist array item is not a state array";
            return std::nullopt;
          }
          const auto& value = solver_variables[found->second].domain.front();
          runtime::PackedLogic4 resized(profile.width,
              solver_variables[found->second].profile.signed_value
                      && !value.empty()
                  ? value.get(value.width() - 1U) : runtime::Logic4::zero);
          for (std::size_t bit = 0;
               bit < std::min(profile.width, value.width()); ++bit) {
            resized.set(bit, value.get(bit));
          }
          runtime::SystemVerilogConstraintDistributionEntry entry;
          entry.weight = *sampled_weight;
          entry.weight_kind
              = runtime::SystemVerilogConstraintDistributionWeight::PerValue;
          entry.low = resized;
          entry.high = std::move(resized);
          distribution.entries.push_back(std::move(entry));
        }
        continue;
      }
    }
    runtime::SystemVerilogConstraintDistributionEntry retained;
    retained.weight = *sampled_weight;
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
      // An element select (`solve a[0] before y`) or another item without
      // an exact binding orders nothing here; the ordering never changes
      // the solution set (IEEE 1800-2017 18.5.10).
      if (item.kind != semantic::sv::ConstraintExpressionKind::name) {
        continue;
      }
      const auto binding = std::ranges::find(
          item.bindings, specialization_identity,
          &semantic::sv::ConstraintBinding::specialization_identity);
      if (binding == item.bindings.end()) return std::nullopt;
      const auto variable = variables.find(binding->canonical_identity);
      if (variable != variables.end()) {
        result.push_back(variable->second);
        continue;
      }
      // An array item orders every element variable of the array
      // (IEEE 1800-2017 18.5.10).
      const auto prefix = binding->canonical_identity + "[";
      bool element { };
      for (auto candidate = variables.lower_bound(prefix);
           candidate != variables.end()
           && candidate->first.starts_with(prefix);
           ++candidate) {
        result.push_back(candidate->second);
        element = true;
      }
      // An item with no materialized variables orders nothing; the
      // ordering never changes the solution set (18.5.10).
      static_cast<void>(element);
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

void configure_class_constraints(
    runtime::SystemVerilogConstraintSolver& solver,
    const runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::Hir& hir,
    const semantic::sv::ClassSpecialization& specialization,
    const std::function<bool(std::string_view)>& constraint_enabled,
    const std::string_view identity_prefix,
    const std::map<std::string, std::string, std::less<>>& string_state) {
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
  // The class randomizer materializes container `c` as `c[]` and its
  // elements `c[0]`, `c[1]`, ...
  // A nested container `c[1]` has its own count `c[1][]`; its parent
  // lists it among its element containers.
  std::map<std::string, std::vector<runtime::SystemVerilogConstraintVariableId>>
      container_variables;
  std::map<std::string, std::vector<std::string>> nested_containers;
  for (const auto& [identity, id] : variables) {
    if (!identity.ends_with("[]")) continue;
    const auto base = identity.substr(0, identity.size() - 2U);
    std::vector<runtime::SystemVerilogConstraintVariableId> elements;
    std::vector<std::string> children;
    // The count bounds the elements: the objects of an array of handles
    // past its solved size keep their variables as state.
    std::size_t count = std::numeric_limits<std::size_t>::max();
    if (id < solver.variables().size()
        && solver.variables()[id].domain.size() == 1U) {
      count = static_cast<std::size_t>(
          solver.variables()[id].domain.front().low_word().aval);
    }
    for (std::size_t index = 0; index < count; ++index) {
      auto element_name = base + "[" + std::to_string(index) + "]";
      if (const auto element = variables.find(element_name);
          element != variables.end()) {
        elements.push_back(element->second);
      } else if (variables.contains(element_name + "[]")) {
        children.push_back(std::move(element_name));
      } else if (const auto member = variables.lower_bound(element_name + ".");
                 member != variables.end()
                 && member->first.starts_with(element_name + ".")) {
        // An element of an array of structures: its members `c[0].m`.
        children.push_back(std::move(element_name));
      } else {
        break;
      }
    }
    if (children.empty()) {
      container_variables.emplace(base, std::move(elements));
    } else {
      nested_containers.emplace(base, std::move(children));
    }
  }
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
      // Objects of one class reached along different paths each add their
      // constraints; the path keeps the clause identities distinct.
      const auto identity = std::string{identity_prefix}
          + constraint->canonical_identity + "::" + std::to_string(index);
      std::string error;
      if (expression.kind
          == semantic::sv::ConstraintExpressionKind::distribution) {
        auto distribution = lower_systemverilog_constraint_distribution(
            expression, specialization.specialization_identity, variables,
            solver.variables(), identity, error);
        // Other subjects and bounds take the general path below.
        if (distribution) {
          solver.add_distribution(std::move(*distribution));
          continue;
        }
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
      // `disable soft v` discards the lower-priority (earlier) soft
      // constraints on v (IEEE 1800-2017 18.5.14.2).
      if (expression.kind == semantic::sv::ConstraintExpressionKind::call
          && expression.text == "@disable-soft" && expression.operands.size() == 1U) {
        if (!lower_systemverilog_constraint_expression(
                expression.operands.front(), specialization.specialization_identity,
                variables, dependencies, error)) {
          throw std::invalid_argument{
              "cannot lower class constraint '" + identity + "': " + error};
        }
        solver.discard_soft_clauses(dependencies);
        continue;
      }
      Lowerer lowerer{specialization.specialization_identity, variables,
          container_variables, dependencies, error, solver.variables()};
      lowerer.set_container_shapes(
          &nested_containers, &solver, identity_prefix);
      lowerer.set_string_state(&string_state);
      std::vector<Lowerer::Clause> clauses;
      if (!lowerer.lower_clauses(expression, clauses)) {
        throw std::invalid_argument{
            "cannot lower class constraint '" + identity
            + "': " + error};
      }
      for (std::size_t part = 0; part < clauses.size(); ++part) {
        if (clauses[part].distribution) {
          // A guard fixed by state variables decides the distribution and
          // its membership; another guard keeps only the membership.
          const auto holds = solver.fixed_truth(clauses[part].expression);
          if (!holds) {
            // A random guard weights the variable where it holds; the
            // guarded membership clause follows.
            auto distribution = std::move(*clauses[part].distribution);
            distribution.canonical_identity
                = identity + "::" + std::to_string(part);
            distribution.guard = std::make_shared<
                const runtime::SystemVerilogConstraintExpression>(
                std::move(clauses[part].expression));
            solver.add_distribution(std::move(distribution));
            continue;
          }
          if (*holds) {
            clauses[part].distribution->canonical_identity
                = identity + "::" + std::to_string(part);
            solver.add_distribution(std::move(*clauses[part].distribution));
          }
          ++part;
          continue;
        }
        if (clauses[part].discard) {
          const auto holds = solver.fixed_truth(clauses[part].expression);
          if (!holds) {
            throw std::invalid_argument{
                "cannot lower class constraint '" + identity
                + "': a disable soft guard depends on random variables"};
          }
          if (*holds) solver.discard_soft_clauses(clauses[part].discarded);
          continue;
        }
        auto clause = runtime::systemverilog_constraint_expression_clause(
            part == 0U ? identity : identity + "::" + std::to_string(part),
            std::move(clauses[part].dependencies),
            std::move(clauses[part].expression));
        clause.soft = clauses[part].soft;
        solver.add_clause(std::move(clause));
      }
    }
  }
}

void configure_systemverilog_class_constraints(
    runtime::SystemVerilogConstraintSolver& solver,
    const runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::Hir& hir,
    const semantic::sv::ClassSpecialization& specialization,
    const std::function<bool(std::string_view)>& constraint_enabled,
    const std::string_view identity_prefix,
    const std::map<std::string, std::string, std::less<>>& string_state) {
  configure_class_constraints(
      solver, variables, hir, specialization, constraint_enabled,
      identity_prefix, string_state);
}

}  // namespace fsim::app::application_detail
