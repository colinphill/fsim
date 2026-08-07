// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogVpiIoError {
  None,
  InvalidService,
  InvalidHandle,
  CrossService,
  StaleHandle,
  InvalidPath,
  InvalidMode,
  InvalidDescriptor,
  InvalidSeverity,
  InvalidFormat,
  StringTooLong,
  ResourceLimit,
  OpenFailed,
  WriteFailed,
  FlushFailed,
  CloseFailed,
  SinkFailed,
  Closed,
};

enum class SystemVerilogVpiIoSeverity {
  Note,
  Warning,
  Error,
  Fatal,
};

enum class SystemVerilogVpiIoDescriptorKind {
  StandardOutput,
  Multichannel,
  FileDescriptor,
};

struct SystemVerilogVpiIoHandle {
  std::uint64_t owner{};
  std::uint32_t value{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && value != 0U;
  }

  friend bool operator==(
      const SystemVerilogVpiIoHandle&,
      const SystemVerilogVpiIoHandle&) = default;
};

struct SystemVerilogVpiIoDiagnostic {
  SystemVerilogVpiIoSeverity severity{
      SystemVerilogVpiIoSeverity::Note};
  std::string message;
};

using SystemVerilogVpiIoOutputSink =
    std::function<void(std::string_view)>;
using SystemVerilogVpiIoDiagnosticSink =
    std::function<void(const SystemVerilogVpiIoDiagnostic&)>;

struct SystemVerilogVpiIoConfiguration {
  std::uint64_t simulation_identity{};
  std::filesystem::path file_root;
  std::vector<std::string> arguments;
  std::string product{"fsim"};
  std::string version;
  SystemVerilogVpiIoOutputSink output;
  SystemVerilogVpiIoDiagnosticSink diagnostic;
};

struct SystemVerilogVpiIoOpenResult {
  SystemVerilogVpiIoHandle value;
  SystemVerilogVpiIoError error{SystemVerilogVpiIoError::None};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiIoError::None
        && static_cast<bool>(value);
  }
};

class SystemVerilogVpiIoService final {
 public:
  explicit SystemVerilogVpiIoService(
      SystemVerilogVpiIoConfiguration configuration);
  ~SystemVerilogVpiIoService();

  SystemVerilogVpiIoService(
      const SystemVerilogVpiIoService&) = delete;
  SystemVerilogVpiIoService& operator=(
      const SystemVerilogVpiIoService&) = delete;
  SystemVerilogVpiIoService(
      SystemVerilogVpiIoService&&) = delete;
  SystemVerilogVpiIoService& operator=(
      SystemVerilogVpiIoService&&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] SystemVerilogVpiIoError configuration_error() const noexcept;
  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
  [[nodiscard]] SystemVerilogVpiIoHandle standard_output() const noexcept;
  [[nodiscard]] SystemVerilogVpiIoOpenResult open_mcd(
      std::string_view path);
  [[nodiscard]] SystemVerilogVpiIoOpenResult open_file_descriptor(
      std::string_view path, std::string_view mode);
  [[nodiscard]] SystemVerilogVpiIoOpenResult combine_mcd(
      std::span<const SystemVerilogVpiIoHandle> channels) const;
  [[nodiscard]] SystemVerilogVpiIoError write(
      SystemVerilogVpiIoHandle handle, std::string_view text);
  [[nodiscard]] SystemVerilogVpiIoError vlog(std::string_view text);
  [[nodiscard]] SystemVerilogVpiIoError report(
      SystemVerilogVpiIoSeverity severity,
      std::string_view format,
      std::span<const std::string_view> arguments = {});
  [[nodiscard]] SystemVerilogVpiIoError flush(
      SystemVerilogVpiIoHandle handle);
  [[nodiscard]] SystemVerilogVpiIoError close(
      SystemVerilogVpiIoHandle handle);
  [[nodiscard]] SystemVerilogVpiIoError descriptor_kind(
      SystemVerilogVpiIoHandle handle,
      SystemVerilogVpiIoDescriptorKind& kind) const;
  [[nodiscard]] std::vector<std::string> arguments() const;
  [[nodiscard]] std::string product() const;
  [[nodiscard]] std::string version() const;
  [[nodiscard]] std::size_t open_files() const;
  void teardown() noexcept;

  struct FileRecord;

 private:
  SystemVerilogVpiIoOpenResult open(
      std::string_view path,
      std::string_view mode,
      bool multichannel);

  mutable std::mutex mutex_;
  std::uint64_t owner_{};
  SystemVerilogVpiIoConfiguration configuration_;
  SystemVerilogVpiIoError configuration_error_{
      SystemVerilogVpiIoError::None};
  std::filesystem::path canonical_root_;
  std::uint32_t next_mcd_channel_{1};
  std::uint32_t next_file_descriptor_{1};
  bool closed_{};
  std::map<std::uint32_t, std::shared_ptr<FileRecord>> files_;
};

}  // namespace fsim::runtime
