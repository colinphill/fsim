// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <unordered_map>

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

// The ordinal a sparse array key holds.
[[nodiscard]] std::uint64_t sparse_key_ordinal(const PackedLogic4& key) {
  return key.known_unsigned_value().value_or(0U);
}

[[nodiscard]] PackedLogic4 sparse_key(const std::uint64_t ordinal) {
  return PackedLogic4::from_aval_bval(32U, ordinal, 0U);
}

// Whether a predicate graph reads the iterator index (item.index).
[[nodiscard]] bool predicate_reads_index(
    const std::span<const ContainerPredicateNode> predicate) {
  return std::ranges::any_of(
      predicate, [](const ContainerPredicateNode& node) {
        return node.operation == ContainerPredicateOperator::index;
      });
}

// Whether a sparse array holds its stored elements as keyed entries rather
// than as a compact image.
[[nodiscard]] bool sparse_keyed(const ContainerValue& value) {
  return value.type.sparse && value.compact_planes.empty();
}

// The first ordinal of a keyed sparse array that holds no stored element.
[[nodiscard]] std::uint64_t first_unstored_ordinal(
    const ContainerValue& value) {
  std::uint64_t ordinal{};
  for (const auto& key : value.keys) {
    if (sparse_key_ordinal(key) != ordinal) {
      break;
    }
    ++ordinal;
  }
  return ordinal;
}

// The declared index of storage ordinal `offset` of a container.
[[nodiscard]] std::int32_t container_declared_index(
    const ContainerType& type, const std::uint64_t offset) {
  const auto step = static_cast<std::int32_t>(offset);
  return type.fixed
      ? type.index_left >= type.index_right
            ? type.index_left - step
            : type.index_left + step
      : step;
}

// A uniform sample below `span` from the deterministic 32-bit source,
// rejecting the samples of the incomplete last range.
[[nodiscard]] std::uint32_t uniform_below(
    const std::function<std::uint32_t()>& random,
    const std::uint32_t span) {
  const auto full_range = UINT64_C(1) << 32U;
  const auto accepted = full_range - full_range % span;
  std::uint32_t sample{};
  do {
    sample = random();
  } while (static_cast<std::uint64_t>(sample) >= accepted);
  return sample % span;
}

// Fold `copies` copies of `element` into `result`, as that many successive
// folds would: double-and-add or square-and-multiply for the arithmetic
// reductions, at most one fold for the idempotent ones, parity for xor.
void fold_repeated(
    PackedLogic4& result,
    const BinaryOperator binary,
    const ContainerReductionOperator operation,
    PackedLogic4 element,
    std::uint64_t copies) {
  switch (operation) {
  case ContainerReductionOperator::sum:
  case ContainerReductionOperator::product:
    while (copies != 0U) {
      if ((copies & 1U) != 0U) {
        result = binary_value(binary, result, element);
      }
      copies >>= 1U;
      if (copies != 0U) {
        element = binary_value(binary, element, element);
      }
    }
    return;
  case ContainerReductionOperator::bit_and:
  case ContainerReductionOperator::bit_or:
    if (copies != 0U) {
      result = binary_value(binary, result, element);
    }
    return;
  case ContainerReductionOperator::bit_xor:
    if ((copies & 1U) != 0U) {
      result = binary_value(binary, result, element);
    }
    return;
  }
}

// A sparse value like `like` (type and background) holding no element but
// the background.
[[nodiscard]] ContainerValue empty_sparse_like(const ContainerValue& like) {
  ContainerValue result;
  result.type = like.type;
  result.associative_default = like.associative_default;
  result.associative_default_text = like.associative_default_text;
  return result;
}

// The order element_less defines, as words compared most significant first:
// two bits per value bit, the bit's rank in that order.
void append_rank_words(
    const ContainerType& type,
    const PackedLogic4& key,
    std::vector<std::uint64_t>& words,
    const std::size_t stride) {
  const auto width = key.width();
  const auto base = words.size();
  words.resize(base + stride, 0U);
  std::size_t position{};
  for (std::size_t index = width; index-- != 0U; ++position) {
    const auto bit = key.get(index);
    std::uint64_t rank = static_cast<std::uint8_t>(bit);
    if (type.signed_elements && index + 1U == type.element_width) {
      rank = bit == Logic4::one ? 0U
          : bit == Logic4::zero ? 1U
          : bit == Logic4::x ? 2U : 3U;
    }
    const auto bit_position = position * 2U;
    words[base + bit_position / 64U]
        |= rank << (62U - bit_position % 64U);
  }
}

// Stable ordering of every element of a sparse array by rank-encoded keys
// held in a compact temporary (one key image per element and a 32-bit
// permutation), for orderings with no exact sparse form.
void order_sparse_by_rank(
    ContainerValue& value,
    const bool descending,
    const std::span<const ContainerPredicateNode> key) {
  const auto count = sparse_container_element_count(value.type);
  std::vector<std::uint64_t> words;
  std::size_t stride{};
  {
    ContainerElementCursor cursor{value};
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
      const auto element = cursor.at(ordinal);
      const auto rank_key = key.empty()
          ? element
          : evaluate_container_predicate(
                value.type, element,
                container_declared_index(value.type, ordinal), key);
      if (ordinal == 0U) {
        stride = std::max<std::size_t>(
            (rank_key.width() * 2U + 63U) / 64U, 1U);
        words.reserve(static_cast<std::size_t>(count) * stride);
      }
      append_rank_words(value.type, rank_key, words, stride);
    }
  }
  std::vector<std::uint32_t> permutation(static_cast<std::size_t>(count));
  std::iota(permutation.begin(), permutation.end(), std::uint32_t { 0 });
  const auto rank_less =
      [&](const std::uint32_t left, const std::uint32_t right) {
        const auto* const a = words.data() + std::size_t { left } * stride;
        const auto* const b = words.data() + std::size_t { right } * stride;
        for (std::size_t word = 0U; word < stride; ++word) {
          if (a[word] != b[word]) {
            return descending ? b[word] < a[word] : a[word] < b[word];
          }
        }
        return false;
      };
  std::stable_sort(permutation.begin(), permutation.end(), rank_less);
  words.clear();
  words.shrink_to_fit();
  auto result = empty_sparse_like(value);
  for (std::uint64_t position = 0U; position < count; ++position) {
    static_cast<void>(write_sparse_container_element(
        result, position,
        container_packed_element(
            value, permutation[static_cast<std::size_t>(position)])));
  }
  value = std::move(result);
}

// Ordering methods (7.12.2) of a sparse fixed array: exact sparse forms
// where the result follows from the stored elements and the background,
// every element otherwise.
void order_sparse_container_value(
    ContainerValue& value,
    const ContainerOrderingOperator operation,
    const std::span<const ContainerPredicateNode> key,
    const std::function<std::uint32_t()>& random) {
  const auto count = sparse_container_element_count(value.type);
  const bool strings =
      value.type.element_kind == ContainerElementKind::String;
  if (operation == ContainerOrderingOperator::shuffle) {
    if (!key.empty() || !random) {
      throw std::invalid_argument{
          "container shuffle requires an unkeyed random source"};
    }
    if (count > std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error{
          "container shuffle exceeds the deterministic index domain"};
    }
    if (!sparse_keyed(value)) {
      for (auto remaining = count; remaining > 1U; --remaining) {
        const auto other = uniform_below(
            random, static_cast<std::uint32_t>(remaining));
        if (other == remaining - 1U) {
          continue;
        }
        auto last = container_packed_element(value, remaining - 1U);
        set_container_packed_element(
            value, remaining - 1U, container_packed_element(value, other));
        set_container_packed_element(value, other, std::move(last));
      }
      return;
    }
    // The same swaps as a dense array, tracking only where each stored
    // element moves.
    std::unordered_map<std::uint64_t, std::size_t> stored_at;
    stored_at.reserve(value.keys.size());
    for (std::size_t index = 0U; index < value.keys.size(); ++index) {
      stored_at.emplace(sparse_key_ordinal(value.keys[index]), index);
    }
    for (auto remaining = count; remaining > 1U; --remaining) {
      const auto last = remaining - 1U;
      const std::uint64_t other = uniform_below(
          random, static_cast<std::uint32_t>(remaining));
      if (other == last) {
        continue;
      }
      const auto at_last = stored_at.find(last);
      const auto at_other = stored_at.find(other);
      if (at_last == stored_at.end() && at_other == stored_at.end()) {
        continue;
      }
      if (at_last != stored_at.end() && at_other != stored_at.end()) {
        std::swap(at_last->second, at_other->second);
        continue;
      }
      const bool from_last = at_last != stored_at.end();
      const auto moved = from_last ? at_last->second : at_other->second;
      stored_at.erase(from_last ? last : other);
      stored_at.emplace(from_last ? other : last, moved);
    }
    std::vector<std::pair<std::uint64_t, std::size_t>> placed(
        stored_at.begin(), stored_at.end());
    std::ranges::sort(placed);
    std::vector<PackedLogic4> keys;
    keys.reserve(placed.size());
    std::vector<PackedLogic4> elements;
    std::vector<std::string> string_elements;
    for (const auto& [ordinal, index] : placed) {
      keys.push_back(sparse_key(ordinal));
      if (strings) {
        string_elements.push_back(std::move(value.string_elements[index]));
      } else {
        elements.push_back(std::move(value.elements[index]));
      }
    }
    value.keys = std::move(keys);
    value.elements = std::move(elements);
    value.string_elements = std::move(string_elements);
    return;
  }
  if (operation == ContainerOrderingOperator::reverse) {
    if (!key.empty()) {
      throw std::invalid_argument{
          "SimIR container reverse cannot have key metadata"};
    }
    if (!sparse_keyed(value)) {
      for (std::uint64_t low = 0U; low < count / 2U; ++low) {
        const auto high = count - 1U - low;
        auto element = container_packed_element(value, low);
        set_container_packed_element(
            value, low, container_packed_element(value, high));
        set_container_packed_element(value, high, std::move(element));
      }
      return;
    }
    std::reverse(value.keys.begin(), value.keys.end());
    for (auto& stored : value.keys) {
      stored = sparse_key(count - 1U - sparse_key_ordinal(stored));
    }
    std::reverse(value.elements.begin(), value.elements.end());
    std::reverse(
        value.string_elements.begin(), value.string_elements.end());
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
  if (strings) {
    // As for a dense array, ordering applies to packed elements only.
    return;
  }
  const bool descending =
      operation == ContainerOrderingOperator::descending;
  const auto ordered =
      [&](const PackedLogic4& left, const PackedLogic4& right) {
        return descending ? element_less(value.type, right, left)
                          : element_less(value.type, left, right);
      };
  const auto background = sparse_container_background(value);
  if (key.empty() && sparse_keyed(value)) {
    // Stored elements ordered before the background come first, then every
    // unstored copy of it, then the rest. No stored element equals the
    // background, and equivalent elements are equal.
    auto sorted = value.elements;
    std::stable_sort(sorted.begin(), sorted.end(), ordered);
    const auto before = static_cast<std::uint64_t>(
        std::partition_point(
            sorted.begin(), sorted.end(),
            [&](const PackedLogic4& element) {
              return ordered(element, background);
            })
        - sorted.begin());
    const auto after = sorted.size() - before;
    value.keys.clear();
    for (std::uint64_t index = 0U; index < before; ++index) {
      value.keys.push_back(sparse_key(index));
    }
    for (std::uint64_t index = 0U; index < after; ++index) {
      value.keys.push_back(sparse_key(count - after + index));
    }
    value.elements = std::move(sorted);
    return;
  }
  if (!key.empty() && sparse_keyed(value) && !predicate_reads_index(key)) {
    // Every unstored element has the background's key. Stored elements
    // with an equivalent key keep their place among those copies, as a
    // stable sort leaves them.
    struct Entry {
      std::uint64_t ordinal{};
      std::uint64_t stored_before{};
      PackedLogic4 element;
      PackedLogic4 key;
    };
    std::vector<Entry> entries;
    entries.reserve(value.keys.size());
    for (std::size_t index = 0U; index < value.keys.size(); ++index) {
      const auto ordinal = sparse_key_ordinal(value.keys[index]);
      entries.push_back(Entry{
          ordinal, index, value.elements[index],
          evaluate_container_predicate(
              value.type, value.elements[index],
              container_declared_index(value.type, ordinal), key)});
    }
    const auto background_key = evaluate_container_predicate(
        value.type, background, 0, key);
    std::stable_sort(
        entries.begin(), entries.end(),
        [&](const Entry& left, const Entry& right) {
          return ordered(left.key, right.key);
        });
    const auto group_begin = std::partition_point(
        entries.begin(), entries.end(), [&](const Entry& entry) {
          return ordered(entry.key, background_key);
        });
    const auto group_end = std::partition_point(
        group_begin, entries.end(), [&](const Entry& entry) {
          return !ordered(background_key, entry.key);
        });
    const auto before =
        static_cast<std::uint64_t>(group_begin - entries.begin());
    const auto after =
        static_cast<std::uint64_t>(entries.end() - group_end);
    value.keys.clear();
    value.elements.clear();
    std::uint64_t position{};
    for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
      if (entry < group_begin) {
        position = static_cast<std::uint64_t>(entry - entries.begin());
      } else if (entry < group_end) {
        position = before
            + static_cast<std::uint64_t>(entry - group_begin)
            + entry->ordinal - entry->stored_before;
      } else {
        position = count - after
            + static_cast<std::uint64_t>(entry - group_end);
      }
      value.keys.push_back(sparse_key(position));
      value.elements.push_back(std::move(entry->element));
    }
    return;
  }
  order_sparse_by_rank(value, descending, key);
}

// Locator methods (7.12.1) of a sparse fixed array. With an index-free
// predicate and keyed storage, only the stored elements and one copy of the
// background (at the first unstored ordinal) can decide the result.
void locate_sparse_container_values(
    ContainerValue& destination,
    const ContainerValue source,
    const ContainerLocatorOperator operation,
    const std::span<const ContainerPredicateNode> predicate,
    const std::span<const ContainerPredicateNode> transformation,
    const bool index_result) {
  const auto& type = source.type;
  const auto count = sparse_container_element_count(type);
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
  const auto append_match =
      [&](const std::uint64_t ordinal, const PackedLogic4& element) {
        if (index_result) {
          append(PackedLogic4::from_aval_bval(
              32,
              static_cast<std::uint32_t>(
                  container_declared_index(type, ordinal)),
              0));
        } else {
          append(element);
        }
      };
  const auto background = sparse_container_background(source);
  const bool keyed = sparse_keyed(source)
      && !predicate_reads_index(predicate)
      && !predicate_reads_index(transformation);
  if (operation >= ContainerLocatorOperator::find) {
    const bool select_last =
        operation == ContainerLocatorOperator::find_last
        || operation == ContainerLocatorOperator::find_last_index;
    const bool select_one =
        select_last
        || operation == ContainerLocatorOperator::find_first
        || operation == ContainerLocatorOperator::find_first_index;
    const auto matches =
        [&](const PackedLogic4& element, const std::uint64_t ordinal) {
          return truth_value(evaluate_container_predicate(
                     type, element,
                     container_declared_index(type, ordinal), predicate))
              == Logic4::one;
        };
    if (keyed
        && (count == source.keys.size() || !matches(background, 0U))) {
      for (std::size_t step = 0U; step < source.keys.size(); ++step) {
        const auto index =
            select_last ? source.keys.size() - step - 1U : step;
        const auto ordinal = sparse_key_ordinal(source.keys[index]);
        if (!matches(source.elements[index], ordinal)) {
          continue;
        }
        append_match(ordinal, source.elements[index]);
        if (select_one) {
          break;
        }
      }
      return;
    }
    if (select_last) {
      for (std::uint64_t step = 0U; step < count; ++step) {
        const auto ordinal = count - step - 1U;
        const auto element = container_packed_element(source, ordinal);
        if (matches(element, ordinal)) {
          append_match(ordinal, element);
          break;
        }
      }
      return;
    }
    ContainerElementCursor cursor{source};
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
      const auto element = cursor.at(ordinal);
      if (!matches(element, ordinal)) {
        continue;
      }
      append_match(ordinal, element);
      if (select_one) {
        break;
      }
    }
    return;
  }
  const auto key_of =
      [&](const PackedLogic4& element, const std::uint64_t ordinal) {
        return transformation.empty()
            ? element
            : evaluate_container_predicate(
                  type, element,
                  container_declared_index(type, ordinal), transformation);
      };
  // Every element that can decide the result, in ordinal order.
  const auto visit = [&](const auto& callback) {
    if (!keyed) {
      ContainerElementCursor cursor{source};
      for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
        callback(ordinal, cursor.at(ordinal));
      }
      return;
    }
    const auto unstored = count > source.keys.size()
        ? std::optional<std::uint64_t>{first_unstored_ordinal(source)}
        : std::nullopt;
    bool background_visited = !unstored;
    for (std::size_t index = 0U; index < source.keys.size(); ++index) {
      const auto ordinal = sparse_key_ordinal(source.keys[index]);
      if (!background_visited && *unstored < ordinal) {
        callback(*unstored, background);
        background_visited = true;
      }
      callback(ordinal, source.elements[index]);
    }
    if (!background_visited) {
      callback(*unstored, background);
    }
  };
  if (operation == ContainerLocatorOperator::minimum
      || operation == ContainerLocatorOperator::maximum) {
    std::optional<PackedLogic4> selected;
    std::optional<PackedLogic4> selected_key;
    visit([&](const std::uint64_t ordinal, const PackedLogic4& element) {
      auto current = key_of(element, ordinal);
      const bool replace = !selected_key
          || (operation == ContainerLocatorOperator::minimum
                  ? element_less(type, current, *selected_key)
                  : element_less(type, *selected_key, current));
      if (replace) {
        selected = element;
        selected_key = std::move(current);
      }
    });
    if (selected) {
      append(std::move(*selected));
    }
    return;
  }
  const auto key_less =
      [&](const PackedLogic4& left, const PackedLogic4& right) {
        return element_less(type, left, right);
      };
  std::set<PackedLogic4, decltype(key_less)> seen(key_less);
  visit([&](const std::uint64_t ordinal, const PackedLogic4& element) {
    if (seen.insert(key_of(element, ordinal)).second) {
      append_match(ordinal, element);
    }
  });
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
  if (value.type.sparse) {
    const auto count = sparse_container_element_count(value.type);
    const auto fold =
        [&](const PackedLogic4& element, const std::uint64_t ordinal) {
          result = binary_value(
              binary, result,
              transformation.empty()
                  ? element
                  : evaluate_container_predicate(
                        value.type, element,
                        container_declared_index(value.type, ordinal),
                        transformation));
        };
    if (!sparse_keyed(value) || predicate_reads_index(transformation)) {
      ContainerElementCursor cursor{value};
      for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
        fold(cursor.at(ordinal), ordinal);
      }
      return result;
    }
    // The stored elements, then every unstored copy of the background.
    for (std::size_t index = 0U; index < value.keys.size(); ++index) {
      fold(value.elements[index], sparse_key_ordinal(value.keys[index]));
    }
    const auto background = sparse_container_background(value);
    fold_repeated(
        result, binary, operation,
        transformation.empty()
            ? background
            : evaluate_container_predicate(
                  value.type, background, 0, transformation),
        count - value.keys.size());
    return result;
  }
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
  if (value.type.sparse) {
    order_sparse_container_value(value, operation, key, random);
    return;
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
      swap_elements(
          remaining - 1U,
          static_cast<std::size_t>(uniform_below(
              random, static_cast<std::uint32_t>(remaining))));
    }
    return;
  }
  if (operation == ContainerOrderingOperator::reverse) {
    if (!key.empty()) {
      throw std::invalid_argument{
          "SimIR container reverse cannot have key metadata"};
    }
    std::reverse(value.elements.begin(), value.elements.end());
    std::reverse(
        value.string_elements.begin(), value.string_elements.end());
    std::reverse(
        value.nested_elements.begin(), value.nested_elements.end());
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
  if (source.type.sparse) {
    locate_sparse_container_values(
        destination, source, operation, predicate, transformation,
        index_result);
    return;
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
  if (value.type.sparse) {
    return static_cast<std::size_t>(
        sparse_container_element_count(value.type));
  }
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
    return container_packed_element(value, offset);
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
    set_container_packed_element(value, offset, std::move(replacement));
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

MemoryLoadResult load_memory_text_with_status(
    ContainerValue& target,
    const std::string_view text,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  validate_container_value(target);
  // A dynamic array or queue keeps its size; an associative array needs
  // integral indices (IEEE 1800-2017 21.4.1).
  if (target.type.associative
      && (target.type.string_indices || target.type.index_width > 64U
          || target.type.element_kind != ContainerElementKind::Packed)) {
    throw std::invalid_argument{
        "$readmemb/$readmemh associative target must have integral indices "
        "and packed elements"};
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

  if (target.type.associative) {
    // Each address loaded creates the element of that index (21.4.1); the
    // start and finish addresses bound the loaded window.
    const auto step = start && finish && *finish < *start ? -1 : 1;
    const auto in_window = [&](const std::int64_t index) {
      const auto low = step > 0 ? start : finish;
      const auto high = step > 0 ? finish : start;
      return (!low || index >= *low) && (!high || index <= *high);
    };
    const auto width = target.type.index_width;
    const auto mask = width >= 64U
        ? ~std::uint64_t{}
        : (std::uint64_t{1} << width) - 1U;
    auto current = static_cast<std::int64_t>(start.value_or(0));
    for (const auto& item : tokens) {
      if (item.starts_with('@')) {
        current = parse_address(std::string_view{item}.substr(1));
        if (!in_window(current)) {
          throw std::out_of_range{
              "read-memory @address is outside the selected range"};
        }
        continue;
      }
      const auto value = parse_data(item, target.type.element_width);
      if (!in_window(current)) {
        continue;
      }
      auto key = PackedLogic4::from_aval_bval(
          width, static_cast<std::uint64_t>(current) & mask, 0U);
      const auto position = std::ranges::lower_bound(
          replacement.keys, key,
          [&](const PackedLogic4& left, const PackedLogic4& right) {
            return associative_index_key_less(
                replacement.type, left, right);
          });
      const auto offset = static_cast<std::size_t>(
          position - replacement.keys.begin());
      if (position == replacement.keys.end()
          || associative_index_key_less(
              replacement.type, key, *position)) {
        replacement.keys.insert(position, std::move(key));
        replacement.elements.insert(
            replacement.elements.begin()
                + static_cast<std::ptrdiff_t>(offset),
            value);
      } else {
        replacement.elements[offset] = value;
      }
      current += step;
    }
    validate_container_value(replacement);
    target = std::move(replacement);
    return MemoryLoadResult{false};
  }
  const auto count = memory_element_count(target);
  if (count == 0) {
    throw std::invalid_argument{
        "read-memory target has no materialized elements"};
  }
  // A dynamic array or queue is addressed from 0 (7.5, 7.10).
  const bool linear = target.type.dimensions.size() > 1U
      || !target.type.fixed;
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
  bool has_address_entries{};
  bool sequential_data_outside_window{};
  std::size_t data_word_count{};
  for (const auto& item : tokens) {
    if (item.starts_with('@')) {
      has_address_entries = true;
      current = parse_address(
          std::string_view{item}.substr(1));
      if (!in_declared_range(current)
          || !in_window(current)) {
        throw std::out_of_range{
            "read-memory @address is outside the selected range"};
      }
      continue;
    }
    ++data_word_count;
    if (!in_window(current)) {
      sequential_data_outside_window = true;
      // Keep scanning so a later @address is still checked. Without file
      // addresses, a count mismatch is a warning and excess words are ignored.
      // Still validate the syntax of each ignored data token.
      if (replacement.type.element_kind
          == ContainerElementKind::String) {
        (void)parse_memory_string(item);
      } else {
        const auto width = memory_packed_element(
            replacement, 0).width();
        (void)parse_data(item, width);
      }
      continue;
    }
    if (!in_declared_range(current)) {
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
      set_container_string_element(
          replacement, offset, parse_memory_string(item));
    } else {
      const auto width = memory_packed_element(
          replacement, offset).width();
      replace_memory_packed_element(
          replacement, offset, parse_data(item, width));
    }
    current += step;
  }
  if (has_address_entries && sequential_data_outside_window) {
    throw std::out_of_range{
        "read-memory data exceeds the selected range"};
  }
  const auto selected_word_count = static_cast<std::size_t>(
      (first <= last ? last - first : first - last) + 1);
  const MemoryLoadResult result {
      !has_address_entries && data_word_count != selected_word_count };
  validate_container_value(replacement);
  target = std::move(replacement);
  return result;
}

void load_memory_text(
    ContainerValue& target,
    const std::string_view text,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  (void)load_memory_text_with_status(
      target, text, hexadecimal, start, finish);
}

std::string write_memory_text(
    const ContainerValue& source,
    const bool hexadecimal,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish) {
  validate_container_value(source);
  if (source.type.associative
      && (source.type.string_indices || source.type.index_width > 64U
          || source.type.element_kind != ContainerElementKind::Packed)) {
    throw std::invalid_argument{
        "$writememb/$writememh associative source must have integral "
        "indices and packed elements"};
  }
  const auto count = memory_element_count(source);
  if (count == 0 && source.type.fixed) {
    throw std::invalid_argument{
        "write-memory source has no materialized elements"};
  }
  // A dynamic array or queue is addressed from 0 (7.5, 7.10).
  const bool linear = source.type.dimensions.size() > 1U
      || !source.type.fixed;
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
  if (source.type.associative) {
    // The existing elements in index order, each run of consecutive
    // indices after an address specifier (21.5.3).
    const auto window_low = start && finish
        ? std::min(*start, *finish)
        : start.value_or(std::numeric_limits<std::int32_t>::min());
    const auto window_high = start && finish
        ? std::max(*start, *finish)
        : finish.value_or(std::numeric_limits<std::int32_t>::max());
    std::optional<std::int64_t> next;
    for (std::size_t offset = 0; offset < source.keys.size(); ++offset) {
      const auto key = source.type.signed_indices
          ? source.keys[offset].known_signed_value()
          : std::optional<std::int64_t>{};
      const auto unsigned_key = source.keys[offset].known_unsigned_value();
      if (!key && !unsigned_key) {
        continue;
      }
      const auto index = key ? *key
                             : static_cast<std::int64_t>(*unsigned_key);
      if (index < window_low || index > window_high) {
        continue;
      }
      if (index != next.value_or(0)) {
        std::array<char, 24> digits{};
        const auto written = std::to_chars(
            digits.data(), digits.data() + digits.size(), index, 16);
        result.push_back('@');
        result.append(digits.data(), written.ptr);
        result.push_back('\n');
      }
      result += format(source.elements[offset]);
      result.push_back('\n');
      next = index + 1;
    }
    if (result.size() > maximum_memory_file_bytes)
      throw std::length_error{"write-memory file exceeds the 1 MiB limit"};
    return result;
  }
  if (count == 0) {
    return result;
  }
  const auto in_range = [&](const std::int64_t index) {
    return index >= low && index <= high;
  };
  if (!in_range(first) || !in_range(last)) {
    throw std::out_of_range{
        "write-memory start/finish is outside the source range"};
  }
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
        ? format_memory_string(container_string_element(source, offset))
        : format(memory_packed_element(source, offset));
    result.push_back('\n');
    if (result.size() > maximum_memory_file_bytes)
      throw std::length_error{"write-memory file exceeds the 1 MiB limit"};
    if (index == last) break;
  }
  if (result.size() > maximum_memory_file_bytes)
    throw std::length_error{"write-memory file exceeds the 1 MiB limit"};
  return result;
}

}  // namespace fsim::runtime::simir
