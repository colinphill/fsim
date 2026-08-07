// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_value.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fsim::runtime {

struct VhdlVhpiCompositeValue;

struct VhdlVhpiArrayValue {
  std::vector<VhdlVhpiRange> dimensions;
  std::vector<VhdlVhpiCompositeValue> elements;
};

struct VhdlVhpiRecordValue {
  std::vector<VhdlVhpiCompositeValue> fields;
};

struct VhdlVhpiCompositeValue {
  std::variant<
      VhdlVhpiValuePayload,
      VhdlVhpiArrayValue,
      VhdlVhpiRecordValue>
      value{VhdlVhpiValuePayload{false}};
};

struct VhdlVhpiRecordField {
  std::string name;
  fsim_vhpi_handle_v1 type{};
  VhdlVhpiValueProfile scalar_profile;
};

enum class VhdlVhpiCompositeKind : std::uint32_t {
  Array,
  Record,
};

struct VhdlVhpiCompositeTypeDescriptor {
  fsim_vhpi_handle_v1 type{};
  VhdlVhpiCompositeKind kind{VhdlVhpiCompositeKind::Array};
  bool unconstrained{};
  std::vector<VhdlVhpiRange> dimensions;
  fsim_vhpi_handle_v1 element_type{};
  VhdlVhpiValueProfile element_scalar_profile;
  std::vector<VhdlVhpiRecordField> fields;
};

enum class VhdlVhpiCompositeError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidDeclaration,
  InvalidType,
  InvalidDescriptor,
  InvalidRange,
  InvalidShape,
  InvalidIndex,
  TypeMismatch,
  AlreadyPublished,
  AlreadyBound,
  NotFound,
  BufferTooSmall,
  DepthLimit,
  NodeLimit,
  ResourceLimit,
};

struct VhdlVhpiCompositeResult {
  VhdlVhpiCompositeValue value;
  VhdlVhpiCompositeError error{VhdlVhpiCompositeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCompositeError::None;
  }
};

struct VhdlVhpiCompositeBufferResult {
  std::size_t required_size{};
  VhdlVhpiCompositeError error{VhdlVhpiCompositeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCompositeError::None;
  }
};

struct VhdlVhpiArrayOffsetResult {
  std::size_t value{};
  VhdlVhpiCompositeError error{VhdlVhpiCompositeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCompositeError::None;
  }
};

class VhdlVhpiCompositeSystem final {
 public:
  VhdlVhpiCompositeSystem(
      VhdlVhpiTypeSystem& types,
      VhdlVhpiValueSystem& values) noexcept;

  [[nodiscard]] VhdlVhpiCompositeError publish_type(
      const VhdlVhpiCompositeTypeDescriptor& descriptor);
  [[nodiscard]] VhdlVhpiCompositeError bind(
      fsim_vhpi_handle_v1 declaration,
      const VhdlVhpiCompositeValue& initial);
  [[nodiscard]] VhdlVhpiCompositeResult read(
      fsim_vhpi_handle_v1 declaration) const;
  [[nodiscard]] VhdlVhpiCompositeError write(
      fsim_vhpi_handle_v1 declaration,
      const VhdlVhpiCompositeValue& value);
  [[nodiscard]] VhdlVhpiCompositeBufferResult read_members(
      fsim_vhpi_handle_v1 declaration,
      std::span<VhdlVhpiCompositeValue> buffer) const;
  [[nodiscard]] VhdlVhpiArrayOffsetResult array_offset(
      fsim_vhpi_handle_v1 declaration,
      std::span<const std::int64_t> indices) const;
  [[nodiscard]] VhdlVhpiCompositeError write_array_element(
      fsim_vhpi_handle_v1 declaration,
      std::span<const std::int64_t> indices,
      const VhdlVhpiCompositeValue& value);

 private:
  struct Entry {
    fsim_vhpi_handle_v1 type{};
    VhdlVhpiCompositeValue value;
  };

  [[nodiscard]] static VhdlVhpiCompositeError type_error(
      VhdlVhpiTypeError error) noexcept;
  [[nodiscard]] static VhdlVhpiCompositeError value_error(
      VhdlVhpiValueError error) noexcept;
  [[nodiscard]] static VhdlVhpiCompositeError validate_ranges(
      std::span<const VhdlVhpiRange> ranges,
      std::size_t& elements) noexcept;
  [[nodiscard]] VhdlVhpiCompositeError validate_member_type(
      fsim_vhpi_handle_v1 type,
      const VhdlVhpiValueProfile& scalar_profile) const;
  [[nodiscard]] VhdlVhpiCompositeError validate_value(
      fsim_vhpi_handle_v1 type,
      const VhdlVhpiValueProfile& scalar_profile,
      const VhdlVhpiCompositeValue& value,
      std::uint32_t depth,
      std::uint32_t& nodes) const;
  [[nodiscard]] VhdlVhpiArrayOffsetResult array_offset_locked(
      const Entry& entry,
      std::span<const std::int64_t> indices) const;

  VhdlVhpiTypeSystem* types_{};
  VhdlVhpiValueSystem* values_{};
  mutable std::mutex mutex_;
  std::unordered_map<
      fsim_vhpi_handle_v1,
      VhdlVhpiCompositeTypeDescriptor>
      descriptors_;
  std::unordered_map<fsim_vhpi_handle_v1, Entry> entries_;
};

}  // namespace fsim::runtime
