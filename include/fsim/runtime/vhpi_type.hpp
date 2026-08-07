// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_object.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiScalarKind : std::uint32_t {
  Boolean,
  Bit,
  Character,
  Integer,
  Real,
  Time,
  Enumeration,
  Physical,
  Access,
  Array,
  Record,
  File,
  Protected,
  Logic9,
};

enum class VhdlVhpiDirection : std::uint32_t {
  To,
  Downto,
};

struct VhdlVhpiRange {
  std::int64_t left{};
  std::int64_t right{};
  VhdlVhpiDirection direction{VhdlVhpiDirection::To};
  bool is_null{};
};

enum class VhdlVhpiConstraintKind : std::uint32_t {
  None,
  Range,
  Array,
  Record,
};

struct VhdlVhpiConstraint {
  VhdlVhpiConstraintKind kind{VhdlVhpiConstraintKind::None};
  std::optional<VhdlVhpiRange> range;
  std::vector<VhdlVhpiConstraint> children;
};

struct VhdlVhpiTypeDescriptor {
  VhdlVhpiScalarKind scalar_kind{VhdlVhpiScalarKind::Boolean};
  fsim_vhpi_handle_v1 base_type{};
  VhdlVhpiConstraint constraint;
  bool resolved{};
  fsim_vhpi_handle_v1 resolution_function{};
};

enum class VhdlVhpiTypeError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidTypeObject,
  InvalidDeclaration,
  InvalidScalarKind,
  InvalidBaseType,
  InvalidConstraint,
  InvalidRange,
  InvalidResolution,
  AlreadyPublished,
  NotFound,
  DepthLimit,
  NodeLimit,
  ResourceLimit,
};

struct VhdlVhpiTypeResult {
  fsim_vhpi_handle_v1 handle{};
  VhdlVhpiTypeDescriptor descriptor;
  VhdlVhpiTypeError error{VhdlVhpiTypeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiTypeError::None && handle != 0U;
  }
};

struct VhdlVhpiTypeHandleResult {
  fsim_vhpi_handle_v1 value{};
  VhdlVhpiTypeError error{VhdlVhpiTypeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiTypeError::None && value != 0U;
  }
};

struct VhdlVhpiDeclarationTypeResult {
  fsim_vhpi_handle_v1 declaration{};
  fsim_vhpi_handle_v1 type{};
  VhdlVhpiTypeDescriptor descriptor;
  VhdlVhpiTypeError error{VhdlVhpiTypeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiTypeError::None && declaration != 0U
        && type != 0U;
  }
};

class VhdlVhpiTypeSystem final {
 public:
  explicit VhdlVhpiTypeSystem(
      VhdlVhpiObjectRegistry& objects) noexcept;

  [[nodiscard]] VhdlVhpiTypeError publish(
      fsim_vhpi_handle_v1 type,
      const VhdlVhpiTypeDescriptor& descriptor);
  [[nodiscard]] VhdlVhpiTypeResult query(
      fsim_vhpi_handle_v1 type) const;
  [[nodiscard]] VhdlVhpiTypeHandleResult base_type(
      fsim_vhpi_handle_v1 type) const;
  [[nodiscard]] VhdlVhpiTypeError bind_declaration(
      fsim_vhpi_handle_v1 declaration,
      fsim_vhpi_handle_v1 type);
  [[nodiscard]] VhdlVhpiDeclarationTypeResult declaration_type(
      fsim_vhpi_handle_v1 declaration) const;

 private:
  [[nodiscard]] static VhdlVhpiTypeError object_error(
      VhdlVhpiObjectError error) noexcept;
  [[nodiscard]] static VhdlVhpiTypeError validate_constraint(
      const VhdlVhpiConstraint& constraint,
      std::uint32_t depth,
      std::uint32_t& nodes) noexcept;

  VhdlVhpiObjectRegistry* objects_{};
  mutable std::mutex mutex_;
  std::unordered_map<fsim_vhpi_handle_v1, VhdlVhpiTypeDescriptor> types_;
  std::unordered_map<fsim_vhpi_handle_v1, fsim_vhpi_handle_v1>
      declarations_;
};

}  // namespace fsim::runtime
