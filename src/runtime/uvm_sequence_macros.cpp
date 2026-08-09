// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <utility>

namespace fsim::runtime {

SystemVerilogUvmSequenceHandle SystemVerilogUvmSequenceService::macro_create(
    SystemVerilogUvmSequenceDescriptor prototype) {
  const auto created = objects_->create(prototype.object, prototype.name);
  prototype.object = created;
  try {
    return register_sequence(std::move(prototype));
  } catch (...) {
    objects_->erase(created);
    (void)heap_->release(created);
    throw;
  }
}

SystemVerilogUvmSequenceItemHandle
SystemVerilogUvmSequenceService::macro_create(
    SystemVerilogUvmSequenceItemDescriptor prototype) {
  const auto created = objects_->create(prototype.object, prototype.name);
  prototype.object = created;
  try {
    return register_item(std::move(prototype));
  } catch (...) {
    objects_->erase(created);
    (void)heap_->release(created);
    throw;
  }
}

SystemVerilogUvmSequenceRequestHandle
SystemVerilogUvmSequenceService::macro_send(
    const SystemVerilogUvmSequenceHandle sequence,
    const std::uint32_t priority) {
  SystemVerilogUvmSequenceRequestDescriptor request;
  request.sequence = sequence;
  request.priority = priority;
  return enqueue_request(std::move(request));
}

SystemVerilogUvmSequenceRequestHandle
SystemVerilogUvmSequenceService::macro_send(
    const SystemVerilogUvmSequenceItemHandle item_handle,
    const std::uint32_t priority) {
  const auto& selected_item = item(item_handle).value;
  SystemVerilogUvmSequenceRequestDescriptor request;
  request.sequence = selected_item.owner_sequence;
  request.priority = priority;
  request.item = item_handle;
  return enqueue_request(std::move(request));
}

SystemVerilogUvmSequenceMacroResult
SystemVerilogUvmSequenceService::macro_random_send(
    const SystemVerilogUvmSequenceHandle sequence_handle,
    const SystemVerilogClassRandomizeRequest& randomization,
    const std::uint32_t priority) {
  const auto object = sequence(sequence_handle).value.object;
  const auto queued = macro_send(sequence_handle, priority);
  SystemVerilogUvmSequenceMacroResult result;
  try {
    result.randomization = randomize_systemverilog_class_object(
        *heap_, object, randomization);
  } catch (...) {
    rollback_latest_request(queued);
    throw;
  }
  if (result.randomization.language_result() == 0) {
    rollback_latest_request(queued);
    return result;
  }
  result.status = SystemVerilogUvmSequenceMacroStatus::Queued;
  result.request = queued;
  return result;
}

SystemVerilogUvmSequenceMacroResult
SystemVerilogUvmSequenceService::macro_random_send(
    const SystemVerilogUvmSequenceItemHandle item_handle,
    const SystemVerilogClassRandomizeRequest& randomization,
    const std::uint32_t priority) {
  const auto object = item(item_handle).value.object;
  const auto queued = macro_send(item_handle, priority);
  SystemVerilogUvmSequenceMacroResult result;
  try {
    result.randomization = randomize_systemverilog_class_object(
        *heap_, object, randomization);
  } catch (...) {
    rollback_latest_request(queued);
    throw;
  }
  if (result.randomization.language_result() == 0) {
    rollback_latest_request(queued);
    return result;
  }
  result.status = SystemVerilogUvmSequenceMacroStatus::Queued;
  result.request = queued;
  return result;
}

SystemVerilogUvmSequenceMacroResult
SystemVerilogUvmSequenceService::macro_random_start(
    const SystemVerilogUvmSequenceHandle sequence_handle,
    const SystemVerilogClassRandomizeRequest& randomization,
    SystemVerilogUvmSequenceStartOptions options) {
  const auto object = sequence(sequence_handle).value.object;
  SystemVerilogUvmSequenceMacroResult result;
  result.randomization = randomize_systemverilog_class_object(
      *heap_, object, randomization);
  if (result.randomization.language_result() == 0) return result;
  result.execution = start(sequence_handle, std::move(options));
  result.status = SystemVerilogUvmSequenceMacroStatus::Started;
  return result;
}

}  // namespace fsim::runtime
