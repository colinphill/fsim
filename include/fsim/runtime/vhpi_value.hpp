// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_type.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fsim::runtime {

struct VhdlVhpiEnumerationValue {
  std::uint32_t position{};
};

struct VhdlVhpiPhysicalValue {
  std::int64_t magnitude{};
  std::uint32_t unit_position{};
};

struct VhdlVhpiAccessValue {
  fsim_vhpi_handle_v1 object{};
};

using VhdlVhpiValuePayload = std::variant<
    bool,
    char32_t,
    std::int64_t,
    double,
    std::uint64_t,
    VhdlVhpiEnumerationValue,
    VhdlVhpiPhysicalValue,
    VhdlVhpiAccessValue>;

struct VhdlVhpiPhysicalUnit {
  std::string name;
  std::uint64_t primary_multiplier{1};
};

struct VhdlVhpiValueProfile {
  fsim_vhpi_handle_v1 type{};
  std::vector<std::string> enumeration_literals;
  std::vector<VhdlVhpiPhysicalUnit> physical_units;
  fsim_vhpi_handle_v1 designated_subtype{};
};

enum class VhdlVhpiValueError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidDeclaration,
  InvalidType,
  InvalidProfile,
  TypeMismatch,
  RangeViolation,
  InvalidPosition,
  InvalidUnit,
  InvalidAccess,
  AlreadyBound,
  NotBound,
  BufferTooSmall,
  ResourceLimit,
};

struct VhdlVhpiValueResult {
  VhdlVhpiValuePayload value{false};
  VhdlVhpiValueError error{VhdlVhpiValueError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiValueError::None;
  }
};

struct VhdlVhpiValueProfileResult {
  VhdlVhpiValueProfile value;
  VhdlVhpiValueError error{VhdlVhpiValueError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiValueError::None;
  }
};

struct VhdlVhpiTextResult {
  std::size_t required_size{};
  VhdlVhpiValueError error{VhdlVhpiValueError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiValueError::None;
  }
};

class VhdlVhpiValueSystem final {
 public:
  VhdlVhpiValueSystem(
      VhdlVhpiObjectRegistry& objects,
      VhdlVhpiTypeSystem& types) noexcept;

  [[nodiscard]] VhdlVhpiValueError bind(
      fsim_vhpi_handle_v1 declaration,
      const VhdlVhpiValueProfile& profile,
      const VhdlVhpiValuePayload& initial);
  [[nodiscard]] VhdlVhpiValueResult read(
      fsim_vhpi_handle_v1 declaration) const;
  [[nodiscard]] VhdlVhpiValueError write(
      fsim_vhpi_handle_v1 declaration,
      const VhdlVhpiValuePayload& value);
  [[nodiscard]] VhdlVhpiValueProfileResult profile(
      fsim_vhpi_handle_v1 declaration) const;
  [[nodiscard]] VhdlVhpiTextResult enumeration_literal(
      fsim_vhpi_handle_v1 declaration,
      std::span<char> buffer) const;
  [[nodiscard]] VhdlVhpiValueError validate_profile_for_type(
      fsim_vhpi_handle_v1 type,
      const VhdlVhpiValueProfile& profile) const;
  [[nodiscard]] VhdlVhpiValueError validate_typed_value(
      fsim_vhpi_handle_v1 type,
      const VhdlVhpiValueProfile& profile,
      const VhdlVhpiValuePayload& value) const;

 private:
  struct Entry {
    VhdlVhpiTypeDescriptor type;
    VhdlVhpiValueProfile profile;
    VhdlVhpiValuePayload value;
  };

  [[nodiscard]] static VhdlVhpiValueError type_error(
      VhdlVhpiTypeError error) noexcept;
  [[nodiscard]] VhdlVhpiValueError validate_profile(
      const VhdlVhpiTypeDescriptor& type,
      const VhdlVhpiValueProfile& profile) const;
  [[nodiscard]] VhdlVhpiValueError validate_value(
      const Entry& entry,
      const VhdlVhpiValuePayload& value) const;
  [[nodiscard]] VhdlVhpiValueError validate_live(
      fsim_vhpi_handle_v1 declaration) const;

  [[maybe_unused]] VhdlVhpiObjectRegistry* objects_{};
  VhdlVhpiTypeSystem* types_{};
  mutable std::mutex mutex_;
  std::unordered_map<fsim_vhpi_handle_v1, Entry> entries_;
};

}  // namespace fsim::runtime
