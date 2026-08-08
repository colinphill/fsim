// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_report.hpp"

#include "fsim/runtime/uvm_component.hpp"

#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

template <typename Map, typename Key, typename Value>
void assign_bounded(
    Map& values,
    Key&& key,
    Value&& value,
    std::size_t& count,
    const std::size_t maximum) {
  const auto found = values.find(key);
  if (found != values.end()) {
    found->second = std::forward<Value>(value);
    return;
  }
  if (count >= maximum) {
    throw std::length_error{"UVM report handler setting budget exceeded"};
  }
  values.emplace(
      std::forward<Key>(key), std::forward<Value>(value));
  ++count;
}

}  // namespace

std::size_t SystemVerilogUvmReportService::severity_index(
    const SystemVerilogUvmReportSeverity severity) {
  switch (severity) {
  case SystemVerilogUvmReportSeverity::Info: return 0;
  case SystemVerilogUvmReportSeverity::Warning: return 1;
  case SystemVerilogUvmReportSeverity::Error: return 2;
  case SystemVerilogUvmReportSeverity::Fatal: return 3;
  default: throw std::invalid_argument{"invalid UVM report severity"};
  }
}

SystemVerilogUvmReportService::HandlerState&
SystemVerilogUvmReportService::mutable_handler(
    const SystemVerilogClassHandle report_object) {
  validate_object(report_object);
  const auto found = handlers_.find(report_object);
  if (found != handlers_.end()) return found->second;
  if (handlers_.size() >= limits_.max_handlers) {
    throw std::length_error{"UVM report handler budget exceeded"};
  }
  HandlerState state;
  state.verbosity = default_verbosity_;
  return handlers_.emplace(report_object, std::move(state)).first->second;
}

const SystemVerilogUvmReportService::HandlerState*
SystemVerilogUvmReportService::find_handler(
    const SystemVerilogClassHandle report_object) const noexcept {
  const auto found = handlers_.find(report_object);
  return found == handlers_.end() ? nullptr : &found->second;
}

SystemVerilogUvmReportPolicy SystemVerilogUvmReportService::policy(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    const std::string_view id) const {
  validate_object(report_object);
  validate_severity(severity);
  validate_id(id);
  const auto* state = find_handler(report_object);
  const auto original_index = severity_index(severity);
  SystemVerilogUvmReportPolicy result;
  result.severity = severity;
  result.verbosity = state ? state->verbosity : default_verbosity_;
  if (!state) {
    result.action = HandlerState{}.severity_actions[original_index];
    return result;
  }

  const auto severity_id_verbosity =
      state->severity_id_verbosities[original_index].find(id);
  if (severity_id_verbosity
      != state->severity_id_verbosities[original_index].end()) {
    result.verbosity = severity_id_verbosity->second;
  } else if (const auto id_verbosity = state->id_verbosities.find(id);
             id_verbosity != state->id_verbosities.end()) {
    result.verbosity = id_verbosity->second;
  }

  const auto id_override = state->severity_id_overrides.find(id);
  if (id_override != state->severity_id_overrides.end()) {
    if (id_override->second[original_index]) {
      result.severity = *id_override->second[original_index];
    }
  } else if (state->severity_overrides[original_index]) {
    result.severity = *state->severity_overrides[original_index];
  }

  const auto resolved_index = severity_index(result.severity);
  result.action = state->severity_actions[resolved_index];
  const auto severity_id_action =
      state->severity_id_actions[resolved_index].find(id);
  if (severity_id_action
      != state->severity_id_actions[resolved_index].end()) {
    result.action = severity_id_action->second;
  } else if (const auto id_action = state->id_actions.find(id);
             id_action != state->id_actions.end()) {
    result.action = id_action->second;
  }

  result.file = state->default_file;
  if (state->severity_files[resolved_index] != 0) {
    result.file = state->severity_files[resolved_index];
  }
  if (const auto id_file = state->id_files.find(id);
      id_file != state->id_files.end() && id_file->second != 0) {
    result.file = id_file->second;
  }
  if (const auto severity_id_file =
          state->severity_id_files[resolved_index].find(id);
      severity_id_file != state->severity_id_files[resolved_index].end()
      && severity_id_file->second != 0) {
    result.file = severity_id_file->second;
  }
  return result;
}

void SystemVerilogUvmReportService::set_default_verbosity(
    const std::int32_t verbosity) noexcept {
  default_verbosity_ = verbosity;
  for (auto& [object, handler] : handlers_) {
    (void)object;
    handler.verbosity = verbosity;
  }
}

void SystemVerilogUvmReportService::set_verbosity(
    const SystemVerilogClassHandle report_object,
    const std::int32_t verbosity) {
  mutable_handler(report_object).verbosity = verbosity;
}

void SystemVerilogUvmReportService::set_id_verbosity(
    const SystemVerilogClassHandle report_object,
    std::string id,
    const std::int32_t verbosity) {
  validate_id(id);
  auto& state = mutable_handler(report_object);
  assign_bounded(
      state.id_verbosities, std::move(id), verbosity,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_severity_id_verbosity(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const std::int32_t verbosity) {
  validate_severity(severity);
  validate_id(id);
  auto& values = mutable_handler(report_object)
      .severity_id_verbosities[severity_index(severity)];
  assign_bounded(
      values, std::move(id), verbosity,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_severity_action(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    const SystemVerilogUvmReportAction action) {
  validate_severity(severity);
  validate_action(action);
  mutable_handler(report_object).severity_actions[severity_index(severity)] =
      action;
}

void SystemVerilogUvmReportService::set_id_action(
    const SystemVerilogClassHandle report_object,
    std::string id,
    const SystemVerilogUvmReportAction action) {
  validate_id(id);
  validate_action(action);
  auto& values = mutable_handler(report_object).id_actions;
  assign_bounded(
      values, std::move(id), action,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_severity_id_action(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const SystemVerilogUvmReportAction action) {
  validate_severity(severity);
  validate_id(id);
  validate_action(action);
  auto& values = mutable_handler(report_object)
      .severity_id_actions[severity_index(severity)];
  assign_bounded(
      values, std::move(id), action,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_default_file(
    const SystemVerilogClassHandle report_object,
    const std::uint64_t file) {
  mutable_handler(report_object).default_file = file;
}

void SystemVerilogUvmReportService::set_severity_file(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    const std::uint64_t file) {
  validate_severity(severity);
  mutable_handler(report_object).severity_files[severity_index(severity)] =
      file;
}

void SystemVerilogUvmReportService::set_id_file(
    const SystemVerilogClassHandle report_object,
    std::string id,
    const std::uint64_t file) {
  validate_id(id);
  auto& values = mutable_handler(report_object).id_files;
  assign_bounded(
      values, std::move(id), file,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_severity_id_file(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const std::uint64_t file) {
  validate_severity(severity);
  validate_id(id);
  auto& values = mutable_handler(report_object)
      .severity_id_files[severity_index(severity)];
  assign_bounded(
      values, std::move(id), file,
      setting_count_, limits_.max_settings);
}

void SystemVerilogUvmReportService::set_severity_override(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity current,
    const SystemVerilogUvmReportSeverity replacement) {
  validate_severity(current);
  validate_severity(replacement);
  mutable_handler(report_object).severity_overrides[severity_index(current)] =
      replacement;
}

void SystemVerilogUvmReportService::set_severity_id_override(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity current,
    std::string id,
    const SystemVerilogUvmReportSeverity replacement) {
  validate_severity(current);
  validate_severity(replacement);
  validate_id(id);
  auto& overrides = mutable_handler(report_object).severity_id_overrides;
  auto found = overrides.find(id);
  if (found == overrides.end()) {
    if (setting_count_ >= limits_.max_settings) {
      throw std::length_error{
          "UVM report handler setting budget exceeded"};
    }
    found = overrides.emplace(
        std::move(id),
        std::array<std::optional<SystemVerilogUvmReportSeverity>, 4>{})
        .first;
  }
  auto& entry = found->second[severity_index(current)];
  if (!entry) {
    if (setting_count_ >= limits_.max_settings) {
      if (found->second == std::array<
              std::optional<SystemVerilogUvmReportSeverity>, 4>{}) {
        overrides.erase(found);
      }
      throw std::length_error{
          "UVM report handler setting budget exceeded"};
    }
    ++setting_count_;
  }
  entry = replacement;
}

void SystemVerilogUvmReportService::set_report_hook(
    const SystemVerilogClassHandle report_object,
    ReportHook hook) {
  mutable_handler(report_object).report_hook = std::move(hook);
}

void SystemVerilogUvmReportService::set_severity_hook(
    const SystemVerilogClassHandle report_object,
    const SystemVerilogUvmReportSeverity severity,
    ReportHook hook) {
  validate_severity(severity);
  mutable_handler(report_object).severity_hooks[severity_index(severity)] =
      std::move(hook);
}

bool SystemVerilogUvmReportService::run_hooks(
    const SystemVerilogUvmReportMessage& message) {
  if (!has_action(message.action, SystemVerilogUvmReportAction::CallHook)) {
    return true;
  }
  const auto* state = find_handler(message.report_object);
  if (!state) return true;
  const auto generic = state->report_hook;
  const auto severity = state->severity_hooks[severity_index(message.severity)];
  auto accepted = true;
  const auto invoke = [&](const ReportHook& hook) {
    if (!hook) return;
    try {
      accepted = hook(message) && accepted;
    } catch (...) {
      ++hook_failures_;
      accepted = false;
    }
  };
  invoke(generic);
  invoke(severity);
  return accepted;
}

void SystemVerilogUvmReportService::mutate_hierarchy(
    const SystemVerilogClassHandle component,
    const std::function<void(SystemVerilogClassHandle)>& mutation) {
  if (!components_ || !components_->contains(component)) {
    throw std::invalid_argument{"invalid UVM report component handle"};
  }
  std::vector<SystemVerilogClassHandle> stack{component};
  std::vector<SystemVerilogClassHandle> ordered;
  while (!stack.empty()) {
    const auto current = stack.back();
    stack.pop_back();
    ordered.push_back(current);
    const auto& children = components_->children(current);
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      stack.push_back(*child);
    }
  }
  std::vector<std::pair<
      SystemVerilogClassHandle, std::optional<HandlerState>>> backup;
  backup.reserve(ordered.size());
  for (const auto object : ordered) {
    const auto found = handlers_.find(object);
    backup.emplace_back(
        object,
        found == handlers_.end()
            ? std::optional<HandlerState>{}
            : std::optional<HandlerState>{found->second});
  }
  const auto settings_before = setting_count_;
  try {
    for (const auto object : ordered) mutation(object);
  } catch (...) {
    setting_count_ = settings_before;
    for (auto& [object, state] : backup) {
      if (!state) {
        handlers_.erase(object);
      } else {
        handlers_.find(object)->second = std::move(*state);
      }
    }
    throw;
  }
}

void SystemVerilogUvmReportService::set_verbosity_hier(
    const SystemVerilogClassHandle component,
    const std::int32_t verbosity) {
  mutate_hierarchy(
      component, [&](const auto object) { set_verbosity(object, verbosity); });
}

void SystemVerilogUvmReportService::set_id_verbosity_hier(
    const SystemVerilogClassHandle component,
    std::string id,
    const std::int32_t verbosity) {
  validate_id(id);
  mutate_hierarchy(component, [&](const auto object) {
    set_id_verbosity(object, id, verbosity);
  });
}

void SystemVerilogUvmReportService::set_severity_id_verbosity_hier(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const std::int32_t verbosity) {
  validate_severity(severity);
  validate_id(id);
  mutate_hierarchy(component, [&](const auto object) {
    set_severity_id_verbosity(object, severity, id, verbosity);
  });
}

void SystemVerilogUvmReportService::set_severity_action_hier(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmReportSeverity severity,
    const SystemVerilogUvmReportAction action) {
  validate_severity(severity);
  validate_action(action);
  mutate_hierarchy(component, [&](const auto object) {
    set_severity_action(object, severity, action);
  });
}

void SystemVerilogUvmReportService::set_id_action_hier(
    const SystemVerilogClassHandle component,
    std::string id,
    const SystemVerilogUvmReportAction action) {
  validate_id(id);
  validate_action(action);
  mutate_hierarchy(component, [&](const auto object) {
    set_id_action(object, id, action);
  });
}

void SystemVerilogUvmReportService::set_severity_id_action_hier(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const SystemVerilogUvmReportAction action) {
  validate_severity(severity);
  validate_id(id);
  validate_action(action);
  mutate_hierarchy(component, [&](const auto object) {
    set_severity_id_action(object, severity, id, action);
  });
}

void SystemVerilogUvmReportService::set_default_file_hier(
    const SystemVerilogClassHandle component,
    const std::uint64_t file) {
  mutate_hierarchy(
      component, [&](const auto object) { set_default_file(object, file); });
}

void SystemVerilogUvmReportService::set_severity_file_hier(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmReportSeverity severity,
    const std::uint64_t file) {
  validate_severity(severity);
  mutate_hierarchy(component, [&](const auto object) {
    set_severity_file(object, severity, file);
  });
}

void SystemVerilogUvmReportService::set_id_file_hier(
    const SystemVerilogClassHandle component,
    std::string id,
    const std::uint64_t file) {
  validate_id(id);
  mutate_hierarchy(component, [&](const auto object) {
    set_id_file(object, id, file);
  });
}

void SystemVerilogUvmReportService::set_severity_id_file_hier(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmReportSeverity severity,
    std::string id,
    const std::uint64_t file) {
  validate_severity(severity);
  validate_id(id);
  mutate_hierarchy(component, [&](const auto object) {
    set_severity_id_file(object, severity, id, file);
  });
}

}  // namespace fsim::runtime
