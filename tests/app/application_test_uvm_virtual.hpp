// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fsim::test {

class ApplicationVirtualSequenceProbe final {
public:
  ApplicationVirtualSequenceProbe(
      app::Simulation &simulation,
      runtime::SystemVerilogClassHandle virtual_component,
      runtime::SystemVerilogUvmSequencerHandle domain_sequencer,
      runtime::SystemVerilogClassHandle virtual_sequence_object,
      runtime::SystemVerilogClassHandle domain_sequence_object,
      std::string component_nominal_type, std::string sequence_nominal_type,
      runtime::SystemVerilogUvmSequenceProfile profile);

  [[nodiscard]] runtime::SystemVerilogUvmTaskPhaseStatus
  dispatch(runtime::SystemVerilogUvmPhaseHandle phase,
           runtime::SystemVerilogUvmPhaseProcessHandle process);
  [[nodiscard]] bool
  verify(std::span<const runtime::SystemVerilogUvmPhaseProcessSnapshot>
             final_processes) const;
  [[nodiscard]] std::uint64_t trace_signature() const noexcept;

private:
  app::Simulation *simulation_{};
  runtime::SystemVerilogUvmSequenceService *sequences_{};
  runtime::SystemVerilogUvmCallbackService *callbacks_{};
  runtime::SystemVerilogUvmTransactionRecorderService *transactions_{};
  runtime::SystemVerilogUvmRegisterModelService *register_model_{};
  runtime::SystemVerilogClassHandle transaction_object_{};
  runtime::SystemVerilogUvmRootHandle transaction_root_{};
  runtime::SystemVerilogUvmSequencerHandle domain_sequencer_;
  runtime::SystemVerilogUvmSequencerHandle virtual_sequencer_;
  runtime::SystemVerilogUvmSequenceHandle domain_sequence_;
  runtime::SystemVerilogUvmSequenceHandle virtual_sequence_;
  runtime::SystemVerilogUvmVirtualSequenceResult virtual_result_;
  runtime::SystemVerilogUvmSequenceExecutionResult outer_result_;
  runtime::SystemVerilogUvmPhaseProcessHandle child_process_;
  runtime::SystemVerilogUvmTransactionHandle transaction_;
  runtime::SystemVerilogUvmRegisterBlockHandle register_block_;
  runtime::SystemVerilogUvmRegisterMapHandle register_map_;
  runtime::SystemVerilogUvmRegisterHandle status_register_;
  runtime::SystemVerilogUvmRegisterFieldHandle ready_field_;
  runtime::SystemVerilogUvmRegisterMemoryHandle samples_memory_;
  runtime::SystemVerilogUvmRegisterHandle user_register_;
  runtime::SystemVerilogUvmRegisterHandle hdl_register_;
  runtime::SystemVerilogUvmRegisterFieldHandle hdl_field_;
  runtime::SystemVerilogUvmRegisterAdapterHandle register_adapter_;
  runtime::SystemVerilogUvmRegisterUserFrontdoorHandle user_frontdoor_;
  runtime::SystemVerilogUvmRegisterHdlPathHandle hdl_path_;
  runtime::SystemVerilogUvmRegisterFrontdoorSnapshot frontdoor_write_;
  runtime::SystemVerilogUvmRegisterFrontdoorSnapshot frontdoor_mirror_;
  runtime::SystemVerilogUvmRegisterFrontdoorSnapshot frontdoor_update_;
  runtime::SystemVerilogUvmRegisterFrontdoorSnapshot frontdoor_memory_read_;
  runtime::SystemVerilogUvmRegisterStandardSequenceSnapshot
      register_standard_sequence_;
  std::vector<runtime::SystemVerilogUvmRegisterCallbackHandle>
      register_callbacks_;
  runtime::SystemVerilogUvmRegisterCoverageHandle register_coverage_;
  std::vector<std::pair<runtime::SystemVerilogUvmSequenceItemHandle,
                        runtime::SystemVerilogUvmRegisterBusResponse>>
      frontdoor_responses_;
  std::map<std::uint64_t, runtime::PackedLogic4> frontdoor_hardware_;
  std::vector<runtime::SystemVerilogUvmRegisterBusItem> frontdoor_bus_items_;
  std::vector<std::string> callback_order_;
  std::string request_type_;
  std::string response_type_;
  std::string register_value_signature_;
  std::string register_map_signature_;
  std::string register_frontdoor_signature_;
  std::string register_backdoor_signature_;
  std::string register_standard_sequence_signature_;
  bool register_operations_ok_{};
  bool register_map_ok_{};
  bool register_frontdoor_ok_{};
  bool register_backdoor_ok_{};
  bool register_standard_sequence_ok_{};
  runtime::PackedLogic4 user_frontdoor_value_{8, runtime::Logic4::zero};
  std::uint64_t next_frontdoor_item_{};
  std::uint32_t priority_{};
};

} // namespace fsim::test
