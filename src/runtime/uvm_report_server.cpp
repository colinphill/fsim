// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_report.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fsim::runtime {
namespace {

constexpr std::uint32_t kKnownActions =
    static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Display)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Log)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Count)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Exit)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::CallHook)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Stop)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Record);

std::string_view severity_name(
    const SystemVerilogUvmReportSeverity severity) {
  switch (severity) {
  case SystemVerilogUvmReportSeverity::Info: return "UVM_INFO";
  case SystemVerilogUvmReportSeverity::Warning: return "UVM_WARNING";
  case SystemVerilogUvmReportSeverity::Error: return "UVM_ERROR";
  case SystemVerilogUvmReportSeverity::Fatal: return "UVM_FATAL";
  default: throw std::invalid_argument{"invalid UVM report severity"};
  }
}

std::string verbosity_name(const std::int32_t verbosity) {
  switch (verbosity) {
  case 0: return "UVM_NONE";
  case 100: return "UVM_LOW";
  case 200: return "UVM_MEDIUM";
  case 300: return "UVM_HIGH";
  case 400: return "UVM_FULL";
  case 500: return "UVM_DEBUG";
  default: return std::to_string(verbosity);
  }
}

void append_bounded(
    std::string& destination,
    const std::string_view value,
    const std::size_t maximum) {
  if (destination.size() > maximum
      || value.size() > maximum - destination.size()) {
    throw std::length_error{"UVM report-server output exceeds its budget"};
  }
  destination.append(value);
}

std::optional<std::uint64_t> effective_log_file(
    const std::uint64_t file) {
  constexpr std::uint64_t stdout_handle{0x8000'0001ULL};
  constexpr std::uint64_t mcd_mask{0x8000'0000ULL};
  if (file == 0 || file == stdout_handle) return std::nullopt;
  const auto result = (file & mcd_mask) == 0 ? file & ~1ULL : file;
  return result == 0 ? std::nullopt : std::optional{result};
}

}  // namespace

SystemVerilogUvmReportServer::SystemVerilogUvmReportServer(
    SystemVerilogUvmReportServerLimits limits)
    : limits_(limits) {
  if (limits_.max_ids == 0 || limits_.max_output_bytes == 0) {
    throw std::invalid_argument{"UVM report-server limits must be nonzero"};
  }
}

std::size_t SystemVerilogUvmReportServer::severity_index(
    const SystemVerilogUvmReportSeverity severity) {
  switch (severity) {
  case SystemVerilogUvmReportSeverity::Info: return 0;
  case SystemVerilogUvmReportSeverity::Warning: return 1;
  case SystemVerilogUvmReportSeverity::Error: return 2;
  case SystemVerilogUvmReportSeverity::Fatal: return 3;
  default: throw std::invalid_argument{"invalid UVM report severity"};
  }
}

void SystemVerilogUvmReportServer::validate_severity(
    const SystemVerilogUvmReportSeverity severity) const {
  (void)severity_index(severity);
}

std::string SystemVerilogUvmReportServer::compose(
    const SystemVerilogUvmReportMessage& message,
    const std::string_view payload) const {
  validate_severity(message.severity);
  std::string result;
  append_bounded(result, severity_name(message.severity),
                 limits_.max_output_bytes);
  if (show_verbosity_) {
    append_bounded(result, "(", limits_.max_output_bytes);
    append_bounded(
        result, verbosity_name(message.verbosity),
        limits_.max_output_bytes);
    append_bounded(result, ")", limits_.max_output_bytes);
  }
  append_bounded(result, " ", limits_.max_output_bytes);
  if (!message.filename.empty()) {
    append_bounded(result, message.filename, limits_.max_output_bytes);
    append_bounded(result, "(", limits_.max_output_bytes);
    append_bounded(
        result, std::to_string(message.line), limits_.max_output_bytes);
    append_bounded(result, ") ", limits_.max_output_bytes);
  }
  append_bounded(result, "@ ", limits_.max_output_bytes);
  append_bounded(
      result, std::to_string(message.timestamp), limits_.max_output_bytes);
  append_bounded(result, ": ", limits_.max_output_bytes);
  append_bounded(result, message.report_object_name,
                 limits_.max_output_bytes);
  if (!message.context.empty()) {
    append_bounded(result, "@@", limits_.max_output_bytes);
    append_bounded(result, message.context, limits_.max_output_bytes);
  }
  append_bounded(result, " [", limits_.max_output_bytes);
  append_bounded(result, message.id, limits_.max_output_bytes);
  append_bounded(result, "] ", limits_.max_output_bytes);
  append_bounded(result, payload, limits_.max_output_bytes);
  if (show_terminator_) {
    append_bounded(result, " -", limits_.max_output_bytes);
    append_bounded(result, severity_name(message.severity),
                   limits_.max_output_bytes);
  }
  return result;
}

bool SystemVerilogUvmReportServer::process(
    SystemVerilogUvmReportMessage& message,
    const std::string_view payload,
    SystemVerilogUvmReportExecution* execution) {
  validate_severity(message.severity);
  if ((static_cast<std::uint32_t>(message.action) & ~kKnownActions) != 0) {
    throw std::invalid_argument{"invalid UVM report action"};
  }
  if (message.action == SystemVerilogUvmReportAction::None) return false;
  const auto severity = severity_index(message.severity);
  if (severity_counts_[severity]
          == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM report severity count overflow"};
  }
  const auto id = id_counts_.find(message.id);
  if (id == id_counts_.end() && id_counts_.size() >= limits_.max_ids) {
    throw std::length_error{"UVM report ID-count budget exceeded"};
  }
  if (id != id_counts_.end()
      && id->second == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM report ID count overflow"};
  }
  const auto count_action = has_action(
      message.action, SystemVerilogUvmReportAction::Count);
  if (count_action && max_quit_count_ != 0
      && quit_count_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM report quit count overflow"};
  }

  ++severity_counts_[severity];
  if (id == id_counts_.end()) {
    id_counts_.emplace(message.id, 1);
  } else {
    ++id->second;
  }
  if (record_all_messages_) {
    message.action = message.action | SystemVerilogUvmReportAction::Record;
  }

  SystemVerilogUvmReportExecution result;
  result.action = message.action;
  result.file = message.file;
  if (has_action(message.action, SystemVerilogUvmReportAction::Display)
      || has_action(message.action, SystemVerilogUvmReportAction::Log)) {
    result.composed = compose(message, payload);
  }
  const auto sink_record = result.composed + "\n";
  const auto contain = [&](const auto& function) {
    try {
      function();
    } catch (...) {
      ++sink_failures_;
    }
  };

  if (has_action(message.action, SystemVerilogUvmReportAction::Record)) {
    result.recorded = true;
    if (record_sink_) contain([&] { record_sink_(message); });
  }
  if (has_action(message.action, SystemVerilogUvmReportAction::Display)) {
    result.displayed = true;
    if (display_sink_) contain([&] { display_sink_(sink_record); });
  }
  if (has_action(message.action, SystemVerilogUvmReportAction::Log)) {
    const auto file = effective_log_file(message.file);
    if (file) {
      result.logged = true;
      result.file = *file;
      if (file_sink_) contain([&] { file_sink_(*file, sink_record); });
    }
  }
  if (count_action && max_quit_count_ != 0) {
    ++quit_count_;
    if (quit_count_ >= max_quit_count_) {
      message.action = message.action | SystemVerilogUvmReportAction::Exit;
    }
  }
  result.action = message.action;
  result.exit_requested = has_action(
      message.action, SystemVerilogUvmReportAction::Exit);
  result.stop_requested = has_action(
      message.action, SystemVerilogUvmReportAction::Stop);
  if ((result.exit_requested || result.stop_requested) && control_sink_) {
    contain([&] { control_sink_(result); });
  }
  if (execution != nullptr) *execution = std::move(result);
  return true;
}

bool SystemVerilogUvmReportServer::set_max_quit_count(
    const std::uint64_t count,
    const bool overridable) {
  if (!max_quit_overridable_) return false;
  max_quit_count_ = count;
  max_quit_overridable_ = overridable;
  return true;
}

void SystemVerilogUvmReportServer::set_severity_count(
    const SystemVerilogUvmReportSeverity severity,
    const std::uint64_t count) {
  severity_counts_[severity_index(severity)] = count;
}

void SystemVerilogUvmReportServer::set_id_count(
    std::string id,
    const std::uint64_t count) {
  const auto found = id_counts_.find(id);
  if (found != id_counts_.end()) {
    found->second = count;
    return;
  }
  if (id_counts_.size() >= limits_.max_ids) {
    throw std::length_error{"UVM report ID-count budget exceeded"};
  }
  id_counts_.emplace(std::move(id), count);
}

void SystemVerilogUvmReportServer::reset_counts() noexcept {
  severity_counts_.fill(0);
  id_counts_.clear();
  quit_count_ = 0;
}

std::uint64_t SystemVerilogUvmReportServer::severity_count(
    const SystemVerilogUvmReportSeverity severity) const {
  return severity_counts_[severity_index(severity)];
}

std::uint64_t SystemVerilogUvmReportServer::id_count(
    const std::string_view id) const noexcept {
  const auto found = id_counts_.find(id);
  return found == id_counts_.end() ? 0 : found->second;
}

std::string SystemVerilogUvmReportServer::summarize() const {
  std::ostringstream stream;
  stream << "\n--- UVM Report Summary ---\n\n";
  if (max_quit_count_ != 0) {
    if (quit_count_ >= max_quit_count_) {
      stream << "Quit count reached!\n";
    }
    stream << "Quit count : " << std::setw(5) << quit_count_
           << " of " << std::setw(5) << max_quit_count_ << '\n';
  }
  stream << "** Report counts by severity\n";
  for (std::size_t index = 0; index < severity_counts_.size(); ++index) {
    stream << severity_name(
                  static_cast<SystemVerilogUvmReportSeverity>(index))
           << " :" << std::setw(5) << severity_counts_[index] << '\n';
  }
  if (enable_id_summary_) {
    stream << "** Report counts by id\n";
    for (const auto& [id, count] : id_counts_) {
      stream << '[' << id << "] " << std::setw(5) << count << '\n';
    }
  }
  auto result = stream.str();
  if (result.size() > limits_.max_output_bytes) {
    throw std::length_error{"UVM report-server output exceeds its budget"};
  }
  return result;
}

}  // namespace fsim::runtime
