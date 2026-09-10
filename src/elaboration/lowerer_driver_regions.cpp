// SPDX-License-Identifier: Apache-2.0
#include "lowerer_driver_regions.hpp"

#include <limits>
#include <type_traits>

namespace fsim::elaboration::elaboration_detail {

std::vector<runtime::simir::Process::DriverRegion>
collect_driver_regions(
    const runtime::simir::Process& process,
    const std::span<const std::size_t> register_widths) {
  using namespace runtime::simir;
  std::vector<Process::DriverRegion> result;
  const auto whole = [&](const SignalId signal) {
    result.push_back(Process::DriverRegion{
        signal, 0, 0, true});
  };
  const auto slice = [&](const SignalId signal,
                         const RegisterId source,
                         const std::uint32_t offset) {
    if (source >= register_widths.size()
        || register_widths[source]
            > std::numeric_limits<std::uint32_t>::max()) {
      whole(signal);
      return;
    }
    result.push_back(Process::DriverRegion{
        signal,
        offset,
        static_cast<std::uint32_t>(register_widths[source]),
        false});
  };
  for (const auto& operation : process.operations) {
    visit_operation(
        [&](const auto& value) {
          using OperationType = std::decay_t<decltype(value)>;
          if constexpr (
              std::is_same_v<OperationType, WriteBlocking>
              || std::is_same_v<OperationType, WriteUpdate>
              || std::is_same_v<OperationType, WriteAfter>
              || std::is_same_v<OperationType, WriteInertial>
              || std::is_same_v<OperationType, WriteProjected>
              || std::is_same_v<OperationType, WriteProjectedWaveform>
              || std::is_same_v<
                  OperationType, WriteBlockingDynamicSlice>
              || std::is_same_v<
                  OperationType, WriteUpdateDynamicSlice>
              || std::is_same_v<
                  OperationType, WriteAfterDynamicSlice>
              || std::is_same_v<
                  OperationType, WriteBlockingDynamicPartSlice>
              || std::is_same_v<
                  OperationType, WriteUpdateDynamicPartSlice>
              || std::is_same_v<
                  OperationType, WriteAfterDynamicPartSlice>
              || std::is_same_v<
                  OperationType, WriteInertialDynamicSlice>
              || std::is_same_v<
                  OperationType, WriteInertialDynamicPartSlice>
              || std::is_same_v<
                  OperationType, WriteProjectedDynamicSlice>
              || std::is_same_v<
                  OperationType, WriteProjectedWaveformDynamicSlice>) {
              whole(value.signal);
          } else if constexpr (
              std::is_same_v<OperationType, VitalTimingCheck>) {
              if (value.trigger_signal)
                  whole(*value.trigger_signal);
          } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
              whole(value.output);
          } else if constexpr (
              std::is_same_v<OperationType, WriteBlockingSlice>
              || std::is_same_v<OperationType, WriteUpdateSlice>
              || std::is_same_v<OperationType, WriteAfterSlice>
              || std::is_same_v<OperationType, WriteInertialSlice>
              || std::is_same_v<OperationType, WriteProjectedSlice>) {
              slice(value.signal, value.source, value.offset);
          } else if constexpr (std::is_same_v<
                                   OperationType,
                                   WriteProjectedWaveformSlice>) {
              if (value.elements.empty()) {
                  whole(value.signal);
              } else {
                  slice(
                      value.signal,
                      value.elements.front().source,
                      value.offset);
              }
          }
        },
        operation);
  }
  return result;
}
} // namespace fsim::elaboration::elaboration_detail
