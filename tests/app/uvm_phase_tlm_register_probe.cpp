// SPDX-License-Identifier: Apache-2.0
#include "uvm_phase_tlm_register_probe.hpp"

#include "fsim/app/design_artifact.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::app {
namespace {

using namespace fsim::runtime;

const frontend::SystemVerilogClassSpecialization &
find_class(const fsim::app::Simulation &simulation,
           const std::string_view suffix) {
  const auto found = std::ranges::find_if(
      simulation.class_specializations(), [&](const auto &specialization) {
        return specialization.declaration_identity.ends_with(suffix);
      });
  assert(found != simulation.class_specializations().end());
  return *found;
}

SystemVerilogClassHandle
allocate_object(fsim::app::Simulation &simulation,
                const frontend::SystemVerilogClassSpecialization &type,
                std::string name) {
  return simulation.allocate_uvm_object(
      type.specialization_identity, std::move(name), type.declaration_identity);
}

SystemVerilogClassHandle
allocate_component(fsim::app::Simulation &simulation,
                   const frontend::SystemVerilogClassSpecialization &type,
                   std::string name, const SystemVerilogClassHandle parent,
                   const SystemVerilogUvmRootHandle root = {}) {
  return simulation.allocate_uvm_component(type.specialization_identity,
                                           std::move(name), parent, root,
                                           type.declaration_identity);
}

} // namespace

std::string
exercise_exact_uvm_register_environment(fsim::app::Simulation &simulation,
                                        const SystemVerilogUvmRootHandle root) {
  const auto &item_type = find_class(simulation, "::fsim_uvm_item");
  const auto &sequencer_type = find_class(simulation, "::fsim_uvm_sequencer");
  const auto &environment_type = find_class(simulation, "::fsim_uvm_env");
  const auto &block_type = find_class(simulation, "::fsim_uvm_reg_block");
  const auto &reg_type = find_class(simulation, "::fsim_uvm_reg");
  const auto &adapter_type = find_class(simulation, "::fsim_uvm_reg_adapter");
  const auto &predictor_type =
      find_class(simulation, "::fsim_uvm_reg_predictor");
  const auto &sequence_type = find_class(simulation, "::fsim_uvm_reg_sequence");
  const auto &callback_type = find_class(simulation, "::fsim_uvm_reg_callback");

  const auto environment =
      allocate_component(simulation, environment_type, "register_env", 0, root);
  const auto sequencer_component = allocate_component(
      simulation, sequencer_type, "register_sequencer", environment);
  const auto predictor_component = allocate_component(
      simulation, predictor_type, "register_predictor", environment);
  const auto block_object =
      allocate_object(simulation, block_type, "register_block_object");
  const auto reg_object =
      allocate_object(simulation, reg_type, "register_object");
  const auto item_object =
      allocate_object(simulation, item_type, "register_item_prototype");
  const auto adapter_object =
      allocate_object(simulation, adapter_type, "register_adapter_object");
  const auto sequence_object =
      allocate_object(simulation, sequence_type, "register_sequence_object");
  const auto callback_object =
      allocate_object(simulation, callback_type, "register_callback_object");
  assert(environment && sequencer_component && predictor_component &&
         block_object && reg_object && item_object && adapter_object &&
         sequence_object && callback_object &&
         simulation.uvm_components().contains(predictor_component));

  auto &sequences = simulation.uvm_sequences();
  const SystemVerilogUvmSequenceProfile profile{
      item_type.specialization_identity, item_type.specialization_identity};
  const auto sequencer = sequences.register_sequencer(
      {sequencer_component, sequencer_type.specialization_identity, profile});
  const auto sequence =
      sequences.register_sequence({sequence_object,
                                   "exact_register_sequence",
                                   sequence_type.specialization_identity,
                                   profile,
                                   {},
                                   sequencer,
                                   {}});

  auto &model = simulation.uvm_register_model();
  const auto block =
      model.create_block({root, std::nullopt, "exact_registers"});
  const auto little =
      model.create_map({block, "little", 0x2000, 2,
                        SystemVerilogUvmRegisterMapEndianness::Little, true});
  const auto big =
      model.create_map({block, "big", 0x3000, 2,
                        SystemVerilogUvmRegisterMapEndianness::Big, true});
  const auto control = model.create_register({block, "control", 32, 0x20});
  const auto low = model.create_field(
      {control, "low", 16, 0, SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  const auto high =
      model.create_field({control, "high", 16, 16,
                          SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  model.set_reset(low, "HARD", PackedLogic4::from_aval_bval(16, 0x34, 0));
  model.set_reset(high, "HARD", PackedLogic4::from_aval_bval(16, 0x12, 0));
  const auto memory =
      model.create_memory({block,
                           "samples",
                           32,
                           0x100,
                           {4},
                           SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  for (const auto &map : {little, big}) {
    model.add_register(map, control, 0x20);
    model.add_memory(map, memory, 0x100);
  }
  model.lock_model(block);
  model.reset(block, "HARD");
  assert(model.get_mirrored(control).low_word().aval == 0x00120034);

  const std::array index{std::size_t{1}};
  assert(
      model
          .write(memory, index, PackedLogic4::from_aval_bval(32, 0x11223344, 0))
          .success());
  const std::array byte_enables{std::uint8_t{1}, std::uint8_t{0},
                                std::uint8_t{1}, std::uint8_t{0}};
  assert(model
             .write(memory, index,
                    PackedLogic4::from_aval_bval(32, 0xaabbccdd, 0),
                    byte_enables)
             .success());
  assert(model.read(memory, index).value.low_word().aval == 0x11bb33dd);

  std::vector<SystemVerilogUvmRegisterBusItem> bus_items;
  std::vector<std::pair<SystemVerilogUvmSequenceItemHandle,
                        SystemVerilogUvmRegisterBusResponse>>
      responses;
  std::map<std::uint64_t, PackedLogic4> hardware;
  std::uint64_t next_item{};
  SystemVerilogUvmRegisterAdapterDescriptor adapter;
  adapter.root = root;
  adapter.sequencer = sequencer;
  adapter.sequence = sequence;
  adapter.name = "exact_register_adapter";
  adapter.auto_predict = true;
  adapter.reg_to_bus = [&](const auto &) {
    return sequences.macro_create(
        {item_object, "register_request_" + std::to_string(next_item++),
         item_type.specialization_identity,
         SystemVerilogUvmSequenceItemRole::Request, sequence, sequencer});
  };
  adapter.drive =
      [&](const auto &bus,
          const auto &) -> std::optional<SystemVerilogUvmSequenceItemHandle> {
    bus_items.push_back(bus);
    SystemVerilogUvmRegisterBusResponse response;
    if (bus.kind == SystemVerilogUvmRegisterBusOperationKind::Write) {
      hardware[bus.address] = bus.data;
      response.data = bus.data;
    } else {
      const auto found = hardware.find(bus.address);
      response.data = found == hardware.end()
                          ? PackedLogic4{bus.data.width(), Logic4::zero}
                          : found->second;
    }
    const auto item = sequences.macro_create(
        {item_object, "register_response_" + std::to_string(next_item++),
         item_type.specialization_identity,
         SystemVerilogUvmSequenceItemRole::Response, sequence, sequencer});
    responses.emplace_back(item, std::move(response));
    return item;
  };
  adapter.bus_to_reg = [&](const auto response) {
    const auto found =
        std::ranges::find_if(responses, [response](const auto &entry) {
          return entry.first == response;
        });
    assert(found != responses.end());
    return found->second;
  };
  const auto adapter_handle = model.register_adapter(std::move(adapter));
  const auto little_write =
      model.frontdoor_write(adapter_handle, little, control,
                            PackedLogic4::from_aval_bval(32, 0x44332211, 0));
  const auto big_write =
      model.frontdoor_write(adapter_handle, big, control,
                            PackedLogic4::from_aval_bval(32, 0xa1b2c3d4, 0));
  const auto frontdoor_ok =
      little_write.state == SystemVerilogUvmRegisterFrontdoorState::Completed &&
      big_write.state == SystemVerilogUvmRegisterFrontdoorState::Completed &&
      little_write.transactions.size() == 2 &&
      big_write.transactions.size() == 2 && bus_items.size() == 4 &&
      std::ranges::all_of(bus_items, [](const auto &bus) {
        return bus.byte_enables == std::vector<std::uint8_t>{1, 1};
      });
  if (!frontdoor_ok) {
    std::string enables;
    for (const auto &bus : bus_items) {
      if (!enables.empty())
        enables += ',';
      for (const auto enabled : bus.byte_enables)
        enables += enabled != 0 ? '1' : '0';
    }
    throw std::logic_error{
        "exact register frontdoor failed: little=" +
        std::to_string(static_cast<unsigned>(little_write.state)) + "/" +
        std::to_string(little_write.transactions.size()) + ":" +
        little_write.result.message +
        " big=" + std::to_string(static_cast<unsigned>(big_write.state)) + "/" +
        std::to_string(big_write.transactions.size()) + ":" +
        big_write.result.message +
        " buses=" + std::to_string(bus_items.size()) + " enables=" + enables};
  }

  auto &tlm1 = simulation.uvm_tlm1();
  const SystemVerilogUvmTlm1Profile analysis_profile{
      SystemVerilogUvmTlm1Interface::Analysis,
      SystemVerilogUvmTlm1Direction::Forward,
      item_type.specialization_identity,
      {}};
  const auto port = tlm1.register_endpoint(
      {SystemVerilogUvmTlm1EndpointKind::Port, analysis_profile, environment,
       "register_observed", 1, 1});
  const auto implementation = tlm1.register_analysis_implementation(
      predictor_component, "bus_in", item_type.specialization_identity,
      [](SystemVerilogUvmTlm1Payload) {});
  tlm1.connect(port, implementation);
  tlm1.resolve_all();
  const auto predictor = model.register_predictor(
      {little, implementation, "exact_register_predictor",
       [](const auto &payload)
           -> std::optional<SystemVerilogUvmRegisterBusObservation> {
         return SystemVerilogUvmRegisterBusObservation{
             SystemVerilogUvmRegisterBusOperationKind::Read,
             0x2020,
             payload.value,
             {1, 1, 1, 1},
             0,
             SystemVerilogUvmRegisterOperationStatus::IsOk};
       }});
  const auto publication = tlm1.write_analysis(
      port, {item_type.specialization_identity,
             PackedLogic4::from_aval_bval(32, 0x55667788, 0),
             0,
             root,
             {},
             0});
  assert(publication.success() &&
         model.predictor_snapshot(predictor).predictions == 1 &&
         model.get_mirrored(control).low_word().aval == 0x55667788);

  const auto hdl_path =
      model.register_hdl_path({root,
                               "RTL",
                               {control, std::nullopt, std::nullopt, 0},
                               {{SystemVerilogUvmRegisterHdlKind::Vpi,
                                 "left.observed_payload", 0, 0, 16},
                                {SystemVerilogUvmRegisterHdlKind::Vhpi,
                                 "right.observed_payload", 0, 16, 16}}});
  const auto deposited = model.backdoor_write(
      hdl_path, PackedLogic4::from_aval_bval(32, 0xcafebabe, 0));
  const auto backdoor = model.backdoor_read(hdl_path);
  const auto forced = model.backdoor_write(
      hdl_path, PackedLogic4::from_aval_bval(32, 0x12345678, 0),
      SystemVerilogUvmRegisterBackdoorKind::Force);
  const auto released =
      model.backdoor_write(hdl_path, PackedLogic4{32},
                           SystemVerilogUvmRegisterBackdoorKind::Release);
  assert(deposited.success() && backdoor.success() && forced.success() &&
         released.success() && backdoor.value.low_word().aval == 0xcafebabe &&
         model.hdl_path_snapshot(hdl_path).slices.size() == 2);

  SystemVerilogUvmRegisterCallbackDescriptor callback;
  callback.scope = SystemVerilogUvmRegisterCallbackScope::Register;
  callback.reg = control;
  callback.name = "exact_register_callback";
  callback.callback = [](auto &) {};
  const auto callback_handle = model.register_callback(std::move(callback));
  const auto coverage = model.register_coverage_model(
      {block, "exact_register_coverage", true, true, true});
  assert(model
             .callback_write({control, std::nullopt, std::nullopt, 0},
                             PackedLogic4::from_aval_bval(32, 0x0badf00d, 0),
                             little)
             .success());
  SystemVerilogUvmRegisterStandardSequenceOptions sequence_options;
  sequence_options.kind = SystemVerilogUvmRegisterStandardSequenceKind::Access;
  sequence_options.top = block;
  sequence_options.map = little;
  sequence_options.seed = 161;
  const auto standard = model.run_standard_sequence(sequence_options);
  assert(standard.success() && standard.operations > 0 &&
         model.callback_snapshot(callback_handle).invocations > 0 &&
         model.coverage_snapshot(coverage).samples > 0);

  const auto checkpoint = simulation.capture_uvm_checkpoint();
  assert(checkpoint && !checkpoint.artifact.records.empty());
  diagnostic::Engine diagnostics;
  const auto encoded = fsim::app::serialize_systemverilog_uvm_state(
      checkpoint.artifact, diagnostics);
  const auto decoded =
      encoded ? fsim::app::deserialize_systemverilog_uvm_state(
                    *encoded, "relocated-exact-register", diagnostics)
              : std::nullopt;
  assert(encoded && decoded && !diagnostics.has_error() &&
         *decoded == checkpoint.artifact);
  SystemVerilogUvmCheckpointLimits bounded_limits;
  bounded_limits.maximum_records = checkpoint.artifact.records.size() - 1;
  const auto bounded = simulation.capture_uvm_checkpoint(bounded_limits);
  assert(!bounded &&
         bounded.error == SystemVerilogUvmCheckpointError::ResourceLimit);

  const auto debug = simulation.uvm_debug_snapshot();
  assert(debug.register_blocks.size() >= 1 && debug.register_maps.size() >= 2 &&
         debug.registers.size() >= 1 && debug.register_fields.size() >= 2 &&
         debug.register_memories.size() >= 1 &&
         debug.register_sequences.size() >= 1 &&
         debug.register_callbacks.size() >= 1 &&
         debug.register_coverage.size() >= 1);
  return " register=frontdoor/backdoor/predictor maps=little/big"
         " byte_enable=1010 callback_coverage=1 sequence=access"
         " replay=relocated cap=records";
}

} // namespace fsim::tests::app
