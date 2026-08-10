// SPDX-License-Identifier: Apache-2.0
#include "uvm_phase_tlm_sequence_probe.hpp"

#include "fsim/runtime/uvm_packer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
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

SystemVerilogUvmSequenceDescriptor
sequence_descriptor(const SystemVerilogClassHandle object, std::string name,
                    const std::string &nominal_type,
                    const SystemVerilogUvmSequenceProfile &profile,
                    const SystemVerilogUvmSequencerHandle sequencer) {
  return {object, std::move(name), nominal_type, profile, {}, sequencer, {}};
}

SystemVerilogUvmSequenceItemDescriptor
item_descriptor(const SystemVerilogClassHandle object, std::string name,
                const std::string &nominal_type,
                const SystemVerilogUvmSequenceItemRole role,
                const SystemVerilogUvmSequenceHandle owner,
                const SystemVerilogUvmSequencerHandle sequencer) {
  return {object, std::move(name), nominal_type, role, owner, sequencer};
}

} // namespace

std::string
exercise_exact_uvm_sequence_environment(fsim::app::Simulation &simulation,
                                        const SystemVerilogUvmRootHandle root) {
  const auto &item_type = find_class(simulation, "::fsim_uvm_item");
  const auto &sequence_type = find_class(simulation, "::fsim_uvm_sequence");
  const auto &virtual_sequence_type =
      find_class(simulation, "::fsim_uvm_virtual_sequence");
  const auto &sequencer_type = find_class(simulation, "::fsim_uvm_sequencer");
  const auto &driver_type = find_class(simulation, "::fsim_uvm_driver");
  const auto &monitor_type = find_class(simulation, "::fsim_uvm_monitor");
  const auto &agent_type = find_class(simulation, "::fsim_uvm_agent");
  const auto &scoreboard_type = find_class(simulation, "::fsim_uvm_scoreboard");
  const auto &environment_type = find_class(simulation, "::fsim_uvm_env");
  const auto &callback_type = find_class(simulation, "::fsim_uvm_callback");

  const auto environment =
      allocate_component(simulation, environment_type, "sequence_env", 0, root);
  const auto agent =
      allocate_component(simulation, agent_type, "agent", environment);
  const auto sequencer_component =
      allocate_component(simulation, sequencer_type, "sequencer", agent);
  const auto driver =
      allocate_component(simulation, driver_type, "driver", agent);
  const auto monitor =
      allocate_component(simulation, monitor_type, "monitor", agent);
  const auto scoreboard = allocate_component(simulation, scoreboard_type,
                                             "scoreboard", environment);
  const auto virtual_component = allocate_component(
      simulation, sequencer_type, "virtual_sequencer", environment);
  const auto callback_object =
      allocate_object(simulation, callback_type, "sequence_callback");
  assert(environment && agent && sequencer_component && driver && monitor &&
         scoreboard && virtual_component && callback_object);
  for (const auto &[name, component] :
       std::array{std::pair{std::string_view{"environment"}, environment},
                  std::pair{std::string_view{"agent"}, agent},
                  std::pair{std::string_view{"sequencer"}, sequencer_component},
                  std::pair{std::string_view{"driver"}, driver},
                  std::pair{std::string_view{"monitor"}, monitor},
                  std::pair{std::string_view{"scoreboard"}, scoreboard},
                  std::pair{std::string_view{"virtual"}, virtual_component}}) {
    if (!simulation.uvm_components().contains(component))
      throw std::logic_error{"exact UVM component was not initialized: " +
                             std::string{name}};
  }

  auto &sequences = simulation.uvm_sequences();
  const SystemVerilogUvmSequenceProfile profile{
      item_type.specialization_identity, item_type.specialization_identity};
  const auto sequencer = sequences.register_sequencer(
      {sequencer_component, sequencer_type.specialization_identity, profile});

  SystemVerilogUvmSequenceRoleDescriptor agent_role_descriptor;
  agent_role_descriptor.component = agent;
  agent_role_descriptor.kind = SystemVerilogUvmSequenceRoleKind::Agent;
  agent_role_descriptor.default_agent_mode = SystemVerilogUvmAgentMode::Active;
  const auto agent_role =
      sequences.register_role(std::move(agent_role_descriptor));

  SystemVerilogUvmSequenceRoleDescriptor driver_role_descriptor;
  driver_role_descriptor.component = driver;
  driver_role_descriptor.kind = SystemVerilogUvmSequenceRoleKind::Driver;
  driver_role_descriptor.agent = agent_role;
  driver_role_descriptor.sequencer = sequencer;
  const auto driver_role =
      sequences.register_role(std::move(driver_role_descriptor));

  SystemVerilogUvmSequenceRoleDescriptor monitor_role_descriptor;
  monitor_role_descriptor.component = monitor;
  monitor_role_descriptor.kind = SystemVerilogUvmSequenceRoleKind::Monitor;
  monitor_role_descriptor.agent = agent_role;
  monitor_role_descriptor.analysis_type = item_type.specialization_identity;
  const auto monitor_role =
      sequences.register_role(std::move(monitor_role_descriptor));

  std::vector<std::uint64_t> scoreboard_values;
  SystemVerilogUvmSequenceRoleDescriptor scoreboard_role_descriptor;
  scoreboard_role_descriptor.component = scoreboard;
  scoreboard_role_descriptor.kind =
      SystemVerilogUvmSequenceRoleKind::Scoreboard;
  scoreboard_role_descriptor.analysis_type = item_type.specialization_identity;
  scoreboard_role_descriptor.analysis_dispatch = [&](auto payload) {
    scoreboard_values.push_back(payload.value.low_word().aval);
  };
  const auto scoreboard_role =
      sequences.register_role(std::move(scoreboard_role_descriptor));
  sequences.connect_analysis(monitor_role, scoreboard_role);

  const auto schedule = *simulation.uvm_phases().standard_schedule();
  for (const auto &phase :
       {schedule.phase(SystemVerilogUvmPhaseKind::Build),
        schedule.phase(SystemVerilogUvmPhaseKind::Connect)}) {
    for (const auto component : {agent, driver, monitor, scoreboard}) {
      sequences.dispatch_role_function(
          component, phase, SystemVerilogUvmPhaseCallbackKind::Execute);
    }
  }
  assert(sequences.role_snapshot(driver_role).state ==
             SystemVerilogUvmSequenceRoleState::Connected &&
         sequences.role_snapshot(monitor_role).state ==
             SystemVerilogUvmSequenceRoleState::Connected);

  const auto make_sequence = [&](std::string name) {
    const auto object_name = name + "_object";
    return sequences.register_sequence(sequence_descriptor(
        allocate_object(simulation, sequence_type, object_name),
        std::move(name), sequence_type.specialization_identity, profile,
        sequencer));
  };
  const auto low_sequence = make_sequence("low_sequence");
  const auto high_sequence = make_sequence("high_sequence");
  const auto low_item = sequences.register_item(item_descriptor(
      allocate_object(simulation, item_type, "low_request"), "low_request",
      item_type.specialization_identity,
      SystemVerilogUvmSequenceItemRole::Request, low_sequence, sequencer));
  const auto high_item = sequences.register_item(item_descriptor(
      allocate_object(simulation, item_type, "high_request"), "high_request",
      item_type.specialization_identity,
      SystemVerilogUvmSequenceItemRole::Request, high_sequence, sequencer));
  const auto high_response = sequences.register_item(item_descriptor(
      allocate_object(simulation, item_type, "high_response"), "high_response",
      item_type.specialization_identity,
      SystemVerilogUvmSequenceItemRole::Response, high_sequence, sequencer));

  const auto low_object = sequences.snapshot(low_item).object;
  const auto high_object = sequences.snapshot(high_item).object;
  const auto value_property = [&](const SystemVerilogClassHandle object) {
    const auto& value = simulation.class_heap().object(object);
    const auto found = std::ranges::find_if(
        value.property_names,
        [](const auto& property) { return property.ends_with("::value"); });
    assert(found != value.property_names.end());
    return *found;
  };
  const auto low_value = value_property(low_object);
  const auto high_value = value_property(high_object);
  simulation.class_heap().property(low_object, low_value).packed =
      PackedLogic4::from_aval_bval(32, 161, 0);
  simulation.class_heap().property(high_object, high_value).packed =
      PackedLogic4::from_aval_bval(32, 161, 0);
  auto& object_policies = simulation.uvm_objects();
  const auto equal_policy =
      object_policies.compare_detailed(low_object, high_object);
  const auto line = object_policies.print_formatted(low_object);
  SystemVerilogUvmPrinterPolicy tree_policy;
  tree_policy.kind = SystemVerilogUvmPrinterKind::Tree;
  const auto tree = object_policies.print_formatted(low_object, tree_policy);
  SystemVerilogUvmPrinterPolicy table_policy;
  table_policy.kind = SystemVerilogUvmPrinterKind::Table;
  const auto table = object_policies.print_formatted(low_object, table_policy);
  assert(equal_policy.equal() && !line.text.empty() && !tree.text.empty()
         && table.text.starts_with("Name")
         && line.text == object_policies.print_formatted(low_object).text);
  simulation.class_heap().property(high_object, high_value).packed =
      PackedLogic4::from_aval_bval(32, 162, 0);
  SystemVerilogUvmComparerPolicy mismatch_policy;
  mismatch_policy.show_max = 1;
  const auto mismatch = object_policies.compare_detailed(
      low_object, high_object, mismatch_policy);
  assert(!mismatch.equal() && mismatch.mismatch_count == 1
         && mismatch.mismatches.size() == 1);
  const auto low_owned = object_policies.object_handle(low_object);
  const auto high_owned = object_policies.object_handle(high_object);
  const auto deep_copy = object_policies.clone_detailed(low_owned);
  SystemVerilogUvmCopierPolicy shallow_copier;
  shallow_copier.recursion = SystemVerilogUvmRecursionPolicy::Shallow;
  const auto shallow_copy =
      object_policies.clone_detailed(low_owned, shallow_copier);
  SystemVerilogUvmCopierPolicy reference_copier;
  reference_copier.recursion = SystemVerilogUvmRecursionPolicy::Reference;
  const auto reference_copy =
      object_policies.clone_detailed(low_owned, reference_copier);
  const auto restored = object_policies.copy_detailed(high_owned, low_owned);
  assert(
      object_policies.compare(low_object, deep_copy.destination.object()) &&
      object_policies.compare(low_object, shallow_copy.destination.object()) &&
      object_policies.compare(low_object, reference_copy.destination.object()) &&
      object_policies.compare(low_object, high_object) &&
      deep_copy.created_objects == 1 && shallow_copy.created_objects == 1 &&
      reference_copy.created_objects == 1 && restored.created_objects == 0);
  bool malformed_policy{};
  try {
    auto invalid = table_policy;
    invalid.separator = "\n";
    (void)object_policies.print_formatted(low_object, invalid);
  } catch (const SystemVerilogUvmObjectPolicyError& error) {
    malformed_policy =
        error.diagnostic_code() == "FSIM-UVM-POLICY-001";
  }
  bool malformed_copier{};
  try {
    auto invalid = SystemVerilogUvmCopierPolicy{};
    invalid.recursion = static_cast<SystemVerilogUvmRecursionPolicy>(255);
    (void)object_policies.copy_detailed(high_owned, low_owned, invalid);
  } catch (const SystemVerilogUvmObjectPolicyError& error) {
    malformed_copier =
        error.diagnostic_code() == "FSIM-UVM-POLICY-001";
  }
  assert(malformed_policy && malformed_copier);

  SystemVerilogUvmPackItem packed_bits;
  packed_bits.name = "logic";
  packed_bits.type_name = "logic[3:0]";
  packed_bits.kind = SystemVerilogUvmPackItemKind::Bits;
  packed_bits.bits = PackedLogic4::from_msb_string("10xz");
  SystemVerilogUvmPackItem packed_bytes;
  packed_bytes.name = "bytes";
  packed_bytes.type_name = "byte[]";
  packed_bytes.kind = SystemVerilogUvmPackItemKind::Bytes;
  packed_bytes.bytes = {0x01, 0x7f, 0x80, 0xff};
  SystemVerilogUvmPackItem packed_integers;
  packed_integers.name = "integers";
  packed_integers.type_name = "longint[]";
  packed_integers.kind = SystemVerilogUvmPackItemKind::Integers;
  packed_integers.integers = {UINT64_C(0x0102030405060708), UINT64_C(161)};
  SystemVerilogUvmPackItem packed_string;
  packed_string.name = "text";
  packed_string.type_name = "string";
  packed_string.kind = SystemVerilogUvmPackItemKind::String;
  packed_string.string_value = std::string{"exact\0uvm", 9};
  SystemVerilogUvmPackItem packed_real;
  packed_real.name = "real";
  packed_real.type_name = "real";
  packed_real.kind = SystemVerilogUvmPackItemKind::Real;
  packed_real.real_value = 16.25;
  SystemVerilogUvmPackItem packed_object;
  packed_object.name = "object";
  packed_object.type_name = "uvm_object";
  packed_object.kind = SystemVerilogUvmPackItemKind::Object;
  packed_object.object_identity = low_object;
  SystemVerilogUvmPackItem packed_array;
  packed_array.name = "array";
  packed_array.type_name = "uvm_field[]";
  packed_array.kind = SystemVerilogUvmPackItemKind::Array;
  packed_array.elements = {packed_bits, packed_object};
  const std::vector packed_items{packed_bits, packed_bytes, packed_integers,
                                 packed_string, packed_real, packed_object,
                                 packed_array};
  const SystemVerilogUvmPacker big_packer;
  const auto big_payload = big_packer.pack(packed_items);
  auto little_policy = SystemVerilogUvmPackerPolicy{};
  little_policy.endian = SystemVerilogUvmPackerEndian::Little;
  const SystemVerilogUvmPacker little_packer{little_policy};
  const auto little_payload = little_packer.pack(packed_items);
  bool malformed_pack{};
  try {
    auto truncated = big_payload.bytes;
    truncated.pop_back();
    (void)big_packer.unpack(truncated);
  } catch (const SystemVerilogUvmPackerError& error) {
    malformed_pack = error.diagnostic_code() == "FSIM-UVM-PACK-001";
  }
  assert(big_packer.unpack(big_payload.bytes) == packed_items &&
         little_packer.unpack(little_payload.bytes) == packed_items &&
         big_payload.bytes != little_payload.bytes && malformed_pack);

  auto& synchronization = simulation.uvm_synchronization();
  const auto policy_event = synchronization.event("phase_done");
  std::vector<std::string> synchronization_order;
  (void)synchronization.add_event_callback(
      policy_event, [&](const auto, const auto data) {
        assert(data == low_object);
        synchronization_order.push_back("callback");
      });
  (void)synchronization.wait_event(
      policy_event, 161, SystemVerilogUvmEventWaitKind::Trigger,
      [&](const auto outcome, const auto data) {
        assert(outcome == SystemVerilogUvmWaitOutcome::Triggered &&
               data == low_object);
        synchronization_order.push_back("waiter");
      });
  synchronization.trigger_event(policy_event, low_object);
  const auto policy_barrier =
      synchronization.create_barrier("exact_join", 2, true);
  (void)synchronization.wait_barrier(
      policy_barrier, 162,
      [&](const auto, const auto) { synchronization_order.push_back("left"); });
  (void)synchronization.wait_barrier(
      policy_barrier, 163,
      [&](const auto, const auto) { synchronization_order.push_back("right"); });
  const auto policy_pool = synchronization.create_pool("exact_pool");
  synchronization.pool_put(policy_pool, "object", packed_object);
  const auto policy_queue = synchronization.create_queue("exact_queue");
  synchronization.queue_push_back(policy_queue, packed_bits);
  synchronization.queue_push_front(policy_queue, packed_string);
  const auto heartbeat_event = synchronization.event("heartbeat_tick");
  const auto heartbeat = synchronization.create_heartbeat(
      "exact_heartbeat", heartbeat_event, SystemVerilogUvmHeartbeatMode::All);
  synchronization.heartbeat_add(heartbeat, low_object);
  synchronization.heartbeat_add(heartbeat, high_object);
  synchronization.heartbeat_start(heartbeat);
  synchronization.heartbeat_beat(heartbeat, low_object);
  synchronization.heartbeat_beat(heartbeat, high_object);
  synchronization.trigger_event(heartbeat_event);
  const auto expected_synchronization_order =
      std::vector<std::string>{"callback", "waiter", "left", "right"};
  const auto spelling = synchronization.spell_challenge(
      "phase_dne", {"heartbeat_tick", "phase_done"}, 2);
  assert(
      synchronization_order == expected_synchronization_order &&
      synchronization.event_snapshot(policy_event).trigger_count == 1 &&
      synchronization.barrier_snapshot(policy_barrier).release_count == 1 &&
      synchronization.pool_get(policy_pool, "object")->object_identity ==
          low_object &&
      synchronization.queue_size(policy_queue) == 2 &&
      synchronization.queue_get(policy_queue, 0)->string_value ==
          packed_string.string_value &&
      synchronization.heartbeat_snapshot(heartbeat).checks == 1 &&
      synchronization.heartbeat_snapshot(heartbeat).failures == 0 &&
      spelling == std::vector<std::string>{"phase_done"});

  const auto low_lock = sequences.request_lock(low_sequence);
  const auto high_grab = sequences.request_grab(high_sequence);
  assert(sequences.access_snapshot(low_lock).state ==
             SystemVerilogUvmSequenceAccessState::Granted &&
         sequences.access_snapshot(high_grab).state ==
             SystemVerilogUvmSequenceAccessState::Pending);
  sequences.unlock(low_sequence);
  assert(sequences.access_snapshot(high_grab).state ==
         SystemVerilogUvmSequenceAccessState::Granted);
  sequences.ungrab(high_sequence);

  sequences.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::StrictFifo, 161);
  const auto low_request =
      sequences.enqueue_request({low_sequence, 100, true, {}, low_item});
  const auto high_request =
      sequences.enqueue_request({high_sequence, 200, true, {}, high_item});
  const auto high_acquired = sequences.get_next_item(sequencer);
  assert(high_acquired.transaction &&
         high_acquired.transaction->request.handle == high_request);
  sequences.item_done(high_acquired.transaction->handle, high_response);
  const auto low_acquired = sequences.get_next_item(sequencer);
  assert(low_acquired.transaction &&
         low_acquired.transaction->request.handle == low_request);
  sequences.item_done(low_acquired.transaction->handle);
  assert(sequences.pop_response(high_sequence) == high_response &&
         sequences.requests(sequencer).empty());

  const auto analysis = sequences.publish_monitor(
      monitor_role, {item_type.specialization_identity,
                     PackedLogic4::from_aval_bval(32, 161, 0),
                     0,
                     root,
                     {},
                     0});
  assert(analysis.success() && analysis.deliveries.size() == 1 &&
         scoreboard_values == std::vector<std::uint64_t>{161});

  const auto virtual_sequencer = sequences.register_virtual_sequencer(
      {virtual_component,
       sequencer_type.specialization_identity,
       profile,
       {{"main", sequencer, profile}}});
  auto child_descriptor = sequence_descriptor(
      allocate_object(simulation, sequence_type, "virtual_child_object"),
      "virtual_child", sequence_type.specialization_identity, profile,
      sequencer);
  std::uint32_t virtual_priority{};
  child_descriptor.hooks.body = [&](const auto child) {
    const auto child_snapshot = sequences.snapshot(child);
    const auto acquired = sequences.get_next_item(
        sequencer, {child_snapshot.phase, child_snapshot.process, 0});
    assert(acquired.transaction);
    virtual_priority = acquired.transaction->request.priority;
    sequences.item_done(acquired.transaction->handle);
  };
  const auto child_sequence =
      sequences.register_sequence(std::move(child_descriptor));
  auto virtual_descriptor = sequence_descriptor(
      allocate_object(simulation, virtual_sequence_type,
                      "virtual_sequence_object"),
      "virtual_sequence", virtual_sequence_type.specialization_identity,
      profile, virtual_sequencer);
  SystemVerilogUvmVirtualSequenceResult virtual_result;
  virtual_descriptor.hooks.body = [&](const auto active) {
    const std::array steps{SystemVerilogUvmVirtualSequenceStep{
        "main", child_sequence, 161,
        SystemVerilogUvmVirtualSequenceAccess::Lock, true, false}};
    virtual_result = sequences.coordinate_virtual(active, steps);
  };
  const auto virtual_sequence =
      sequences.register_virtual_sequence(std::move(virtual_descriptor));
  auto &phases = simulation.uvm_phases();
  const auto virtual_domain = phases.create_domain(
      "exact_sequence", SystemVerilogUvmDomainKind::Custom);
  phases.participate(virtual_domain, root);
  const auto virtual_phase =
      phases.create_custom_phase(virtual_domain, "virtual_sequence",
                                 SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmSequenceExecutionResult virtual_execution;
  const auto virtual_phase_execution = phases.execute_task_phase(
      virtual_phase, [](const auto, const auto, const auto) {},
      [&](const auto component, const auto phase, const auto process) {
        if (component == virtual_component) {
          virtual_execution =
              sequences.start(virtual_sequence, {phase, process, true, false});
        }
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });
  if (!(virtual_execution.success() && virtual_result.success() &&
        virtual_phase_execution.success() &&
        virtual_result.children.size() == 1 && virtual_priority == 161 &&
        sequences.requests(sequencer).empty() &&
        sequences.access_requests(sequencer).empty())) {
    throw std::logic_error{
        "exact UVM virtual sequence failed: outer=" +
        std::to_string(virtual_execution.success()) + " failure=" +
        (virtual_execution.failures.empty()
             ? std::string{"none"}
             : virtual_execution.failures.front().diagnostic_code + ":" +
                   virtual_execution.failures.front().message) +
        " completed=" + std::to_string(virtual_result.completed) +
        " stopped=" + std::to_string(virtual_result.stopped) +
        " killed=" + std::to_string(virtual_result.killed) +
        " reset=" + std::to_string(virtual_result.reset) +
        " children=" + std::to_string(virtual_result.children.size()) +
        " priority=" + std::to_string(virtual_priority) + " requests=" +
        std::to_string(sequences.requests(sequencer).size()) + " access=" +
        std::to_string(sequences.access_requests(sequencer).size())};
  }

  auto &callbacks = simulation.uvm_callbacks();
  std::vector<std::string> callback_order;
  const auto callback_target = sequences.snapshot(high_sequence).object;
  const auto callback_target_type =
      std::string{simulation.uvm_objects().type_name(callback_target)};
  for (const auto ordering : {SystemVerilogUvmCallbackOrdering::Prepend,
                              SystemVerilogUvmCallbackOrdering::Append}) {
    const auto instance = ordering == SystemVerilogUvmCallbackOrdering::Append
                              ? callback_target
                              : 0;
    (void)callbacks.add({callback_target_type, "uvm_transaction_callback",
                         ordering == SystemVerilogUvmCallbackOrdering::Prepend
                             ? "type_callback"
                             : "instance_callback",
                         instance, UINT64_C(15), ordering,
                         [&](auto &invocation) {
                           callback_order.push_back(invocation.operation);
                           if (invocation.operation == "begin")
                             invocation.attributes["callback"] = "exact";
                         }});
  }
  auto &transactions = simulation.uvm_transactions();
  const auto transaction =
      transactions.begin({"sequence_transaction",
                          "sequence_bus",
                          "request",
                          root,
                          callback_target,
                          std::nullopt,
                          {{"priority", std::uint64_t{161}}}});
  transactions.record_attribute(transaction, {"response", std::uint64_t{161}});
  transactions.record_object(transaction, low_object, "exact");
  transactions.end(transaction);
  const auto transaction_snapshot = transactions.snapshot(transaction);
  const auto replayed_transactions =
      transactions.replay_trace(transactions.trace_records());
  if (!(callback_order.size() == 42 && callback_order[0] == "begin" &&
        callback_order[1] == "begin" &&
        std::ranges::count(callback_order, "attribute") == 38 &&
        callback_order[40] == "end" && callback_order[41] == "end" &&
        transaction_snapshot.state ==
            SystemVerilogUvmTransactionState::Completed &&
        transaction_snapshot.attributes.size() == 21 &&
        transactions.trace_records().size() == 23 &&
        replayed_transactions.size() == 1 &&
        replayed_transactions.front().identity == transaction_snapshot.identity &&
        replayed_transactions.front().attributes.size() == 21 &&
        replayed_transactions.front().state ==
            SystemVerilogUvmTransactionState::Completed)) {
    std::string observed_order;
    for (const auto &operation : callback_order) {
      if (!observed_order.empty())
        observed_order += ',';
      observed_order += operation;
    }
    throw std::logic_error{
        "exact UVM transaction proof failed: callbacks=" + observed_order +
        " state=" +
        std::to_string(static_cast<unsigned>(transaction_snapshot.state)) +
        " attributes=" +
        std::to_string(transaction_snapshot.attributes.size()) +
        " traces=" + std::to_string(transactions.trace_records().size())};
  }

  auto& command_line = simulation.uvm_command_line();
  auto& activity = simulation.uvm_activity();
  auto& factory = simulation.uvm_factory();
  auto& resources = simulation.uvm_resources();
  auto& config_db = simulation.uvm_config_db();
  factory.set_trace_enabled(true);
  factory.set_trace_callback([&](const auto& record) {
    activity.publish({
        SystemVerilogUvmActivityKind::Configuration,
        SystemVerilogUvmActivityAction::Updated,
        "factory:" + record.resolution.requested_name,
        "steps=" + std::to_string(record.resolution.steps.size()),
        root, record.resolution.resolved});
  });
  resources.set_trace_callback([&](const auto& record) {
    activity.publish({
        SystemVerilogUvmActivityKind::Configuration,
        SystemVerilogUvmActivityAction::Updated,
        "resource:" + record.name,
        "matches=" + std::to_string(record.match_count),
        root, record.selected});
  });
  config_db.set_trace_callback([&](const auto& record) {
    activity.publish({
        SystemVerilogUvmActivityKind::Configuration,
        SystemVerilogUvmActivityAction::Updated,
        "config:" + record.field_name,
        record.instance_name, root, record.resource});
  });
  const std::vector<std::string> exact_arguments{
      "fsim", "+UVM_TESTNAME=fsim_uvm_test", "+USER=first", "+USER=second",
      "+ntb_random_seed=161", "+UVM_TIMEOUT=20,YES",
      "+UVM_VERBOSITY=UVM_LOW", "+UVM_MAX_QUIT_COUNT=3,NO",
      "+UVM_OBJECTION_TRACE", "+UVM_RESOURCE_DB_TRACE",
      "+UVM_CONFIG_DB_TRACE",
      "+uvm_set_config_string=sequence_env.*,trace_mode,enabled",
      "+uvm_set_verbosity=sequence_env.*,_ALL_,UVM_FULL,run", "-quiet"};
  command_line.apply(exact_arguments);
  const auto exact_user_values = command_line.get_arg_values("+USER=");
  assert(command_line.get_args() == exact_arguments &&
         command_line.get_plusargs().size() == 12 &&
         command_line.get_uvm_args().size() == 9 &&
         command_line.get_arg_matches(
             "+UVM_TESTNAME=fsim_uvm_test", true).size() == 1 &&
         exact_user_values ==
             std::vector<std::string>({"first", "second"}) &&
         command_line.get_arg_value("+USER=") == "first" &&
         command_line.get_tool_name() == "fsim" &&
         command_line.get_tool_version() == "v2");
  const auto registered_factory_types = simulation.uvm_registry().types();
  assert(!registered_factory_types.empty());
  const auto factory_resolution = factory.debug_resolve_by_type(
      registered_factory_types.front().wrapper, "sequence_env.agent");
  const auto config_value = config_db.get(
      {"", 0}, "sequence_env.agent", "trace_mode",
      kSystemVerilogUvmStringConfigType, "sequence_env.agent");
  const auto resource = resources.get_by_name(
      "sequence_env.agent", "trace_mode",
      kSystemVerilogUvmStringConfigType);
  assert(factory_resolution.resolved == registered_factory_types.front().wrapper &&
         config_value &&
         std::get<std::string>(*config_value) == "enabled" && resource != 0 &&
         factory.report(true).find("Registered Types\n") == 0 &&
         config_db.report(true).find("trace_mode") != std::string::npos &&
         factory.trace_text().find("UVM_FACTORY_TRACE") == 0 &&
         resources.trace_text().find("UVM_RESOURCE_DB_TRACE") == 0 &&
         config_db.trace_text().find("UVM_CONFIG_DB_TRACE") == 0 &&
         factory.trace_callback_failures() == 0 &&
         resources.trace_callback_failures() == 0 &&
         config_db.trace_callback_failures() == 0);
  factory.set_trace_enabled(false);
  resources.set_trace_enabled(false);
  config_db.set_trace_enabled(false);
  auto& reports = simulation.uvm_reports();
  reports.set_severity_override(
      agent, SystemVerilogUvmReportSeverity::Warning,
      SystemVerilogUvmReportSeverity::Error);
  reports.set_id_action_hier(
      environment, "EXACT", SystemVerilogUvmReportAction::Log);
  reports.set_id_file_hier(environment, "EXACT", 17);
  const auto exact_catcher = reports.add_catcher(
      agent, "exact_report_catcher", [](auto& context) {
        context.set_context("exact-catcher");
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  command_line.apply_initial_report_settings(
      reports, simulation.uvm_objections());
  const auto report_controls = command_line.apply_report_settings(
      simulation.uvm_components(), reports, "run", 10);
  const auto report_policy = reports.policy(
      agent, SystemVerilogUvmReportSeverity::Warning, "EXACT");
  assert(reports.default_verbosity() == 100 &&
         reports.server().max_quit_count() == 3 &&
         !reports.server().max_quit_overridable() &&
         simulation.uvm_objections().trace_enabled() &&
         report_controls.size() == 6 &&
         std::ranges::all_of(report_controls, [](const auto& control) {
           return control.verbosity == 400 && control.phase == "run";
         }) &&
         report_policy.severity == SystemVerilogUvmReportSeverity::Error &&
         report_policy.verbosity == 400 &&
         report_policy.action == SystemVerilogUvmReportAction::Log &&
         report_policy.file == 17 && reports.catcher_enabled(exact_catcher));
  auto& test_runner = simulation.uvm_test_runner();
  const auto selected_run = test_runner.run_test(
      root, {}, [&](const auto test, const auto seed) {
        assert(simulation.uvm_components().full_name(test) == "uvm_test_top" &&
               seed == 161);
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Completed, 10, "completed"};
      });
  auto repeated_options = SystemVerilogUvmRunOptions{};
  repeated_options.test_name = "fsim_uvm_test";
  repeated_options.seed = 162;
  repeated_options.timeout = 5;
  const auto repeated_run = test_runner.run_test(
      root, repeated_options, [](const auto, const auto) {
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Finished, 5, "$finish"};
      });
  const auto runs_ok =
      selected_run.success() && selected_run.seed == 161 &&
      selected_run.timeout == 20 && selected_run.cleaned &&
      selected_run.topology.find("uvm_test_top (fsim_uvm_test)") !=
          std::string::npos &&
      repeated_run.success() && repeated_run.seed == 162 &&
      repeated_run.cleaned && test_runner.run_count() == 2;
  if (!runs_ok) {
    throw std::logic_error{
        "exact UVM run_test proof failed: selected=" +
        std::to_string(selected_run.success()) + "/" +
        std::to_string(selected_run.seed) + "/" +
        std::to_string(selected_run.timeout.value_or(0)) + "/" +
        std::to_string(selected_run.cleaned) + "/" +
        selected_run.diagnostic_code + "/" + selected_run.message +
        " topology=" +
        selected_run.topology + " repeated=" +
        std::to_string(repeated_run.success()) + "/" +
        std::to_string(repeated_run.seed) + "/" +
        std::to_string(repeated_run.cleaned) + "/" +
        repeated_run.diagnostic_code + "/" + repeated_run.message +
        " count=" +
        std::to_string(test_runner.run_count())};
  }

  const auto checkpoint = simulation.capture_uvm_checkpoint();
  assert(checkpoint && !checkpoint.artifact.records.empty());
  auto bounded_limits = SystemVerilogUvmCheckpointLimits{};
  bounded_limits.maximum_records = checkpoint.artifact.records.size() - 1;
  const auto bounded = simulation.capture_uvm_checkpoint(bounded_limits);
  assert(!bounded &&
         bounded.error == SystemVerilogUvmCheckpointError::ResourceLimit);

  const auto debug = simulation.uvm_debug_snapshot();
  assert(debug.sequencers.size() >= 2 && debug.sequences.size() >= 4 &&
         debug.sequence_items.size() >= 3 && debug.callbacks.size() >= 2 &&
         debug.transactions.size() >= 1 &&
         debug.factory_trace_records.size() == 1 &&
         debug.resource_trace_records.size() == 2 &&
         debug.config_trace_records.size() == 1 &&
         fsim::app::format_uvm_debug_snapshot(
             debug, fsim::app::UvmDebugSection::configuration)
             .find("factory trace") != std::string::npos &&
         std::ranges::count_if(
             activity.events(), [](const auto& event) {
               return event.kind == SystemVerilogUvmActivityKind::Configuration;
             }) == 4);
  return " sequence=arb/lock/response/virtual "
         "roles=agent/driver/monitor/scoreboard"
         " callback=42 transaction=23 cap=records"
         " policy=line/tree/table compare=deep/mismatch/limit"
         " copier=deep/shallow/reference"
         " packer=big/little/metadata/unpack recorder=object/replay"
         " sync=event/pool/barrier/queue/heartbeat/spell"
         " cmdline=args/plus/uvm/exact/prefix/value/tool/isolation"
         " run_test=select/topology/timeout/seed/repeat/finish/fatal"
         " report=verbosity/severity/action/file/catcher/phase/time"
         " objection_trace=on/bounded"
         " tracing=factory/config/resource/debug/activity"
         " legacy_macros=field/object/component/sequence/registry/callback/report"
         " legacy_api=phase/objection/tlm/sequence/callback/register/policy/"
         "cmdline/aliases/negative"
         " uvm2020_api=policy/field_op/copier/object/printer/comparer/packer/"
         "recorder/report/version/removed"
         " uvm_release=selected/provenance/object/design/cache/checkpoint/"
         "replay/mismatch"
         " core_smoke=governed/project/object/factory/resource/config/cmdline/"
         "report/callback/run_test/topology/timeout/seed"
         " flow_smoke=phase/objection/sequence/sequencer/roles/virtual/tlm1/"
         "tlm2/callback/transaction/cancellation";
}

} // namespace fsim::tests::app
