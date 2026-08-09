// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <limits>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidFrontdoor{"FSIM-UVM-REG-007"};
constexpr std::string_view kFrontdoorLimit{"FSIM-UVM-REG-008"};

[[noreturn]] void fail_frontdoor_api(const std::string_view code,
                                     std::string message) {
  throw SystemVerilogUvmRegisterModelError{std::string{code},
                                           std::move(message)};
}

[[nodiscard]] std::string current_exception_message() {
  try {
    throw;
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return "unknown UVM register frontdoor callback failure";
  }
}

[[nodiscard]] PackedLogic4 extract_bus_data(const PackedLogic4 &value,
                                            const std::size_t byte_offset,
                                            const std::size_t byte_count) {
  const auto bit_offset = byte_offset * 8U;
  const auto bit_count =
      std::min(byte_count * 8U, value.width() - bit_offset);
  PackedLogic4 result{bit_count, Logic4::zero};
  for (std::size_t bit = 0; bit < bit_count; ++bit)
    result.set(bit, value.get(bit_offset + bit));
  return result;
}

void insert_bus_data(PackedLogic4 &target, const PackedLogic4 &value,
                     const std::size_t byte_offset,
                     const std::span<const std::uint8_t> byte_enables) {
  const auto bit_offset = byte_offset * 8U;
  const auto available = target.width() - bit_offset;
  const auto bit_count = std::min(value.width(), available);
  for (std::size_t bit = 0; bit < bit_count; ++bit) {
    if (bit / 8U < byte_enables.size() && byte_enables[bit / 8U] != 0)
      target.set(bit_offset + bit, value.get(bit));
  }
}

[[nodiscard]] bool terminal(
    const SystemVerilogUvmRegisterFrontdoorState state) noexcept {
  return state != SystemVerilogUvmRegisterFrontdoorState::Pending;
}

[[nodiscard]] bool has_unknown_value(const PackedLogic4 &value) {
  if (value.is_logic9())
    return true;
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    const auto scalar = value.get(bit);
    if (scalar != Logic4::zero && scalar != Logic4::one)
      return true;
  }
  return false;
}

} // namespace

void SystemVerilogUvmRegisterModelService::set_frontdoor_services(
    SystemVerilogUvmSequenceService &sequences, SystemVerilogUvmTlm1Service &tlm1,
    SystemVerilogUvmPhaseService &phases) noexcept {
  sequences_ = &sequences;
  tlm1_ = &tlm1;
  phases_ = &phases;
}

void SystemVerilogUvmRegisterModelService::require_frontdoor_services() const {
  if (!sequences_ || !tlm1_ || !phases_)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor services are not configured");
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterAdapterHandle handle) const noexcept {
  const auto found = adapters_.find(handle.slot_);
  return handle.owner_ == owner_ && found != adapters_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterPredictorHandle handle) const noexcept {
  const auto found = predictors_.find(handle.slot_);
  return handle.owner_ == owner_ && found != predictors_.end() &&
         found->second.generation == handle.generation_;
}

bool SystemVerilogUvmRegisterModelService::contains(
    const SystemVerilogUvmRegisterFrontdoorHandle handle) const noexcept {
  const auto found = frontdoors_.find(handle.slot_);
  return handle.owner_ == owner_ && found != frontdoors_.end() &&
         found->second.generation == handle.generation_;
}

SystemVerilogUvmRegisterModelService::AdapterEntry &
SystemVerilogUvmRegisterModelService::adapter(
    const SystemVerilogUvmRegisterAdapterHandle handle) {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register adapter handle");
  return adapters_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::AdapterEntry &
SystemVerilogUvmRegisterModelService::adapter(
    const SystemVerilogUvmRegisterAdapterHandle handle) const {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register adapter handle");
  return adapters_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::PredictorEntry &
SystemVerilogUvmRegisterModelService::predictor(
    const SystemVerilogUvmRegisterPredictorHandle handle) {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register predictor handle");
  return predictors_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::PredictorEntry &
SystemVerilogUvmRegisterModelService::predictor(
    const SystemVerilogUvmRegisterPredictorHandle handle) const {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register predictor handle");
  return predictors_.at(handle.slot_);
}

SystemVerilogUvmRegisterModelService::FrontdoorEntry &
SystemVerilogUvmRegisterModelService::frontdoor(
    const SystemVerilogUvmRegisterFrontdoorHandle handle) {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register frontdoor handle");
  return frontdoors_.at(handle.slot_);
}

const SystemVerilogUvmRegisterModelService::FrontdoorEntry &
SystemVerilogUvmRegisterModelService::frontdoor(
    const SystemVerilogUvmRegisterFrontdoorHandle handle) const {
  if (!contains(handle))
    fail_frontdoor_api(kInvalidFrontdoor,
                       "invalid or foreign UVM register frontdoor handle");
  return frontdoors_.at(handle.slot_);
}

SystemVerilogUvmRegisterAdapterHandle
SystemVerilogUvmRegisterModelService::register_adapter(
    SystemVerilogUvmRegisterAdapterDescriptor descriptor) {
  require_frontdoor_services();
  validate_name(descriptor.name);
  if (!descriptor.root || !descriptor.sequencer || !descriptor.sequence ||
      !descriptor.reg_to_bus || !descriptor.drive || !descriptor.bus_to_reg)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "incomplete UVM register adapter descriptor");
  const auto sequencer = sequences_->snapshot(descriptor.sequencer);
  const auto sequence = sequences_->snapshot(descriptor.sequence);
  if (sequencer.root != descriptor.root || sequence.root != descriptor.root ||
      sequence.sequencer != descriptor.sequencer || sequencer.is_virtual ||
      sequence.is_virtual || descriptor.priority == 0 ||
      descriptor.priority > sequences_->limits().maximum_priority)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register adapter sequence ownership is invalid");
  if (adapters_.size() >= limits_.maximum_adapters ||
      next_adapter_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register adapter resource ceiling exceeded");
  record_mutation();
  const auto slot = next_adapter_slot_++;
  const auto handle =
      SystemVerilogUvmRegisterAdapterHandle{owner_, slot, 1};
  SystemVerilogUvmRegisterAdapterSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.root = descriptor.root;
  snapshot.sequencer = descriptor.sequencer;
  snapshot.sequence = descriptor.sequence;
  snapshot.name = std::move(descriptor.name);
  snapshot.priority = descriptor.priority;
  snapshot.auto_predict = descriptor.auto_predict;
  snapshot.registration_order = next_frontdoor_order_++;
  adapters_.emplace(
      slot, AdapterEntry{std::move(snapshot), std::move(descriptor.reg_to_bus),
                         std::move(descriptor.drive),
                         std::move(descriptor.bus_to_reg), 1});
  return handle;
}

SystemVerilogUvmRegisterPredictorHandle
SystemVerilogUvmRegisterModelService::register_predictor(
    SystemVerilogUvmRegisterPredictorDescriptor descriptor) {
  require_frontdoor_services();
  validate_name(descriptor.name);
  const auto map_snapshot = map(descriptor.map).snapshot;
  require_locked(map_snapshot.block);
  if (!descriptor.analysis_implementation || !descriptor.convert)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "incomplete UVM register predictor descriptor");
  const auto endpoint = tlm1_->snapshot(descriptor.analysis_implementation);
  const auto root = block(map_snapshot.block).snapshot.root;
  if (endpoint.kind != SystemVerilogUvmTlm1EndpointKind::Implementation ||
      endpoint.profile.interface_kind != SystemVerilogUvmTlm1Interface::Analysis ||
      endpoint.root != root)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register predictor analysis ownership is invalid");
  if (predictors_.size() >= limits_.maximum_predictors ||
      next_predictor_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register predictor resource ceiling exceeded");
  record_mutation();
  const auto slot = next_predictor_slot_++;
  const auto handle =
      SystemVerilogUvmRegisterPredictorHandle{owner_, slot, 1};
  SystemVerilogUvmRegisterPredictorSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.map = descriptor.map;
  snapshot.analysis_implementation = descriptor.analysis_implementation;
  snapshot.name = std::move(descriptor.name);
  snapshot.registration_order = next_frontdoor_order_++;
  predictors_.emplace(
      slot, PredictorEntry{std::move(snapshot), std::move(descriptor.convert), 1});
  tlm1_->set_analysis_subscriber(
      descriptor.analysis_implementation,
      [this, handle](const SystemVerilogUvmTlm1Payload payload) {
        auto &entry = predictor(handle);
        if (entry.snapshot.observations >= limits_.maximum_predictor_observations)
          fail_frontdoor_api(kFrontdoorLimit,
                             "UVM register predictor observation limit exceeded");
        ++entry.snapshot.observations;
        std::optional<SystemVerilogUvmRegisterBusObservation> observation;
        try {
          observation = entry.convert(payload);
        } catch (...) {
          ++entry.snapshot.failures;
          fail_frontdoor_api(kInvalidFrontdoor,
                             "UVM register predictor conversion callback threw");
        }
        if (!observation)
          return;
        try {
          const auto result = predict_observation(entry.snapshot.map, *observation);
          if (result.success())
            ++entry.snapshot.predictions;
          else
            ++entry.snapshot.failures;
        } catch (...) {
          ++entry.snapshot.failures;
          throw;
        }
      });
  return handle;
}

SystemVerilogUvmRegisterAdapterSnapshot
SystemVerilogUvmRegisterModelService::adapter_snapshot(
    const SystemVerilogUvmRegisterAdapterHandle handle) const {
  return adapter(handle).snapshot;
}

SystemVerilogUvmRegisterPredictorSnapshot
SystemVerilogUvmRegisterModelService::predictor_snapshot(
    const SystemVerilogUvmRegisterPredictorHandle handle) const {
  return predictor(handle).snapshot;
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_snapshot(
    const SystemVerilogUvmRegisterFrontdoorHandle handle) const {
  return frontdoor(handle).snapshot;
}

std::vector<SystemVerilogUvmRegisterAdapterSnapshot>
SystemVerilogUvmRegisterModelService::adapters() const {
  std::vector<SystemVerilogUvmRegisterAdapterSnapshot> result;
  result.reserve(adapters_.size());
  for (const auto &[slot, entry] : adapters_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterPredictorSnapshot>
SystemVerilogUvmRegisterModelService::predictors() const {
  std::vector<SystemVerilogUvmRegisterPredictorSnapshot> result;
  result.reserve(predictors_.size());
  for (const auto &[slot, entry] : predictors_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

std::vector<SystemVerilogUvmRegisterFrontdoorSnapshot>
SystemVerilogUvmRegisterModelService::frontdoor_operations() const {
  std::vector<SystemVerilogUvmRegisterFrontdoorSnapshot> result;
  result.reserve(frontdoors_.size());
  for (const auto &[slot, entry] : frontdoors_) {
    (void)slot;
    result.push_back(entry.snapshot);
  }
  return result;
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::begin_frontdoor(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const std::optional<SystemVerilogUvmRegisterHandle> register_handle,
    const std::optional<SystemVerilogUvmRegisterMemoryHandle> memory_handle,
    const std::size_t memory_word_index,
    const SystemVerilogUvmRegisterFrontdoorKind kind, PackedLogic4 value,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  require_frontdoor_services();
  const auto &adapter_entry = adapter(adapter_handle);
  const auto map_snapshot = map(map_handle).snapshot;
  require_locked(map_snapshot.block);
  const auto root = block(map_snapshot.block).snapshot.root;
  if (adapter_entry.snapshot.root != root ||
      register_handle.has_value() == memory_handle.has_value())
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor target ownership is invalid");

  std::vector<SystemVerilogUvmRegisterBusBeat> beats;
  std::uint32_t width{};
  PackedLogic4 original;
  if (register_handle) {
    const auto register_snapshot = reg(*register_handle).snapshot;
    if (register_snapshot.root != root)
      fail_frontdoor_api(kInvalidFrontdoor,
                         "UVM register frontdoor register has a foreign root");
    width = register_snapshot.width_bits;
    original = get_mirrored(*register_handle);
    beats = register_accesses(map_handle, *register_handle);
  } else {
    const auto memory_snapshot = memory(*memory_handle).snapshot;
    if (memory_snapshot.root != root ||
        memory_word_index >= memory_snapshot.word_count)
      fail_frontdoor_api(kInvalidFrontdoor,
                         "UVM register frontdoor memory target is invalid");
    width = memory_snapshot.word_width_bits;
    const auto index = flatten_memory_index(
        memory_snapshot, unflatten_memory_index(memory_snapshot, memory_word_index));
    const auto &state = memory_values_.at(memory_snapshot.identity);
    const auto found = state.mirrored.find(index);
    original = found == state.mirrored.end()
                   ? PackedLogic4{width, Logic4::zero}
                   : found->second;
    beats = memory_accesses(map_handle, *memory_handle, memory_word_index);
  }
  const bool is_read = kind == SystemVerilogUvmRegisterFrontdoorKind::Read ||
                       kind == SystemVerilogUvmRegisterFrontdoorKind::Mirror;
  for (const auto &beat : beats) {
    const auto target = lookup(map_handle, beat.address, is_read);
    const bool matches =
        target &&
        ((register_handle && target->register_handle == register_handle) ||
         (memory_handle && target->memory == memory_handle &&
          target->memory_word_index == memory_word_index));
    if (!matches)
      fail_frontdoor_api(kInvalidFrontdoor,
                         "UVM register frontdoor map rights deny its target");
  }
  if (is_read)
    value = PackedLogic4{width, Logic4::zero};
  if (value.width() != width)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor value width is invalid");
  if (value.width() > limits_.maximum_frontdoor_value_bits ||
      frontdoors_.size() >= limits_.maximum_frontdoor_operations ||
      pending_frontdoors_ >= limits_.maximum_pending_frontdoor_operations ||
      beats.size() > limits_.maximum_frontdoor_bus_items - frontdoor_bus_items_)
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register frontdoor operation limit exceeded");
  std::size_t byte_enable_count{};
  for (const auto &beat : beats) {
    if (beat.byte_enables.size() >
        limits_.maximum_frontdoor_byte_enables - byte_enable_count)
      fail_frontdoor_api(kFrontdoorLimit,
                         "UVM register frontdoor byte-enable limit exceeded");
    byte_enable_count += beat.byte_enables.size();
  }
  if (byte_enable_count >
      limits_.maximum_frontdoor_byte_enables - frontdoor_byte_enables_ ||
      next_frontdoor_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register frontdoor retained resource limit exceeded");

  record_mutation();
  const auto slot = next_frontdoor_slot_++;
  const auto handle =
      SystemVerilogUvmRegisterFrontdoorHandle{owner_, slot, 1};
  SystemVerilogUvmRegisterFrontdoorSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.kind = kind;
  snapshot.adapter = adapter_handle;
  snapshot.map = map_handle;
  snapshot.register_handle = register_handle;
  snapshot.memory = memory_handle;
  snapshot.memory_word_index = memory_word_index;
  snapshot.result.value = is_read ? PackedLogic4{width, Logic4::zero} : value;
  snapshot.phase = options.phase;
  snapshot.process = options.process;
  if (options.timeout_ticks != 0)
    snapshot.deadline = sequences_->handshake_time() + options.timeout_ticks;
  snapshot.check = options.check;
  snapshot.operation_order = next_frontdoor_order_++;
  snapshot.bus_items.reserve(beats.size());
  for (std::size_t index = 0; index < beats.size(); ++index) {
    const auto &beat = beats[index];
    SystemVerilogUvmRegisterBusItem item;
    item.kind = is_read ? SystemVerilogUvmRegisterBusOperationKind::Read
                        : SystemVerilogUvmRegisterBusOperationKind::Write;
    item.map = map_handle;
    item.register_handle = register_handle;
    item.memory = memory_handle;
    item.memory_word_index = memory_word_index;
    item.address = beat.address;
    item.byte_enables = beat.byte_enables;
    item.data_byte_offset = beat.data_byte_offset;
    item.beat_index = index;
    item.beat_count = beats.size();
    item.operation_order = snapshot.operation_order;
    item.data = extract_bus_data(value, beat.data_byte_offset,
                                 beat.byte_enables.size());
    snapshot.bus_items.push_back(std::move(item));
  }
  frontdoor_bus_items_ += beats.size();
  frontdoor_byte_enables_ += byte_enable_count;
  ++pending_frontdoors_;
  const auto [inserted, did_insert] = frontdoors_.emplace(
      slot, FrontdoorEntry{std::move(snapshot), value, original, std::nullopt, 1});
  if (!did_insert)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "duplicate UVM register frontdoor identity");
  continue_frontdoor(inserted->second);
  return inserted->second.snapshot;
}

void SystemVerilogUvmRegisterModelService::fail_frontdoor(
    FrontdoorEntry &operation,
    const SystemVerilogUvmRegisterOperationStatus status, std::string message) {
  if (terminal(operation.snapshot.state))
    return;
  operation.snapshot.state = SystemVerilogUvmRegisterFrontdoorState::Failed;
  operation.snapshot.result.status = status;
  operation.snapshot.result.message = std::move(message);
  operation.pending_transaction.reset();
  --pending_frontdoors_;
}

void SystemVerilogUvmRegisterModelService::consume_frontdoor_response(
    FrontdoorEntry &operation,
    const SystemVerilogUvmSequenceItemHandle response_item) {
  if (!operation.pending_transaction)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor has no pending bus beat");
  auto &adapter_entry = adapter(operation.snapshot.adapter);
  sequences_->item_done(*operation.pending_transaction, response_item);
  const auto response = adapter_entry.bus_to_reg(response_item);
  operation.pending_transaction.reset();
  if (!response.success()) {
    fail_frontdoor(operation, response.status,
                   response.message.empty() ? "UVM register bus response failed"
                                            : response.message);
    return;
  }
  const auto &item =
      operation.snapshot.bus_items[operation.snapshot.completed_beats];
  if (item.kind == SystemVerilogUvmRegisterBusOperationKind::Read) {
    if (response.data.width() > item.byte_enables.size() * 8U) {
      fail_frontdoor(operation, SystemVerilogUvmRegisterOperationStatus::NotOk,
                     "UVM register bus response width exceeds its beat");
      return;
    }
    insert_bus_data(operation.snapshot.result.value, response.data,
                    item.data_byte_offset, item.byte_enables);
  }
  ++operation.snapshot.completed_beats;
}

void SystemVerilogUvmRegisterModelService::finish_frontdoor(
    FrontdoorEntry &operation) {
  auto &snapshot = operation.snapshot;
  const auto &adapter_entry = adapter(snapshot.adapter);
  const bool is_read = snapshot.kind == SystemVerilogUvmRegisterFrontdoorKind::Read ||
                       snapshot.kind == SystemVerilogUvmRegisterFrontdoorKind::Mirror;
  if (adapter_entry.snapshot.auto_predict) {
    const auto predict_kind = is_read ? SystemVerilogUvmRegisterPredictKind::Read
                                      : SystemVerilogUvmRegisterPredictKind::Write;
    if (snapshot.register_handle) {
      snapshot.result =
          predict(*snapshot.register_handle, snapshot.result.value, predict_kind);
    } else {
      const auto memory_snapshot = memory(*snapshot.memory).snapshot;
      snapshot.result = predict(
          *snapshot.memory,
          unflatten_memory_index(memory_snapshot, snapshot.memory_word_index),
          snapshot.result.value, predict_kind);
    }
  }
  if (snapshot.kind == SystemVerilogUvmRegisterFrontdoorKind::Mirror &&
      snapshot.check) {
    snapshot.mismatch = operation.original_mirror != snapshot.result.value;
  }
  snapshot.state = snapshot.result.success()
                       ? SystemVerilogUvmRegisterFrontdoorState::Completed
                       : SystemVerilogUvmRegisterFrontdoorState::Failed;
  --pending_frontdoors_;
}

void SystemVerilogUvmRegisterModelService::continue_frontdoor(
    FrontdoorEntry &operation,
    const std::optional<SystemVerilogUvmSequenceItemHandle> supplied_response) {
  if (terminal(operation.snapshot.state))
    return;
  SystemVerilogUvmSequenceRequestHandle queued_request;
  try {
    if (supplied_response)
      consume_frontdoor_response(operation, *supplied_response);
    while (!terminal(operation.snapshot.state) &&
           operation.snapshot.completed_beats <
               operation.snapshot.bus_items.size()) {
      if (!drive_frontdoor_beat(operation, queued_request))
        return;
    }
    if (!terminal(operation.snapshot.state))
      finish_frontdoor(operation);
  } catch (...) {
    cancel_frontdoor_transport(operation, queued_request);
    fail_frontdoor(operation, SystemVerilogUvmRegisterOperationStatus::NotOk,
                   current_exception_message());
  }
}

bool SystemVerilogUvmRegisterModelService::drive_frontdoor_beat(
    FrontdoorEntry &operation,
    SystemVerilogUvmSequenceRequestHandle &queued_request) {
  const auto &item =
      operation.snapshot.bus_items[operation.snapshot.completed_beats];
  auto &adapter_entry = adapter(operation.snapshot.adapter);
  const auto request_item = adapter_entry.reg_to_bus(item);
  queued_request = sequences_->enqueue_request(
      {adapter_entry.snapshot.sequence, adapter_entry.snapshot.priority, true,
       {}, request_item});
  const auto timeout =
      operation.snapshot.deadline
          ? *operation.snapshot.deadline - sequences_->handshake_time()
          : 0;
  const auto acquired = sequences_->get_next_item(
      adapter_entry.snapshot.sequencer,
      {operation.snapshot.phase, operation.snapshot.process, timeout});
  if (acquired.status != SystemVerilogUvmSequenceAcquireStatus::Acquired ||
      !acquired.transaction) {
    sequences_->cancel_request(queued_request);
    queued_request = {};
    fail_frontdoor(operation, SystemVerilogUvmRegisterOperationStatus::NotOk,
                   "UVM register frontdoor driver did not acquire its beat");
    return false;
  }
  queued_request = {};
  operation.snapshot.transactions.push_back(acquired.transaction->handle);
  operation.pending_transaction = acquired.transaction->handle;
  const auto response = adapter_entry.drive(item, *acquired.transaction);
  if (!response)
    return false;
  consume_frontdoor_response(operation, *response);
  return true;
}

void SystemVerilogUvmRegisterModelService::cancel_frontdoor_transport(
    FrontdoorEntry &operation,
    const SystemVerilogUvmSequenceRequestHandle queued_request) noexcept {
  if (queued_request) {
    try {
      sequences_->cancel_request(queued_request);
    } catch (...) {
    }
  }
  if (!operation.pending_transaction)
    return;
  try {
    sequences_->cancel_transaction(
        *operation.pending_transaction,
        SystemVerilogUvmSequenceCancellationReason::Explicit);
  } catch (...) {
  }
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_read(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  return begin_frontdoor(adapter_handle, map_handle, register_handle,
                         std::nullopt, 0,
                         SystemVerilogUvmRegisterFrontdoorKind::Read,
                         PackedLogic4{}, options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_write(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle, PackedLogic4 value,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  return begin_frontdoor(adapter_handle, map_handle, register_handle,
                         std::nullopt, 0,
                         SystemVerilogUvmRegisterFrontdoorKind::Write,
                         std::move(value), options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_update(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  if (!needs_update(register_handle)) {
    require_frontdoor_services();
    const auto &adapter_entry = adapter(adapter_handle);
    const auto map_snapshot = map(map_handle).snapshot;
    require_locked(map_snapshot.block);
    const auto register_snapshot = reg(register_handle).snapshot;
    if (adapter_entry.snapshot.root != register_snapshot.root ||
        block(map_snapshot.block).snapshot.root != register_snapshot.root)
      fail_frontdoor_api(kInvalidFrontdoor,
                         "UVM register update target ownership is invalid");
    if (frontdoors_.size() >= limits_.maximum_frontdoor_operations ||
        next_frontdoor_slot_ == std::numeric_limits<std::uint64_t>::max() ||
        next_frontdoor_order_ == std::numeric_limits<std::uint64_t>::max())
      fail_frontdoor_api(kFrontdoorLimit,
                         "UVM register frontdoor operation limit exceeded");
    record_mutation();
    const auto slot = next_frontdoor_slot_++;
    const auto handle =
        SystemVerilogUvmRegisterFrontdoorHandle{owner_, slot, 1};
    SystemVerilogUvmRegisterFrontdoorSnapshot snapshot;
    snapshot.handle = handle;
    snapshot.kind = SystemVerilogUvmRegisterFrontdoorKind::Update;
    snapshot.state = SystemVerilogUvmRegisterFrontdoorState::Completed;
    snapshot.adapter = adapter_handle;
    snapshot.map = map_handle;
    snapshot.register_handle = register_handle;
    snapshot.result.value = get(register_handle);
    snapshot.phase = options.phase;
    snapshot.process = options.process;
    snapshot.check = options.check;
    snapshot.operation_order = next_frontdoor_order_++;
    frontdoors_.emplace(
        slot, FrontdoorEntry{snapshot, snapshot.result.value,
                             get_mirrored(register_handle), std::nullopt, 1});
    return snapshot;
  }
  return begin_frontdoor(adapter_handle, map_handle, register_handle,
                         std::nullopt, 0,
                         SystemVerilogUvmRegisterFrontdoorKind::Update,
                         get(register_handle), options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_mirror(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterHandle register_handle,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  return begin_frontdoor(adapter_handle, map_handle, register_handle,
                         std::nullopt, 0,
                         SystemVerilogUvmRegisterFrontdoorKind::Mirror,
                         PackedLogic4{}, options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_read(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::size_t word_index,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  return begin_frontdoor(adapter_handle, map_handle, std::nullopt, memory_handle,
                         word_index, SystemVerilogUvmRegisterFrontdoorKind::Read,
                         PackedLogic4{}, options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::frontdoor_write(
    const SystemVerilogUvmRegisterAdapterHandle adapter_handle,
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterMemoryHandle memory_handle,
    const std::size_t word_index, PackedLogic4 value,
    const SystemVerilogUvmRegisterFrontdoorOptions options) {
  return begin_frontdoor(adapter_handle, map_handle, std::nullopt, memory_handle,
                         word_index,
                         SystemVerilogUvmRegisterFrontdoorKind::Write,
                         std::move(value), options);
}

SystemVerilogUvmRegisterFrontdoorSnapshot
SystemVerilogUvmRegisterModelService::complete_frontdoor(
    const SystemVerilogUvmRegisterFrontdoorHandle handle,
    const SystemVerilogUvmSequenceItemHandle response) {
  auto &operation = frontdoor(handle);
  if (operation.snapshot.state !=
          SystemVerilogUvmRegisterFrontdoorState::Pending ||
      !operation.pending_transaction)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor operation is not awaiting a response");
  continue_frontdoor(operation, response);
  return operation.snapshot;
}

void SystemVerilogUvmRegisterModelService::cancel_frontdoor(
    const SystemVerilogUvmRegisterFrontdoorHandle handle,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  auto &operation = frontdoor(handle);
  if (operation.snapshot.state != SystemVerilogUvmRegisterFrontdoorState::Pending)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "terminal UVM register frontdoor cannot be cancelled");
  if (reason == SystemVerilogUvmSequenceCancellationReason::None)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register frontdoor cancellation reason is invalid");
  if (operation.pending_transaction)
    sequences_->cancel_transaction(*operation.pending_transaction, reason);
  operation.pending_transaction.reset();
  operation.snapshot.state = SystemVerilogUvmRegisterFrontdoorState::Cancelled;
  operation.snapshot.cancellation = reason;
  operation.snapshot.result.status =
      SystemVerilogUvmRegisterOperationStatus::NotOk;
  operation.snapshot.result.message = "UVM register frontdoor was cancelled";
  --pending_frontdoors_;
}

void SystemVerilogUvmRegisterModelService::cancel_phase_frontdoors(
    const SystemVerilogUvmPhaseHandle phase,
    const SystemVerilogUvmSequenceCancellationReason reason) {
  require_frontdoor_services();
  sequences_->cancel_phase_transactions(phase, reason);
  synchronize_frontdoors();
}

void SystemVerilogUvmRegisterModelService::advance_frontdoor_time(
    const SimulationTick ticks) {
  require_frontdoor_services();
  sequences_->advance_handshake_time(ticks);
  synchronize_frontdoors();
}

void SystemVerilogUvmRegisterModelService::synchronize_frontdoors() {
  require_frontdoor_services();
  for (auto &[slot, operation] : frontdoors_) {
    (void)slot;
    if (operation.snapshot.state !=
            SystemVerilogUvmRegisterFrontdoorState::Pending ||
        !operation.pending_transaction)
      continue;
    const auto transaction =
        sequences_->transaction_snapshot(*operation.pending_transaction);
    if (transaction.state != SystemVerilogUvmSequenceTransactionState::Cancelled)
      continue;
    operation.pending_transaction.reset();
    operation.snapshot.state = SystemVerilogUvmRegisterFrontdoorState::Cancelled;
    operation.snapshot.cancellation = transaction.cancellation;
    operation.snapshot.result.status =
        SystemVerilogUvmRegisterOperationStatus::NotOk;
    operation.snapshot.result.message =
        "UVM register frontdoor transaction was cancelled";
    --pending_frontdoors_;
  }
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict_bus(
    const SystemVerilogUvmRegisterPredictorHandle handle,
    const SystemVerilogUvmRegisterBusObservation &observation) {
  auto &entry = predictor(handle);
  if (entry.snapshot.observations >= limits_.maximum_predictor_observations)
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register predictor observation limit exceeded");
  ++entry.snapshot.observations;
  try {
    auto result = predict_observation(entry.snapshot.map, observation);
    if (result.success())
      ++entry.snapshot.predictions;
    else
      ++entry.snapshot.failures;
    return result;
  } catch (...) {
    ++entry.snapshot.failures;
    throw;
  }
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict_observation(
    const SystemVerilogUvmRegisterMapHandle map_handle,
    const SystemVerilogUvmRegisterBusObservation &observation) {
  if (observation.status != SystemVerilogUvmRegisterOperationStatus::IsOk)
    return {observation.status, PackedLogic4{0, Logic4::zero}, false,
            "UVM register predictor observed a failed bus transaction"};
  const bool read = observation.kind ==
                    SystemVerilogUvmRegisterBusOperationKind::Read;
  if (!read && observation.kind !=
                   SystemVerilogUvmRegisterBusOperationKind::Write)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register predictor bus operation is invalid");
  const auto target = lookup(map_handle, observation.address, read);
  if (!target)
    return {SystemVerilogUvmRegisterOperationStatus::NotOk,
            PackedLogic4{0, Logic4::zero}, false,
            "UVM register predictor address is unmapped or denied"};
  if (target->register_handle) {
    auto merged = get_mirrored(*target->register_handle);
    std::vector<std::uint8_t> enables = observation.byte_enables;
    if (enables.empty())
      enables.assign((observation.data.width() + 7U) / 8U, 1);
    insert_bus_data(merged, observation.data, observation.data_byte_offset,
                    enables);
    return predict(*target->register_handle, std::move(merged),
                   read ? SystemVerilogUvmRegisterPredictKind::Read
                        : SystemVerilogUvmRegisterPredictKind::Write);
  }
  if (!target->memory)
    fail_frontdoor_api(kInvalidFrontdoor,
                       "UVM register predictor lookup has no target");
  const auto memory_snapshot = memory(*target->memory).snapshot;
  auto indices =
      unflatten_memory_index(memory_snapshot, target->memory_word_index);
  return predict(*target->memory, indices, observation.data,
                 read ? SystemVerilogUvmRegisterPredictKind::Read
                      : SystemVerilogUvmRegisterPredictKind::Write);
}

void SystemVerilogUvmRegisterModelService::restore_memory_prediction(
    MemoryValueState &state, const std::size_t index,
    const std::optional<PackedLogic4> &desired,
    const std::optional<PackedLogic4> &mirrored,
    const std::size_t materialized_bits) {
  if (desired)
    state.desired[index] = *desired;
  else
    state.desired.erase(index);
  if (mirrored)
    state.mirrored[index] = *mirrored;
  else
    state.mirrored.erase(index);
  materialized_memory_bits_ = materialized_bits;
}

SystemVerilogUvmRegisterOperationResult
SystemVerilogUvmRegisterModelService::predict(
    const SystemVerilogUvmRegisterMemoryHandle handle,
    const std::span<const std::size_t> indices, PackedLogic4 value,
    const SystemVerilogUvmRegisterPredictKind kind) {
  const auto memory_snapshot = memory(handle).snapshot;
  require_locked(memory_snapshot.block);
  const bool unknown = has_unknown_value(value);
  if (value.width() != memory_snapshot.word_width_bits || unknown ||
      (kind != SystemVerilogUvmRegisterPredictKind::Direct &&
       kind != SystemVerilogUvmRegisterPredictKind::Read &&
       kind != SystemVerilogUvmRegisterPredictKind::Write))
    return {unknown
                ? SystemVerilogUvmRegisterOperationStatus::HasUnknown
                : SystemVerilogUvmRegisterOperationStatus::NotOk,
            std::move(value), false,
            "invalid UVM register-memory prediction"};
  if (kind == SystemVerilogUvmRegisterPredictKind::Write)
    return write(handle, indices, std::move(value));
  const auto index = flatten_memory_index(memory_snapshot, indices);
  auto &state = memory_values_.at(memory_snapshot.identity);
  const bool materialize = !state.mirrored.contains(index);
  if (materialize &&
      (state.mirrored.size() >= limits_.maximum_materialized_memory_words ||
       memory_snapshot.word_width_bits >
           limits_.maximum_materialized_memory_bits - materialized_memory_bits_))
    fail_frontdoor_api(kFrontdoorLimit,
                       "UVM register-memory prediction storage limit exceeded");
  if (kind == SystemVerilogUvmRegisterPredictKind::Direct) {
    const auto old = state.mirrored.find(index);
    const bool changed = old == state.mirrored.end() || old->second != value ||
                         !state.desired.contains(index);
    record_operation(changed);
    state.desired[index] = value;
    state.mirrored[index] = value;
    if (materialize)
      materialized_memory_bits_ += memory_snapshot.word_width_bits;
    return {SystemVerilogUvmRegisterOperationStatus::IsOk, std::move(value),
            changed, {}};
  }

  const auto desired = state.desired.find(index);
  const auto mirrored = state.mirrored.find(index);
  const auto desired_backup = desired == state.desired.end()
                                  ? std::optional<PackedLogic4>{}
                                  : std::optional<PackedLogic4>{desired->second};
  const auto mirrored_backup = mirrored == state.mirrored.end()
                                   ? std::optional<PackedLogic4>{}
                                   : std::optional<PackedLogic4>{mirrored->second};
  const auto bits_before = materialized_memory_bits_;
  state.desired[index] = value;
  state.mirrored[index] = value;
  if (materialize)
    materialized_memory_bits_ += memory_snapshot.word_width_bits;
  try {
    auto result = read(handle, indices);
    if (!result.success())
      restore_memory_prediction(state, index, desired_backup, mirrored_backup,
                                bits_before);
    return result;
  } catch (...) {
    restore_memory_prediction(state, index, desired_backup, mirrored_backup,
                              bits_before);
    throw;
  }
}

} // namespace fsim::runtime
