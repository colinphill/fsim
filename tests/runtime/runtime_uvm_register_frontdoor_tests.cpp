// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_register_model.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kSequencerType{"work::reg_sequencer"};
constexpr std::string_view kSequenceType{"work::reg_sequence"};
constexpr std::string_view kRequestType{"work::reg_bus_request"};
constexpr std::string_view kResponseType{"work::reg_bus_response"};
constexpr std::string_view kAnalysisType{"work::reg_bus_observation"};

void require(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

template <typename Function>
void require_error(const std::string_view code, Function &&function,
                   const std::string_view message) {
  try {
    function();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogClassDescriptor class_descriptor(
    const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  if (specialization == kSequencerType) {
    result.declared_type = "uvm_pkg::uvm_sequencer";
    result.assignable_declared_types = {
        std::string{kSequencerType}, "uvm_pkg::uvm_sequencer",
        "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"};
  } else if (specialization == kSequenceType) {
    result.declared_type = "uvm_pkg::uvm_sequence";
    result.assignable_declared_types = {
        std::string{kSequenceType}, "uvm_pkg::uvm_sequence",
        "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"};
  } else {
    result.declared_type = "uvm_pkg::uvm_sequence_item";
    result.assignable_declared_types = {
        std::string{specialization}, "uvm_pkg::uvm_sequence_item",
        "uvm_pkg::uvm_object"};
  }
  return result;
}

SystemVerilogUvmSequenceProfile sequence_profile() {
  return {std::string{kRequestType}, std::string{kResponseType}};
}

SystemVerilogUvmTlm1Profile analysis_profile() {
  return {SystemVerilogUvmTlm1Interface::Analysis,
          SystemVerilogUvmTlm1Direction::Forward, std::string{kAnalysisType}, {}};
}

struct FrontdoorFixture {
  SystemVerilogClassHeap heap{{1'024, 1U << 20U}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmPhaseService phases;
  SystemVerilogUvmObjectionService objections;
  SystemVerilogUvmTlm1Service tlm1;
  SystemVerilogUvmSequenceService sequences;
  SystemVerilogUvmRegisterModelService model;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRootHandle peer_root{};
  SystemVerilogClassHandle sequencer_component{};
  SystemVerilogClassHandle monitor_component{};
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmRegisterMapHandle read_only_map;
  SystemVerilogUvmRegisterHandle reg;
  SystemVerilogUvmRegisterMemoryHandle memory;
  SystemVerilogUvmPhaseHandle run_phase;
  SystemVerilogUvmPhaseProcessHandle run_process;
  std::uint64_t next_item{};
  std::vector<std::pair<SystemVerilogUvmSequenceItemHandle,
                        SystemVerilogUvmRegisterBusResponse>>
      responses;
  std::map<std::uint64_t, PackedLogic4> hardware;
  std::vector<SystemVerilogUvmRegisterBusItem> driven;

  explicit FrontdoorFixture(SystemVerilogUvmRegisterModelLimits limits = {})
      : objects(
            heap,
            [this](const std::string_view specialization,
                   const std::string_view, const std::string_view) {
              return heap.allocate(class_descriptor(specialization));
            }),
        components(heap, objects), phases(components),
        objections(objects, components, phases), tlm1(heap, components),
        sequences(heap, objects, components, phases, objections),
        model(components, limits) {
    for (const auto type :
         {kSequencerType, kSequenceType, kRequestType, kResponseType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    root = components.create_root("reg_frontdoor");
    peer_root = components.create_root("peer");
    sequencer_component = make_component(kSequencerType, "sequencer", root);
    monitor_component = make_component(kSequencerType, "monitor", root);
    sequencer = sequences.register_sequencer(
        {sequencer_component, std::string{kSequencerType}, sequence_profile()});
    sequence = sequences.register_sequence(
        {make_object(kSequenceType, "frontdoor_sequence"),
         "frontdoor_sequence", std::string{kSequenceType}, sequence_profile(),
         {}, sequencer, {}});
    model.set_frontdoor_services(sequences, tlm1, phases);

    block = model.create_block({root, std::nullopt, "registers"});
    map = model.create_map({block, "bus", 0x100, 2,
                            SystemVerilogUvmRegisterMapEndianness::Little,
                            true});
    read_only_map = model.create_map(
        {block, "read_only", 0x200, 2,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    reg = model.create_register({block, "control", 32, 0});
    (void)model.create_field(
        {reg, "value", 32, 0, SystemVerilogUvmRegisterAccessPolicy::ReadWrite,
         false, SystemVerilogUvmRegisterComparePolicy::Check});
    memory = model.create_memory(
        {block, "samples", 16, 0, {4},
         SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.add_register(map, reg, 0);
    model.add_register(read_only_map, reg, 0,
                       SystemVerilogUvmRegisterMapRights::ReadOnly);
    model.add_memory(map, memory, 0x20);
    model.lock_model(block);
    const std::array roots{root};
    const auto schedule = phases.create_standard_schedule(roots);
    run_phase = schedule.phase(SystemVerilogUvmPhaseKind::Run);
    const auto execution = phases.execute_task_phase(
        run_phase, [](const auto, const auto, const auto) {},
        [&](const auto component, const auto, const auto process) {
          if (component == sequencer_component)
            run_process = process;
          return SystemVerilogUvmTaskPhaseStatus::Suspended;
        });
    (void)execution;
  }

  SystemVerilogClassHandle make_object(const std::string_view type,
                                       std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  SystemVerilogClassHandle make_component(const std::string_view type,
                                          std::string name,
                                          const SystemVerilogUvmRootHandle owner) {
    const auto result = make_object(type, name);
    components.initialize(result, name, 0, owner);
    return result;
  }

  SystemVerilogUvmSequenceItemHandle make_item(
      const SystemVerilogUvmSequenceItemRole role) {
    const auto number = next_item++;
    const auto name = std::string{role == SystemVerilogUvmSequenceItemRole::Request
                                      ? "request_"
                                      : "response_"} +
                      std::to_string(number);
    const auto type = role == SystemVerilogUvmSequenceItemRole::Request
                          ? kRequestType
                          : kResponseType;
    return sequences.register_item(
        {make_object(type, name), name, std::string{type}, role, sequence,
         sequencer});
  }

  void retain_response(const SystemVerilogUvmSequenceItemHandle item,
                       SystemVerilogUvmRegisterBusResponse response) {
    responses.emplace_back(item, std::move(response));
  }

  SystemVerilogUvmRegisterAdapterHandle make_adapter(
      std::string name = "adapter", const bool auto_predict = true,
      const bool pending = false,
      const SystemVerilogUvmRootHandle owner = {},
      const SystemVerilogUvmRegisterOperationStatus response_status =
          SystemVerilogUvmRegisterOperationStatus::IsOk,
      const bool throw_conversion = false) {
    SystemVerilogUvmRegisterAdapterDescriptor descriptor;
    descriptor.root = owner ? owner : root;
    descriptor.sequencer = sequencer;
    descriptor.sequence = sequence;
    descriptor.name = std::move(name);
    descriptor.auto_predict = auto_predict;
    descriptor.reg_to_bus = [this](const auto &) {
      return make_item(SystemVerilogUvmSequenceItemRole::Request);
    };
    descriptor.drive = [this, pending, response_status](const auto &item,
                                                        const auto &)
        -> std::optional<SystemVerilogUvmSequenceItemHandle> {
      driven.push_back(item);
      if (pending)
        return std::nullopt;
      const auto response = make_item(SystemVerilogUvmSequenceItemRole::Response);
      SystemVerilogUvmRegisterBusResponse converted;
      converted.status = response_status;
      converted.data = item.kind == SystemVerilogUvmRegisterBusOperationKind::Read
                           ? hardware.at(item.address)
                           : PackedLogic4{0, Logic4::zero};
      if (item.kind == SystemVerilogUvmRegisterBusOperationKind::Write)
        hardware[item.address] = item.data;
      retain_response(response, std::move(converted));
      return response;
    };
    descriptor.bus_to_reg = [this, throw_conversion](const auto item) {
      if (throw_conversion)
        throw std::runtime_error{"injected bus conversion failure"};
      const auto found = std::ranges::find_if(
          responses, [item](const auto &entry) { return entry.first == item; });
      if (found == responses.end())
        throw std::runtime_error{"missing converted bus response"};
      return found->second;
    };
    return model.register_adapter(std::move(descriptor));
  }
};

} // namespace

void test_systemverilog_uvm_register_frontdoors() {
  FrontdoorFixture fixture;
  fixture.hardware[0x100] =
      PackedLogic4::from_aval_bval(16, 0x7788, 0);
  fixture.hardware[0x102] =
      PackedLogic4::from_aval_bval(16, 0x5566, 0);
  const auto adapter = fixture.make_adapter();
  require(fixture.model.adapters().size() == 1 &&
              fixture.model.adapter_snapshot(adapter).sequence == fixture.sequence,
          "register adapter inventory must retain exact sequence ownership");
  require_error(
      "FSIM-UVM-REG-007",
      [&] {
        (void)fixture.make_adapter("foreign", true, false,
                                   fixture.peer_root);
      },
      "register adapters must not cross simulation roots");

  const auto write = fixture.model.frontdoor_write(
      adapter, fixture.map, fixture.reg,
      PackedLogic4::from_aval_bval(32, 0x11223344, 0));
  require(write.state == SystemVerilogUvmRegisterFrontdoorState::Completed &&
              write.completed_beats == 2 && write.transactions.size() == 2 &&
              fixture.driven[0].address == 0x100 &&
              fixture.driven[0].data.low_word().aval == 0x3344 &&
              fixture.driven[1].address == 0x102 &&
              fixture.driven[1].data.low_word().aval == 0x1122 &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                  0x11223344,
          "frontdoor writes must execute exact sequence beats and auto-predict");

  fixture.hardware[0x100] =
      PackedLogic4::from_aval_bval(16, 0x7788, 0);
  fixture.hardware[0x102] =
      PackedLogic4::from_aval_bval(16, 0x5566, 0);
  const auto mirror =
      fixture.model.frontdoor_mirror(adapter, fixture.map, fixture.reg);
  require(mirror.state == SystemVerilogUvmRegisterFrontdoorState::Completed &&
              mirror.result.value.low_word().aval == 0x55667788 &&
              mirror.mismatch &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                  0x55667788,
          "frontdoor mirror must assemble read beats, check, and predict");

  fixture.model.set(
      fixture.reg, PackedLogic4::from_aval_bval(32, 0xa1b2c3d4, 0));
  const auto before_update = fixture.driven.size();
  const auto update =
      fixture.model.frontdoor_update(adapter, fixture.map, fixture.reg);
  require(update.state == SystemVerilogUvmRegisterFrontdoorState::Completed &&
              fixture.driven.size() == before_update + 2 &&
              !fixture.model.needs_update(fixture.reg),
          "frontdoor update must write the desired value only when needed");
  const auto after_update = fixture.driven.size();
  const auto no_update =
      fixture.model.frontdoor_update(adapter, fixture.map, fixture.reg);
  require(no_update.bus_items.empty() &&
              fixture.driven.size() == after_update,
          "clean frontdoor update must complete without bus traffic");
  require_error(
      "FSIM-UVM-REG-007",
      [&] {
        (void)fixture.model.frontdoor_write(
            adapter, fixture.read_only_map, fixture.reg,
            PackedLogic4::from_aval_bval(32, 1, 0));
      },
      "frontdoor write must enforce the selected map's rights");

  fixture.hardware[0x120] =
      PackedLogic4::from_aval_bval(16, 0xbeef, 0);
  const auto memory_read =
      fixture.model.frontdoor_read(adapter, fixture.map, fixture.memory, 0);
  require(memory_read.result.success() &&
              memory_read.result.value.low_word().aval == 0xbeef,
          "memory frontdoors must reuse mapped sequence transport");

  const auto mirror_before_failure = fixture.model.get_mirrored(fixture.reg);
  const auto failing_adapter = fixture.make_adapter(
      "not_ok", true, false, {},
      SystemVerilogUvmRegisterOperationStatus::NotOk);
  const auto failed =
      fixture.model.frontdoor_read(failing_adapter, fixture.map, fixture.reg);
  require(failed.state == SystemVerilogUvmRegisterFrontdoorState::Failed &&
              failed.result.status ==
                  SystemVerilogUvmRegisterOperationStatus::NotOk &&
              fixture.model.get_mirrored(fixture.reg) == mirror_before_failure,
          "failed bus status must be contained without mirror mutation");

  const auto throwing_adapter = fixture.make_adapter(
      "throwing", true, false, {},
      SystemVerilogUvmRegisterOperationStatus::IsOk, true);
  const auto conversion_failure = fixture.model.frontdoor_read(
      throwing_adapter, fixture.map, fixture.reg);
  require(conversion_failure.state ==
              SystemVerilogUvmRegisterFrontdoorState::Failed &&
              conversion_failure.result.message.find(
                  "injected bus conversion failure") != std::string::npos,
          "adapter conversion exceptions must become retained failures");
}

void test_systemverilog_uvm_register_predictors_and_cancellation() {
  FrontdoorFixture fixture;
  fixture.hardware[0x100] =
      PackedLogic4::from_aval_bval(16, 0x2211, 0);
  fixture.hardware[0x102] =
      PackedLogic4::from_aval_bval(16, 0x4433, 0);
  const auto pending_adapter = fixture.make_adapter("pending", true, true);
  SystemVerilogUvmRegisterFrontdoorOptions timeout_options;
  timeout_options.phase = fixture.run_phase;
  timeout_options.process = fixture.run_process;
  timeout_options.timeout_ticks = 4;
  timeout_options.check = false;
  const auto pending = fixture.model.frontdoor_read(
      pending_adapter, fixture.map, fixture.reg, timeout_options);
  require(pending.state == SystemVerilogUvmRegisterFrontdoorState::Pending &&
              pending.transactions.size() == 1,
          "delayed register adapter must retain its phase-owned transaction");
  fixture.model.advance_frontdoor_time(4);
  const auto timed_out = fixture.model.frontdoor_snapshot(pending.handle);
  require(timed_out.state ==
              SystemVerilogUvmRegisterFrontdoorState::Cancelled &&
              timed_out.cancellation ==
                  SystemVerilogUvmSequenceCancellationReason::Timeout,
          "frontdoor timeout must reuse sequencer cancellation ownership");

  SystemVerilogUvmRegisterFrontdoorOptions phase_options;
  phase_options.phase = fixture.run_phase;
  phase_options.process = fixture.run_process;
  phase_options.check = false;
  const auto phase_pending = fixture.model.frontdoor_read(
      pending_adapter, fixture.map, fixture.reg, phase_options);
  fixture.model.cancel_phase_frontdoors(
      fixture.run_phase,
      SystemVerilogUvmSequenceCancellationReason::PhaseJumped);
  const auto phase_cancelled =
      fixture.model.frontdoor_snapshot(phase_pending.handle);
  require(phase_cancelled.state ==
              SystemVerilogUvmRegisterFrontdoorState::Cancelled &&
              phase_cancelled.cancellation ==
                  SystemVerilogUvmSequenceCancellationReason::PhaseJumped,
          "phase cancellation must terminate only its owned frontdoors");

  SystemVerilogUvmRegisterFrontdoorOptions unchecked_options;
  unchecked_options.check = false;
  const auto explicit_pending = fixture.model.frontdoor_read(
      pending_adapter, fixture.map, fixture.reg, unchecked_options);
  const auto response =
      fixture.make_item(SystemVerilogUvmSequenceItemRole::Response);
  fixture.retain_response(
      response,
      {SystemVerilogUvmRegisterOperationStatus::IsOk,
       PackedLogic4::from_aval_bval(16, 0x2211, 0), {}});
  const auto next = fixture.model.complete_frontdoor(explicit_pending.handle,
                                                     response);
  require(next.state == SystemVerilogUvmRegisterFrontdoorState::Pending &&
              next.completed_beats == 1,
          "explicit response must advance a multi-beat frontdoor exactly once");
  const auto second_response =
      fixture.make_item(SystemVerilogUvmSequenceItemRole::Response);
  fixture.retain_response(
      second_response,
      {SystemVerilogUvmRegisterOperationStatus::IsOk,
       PackedLogic4::from_aval_bval(16, 0x4433, 0), {}});
  const auto completed = fixture.model.complete_frontdoor(
      explicit_pending.handle, second_response);
  require(completed.state ==
              SystemVerilogUvmRegisterFrontdoorState::Completed &&
              completed.result.value.low_word().aval == 0x44332211,
          "explicit response completion must assemble and predict all beats");

  const auto invalid_response_operation = fixture.model.frontdoor_read(
      pending_adapter, fixture.map, fixture.reg, unchecked_options);
  const auto request_instead_of_response =
      fixture.make_item(SystemVerilogUvmSequenceItemRole::Request);
  const auto invalid_response = fixture.model.complete_frontdoor(
      invalid_response_operation.handle, request_instead_of_response);
  require(invalid_response.state ==
              SystemVerilogUvmRegisterFrontdoorState::Failed &&
              invalid_response.result.status ==
                  SystemVerilogUvmRegisterOperationStatus::NotOk,
          "invalid response-item ownership must be contained as a failure");

  const auto port = fixture.tlm1.register_endpoint(
      {SystemVerilogUvmTlm1EndpointKind::Port, analysis_profile(),
       fixture.monitor_component, "observed", 1, 1});
  const auto implementation = fixture.tlm1.register_analysis_implementation(
      fixture.monitor_component, "predict", std::string{kAnalysisType},
      [](SystemVerilogUvmTlm1Payload) {});
  fixture.tlm1.connect(port, implementation);
  const auto failing_port = fixture.tlm1.register_endpoint(
      {SystemVerilogUvmTlm1EndpointKind::Port, analysis_profile(),
       fixture.monitor_component, "failing_observed", 1, 1});
  const auto failing_implementation =
      fixture.tlm1.register_analysis_implementation(
          fixture.monitor_component, "failing_predict",
          std::string{kAnalysisType}, [](SystemVerilogUvmTlm1Payload) {});
  fixture.tlm1.connect(failing_port, failing_implementation);
  fixture.tlm1.resolve_all();
  const auto predictor = fixture.model.register_predictor(
      {fixture.map, implementation, "predictor",
       [](const SystemVerilogUvmTlm1Payload &payload)
           -> std::optional<SystemVerilogUvmRegisterBusObservation> {
         return SystemVerilogUvmRegisterBusObservation{
             SystemVerilogUvmRegisterBusOperationKind::Read, 0x100,
             payload.value, {1, 1, 1, 1}, 0,
             SystemVerilogUvmRegisterOperationStatus::IsOk};
       }});
  const auto publication = fixture.tlm1.write_analysis(
      port, {std::string{kAnalysisType},
             PackedLogic4::from_aval_bval(32, 0xaabbccdd, 0), 0, 0, {}, 0});
  const auto predictor_state = fixture.model.predictor_snapshot(predictor);
  require(publication.success() && predictor_state.observations == 1 &&
              predictor_state.predictions == 1 &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                  0xaabbccdd,
          "analysis predictor must convert, lookup, and explicitly predict");

  const auto unmapped = fixture.model.predict_bus(
      predictor,
      {SystemVerilogUvmRegisterBusOperationKind::Read, 0xffff,
       PackedLogic4::from_aval_bval(16, 1, 0), {1, 1}, 0,
       SystemVerilogUvmRegisterOperationStatus::IsOk});
  require(!unmapped.success() &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                  0xaabbccdd,
          "explicit predictor lookup failure must not mutate the mirror");

  const auto failing_predictor = fixture.model.register_predictor(
      {fixture.map, failing_implementation, "failing_predictor",
       [](const SystemVerilogUvmTlm1Payload &)
           -> std::optional<SystemVerilogUvmRegisterBusObservation> {
         throw std::runtime_error{"injected predictor conversion failure"};
       }});
  const auto failed_publication = fixture.tlm1.write_analysis(
      failing_port,
      {std::string{kAnalysisType},
       PackedLogic4::from_aval_bval(32, 0x12345678, 0), 0, 0, {}, 0});
  const auto failing_predictor_state =
      fixture.model.predictor_snapshot(failing_predictor);
  require(failed_publication.failures.size() == 1 &&
              failed_publication.failures.front().message.find(
                  "predictor conversion callback threw") !=
                  std::string::npos &&
              failing_predictor_state.observations == 1 &&
              failing_predictor_state.failures == 1,
          "predictor conversion exceptions must be contained by TLM analysis");
}

void test_systemverilog_uvm_register_frontdoor_limits() {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects{
      heap, [](std::string_view, std::string_view, std::string_view) {
        return SystemVerilogClassHandle{};
      }};
  SystemVerilogUvmComponentService components{heap, objects};
  SystemVerilogUvmRegisterModelService unconfigured{components};
  require_error(
      "FSIM-UVM-REG-007",
      [&] {
        (void)unconfigured.register_adapter(
            SystemVerilogUvmRegisterAdapterDescriptor{});
      },
      "unconfigured frontdoor services must reject stably");

  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_adapters = 1;
  limits.maximum_pending_frontdoor_operations = 1;
  FrontdoorFixture bounded{limits};
  const auto pending = bounded.make_adapter("bounded", true, true);
  require_error(
      "FSIM-UVM-REG-008", [&] { (void)bounded.make_adapter("overflow"); },
      "adapter ceilings must reject before publication");
  bounded.hardware[0x100] = PackedLogic4::from_aval_bval(16, 0, 0);
  bounded.hardware[0x102] = PackedLogic4::from_aval_bval(16, 0, 0);
  const auto operation =
      bounded.model.frontdoor_read(pending, bounded.map, bounded.reg);
  require(operation.state == SystemVerilogUvmRegisterFrontdoorState::Pending,
          "bounded pending fixture must reserve one operation");
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)bounded.model.frontdoor_read(pending, bounded.map, bounded.reg);
      },
      "pending frontdoor ceilings must reject without another transaction");

  SystemVerilogUvmRegisterModelLimits operation_limits;
  operation_limits.maximum_frontdoor_operations = 1;
  FrontdoorFixture operation_bounded{operation_limits};
  const auto operation_adapter = operation_bounded.make_adapter();
  operation_bounded.hardware[0x100] =
      PackedLogic4::from_aval_bval(16, 0, 0);
  operation_bounded.hardware[0x102] =
      PackedLogic4::from_aval_bval(16, 0, 0);
  (void)operation_bounded.model.frontdoor_read(
      operation_adapter, operation_bounded.map, operation_bounded.reg);
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)operation_bounded.model.frontdoor_read(
            operation_adapter, operation_bounded.map, operation_bounded.reg);
      },
      "retained frontdoor operation ceilings must reject before publication");

  SystemVerilogUvmRegisterModelLimits bus_item_limits;
  bus_item_limits.maximum_frontdoor_bus_items = 1;
  FrontdoorFixture bus_item_bounded{bus_item_limits};
  const auto bus_item_adapter = bus_item_bounded.make_adapter();
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)bus_item_bounded.model.frontdoor_read(
            bus_item_adapter, bus_item_bounded.map, bus_item_bounded.reg);
      },
      "aggregate frontdoor bus-item ceilings must reject multi-beat access");

  SystemVerilogUvmRegisterModelLimits value_limits;
  value_limits.maximum_frontdoor_value_bits = 16;
  FrontdoorFixture value_bounded{value_limits};
  const auto value_adapter = value_bounded.make_adapter();
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)value_bounded.model.frontdoor_read(
            value_adapter, value_bounded.map, value_bounded.reg);
      },
      "frontdoor value-bit ceilings must reject before transport");

  SystemVerilogUvmRegisterModelLimits byte_enable_limits;
  byte_enable_limits.maximum_frontdoor_byte_enables = 3;
  FrontdoorFixture byte_enable_bounded{byte_enable_limits};
  const auto byte_enable_adapter = byte_enable_bounded.make_adapter();
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)byte_enable_bounded.model.frontdoor_read(
            byte_enable_adapter, byte_enable_bounded.map,
            byte_enable_bounded.reg);
      },
      "aggregate frontdoor byte-enable ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits predictor_limits;
  predictor_limits.maximum_predictors = 1;
  FrontdoorFixture predictor_bounded{predictor_limits};
  const auto predictor_implementation =
      predictor_bounded.tlm1.register_analysis_implementation(
          predictor_bounded.monitor_component, "bounded_predict",
          std::string{kAnalysisType}, [](SystemVerilogUvmTlm1Payload) {});
  const auto predictor_implementation_overflow =
      predictor_bounded.tlm1.register_analysis_implementation(
          predictor_bounded.monitor_component, "overflow_predict",
          std::string{kAnalysisType}, [](SystemVerilogUvmTlm1Payload) {});
  const auto converter = [](const SystemVerilogUvmTlm1Payload &)
      -> std::optional<SystemVerilogUvmRegisterBusObservation> {
    return std::nullopt;
  };
  (void)predictor_bounded.model.register_predictor(
      {predictor_bounded.map, predictor_implementation, "bounded_predictor",
       converter});
  require_error(
      "FSIM-UVM-REG-008",
      [&] {
        (void)predictor_bounded.model.register_predictor(
            {predictor_bounded.map, predictor_implementation_overflow,
             "overflow_predictor", converter});
      },
      "predictor ceilings must reject before publication");

  SystemVerilogUvmRegisterModelLimits observation_limits;
  observation_limits.maximum_predictor_observations = 1;
  FrontdoorFixture observation_bounded{observation_limits};
  const auto observation_port = observation_bounded.tlm1.register_endpoint(
      {SystemVerilogUvmTlm1EndpointKind::Port, analysis_profile(),
       observation_bounded.monitor_component, "bounded_observations", 1, 1});
  const auto observation_implementation =
      observation_bounded.tlm1.register_analysis_implementation(
          observation_bounded.monitor_component, "bounded_prediction",
          std::string{kAnalysisType}, [](SystemVerilogUvmTlm1Payload) {});
  observation_bounded.tlm1.connect(observation_port,
                                   observation_implementation);
  observation_bounded.tlm1.resolve_all();
  const auto observation_predictor =
      observation_bounded.model.register_predictor(
          {observation_bounded.map, observation_implementation,
           "observation_predictor", converter});
  const auto payload = SystemVerilogUvmTlm1Payload{
      std::string{kAnalysisType}, PackedLogic4::from_aval_bval(16, 0, 0),
      0, 0, {}, 0};
  require(observation_bounded.tlm1.write_analysis(observation_port, payload)
              .success(),
          "first bounded predictor observation must succeed");
  const auto observation_overflow =
      observation_bounded.tlm1.write_analysis(observation_port, payload);
  require(observation_overflow.failures.size() == 1 &&
              observation_overflow.failures.front().message.find(
                  "predictor observation limit exceeded") !=
                  std::string::npos &&
              observation_bounded.model
                      .predictor_snapshot(observation_predictor)
                      .observations == 1,
          "predictor observation ceilings must be contained by TLM analysis");
}

} // namespace fsim::tests::runtime
