// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_io.hpp"

#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <atomic>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace fsim::runtime {

struct SystemVerilogVpiIoService::FileRecord {
  std::filesystem::path path;
  std::string mode;
  std::fstream stream;
};

namespace {

constexpr std::size_t maximum_text_bytes = 4'096;
constexpr std::size_t maximum_arguments = 65'536;
constexpr std::uint32_t standard_output_bit = UINT32_C(1);
constexpr std::uint32_t file_descriptor_flag = UINT32_C(0x80000000);
constexpr std::uint32_t mcd_mask = UINT32_C(0x7fffffff);
std::atomic<std::uint64_t> next_io_owner{1};

bool below_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end()
        || *root_part != *candidate_part) {
      return false;
    }
  }
  return true;
}

bool valid_severity(const SystemVerilogVpiIoSeverity severity) noexcept {
  switch (severity) {
    case SystemVerilogVpiIoSeverity::Note:
    case SystemVerilogVpiIoSeverity::Warning:
    case SystemVerilogVpiIoSeverity::Error:
    case SystemVerilogVpiIoSeverity::Fatal:
      return true;
  }
  return false;
}

std::optional<std::ios::openmode> output_mode(
    const std::string_view mode) {
  if (mode == "w" || mode == "wb") {
    return std::ios::out | std::ios::trunc | std::ios::binary;
  }
  if (mode == "a" || mode == "ab") {
    return std::ios::out | std::ios::app | std::ios::binary;
  }
  return std::nullopt;
}

std::string bounded(std::string text) noexcept {
  if (text.size() > maximum_text_bytes) {
    text.resize(maximum_text_bytes);
  }
  return text;
}

SystemVerilogVpiIoError handle_error(
    const SystemVerilogVpiIoHandle handle,
    const std::uint64_t owner) noexcept {
  if (!handle) {
    return SystemVerilogVpiIoError::InvalidHandle;
  }
  if (handle.owner != owner) {
    return SystemVerilogVpiIoError::CrossService;
  }
  return SystemVerilogVpiIoError::None;
}

SystemVerilogVpiIoError format_message(
    const std::string_view format,
    const std::span<const std::string_view> arguments,
    std::string& result) {
  if (format.size() > maximum_text_bytes
      || arguments.size() > maximum_arguments) {
    return SystemVerilogVpiIoError::StringTooLong;
  }
  result.clear();
  try {
    std::size_t argument{};
    for (std::size_t index = 0; index < format.size();) {
      if (format[index] == '{') {
        if (index + 1U < format.size() && format[index + 1U] == '{') {
          result.push_back('{');
          index += 2U;
          continue;
        }
        if (index + 1U >= format.size() || format[index + 1U] != '}'
            || argument >= arguments.size()) {
          return SystemVerilogVpiIoError::InvalidFormat;
        }
        if (arguments[argument].size() > maximum_text_bytes
            || result.size() + arguments[argument].size()
                > maximum_text_bytes) {
          return SystemVerilogVpiIoError::StringTooLong;
        }
        result.append(arguments[argument++]);
        index += 2U;
        continue;
      }
      if (format[index] == '}') {
        if (index + 1U < format.size() && format[index + 1U] == '}') {
          result.push_back('}');
          index += 2U;
          continue;
        }
        return SystemVerilogVpiIoError::InvalidFormat;
      }
      result.push_back(format[index++]);
      if (result.size() > maximum_text_bytes) {
        return SystemVerilogVpiIoError::StringTooLong;
      }
    }
    if (argument != arguments.size()) {
      return SystemVerilogVpiIoError::InvalidFormat;
    }
  } catch (...) {
    return SystemVerilogVpiIoError::ResourceLimit;
  }
  return SystemVerilogVpiIoError::None;
}

}  // namespace

SystemVerilogVpiIoService::SystemVerilogVpiIoService(
    SystemVerilogVpiIoConfiguration configuration)
    : owner_(next_io_owner.fetch_add(1, std::memory_order_relaxed)),
      configuration_(std::move(configuration)) {
  if (configuration_.version.empty()) {
    configuration_.version = std::string{fsim::version};
  }
  if (owner_ == 0U || configuration_.simulation_identity == 0U) {
    configuration_error_ = SystemVerilogVpiIoError::InvalidService;
    return;
  }
  if (configuration_.arguments.size() > maximum_arguments
      || configuration_.product.empty()
      || configuration_.product.size() > maximum_text_bytes
      || configuration_.version.empty()
      || configuration_.version.size() > maximum_text_bytes) {
    configuration_error_ = SystemVerilogVpiIoError::StringTooLong;
    return;
  }
  for (const auto& argument : configuration_.arguments) {
    if (argument.size() > maximum_text_bytes) {
      configuration_error_ = SystemVerilogVpiIoError::StringTooLong;
      return;
    }
  }
  if (configuration_.file_root.empty()) {
    return;
  }
  std::error_code error;
  canonical_root_ = std::filesystem::weakly_canonical(
      configuration_.file_root, error);
  if (error
      || !std::filesystem::is_directory(canonical_root_, error)) {
    configuration_error_ = SystemVerilogVpiIoError::InvalidPath;
    canonical_root_.clear();
  }
}

SystemVerilogVpiIoService::~SystemVerilogVpiIoService() {
  teardown();
}

bool SystemVerilogVpiIoService::valid() const noexcept {
  std::scoped_lock lock(mutex_);
  return owner_ != 0U
      && configuration_error_ == SystemVerilogVpiIoError::None
      && !closed_;
}

SystemVerilogVpiIoError
SystemVerilogVpiIoService::configuration_error() const noexcept {
  std::scoped_lock lock(mutex_);
  return configuration_error_;
}

std::uint64_t SystemVerilogVpiIoService::simulation_identity()
    const noexcept {
  std::scoped_lock lock(mutex_);
  return configuration_error_ == SystemVerilogVpiIoError::None
          && !closed_
      ? configuration_.simulation_identity : 0U;
}

SystemVerilogVpiIoHandle
SystemVerilogVpiIoService::standard_output() const noexcept {
  std::scoped_lock lock(mutex_);
  return configuration_error_ == SystemVerilogVpiIoError::None
          && !closed_
      ? SystemVerilogVpiIoHandle{owner_, standard_output_bit}
      : SystemVerilogVpiIoHandle{};
}

SystemVerilogVpiIoOpenResult
SystemVerilogVpiIoService::open_mcd(const std::string_view path) {
  return open(path, "w", true);
}

SystemVerilogVpiIoOpenResult
SystemVerilogVpiIoService::open_file_descriptor(
    const std::string_view path,
    const std::string_view mode) {
  return open(path, mode, false);
}

SystemVerilogVpiIoOpenResult SystemVerilogVpiIoService::open(
    const std::string_view path_text,
    const std::string_view mode_text,
    const bool multichannel) {
  if (path_text.empty() || path_text.size() > maximum_text_bytes) {
    return {{}, SystemVerilogVpiIoError::InvalidPath,
            "VPI output path is empty or exceeds the bounded-string limit"};
  }
  const auto mode = output_mode(mode_text);
  if (!mode) {
    return {{}, SystemVerilogVpiIoError::InvalidMode,
            "VPI output mode must be w, wb, a, or ab"};
  }
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return {{}, SystemVerilogVpiIoError::Closed,
            "VPI I/O service has been torn down"};
  }
  if (configuration_error_ != SystemVerilogVpiIoError::None) {
    return {{}, configuration_error_,
            "VPI I/O service configuration is invalid"};
  }
  if (canonical_root_.empty()) {
    return {{}, SystemVerilogVpiIoError::InvalidPath,
            "VPI output has no configured file root"};
  }

  std::filesystem::path relative;
  try {
    relative = fsim::support::path_from_utf8(path_text).lexically_normal();
  } catch (...) {
    return {{}, SystemVerilogVpiIoError::InvalidPath,
            "VPI output path is not valid UTF-8"};
  }
  if (relative.empty() || relative.is_absolute()
      || relative.has_root_name()) {
    return {{}, SystemVerilogVpiIoError::InvalidPath,
            "VPI output path must be root-relative"};
  }
  for (const auto& component : relative) {
    if (component == "..") {
      return {{}, SystemVerilogVpiIoError::InvalidPath,
              "VPI output path escapes the configured root"};
    }
  }

  const auto joined = canonical_root_ / relative;
  std::error_code error;
  auto checked = std::filesystem::weakly_canonical(
      joined.parent_path(), error);
  if (error || !below_root(canonical_root_, checked)) {
    return {{}, SystemVerilogVpiIoError::InvalidPath,
            "VPI output parent is outside the configured root"};
  }
  checked /= joined.filename();

  auto record = std::make_shared<FileRecord>();
  record->path = checked;
  record->mode = std::string{mode_text};
  record->stream.open(checked, *mode);
  if (!record->stream.is_open()) {
    return {{}, SystemVerilogVpiIoError::OpenFailed,
            bounded("cannot open VPI output '" + std::string{path_text} + "'")};
  }

  std::uint32_t value{};
  if (multichannel) {
    if (next_mcd_channel_ >= 31U) {
      return {{}, SystemVerilogVpiIoError::ResourceLimit,
              "VPI MCD channel space is exhausted"};
    }
    value = UINT32_C(1) << next_mcd_channel_++;
  } else {
    if (next_file_descriptor_ == 0U
        || next_file_descriptor_ >= file_descriptor_flag) {
      return {{}, SystemVerilogVpiIoError::ResourceLimit,
              "VPI file descriptor space is exhausted"};
    }
    value = file_descriptor_flag | next_file_descriptor_++;
  }
  try {
    files_.emplace(value, std::move(record));
  } catch (...) {
    return {{}, SystemVerilogVpiIoError::ResourceLimit,
            "VPI output ownership allocation failed"};
  }
  return {{owner_, value}, SystemVerilogVpiIoError::None, {}};
}

SystemVerilogVpiIoOpenResult
SystemVerilogVpiIoService::combine_mcd(
    const std::span<const SystemVerilogVpiIoHandle> channels) const {
  if (channels.empty()) {
    return {{}, SystemVerilogVpiIoError::InvalidDescriptor,
            "VPI MCD combination is empty"};
  }
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return {{}, SystemVerilogVpiIoError::Closed, {}};
  }
  std::uint32_t value{};
  for (const auto channel : channels) {
    const auto error = handle_error(channel, owner_);
    if (error != SystemVerilogVpiIoError::None) {
      return {{}, error, "VPI MCD channel has invalid ownership"};
    }
    if ((channel.value & file_descriptor_flag) != 0U) {
      return {{}, SystemVerilogVpiIoError::InvalidDescriptor,
              "VPI file descriptor cannot join an MCD"};
    }
    for (std::uint32_t bit = 1; bit < 31U; ++bit) {
      const auto selected = UINT32_C(1) << bit;
      if ((channel.value & selected) != 0U
          && !files_.contains(selected)) {
        return {{}, SystemVerilogVpiIoError::StaleHandle,
                "VPI MCD contains a closed channel"};
      }
    }
    value |= channel.value;
  }
  return {{owner_, value}, SystemVerilogVpiIoError::None, {}};
}

SystemVerilogVpiIoError SystemVerilogVpiIoService::write(
    const SystemVerilogVpiIoHandle handle,
    const std::string_view text) {
  if (text.size() > maximum_text_bytes) {
    return SystemVerilogVpiIoError::StringTooLong;
  }
  const auto ownership = handle_error(handle, owner_);
  if (ownership != SystemVerilogVpiIoError::None) {
    return ownership;
  }
  SystemVerilogVpiIoOutputSink output;
  {
    std::scoped_lock lock(mutex_);
    if (closed_) {
      return SystemVerilogVpiIoError::Closed;
    }
    std::vector<std::shared_ptr<FileRecord>> selected;
    if ((handle.value & file_descriptor_flag) != 0U) {
      const auto found = files_.find(handle.value);
      if (found == files_.end()) {
        return SystemVerilogVpiIoError::StaleHandle;
      }
      selected.push_back(found->second);
    } else {
      if ((handle.value & ~mcd_mask) != 0U) {
        return SystemVerilogVpiIoError::InvalidDescriptor;
      }
      for (std::uint32_t bit = 1; bit < 31U; ++bit) {
        const auto selected_bit = UINT32_C(1) << bit;
        if ((handle.value & selected_bit) != 0U) {
          const auto found = files_.find(selected_bit);
          if (found == files_.end()) {
            return SystemVerilogVpiIoError::StaleHandle;
          }
          selected.push_back(found->second);
        }
      }
      if ((handle.value & standard_output_bit) != 0U) {
        output = configuration_.output;
        if (!output) {
          return SystemVerilogVpiIoError::SinkFailed;
        }
      }
    }
    for (const auto& file : selected) {
      file->stream.write(
          text.data(), static_cast<std::streamsize>(text.size()));
      file->stream.flush();
      if (!file->stream.good()) {
        return SystemVerilogVpiIoError::WriteFailed;
      }
    }
  }
  if (output) {
    try {
      output(text);
    } catch (...) {
      return SystemVerilogVpiIoError::SinkFailed;
    }
  }
  return SystemVerilogVpiIoError::None;
}

SystemVerilogVpiIoError
SystemVerilogVpiIoService::vlog(const std::string_view text) {
  SystemVerilogVpiIoHandle output;
  {
    std::scoped_lock lock(mutex_);
    if (closed_) {
      return SystemVerilogVpiIoError::Closed;
    }
    if (configuration_error_ != SystemVerilogVpiIoError::None) {
      return configuration_error_;
    }
    output = {owner_, standard_output_bit};
  }
  return write(output, text);
}

SystemVerilogVpiIoError SystemVerilogVpiIoService::report(
    const SystemVerilogVpiIoSeverity severity,
    const std::string_view format,
    const std::span<const std::string_view> arguments) {
  if (!valid_severity(severity)) {
    return SystemVerilogVpiIoError::InvalidSeverity;
  }
  std::string message;
  const auto formatted =
      format_message(format, arguments, message);
  if (formatted != SystemVerilogVpiIoError::None) {
    return formatted;
  }
  SystemVerilogVpiIoDiagnosticSink sink;
  {
    std::scoped_lock lock(mutex_);
    if (closed_) {
      return SystemVerilogVpiIoError::Closed;
    }
    sink = configuration_.diagnostic;
  }
  if (!sink) {
    return SystemVerilogVpiIoError::SinkFailed;
  }
  try {
    sink({severity, std::move(message)});
  } catch (...) {
    return SystemVerilogVpiIoError::SinkFailed;
  }
  return SystemVerilogVpiIoError::None;
}

SystemVerilogVpiIoError SystemVerilogVpiIoService::flush(
    const SystemVerilogVpiIoHandle handle) {
  const auto ownership = handle_error(handle, owner_);
  if (ownership != SystemVerilogVpiIoError::None) {
    return ownership;
  }
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return SystemVerilogVpiIoError::Closed;
  }
  bool found_any = handle.value == standard_output_bit;
  for (const auto& [value, file] : files_) {
    const bool selected = (handle.value & file_descriptor_flag) != 0U
        ? value == handle.value
        : (handle.value & value) != 0U;
    if (selected) {
      found_any = true;
      file->stream.flush();
      if (!file->stream.good()) {
        return SystemVerilogVpiIoError::FlushFailed;
      }
    }
  }
  return found_any ? SystemVerilogVpiIoError::None
                   : SystemVerilogVpiIoError::StaleHandle;
}

SystemVerilogVpiIoError SystemVerilogVpiIoService::close(
    const SystemVerilogVpiIoHandle handle) {
  const auto ownership = handle_error(handle, owner_);
  if (ownership != SystemVerilogVpiIoError::None) {
    return ownership;
  }
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return SystemVerilogVpiIoError::Closed;
  }
  std::vector<std::uint32_t> selected;
  if ((handle.value & file_descriptor_flag) != 0U) {
    if (!files_.contains(handle.value)) {
      return SystemVerilogVpiIoError::StaleHandle;
    }
    selected.push_back(handle.value);
  } else {
    for (std::uint32_t bit = 1; bit < 31U; ++bit) {
      const auto selected_bit = UINT32_C(1) << bit;
      if ((handle.value & selected_bit) != 0U) {
        if (!files_.contains(selected_bit)) {
          return SystemVerilogVpiIoError::StaleHandle;
        }
        selected.push_back(selected_bit);
      }
    }
    if (selected.empty()) {
      return SystemVerilogVpiIoError::InvalidDescriptor;
    }
  }
  bool failed{};
  for (const auto value : selected) {
    auto file = files_.at(value);
    file->stream.flush();
    file->stream.close();
    failed = failed || file->stream.fail();
    files_.erase(value);
  }
  return failed ? SystemVerilogVpiIoError::CloseFailed
                : SystemVerilogVpiIoError::None;
}

SystemVerilogVpiIoError
SystemVerilogVpiIoService::descriptor_kind(
    const SystemVerilogVpiIoHandle handle,
    SystemVerilogVpiIoDescriptorKind& kind) const {
  const auto ownership = handle_error(handle, owner_);
  if (ownership != SystemVerilogVpiIoError::None) {
    return ownership;
  }
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return SystemVerilogVpiIoError::Closed;
  }
  if (handle.value == standard_output_bit) {
    kind = SystemVerilogVpiIoDescriptorKind::StandardOutput;
    return SystemVerilogVpiIoError::None;
  }
  if ((handle.value & file_descriptor_flag) != 0U) {
    if (!files_.contains(handle.value)) {
      return SystemVerilogVpiIoError::StaleHandle;
    }
    kind = SystemVerilogVpiIoDescriptorKind::FileDescriptor;
    return SystemVerilogVpiIoError::None;
  }
  for (std::uint32_t bit = 1; bit < 31U; ++bit) {
    const auto selected = UINT32_C(1) << bit;
    if ((handle.value & selected) != 0U && !files_.contains(selected)) {
      return SystemVerilogVpiIoError::StaleHandle;
    }
  }
  kind = SystemVerilogVpiIoDescriptorKind::Multichannel;
  return SystemVerilogVpiIoError::None;
}

std::vector<std::string>
SystemVerilogVpiIoService::arguments() const {
  std::scoped_lock lock(mutex_);
  try {
    return configuration_.arguments;
  } catch (...) {
    return {};
  }
}

std::string SystemVerilogVpiIoService::product() const {
  std::scoped_lock lock(mutex_);
  try {
    return configuration_.product;
  } catch (...) {
    return {};
  }
}

std::string SystemVerilogVpiIoService::version() const {
  std::scoped_lock lock(mutex_);
  try {
    return configuration_.version;
  } catch (...) {
    return {};
  }
}

std::size_t SystemVerilogVpiIoService::open_files() const {
  std::scoped_lock lock(mutex_);
  return files_.size();
}

void SystemVerilogVpiIoService::teardown() noexcept {
  std::scoped_lock lock(mutex_);
  if (closed_) {
    return;
  }
  closed_ = true;
  for (auto& [value, file] : files_) {
    static_cast<void>(value);
    try {
      file->stream.flush();
      file->stream.close();
    } catch (...) {
    }
  }
  files_.clear();
  configuration_.output = {};
  configuration_.diagnostic = {};
}

}  // namespace fsim::runtime
