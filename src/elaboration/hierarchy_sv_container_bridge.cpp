// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <limits>

namespace fsim::elaboration {

namespace {

struct BridgeShape {
  enum class Kind {
    leaf,
    array,
    aggregate,
  };

  Kind kind{Kind::leaf};
  std::uint64_t width{};
  bool two_state{};
  std::uint64_t count{1};
  std::vector<BridgeShape> children;

  friend bool operator==(
      const BridgeShape&, const BridgeShape&) = default;
};

[[nodiscard]] std::optional<std::uint64_t> range_count(
    const std::int64_t left,
    const std::int64_t right) {
  const auto low = std::min(left, right);
  const auto high = std::max(left, right);
  const auto distance =
      static_cast<std::uint64_t>(high - low);
  if (distance == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return distance + 1U;
}

[[nodiscard]] BridgeShape wrap_array(
    BridgeShape element,
    const std::uint64_t count) {
  if (count == 1) return element;
  BridgeShape result;
  result.kind = BridgeShape::Kind::array;
  result.count = count;
  result.children.push_back(std::move(element));
  return result;
}

[[nodiscard]] std::optional<BridgeShape>
container_element_shape(const ContainerType& type);

[[nodiscard]] std::optional<BridgeShape> container_shape(
    const ContainerType& type) {
  auto element = container_element_shape(type);
  if (!element) return std::nullopt;
  if (type.aggregate_value) return element;
  if (!type.fixed || type.queue || type.associative
      || type.dimensions.empty()) {
    return std::nullopt;
  }
  for (auto dimension = type.dimensions.rbegin();
       dimension != type.dimensions.rend(); ++dimension) {
    const auto count =
        range_count(dimension->first, dimension->second);
    if (!count) return std::nullopt;
    *element = wrap_array(std::move(*element), *count);
  }
  return element;
}

[[nodiscard]] std::optional<BridgeShape>
container_element_shape(const ContainerType& type) {
  switch (type.element_kind) {
    case ContainerElementKind::Packed:
      return BridgeShape{
          BridgeShape::Kind::leaf,
          type.element_width,
          type.two_state,
          1,
          {}};
    case ContainerElementKind::Scalar:
    case ContainerElementKind::String:
      return std::nullopt;
    case ContainerElementKind::Container:
      if (type.element_types.size() != 1) return std::nullopt;
      return container_shape(type.element_types.front());
    case ContainerElementKind::Aggregate: {
      if (type.union_aggregate || type.element_types.empty()) {
        return std::nullopt;
      }
      BridgeShape result;
      result.kind = BridgeShape::Kind::aggregate;
      for (const auto& member : type.element_types) {
        auto child = container_shape(member);
        if (!child) return std::nullopt;
        result.children.push_back(std::move(*child));
      }
      return result;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BridgeShape> frontend_shape(
    const frontend::Type& type);

[[nodiscard]] std::optional<BridgeShape> packed_member_shape(
    const frontend::PackedMember& member) {
  if (!member.nested_types.empty()) {
    if (member.nested_types.size() != 1) return std::nullopt;
    return frontend_shape(member.nested_types.front());
  }
  const auto width = member.width();
  if (!width || *width == 0) return std::nullopt;
  return BridgeShape{
      BridgeShape::Kind::leaf,
      *width,
      elaboration_detail::is_two_state_domain(member.domain),
      1,
      {}};
}

[[nodiscard]] std::optional<BridgeShape> frontend_shape(
    const frontend::Type& type) {
  if (!type.packed_members.empty()) {
    BridgeShape result;
    result.kind = BridgeShape::Kind::aggregate;
    for (const auto& member : type.packed_members) {
      auto child = packed_member_shape(member);
      if (!child) return std::nullopt;
      result.children.push_back(std::move(*child));
    }
    return result;
  }
  if (type.vhdl_array) {
    const auto& array = *type.vhdl_array;
    if (!array.flat_width || *array.flat_width == 0
        || array.element_types.size() != 1
        || array.dimensions.empty()) {
      return std::nullopt;
    }
    const auto& element_type = array.element_types.front();
    const bool scalar_element =
        !element_type.vhdl_array
        && element_type.packed_members.empty()
        && element_type.width().value_or(0) == 1;
    if (scalar_element) {
      return BridgeShape{
          BridgeShape::Kind::leaf,
          *array.flat_width,
          elaboration_detail::is_two_state_domain(
              array.element_domain),
          1,
          {}};
    }
    auto element = frontend_shape(element_type);
    if (!element) return std::nullopt;
    for (auto dimension = array.dimensions.rbegin();
         dimension != array.dimensions.rend(); ++dimension) {
      if (!dimension->range || dimension->null) {
        return std::nullopt;
      }
      const auto count = range_count(
          dimension->range->left, dimension->range->right);
      if (!count) return std::nullopt;
      *element = wrap_array(std::move(*element), *count);
    }
    return element;
  }
  const auto width = type.width();
  if (!width || *width == 0) return std::nullopt;
  return BridgeShape{
      BridgeShape::Kind::leaf,
      *width,
      elaboration_detail::is_two_state_domain(type.domain),
      1,
      {}};
}

[[nodiscard]] frontend::Type signal_type(
    const SignalInfo& signal) {
  frontend::Type result;
  result.domain = signal.source_domain;
  result.spelling = signal.type_name;
  result.systemverilog_scalar = signal.systemverilog_scalar;
  result.is_signed = signal.is_signed;
  result.packed_range = signal.packed_range;
  result.vhdl_array = signal.vhdl_array;
  result.packed_members = signal.packed_members;
  result.nominal_type = signal.nominal_type;
  return result;
}

}  // namespace

std::optional<ContainerObjectId>
HierarchyBuilder::connect_cross_language_container_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const SignalMap& parent_signals,
    const ContainerType& expected) {
  const auto reject = [&](std::string message) {
    report(
        "FSIM-ELAB-SVPORT-004",
        std::move(message),
        connection.span);
    return std::optional<ContainerObjectId>{};
  };
  if (connection.value.kind
      != frontend::ExpressionKind::Identifier) {
    return reject(
        "a cross-language SystemVerilog container port requires "
        "a direct fixed-shape signal actual at '" + path + "."
        + port.name + "'");
  }
  const auto actual =
      parent_signals.find(connection.value.text);
  if (actual == parent_signals.end()) {
    return reject(
        "unknown cross-language container signal '"
        + connection.value.text + "' on instance '" + path + "'");
  }
  const auto& actual_info =
      design_.signal_info_.at(actual->second);
  auto formal_shape = container_shape(expected);
  auto actual_shape = frontend_shape(signal_type(actual_info));
  if (!formal_shape || !actual_shape
      || *formal_shape != *actual_shape) {
    return reject(
        "cross-language SystemVerilog container port '" + path + "."
        + port.name + "' requires identical fixed dimensions, "
          "aggregate member grouping, leaf widths, and state domains");
  }

  const auto index = design_.container_objects_.size();
  const auto object = static_cast<ContainerObjectId>(index);
  if (static_cast<std::size_t>(object) != index) {
    throw std::length_error{
        "too many elaborated container objects"};
  }
  const auto full_name = path + "." + port.name;
  design_.container_object_info_.push_back(
      ContainerObjectInfo{
          object,
          full_name,
          expected,
          port.span,
          true,
          port.direction,
          std::nullopt});
  design_.container_objects_.push_back(
      ContainerObject{
          full_name,
          default_container_value(expected),
          std::nullopt});
  const bool writable =
      port.direction == frontend::PortDirection::Output
      || port.direction == frontend::PortDirection::Buffer
      || port.direction == frontend::PortDirection::Inout;
  const bool readable =
      port.direction == frontend::PortDirection::Input
      || port.direction == frontend::PortDirection::Inout;
  if (!readable && !writable) {
    return reject(
        "cross-language SystemVerilog container port '" + full_name
        + "' has an unsupported ownership direction");
  }
  design_.container_signal_aliases_.push_back(
      runtime::simir::ContainerSignalAlias{
          object, actual->second, readable, writable});
  return object;
}

}  // namespace fsim::elaboration
