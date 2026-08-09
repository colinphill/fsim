// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kVirtualSequencerType{"work::virtual_sequencer"};
constexpr std::string_view kASequencerType{"work::a_sequencer#(a_item)"};
constexpr std::string_view kBSequencerType{"work::b_sequencer#(b_item)"};
constexpr std::string_view kVirtualSequenceType{"work::virtual_sequence"};
constexpr std::string_view kASequenceType{"work::a_sequence#(a_item)"};
constexpr std::string_view kBSequenceType{"work::b_sequence#(b_item)"};
constexpr std::string_view kARequestType{"work::a_request"};
constexpr std::string_view kAResponseType{"work::a_response"};
constexpr std::string_view kBRequestType{"work::b_request"};
constexpr std::string_view kBResponseType{"work::b_response"};

void require(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

void require_error(const std::string_view code,
                   const std::function<void()> &operation,
                   const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmSequenceError &error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogUvmSequenceProfile a_profile() {
  return {std::string{kARequestType}, std::string{kAResponseType}};
}

SystemVerilogUvmSequenceProfile b_profile() {
  return {std::string{kBRequestType}, std::string{kBResponseType}};
}

SystemVerilogUvmSequenceProfile virtual_profile() {
  return {"uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_sequence_item"};
}

SystemVerilogClassDescriptor
class_descriptor(const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  if (specialization == kVirtualSequencerType ||
      specialization == kASequencerType || specialization == kBSequencerType) {
    result.declared_type = "uvm_pkg::uvm_sequencer";
    result.assignable_declared_types = {
        std::string{specialization}, "uvm_pkg::uvm_sequencer",
        "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"};
  } else if (specialization == kVirtualSequenceType ||
             specialization == kASequenceType ||
             specialization == kBSequenceType) {
    result.declared_type = "uvm_pkg::uvm_sequence";
    result.assignable_declared_types = {
        std::string{specialization}, "uvm_pkg::uvm_sequence",
        "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"};
  } else {
    result.declared_type = "uvm_pkg::uvm_sequence_item";
    result.assignable_declared_types = {std::string{specialization},
                                        "uvm_pkg::uvm_sequence_item",
                                        "uvm_pkg::uvm_object"};
  }
  return result;
}

struct VirtualFixture {
  SystemVerilogClassHeap heap{{1'024, 1U << 20U}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases;
  SystemVerilogUvmObjectionService objections;
  SystemVerilogUvmSequenceService sequences;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRootHandle peer_root{};
  SystemVerilogUvmStandardSchedule schedule;

  explicit VirtualFixture(SystemVerilogUvmSequenceLimits limits = {})
      : objects(heap,
                [this](const std::string_view specialization,
                       const std::string_view, const std::string_view) {
                  return heap.allocate(class_descriptor(specialization));
                }),
        components(heap, objects), phases(components, scheduler),
        objections(objects, components, phases, scheduler),
        sequences(heap, objects, components, phases, objections,
                  std::move(limits)) {
    for (const auto type :
         {kVirtualSequencerType, kASequencerType, kBSequencerType,
          kVirtualSequenceType, kASequenceType, kBSequenceType, kARequestType,
          kAResponseType, kBRequestType, kBResponseType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    root = components.create_root("virtual-root");
    peer_root = components.create_root("virtual-peer");
    const std::array roots{root, peer_root};
    schedule = phases.create_standard_schedule(roots);
    phases.set_objection_service(objections);
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_object(const std::string_view type, std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_component(const std::string_view type, std::string name,
                 const SystemVerilogUvmRootHandle selected_root) {
    const auto result = make_object(type, name);
    components.initialize(result, std::move(name), 0, selected_root);
    return result;
  }
};

SystemVerilogUvmSequencerDescriptor
sequencer_descriptor(const SystemVerilogClassHandle component, std::string type,
                     SystemVerilogUvmSequenceProfile profile) {
  return {component, std::move(type), std::move(profile)};
}

SystemVerilogUvmSequenceDescriptor
sequence_descriptor(const SystemVerilogClassHandle object, std::string name,
                    std::string type, SystemVerilogUvmSequenceProfile profile,
                    const SystemVerilogUvmSequencerHandle sequencer) {
  SystemVerilogUvmSequenceDescriptor result;
  result.object = object;
  result.name = std::move(name);
  result.nominal_type = std::move(type);
  result.profile = std::move(profile);
  result.sequencer = sequencer;
  return result;
}

SystemVerilogUvmSequenceItemDescriptor
response_descriptor(const SystemVerilogClassHandle object, std::string name,
                    const SystemVerilogUvmSequenceHandle sequence,
                    const SystemVerilogUvmSequencerHandle sequencer) {
  return {object,
          std::move(name),
          std::string{kAResponseType},
          SystemVerilogUvmSequenceItemRole::Response,
          sequence,
          sequencer};
}

struct VirtualTopology {
  SystemVerilogClassHandle virtual_component{};
  SystemVerilogClassHandle a_component{};
  SystemVerilogClassHandle b_component{};
  SystemVerilogUvmSequencerHandle virtual_sequencer;
  SystemVerilogUvmSequencerHandle a_sequencer;
  SystemVerilogUvmSequencerHandle b_sequencer;
};

VirtualTopology make_topology(VirtualFixture &fixture) {
  VirtualTopology result;
  result.a_component =
      fixture.make_component(kASequencerType, "a_sequencer", fixture.root);
  result.b_component =
      fixture.make_component(kBSequencerType, "b_sequencer", fixture.root);
  result.virtual_component = fixture.make_component(
      kVirtualSequencerType, "virtual_sequencer", fixture.root);
  result.a_sequencer =
      fixture.sequences.register_sequencer(sequencer_descriptor(
          result.a_component, std::string{kASequencerType}, a_profile()));
  result.b_sequencer =
      fixture.sequences.register_sequencer(sequencer_descriptor(
          result.b_component, std::string{kBSequencerType}, b_profile()));
  SystemVerilogUvmVirtualSequencerDescriptor descriptor;
  descriptor.component = result.virtual_component;
  descriptor.nominal_type = std::string{kVirtualSequencerType};
  descriptor.profile = virtual_profile();
  descriptor.domains = {{"a", result.a_sequencer, a_profile()},
                        {"b", result.b_sequencer, b_profile()}};
  result.virtual_sequencer =
      fixture.sequences.register_virtual_sequencer(std::move(descriptor));
  return result;
}

SystemVerilogUvmPhaseProcessSnapshot
process_snapshot(const SystemVerilogUvmPhaseExecutionResult &execution,
                 const SystemVerilogUvmPhaseProcessHandle handle) {
  const auto found =
      std::ranges::find(execution.final_processes, handle,
                        &SystemVerilogUvmPhaseProcessSnapshot::handle);
  require(found != execution.final_processes.end(),
          "virtual sequence process must remain in final phase evidence");
  return *found;
}

std::string exercise_virtual_limit(SystemVerilogUvmSequenceLimits limits,
                                   const bool two_steps,
                                   const bool reset_always) {
  VirtualFixture fixture{std::move(limits)};
  const auto topology = make_topology(fixture);
  SystemVerilogUvmSequenceHandle virtual_sequence;

  auto a_descriptor = sequence_descriptor(
      fixture.make_object(kASequenceType, "limit_a_sequence"),
      "limit_a_sequence", std::string{kASequenceType}, a_profile(),
      topology.a_sequencer);
  if (reset_always) {
    a_descriptor.hooks.body = [&](const auto) {
      fixture.sequences.request_virtual_reset(virtual_sequence, true);
    };
  }
  const auto a_sequence =
      fixture.sequences.register_sequence(std::move(a_descriptor));
  const auto b_sequence =
      fixture.sequences.register_sequence(sequence_descriptor(
          fixture.make_object(kBSequenceType, "limit_b_sequence"),
          "limit_b_sequence", std::string{kBSequenceType}, b_profile(),
          topology.b_sequencer));

  std::string diagnostic;
  auto virtual_descriptor = sequence_descriptor(
      fixture.make_object(kVirtualSequenceType, "limit_virtual_sequence"),
      "limit_virtual_sequence", std::string{kVirtualSequenceType},
      virtual_profile(), topology.virtual_sequencer);
  virtual_descriptor.hooks.body = [&](const auto handle) {
    std::vector<SystemVerilogUvmVirtualSequenceStep> steps{
        {"a", a_sequence, 100}};
    if (two_steps)
      steps.push_back({"b", b_sequence, 200});
    try {
      (void)fixture.sequences.coordinate_virtual(handle, steps);
    } catch (const SystemVerilogUvmSequenceError &error) {
      diagnostic = error.diagnostic_code();
    }
  };
  virtual_sequence = fixture.sequences.register_virtual_sequence(
      std::move(virtual_descriptor));
  const auto run = fixture.schedule.phase(SystemVerilogUvmPhaseKind::Run);
  (void)fixture.phases.execute_task_phase(
      run, [](const auto, const auto, const auto) {},
      [&](const auto component, const auto phase, const auto process) {
        if (component == topology.virtual_component) {
          (void)fixture.sequences.start(virtual_sequence,
                                        {phase, process, true, false});
        }
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });
  require(fixture.sequences.requests(topology.a_sequencer).empty() &&
              fixture.sequences.requests(topology.b_sequencer).empty() &&
              fixture.sequences.access_requests(topology.a_sequencer).empty() &&
              fixture.sequences.access_requests(topology.b_sequencer).empty(),
          "virtual limit rejection must not leak requests or access state");
  return diagnostic;
}

} // namespace

void test_systemverilog_uvm_virtual_sequences() {
  VirtualFixture fixture;
  const auto topology = make_topology(fixture);
  auto &sequences = fixture.sequences;

  std::vector<std::uint32_t> priorities;
  std::vector<SystemVerilogUvmPhaseProcessHandle> a_processes;
  SystemVerilogUvmPhaseProcessHandle b_process;
  std::vector<SystemVerilogUvmSequenceTransactionHandle> a_transactions;
  SystemVerilogUvmSequenceTransactionHandle b_transaction;
  std::size_t a_calls{};
  SystemVerilogUvmSequenceHandle virtual_sequence;
  SystemVerilogUvmSequenceItemHandle a_response;
  SystemVerilogUvmVirtualSequenceResult virtual_result;

  auto a_descriptor = sequence_descriptor(
      fixture.make_object(kASequenceType, "a_sequence"), "a_sequence",
      std::string{kASequenceType}, a_profile(), topology.a_sequencer);
  a_descriptor.hooks.body = [&](const auto child) {
    ++a_calls;
    a_processes.push_back(sequences.snapshot(child).process);
    const auto acquired = sequences.get_next_item(
        topology.a_sequencer, {sequences.snapshot(child).phase,
                               sequences.snapshot(child).process, 0});
    require(acquired.transaction.has_value(),
            "virtual A child must acquire its coordinated request");
    priorities.push_back(acquired.transaction->request.priority);
    a_transactions.push_back(acquired.transaction->handle);
    (void)sequences.route_response(a_response);
    if (a_calls == 1) {
      sequences.request_virtual_reset(virtual_sequence, true);
    } else {
      sequences.item_done(acquired.transaction->handle);
    }
  };
  const auto a_sequence = sequences.register_sequence(std::move(a_descriptor));
  a_response = sequences.register_item(
      response_descriptor(fixture.make_object(kAResponseType, "a_response"),
                          "response", a_sequence, topology.a_sequencer));

  auto b_descriptor = sequence_descriptor(
      fixture.make_object(kBSequenceType, "b_sequence"), "b_sequence",
      std::string{kBSequenceType}, b_profile(), topology.b_sequencer);
  b_descriptor.hooks.body = [&](const auto child) {
    b_process = sequences.snapshot(child).process;
    const auto acquired = sequences.get_next_item(
        topology.b_sequencer, {sequences.snapshot(child).phase,
                               sequences.snapshot(child).process, 0});
    require(acquired.transaction.has_value(),
            "virtual B child must acquire its coordinated request");
    priorities.push_back(acquired.transaction->request.priority);
    b_transaction = acquired.transaction->handle;
    sequences.item_done(b_transaction);
  };
  const auto b_sequence = sequences.register_sequence(std::move(b_descriptor));

  auto virtual_descriptor = sequence_descriptor(
      fixture.make_object(kVirtualSequenceType, "virtual_sequence"),
      "virtual_sequence", std::string{kVirtualSequenceType}, virtual_profile(),
      topology.virtual_sequencer);
  virtual_descriptor.hooks.body = [&](const auto handle) {
    const std::array steps{
        SystemVerilogUvmVirtualSequenceStep{
            "a", a_sequence, 700, SystemVerilogUvmVirtualSequenceAccess::Lock,
            true, true},
        SystemVerilogUvmVirtualSequenceStep{
            "b", b_sequence, 900, SystemVerilogUvmVirtualSequenceAccess::Grab,
            true, true}};
    virtual_result = sequences.coordinate_virtual(handle, steps);
  };
  virtual_sequence =
      sequences.register_virtual_sequence(std::move(virtual_descriptor));

  SystemVerilogUvmSequenceExecutionResult outer_result;
  SystemVerilogUvmPhaseProcessHandle component_process;
  const auto run = fixture.schedule.phase(SystemVerilogUvmPhaseKind::Run);
  const auto phase_result = fixture.phases.execute_task_phase(
      run, [](const auto, const auto, const auto) {},
      [&](const auto component, const auto phase, const auto process) {
        if (component == topology.virtual_component) {
          component_process = process;
          outer_result =
              sequences.start(virtual_sequence, {phase, process, true, true});
        }
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });

  require(outer_result.success() && virtual_result.success() &&
              virtual_result.epochs == 2 && virtual_result.restarts == 1 &&
              virtual_result.children.size() == 3 && a_calls == 2 &&
              priorities == std::vector<std::uint32_t>{700, 700, 900},
          "virtual coordination must restart one reset epoch and preserve each "
          "typed child priority");
  require(
      virtual_result.children[0].execution.killed &&
          virtual_result.children[1].execution.success() &&
          virtual_result.children[2].execution.success() &&
          sequences.transaction_snapshot(a_transactions[0]).cancellation ==
              SystemVerilogUvmSequenceCancellationReason::VirtualReset &&
          sequences.transaction_snapshot(a_transactions[1]).state ==
              SystemVerilogUvmSequenceTransactionState::Completed &&
          sequences.transaction_snapshot(b_transaction).state ==
              SystemVerilogUvmSequenceTransactionState::Completed,
      "virtual reset must cancel only the active child transaction and allow "
      "the restarted sibling plan to complete");
  require(
      sequences.requests(topology.a_sequencer).empty() &&
          sequences.requests(topology.b_sequencer).empty() &&
          sequences.access_requests(topology.a_sequencer).empty() &&
          sequences.access_requests(topology.b_sequencer).empty() &&
          sequences.responses(a_sequence) ==
              std::vector<SystemVerilogUvmSequenceItemHandle>{a_response} &&
          sequences.snapshot(a_response).state ==
              SystemVerilogUvmSequenceItemState::Routed,
      "virtual coordination must leave no pending request, access, or reset "
      "response duplication");

  const auto virtual_process = process_snapshot(
      phase_result, sequences.snapshot(virtual_sequence).process);
  const auto first_a_process = process_snapshot(phase_result, a_processes[0]);
  const auto second_a_process = process_snapshot(phase_result, a_processes[1]);
  const auto final_b_process = process_snapshot(phase_result, b_process);
  require(virtual_process.parent ==
                  std::optional<SystemVerilogUvmPhaseProcessHandle>{
                      component_process} &&
              first_a_process.parent ==
                  std::optional<SystemVerilogUvmPhaseProcessHandle>{
                      virtual_process.handle} &&
              second_a_process.parent ==
                  std::optional<SystemVerilogUvmPhaseProcessHandle>{
                      virtual_process.handle} &&
              final_b_process.parent ==
                  std::optional<SystemVerilogUvmPhaseProcessHandle>{
                      virtual_process.handle} &&
              first_a_process.state ==
                  SystemVerilogUvmPhaseProcessState::Cancelled &&
              second_a_process.state ==
                  SystemVerilogUvmPhaseProcessState::Completed &&
              final_b_process.state ==
                  SystemVerilogUvmPhaseProcessState::Completed,
          "virtual child processes must be exact siblings beneath the virtual "
          "process across cancellation and restart");

  const auto raised = std::ranges::count(
      fixture.objections.trace(), SystemVerilogUvmObjectionEventKind::Raised,
      &SystemVerilogUvmObjectionEvent::kind);
  const auto dropped = std::ranges::count(
      fixture.objections.trace(), SystemVerilogUvmObjectionEventKind::Dropped,
      &SystemVerilogUvmObjectionEvent::kind);
  require(
      raised == dropped && raised == 4 &&
          std::ranges::any_of(
              virtual_result.events,
              [](const auto &event) {
                return event.kind ==
                       SystemVerilogUvmVirtualSequenceEventKind::ResetRequested;
              }) &&
          std::ranges::any_of(
              virtual_result.events,
              [](const auto &event) {
                return event.kind ==
                       SystemVerilogUvmVirtualSequenceEventKind::Restarted;
              }),
      "virtual and child automatic objections must balance with retained reset "
      "and restart events");

  require_error(
      "FSIM-UVM-SEQ-013",
      [&] { sequences.request_virtual_reset(virtual_sequence); },
      "inactive virtual sequences must reject reset");
  require_error(
      "FSIM-UVM-SEQ-013",
      [&] {
        const std::array steps{
            SystemVerilogUvmVirtualSequenceStep{"a", a_sequence, 100}};
        (void)sequences.coordinate_virtual(virtual_sequence, steps);
      },
      "virtual coordination must reject calls outside the active body");

  SystemVerilogUvmSequenceHandle killed_virtual;
  SystemVerilogUvmSequenceTransactionHandle killed_transaction;
  SystemVerilogUvmSequenceItemHandle killed_response;
  SystemVerilogUvmVirtualSequenceResult killed_virtual_result;
  SystemVerilogUvmSequenceExecutionResult killed_outer_result;
  SystemVerilogUvmPhaseProcessHandle killed_child_process;
  auto killed_child_descriptor = sequence_descriptor(
      fixture.make_object(kASequenceType, "killed_domain_sequence"),
      "killed_domain_sequence", std::string{kASequenceType}, a_profile(),
      topology.a_sequencer);
  killed_child_descriptor.hooks.body = [&](const auto child) {
    const auto child_snapshot = sequences.snapshot(child);
    killed_child_process = child_snapshot.process;
    const auto acquired = sequences.get_next_item(
        topology.a_sequencer,
        {child_snapshot.phase, child_snapshot.process, 0});
    require(acquired.transaction.has_value(),
            "killed virtual child must acquire its request");
    killed_transaction = acquired.transaction->handle;
    (void)sequences.route_response(killed_response);
    sequences.kill(killed_virtual);
  };
  const auto killed_child =
      sequences.register_sequence(std::move(killed_child_descriptor));
  killed_response = sequences.register_item(response_descriptor(
      fixture.make_object(kAResponseType, "killed_response"), "killed_response",
      killed_child, topology.a_sequencer));
  auto killed_virtual_descriptor = sequence_descriptor(
      fixture.make_object(kVirtualSequenceType, "killed_virtual"),
      "killed_virtual", std::string{kVirtualSequenceType}, virtual_profile(),
      topology.virtual_sequencer);
  killed_virtual_descriptor.hooks.body = [&](const auto handle) {
    const std::array steps{SystemVerilogUvmVirtualSequenceStep{
        "a", killed_child, 333, SystemVerilogUvmVirtualSequenceAccess::Grab,
        true, true}};
    killed_virtual_result = sequences.coordinate_virtual(handle, steps);
  };
  killed_virtual =
      sequences.register_virtual_sequence(std::move(killed_virtual_descriptor));
  const auto main = fixture.schedule.phase(SystemVerilogUvmPhaseKind::Main);
  const auto killed_phase_result = fixture.phases.execute_task_phase(
      main, [](const auto, const auto, const auto) {},
      [&](const auto component, const auto phase, const auto process) {
        if (component == topology.virtual_component) {
          killed_outer_result =
              sequences.start(killed_virtual, {phase, process, true, true});
        }
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });
  const auto killed_virtual_process = process_snapshot(
      killed_phase_result, sequences.snapshot(killed_virtual).process);
  const auto killed_child_process_snapshot =
      process_snapshot(killed_phase_result, killed_child_process);
  require(
      killed_outer_result.killed && killed_virtual_result.killed &&
          sequences.transaction_snapshot(killed_transaction).cancellation ==
              SystemVerilogUvmSequenceCancellationReason::SequenceKilled &&
          sequences.requests(topology.a_sequencer).empty() &&
          sequences.access_requests(topology.a_sequencer).empty() &&
          sequences.responses(killed_child).empty() &&
          sequences.snapshot(killed_response).state ==
              SystemVerilogUvmSequenceItemState::Owned &&
          killed_virtual_process.state ==
              SystemVerilogUvmPhaseProcessState::Cancelled &&
          killed_child_process_snapshot.state ==
              SystemVerilogUvmPhaseProcessState::Cancelled &&
          killed_child_process_snapshot.parent ==
              std::optional<SystemVerilogUvmPhaseProcessHandle>{
                  killed_virtual_process.handle},
      "killing a virtual sequence must cancel its exact active sibling subtree "
      "without leaking request, access, or response state");
}

void test_systemverilog_uvm_virtual_sequence_limits() {
  VirtualFixture constructor_fixture;
  for (std::size_t index = 0; index < 4; ++index) {
    auto limits = SystemVerilogUvmSequenceLimits{};
    if (index == 0)
      limits.maximum_virtual_domains_per_sequencer = 0;
    if (index == 1)
      limits.maximum_virtual_steps = 0;
    if (index == 2)
      limits.maximum_virtual_restarts = 0;
    if (index == 3)
      limits.maximum_virtual_events = 0;
    require_error(
        "FSIM-UVM-SEQ-006",
        [&] {
          SystemVerilogUvmSequenceService invalid{
              constructor_fixture.heap,       constructor_fixture.objects,
              constructor_fixture.components, constructor_fixture.phases,
              constructor_fixture.objections, limits};
        },
        "every virtual-sequence limit must reject zero");
  }

  auto domain_limits = SystemVerilogUvmSequenceLimits{};
  domain_limits.maximum_virtual_domains_per_sequencer = 1;
  VirtualFixture domain_fixture{domain_limits};
  const auto a_component = domain_fixture.make_component(
      kASequencerType, "limited_a", domain_fixture.root);
  const auto b_component = domain_fixture.make_component(
      kBSequencerType, "limited_b", domain_fixture.root);
  const auto virtual_component = domain_fixture.make_component(
      kVirtualSequencerType, "limited_virtual", domain_fixture.root);
  const auto a_sequencer =
      domain_fixture.sequences.register_sequencer(sequencer_descriptor(
          a_component, std::string{kASequencerType}, a_profile()));
  const auto b_sequencer =
      domain_fixture.sequences.register_sequencer(sequencer_descriptor(
          b_component, std::string{kBSequencerType}, b_profile()));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        SystemVerilogUvmVirtualSequencerDescriptor descriptor;
        descriptor.component = virtual_component;
        descriptor.nominal_type = std::string{kVirtualSequencerType};
        descriptor.profile = virtual_profile();
        descriptor.domains = {{"a", a_sequencer, a_profile()},
                              {"b", b_sequencer, b_profile()}};
        (void)domain_fixture.sequences.register_virtual_sequencer(
            std::move(descriptor));
      },
      "virtual domain count must reject before registering a virtual "
      "sequencer");
  require(!domain_fixture.sequences.snapshot(a_sequencer).parent_virtual &&
              !domain_fixture.sequences.snapshot(b_sequencer).parent_virtual,
          "virtual domain-limit rejection must preserve child ownership");

  VirtualFixture cross_root_fixture;
  const auto local_component = cross_root_fixture.make_component(
      kASequencerType, "local", cross_root_fixture.root);
  const auto peer_component = cross_root_fixture.make_component(
      kBSequencerType, "peer", cross_root_fixture.peer_root);
  const auto cross_virtual_component = cross_root_fixture.make_component(
      kVirtualSequencerType, "cross_virtual", cross_root_fixture.root);
  const auto local =
      cross_root_fixture.sequences.register_sequencer(sequencer_descriptor(
          local_component, std::string{kASequencerType}, a_profile()));
  const auto peer =
      cross_root_fixture.sequences.register_sequencer(sequencer_descriptor(
          peer_component, std::string{kBSequencerType}, b_profile()));
  require_error(
      "FSIM-UVM-SEQ-013",
      [&] {
        SystemVerilogUvmVirtualSequencerDescriptor descriptor;
        descriptor.component = cross_virtual_component;
        descriptor.nominal_type = std::string{kVirtualSequencerType};
        descriptor.profile = virtual_profile();
        descriptor.domains = {{"local", local, a_profile()},
                              {"peer", peer, b_profile()}};
        (void)cross_root_fixture.sequences.register_virtual_sequencer(
            std::move(descriptor));
      },
      "virtual sequencers must reject cross-root child domains");
  require(cross_root_fixture.sequences.sequencer_count() == 2,
          "cross-root virtual registration must roll back completely");

  VirtualFixture release_fixture;
  const auto release_topology = make_topology(release_fixture);
  require_error(
      "FSIM-UVM-SEQ-005",
      [&] { release_fixture.sequences.release(release_topology.a_sequencer); },
      "a live virtual sequencer must retain its child sequencers");
  release_fixture.sequences.release(release_topology.virtual_sequencer);
  require(!release_fixture.sequences.snapshot(release_topology.a_sequencer)
                  .parent_virtual &&
              !release_fixture.sequences.snapshot(release_topology.b_sequencer)
                   .parent_virtual,
          "virtual sequencer release must detach every typed child domain");
  release_fixture.sequences.release(release_topology.a_sequencer);
  release_fixture.sequences.release(release_topology.b_sequencer);

  auto step_limits = SystemVerilogUvmSequenceLimits{};
  step_limits.maximum_virtual_steps = 1;
  require(exercise_virtual_limit(step_limits, true, false) ==
              "FSIM-UVM-SEQ-006",
          "virtual step ceiling must reject a larger coordinated plan");
  auto event_limits = SystemVerilogUvmSequenceLimits{};
  event_limits.maximum_virtual_events = 11;
  require(exercise_virtual_limit(event_limits, false, false) ==
              "FSIM-UVM-SEQ-006",
          "virtual event ceiling must reject before an epoch starts");
  auto restart_limits = SystemVerilogUvmSequenceLimits{};
  restart_limits.maximum_virtual_restarts = 1;
  require(exercise_virtual_limit(restart_limits, false, true) ==
              "FSIM-UVM-SEQ-006",
          "virtual restart ceiling must cancel and reject the next epoch");
}

} // namespace fsim::tests::runtime
