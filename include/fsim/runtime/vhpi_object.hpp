// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_abi.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiObjectKind : std::uint32_t {
  Root,
  Library,
  DesignUnit,
  Region,
  Entity,
  Architecture,
  Configuration,
  Package,
  PackageBody,
  Component,
  Block,
  Generate,
  Process,
  Subprogram,
  Signal,
  Variable,
  Constant,
  File,
  Type,
  Subtype,
};

enum class VhdlVhpiObjectError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidKind,
  InvalidParent,
  InvalidName,
  DuplicateName,
  InvalidSource,
  NotFound,
  HasChildren,
  ResourceLimit,
};

struct VhdlVhpiSourceLocation {
  std::string file;
  std::uint32_t line{};
  std::uint32_t column{};
};

struct VhdlVhpiObjectDescriptor {
  VhdlVhpiObjectKind kind{VhdlVhpiObjectKind::Root};
  fsim_vhpi_handle_v1 parent{};
  std::string_view name;
  std::span<const std::int64_t> indices;
  std::optional<VhdlVhpiSourceLocation> source;
};

struct VhdlVhpiObjectMetadata {
  fsim_vhpi_handle_v1 handle{};
  fsim_vhpi_handle_v1 parent{};
  VhdlVhpiObjectKind kind{VhdlVhpiObjectKind::Root};
  std::uint32_t live_children{};
  std::uint64_t ordinal{};
  std::string name;
  std::string selected_name;
  std::string full_name;
  std::vector<std::int64_t> indices;
  std::optional<VhdlVhpiSourceLocation> source;
};

struct VhdlVhpiObjectResult {
  fsim_vhpi_handle_v1 value{};
  VhdlVhpiObjectError error{VhdlVhpiObjectError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiObjectError::None && value != 0U;
  }
};

struct VhdlVhpiObjectLookupResult {
  VhdlVhpiObjectMetadata value;
  VhdlVhpiObjectError error{VhdlVhpiObjectError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiObjectError::None && value.handle != 0U;
  }
};

enum class VhdlVhpiIteratorError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  InvalidObject,
  InvalidRelationship,
  Exhausted,
  ResourceLimit,
};

enum class VhdlVhpiRelationshipKind : std::uint32_t {
  Children,
  Regions,
  Declarations,
};

struct VhdlVhpiIteratorResult {
  fsim_vhpi_handle_v1 value{};
  VhdlVhpiIteratorError error{VhdlVhpiIteratorError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiIteratorError::None && value != 0U;
  }
};

struct VhdlVhpiIteratorScanResult {
  fsim_vhpi_handle_v1 value{};
  VhdlVhpiIteratorError error{VhdlVhpiIteratorError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiIteratorError::None && value != 0U;
  }
};

class VhdlVhpiObjectRegistry final {
 public:
  explicit VhdlVhpiObjectRegistry(
      std::uint64_t simulation_identity) noexcept;

  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] VhdlVhpiObjectResult create_object(
      VhdlVhpiObjectKind kind,
      fsim_vhpi_handle_v1 parent = 0);
  [[nodiscard]] VhdlVhpiObjectResult create_object(
      const VhdlVhpiObjectDescriptor& descriptor);
  [[nodiscard]] VhdlVhpiObjectLookupResult lookup_object(
      fsim_vhpi_handle_v1 handle) const;
  [[nodiscard]] VhdlVhpiObjectLookupResult find(
      std::string_view full_name) const;
  [[nodiscard]] VhdlVhpiObjectLookupResult find_child(
      fsim_vhpi_handle_v1 parent,
      std::string_view name,
      std::span<const std::int64_t> indices = {}) const;
  [[nodiscard]] VhdlVhpiObjectError release_object(
      fsim_vhpi_handle_v1 handle);

  [[nodiscard]] VhdlVhpiIteratorResult create_iterator(
      std::span<const fsim_vhpi_handle_v1> objects);
  [[nodiscard]] VhdlVhpiIteratorScanResult scan(
      fsim_vhpi_handle_v1 iterator);
  [[nodiscard]] VhdlVhpiIteratorError release_iterator(
      fsim_vhpi_handle_v1 iterator);
  [[nodiscard]] VhdlVhpiIteratorResult iterate_relationship(
      fsim_vhpi_handle_v1 parent,
      VhdlVhpiRelationshipKind relationship);

 private:
  struct ObjectRecord {
    std::uint16_t generation{};
    bool live{};
    std::uint32_t live_children{};
    std::uint64_t ordinal{};
    fsim_vhpi_handle_v1 parent{};
    VhdlVhpiObjectKind kind{VhdlVhpiObjectKind::Root};
    std::string name;
    std::string selected_name;
    std::string full_name;
    std::vector<std::int64_t> indices;
    std::optional<VhdlVhpiSourceLocation> source;
  };

  struct IteratorRecord {
    std::uint16_t generation{};
    bool live{};
    std::size_t cursor{};
    std::vector<fsim_vhpi_handle_v1> objects;
  };

  [[nodiscard]] fsim_vhpi_handle_v1 encode_object(
      std::uint32_t slot, std::uint16_t generation) const noexcept;
  [[nodiscard]] fsim_vhpi_handle_v1 encode_iterator(
      std::uint32_t slot, std::uint16_t generation) const noexcept;
  [[nodiscard]] VhdlVhpiObjectError resolve_object(
      fsim_vhpi_handle_v1 handle, std::uint32_t& slot) const noexcept;
  [[nodiscard]] VhdlVhpiIteratorError resolve_iterator(
      fsim_vhpi_handle_v1 handle, std::uint32_t& slot) const noexcept;
  [[nodiscard]] VhdlVhpiObjectLookupResult lookup_locked(
      fsim_vhpi_handle_v1 handle) const;
  [[nodiscard]] VhdlVhpiIteratorResult create_iterator_locked(
      std::span<const fsim_vhpi_handle_v1> objects);
  [[nodiscard]] static bool normalize_identifier(
      std::string_view input, std::string& output);
  [[nodiscard]] static bool normalize_segment(
      std::string_view input, std::string& output);
  [[nodiscard]] static bool normalize_full_name(
      std::string_view input, std::string& output);
  [[nodiscard]] static std::string format_segment(
      std::string_view name, std::span<const std::int64_t> indices);
  [[nodiscard]] static std::string sibling_key(
      fsim_vhpi_handle_v1 parent, std::string_view selected_name);
  [[nodiscard]] static bool is_region(VhdlVhpiObjectKind kind) noexcept;

  std::uint64_t simulation_identity_{};
  std::uint32_t registry_identity_{};
  std::uint64_t next_ordinal_{};
  mutable std::mutex mutex_;
  std::vector<ObjectRecord> objects_;
  std::vector<std::uint32_t> free_objects_;
  std::vector<IteratorRecord> iterators_;
  std::vector<std::uint32_t> free_iterators_;
  std::unordered_map<std::string, fsim_vhpi_handle_v1> siblings_;
  std::unordered_map<std::string, fsim_vhpi_handle_v1> full_names_;
};

}  // namespace fsim::runtime
