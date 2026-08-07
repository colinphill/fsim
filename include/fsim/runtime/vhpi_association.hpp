// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_object.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiAssociationKind : std::uint32_t {
  Generic,
  Port,
};

enum class VhdlVhpiAssociationMode : std::uint32_t {
  In,
  Out,
  InOut,
  Buffer,
  Linkage,
};

enum class VhdlVhpiAssociationClass : std::uint32_t {
  Constant,
  Signal,
  Variable,
  File,
  Type,
};

enum class VhdlVhpiActualKind : std::uint32_t {
  Object,
  Open,
  Disconnected,
};

enum class VhdlVhpiAssociationError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  CrossRoot,
  InvalidOwner,
  InvalidFormal,
  InvalidActual,
  InvalidProfile,
  DuplicateFormal,
  AlreadyPublished,
  NotFound,
  ResourceLimit,
};

struct VhdlVhpiAssociationProfile {
  VhdlVhpiAssociationKind kind{VhdlVhpiAssociationKind::Generic};
  fsim_vhpi_handle_v1 formal{};
  fsim_vhpi_handle_v1 actual{};
  VhdlVhpiActualKind actual_kind{VhdlVhpiActualKind::Object};
  VhdlVhpiAssociationMode mode{VhdlVhpiAssociationMode::In};
  VhdlVhpiAssociationClass object_class{
      VhdlVhpiAssociationClass::Constant};
  std::uint64_t object_user_data{};
  std::uint64_t call_user_data{};
};

struct VhdlVhpiAssociationDescriptor {
  std::uint64_t identity{};
  fsim_vhpi_handle_v1 owner{};
  fsim_vhpi_handle_v1 root{};
  VhdlVhpiAssociationKind kind{VhdlVhpiAssociationKind::Generic};
  fsim_vhpi_handle_v1 formal{};
  fsim_vhpi_handle_v1 actual{};
  VhdlVhpiActualKind actual_kind{VhdlVhpiActualKind::Object};
  VhdlVhpiAssociationMode mode{VhdlVhpiAssociationMode::In};
  VhdlVhpiAssociationClass object_class{
      VhdlVhpiAssociationClass::Constant};
  std::uint64_t ordinal{};
  std::uint64_t object_user_data{};
  std::uint64_t call_user_data{};
};

template <typename T>
struct VhdlVhpiAssociationResult {
  T value;
  VhdlVhpiAssociationError error{VhdlVhpiAssociationError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiAssociationError::None;
  }
};

using VhdlVhpiAssociationDescriptorResult =
    VhdlVhpiAssociationResult<VhdlVhpiAssociationDescriptor>;
using VhdlVhpiAssociationListResult =
    VhdlVhpiAssociationResult<std::vector<VhdlVhpiAssociationDescriptor>>;

class VhdlVhpiAssociationSystem final {
 public:
  explicit VhdlVhpiAssociationSystem(
      VhdlVhpiObjectRegistry& objects) noexcept;

  [[nodiscard]] VhdlVhpiAssociationError publish(
      fsim_vhpi_handle_v1 owner,
      std::span<const VhdlVhpiAssociationProfile> profiles);
  [[nodiscard]] VhdlVhpiAssociationDescriptorResult association(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiAssociationListResult associations(
      fsim_vhpi_handle_v1 owner,
      VhdlVhpiAssociationKind kind) const;
  [[nodiscard]] VhdlVhpiAssociationError set_user_data(
      std::uint64_t identity,
      std::uint64_t object_user_data,
      std::uint64_t call_user_data);

 private:
  [[nodiscard]] static bool valid_owner_kind(
      VhdlVhpiObjectKind kind) noexcept;
  [[nodiscard]] static bool class_matches(
      VhdlVhpiAssociationClass object_class,
      VhdlVhpiObjectKind object_kind) noexcept;
  [[nodiscard]] VhdlVhpiAssociationResult<fsim_vhpi_handle_v1> root_of(
      fsim_vhpi_handle_v1 object) const;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiAssociationError validate_identity(
      std::uint64_t identity) const noexcept;

  VhdlVhpiObjectRegistry* objects_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_association_{1};
  std::uint64_t next_ordinal_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, VhdlVhpiAssociationDescriptor>
      associations_;
  std::unordered_map<fsim_vhpi_handle_v1, std::vector<std::uint64_t>>
      owners_;
};

}  // namespace fsim::runtime
