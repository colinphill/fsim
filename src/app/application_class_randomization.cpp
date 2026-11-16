// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

void configure_systemverilog_randomize_selection(
    runtime::SystemVerilogClassRandomizeRequest& request,
    const std::span<const std::string> selected_names,
    const bool no_properties) {
  // `randomize(null)` is the checker call (18.11.1): it names nothing else.
  if (no_properties) {
    if (std::ranges::any_of(selected_names,
            [](const std::string& name) { return !name.empty(); })) {
      throw std::invalid_argument{
          "class randomize checker marker must be the only selection"};
    }
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
    const bool random_mode,
    const std::string_view target,
    const std::span<const runtime::PackedLogic4> actuals) {
  if (actuals.size() > 1U) {
    throw std::invalid_argument{
        "randomization mode method accepts zero or one argument"};
  }
  const auto random = random_mode;
  const auto identity = target;
  // An object-level call sets every random variable or constraint and has
  // no value (IEEE 1800-2017 18.8, 18.9).
  if (identity.empty()) {
    const auto value = actuals.empty()
        ? runtime::Logic4Word{}
        : actuals.front().low_word();
    if (actuals.empty() || value.bval != 0
        || (value.aval != 0 && value.aval != 1)) {
      throw std::invalid_argument{
          "object randomization mode update requires a known zero or one"};
    }
    if (random) heap.set_all_random_modes(handle, value.aval != 0);
    else heap.set_all_constraint_modes(handle, value.aval != 0);
    return runtime::PackedLogic4::from_aval_bval(32, 0U, 0);
  }
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
