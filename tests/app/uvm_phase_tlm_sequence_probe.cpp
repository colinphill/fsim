// SPDX-License-Identifier: Apache-2.0
#include "uvm_phase_tlm_sequence_probe.hpp"

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
  transactions.end(transaction);
  const auto transaction_snapshot = transactions.snapshot(transaction);
  const auto expected_callback_order = std::vector<std::string>{
      "begin", "begin", "attribute", "attribute", "end", "end"};
  if (!(callback_order == expected_callback_order &&
        transaction_snapshot.state ==
            SystemVerilogUvmTransactionState::Completed &&
        transaction_snapshot.attributes.size() == 3 &&
        transactions.trace_records().size() == 5)) {
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
         debug.transactions.size() >= 1);
  return " sequence=arb/lock/response/virtual "
         "roles=agent/driver/monitor/scoreboard"
         " callback=6 transaction=5 cap=records";
}

} // namespace fsim::tests::app
