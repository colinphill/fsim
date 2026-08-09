// SPDX-License-Identifier: Apache-2.0
#include "application_test_uvm_virtual.hpp"
#include "fsim/app/design_artifact.hpp"

#include <array>
#include <optional>
#include <ranges>
#include <utility>

namespace fsim::test {

ApplicationVirtualSequenceProbe::ApplicationVirtualSequenceProbe(
    app::Simulation &simulation,
    const runtime::SystemVerilogClassHandle virtual_component,
    const runtime::SystemVerilogUvmSequencerHandle domain_sequencer,
    const runtime::SystemVerilogClassHandle virtual_sequence_object,
    const runtime::SystemVerilogClassHandle domain_sequence_object,
    std::string component_nominal_type, std::string sequence_nominal_type,
    runtime::SystemVerilogUvmSequenceProfile profile)
    : simulation_(&simulation), sequences_(&simulation.uvm_sequences()),
      callbacks_(&simulation.uvm_callbacks()),
      transactions_(&simulation.uvm_transactions()),
      register_model_(&simulation.uvm_register_model()),
      transaction_object_(virtual_sequence_object),
      transaction_root_(simulation.uvm_components().root_of(virtual_component)),
      domain_sequencer_(domain_sequencer), request_type_(profile.request_type),
      response_type_(profile.response_type) {
  register_block_ = register_model_->create_block(
      {transaction_root_, std::nullopt, "api_register_model"});
  register_map_ = register_model_->create_map(
      {register_block_, "bus", 0x1000, 4,
       runtime::SystemVerilogUvmRegisterMapEndianness::Little, true});
  status_register_ =
      register_model_->create_register({register_block_, "status", 32, 0x10});
  ready_field_ = register_model_->create_field(
      {status_register_, "ready", 1, 0});
  register_model_->set_reset(
      ready_field_, "HARD",
      runtime::PackedLogic4{1, runtime::Logic4::zero});
  samples_memory_ = register_model_->create_memory(
      {register_block_, "samples", 32, 0x100, {4, 8}});
  user_register_ =
      register_model_->create_register({register_block_, "user", 8, 0x20});
  hdl_register_ =
      register_model_->create_register({register_block_, "hdl", 8, 0x24});
  hdl_field_ = register_model_->create_field({hdl_register_, "value", 8, 0});
  register_model_->add_register(register_map_, status_register_, 0x10);
  register_model_->add_memory(register_map_, samples_memory_, 0x100);
  register_model_->lock_model(register_block_);
  register_coverage_ = register_model_->register_coverage_model(
      {register_block_, "api_register_coverage", true, true, true});
  const auto add_register_callback =
      [&](const runtime::SystemVerilogUvmRegisterCallbackScope scope,
          const std::string_view name) {
        runtime::SystemVerilogUvmRegisterCallbackDescriptor descriptor;
        descriptor.scope = scope;
        descriptor.name = std::string{name};
        descriptor.callback = [](auto &) {};
        switch (scope) {
        case runtime::SystemVerilogUvmRegisterCallbackScope::Block:
          descriptor.block = register_block_;
          break;
        case runtime::SystemVerilogUvmRegisterCallbackScope::Map:
          descriptor.map = register_map_;
          break;
        case runtime::SystemVerilogUvmRegisterCallbackScope::Register:
          descriptor.reg = status_register_;
          break;
        case runtime::SystemVerilogUvmRegisterCallbackScope::Field:
          descriptor.field = ready_field_;
          break;
        case runtime::SystemVerilogUvmRegisterCallbackScope::Memory:
          descriptor.memory = samples_memory_;
          break;
        }
        register_callbacks_.push_back(
            register_model_->register_callback(std::move(descriptor)));
      };
  add_register_callback(runtime::SystemVerilogUvmRegisterCallbackScope::Block,
                        "api_block_callback");
  add_register_callback(runtime::SystemVerilogUvmRegisterCallbackScope::Map,
                        "api_map_callback");
  add_register_callback(runtime::SystemVerilogUvmRegisterCallbackScope::Register,
                        "api_register_callback");
  add_register_callback(runtime::SystemVerilogUvmRegisterCallbackScope::Field,
                        "api_field_callback");
  const auto callback_target_type =
      simulation.uvm_objects().type_name(virtual_sequence_object);
  (void)callbacks_->add(
      {callback_target_type, "uvm_transaction_callback", "api_type_callback", 0,
       UINT64_C(7), runtime::SystemVerilogUvmCallbackOrdering::Prepend,
       [this](auto &invocation) {
         callback_order_.push_back("type:" + invocation.operation);
         if (invocation.operation == "begin")
           invocation.attributes["engine_neutral"] = "true";
       }});
  (void)callbacks_->add(
      {callback_target_type, "uvm_transaction_callback",
       "api_instance_callback", virtual_sequence_object, UINT64_C(7),
       runtime::SystemVerilogUvmCallbackOrdering::Append,
       [this](auto &invocation) {
         callback_order_.push_back("instance:" + invocation.operation);
       }});
  runtime::SystemVerilogUvmVirtualSequencerDescriptor virtual_sequencer;
  virtual_sequencer.component = virtual_component;
  virtual_sequencer.nominal_type = std::move(component_nominal_type);
  virtual_sequencer.profile = profile;
  virtual_sequencer.domains = {{"main", domain_sequencer_, profile}};
  virtual_sequencer_ =
      sequences_->register_virtual_sequencer(std::move(virtual_sequencer));

  runtime::SystemVerilogUvmSequenceDescriptor domain_sequence;
  domain_sequence.object = domain_sequence_object;
  domain_sequence.name = "api_domain_sequence";
  domain_sequence.nominal_type = sequence_nominal_type;
  domain_sequence.profile = profile;
  domain_sequence.sequencer = domain_sequencer_;
  domain_sequence.hooks.body = [this](const auto child) {
    const auto snapshot = sequences_->snapshot(child);
    child_process_ = snapshot.process;
    const auto acquired = sequences_->get_next_item(
        domain_sequencer_, {snapshot.phase, snapshot.process, 0});
    if (!acquired.transaction)
      return;
    priority_ = acquired.transaction->request.priority;
    sequences_->item_done(acquired.transaction->handle);
  };
  domain_sequence_ = sequences_->register_sequence(std::move(domain_sequence));

  runtime::SystemVerilogUvmSequenceDescriptor virtual_sequence;
  virtual_sequence.object = virtual_sequence_object;
  virtual_sequence.name = "api_virtual_sequence";
  virtual_sequence.nominal_type = std::move(sequence_nominal_type);
  virtual_sequence.profile = std::move(profile);
  virtual_sequence.sequencer = virtual_sequencer_;
  virtual_sequence.hooks.body = [this](const auto handle) {
    const std::array steps{runtime::SystemVerilogUvmVirtualSequenceStep{
        "main", domain_sequence_, 161,
        runtime::SystemVerilogUvmVirtualSequenceAccess::Lock, true, true}};
    virtual_result_ = sequences_->coordinate_virtual(handle, steps);
  };
  virtual_sequence_ =
      sequences_->register_virtual_sequence(std::move(virtual_sequence));

  runtime::SystemVerilogUvmRegisterAdapterDescriptor adapter;
  adapter.root = transaction_root_;
  adapter.sequencer = domain_sequencer_;
  adapter.sequence = domain_sequence_;
  adapter.name = "api_register_adapter";
  adapter.auto_predict = true;
  adapter.reg_to_bus = [this](const auto &) {
    const auto name =
        "api_frontdoor_request_" + std::to_string(next_frontdoor_item_++);
    return sequences_->macro_create(
        {transaction_object_, name, request_type_,
         runtime::SystemVerilogUvmSequenceItemRole::Request, domain_sequence_,
         domain_sequencer_});
  };
  adapter.drive = [this](const auto &bus, const auto &)
      -> std::optional<runtime::SystemVerilogUvmSequenceItemHandle> {
    frontdoor_bus_items_.push_back(bus);
    runtime::SystemVerilogUvmRegisterBusResponse converted;
    if (bus.kind == runtime::SystemVerilogUvmRegisterBusOperationKind::Write) {
      frontdoor_hardware_[bus.address] = bus.data;
      converted.data = bus.data;
    } else {
      const auto found = frontdoor_hardware_.find(bus.address);
      converted.data =
          found == frontdoor_hardware_.end()
              ? runtime::PackedLogic4{bus.data.width(), runtime::Logic4::zero}
              : found->second;
    }
    const auto name =
        "api_frontdoor_response_" + std::to_string(next_frontdoor_item_++);
    const auto response = sequences_->macro_create(
        {transaction_object_, name, response_type_,
         runtime::SystemVerilogUvmSequenceItemRole::Response, domain_sequence_,
         domain_sequencer_});
    frontdoor_responses_.emplace_back(response, std::move(converted));
    return response;
  };
  adapter.bus_to_reg = [this](const auto response) {
    const auto found = std::ranges::find_if(
        frontdoor_responses_, [response](const auto &entry) {
          return entry.first == response;
        });
    if (found == frontdoor_responses_.end())
      throw std::runtime_error{"missing application bus response"};
    return found->second;
  };
  register_adapter_ = register_model_->register_adapter(std::move(adapter));
  user_frontdoor_ = register_model_->register_user_frontdoor(
      {transaction_root_,
       "api_user_frontdoor",
       {user_register_, std::nullopt, std::nullopt, 0},
       [this](const auto &) {
         return runtime::SystemVerilogUvmRegisterOperationResult{
             runtime::SystemVerilogUvmRegisterOperationStatus::IsOk,
             user_frontdoor_value_, false, {}};
       },
       [this](const auto &value, const auto &) {
         user_frontdoor_value_ = value;
         return runtime::SystemVerilogUvmRegisterOperationResult{
             runtime::SystemVerilogUvmRegisterOperationStatus::IsOk,
             runtime::PackedLogic4{0}, true, {}};
       }});
  hdl_path_ = register_model_->register_hdl_path(
      {transaction_root_,
       "RTL",
       {hdl_register_, std::nullopt, std::nullopt, 0},
       {{runtime::SystemVerilogUvmRegisterHdlKind::Vpi,
         "class_top.source_property", 0, 0, 8}}});
}

runtime::SystemVerilogUvmTaskPhaseStatus
ApplicationVirtualSequenceProbe::dispatch(
    const runtime::SystemVerilogUvmPhaseHandle phase,
    const runtime::SystemVerilogUvmPhaseProcessHandle process) {
  transaction_ = transactions_->begin({"api_virtual_dispatch",
                                       "api_sequence",
                                       "virtual",
                                       transaction_root_,
                                       transaction_object_,
                                       std::nullopt,
                                       {{"priority", std::uint64_t{161}}}});
  outer_result_ =
      sequences_->start(virtual_sequence_, {phase, process, true, true});
  register_model_->reset(register_block_, "HARD");
  const auto field_write = register_model_->write(
      ready_field_, runtime::PackedLogic4{1, runtime::Logic4::one});
  const std::array memory_values{
      runtime::PackedLogic4::from_aval_bval(32, UINT64_C(0xa5), 0)};
  const auto memory_write = register_model_->write_burst(
      register_map_, samples_memory_, 19, memory_values);
  const auto memory_read =
      register_model_->read_burst(register_map_, samples_memory_, 19, 1);
  const auto register_beats =
      register_model_->register_accesses(register_map_, status_register_);
  const auto register_lookup =
      register_model_->lookup(register_map_, 0x1010, true);
  const auto memory_lookup =
      register_model_->lookup(register_map_, 0x114c, false);
  register_operations_ok_ = field_write.success() && memory_write.success() &&
                            memory_read.success();
  register_map_ok_ = register_beats.size() == 1 &&
                     register_beats.front().address == 0x1010 &&
                     register_lookup &&
                     register_lookup->register_handle == status_register_ &&
                     memory_lookup && memory_lookup->memory == samples_memory_ &&
                     memory_lookup->memory_word_index == 19;
  const runtime::SystemVerilogUvmRegisterFrontdoorOptions frontdoor_options{
      phase, process, 16, true};
  frontdoor_write_ = register_model_->frontdoor_write(
      register_adapter_, register_map_, status_register_,
      runtime::PackedLogic4::from_aval_bval(32, 1, 0), frontdoor_options);
  frontdoor_hardware_[0x1010] =
      runtime::PackedLogic4::from_aval_bval(32, 0, 0);
  frontdoor_mirror_ = register_model_->frontdoor_mirror(
      register_adapter_, register_map_, status_register_, frontdoor_options);
  register_model_->set(status_register_,
                       runtime::PackedLogic4::from_aval_bval(32, 1, 0));
  frontdoor_update_ = register_model_->frontdoor_update(
      register_adapter_, register_map_, status_register_, frontdoor_options);
  frontdoor_hardware_[0x114c] =
      runtime::PackedLogic4::from_aval_bval(32, UINT64_C(0xa5), 0);
  frontdoor_memory_read_ = register_model_->frontdoor_read(
      register_adapter_, register_map_, samples_memory_, 19,
      frontdoor_options);
  register_frontdoor_ok_ =
      frontdoor_write_.state ==
          runtime::SystemVerilogUvmRegisterFrontdoorState::Completed &&
      frontdoor_mirror_.state ==
          runtime::SystemVerilogUvmRegisterFrontdoorState::Completed &&
      frontdoor_mirror_.mismatch &&
      frontdoor_update_.state ==
          runtime::SystemVerilogUvmRegisterFrontdoorState::Completed &&
      frontdoor_memory_read_.state ==
          runtime::SystemVerilogUvmRegisterFrontdoorState::Completed &&
      frontdoor_memory_read_.result.value.to_msb_string() ==
          "00000000000000000000000010100101" &&
      frontdoor_bus_items_.size() == 4;
  const auto user_write = register_model_->user_frontdoor_write(
      user_frontdoor_,
      runtime::PackedLogic4::from_aval_bval(8, UINT64_C(0xc3), 0),
      frontdoor_options);
  const auto user_read =
      register_model_->user_frontdoor_read(user_frontdoor_, frontdoor_options);
  const auto hdl_read = register_model_->backdoor_read(hdl_path_);
  register_backdoor_ok_ =
      user_write.success() && user_read.success() &&
      hdl_read.status !=
          runtime::SystemVerilogUvmRegisterOperationStatus::NotOk &&
      user_read.value.low_word().aval == 0xc3 && hdl_read.value.width() == 8;
  runtime::SystemVerilogUvmRegisterStandardSequenceOptions sequence_options;
  sequence_options.kind =
      runtime::SystemVerilogUvmRegisterStandardSequenceKind::Access;
  sequence_options.top = register_block_;
  sequence_options.map = register_map_;
  sequence_options.seed = 161;
  register_standard_sequence_ =
      register_model_->run_standard_sequence(sequence_options);
  register_standard_sequence_ok_ = register_standard_sequence_.success() &&
                                   register_standard_sequence_.operations == 3 &&
                                   register_standard_sequence_.reads == 1 &&
                                   register_standard_sequence_.writes == 2;
  register_value_signature_ =
      register_model_->get(status_register_).to_msb_string() + ":" +
      register_model_->get_mirrored(status_register_).to_msb_string() + ":" +
      memory_read.values.front().to_msb_string();
  const auto map_snapshot = register_model_->snapshot(register_map_);
  register_map_signature_ =
      std::to_string(map_snapshot.base_offset) + ":" +
      std::to_string(map_snapshot.bus_width_bytes) + ":" +
      std::to_string(static_cast<std::uint8_t>(map_snapshot.endianness)) + ":" +
      std::to_string(register_model_->mapped_registers(register_map_).size()) +
      ":" +
      std::to_string(register_model_->mapped_memories(register_map_).size());
  register_frontdoor_signature_ =
      std::to_string(register_model_->adapter_snapshot(register_adapter_)
                         .registration_order) +
      ":" +
      std::to_string(register_model_->frontdoor_operations().size()) + ":" +
      std::to_string(frontdoor_bus_items_.size()) + ":" +
      frontdoor_memory_read_.result.value.to_msb_string();
  register_backdoor_signature_ =
      std::to_string(register_model_->user_frontdoor_snapshot(user_frontdoor_)
                         .registration_order) +
      ":" +
      std::to_string(
          register_model_->hdl_path_snapshot(hdl_path_).registration_order) +
      ":" + user_read.value.to_msb_string() + ":" +
      hdl_read.value.to_msb_string();
  register_standard_sequence_signature_ =
      std::to_string(register_standard_sequence_.execution_order) + ":" +
      std::to_string(register_standard_sequence_.final_random_state) + ":" +
      std::to_string(register_standard_sequence_.operations) + ":" +
      std::to_string(register_standard_sequence_.failures.size()) + ":" +
      std::to_string(register_model_->callback_snapshot(register_callbacks_.front())
                         .invocations) +
      ":" +
      std::to_string(
          register_model_->coverage_snapshot(register_coverage_).bins.size());
  transactions_->record_attribute(
      transaction_,
      {"children", std::uint64_t{virtual_result_.children.size()}});
  transactions_->end(transaction_);
  return runtime::SystemVerilogUvmTaskPhaseStatus::Completed;
}

bool ApplicationVirtualSequenceProbe::verify(
    const std::span<const runtime::SystemVerilogUvmPhaseProcessSnapshot>
        final_processes) const {
  const auto virtual_process = sequences_->snapshot(virtual_sequence_).process;
  const auto virtual_snapshot =
      std::ranges::find(final_processes, virtual_process,
                        &runtime::SystemVerilogUvmPhaseProcessSnapshot::handle);
  const auto child_snapshot =
      std::ranges::find(final_processes, child_process_,
                        &runtime::SystemVerilogUvmPhaseProcessSnapshot::handle);
  const auto transaction = transactions_->snapshot(transaction_);
  const auto debug = simulation_->uvm_debug_snapshot();
  const auto register_block = register_model_->snapshot(register_block_);
  const auto register_declarations = register_model_->declarations();
  const auto register_value = register_model_->value_snapshot(ready_field_);
  const auto callback_text =
      app::format_uvm_debug_snapshot(debug, app::UvmDebugSection::callbacks);
  const auto transaction_text =
      app::format_uvm_debug_snapshot(debug, app::UvmDebugSection::transactions);
  const auto sequence_text =
      app::format_uvm_debug_snapshot(debug, app::UvmDebugSection::sequences);
  const auto register_text = app::format_uvm_debug_snapshot(
      debug, app::UvmDebugSection::register_model);
  const auto checkpoint = simulation_->capture_uvm_checkpoint();
  diagnostic::Engine diagnostics;
  const auto encoded = checkpoint
                           ? app::serialize_systemverilog_uvm_state(
                                 checkpoint.artifact, diagnostics)
                           : std::nullopt;
  const auto decoded = encoded
                           ? app::deserialize_systemverilog_uvm_state(
                                 *encoded, "virtual-uvm-state", diagnostics)
                           : std::nullopt;
  runtime::SystemVerilogUvmCheckpointLimits bounded_limits;
  bounded_limits.maximum_records =
      checkpoint && !checkpoint.artifact.records.empty()
          ? checkpoint.artifact.records.size() - 1U
          : 1U;
  const auto bounded_checkpoint =
      simulation_->capture_uvm_checkpoint(bounded_limits);
  return outer_result_.success() && virtual_result_.success() &&
         virtual_result_.children.size() == 1 && priority_ == 161 &&
         sequences_->requests(domain_sequencer_).empty() &&
         sequences_->access_requests(domain_sequencer_).empty() &&
         virtual_snapshot != final_processes.end() &&
         child_snapshot != final_processes.end() &&
         callback_order_ ==
             std::vector<std::string>{"type:begin",     "instance:begin",
                                      "type:attribute", "instance:attribute",
                                      "type:end",       "instance:end"} &&
         transaction.state ==
             runtime::SystemVerilogUvmTransactionState::Completed &&
         transaction.attributes.size() == 3 && debug.callbacks.size() == 2 &&
         register_block.state ==
             runtime::SystemVerilogUvmRegisterModelState::Locked &&
         register_block.full_name.find("api_register_model") !=
             std::string::npos &&
         register_declarations.size() == 8 &&
         register_declarations.front().identity == 1 &&
         register_declarations.back().declaration_order == 8 &&
         register_operations_ok_ && register_map_ok_ &&
         register_frontdoor_ok_ &&
         register_backdoor_ok_ &&
         register_standard_sequence_ok_ &&
         register_model_->user_frontdoors().size() == 1 &&
         register_model_->hdl_paths().size() == 1 &&
         register_model_->adapters().size() == 1 &&
         register_model_->frontdoor_operations().size() == 4 &&
         register_model_->standard_sequences().size() == 1 &&
         register_model_->register_callbacks().size() == 4 &&
         register_model_->coverage_models().size() == 1 &&
         register_model_->callback_snapshot(register_callbacks_.front())
                 .invocations == 6 &&
         register_model_->coverage_snapshot(register_coverage_).samples == 3 &&
         frontdoor_write_.transactions.size() == 1 &&
         frontdoor_mirror_.transactions.size() == 1 &&
         frontdoor_update_.transactions.size() == 1 &&
         frontdoor_memory_read_.transactions.size() == 1 &&
         frontdoor_bus_items_.front().address == 0x1010 &&
         frontdoor_bus_items_.back().address == 0x114c &&
         register_model_->mapped_registers(register_map_).size() == 1 &&
         register_model_->mapped_memories(register_map_).size() == 1 &&
         register_value.desired.to_msb_string() == "1" &&
         register_value.mirrored.to_msb_string() == "1" &&
         register_value.reset_values.at("HARD").to_msb_string() == "0" &&
         debug.transactions.size() == 1 &&
         debug.transaction_trace_records.size() == 5 &&
         debug.sequencers.size() >= 2 && debug.sequences.size() >= 2 &&
         debug.register_blocks.size() == 1 && debug.register_maps.size() == 1 &&
         debug.registers.size() == 3 && debug.register_fields.size() == 2 &&
         debug.register_memories.size() == 1 &&
         debug.register_sequences.size() == 1 &&
         debug.register_callbacks.size() == 4 &&
         debug.register_coverage.size() == 1 &&
         callback_text.find("api_type_callback") != std::string::npos &&
         transaction_text.find("api_virtual_dispatch") != std::string::npos &&
         sequence_text.find("api_virtual_sequence") != std::string::npos &&
         register_text.find("api_register_model.status.ready") !=
             std::string::npos &&
         checkpoint && encoded && decoded && !diagnostics.has_error() &&
         *decoded == checkpoint.artifact &&
         !bounded_checkpoint &&
         bounded_checkpoint.error ==
             runtime::SystemVerilogUvmCheckpointError::ResourceLimit &&
         std::ranges::any_of(checkpoint.artifact.records,
                             [](const auto &record) {
                               return record.kind ==
                                          FSIM_UVM_FOREIGN_SEQUENCE &&
                                      record.identity.find(
                                          "api_virtual_sequence") !=
                                          std::string::npos;
                             }) &&
         std::ranges::any_of(checkpoint.artifact.records,
                             [](const auto &record) {
                               return record.kind ==
                                          FSIM_UVM_FOREIGN_REGISTER_FIELD &&
                                      record.identity.find(
                                          "api_register_model.status.ready") !=
                                          std::string::npos &&
                                      record.detail.find("mirrored=") !=
                                          std::string::npos;
                             }) &&
         std::ranges::any_of(checkpoint.artifact.records,
                             [](const auto &record) {
                               return record.kind ==
                                          FSIM_UVM_FOREIGN_REGISTER_SEQUENCE &&
                                      record.auxiliary != 0;
                             }) &&
         std::ranges::any_of(
             simulation_->uvm_activity().events(), [](const auto &event) {
               return event.kind ==
                          runtime::SystemVerilogUvmActivityKind::Sequence &&
                      event.action ==
                          runtime::SystemVerilogUvmActivityAction::Completed;
             }) &&
         std::ranges::any_of(
             simulation_->uvm_activity().events(), [](const auto &event) {
               return event.kind ==
                          runtime::SystemVerilogUvmActivityKind::RegisterModel &&
                      event.detail.starts_with("callback-");
             }) &&
         child_snapshot->parent ==
             std::optional<runtime::SystemVerilogUvmPhaseProcessHandle>{
                 virtual_process};
}

std::uint64_t
ApplicationVirtualSequenceProbe::trace_signature() const noexcept {
  std::uint64_t result{UINT64_C(14695981039346656037)};
  const auto mix = [&](const std::string_view text) {
    for (const auto byte : text) {
      result ^= static_cast<unsigned char>(byte);
      result *= UINT64_C(1099511628211);
    }
  };
  for (const auto &record : transactions_->trace_records()) {
    result ^= record.sequence;
    result *= UINT64_C(1099511628211);
    result ^= static_cast<std::uint8_t>(record.kind);
    result *= UINT64_C(1099511628211);
    result ^= record.transaction_identity;
    result *= UINT64_C(1099511628211);
    result ^= record.related_identity.value_or(0);
    result *= UINT64_C(1099511628211);
    result ^= record.root;
    result *= UINT64_C(1099511628211);
    result ^= record.time;
    result *= UINT64_C(1099511628211);
    result ^= record.delta;
    result *= UINT64_C(1099511628211);
    mix(record.name);
    mix(record.value);
  }
  for (const auto &declaration : register_model_->declarations()) {
    result ^= static_cast<std::uint8_t>(declaration.kind);
    result *= UINT64_C(1099511628211);
    result ^= declaration.identity;
    result *= UINT64_C(1099511628211);
    result ^= declaration.owner_identity;
    result *= UINT64_C(1099511628211);
    result ^= declaration.root;
    result *= UINT64_C(1099511628211);
    result ^= declaration.declaration_order;
    result *= UINT64_C(1099511628211);
    mix(declaration.name);
    mix(declaration.full_name);
  }
  mix(register_value_signature_);
  mix(register_map_signature_);
  mix(register_frontdoor_signature_);
  mix(register_backdoor_signature_);
  mix(register_standard_sequence_signature_);
  return result;
}

} // namespace fsim::test
