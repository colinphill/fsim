// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_context.hpp"

#include <new>
#include <string_view>

namespace fsim::runtime {

namespace {

[[nodiscard]] bool has_control_character(const std::string_view name) {
  for (const char byte : name) {
    const auto character = static_cast<unsigned char>(byte);
    if (character < 0x20U || character == 0x7fU) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool scope_belongs_to_module(
    const std::string_view module_instance,
    const std::string_view scope) noexcept {
  return scope == module_instance ||
         (scope.size() > module_instance.size() &&
          scope.starts_with(module_instance) &&
          scope[module_instance.size()] == '.');
}

}  // namespace

TfContextValidationResult validate_and_copy_tf_context(
    const TfContextProfile& profile) noexcept {
  if (profile.module_instance_name.empty()) {
    return {.value = {}, .error = TfContextError::EmptyModuleInstance};
  }
  if (profile.scope_name.empty()) {
    return {.value = {}, .error = TfContextError::EmptyScope};
  }
  if (profile.module_instance_name.size() > kMaxTfContextNameSize ||
      profile.scope_name.size() > kMaxTfContextNameSize) {
    return {.value = {}, .error = TfContextError::NameSize};
  }
  if (has_control_character(profile.module_instance_name) ||
      has_control_character(profile.scope_name)) {
    return {.value = {}, .error = TfContextError::ControlCharacter};
  }
  if (!scope_belongs_to_module(profile.module_instance_name,
                               profile.scope_name)) {
    return {.value = {}, .error = TfContextError::ScopeOwnership};
  }
  try {
    return {.value = profile};
  } catch (const std::bad_alloc&) {
    return {.value = {}, .error = TfContextError::Allocation};
  } catch (...) {
    return {.value = {}, .error = TfContextError::Allocation};
  }
}

}  // namespace fsim::runtime
