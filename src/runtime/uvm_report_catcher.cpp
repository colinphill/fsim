// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_report.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

constexpr std::string_view kDefaultActionProbe{"*@&*^*^*#"};

constexpr std::array<std::string_view, 4> kSeverityNames{
    "UVM_INFO", "UVM_WARNING", "UVM_ERROR", "UVM_FATAL"};

class CatcherDispatchGuard final {
 public:
  explicit CatcherDispatchGuard(bool& active) noexcept : active_{active} {
    active_ = true;
  }
  ~CatcherDispatchGuard() { active_ = false; }

 private:
  bool& active_;
};

}  // namespace

SystemVerilogUvmReportCatcherHandle
SystemVerilogUvmReportService::add_catcher(
    const SystemVerilogClassHandle report_object,
    std::string name,
    ReportCatcher catcher,
    const SystemVerilogUvmReportCatcherOrdering ordering) {
  if (report_object != 0) {
    validate_object(report_object);
  }
  if (name.size() > limits_.max_catcher_name_bytes) {
    throw std::length_error{"UVM report catcher name exceeds limit"};
  }
  if (!catcher) {
    throw std::invalid_argument{"UVM report catcher callback is empty"};
  }
  if (ordering != SystemVerilogUvmReportCatcherOrdering::Append
      && ordering != SystemVerilogUvmReportCatcherOrdering::Prepend) {
    throw std::invalid_argument{"invalid UVM report catcher ordering"};
  }
  if (catchers_.size() >= limits_.max_catchers) {
    throw std::length_error{"UVM report catcher count exceeds limit"};
  }
  if (next_catcher_ == 0) {
    throw std::overflow_error{"UVM report catcher handle overflow"};
  }

  const auto handle = next_catcher_;
  auto [inserted, created] = catchers_.emplace(
      handle,
      CatcherState{report_object, std::move(name), std::move(catcher), true});
  if (!created) {
    throw std::logic_error{"duplicate UVM report catcher handle"};
  }
  try {
    if (ordering == SystemVerilogUvmReportCatcherOrdering::Prepend) {
      catcher_order_.insert(catcher_order_.begin(), handle);
    } else {
      catcher_order_.push_back(handle);
    }
  } catch (...) {
    catchers_.erase(inserted);
    throw;
  }
  ++next_catcher_;
  return handle;
}

bool SystemVerilogUvmReportService::remove_catcher(
    const SystemVerilogUvmReportCatcherHandle catcher) noexcept {
  const auto found = catchers_.find(catcher);
  if (found == catchers_.end()) {
    return false;
  }
  catchers_.erase(found);
  const auto ordered = std::find(
      catcher_order_.begin(), catcher_order_.end(), catcher);
  if (ordered != catcher_order_.end()) {
    catcher_order_.erase(ordered);
  }
  return true;
}

void SystemVerilogUvmReportService::set_catcher_enabled(
    const SystemVerilogUvmReportCatcherHandle catcher,
    const bool enabled) {
  const auto found = catchers_.find(catcher);
  if (found == catchers_.end()) {
    throw std::invalid_argument{"unknown UVM report catcher handle"};
  }
  found->second.enabled = enabled;
}

bool SystemVerilogUvmReportService::catcher_enabled(
    const SystemVerilogUvmReportCatcherHandle catcher) const {
  const auto found = catchers_.find(catcher);
  if (found == catchers_.end()) {
    throw std::invalid_argument{"unknown UVM report catcher handle"};
  }
  return found->second.enabled;
}

std::uint64_t SystemVerilogUvmReportService::caught_count(
    const SystemVerilogUvmReportSeverity severity) const {
  return caught_counts_.at(severity_index(severity));
}

std::uint64_t SystemVerilogUvmReportService::demoted_count(
    const SystemVerilogUvmReportSeverity severity) const {
  return demoted_counts_.at(severity_index(severity));
}

std::string SystemVerilogUvmReportService::catcher_summary() const {
  std::ostringstream stream;
  stream << "** Report catcher summary\n";
  for (std::size_t index = 0; index < kSeverityNames.size(); ++index) {
    stream << "Caught " << kSeverityNames[index] << " : "
           << std::setw(5) << caught_counts_[index] << '\n';
  }
  for (std::size_t index = 0; index < kSeverityNames.size(); ++index) {
    stream << "Demoted " << kSeverityNames[index] << " : "
           << std::setw(5) << demoted_counts_[index] << '\n';
  }
  return stream.str();
}

bool SystemVerilogUvmReportService::run_catchers(
    SystemVerilogUvmReportMessage& message) {
  if (in_catcher_) {
    ++catcher_reentry_bypasses_;
    return true;
  }

  std::vector<SystemVerilogUvmReportCatcherHandle> dispatch;
  dispatch.reserve(std::min(
      catcher_order_.size(), limits_.max_catcher_dispatches));
  for (const auto handle : catcher_order_) {
    const auto found = catchers_.find(handle);
    if (found == catchers_.end() || !found->second.enabled
        || (found->second.report_object != 0
            && found->second.report_object != message.report_object)) {
      continue;
    }
    if (dispatch.size() >= limits_.max_catcher_dispatches) {
      throw std::length_error{"UVM report catcher dispatch exceeds limit"};
    }
    dispatch.push_back(handle);
  }
  if (dispatch.empty()) {
    return true;
  }

  CatcherDispatchGuard guard{in_catcher_};
  const auto original_severity = message.severity;
  bool thrown = true;
  for (const auto handle : dispatch) {
    const auto found = catchers_.find(handle);
    if (found == catchers_.end() || !found->second.enabled) {
      continue;
    }
    const auto callback = found->second.callback;
    auto saved = message;
    const auto previous_severity = message.severity;
    SystemVerilogUvmReportCatcherContext context{message};
    try {
      ++catcher_invocations_;
      const auto result = callback(context);
      validate_message(message);
      if (!context.action_set() && message.severity != previous_severity) {
        const auto previous_default = policy(
            message.report_object, previous_severity, kDefaultActionProbe);
        if (saved.action == previous_default.action) {
          message.action = policy(
              message.report_object, message.severity, kDefaultActionProbe)
                               .action;
        }
      }
      if (result == SystemVerilogUvmReportCatcherResult::Caught) {
        thrown = false;
        ++caught_counts_.at(severity_index(original_severity));
        break;
      }
      if (result != SystemVerilogUvmReportCatcherResult::Throw) {
        throw std::invalid_argument{"invalid UVM report catcher result"};
      }
    } catch (...) {
      message = std::move(saved);
      ++catcher_failures_;
    }
  }

  if (static_cast<std::uint8_t>(message.severity)
      < static_cast<std::uint8_t>(original_severity)) {
    ++demoted_counts_.at(severity_index(original_severity));
  }
  return thrown;
}

}  // namespace fsim::runtime
