// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiIoKind : std::uint32_t {
  Assertion,
  Report,
  Output,
};

enum class VhdlVhpiSeverity : std::uint32_t {
  Note,
  Warning,
  Error,
  Failure,
};

enum class VhdlVhpiIoError {
  None,
  InvalidSimulation,
  InvalidContext,
  CrossSimulation,
  CrossRoot,
  InvalidObject,
  InvalidSeverity,
  InvalidSource,
  InvalidFormat,
  TextTooLong,
  SinkFailed,
  Closed,
  ResourceLimit,
};

struct VhdlVhpiIoEvent {
  std::uint64_t ordinal{};
  std::uint64_t context{};
  fsim_vhpi_handle_v1 root{};
  fsim_vhpi_handle_v1 subject{};
  VhdlVhpiIoKind kind{VhdlVhpiIoKind::Output};
  VhdlVhpiSeverity severity{VhdlVhpiSeverity::Note};
  std::string message;
  std::optional<VhdlVhpiSourceLocation> source;
};

using VhdlVhpiIoSink = std::function<void(const VhdlVhpiIoEvent&)>;

struct VhdlVhpiIoContextProfile {
  fsim_vhpi_handle_v1 root{};
  std::string name;
  VhdlVhpiIoSink sink;
};

struct VhdlVhpiIoContextDescriptor {
  std::uint64_t identity{};
  fsim_vhpi_handle_v1 root{};
  std::string name;
  std::uint64_t ordinal{};
  std::uint64_t events{};
  bool active{};
};

template <typename T>
struct VhdlVhpiIoResult {
  T value;
  VhdlVhpiIoError error{VhdlVhpiIoError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiIoError::None;
  }
};

using VhdlVhpiIoContextResult =
    VhdlVhpiIoResult<VhdlVhpiIoContextDescriptor>;
using VhdlVhpiIoHistoryResult =
    VhdlVhpiIoResult<std::vector<VhdlVhpiIoEvent>>;

class VhdlVhpiIoSystem final {
 public:
  explicit VhdlVhpiIoSystem(
      VhdlVhpiObjectRegistry& objects) noexcept;
  ~VhdlVhpiIoSystem();

  VhdlVhpiIoSystem(const VhdlVhpiIoSystem&) = delete;
  VhdlVhpiIoSystem& operator=(const VhdlVhpiIoSystem&) = delete;

  [[nodiscard]] VhdlVhpiIoContextResult register_context(
      VhdlVhpiIoContextProfile profile);
  [[nodiscard]] VhdlVhpiIoContextResult context(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiIoError output(
      std::uint64_t context, std::string_view text);
  [[nodiscard]] VhdlVhpiIoError report(
      std::uint64_t context,
      fsim_vhpi_handle_v1 subject,
      VhdlVhpiSeverity severity,
      std::string_view format,
      std::span<const std::string_view> arguments = {},
      std::optional<VhdlVhpiSourceLocation> source = std::nullopt);
  [[nodiscard]] VhdlVhpiIoError assertion(
      std::uint64_t context,
      fsim_vhpi_handle_v1 subject,
      bool condition,
      VhdlVhpiSeverity severity,
      std::string_view format,
      std::span<const std::string_view> arguments = {},
      std::optional<VhdlVhpiSourceLocation> source = std::nullopt);
  [[nodiscard]] VhdlVhpiIoHistoryResult history(
      std::uint64_t context) const;
  [[nodiscard]] std::vector<VhdlVhpiIoEvent> history() const;
  void teardown() noexcept;

 private:
  struct ContextEntry {
    VhdlVhpiIoContextDescriptor descriptor;
    VhdlVhpiIoSink sink;
    std::vector<VhdlVhpiIoEvent> history;
  };

  [[nodiscard]] static VhdlVhpiIoError format_message(
      std::string_view format,
      std::span<const std::string_view> arguments,
      std::string& result);
  [[nodiscard]] VhdlVhpiIoResult<fsim_vhpi_handle_v1> root_of(
      fsim_vhpi_handle_v1 object) const;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiIoError validate_identity(
      std::uint64_t identity) const noexcept;
  [[nodiscard]] VhdlVhpiIoError emit(
      std::uint64_t context,
      fsim_vhpi_handle_v1 subject,
      VhdlVhpiIoKind kind,
      VhdlVhpiSeverity severity,
      std::string message,
      std::optional<VhdlVhpiSourceLocation> source);

  VhdlVhpiObjectRegistry* objects_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_context_{1};
  std::uint64_t next_context_ordinal_{};
  std::uint64_t next_event_ordinal_{};
  bool closed_{};
  mutable std::mutex mutex_;
  std::recursive_mutex dispatch_mutex_;
  std::unordered_map<std::uint64_t, ContextEntry> contexts_;
  std::vector<VhdlVhpiIoEvent> history_;
};

}  // namespace fsim::runtime
