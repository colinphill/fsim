// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <charconv>

namespace fsim::runtime::simir {

namespace {

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
              && has_unknown(node.constant))) {
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
  auto binary = BinaryOperator::add_unsigned;
  auto result = PackedLogic4(
      value.type.element_width, Logic4::zero);
  switch (operation) {
  case ContainerReductionOperator::sum:
    binary = BinaryOperator::add_unsigned;
    break;
  case ContainerReductionOperator::product:
    result.set(0, Logic4::one);
    binary = BinaryOperator::multiply_unsigned;
    break;
  case ContainerReductionOperator::bit_and:
    result.fill(Logic4::one);
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
    const std::span<const ContainerPredicateNode> key,
    const std::function<std::uint32_t()>& random) {
  validate_container_value(value);
  if (value.type.associative) {
    throw std::invalid_argument{
        "container ordering does not support associative arrays"};
  }
  if (operation == ContainerOrderingOperator::shuffle) {
    if (!key.empty() || !random) {
      throw std::invalid_argument{
          "container shuffle requires an unkeyed random source"};
    }
    const auto size = container_value_size(value);
    if (size > std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error{
          "container shuffle exceeds the deterministic index domain"};
    }
    const auto swap_elements =
        [&](const std::size_t left, const std::size_t right) {
          switch (value.type.element_kind) {
          case ContainerElementKind::Packed:
          case ContainerElementKind::Scalar:
            std::swap(value.elements[left], value.elements[right]);
            return;
          case ContainerElementKind::String:
            std::swap(
                value.string_elements[left],
                value.string_elements[right]);
            return;
          case ContainerElementKind::Container:
          case ContainerElementKind::Aggregate:
            std::swap(
                value.nested_elements[left],
                value.nested_elements[right]);
            return;
          }
        };
    for (std::size_t remaining = size;
         remaining > 1U; --remaining) {
      const auto span = static_cast<std::uint32_t>(remaining);
      const auto full_range = UINT64_C(1) << 32U;
      const auto accepted = full_range - full_range % span;
      std::uint32_t sample{};
      do {
        sample = random();
      } while (static_cast<std::uint64_t>(sample) >= accepted);
      swap_elements(
          remaining - 1U,
          static_cast<std::size_t>(sample % span));
    }
    return;
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
  const auto append =
      [&](PackedLogic4 value) {
        if (!destination.type.maximum_elements
            || destination.elements.size()
                < *destination.type.maximum_elements) {
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

namespace {

[[nodiscard]] std::size_t memory_element_count(
    const ContainerValue& value) {
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar:
    return value.elements.size();
  case ContainerElementKind::String:
    return value.string_elements.size();
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate:
    return value.nested_elements.size();
  }
  return 0;
}

[[nodiscard]] PackedLogic4 memory_packed_element(
    const ContainerValue& value,
    const std::size_t offset) {
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar:
    return value.elements.at(offset);
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate:
    return pack_container_signal_value(
        value.nested_elements.at(offset), false);
  case ContainerElementKind::String:
    break;
  }
  throw std::invalid_argument{
      "string memory elements do not have packed values"};
}

void replace_memory_packed_element(
    ContainerValue& value,
    const std::size_t offset,
    PackedLogic4 replacement) {
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar:
    value.elements.at(offset) = std::move(replacement);
    return;
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate:
    unpack_container_signal_value(
        value.nested_elements.at(offset), replacement);
    return;
  case ContainerElementKind::String:
    break;
  }
  throw std::invalid_argument{
      "string memory elements do not accept packed values"};
}

[[nodiscard]] std::string parse_memory_string(
    const std::string_view token) {
  if (token.size() < 2U || token.front() != '"'
      || token.back() != '"') {
    throw std::invalid_argument{
        "string memory data must use a quoted token"};
  }
  std::string result;
  result.reserve(token.size() - 2U);
  for (std::size_t index = 1; index + 1U < token.size(); ++index) {
    const auto character = token[index];
    if (character != '\\') {
      result.push_back(character);
      continue;
    }
    if (++index + 1U >= token.size()) {
      throw std::invalid_argument{
          "unterminated escape in string memory data"};
    }
    const auto escaped = token[index];
    switch (escaped) {
    case '\\': result.push_back('\\'); break;
    case '"': result.push_back('"'); break;
    case 'n': result.push_back('\n'); break;
    case 'r': result.push_back('\r'); break;
    case 't': result.push_back('\t'); break;
    case 'x': {
      if (index + 2U >= token.size() - 1U) {
        throw std::invalid_argument{
            "short hexadecimal escape in string memory data"};
      }
      unsigned byte{};
      for (std::size_t digit = 0; digit < 2U; ++digit) {
        const auto value = token[++index];
        byte <<= 4U;
        if (value >= '0' && value <= '9') {
          byte |= static_cast<unsigned>(value - '0');
        } else if (value >= 'a' && value <= 'f') {
          byte |= static_cast<unsigned>(value - 'a' + 10);
        } else if (value >= 'A' && value <= 'F') {
          byte |= static_cast<unsigned>(value - 'A' + 10);
        } else {
          throw std::invalid_argument{
              "invalid hexadecimal escape in string memory data"};
        }
      }
      result.push_back(static_cast<char>(byte));
      break;
    }
    default:
      throw std::invalid_argument{
          "unsupported escape in string memory data"};
    }
  }
  return result;
}

[[nodiscard]] std::string format_memory_string(
    const std::string_view value) {
  std::string result{"\""};
  constexpr std::string_view digits = "0123456789abcdef";
  for (const auto raw : value) {
    const auto byte = static_cast<unsigned char>(raw);
    switch (raw) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (byte < 0x20U || byte == 0x7fU) {
        result += "\\x";
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
      } else {
        result.push_back(raw);
      }
      break;
    }
  }
  result.push_back('"');
  return result;
}

}  // namespace

void load_memory_text(
    ContainerValue& target,
    const std::string_view text,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  validate_container_value(target);
  if (!target.type.fixed || target.type.associative) {
    throw std::invalid_argument{
        "$readmemb/$readmemh target must be a fixed unpacked array"};
  }
  if (text.size() > maximum_memory_file_bytes) {
    throw std::length_error{
        "read-memory file exceeds the 1 MiB limit"};
  }
  auto replacement = target;

  std::vector<std::string> tokens;
  std::string token;
  bool line_comment{};
  bool block_comment{};
  bool quoted{};
  bool escaped{};
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto character = text[index];
    const auto next =
        index + 1U < text.size() ? text[index + 1U] : '\0';
    if (quoted) {
      token.push_back(character);
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        quoted = false;
      }
      continue;
    }
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
    if (character == '"') {
      if (!token.empty()) {
        throw std::invalid_argument{
            "quoted memory data must begin at a token boundary"};
      }
      token.push_back(character);
      quoted = true;
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
  if (block_comment || quoted || escaped) {
    throw std::invalid_argument{
        block_comment
            ? "unterminated block comment in read-memory file"
            : "unterminated quoted string in read-memory file"};
  }
  if (!token.empty()) {
    tokens.push_back(std::move(token));
  }

  const auto count = memory_element_count(target);
  if (count == 0) {
    throw std::invalid_argument{
        "read-memory target has no materialized elements"};
  }
  const bool linear = target.type.dimensions.size() > 1U;
  const auto range_low = linear
      ? INT64_C(0)
      : static_cast<std::int64_t>(
            std::min(target.type.index_left, target.type.index_right));
  const auto range_high = linear
      ? static_cast<std::int64_t>(count - 1U)
      : static_cast<std::int64_t>(
            std::max(target.type.index_left, target.type.index_right));
  const auto first = start
      ? static_cast<std::int64_t>(*start) : range_low;
  const auto last = finish
      ? static_cast<std::int64_t>(*finish) : range_high;
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
      [&](const std::string_view digits, const std::size_t width) {
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
        if (bits.size() > width) {
          bits.erase(
              0, bits.size() - width);
        } else if (bits.size() < width) {
          bits.insert(
              0, width - bits.size(), '0');
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
    const auto offset = linear
        ? static_cast<std::size_t>(current)
        : target.type.index_left >= target.type.index_right
            ? static_cast<std::size_t>(
                  static_cast<std::int64_t>(
                      target.type.index_left) - current)
            : static_cast<std::size_t>(
                  static_cast<std::int64_t>(current)
                  - target.type.index_left);
    if (replacement.type.element_kind
        == ContainerElementKind::String) {
      replacement.string_elements[offset] = parse_memory_string(item);
    } else {
      const auto width = memory_packed_element(
          replacement, offset).width();
      replace_memory_packed_element(
          replacement, offset, parse_data(item, width));
    }
    current += step;
  }
  validate_container_value(replacement);
  target = std::move(replacement);
}

std::string write_memory_text(
    const ContainerValue& source,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  validate_container_value(source);
  if (!source.type.fixed || source.type.associative) {
    throw std::invalid_argument{
        "$writememb/$writememh source must be a fixed unpacked array"};
  }
  const auto count = memory_element_count(source);
  if (count == 0) {
    throw std::invalid_argument{
        "write-memory source has no materialized elements"};
  }
  const bool linear = source.type.dimensions.size() > 1U;
  const auto low = linear
      ? INT64_C(0)
      : static_cast<std::int64_t>(
            std::min(source.type.index_left, source.type.index_right));
  const auto high = linear
      ? static_cast<std::int64_t>(count - 1U)
      : static_cast<std::int64_t>(
            std::max(source.type.index_left, source.type.index_right));
  const auto first = start
      ? static_cast<std::int64_t>(*start) : low;
  const auto last = finish
      ? static_cast<std::int64_t>(*finish) : high;
  const auto in_range = [&](const std::int64_t index) {
    return index >= low && index <= high;
  };
  if (!in_range(first) || !in_range(last)) {
    throw std::out_of_range{
        "write-memory start/finish is outside the source range"};
  }
  const auto format = [&](const PackedLogic4& value) {
    auto bits = value.to_msb_string();
    if (!hexadecimal) return bits;
    bits.insert(0, (4U - bits.size() % 4U) % 4U, '0');
    std::string text;
    text.reserve(bits.size() / 4U);
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t offset = 0; offset < bits.size(); offset += 4U) {
      const auto nibble = std::string_view{bits}.substr(offset, 4U);
      const bool unknown = nibble.find('X') != std::string_view::npos;
      const bool high_z = nibble.find('Z') != std::string_view::npos;
      if (unknown || high_z) {
        text.push_back(!unknown && std::ranges::all_of(
            nibble, [](const char bit) { return bit == 'Z'; }) ? 'z' : 'x');
        continue;
      }
      unsigned digit{};
      for (const auto bit : nibble)
        digit = (digit << 1U) | static_cast<unsigned>(bit == '1');
      text.push_back(digits[digit]);
    }
    return text;
  };
  std::string result;
  const auto step = first <= last ? 1 : -1;
  for (auto index = first;; index += step) {
    const auto offset = linear
        ? static_cast<std::size_t>(index)
        : source.type.index_left >= source.type.index_right
        ? static_cast<std::size_t>(
              static_cast<std::int64_t>(source.type.index_left) - index)
        : static_cast<std::size_t>(
              static_cast<std::int64_t>(index) - source.type.index_left);
    result += source.type.element_kind == ContainerElementKind::String
        ? format_memory_string(source.string_elements[offset])
        : format(memory_packed_element(source, offset));
    result.push_back('\n');
    if (index == last) break;
  }
  if (result.size() > maximum_memory_file_bytes)
    throw std::length_error{"write-memory file exceeds the 1 MiB limit"};
  return result;
}

}  // namespace fsim::runtime::simir
