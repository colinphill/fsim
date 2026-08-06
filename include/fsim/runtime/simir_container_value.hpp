// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

/// Owning storage budget for one materialized unpacked container, including
/// recursively owned string and aggregate/container element storage.
inline constexpr std::size_t maximum_container_storage_bytes =
    256U * 1024U * 1024U;

using ContainerDimension = std::pair<std::int32_t, std::int32_t>;

enum class ContainerElementKind : std::uint8_t {
  Packed,
  Scalar,
  String,
  Container,
  Aggregate,
};

struct ContainerType {
  ContainerElementKind element_kind{ContainerElementKind::Packed};
  SystemVerilogScalarKind scalar_kind{SystemVerilogScalarKind::None};
  std::uint32_t element_width{1};
  bool two_state{};
  bool signed_elements{};
  bool union_aggregate{};
  bool aggregate_value{};
  bool queue{};
  bool associative{};
  bool fixed{};
  std::uint32_t index_width{32};
  bool two_state_indices{};
  bool signed_indices{true};
  bool string_indices{};
  std::int32_t index_left{};
  std::int32_t index_right{};
  std::optional<std::uint64_t> maximum_elements;
  std::vector<ContainerDimension> dimensions;
  std::string element_nominal_type;
  // Container elements retain exactly one recursive element profile.
  // Aggregate elements retain one profile and optional name per member.
  std::vector<ContainerType> element_types;
  std::vector<std::string> member_names;
  friend bool operator==(const ContainerType&,
                         const ContainerType&) = default;
};

struct ContainerValue {
  ContainerType type;
  // Packed and scalar values share the established bounded bit storage. A
  // scalar profile determines the language comparison semantics of the bits.
  std::vector<PackedLogic4> elements;
  std::vector<std::string> string_elements;
  // Nested containers and heterogeneous aggregate members are value-owned.
  std::vector<ContainerValue> nested_elements;
  // Associative-array keys are canonical and positionally paired with the
  // active element vector. Other container kinds keep this empty.
  std::vector<PackedLogic4> keys;
  std::vector<std::string> string_keys;
  ContainerValue() = default;
  // Retain the original packed-container construction surface while the
  // additional storages remain an internal representation detail.
  ContainerValue(
      ContainerType value_type,
      std::vector<PackedLogic4> packed_elements,
      std::vector<PackedLogic4> associative_keys)
      : type(std::move(value_type)),
        elements(std::move(packed_elements)),
        keys(std::move(associative_keys)) {}
  friend bool operator==(const ContainerValue&,
                         const ContainerValue&) = default;
};

[[nodiscard]] constexpr std::size_t
maximum_container_elements(const ContainerType& type) noexcept {
  std::size_t bytes_per_element = sizeof(PackedLogic4);
  if (type.element_kind == ContainerElementKind::String) {
    bytes_per_element = sizeof(std::string);
  } else if (type.element_kind == ContainerElementKind::Container
             || type.element_kind == ContainerElementKind::Aggregate) {
    bytes_per_element = sizeof(ContainerValue);
  }
  if (type.associative) {
    bytes_per_element += type.string_indices
        ? sizeof(std::string) : sizeof(PackedLogic4);
  }
  return maximum_container_storage_bytes / bytes_per_element;
}

[[nodiscard]] std::size_t
container_value_size(const ContainerValue& value) noexcept;

/// Approximate recursively owned capacity used to enforce the per-value
/// resource budget independently of host allocator behavior.
[[nodiscard]] std::size_t
container_value_storage_bytes(const ContainerValue& value);

/// Resize a dynamic container, preserving the initializer prefix when one is
/// supplied and default-initializing every remaining element.
void resize_container_value(
    ContainerValue& target,
    std::size_t size,
    const ContainerValue* initializer = nullptr);

/// Construct the language-defined initial value for a container type. Fixed
/// unpacked arrays and aggregate members are materialized densely.
[[nodiscard]] ContainerValue
default_container_value(const ContainerType& type);

/// Apply SystemVerilog four-state conditional selection to exactly
/// compatible bounded container values.
void select_container_value(
    ContainerValue& destination,
    const PackedLogic4& condition,
    const ContainerValue& when_true,
    const ContainerValue& when_false);

/// Compare exactly compatible bounded containers with SystemVerilog logical
/// or case-equality semantics.
[[nodiscard]] PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    bool case_equal);

}  // namespace fsim::runtime::simir
