// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <charconv>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] std::size_t fixed_element_count(
    const ContainerType& type) {
  if (!type.dimensions.empty()) {
    std::size_t count = 1;
    for (const auto& dimension : type.dimensions) {
      const auto distance =
          dimension.first >= dimension.second
              ? static_cast<std::int64_t>(dimension.first)
                    - dimension.second
              : static_cast<std::int64_t>(dimension.second)
                    - dimension.first;
      count *= static_cast<std::size_t>(distance + 1);
    }
    return count;
  }
  const auto distance =
      type.index_left >= type.index_right
          ? static_cast<std::int64_t>(type.index_left)
                - type.index_right
          : static_cast<std::int64_t>(type.index_right)
                - type.index_left;
  return static_cast<std::size_t>(distance + 1);
}

[[nodiscard]] std::size_t fixed_offset(
    const ContainerType& type,
    const std::int32_t index) {
  return static_cast<std::size_t>(
      type.index_left >= type.index_right
          ? static_cast<std::int64_t>(type.index_left) - index
          : static_cast<std::int64_t>(index) - type.index_left);
}

[[noreturn]] void container_error(
    const ProcessId process,
    const InstructionIndex instruction,
    const std::string_view message) {
  throw InterpreterError{
      process, instruction, std::string{message}};
}

[[nodiscard]] std::size_t known_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value,
    const bool signed_index,
    const std::string_view role) {
  const auto word = value.low_word();
  if (word.width == 0 || word.width > 64 || word.bval != 0) {
    container_error(
        process, instruction,
        std::string{role}
            + " must be a known integral value");
  }
  if (signed_index && word.width <= 32) {
    const auto signed_value = static_cast<std::int64_t>(
        static_cast<std::int32_t>(
            static_cast<std::uint32_t>(word.aval)));
    if (signed_value < 0) {
      container_error(
          process, instruction,
          std::string{role} + " cannot be negative");
    }
    return static_cast<std::size_t>(signed_value);
  }
  if (word.aval
      > static_cast<std::uint64_t>(
          std::numeric_limits<std::size_t>::max())) {
    container_error(
        process, instruction,
        std::string{role} + " is too large");
  }
  return static_cast<std::size_t>(word.aval);
}

[[nodiscard]] std::int32_t known_fixed_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value) {
  const auto word = value.low_word();
  if (word.width == 0 || word.width > 32 || word.bval != 0) {
    container_error(
        process, instruction,
        "static-array index must be a known 32-bit integral value");
  }
  return static_cast<std::int32_t>(
      static_cast<std::uint32_t>(word.aval));
}

[[nodiscard]] std::size_t fixed_offset(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value) {
  const auto index =
      known_fixed_index(process, instruction, value);
  const auto low = std::min(type.index_left, type.index_right);
  const auto high = std::max(type.index_left, type.index_right);
  if (index < low || index > high) {
    container_error(
        process, instruction,
        "static-array index is out of range");
  }
  return static_cast<std::size_t>(
      type.index_left >= type.index_right
          ? static_cast<std::int64_t>(type.index_left) - index
          : static_cast<std::int64_t>(index) - type.index_left);
}

void require_same_type(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& target,
    const ContainerType& source) {
  if (target != source) {
    container_error(
        process, instruction, "container value type mismatch");
  }
}

void require_queue(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value) {
  if (!value.type.queue) {
    container_error(
        process, instruction,
        value.type.associative
            ? "queue method used on an associative array"
            : "queue method used on a dynamic array");
  }
}

void require_associative(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::string_view operation) {
  if (!value.type.associative) {
    container_error(
        process, instruction,
        std::string{operation}
            + " requires an associative array");
  }
}

[[nodiscard]] PackedLogic4 associative_key(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value) {
  const auto word = value.low_word();
  if (word.width != type.index_width || word.bval != 0) {
    container_error(
        process, instruction,
        word.bval != 0
            ? "associative-array index must be a known integral value"
            : "associative-array index type mismatch");
  }
  return PackedLogic4::from_aval_bval(
      type.index_width, word.aval, 0);
}

[[nodiscard]] bool key_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right) {
  const auto lhs = left.low_word().aval;
  const auto rhs = right.low_word().aval;
  if (type.signed_indices) {
    const auto sign = UINT64_C(1) << (type.index_width - 1U);
    const auto lhs_negative = (lhs & sign) != 0;
    const auto rhs_negative = (rhs & sign) != 0;
    if (lhs_negative != rhs_negative) {
      return lhs_negative;
    }
  }
  return lhs < rhs;
}

[[nodiscard]] std::size_t lower_key(
    const ContainerValue& value,
    const PackedLogic4& key) {
  return static_cast<std::size_t>(
      std::lower_bound(
          value.keys.begin(), value.keys.end(), key,
          [&](const PackedLogic4& candidate,
              const PackedLogic4& sought) {
            return key_less(value.type, candidate, sought);
          })
      - value.keys.begin());
}

[[nodiscard]] bool key_equal(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  return left.low_word().aval == right.low_word().aval;
}

[[nodiscard]] bool element_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right) {
  for (std::size_t index = type.element_width;
       index-- != 0;) {
    const bool sign =
        type.signed_elements
        && index == type.element_width - 1U;
    const auto rank =
        [sign](const Logic4 bit) -> std::uint8_t {
          if (!sign) {
            return static_cast<std::uint8_t>(bit);
          }
          switch (bit) {
          case Logic4::one:
            return 0;
          case Logic4::zero:
            return 1;
          case Logic4::x:
            return 2;
          case Logic4::z:
            return 3;
          }
          return 3;
        };
    const auto left_rank = rank(left.get(index));
    const auto right_rank = rank(right.get(index));
    if (left_rank != right_rank) {
      return left_rank < right_rank;
    }
  }
  return false;
}

[[nodiscard]] PackedLogic4 evaluate_container_predicate(
    const ContainerType& type,
    const PackedLogic4& item,
    const std::int32_t iterator_index,
    const std::span<const ContainerPredicateNode> predicate) {
  if (predicate.empty()
      || predicate.size() > maximum_container_predicate_nodes) {
    throw std::invalid_argument{
        "SimIR container predicate node count is invalid"};
  }
  std::vector<PackedLogic4> values;
  std::vector<ContainerPredicateValueKind> value_kinds;
  values.reserve(predicate.size());
  value_kinds.reserve(predicate.size());
  for (std::size_t index = 0; index < predicate.size(); ++index) {
    const auto& node = predicate[index];
    const auto operand =
        [&](const std::uint32_t id) -> const PackedLogic4& {
          if (id >= index) {
            throw std::invalid_argument{
                "SimIR container predicate operand is not earlier"};
          }
          return values[id];
        };
    switch (node.operation) {
    case ContainerPredicateOperator::item:
      if (node.value_kind
          != ContainerPredicateValueKind::element) {
        throw std::invalid_argument{
            "SimIR container predicate item has the wrong type"};
      }
      values.push_back(item);
      value_kinds.push_back(node.value_kind);
      break;
    case ContainerPredicateOperator::index:
      if (node.value_kind
          != ContainerPredicateValueKind::index) {
        throw std::invalid_argument{
            "SimIR container predicate index has the wrong type"};
      }
      values.push_back(PackedLogic4::from_aval_bval(
          32, static_cast<std::uint32_t>(iterator_index), 0));
      value_kinds.push_back(node.value_kind);
      break;
    case ContainerPredicateOperator::constant:
      if (node.value_kind == ContainerPredicateValueKind::logical
          || node.constant.width()
              != (node.value_kind
                          == ContainerPredicateValueKind::index
                      ? 32U
                      : type.element_width)
          || node.constant.is_logic9()
          || ((node.value_kind
                       == ContainerPredicateValueKind::index
                   || type.two_state)
              && node.constant.low_word().bval != 0)) {
        throw std::invalid_argument{
            "SimIR container predicate constant type mismatch"};
      }
      values.push_back(node.constant);
      value_kinds.push_back(node.value_kind);
      break;
    case ContainerPredicateOperator::equal:
    case ContainerPredicateOperator::not_equal:
    case ContainerPredicateOperator::less:
    case ContainerPredicateOperator::less_equal:
    case ContainerPredicateOperator::greater:
    case ContainerPredicateOperator::greater_equal: {
      const auto& left = operand(node.left);
      const auto& right = operand(node.right);
      if (node.value_kind
              != ContainerPredicateValueKind::logical
          || value_kinds[node.left]
              != value_kinds[node.right]
          || value_kinds[node.left]
              == ContainerPredicateValueKind::logical) {
        throw std::invalid_argument{
            "SimIR container predicate comparison type mismatch"};
      }
      const bool signed_comparison =
          value_kinds[node.left]
              == ContainerPredicateValueKind::index
          || type.signed_elements;
      auto operation = BinaryOperator::equal;
      if (node.operation == ContainerPredicateOperator::not_equal) {
        operation = BinaryOperator::not_equal;
      } else if (node.operation == ContainerPredicateOperator::less) {
        operation = signed_comparison
            ? BinaryOperator::less_signed
            : BinaryOperator::less_unsigned;
      } else if (
          node.operation == ContainerPredicateOperator::less_equal) {
        operation = signed_comparison
            ? BinaryOperator::less_equal_signed
            : BinaryOperator::less_equal_unsigned;
      } else if (
          node.operation == ContainerPredicateOperator::greater) {
        operation = signed_comparison
            ? BinaryOperator::greater_signed
            : BinaryOperator::greater_unsigned;
      } else if (
          node.operation == ContainerPredicateOperator::greater_equal) {
        operation = signed_comparison
            ? BinaryOperator::greater_equal_signed
            : BinaryOperator::greater_equal_unsigned;
      }
      values.push_back(binary_value(operation, left, right));
      value_kinds.push_back(node.value_kind);
      break;
    }
    case ContainerPredicateOperator::logical_and:
    case ContainerPredicateOperator::logical_or:
      if (node.value_kind
              != ContainerPredicateValueKind::logical
          || node.left >= index || node.right >= index) {
        throw std::invalid_argument{
            "SimIR container predicate logical type mismatch"};
      }
      values.push_back(logical_binary(
          node.operation
                  == ContainerPredicateOperator::logical_and
              ? LogicalBinaryOperator::logical_and
              : LogicalBinaryOperator::logical_or,
          operand(node.left), operand(node.right)));
      value_kinds.push_back(node.value_kind);
      break;
    case ContainerPredicateOperator::logical_not:
      if (node.value_kind
              != ContainerPredicateValueKind::logical
          || node.left >= index) {
        throw std::invalid_argument{
            "SimIR container predicate logical type mismatch"};
      }
      values.push_back(logical_not(operand(node.left)));
      value_kinds.push_back(node.value_kind);
      break;
    case ContainerPredicateOperator::conditional: {
      const auto& condition = operand(node.left);
      const auto& when_true = operand(node.right);
      const auto& when_false = operand(node.third);
      if (node.value_kind
              != ContainerPredicateValueKind::element
          || value_kinds[node.right]
              != ContainerPredicateValueKind::element
          || value_kinds[node.third]
              != ContainerPredicateValueKind::element) {
        throw std::invalid_argument{
            "SimIR container conditional type mismatch"};
      }
      values.push_back(conditional_value(
          PackedLogic4{1, truth_value(condition)},
          when_true, when_false));
      value_kinds.push_back(node.value_kind);
      break;
    }
    default:
      throw std::invalid_argument{
          "invalid SimIR container predicate operator"};
    }
  }
  return values.back();
}

}  // namespace

void validate_container_value(const ContainerValue& value) {
  if (value.type.element_width == 0
      || value.type.element_width > 64) {
    throw std::invalid_argument{
        "SimIR container element width must be in 1..64"};
  }
  if (value.elements.size() > maximum_container_elements
      || (value.type.maximum_elements
          && value.elements.size()
              > *value.type.maximum_elements)) {
    throw std::length_error{
        "SimIR container exceeds its element limit"};
  }
  if (!value.type.queue && value.type.maximum_elements) {
    throw std::invalid_argument{
        "a non-queue container cannot have a queue bound"};
  }
  if (static_cast<unsigned>(value.type.queue)
          + static_cast<unsigned>(value.type.associative)
          + static_cast<unsigned>(value.type.fixed)
      > 1U) {
    throw std::invalid_argument{
        "a SimIR container kind must be unambiguous"};
  }
  if (value.type.fixed) {
    const auto count = fixed_element_count(value.type);
    if (count == 0 || count > maximum_container_elements) {
      throw std::length_error{
          "SimIR static array exceeds its element limit"};
    }
    if (value.elements.size() != count) {
      throw std::invalid_argument{
          "SimIR static-array storage does not match its declared range"};
    }
  }
  if (value.type.associative) {
    if (value.type.index_width == 0
        || value.type.index_width > 64) {
      throw std::invalid_argument{
          "SimIR associative-array index width must be in 1..64"};
    }
    if (value.keys.size() != value.elements.size()) {
      throw std::invalid_argument{
          "SimIR associative-array keys and elements must be paired"};
    }
    for (std::size_t index = 0; index < value.keys.size(); ++index) {
      const auto& key = value.keys[index];
      if (key.width() != value.type.index_width
          || key.is_logic9()
          || key.low_word().bval != 0) {
        throw std::invalid_argument{
            "SimIR associative-array key does not match its type"};
      }
      if (index != 0
          && !key_less(value.type, value.keys[index - 1], key)) {
        throw std::invalid_argument{
            "SimIR associative-array keys must be unique and ordered"};
      }
    }
  } else if (!value.keys.empty()) {
    throw std::invalid_argument{
        "non-associative SimIR containers cannot contain keys"};
  }
  for (const auto& element : value.elements) {
    if (element.width() != value.type.element_width
        || element.is_logic9()
        || (value.type.two_state
            && element.low_word().bval != 0)) {
      throw std::invalid_argument{
          "SimIR container element does not match its type"};
    }
  }
}

const ContainerValue&
Interpreter::Impl::read_container_object_value(
    const ContainerObjectId id) {
  auto& object = get_container_object(id);
  if (!object.slice_alias) {
    return object.initial_value;
  }
  const auto& alias = *object.slice_alias;
  const auto& source =
      read_container_object_value(alias.object);
  const auto count =
      object.initial_value.elements.size();
  const auto descending =
      alias.selected_left >= alias.selected_right;
  for (std::size_t ordinal = 0;
       ordinal < count;
       ++ordinal) {
    const auto selected_index =
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                   ? -static_cast<std::int64_t>(ordinal)
                   : static_cast<std::int64_t>(ordinal)));
    object.initial_value.elements[ordinal] =
        source.elements[
            fixed_offset(source.type, selected_index)];
  }
  return object.initial_value;
}

void Interpreter::Impl::write_container_object_value(
    const ContainerObjectId id,
    const ContainerValue& value) {
  validate_container_value(value);
  auto& object = get_container_object(id);
  if (object.initial_value.type != value.type) {
    throw std::invalid_argument{
        "container object write type mismatch"};
  }
  if (!object.slice_alias) {
    object.initial_value = value;
    return;
  }
  const auto alias = *object.slice_alias;
  auto replacement =
      read_container_object_value(alias.object);
  const auto descending =
      alias.selected_left >= alias.selected_right;
  for (std::size_t ordinal = 0;
       ordinal < value.elements.size();
       ++ordinal) {
    const auto selected_index =
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                   ? -static_cast<std::int64_t>(ordinal)
                   : static_cast<std::int64_t>(ordinal)));
    replacement.elements[
        fixed_offset(replacement.type, selected_index)] =
        value.elements[ordinal];
  }
  write_container_object_value(
      alias.object, replacement);
  object.initial_value = value;
}

PackedLogic4 default_container_element(
    const ContainerType& type) {
  return PackedLogic4::from_aval_bval(
      type.element_width, 0, 0);
}

ContainerValue default_container_value(
    const ContainerType& type) {
  ContainerValue result;
  result.type = type;
  if (type.fixed) {
    const auto count = fixed_element_count(type);
    if (count > maximum_container_elements) {
      throw std::length_error{
          "SimIR static array exceeds its element limit"};
    }
    if (type.element_width == 0
        || type.element_width > 64) {
      throw std::invalid_argument{
          "SimIR container element width must be in 1..64"};
    }
    const auto initial =
        type.two_state
            ? default_container_element(type)
            : PackedLogic4{type.element_width, Logic4::x};
    result.elements.assign(count, initial);
  }
  validate_container_value(result);
  return result;
}

void select_container_value(
    ContainerValue& destination,
    const PackedLogic4& condition,
    const ContainerValue& when_true,
    const ContainerValue& when_false) {
  validate_container_value(destination);
  validate_container_value(when_true);
  validate_container_value(when_false);
  if (destination.type != when_true.type
      || destination.type != when_false.type) {
    throw std::invalid_argument{
        "container conditional profiles differ"};
  }
  if (condition.width() != 1) {
    throw std::invalid_argument{
        "container conditional condition must be scalar"};
  }
  const auto state = condition.get(0);
  if (state == Logic4::one || state == Logic4::zero) {
    destination = state == Logic4::one ? when_true : when_false;
    return;
  }
  if (when_true.elements.size() != when_false.elements.size()
      || when_true.keys != when_false.keys) {
    destination = default_container_value(destination.type);
    return;
  }
  destination.keys = when_true.keys;
  destination.elements.clear();
  destination.elements.reserve(when_true.elements.size());
  for (std::size_t index = 0;
       index < when_true.elements.size(); ++index) {
    auto merged = conditional_value(
        condition,
        when_true.elements[index],
        when_false.elements[index]);
    if (destination.type.two_state) {
      const auto word = merged.low_word();
      merged = PackedLogic4::from_aval_bval(
          destination.type.element_width,
          word.aval & ~word.bval,
          0);
    }
    destination.elements.push_back(std::move(merged));
  }
}

PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    const bool case_equal) {
  validate_container_value(lhs);
  validate_container_value(rhs);
  if (lhs.type != rhs.type) {
    throw std::invalid_argument{
        "container equality profiles differ"};
  }
  if (lhs.elements.size() != rhs.elements.size()
      || lhs.keys != rhs.keys) {
    return PackedLogic4(1, Logic4::zero);
  }
  bool unknown{};
  for (std::size_t element = 0;
       element < lhs.elements.size(); ++element) {
    const auto& left = lhs.elements[element];
    const auto& right = rhs.elements[element];
    for (std::size_t bit = 0; bit < lhs.type.element_width; ++bit) {
      const auto left_bit = left.get(bit);
      const auto right_bit = right.get(bit);
      if (case_equal) {
        if (left_bit != right_bit) {
          return PackedLogic4(1, Logic4::zero);
        }
        continue;
      }
      const auto left_known =
          left_bit == Logic4::zero || left_bit == Logic4::one;
      const auto right_known =
          right_bit == Logic4::zero || right_bit == Logic4::one;
      if (left_known && right_known && left_bit != right_bit) {
        return PackedLogic4(1, Logic4::zero);
      }
      unknown |= !left_known || !right_known;
    }
  }
  return PackedLogic4(
      1, unknown ? Logic4::x : Logic4::one);
}

PackedLogic4 reduce_container_value(
    const ContainerValue& value,
    const ContainerReductionOperator operation,
    const std::span<const ContainerPredicateNode> transformation) {
  validate_container_value(value);
  if (!transformation.empty()
      && (value.type.associative
          || transformation.size()
              > maximum_container_predicate_nodes
          || transformation.back().value_kind
              != ContainerPredicateValueKind::element)) {
    throw std::invalid_argument{
        "SimIR container reduction transformation is invalid"};
  }
  auto identity = std::uint64_t{0};
  auto binary = BinaryOperator::add_unsigned;
  switch (operation) {
  case ContainerReductionOperator::sum:
    binary = BinaryOperator::add_unsigned;
    break;
  case ContainerReductionOperator::product:
    identity = 1;
    binary = BinaryOperator::multiply_unsigned;
    break;
  case ContainerReductionOperator::bit_and:
    identity =
        value.type.element_width == 64
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << value.type.element_width) - 1U;
    binary = BinaryOperator::bit_and;
    break;
  case ContainerReductionOperator::bit_or:
    binary = BinaryOperator::bit_or;
    break;
  case ContainerReductionOperator::bit_xor:
    binary = BinaryOperator::bit_xor;
    break;
  default:
    throw std::invalid_argument{
        "invalid SimIR container reduction operator"};
  }
  auto result = PackedLogic4::from_aval_bval(
      value.type.element_width, identity, 0);
  const auto declared_index =
      [&](const std::size_t offset) {
        return value.type.fixed
            ? value.type.index_left >= value.type.index_right
                  ? value.type.index_left
                        - static_cast<std::int32_t>(offset)
                  : value.type.index_left
                        + static_cast<std::int32_t>(offset)
            : static_cast<std::int32_t>(offset);
      };
  for (std::size_t offset = 0;
       offset < value.elements.size(); ++offset) {
    const auto& element = value.elements[offset];
    const auto transformed =
        transformation.empty()
            ? element
            : evaluate_container_predicate(
                  value.type, element,
                  declared_index(offset), transformation);
    result = binary_value(binary, result, transformed);
  }
  return result;
}

void order_container_value(
    ContainerValue& value,
    const ContainerOrderingOperator operation,
    const std::span<const ContainerPredicateNode> key) {
  validate_container_value(value);
  if (value.type.associative) {
    throw std::invalid_argument{
        "container ordering does not support associative arrays"};
  }
  if (operation == ContainerOrderingOperator::reverse) {
    if (!key.empty()) {
      throw std::invalid_argument{
          "SimIR container reverse cannot have key metadata"};
    }
    std::reverse(value.elements.begin(), value.elements.end());
    return;
  }
  if (operation != ContainerOrderingOperator::ascending
      && operation != ContainerOrderingOperator::descending) {
    throw std::invalid_argument{
        "invalid SimIR container ordering operator"};
  }
  if (!key.empty()
      && (key.size() > maximum_container_predicate_nodes
          || key.back().value_kind
              != ContainerPredicateValueKind::element)) {
    throw std::invalid_argument{
        "SimIR container ordering key is invalid"};
  }
  const auto less =
      [&](const PackedLogic4& left,
          const PackedLogic4& right) {
        return element_less(value.type, left, right);
      };
  if (!key.empty()) {
    struct KeyedElement {
      PackedLogic4 element;
      PackedLogic4 key;
    };
    std::vector<KeyedElement> keyed;
    keyed.reserve(value.elements.size());
    const auto declared_index =
        [&](const std::size_t offset) {
          return value.type.fixed
              ? value.type.index_left >= value.type.index_right
                    ? value.type.index_left
                          - static_cast<std::int32_t>(offset)
                    : value.type.index_left
                          + static_cast<std::int32_t>(offset)
              : static_cast<std::int32_t>(offset);
        };
    for (std::size_t offset = 0;
         offset < value.elements.size(); ++offset) {
      const auto& element = value.elements[offset];
      keyed.push_back(
          KeyedElement{
              element,
              evaluate_container_predicate(
                  value.type, element,
                  declared_index(offset), key)});
    }
    const auto key_less =
        [&](const KeyedElement& left,
            const KeyedElement& right) {
          return operation == ContainerOrderingOperator::ascending
              ? less(left.key, right.key)
              : less(right.key, left.key);
        };
    std::stable_sort(keyed.begin(), keyed.end(), key_less);
    std::transform(
        keyed.begin(), keyed.end(), value.elements.begin(),
        [](KeyedElement& entry) {
          return std::move(entry.element);
        });
    return;
  }
  if (operation == ContainerOrderingOperator::ascending) {
    std::stable_sort(
        value.elements.begin(), value.elements.end(), less);
  } else {
    std::stable_sort(
        value.elements.begin(), value.elements.end(),
        [&](const PackedLogic4& left,
            const PackedLogic4& right) {
          return less(right, left);
        });
  }
}

void locate_container_values(
    ContainerValue& destination,
    const ContainerValue& source,
    const ContainerLocatorOperator operation,
    const std::span<const ContainerPredicateNode> predicate,
    const std::span<const ContainerPredicateNode> transformation) {
  validate_container_value(source);
  validate_container_value(destination);
  if (source.type.associative || !destination.type.queue
      || destination.type.associative
      || destination.type.fixed) {
    throw std::invalid_argument{
        "container locators require a nonassociative source and queue result"};
  }
  const bool predicate_locator =
      operation >= ContainerLocatorOperator::find;
  const bool index_result =
      operation == ContainerLocatorOperator::unique_index
      || operation == ContainerLocatorOperator::find_index
      || operation == ContainerLocatorOperator::find_first_index
      || operation == ContainerLocatorOperator::find_last_index;
  if (static_cast<std::uint8_t>(operation)
      > static_cast<std::uint8_t>(
          ContainerLocatorOperator::find_last_index)) {
    throw std::invalid_argument{
        "invalid SimIR container locator operator"};
  }
  if (predicate_locator != !predicate.empty()
      || (predicate_locator && !transformation.empty())) {
    throw std::invalid_argument{
        predicate_locator
            ? "predicate container locator requires only predicate metadata"
            : "non-predicate container locator has predicate metadata"};
  }
  if (!transformation.empty()
      && (transformation.size()
              > maximum_container_predicate_nodes
          || transformation.back().value_kind
              != ContainerPredicateValueKind::element)) {
    throw std::invalid_argument{
        "SimIR container locator transformation is invalid"};
  }
  if (index_result
          ? destination.type.element_width != 32
                || !destination.type.two_state
                || !destination.type.signed_elements
          : destination.type.element_width
                    != source.type.element_width
                || destination.type.two_state
                    != source.type.two_state
                || destination.type.signed_elements
                    != source.type.signed_elements) {
    throw std::invalid_argument{
        "container locator result element type mismatch"};
  }
  const auto elements = source.elements;
  const auto source_type = source.type;
  destination.elements.clear();
  destination.keys.clear();
  const auto limit = destination.type.maximum_elements.value_or(
      static_cast<std::uint32_t>(maximum_container_elements));
  const auto append =
      [&](PackedLogic4 value) {
        if (destination.elements.size() < limit) {
          destination.elements.push_back(std::move(value));
        }
      };
  const auto declared_index =
      [&](const std::size_t offset) {
        return source_type.fixed
            ? source_type.index_left >= source_type.index_right
                  ? source_type.index_left
                        - static_cast<std::int32_t>(offset)
                  : source_type.index_left
                        + static_cast<std::int32_t>(offset)
            : static_cast<std::int32_t>(offset);
      };
  const auto append_match =
      [&](const std::size_t offset) {
        if (index_result) {
          append(PackedLogic4::from_aval_bval(
              32,
              static_cast<std::uint32_t>(
                  declared_index(offset)),
              0));
        } else {
          append(elements[offset]);
        }
      };
  std::vector<PackedLogic4> transformed;
  if (!transformation.empty()) {
    transformed.reserve(elements.size());
    for (std::size_t offset = 0;
         offset < elements.size(); ++offset) {
      transformed.push_back(
          evaluate_container_predicate(
              source_type, elements[offset],
              declared_index(offset), transformation));
    }
  }
  const auto& keys =
      transformation.empty() ? elements : transformed;
  if (predicate_locator) {
    const bool select_last =
        operation == ContainerLocatorOperator::find_last
        || operation
            == ContainerLocatorOperator::find_last_index;
    const bool select_one =
        select_last
        || operation == ContainerLocatorOperator::find_first
        || operation
            == ContainerLocatorOperator::find_first_index;
    for (std::size_t step = 0; step < elements.size(); ++step) {
      const auto offset =
          select_last ? elements.size() - step - 1U : step;
      if (truth_value(evaluate_container_predicate(
              source_type, elements[offset],
              declared_index(offset), predicate))
          != Logic4::one) {
        continue;
      }
      append_match(offset);
      if (select_one) {
        break;
      }
    }
    return;
  }
  if (operation == ContainerLocatorOperator::minimum
      || operation == ContainerLocatorOperator::maximum) {
    if (elements.empty()) {
      return;
    }
    std::size_t selected{};
    for (std::size_t current = 1U;
         current < elements.size(); ++current) {
      const bool replace =
          operation == ContainerLocatorOperator::minimum
              ? element_less(
                    source_type, keys[current], keys[selected])
              : element_less(
                    source_type, keys[selected], keys[current]);
      if (replace) {
        selected = current;
      }
    }
    append(elements[selected]);
    return;
  }
  std::vector<PackedLogic4> seen;
  for (std::size_t offset = 0; offset < elements.size(); ++offset) {
    if (std::ranges::find(seen, keys[offset]) != seen.end()) {
      continue;
    }
    seen.push_back(keys[offset]);
    if (!index_result) {
      append(elements[offset]);
      continue;
    }
    append_match(offset);
  }
}

void load_memory_text(
    ContainerValue& target,
    const std::string_view text,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  validate_container_value(target);
  if (!target.type.fixed) {
    throw std::invalid_argument{
        "$readmemb/$readmemh target must be a static unpacked array"};
  }
  if (text.size() > maximum_memory_file_bytes) {
    throw std::length_error{
        "read-memory file exceeds the 1 MiB limit"};
  }

  std::vector<std::string> tokens;
  std::string token;
  bool line_comment{};
  bool block_comment{};
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto character = text[index];
    const auto next =
        index + 1U < text.size() ? text[index + 1U] : '\0';
    if (line_comment) {
      if (character == '\n') {
        line_comment = false;
      }
      continue;
    }
    if (block_comment) {
      if (character == '*' && next == '/') {
        block_comment = false;
        ++index;
      }
      continue;
    }
    if (character == '/' && next == '/') {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      line_comment = true;
      ++index;
      continue;
    }
    if (character == '/' && next == '*') {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      block_comment = true;
      ++index;
      continue;
    }
    if (std::isspace(
            static_cast<unsigned char>(character)) != 0) {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      continue;
    }
    token.push_back(character);
  }
  if (block_comment) {
    throw std::invalid_argument{
        "unterminated block comment in read-memory file"};
  }
  if (!token.empty()) {
    tokens.push_back(std::move(token));
  }

  const auto range_low =
      std::min(target.type.index_left, target.type.index_right);
  const auto range_high =
      std::max(target.type.index_left, target.type.index_right);
  const auto first = start.value_or(range_low);
  const auto last = finish.value_or(range_high);
  const auto in_declared_range =
      [&](const std::int64_t index) {
        return index >= range_low && index <= range_high;
      };
  if (!in_declared_range(first) || !in_declared_range(last)) {
    throw std::out_of_range{
        "read-memory start/finish is outside the target range"};
  }
  const auto step = first <= last ? 1 : -1;
  const auto in_window =
      [&](const std::int64_t index) {
        return step > 0
            ? index >= first && index <= last
            : index <= first && index >= last;
      };
  auto current = static_cast<std::int64_t>(first);

  const auto parse_address =
      [](const std::string_view digits) -> std::int32_t {
        std::string normalized;
        normalized.reserve(digits.size());
        for (const auto character : digits) {
          if (character != '_') {
            normalized.push_back(character);
          }
        }
        if (normalized.empty()) {
          throw std::invalid_argument{
              "empty @address in read-memory file"};
        }
        std::uint32_t value{};
        const auto [end, error] = std::from_chars(
            normalized.data(),
            normalized.data() + normalized.size(),
            value, 16);
        if (error != std::errc{}
            || end != normalized.data() + normalized.size()) {
          throw std::invalid_argument{
              "invalid @address in read-memory file"};
        }
        return static_cast<std::int32_t>(value);
      };
  const auto parse_data =
      [&](const std::string_view digits) {
        std::string bits;
        bits.reserve(
            digits.size() * (hexadecimal ? 4U : 1U));
        const auto append_unknown =
            [&](const char state) {
              bits.append(hexadecimal ? 4U : 1U, state);
            };
        for (const auto raw : digits) {
          if (raw == '_') {
            continue;
          }
          const auto character = static_cast<char>(
              std::tolower(static_cast<unsigned char>(raw)));
          if (character == 'x') {
            append_unknown('X');
            continue;
          }
          if (character == 'z' || character == '?') {
            append_unknown('Z');
            continue;
          }
          unsigned value{};
          if (character >= '0' && character <= '9') {
            value = static_cast<unsigned>(character - '0');
          } else if (
              character >= 'a' && character <= 'f') {
            value =
                static_cast<unsigned>(character - 'a' + 10);
          } else {
            throw std::invalid_argument{
                "invalid digit in read-memory data token"};
          }
          if ((!hexadecimal && value > 1U)
              || (hexadecimal && value > 15U)) {
            throw std::invalid_argument{
                "digit does not match read-memory radix"};
          }
          if (hexadecimal) {
            for (int bit = 3; bit >= 0; --bit) {
              bits.push_back(
                  ((value >> bit) & 1U) != 0 ? '1' : '0');
            }
          } else {
            bits.push_back(value != 0 ? '1' : '0');
          }
        }
        if (bits.empty()) {
          throw std::invalid_argument{
              "empty data token in read-memory file"};
        }
        if (bits.size() > target.type.element_width) {
          bits.erase(
              0, bits.size() - target.type.element_width);
        } else if (bits.size() < target.type.element_width) {
          bits.insert(
              0, target.type.element_width - bits.size(), '0');
        }
        if (target.type.two_state) {
          std::ranges::replace(bits, 'X', '0');
          std::ranges::replace(bits, 'Z', '0');
        }
        return PackedLogic4::from_msb_string(bits);
      };

  for (const auto& item : tokens) {
    if (item.starts_with('@')) {
      current = parse_address(
          std::string_view{item}.substr(1));
      if (!in_declared_range(current)
          || !in_window(current)) {
        throw std::out_of_range{
            "read-memory @address is outside the selected range"};
      }
      continue;
    }
    if (!in_declared_range(current)
        || !in_window(current)) {
      throw std::out_of_range{
          "read-memory data exceeds the selected range"};
    }
    const auto offset =
        target.type.index_left >= target.type.index_right
            ? static_cast<std::size_t>(
                  static_cast<std::int64_t>(
                      target.type.index_left) - current)
            : static_cast<std::size_t>(
                  static_cast<std::int64_t>(current)
                  - target.type.index_left);
    target.elements[offset] = parse_data(item);
    current += step;
  }
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ResizeContainer& operation) {
  auto& target = get_container_register(process, operation.target);
  if (target.type.queue || target.type.associative
      || target.type.fixed) {
    container_error(
        process.program.id, process.pc,
        target.type.queue
            ? "new[size] cannot resize a queue"
            : target.type.associative
                  ? "new[size] cannot resize an associative array"
                  : "new[size] cannot resize a static array");
  }
  const auto size = known_index(
      process.program.id, process.pc,
      get_register(process, operation.size),
      false, "dynamic-array size");
  if (size > maximum_container_elements) {
    container_error(
        process.program.id, process.pc,
        "dynamic-array size exceeds the 4096-element limit");
  }
  target.elements.assign(
      size, default_container_element(target.type));
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerRegister& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& source =
      get_container_register(process, operation.source);
  require_same_type(
      process.program.id, process.pc,
      destination.type, source.type);
  destination.elements = source.elements;
  destination.keys = source.keys;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ConditionalContainerSelect& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& when_true =
      get_container_register(process, operation.when_true);
  const auto& when_false =
      get_container_register(process, operation.when_false);
  const auto& condition =
      get_register(process, operation.condition);
  try {
    select_container_value(
        destination, condition, when_true, when_false);
  } catch (const std::exception& error) {
    container_error(
        process.program.id, process.pc, error.what());
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CompareContainers& operation) {
  try {
    get_register(process, operation.destination) =
        compare_container_values(
            get_container_register(process, operation.lhs),
            get_container_register(process, operation.rhs),
            operation.case_equal);
  } catch (const std::exception& error) {
    container_error(
        process.program.id, process.pc, error.what());
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ReadContainerObject& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& source =
      read_container_object_value(operation.object);
  require_same_type(
      process.program.id, process.pc,
      destination.type, source.type);
  destination.elements = source.elements;
  destination.keys = source.keys;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const WriteContainerObject& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  try {
    write_container_object_value(
        operation.object, source);
  } catch (const std::invalid_argument& error) {
    container_error(
        process.program.id, process.pc, error.what());
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerSize& operation) {
  const auto size = get_container_register(
      process, operation.source).elements.size();
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(32, size, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerReduction& operation) {
  get_register(process, operation.destination) =
      reduce_container_value(
          get_container_register(process, operation.source),
          operation.operation, operation.transformation);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const OrderContainer& operation) {
  order_container_value(
      get_container_register(process, operation.target),
      operation.operation, operation.key);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const LocateContainer& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& source =
      get_container_register(process, operation.source);
  locate_container_values(
      destination, source, operation.operation,
      operation.predicate, operation.transformation);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerRead& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  if (source.type.associative) {
    const auto key = associative_key(
        process.program.id, process.pc, source.type,
        get_register(process, operation.index));
    const auto at = lower_key(source, key);
    get_register(process, operation.destination) =
        at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.elements[at]
            : default_container_element(source.type);
    ++process.pc;
    return;
  }
  if (source.type.fixed) {
    if (operation.linear_index) {
      const auto index = known_index(
          process.program.id, process.pc,
          get_register(process, operation.index),
          true, "multidimensional linear index");
      if (index >= source.elements.size()) {
        container_error(
            process.program.id, process.pc,
            "multidimensional linear index is out of range");
      }
      get_register(process, operation.destination) =
          source.elements[index];
      ++process.pc;
      return;
    }
    get_register(process, operation.destination) =
        source.elements[fixed_offset(
            process.program.id, process.pc, source.type,
            get_register(process, operation.index))];
    ++process.pc;
    return;
  }
  const auto index = known_index(
      process.program.id, process.pc,
      get_register(process, operation.index),
      operation.signed_index, "container index");
  if (index >= source.elements.size()) {
    container_error(
        process.program.id, process.pc,
        "container index is out of range");
  }
  get_register(process, operation.destination) =
      source.elements[index];
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerWrite& operation) {
  auto& target =
      get_container_register(process, operation.target);
  const auto& source = get_register(process, operation.source);
  if (source.width() != target.type.element_width
      || source.is_logic9()
      || (target.type.two_state
          && source.low_word().bval != 0)) {
    container_error(
        process.program.id, process.pc,
        "container element write type mismatch");
  }
  if (target.type.associative) {
    const auto key = associative_key(
        process.program.id, process.pc, target.type,
        get_register(process, operation.index));
    const auto at = lower_key(target, key);
    if (at < target.keys.size()
        && key_equal(target.keys[at], key)) {
      target.elements[at] = source;
    } else {
      if (target.elements.size() >= maximum_container_elements) {
        container_error(
            process.program.id, process.pc,
            "associative array exceeds the 4096-entry limit");
      }
      target.keys.insert(target.keys.begin() + at, key);
      target.elements.insert(target.elements.begin() + at, source);
    }
    ++process.pc;
    return;
  }
  if (target.type.fixed) {
    if (operation.linear_index) {
      const auto index = known_index(
          process.program.id, process.pc,
          get_register(process, operation.index),
          true, "multidimensional linear index");
      if (index >= target.elements.size()) {
        container_error(
            process.program.id, process.pc,
            "multidimensional linear index is out of range");
      }
      target.elements[index] = source;
      ++process.pc;
      return;
    }
    target.elements[fixed_offset(
        process.program.id, process.pc, target.type,
        get_register(process, operation.index))] = source;
    ++process.pc;
    return;
  }
  const auto index = known_index(
      process.program.id, process.pc,
      get_register(process, operation.index),
      operation.signed_index, "container index");
  if (index >= target.elements.size()) {
    container_error(
        process.program.id, process.pc,
        "container index is out of range");
  }
  target.elements[index] = source;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const DeleteContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  if (operation.index) {
    require_associative(
        process.program.id, process.pc, target, "delete(index)");
    const auto key = associative_key(
        process.program.id, process.pc, target.type,
        get_register(process, *operation.index));
    const auto at = lower_key(target, key);
    if (at < target.keys.size()
        && key_equal(target.keys[at], key)) {
      target.keys.erase(target.keys.begin() + at);
      target.elements.erase(target.elements.begin() + at);
    }
  } else {
    if (target.type.fixed) {
      container_error(
          process.program.id, process.pc,
          "delete() cannot clear a static array");
    }
    target.elements.clear();
    target.keys.clear();
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const LoadMemory& operation) {
  auto& target =
      get_container_register(process, operation.target);
  const auto optional_integer =
      [&](const std::optional<RegisterId> source,
          const std::string_view role)
          -> std::optional<std::int32_t> {
        if (!source) {
          return std::nullopt;
        }
        const auto& value = get_register(process, *source);
        const auto word = value.low_word();
        if (word.width != 32 || word.bval != 0) {
          container_error(
              process.program.id, process.pc,
              std::string{role}
                  + " must be a known 32-bit integral value");
        }
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(word.aval));
      };
  const auto handle = open_file(
      process.program.id,
      get_string_register(process, operation.path), "r");
  std::string text;
  try {
    while (!file_end_of_file(process.program.id, handle)) {
      std::uint32_t count{};
      auto line = read_file_line(
          process.program.id, handle, count);
      if (text.size() + line.size()
          > maximum_memory_file_bytes) {
        throw std::length_error{
            "read-memory file exceeds the 1 MiB limit"};
      }
      text += line;
    }
    close_file(process.program.id, handle);
  } catch (...) {
    try {
      close_file(process.program.id, handle);
    } catch (...) {
    }
    throw;
  }
  try {
    load_memory_text(
        target, text, operation.hexadecimal,
        optional_integer(operation.start, "read-memory start"),
        optional_integer(operation.finish, "read-memory finish"));
  } catch (const std::exception& error) {
    container_error(
        process.program.id, process.pc, error.what());
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerExists& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  require_associative(
      process.program.id, process.pc, source, "exists(index)");
  const auto key = associative_key(
      process.program.id, process.pc, source.type,
      get_register(process, operation.index));
  const auto at = lower_key(source, key);
  const auto exists =
      at < source.keys.size() && key_equal(source.keys[at], key);
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const TraverseContainer& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  require_associative(
      process.program.id, process.pc, source,
      "first/last/next/prev");
  std::optional<std::size_t> selected;
  if (!source.keys.empty()) {
    if (operation.traversal == ContainerTraversal::first) {
      selected = 0;
    } else if (operation.traversal == ContainerTraversal::last) {
      selected = source.keys.size() - 1U;
    } else {
      const auto key = associative_key(
          process.program.id, process.pc, source.type,
          get_register(process, operation.index));
      const auto at = lower_key(source, key);
      if (operation.traversal == ContainerTraversal::next) {
        const auto next =
            at < source.keys.size()
                    && key_equal(source.keys[at], key)
                ? at + 1U
                : at;
        if (next < source.keys.size()) {
          selected = next;
        }
      } else if (at != 0) {
        selected = at - 1U;
      }
    }
  }
  if (selected) {
    get_register(process, operation.index) = source.keys[*selected];
  }
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(
          32, selected.has_value() ? 1U : 0U, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PushContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  require_queue(process.program.id, process.pc, target);
  const auto& source = get_register(process, operation.source);
  if (source.width() != target.type.element_width
      || source.is_logic9()
      || (target.type.two_state
          && source.low_word().bval != 0)) {
    container_error(
        process.program.id, process.pc,
        "queue element write type mismatch");
  }
  if (operation.front) {
    target.elements.insert(target.elements.begin(), source);
  } else {
    target.elements.push_back(source);
  }
  const auto maximum = target.type.maximum_elements.value_or(
      static_cast<std::uint32_t>(
          maximum_container_elements));
  if (target.elements.size() > maximum) {
    target.elements.pop_back();
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PopContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  require_queue(process.program.id, process.pc, target);
  if (target.elements.empty()) {
    container_error(
        process.program.id, process.pc,
        "cannot pop an empty queue");
  }
  auto value =
      operation.front
          ? target.elements.front()
          : target.elements.back();
  if (operation.front) {
    target.elements.erase(target.elements.begin());
  } else {
    target.elements.pop_back();
  }
  get_register(process, operation.destination) = std::move(value);
  ++process.pc;
}

}  // namespace fsim::runtime::simir
