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

/// One index range of a VHDL array value whose bounds are known only at run
/// time (IEEE 1076-2008 5.3.2.2); fixed when the object is elaborated.
struct VhdlShapeRange {
  std::int64_t left{};
  std::int64_t right{};
  bool descending{};
  friend bool operator==(const VhdlShapeRange&,
                         const VhdlShapeRange&) = default;
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
  // A VHDL array with run-time index ranges: one packed payload element in
  // the VHDL packed layout (leftmost element in the most significant bits),
  // elements of `element_width` bits, and `vhdl_rank` ranges in the value's
  // shape.
  bool vhdl_shaped{};
  std::uint32_t vhdl_rank{};
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
  // The index ranges of a VHDL shaped value; empty for every other value.
  std::vector<VhdlShapeRange> shape;
  // An associative array's `default:` value from an assignment pattern: a
  // read of a missing key returns it (IEEE 1800-2017 7.9.11). Run-time
  // state only; elaborated initial values never carry one.
  std::optional<PackedLogic4> associative_default;
  std::optional<std::string> associative_default_text;
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

/// Compare valid integral associative-array keys using their declared signed
/// ordering. Both operands must be known Logic4 values of `type.index_width`.
[[nodiscard]] bool associative_index_key_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right);

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

/// The value profile of one element of a container whose elements are
/// themselves values: a nested container, or an unpacked aggregate held as
/// a boxed aggregate value. Packed, scalar and string elements have none.
[[nodiscard]] std::optional<ContainerType>
nested_element_container_type(const ContainerType& type);

/// Whether `initializer` may initialize a dynamic array of type `target`
/// in `new[n](initializer)`: the same type, or a queue or one-dimensional
/// fixed-size array of the same elements (IEEE 1800-2017 7.5.1, 7.6).
[[nodiscard]] bool new_initializer_compatible(
    const ContainerType& initializer, const ContainerType& target);

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

/// Fixed array bounds may differ when every dimension has the same extent.
/// Other element and container profile requirements remain unchanged.
[[nodiscard]] bool container_equality_compatible(
    const ContainerType& lhs, const ContainerType& rhs);

/// Compare compatible bounded containers with SystemVerilog logical or
/// case-equality semantics.
[[nodiscard]] PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    bool case_equal);

}  // namespace fsim::runtime::simir
