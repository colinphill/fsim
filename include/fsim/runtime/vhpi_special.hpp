// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/vhpi_type.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace fsim::runtime {

enum class VhdlVhpiFileAccess : std::uint32_t {
  ReadOnly,
  WriteOnly,
  ReadWrite,
  Append,
};

enum class VhdlVhpiProtectedAccess : std::uint32_t {
  Shared,
  Exclusive,
};

enum class VhdlVhpiSpecialError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidDeclaration,
  InvalidType,
  InvalidAccess,
  InvalidResource,
  AlreadyOpen,
  NotOpen,
  Busy,
  AlreadyBound,
  NotBound,
  InvalidResolver,
  InvalidValue,
  BufferTooSmall,
  ResourceLimit,
};

struct VhdlVhpiFileState {
  std::uint64_t identity{};
  fsim_vhpi_handle_v1 declaration{};
  fsim_vhpi_handle_v1 type{};
  VhdlVhpiFileAccess access{VhdlVhpiFileAccess::ReadOnly};
  std::string logical_name;
  bool open{};
};

struct VhdlVhpiFileResult {
  VhdlVhpiFileState value;
  VhdlVhpiSpecialError error{VhdlVhpiSpecialError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiSpecialError::None;
  }
};

struct VhdlVhpiResolverDescriptor {
  fsim_vhpi_handle_v1 type{};
  fsim_vhpi_handle_v1 subprogram{};
  std::string provenance;
};

struct VhdlVhpiResolverResult {
  VhdlVhpiResolverDescriptor value;
  VhdlVhpiSpecialError error{VhdlVhpiSpecialError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiSpecialError::None;
  }
};

struct VhdlVhpiProtectedLeaseResult {
  std::uint64_t value{};
  VhdlVhpiSpecialError error{VhdlVhpiSpecialError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiSpecialError::None && value != 0U;
  }
};

struct VhdlVhpiLogicResult {
  PackedLogic9 value;
  VhdlVhpiSpecialError error{VhdlVhpiSpecialError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiSpecialError::None;
  }
};

struct VhdlVhpiLogicBufferResult {
  std::size_t required_size{};
  VhdlVhpiSpecialError error{VhdlVhpiSpecialError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiSpecialError::None;
  }
};

class VhdlVhpiSpecialSystem final {
 public:
  VhdlVhpiSpecialSystem(
      VhdlVhpiObjectRegistry& objects,
      VhdlVhpiTypeSystem& types) noexcept;

  [[nodiscard]] VhdlVhpiFileResult open_file(
      fsim_vhpi_handle_v1 declaration,
      VhdlVhpiFileAccess access,
      std::string logical_name);
  [[nodiscard]] VhdlVhpiFileResult query_file(
      std::uint64_t identity,
      VhdlVhpiFileAccess requested_access) const;
  [[nodiscard]] VhdlVhpiSpecialError close_file(std::uint64_t identity);

  [[nodiscard]] VhdlVhpiSpecialError register_protected(
      fsim_vhpi_handle_v1 declaration);
  [[nodiscard]] VhdlVhpiProtectedLeaseResult acquire_protected(
      fsim_vhpi_handle_v1 declaration,
      fsim_vhpi_handle_v1 caller,
      VhdlVhpiProtectedAccess access);
  [[nodiscard]] VhdlVhpiSpecialError release_protected(
      std::uint64_t lease,
      fsim_vhpi_handle_v1 caller);

  [[nodiscard]] VhdlVhpiSpecialError publish_resolver(
      const VhdlVhpiResolverDescriptor& descriptor);
  [[nodiscard]] VhdlVhpiResolverResult resolver(
      fsim_vhpi_handle_v1 type) const;

  [[nodiscard]] VhdlVhpiSpecialError bind_logic(
      fsim_vhpi_handle_v1 declaration,
      const PackedLogic9& initial);
  [[nodiscard]] VhdlVhpiLogicResult read_logic(
      fsim_vhpi_handle_v1 declaration) const;
  [[nodiscard]] VhdlVhpiSpecialError write_logic(
      fsim_vhpi_handle_v1 declaration,
      const PackedLogic9& value);
  [[nodiscard]] VhdlVhpiLogicBufferResult encode_logic(
      fsim_vhpi_handle_v1 declaration,
      std::span<std::uint8_t> buffer) const;

 private:
  struct FileEntry {
    VhdlVhpiFileState state;
  };
  struct ProtectedEntry {
    std::size_t shared{};
    std::uint64_t exclusive{};
  };
  struct LeaseEntry {
    fsim_vhpi_handle_v1 declaration{};
    fsim_vhpi_handle_v1 caller{};
    VhdlVhpiProtectedAccess access{VhdlVhpiProtectedAccess::Shared};
  };
  struct LogicEntry {
    fsim_vhpi_handle_v1 type{};
    PackedLogic9 value;
  };

  [[nodiscard]] static VhdlVhpiSpecialError object_error(
      VhdlVhpiObjectError error) noexcept;
  [[nodiscard]] static bool valid_file_access(
      VhdlVhpiFileAccess access) noexcept;
  [[nodiscard]] static bool permits(
      VhdlVhpiFileAccess granted,
      VhdlVhpiFileAccess requested) noexcept;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint8_t kind, std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiSpecialError validate_identity(
      std::uint64_t identity, std::uint8_t kind) const noexcept;

  VhdlVhpiObjectRegistry* objects_{};
  VhdlVhpiTypeSystem* types_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_file_{1};
  std::uint32_t next_lease_{1};
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, FileEntry> files_;
  std::unordered_map<fsim_vhpi_handle_v1, std::uint64_t> open_files_;
  std::unordered_map<fsim_vhpi_handle_v1, ProtectedEntry> protected_;
  std::unordered_map<std::uint64_t, LeaseEntry> leases_;
  std::unordered_map<
      fsim_vhpi_handle_v1, VhdlVhpiResolverDescriptor> resolvers_;
  std::unordered_map<fsim_vhpi_handle_v1, LogicEntry> logic_;
};

}  // namespace fsim::runtime
