// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

std::vector<Process> Lowerer::take_generated_processes() {
  return std::exchange(generated_processes_, {});
}

std::optional<SignalId> Lowerer::vhdl_implicit_signal_attribute(
    const SignalId source,
    const std::string_view attribute,
    const runtime::SimulationTick duration,
    const frontend::SourceSpan span) {
  if (source >= design_.signals_.size()
      || source >= design_.signal_info_.size()) {
    report(
        "FSIM-ELAB-VHATTR-004",
        "implicit VHDL signal attribute references an invalid signal",
        span);
    return std::nullopt;
  }

  const auto source_info = design_.signal_info_[source];
  const auto name = source_info.name + "'" + std::string{attribute}
      + "(" + std::to_string(duration) + ")";
  if (const auto existing = design_.signal_by_name_.find(name);
      existing != design_.signal_by_name_.end()) {
    implicit_signal_dependencies_.push_back(existing->second);
    return existing->second;
  }
  if (design_.signals_.size()
      > std::numeric_limits<SignalId>::max()) {
    report(
        "FSIM-ELAB-011",
        "the design has too many signals for an implicit VHDL attribute",
        span);
    return std::nullopt;
  }

  const auto derived = static_cast<SignalId>(design_.signals_.size());
  const bool boolean = attribute != "delayed";
  SignalInfo info;
  info.id = derived;
  info.name = name;
  info.width = boolean ? 1 : source_info.width;
  info.type_name = boolean ? "boolean" : source_info.type_name;
  info.source_domain =
      boolean ? frontend::ValueDomain::Boolean : source_info.source_domain;
  info.is_signed = !boolean && source_info.is_signed;
  if (!boolean) {
    info.packed_range = source_info.packed_range;
    info.vhdl_array = source_info.vhdl_array;
    info.vhdl_access = source_info.vhdl_access;
    info.vhdl_physical = source_info.vhdl_physical;
    info.packed_members = source_info.packed_members;
    info.integer_range = source_info.integer_range;
    info.nominal_type = source_info.nominal_type;
    info.enumeration_literals = source_info.enumeration_literals;
    info.enumeration_range = source_info.enumeration_range;
  }
  info.declaration_span = span;
  design_.signal_info_.push_back(std::move(info));

  auto signal = boolean
      ? Signal{
            name,
            PackedLogic4{
                1,
                attribute == "transaction"
                    ? Logic4::zero
                    : Logic4::one},
            ResolutionKind::none,
            ValueKind::logic4}
      : design_.signals_[source];
  signal.name = name;
  signal.resolution = ResolutionKind::none;
  design_.signals_.push_back(std::move(signal));
  design_.signal_by_name_.emplace(name, derived);

  Process driver;
  const auto process_index = design_.processes_.size()
      + 1 + generated_processes_.size();
  if (process_index > std::numeric_limits<ProcessId>::max()) {
    report(
        "FSIM-ELAB-VHATTR-008",
        "the design has too many processes for an implicit VHDL attribute",
        span);
    return std::nullopt;
  }
  driver.id = static_cast<ProcessId>(process_index);
  driver.name = name + ".$implicit_driver";
  driver.initialize = false;
  driver.static_sensitivity.push_back({
      source,
      attribute == "transaction" || attribute == "quiet"
          ? EdgeKind::transaction
          : EdgeKind::any});
  driver.driver_regions.push_back(
      {derived, 0, static_cast<std::uint32_t>(source_info.width), true});

  if (attribute == "transaction") {
    driver.register_count = 2;
    driver.register_value_kinds.assign(2, ValueKind::logic4);
    driver.operations.emplace_back(ReadSignal{0, derived});
    driver.operations.emplace_back(UnaryNot{1, 0});
    driver.operations.emplace_back(WriteUpdate{derived, 1});
  } else if (attribute == "delayed") {
    driver.register_count = 1;
    driver.register_value_kinds.push_back(
        design_.signals_[source].value_kind);
    driver.operations.emplace_back(ReadSignal{0, source});
    driver.operations.emplace_back(WriteProjected{
        derived, 0, duration, 0, ProjectedDelayMode::transport});
  } else {
    driver.register_count = 2;
    driver.register_value_kinds.assign(2, ValueKind::logic4);
    driver.operations.emplace_back(
        LoadConstant{0, PackedLogic4{1, Logic4::zero}});
    driver.operations.emplace_back(
        LoadConstant{1, PackedLogic4{1, Logic4::one}});
    driver.operations.emplace_back(WriteProjectedWaveform{
        derived,
        {{0, 0}, {1, duration}},
        0,
        ProjectedDelayMode::transport});
  }
  driver.operations.emplace_back(WaitSensitivity{});
  driver.operations.emplace_back(Jump{0});
  generated_processes_.push_back(std::move(driver));
  implicit_signal_dependencies_.push_back(derived);
  return derived;
}

void Lowerer::validate_vhdl_driver_attributes(
    const frontend::SourceSpan span) {
  for (const auto& operation : process_.operations) {
    const auto* query = operation_get_if<SignalDrivingValue>(&operation);
    if (query == nullptr) {
      continue;
    }
    const auto owns_driver = std::ranges::any_of(
        process_.driver_regions,
        [&](const Process::DriverRegion& region) {
          return region.signal == query->signal;
        });
    if (!owns_driver) {
      report(
          "FSIM-ELAB-VHATTR-003",
          "'driving_value requires the calling process to own a driver "
          "for the prefix signal",
          span);
    }
  }
}

}  // namespace fsim::elaboration
