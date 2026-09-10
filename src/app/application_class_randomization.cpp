// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

const frontend::SystemVerilogClassMethodProfile*
systemverilog_randomize_callback(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const runtime::SystemVerilogClassHeap& heap,
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view name) {
  auto identity = heap.object(handle).specialization_identity;
  while (!identity.empty()) {
    const auto current = std::ranges::find(
        specializations, identity,
        &frontend::SystemVerilogClassSpecialization::specialization_identity);
    if (current == specializations.end()) return nullptr;
    const auto found = std::ranges::find(
        current->methods, name,
        &frontend::SystemVerilogClassMethodProfile::name);
    if (found != current->methods.end()) return &*found;
    identity = current->base_specialization_identity;
  }
  return nullptr;
}

void configure_systemverilog_randomize_selection(
    runtime::SystemVerilogClassRandomizeRequest& request,
    const std::span<const std::string> selected_names) {
  constexpr std::string_view no_properties{"@randomize-null"};
  const auto checker_call = selected_names.size() == 1U
      && selected_names.front() == no_properties;
  if (!checker_call
      && std::ranges::find(selected_names, no_properties)
          != selected_names.end()) {
    throw std::invalid_argument{
        "class randomize checker marker must be the only selection"};
  }
  if (checker_call) {
    request.selection =
        runtime::SystemVerilogClassRandomizeSelection::NoProperties;
    return;
  }
  for (const auto& name : selected_names) {
    if (!name.empty()) request.variable_list.push_back(name);
  }
}

runtime::PackedLogic4 invoke_systemverilog_randomization_mode(
    runtime::SystemVerilogClassHeap& heap,
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals) {
  constexpr std::string_view random_prefix{"@builtin-rand-mode:"};
  constexpr std::string_view constraint_prefix{
      "@builtin-constraint-mode:"};
  if (actuals.size() > 1U) {
    throw std::invalid_argument{
        "randomization mode method accepts zero or one argument"};
  }
  const auto random = method.starts_with(random_prefix);
  const auto identity = method.substr(
      random ? random_prefix.size() : constraint_prefix.size());
  if (!actuals.empty()) {
    const auto value = actuals.front().low_word();
    if (value.bval != 0 || (value.aval != 0 && value.aval != 1)) {
      throw std::invalid_argument{
          "randomization mode update requires a known zero or one"};
    }
    if (random) heap.set_random_mode(handle, identity, value.aval != 0);
    else heap.set_constraint_mode(handle, identity, value.aval != 0);
  }
  const auto enabled = random
      ? heap.random_mode(handle, identity)
      : heap.constraint_mode(handle, identity);
  return runtime::PackedLogic4::from_aval_bval(
      32, enabled ? 1U : 0U, 0);
}

}  // namespace fsim::app::application_detail
