// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_io.hpp"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_context_identity = 0xc8U;
constexpr std::size_t maximum_text_bytes = 65'536;
constexpr std::size_t maximum_arguments = 64;
constexpr std::size_t maximum_contexts = 4'096;
constexpr std::size_t maximum_events = 65'536;
std::atomic<std::uint32_t> next_io_system{1U};

[[nodiscard]] bool valid_severity(
    const VhdlVhpiSeverity severity) noexcept {
  return static_cast<std::uint32_t>(severity)
      <= static_cast<std::uint32_t>(VhdlVhpiSeverity::Failure);
}

[[nodiscard]] bool valid_source(
    const VhdlVhpiSourceLocation& source) noexcept {
  return !source.file.empty() && source.file.size() <= 4'096
      && source.line != 0U && source.column != 0U;
}

}  // namespace

VhdlVhpiIoSystem::VhdlVhpiIoSystem(
    VhdlVhpiObjectRegistry& objects) noexcept
    : objects_(&objects),
      system_identity_(
          next_io_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_io_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

VhdlVhpiIoSystem::~VhdlVhpiIoSystem() {
  teardown();
}

std::uint64_t VhdlVhpiIoSystem::make_identity(
    const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(k_context_identity) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiIoError VhdlVhpiIoSystem::validate_identity(
    const std::uint64_t identity) const noexcept {
  if (identity == 0U
      || static_cast<std::uint8_t>(identity >> 56U)
          != k_context_identity
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiIoError::InvalidContext;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiIoError::None
                                   : VhdlVhpiIoError::CrossSimulation;
}

VhdlVhpiIoResult<fsim_vhpi_handle_v1> VhdlVhpiIoSystem::root_of(
    fsim_vhpi_handle_v1 object) const {
  for (std::uint32_t depth = 0; depth < 1'024; ++depth) {
    const auto metadata = objects_->lookup_object(object);
    if (!metadata) {
      return {{}, metadata.error == VhdlVhpiObjectError::CrossSimulation
          ? VhdlVhpiIoError::CrossSimulation
          : VhdlVhpiIoError::InvalidObject};
    }
    if (metadata.value.parent == 0U) {
      return {metadata.value.handle, VhdlVhpiIoError::None};
    }
    object = metadata.value.parent;
  }
  return {{}, VhdlVhpiIoError::ResourceLimit};
}

VhdlVhpiIoContextResult VhdlVhpiIoSystem::register_context(
    VhdlVhpiIoContextProfile profile) {
  if (objects_ == nullptr || !objects_->valid()) {
    return {{}, VhdlVhpiIoError::InvalidSimulation};
  }
  const auto root = objects_->lookup_object(profile.root);
  if (!root || root.value.kind != VhdlVhpiObjectKind::Root
      || root.value.parent != 0U) {
    return {{}, root.error == VhdlVhpiObjectError::CrossSimulation
        ? VhdlVhpiIoError::CrossSimulation
        : VhdlVhpiIoError::InvalidObject};
  }
  if (profile.name.empty() || profile.name.size() > 256
      || !profile.sink) {
    return {{}, VhdlVhpiIoError::InvalidContext};
  }
  std::scoped_lock dispatch_lock{dispatch_mutex_};
  std::scoped_lock lock{mutex_};
  if (closed_) {
    return {{}, VhdlVhpiIoError::Closed};
  }
  if (contexts_.size() >= maximum_contexts || next_context_ == 0U) {
    return {{}, VhdlVhpiIoError::ResourceLimit};
  }
  const auto identity = make_identity(next_context_++);
  VhdlVhpiIoContextDescriptor descriptor{
      identity,
      profile.root,
      std::move(profile.name),
      next_context_ordinal_++,
      0,
      true};
  try {
    contexts_.emplace(
        identity,
        ContextEntry{descriptor, std::move(profile.sink), {}});
  } catch (...) {
    return {{}, VhdlVhpiIoError::ResourceLimit};
  }
  return {descriptor, VhdlVhpiIoError::None};
}

VhdlVhpiIoContextResult VhdlVhpiIoSystem::context(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity);
  if (error != VhdlVhpiIoError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = contexts_.find(identity);
  return found == contexts_.end()
      ? VhdlVhpiIoContextResult{{}, VhdlVhpiIoError::InvalidContext}
      : VhdlVhpiIoContextResult{
            found->second.descriptor, VhdlVhpiIoError::None};
}

VhdlVhpiIoError VhdlVhpiIoSystem::format_message(
    const std::string_view format,
    const std::span<const std::string_view> arguments,
    std::string& result) {
  if (format.size() > maximum_text_bytes
      || arguments.size() > maximum_arguments) {
    return VhdlVhpiIoError::TextTooLong;
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
          return VhdlVhpiIoError::InvalidFormat;
        }
        if (arguments[argument].size() > maximum_text_bytes
            || result.size() + arguments[argument].size()
                > maximum_text_bytes) {
          return VhdlVhpiIoError::TextTooLong;
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
        return VhdlVhpiIoError::InvalidFormat;
      }
      result.push_back(format[index++]);
      if (result.size() > maximum_text_bytes) {
        return VhdlVhpiIoError::TextTooLong;
      }
    }
    if (argument != arguments.size()) {
      return VhdlVhpiIoError::InvalidFormat;
    }
  } catch (...) {
    return VhdlVhpiIoError::ResourceLimit;
  }
  return VhdlVhpiIoError::None;
}

VhdlVhpiIoError VhdlVhpiIoSystem::emit(
    const std::uint64_t context_identity,
    const fsim_vhpi_handle_v1 subject,
    const VhdlVhpiIoKind kind,
    const VhdlVhpiSeverity severity,
    std::string message,
    std::optional<VhdlVhpiSourceLocation> source) {
  if (!valid_severity(severity)) {
    return VhdlVhpiIoError::InvalidSeverity;
  }
  if (message.size() > maximum_text_bytes) {
    return VhdlVhpiIoError::TextTooLong;
  }
  if (source && !valid_source(*source)) {
    return VhdlVhpiIoError::InvalidSource;
  }
  const auto identity_error = validate_identity(context_identity);
  if (identity_error != VhdlVhpiIoError::None) {
    return identity_error;
  }

  std::scoped_lock dispatch_lock{dispatch_mutex_};
  VhdlVhpiIoSink sink;
  VhdlVhpiIoEvent event;
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return VhdlVhpiIoError::Closed;
    }
    const auto found = contexts_.find(context_identity);
    if (found == contexts_.end() || !found->second.descriptor.active) {
      return VhdlVhpiIoError::InvalidContext;
    }
    if (kind != VhdlVhpiIoKind::Output) {
      if (subject == 0U) {
        return VhdlVhpiIoError::InvalidObject;
      }
      const auto subject_root = root_of(subject);
      if (!subject_root) {
        return subject_root.error;
      }
      if (subject_root.value != found->second.descriptor.root) {
        return VhdlVhpiIoError::CrossRoot;
      }
    } else if (subject != 0U || source) {
      return VhdlVhpiIoError::InvalidObject;
    }
    if (history_.size() >= maximum_events
        || found->second.history.size() >= maximum_events) {
      return VhdlVhpiIoError::ResourceLimit;
    }
    event = VhdlVhpiIoEvent{
        next_event_ordinal_++,
        context_identity,
        found->second.descriptor.root,
        subject,
        kind,
        severity,
        std::move(message),
        std::move(source)};
    try {
      sink = found->second.sink;
      history_.push_back(event);
      found->second.history.push_back(event);
      ++found->second.descriptor.events;
    } catch (...) {
      if (!history_.empty()
          && history_.back().ordinal == event.ordinal) {
        history_.pop_back();
      }
      if (!found->second.history.empty()
          && found->second.history.back().ordinal == event.ordinal) {
        found->second.history.pop_back();
      }
      return VhdlVhpiIoError::ResourceLimit;
    }
  }
  try {
    sink(event);
  } catch (...) {
    return VhdlVhpiIoError::SinkFailed;
  }
  return VhdlVhpiIoError::None;
}

VhdlVhpiIoError VhdlVhpiIoSystem::output(
    const std::uint64_t context_identity,
    const std::string_view text) {
  if (text.size() > maximum_text_bytes) {
    return VhdlVhpiIoError::TextTooLong;
  }
  try {
    return emit(
        context_identity,
        0,
        VhdlVhpiIoKind::Output,
        VhdlVhpiSeverity::Note,
        std::string{text},
        std::nullopt);
  } catch (...) {
    return VhdlVhpiIoError::ResourceLimit;
  }
}

VhdlVhpiIoError VhdlVhpiIoSystem::report(
    const std::uint64_t context_identity,
    const fsim_vhpi_handle_v1 subject,
    const VhdlVhpiSeverity severity,
    const std::string_view format,
    const std::span<const std::string_view> arguments,
    std::optional<VhdlVhpiSourceLocation> source) {
  std::string message;
  const auto formatted = format_message(format, arguments, message);
  if (formatted != VhdlVhpiIoError::None) {
    return formatted;
  }
  return emit(
      context_identity,
      subject,
      VhdlVhpiIoKind::Report,
      severity,
      std::move(message),
      std::move(source));
}

VhdlVhpiIoError VhdlVhpiIoSystem::assertion(
    const std::uint64_t context_identity,
    const fsim_vhpi_handle_v1 subject,
    const bool condition,
    const VhdlVhpiSeverity severity,
    const std::string_view format,
    const std::span<const std::string_view> arguments,
    std::optional<VhdlVhpiSourceLocation> source) {
  if (condition) {
    return VhdlVhpiIoError::None;
  }
  std::string message;
  const auto formatted = format_message(format, arguments, message);
  if (formatted != VhdlVhpiIoError::None) {
    return formatted;
  }
  return emit(
      context_identity,
      subject,
      VhdlVhpiIoKind::Assertion,
      severity,
      std::move(message),
      std::move(source));
}

VhdlVhpiIoHistoryResult VhdlVhpiIoSystem::history(
    const std::uint64_t context_identity) const {
  const auto error = validate_identity(context_identity);
  if (error != VhdlVhpiIoError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = contexts_.find(context_identity);
  if (found == contexts_.end()) {
    return {{}, VhdlVhpiIoError::InvalidContext};
  }
  try {
    return {found->second.history, VhdlVhpiIoError::None};
  } catch (...) {
    return {{}, VhdlVhpiIoError::ResourceLimit};
  }
}

std::vector<VhdlVhpiIoEvent> VhdlVhpiIoSystem::history() const {
  std::scoped_lock lock{mutex_};
  return history_;
}

void VhdlVhpiIoSystem::teardown() noexcept {
  std::scoped_lock dispatch_lock{dispatch_mutex_};
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return;
    }
    closed_ = true;
  }
  while (true) {
    VhdlVhpiIoSink sink;
    {
      std::scoped_lock lock{mutex_};
      auto selected = contexts_.end();
      for (auto position = contexts_.begin();
           position != contexts_.end(); ++position) {
        if (!position->second.sink) {
          continue;
        }
        if (selected == contexts_.end()
            || position->second.descriptor.ordinal
                < selected->second.descriptor.ordinal) {
          selected = position;
        }
      }
      if (selected == contexts_.end()) {
        return;
      }
      selected->second.descriptor.active = false;
      sink = std::move(selected->second.sink);
    }
    sink = {};
  }
}

}  // namespace fsim::runtime
