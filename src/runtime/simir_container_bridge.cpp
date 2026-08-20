// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <functional>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] std::optional<std::size_t> checked_add(
    const std::size_t left,
    const std::size_t right) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] std::optional<std::size_t> checked_multiply(
    const std::size_t left,
    const std::size_t right) {
  if (left != 0
      && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

[[nodiscard]] std::optional<std::size_t> fixed_count(
    const ContainerType& type) {
  if (!type.fixed || type.dimensions.empty()) return std::nullopt;
  std::size_t result = 1;
  for (const auto& dimension : type.dimensions) {
    const auto count = static_cast<std::uint64_t>(
        dimension.first >= dimension.second
            ? static_cast<std::int64_t>(dimension.first)
                  - dimension.second
            : static_cast<std::int64_t>(dimension.second)
                  - dimension.first) + 1U;
    if (count > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    const auto multiplied =
        checked_multiply(result, static_cast<std::size_t>(count));
    if (!multiplied) return std::nullopt;
    result = *multiplied;
  }
  return result;
}

[[nodiscard]] std::optional<std::size_t> element_width(
    const ContainerType& type) {
  switch (type.element_kind) {
  case ContainerElementKind::Packed:
    return type.element_width == 0
        ? std::nullopt
        : std::optional<std::size_t>{type.element_width};
  case ContainerElementKind::Scalar:
  case ContainerElementKind::String:
    return std::nullopt;
  case ContainerElementKind::Container:
    if (type.element_types.size() != 1U) return std::nullopt;
    return container_signal_bridge_width(type.element_types.front());
  case ContainerElementKind::Aggregate: {
    if (type.union_aggregate || type.element_types.empty()) {
      return std::nullopt;
    }
    std::size_t result{};
    for (const auto& member : type.element_types) {
      const auto width = container_signal_bridge_width(member);
      if (!width) return std::nullopt;
      const auto added = checked_add(result, *width);
      if (!added) return std::nullopt;
      result = *added;
    }
    return result;
  }
  }
  return std::nullopt;
}

template <typename Value, typename Callback>
void for_each_leaf(Value& value, Callback&& callback) {
  if (value.type.aggregate_value) {
    for (auto& member : value.nested_elements) {
      for_each_leaf(member, callback);
    }
    return;
  }
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar:
    for (auto& element : value.elements) callback(element);
    return;
  case ContainerElementKind::String:
    throw std::invalid_argument{
        "string values cannot cross a packed container boundary"};
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate:
    for (auto& element : value.nested_elements) {
      for_each_leaf(element, callback);
    }
    return;
  }
}

}  // namespace

std::optional<std::size_t>
container_packed_element_width(const ContainerType& type) {
  return element_width(type);
}

std::optional<std::size_t>
container_signal_bridge_width(const ContainerType& type) {
  const auto width = element_width(type);
  if (!width) return std::nullopt;
  if (type.aggregate_value) return width;
  const auto count = fixed_count(type);
  return count ? checked_multiply(*count, *width) : std::nullopt;
}

PackedLogic4 pack_container_signal_value(
    const ContainerValue& value,
    const bool logic9) {
  validate_container_value(value);
  const auto width = container_signal_bridge_width(value.type);
  if (!width) {
    throw std::invalid_argument{
        "container type cannot cross a packed signal boundary"};
  }
  PackedLogic4 result(*width, Logic4::zero);
  std::size_t cursor = *width;
  for_each_leaf(
      value,
      [&](const PackedLogic4& element) {
        if (element.width() > cursor) {
          throw std::invalid_argument{
              "container boundary leaf width exceeds its descriptor"};
        }
        cursor -= element.width();
        result.insert_bits(element, cursor);
      });
  if (cursor != 0) {
    throw std::invalid_argument{
        "container boundary descriptor has unfilled storage"};
  }
  return logic9 ? result.promoted_to_logic9() : result;
}

void unpack_container_signal_value(
    ContainerValue& value,
    const PackedLogic4& packed) {
  validate_container_value(value);
  const auto width = container_signal_bridge_width(value.type);
  if (!width || packed.width() != *width) {
    throw std::invalid_argument{
        "packed signal/container boundary width mismatch"};
  }
  std::size_t cursor = *width;
  for_each_leaf(
      value,
      [&](PackedLogic4& element) {
        if (element.width() > cursor) {
          throw std::invalid_argument{
              "container boundary leaf width exceeds its descriptor"};
        }
        cursor -= element.width();
        auto replacement = packed.extract_bits(cursor, element.width());
        if (replacement.is_logic9()) {
          PackedLogic4 logic4(replacement.width(), Logic4::zero);
          for (std::size_t bit = 0; bit < replacement.width(); ++bit) {
            logic4.set(bit, replacement.get(bit));
          }
          replacement = std::move(logic4);
        }
        element = std::move(replacement);
      });
  if (cursor != 0) {
    throw std::invalid_argument{
        "container boundary descriptor has unconsumed storage"};
  }
  validate_container_value(value);
}

}  // namespace fsim::runtime::simir
