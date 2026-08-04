// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fsim::elaboration {

const std::string& ElaboratedDesign::top() const noexcept {
  return top_;
}

const std::vector<std::string>& ElaboratedDesign::roots() const noexcept {
  return roots_;
}

const std::vector<SignalInfo>&
ElaboratedDesign::signals() const noexcept {
  return signal_info_;
}

const std::vector<BoundaryConversionInfo>&
ElaboratedDesign::boundary_conversions() const noexcept {
  return boundary_conversions_;
}

const std::vector<StringObjectInfo>&
ElaboratedDesign::string_objects() const noexcept {
  return string_object_info_;
}

const std::vector<ContainerObjectInfo>&
ElaboratedDesign::container_objects() const noexcept {
  return container_object_info_;
}

const std::vector<VhdlProtectedObjectInfo>&
ElaboratedDesign::vhdl_protected_objects() const noexcept {
  return vhdl_protected_object_info_;
}

const std::vector<runtime::simir::Process>&
ElaboratedDesign::processes() const noexcept {
  return processes_;
}

const std::vector<SpecializationInfo>&
ElaboratedDesign::specializations() const noexcept {
  return specializations_;
}

const std::vector<SystemCInstanceInfo>&
ElaboratedDesign::systemc_instances() const noexcept {
  return systemc_instances_;
}

const std::vector<SystemCProcessInfo>&
ElaboratedDesign::systemc_processes() const noexcept {
  return systemc_processes_;
}

const std::vector<SystemCNamedObjectInfo>&
ElaboratedDesign::systemc_objects() const noexcept {
  return systemc_objects_;
}

std::optional<runtime::simir::SignalId>
ElaboratedDesign::find_signal(
    const std::string_view name) const noexcept {
  if (const auto found = signal_by_name_.find(std::string{name});
      found != signal_by_name_.end()) {
    return found->second;
  }
  const auto object = std::find_if(
      systemc_objects_.begin(), systemc_objects_.end(),
      [&](const SystemCNamedObjectInfo& candidate) {
        return candidate.name == name && candidate.signal.has_value();
      });
  if (object != systemc_objects_.end()) {
    return object->signal;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, runtime::simir::SignalId>>
ElaboratedDesign::signal_paths() const {
  std::vector<std::pair<std::string, runtime::simir::SignalId>> result;
  result.reserve(signal_by_name_.size() + systemc_objects_.size());
  for (const auto& [path, signal] : signal_by_name_) {
    result.emplace_back(path, signal);
  }
  for (const auto& object : systemc_objects_) {
    if (object.signal) {
      result.emplace_back(object.name, *object.signal);
    }
  }
  std::sort(
      result.begin(),
      result.end(),
      [](const auto& left, const auto& right) {
        if (left.first != right.first) {
          return left.first < right.first;
        }
        return left.second < right.second;
      });
  result.erase(
      std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::optional<runtime::simir::ContainerObjectId>
ElaboratedDesign::find_container(
    const std::string_view name) const noexcept {
  if (const auto found =
          container_by_name_.find(std::string{name});
      found != container_by_name_.end()) {
    return found->second;
  }
  return std::nullopt;
}

std::vector<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
ElaboratedDesign::container_paths() const {
  std::vector<std::pair<
      std::string, runtime::simir::ContainerObjectId>> result;
  result.reserve(container_by_name_.size());
  for (const auto& [path, object] : container_by_name_) {
    result.emplace_back(path, object);
  }
  std::ranges::sort(
      result,
      [](const auto& left, const auto& right) {
        if (left.first != right.first) {
          return left.first < right.first;
        }
        return left.second < right.second;
      });
  return result;
}

std::unique_ptr<runtime::simir::Interpreter>
ElaboratedDesign::create_interpreter(
    const runtime::SchedulerOptions options,
    const std::uint64_t seed) const {
  auto interpreter =
      std::make_unique<runtime::simir::Interpreter>(options, seed);
  for (const auto& signal : signals_) {
    (void)interpreter->add_signal(signal);
  }
  for (const auto& object : string_objects_) {
    (void)interpreter->add_string_object(object);
  }
  for (const auto& object : container_objects_) {
    (void)interpreter->add_container_object(object);
  }
  for (const auto& process : processes_) {
    (void)interpreter->add_process(process);
  }
  return interpreter;
}

}  // namespace fsim::elaboration
