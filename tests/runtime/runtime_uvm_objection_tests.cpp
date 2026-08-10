// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_objection.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename Callback>
bool rejects(const std::string_view code, Callback&& callback) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const SystemVerilogUvmObjectionError& error) {
    return error.diagnostic_code() == code;
  }
  return false;
}

struct ObjectionFixture {
  SystemVerilogClassHeap heap{{64, 1'024}};
  SystemVerilogUvmObjectService objects{
      heap,
      [](const std::string_view,
         const std::string_view,
         const std::string_view) {
        return SystemVerilogClassHandle{};
      }};
  SystemVerilogUvmComponentService components{heap, objects};
  SystemVerilogUvmPhaseService phases{components};
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};
  SystemVerilogUvmPhaseHandle run;
  SystemVerilogUvmPhaseHandle dormant;
  SystemVerilogUvmPhaseHandle first_only;

  ObjectionFixture() {
    SystemVerilogUvmObjectDescriptor component_type;
    component_type.specialization_identity = "work::objection_component";
    component_type.type_name = "objection_component";
    objects.register_type(std::move(component_type));
    SystemVerilogUvmObjectDescriptor object_type;
    object_type.specialization_identity = "work::objection_object";
    object_type.type_name = "objection_object";
    objects.register_type(std::move(object_type));

    first_root = components.create_root("first");
    second_root = components.create_root("second");
    const auto common = phases.create_domain(
        "common", SystemVerilogUvmDomainKind::Common);
    phases.participate(common, first_root);
    phases.participate(common, second_root);
    run = phases.create_standard_phase(common, SystemVerilogUvmPhaseKind::Run);
    dormant = phases.create_custom_phase(
        common, "dormant", SystemVerilogUvmPhaseExecutionKind::Task);
    const auto private_domain = phases.create_domain(
        "private", SystemVerilogUvmDomainKind::Custom);
    phases.participate(private_domain, first_root);
    first_only = phases.create_custom_phase(
        private_domain, "first_only", SystemVerilogUvmPhaseExecutionKind::Task);
  }

  [[nodiscard]] static SystemVerilogClassDescriptor descriptor(
      const bool component) {
    SystemVerilogClassDescriptor result;
    result.declared_type = component
        ? "uvm_pkg::uvm_component"
        : "uvm_pkg::uvm_object";
    result.dynamic_type = component
        ? "work::objection_component"
        : "work::objection_object";
    result.specialization_identity = result.dynamic_type;
    result.assignable_declared_types = {
        result.dynamic_type, "uvm_pkg::uvm_object"};
    if (component) {
      result.assignable_declared_types.push_back("uvm_pkg::uvm_component");
    }
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_component(
      std::string name,
      const SystemVerilogClassHandle parent = 0,
      const SystemVerilogUvmRootHandle root = 0) {
    const auto object = heap.allocate(descriptor(true));
    objects.initialize(object);
    components.initialize(object, std::move(name), parent, root);
    return object;
  }

  [[nodiscard]] SystemVerilogClassHandle make_object() {
    const auto object = heap.allocate(descriptor(false));
    objects.initialize(object);
    return object;
  }

  void start_run() {
    const auto result = phases.execute_task_phase(
        run,
        [](const SystemVerilogClassHandle,
           const SystemVerilogUvmPhaseHandle,
           const SystemVerilogUvmPhaseCallbackKind) {},
        [](const SystemVerilogClassHandle,
           const SystemVerilogUvmPhaseHandle,
           const SystemVerilogUvmPhaseProcessHandle) {
          return SystemVerilogUvmTaskPhaseStatus::Suspended;
        });
    require(
        result.final_state == SystemVerilogUvmPhaseState::Executing,
        "objection fixture run phase must remain executing");
    const auto private_result = phases.execute_task_phase(
        first_only,
        [](const SystemVerilogClassHandle,
           const SystemVerilogUvmPhaseHandle,
           const SystemVerilogUvmPhaseCallbackKind) {},
        [](const SystemVerilogClassHandle,
           const SystemVerilogUvmPhaseHandle,
           const SystemVerilogUvmPhaseProcessHandle) {
          return SystemVerilogUvmTaskPhaseStatus::Suspended;
        });
    require(
        private_result.final_state == SystemVerilogUvmPhaseState::Executing,
        "objection fixture private phase must remain executing");
  }
};

}  // namespace

void test_systemverilog_uvm_objection() {
  static_assert(
      kSystemVerilogUvmOwnershipContract.objections
      == SystemVerilogUvmStateScope::Simulation);

  ObjectionFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto child = fixture.make_component("child", top);
  const auto leaf = fixture.make_component("leaf", child);
  const auto peer = fixture.make_component("peer", top);
  const auto plain = fixture.make_object();
  fixture.start_run();

  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, fixture.phases};
  require(!objections.trace_enabled() && objections.trace_text().empty(),
          "objection trace output must be disabled by default");
  objections.set_trace_enabled(true);
  SystemVerilogUvmActivityService activity;
  objections.set_activity_service(activity);
  const auto leaf_source = objections.bind_source(leaf);
  const auto peer_source = objections.bind_source(peer);
  const auto plain_source = objections.bind_source(plain, fixture.first_root);
  const auto second_root_source = objections.bind_source(
      plain, fixture.second_root);
  require(
      leaf_source && plain_source
          && objections.all_dropped(fixture.run, fixture.first_root),
      "objection source identity and initial all-dropped state must be exact");

  std::vector<SystemVerilogClassHandle> callback_targets;
  std::vector<SystemVerilogUvmObjectionEventKind> callback_kinds;
  bool fail_child_drop{};
  objections.set_callback(
      [&](const SystemVerilogUvmObjectionEvent& event) {
        callback_targets.push_back(event.target);
        callback_kinds.push_back(event.kind);
        if (fail_child_drop
            && event.kind == SystemVerilogUvmObjectionEventKind::Dropped
            && event.target == child) {
          throw std::runtime_error{"contained objection callback"};
        }
      });

  const auto raised = objections.raise(fixture.run, leaf_source, "work", 2);
  const auto objection_snapshots = objections.snapshots();
  require(
      raised.success() && !raised.all_dropped && raised.events.size() == 4
          && callback_targets
              == std::vector<SystemVerilogClassHandle>{leaf, child, top, 0}
          && objections.source_count(fixture.run, leaf_source) == 2
          && objections.source_count(fixture.run, leaf_source, "work") == 2
          && objections.source_count(fixture.run, leaf_source, "work ") == 0
          && objections.propagated_count(
                 fixture.run, fixture.first_root, leaf) == 2
          && objections.propagated_count(
                 fixture.run, fixture.first_root, child) == 2
          && objections.propagated_count(
                 fixture.run, fixture.first_root, top) == 2
          && objections.propagated_count(
                 fixture.run, fixture.first_root) == 2
          && std::ranges::any_of(
              objection_snapshots, [&](const auto& snapshot) {
                return snapshot.kind
                           == SystemVerilogUvmObjectionSnapshotKind::
                               SourceDescription
                    && snapshot.root == fixture.first_root
                    && snapshot.source == leaf
                    && snapshot.description == "work"
                    && snapshot.count == 2;
              }),
      "raises must retain exact descriptions and propagate leaf-to-root counts");
  const auto initial_trace_text = objections.trace_text();
  require(
      initial_trace_text.starts_with(
          "UVM_OBJECTION_TRACE sequence=0 time=0 kind=raised operation=raise")
          && initial_trace_text.find("description=\"work\"")
              != std::string::npos,
      "enabled objection tracing must format stable ordered lifecycle fields");
  objections.set_trace_enabled(false);
  require(objections.trace_text().empty(),
          "disabled objection tracing must suppress formatted output");
  objections.set_trace_enabled(true);

  callback_targets.clear();
  const auto plain_raised = objections.raise(
      fixture.run, plain_source, "background", 3);
  require(
      plain_raised.events.size() == 1 && plain_raised.events.front().root_target
          && callback_targets == std::vector<SystemVerilogClassHandle>{0}
          && objections.propagated_count(
                 fixture.run, fixture.first_root) == 5,
      "noncomponent sources must contribute directly to their associated root");

  callback_targets.clear();
  const auto set_up = objections.set(fixture.run, leaf_source, "work", 5);
  require(
      set_up.events.size() == 4
          && set_up.events.front().operation
              == SystemVerilogUvmObjectionOperation::Set
          && set_up.events.front().kind
              == SystemVerilogUvmObjectionEventKind::Raised
          && set_up.events.front().amount == 3
          && objections.propagated_count(
                 fixture.run, fixture.first_root) == 8,
      "set must raise only the positive difference");

  fail_child_drop = true;
  callback_targets.clear();
  const auto dropped = objections.drop(fixture.run, leaf_source, "work", 2);
  fail_child_drop = false;
  require(
      dropped.events.size() == 4 && dropped.failures.size() == 1
          && dropped.failures.front().diagnostic_code == "FSIM-UVM-OBJ-004"
          && callback_targets
              == std::vector<SystemVerilogClassHandle>{leaf, child, top, 0}
          && objections.source_count(fixture.run, leaf_source, "work") == 3
          && objections.propagated_count(
                 fixture.run, fixture.first_root) == 6,
      "dropped callbacks must remain ordered and contain individual failures");

  const auto set_down = objections.set(fixture.run, leaf_source, "work", 0);
  require(
      set_down.events.size() == 4 && !set_down.all_dropped
          && objections.source_count(fixture.run, leaf_source) == 0
          && objections.propagated_count(
                 fixture.run, fixture.first_root, leaf) == 0
          && objections.propagated_count(
                 fixture.run, fixture.first_root) == 3,
      "set to zero must drop the exact local and propagated difference");

  const auto all_dropped = objections.drop(
      fixture.run, plain_source, "background", 3);
  require(
      all_dropped.all_dropped && all_dropped.events.size() == 3
          && all_dropped.events.front().kind
              == SystemVerilogUvmObjectionEventKind::Dropped
          && all_dropped.events[1].kind
              == SystemVerilogUvmObjectionEventKind::AllDropped
          && all_dropped.events[2].kind
              == SystemVerilogUvmObjectionEventKind::ReadyToEnd
          && all_dropped.events[1].sequence
              == all_dropped.events.front().sequence + 1
          && objections.all_dropped(fixture.run, fixture.first_root),
      "the root zero transition must emit deterministic all-dropped detection");
  require(
      std::ranges::any_of(activity.events(), [](const auto& event) {
        return event.kind == SystemVerilogUvmActivityKind::Objection
            && event.action == SystemVerilogUvmActivityAction::Raised;
      })
          && std::ranges::any_of(activity.events(), [](const auto& event) {
            return event.kind == SystemVerilogUvmActivityKind::Objection
                && event.action == SystemVerilogUvmActivityAction::Dropped;
          }),
      "objection activity must preserve raised-before-dropped publication");
  for (std::size_t index = 1; index < objections.trace().size(); ++index) {
    require(
        objections.trace()[index - 1].sequence + 1
            == objections.trace()[index].sequence,
        "objection trace sequence must be monotonic and gap free");
  }

  require(
      rejects("FSIM-UVM-OBJ-002", [&] {
        (void)objections.raise(fixture.run, leaf_source, "negative", -1);
      })
          && rejects("FSIM-UVM-OBJ-002", [&] {
            (void)objections.drop(fixture.run, leaf_source, "work", 1);
          })
          && rejects("FSIM-UVM-OBJ-002", [&] {
            (void)objections.raise(fixture.dormant, leaf_source, "late", 1);
          })
          && rejects("FSIM-UVM-OBJ-002", [&] {
            (void)objections.bind_source(leaf, fixture.second_root);
          })
          && rejects("FSIM-UVM-OBJ-002", [&] {
            (void)objections.raise(
                fixture.first_only, second_root_source, "wrong root", 1);
          }),
      "negative, underflow, dormant-phase, and phase/root mutations must reject");

  ObjectionFixture foreign;
  const auto foreign_component = foreign.make_component(
      "foreign", 0, foreign.first_root);
  foreign.start_run();
  SystemVerilogUvmObjectionService foreign_objections{
      foreign.objects, foreign.components, foreign.phases};
  const auto foreign_source = foreign_objections.bind_source(foreign_component);
  require(
      rejects("FSIM-UVM-OBJ-001", [&] {
        (void)objections.raise(fixture.run, foreign_source, "foreign", 1);
      })
          && rejects("FSIM-UVM-OBJ-001", [&] {
            (void)objections.raise(foreign.run, leaf_source, "foreign", 1);
          }),
      "cross-simulation source and phase handles must reject");

  const auto stale_object = fixture.make_object();
  const auto stale_source = objections.bind_source(
      stale_object, fixture.first_root);
  fixture.objects.erase(stale_object);
  (void)fixture.heap.release(stale_object);
  require(
      rejects("FSIM-UVM-OBJ-001", [&] {
        (void)objections.source_count(fixture.run, stale_source);
      }),
      "released objection source handles must reject as stale");

  SystemVerilogUvmObjectionLimits count_limits;
  count_limits.maximum_count = 3;
  SystemVerilogUvmObjectionService count_limited{
      fixture.objects, fixture.components, fixture.phases, count_limits};
  const auto count_source = count_limited.bind_source(peer);
  (void)count_limited.raise(fixture.run, count_source, "bounded", 3);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)count_limited.raise(fixture.run, count_source, "bounded", 1);
      })
          && rejects("FSIM-UVM-OBJ-003", [&] {
            (void)count_limited.set(fixture.run, count_source, "bounded", 4);
          })
          && count_limited.source_count(
                 fixture.run, count_source, "bounded") == 3,
      "overflowing raises and sets must reject before changing count state");

  SystemVerilogUvmObjectionLimits depth_limits;
  depth_limits.maximum_propagation_depth = 2;
  SystemVerilogUvmObjectionService depth_limited{
      fixture.objects, fixture.components, fixture.phases, depth_limits};
  const auto depth_source = depth_limited.bind_source(leaf);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)depth_limited.raise(fixture.run, depth_source, "deep", 1);
      }),
      "over-depth propagation must reject before publication");

  SystemVerilogUvmObjectionLimits source_limits;
  source_limits.maximum_sources = 1;
  SystemVerilogUvmObjectionService source_limited{
      fixture.objects, fixture.components, fixture.phases, source_limits};
  const auto source_leaf = source_limited.bind_source(leaf);
  const auto source_peer = source_limited.bind_source(peer);
  (void)source_limited.raise(fixture.run, source_leaf, "one", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)source_limited.raise(fixture.run, source_peer, "two", 1);
      })
          && source_limited.source_count(fixture.run, source_leaf) == 1
          && source_limited.source_count(fixture.run, source_peer) == 0,
      "excess objection source state must reject transactionally");

  SystemVerilogUvmObjectionLimits description_limits;
  description_limits.maximum_descriptions = 1;
  SystemVerilogUvmObjectionService description_limited{
      fixture.objects, fixture.components, fixture.phases,
      description_limits};
  const auto description_source = description_limited.bind_source(leaf);
  (void)description_limited.raise(
      fixture.run, description_source, "one", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)description_limited.raise(
            fixture.run, description_source, "two", 1);
      }),
      "excess distinct objection descriptions must reject transactionally");

  SystemVerilogUvmObjectionLimits entry_limits;
  entry_limits.maximum_entries = 1;
  SystemVerilogUvmObjectionService entry_limited{
      fixture.objects, fixture.components, fixture.phases, entry_limits};
  const auto entry_source = entry_limited.bind_source(leaf);
  (void)entry_limited.raise(fixture.run, entry_source, "one", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)entry_limited.raise(fixture.run, entry_source, "two", 1);
      }),
      "excess objection entries must reject transactionally");

  SystemVerilogUvmObjectionLimits bytes_limits;
  bytes_limits.maximum_description_bytes = 4;
  SystemVerilogUvmObjectionService bytes_limited{
      fixture.objects, fixture.components, fixture.phases, bytes_limits};
  const auto bytes_source = bytes_limited.bind_source(leaf);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)bytes_limited.raise(
            fixture.run, bytes_source, "second", 1);
      }),
      "overlong objection descriptions must reject transactionally");

  SystemVerilogUvmObjectionLimits fanout_limits;
  fanout_limits.maximum_callbacks_per_operation = 2;
  SystemVerilogUvmObjectionService fanout_limited{
      fixture.objects, fixture.components, fixture.phases, fanout_limits};
  const auto fanout_source = fanout_limited.bind_source(leaf);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)fanout_limited.raise(fixture.run, fanout_source, "fanout", 1);
      })
          && fanout_limited.source_count(fixture.run, fanout_source) == 0,
      "excess callback fanout must reject transactionally");

  SystemVerilogUvmObjectionLimits trace_limits;
  trace_limits.maximum_trace_records = 2;
  SystemVerilogUvmObjectionService trace_limited{
      fixture.objects, fixture.components, fixture.phases, trace_limits};
  const auto trace_source = trace_limited.bind_source(
      plain, fixture.first_root);
  (void)trace_limited.raise(fixture.run, trace_source, "trace", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)trace_limited.drop(fixture.run, trace_source, "trace", 1);
      })
          && trace_limited.source_count(
                 fixture.run, trace_source, "trace") == 1,
      "trace exhaustion must reject a drop before mutating counts");

  SystemVerilogUvmObjectionLimits output_limits;
  output_limits.maximum_trace_output_bytes = 8;
  SystemVerilogUvmObjectionService output_limited{
      fixture.objects, fixture.components, fixture.phases, output_limits};
  const auto output_source = output_limited.bind_source(
      plain, fixture.first_root);
  output_limited.set_trace_enabled(true);
  (void)output_limited.raise(
      fixture.run, output_source, "line\n\"quoted", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)output_limited.trace_text();
      })
          && output_limited.source_count(
                 fixture.run, output_source) == 1,
      "objection trace output exhaustion must diagnose without changing "
      "published counts");

  SystemVerilogUvmObjectionService escaped_trace{
      fixture.objects, fixture.components, fixture.phases};
  const auto escaped_source = escaped_trace.bind_source(
      plain, fixture.first_root);
  escaped_trace.set_trace_enabled(true);
  (void)escaped_trace.raise(
      fixture.run, escaped_source, "line\n\"quoted", 1);
  require(
      escaped_trace.trace_text().find(
          "description=\"line\\x0a\\\"quoted\"") != std::string::npos,
      "objection trace descriptions must escape controls and quotes stably");

  SystemVerilogUvmObjectionLimits mutation_limits;
  mutation_limits.maximum_mutations = 1;
  SystemVerilogUvmObjectionService mutation_limited{
      fixture.objects, fixture.components, fixture.phases, mutation_limits};
  const auto mutation_source = mutation_limited.bind_source(
      plain, fixture.first_root);
  (void)mutation_limited.raise(fixture.run, mutation_source, "mutation", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)mutation_limited.drop(
            fixture.run, mutation_source, "mutation", 1);
      })
          && mutation_limited.source_count(
                 fixture.run, mutation_source) == 1,
      "mutation exhaustion must preserve the previously published state");

  objections.clear_trace();
  require(
      objections.trace().empty() && objections.mutation_count() == 6,
      "objection traces must be independently clearable without changing state");
}

void test_systemverilog_uvm_objection_drains() {
  ObjectionFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto leaf = fixture.make_component("leaf", top);
  const auto other_root = fixture.make_component(
      "other", 0, fixture.second_root);
  const auto plain = fixture.make_object();
  fixture.start_run();

  Scheduler scheduler;
  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, fixture.phases, scheduler};
  SystemVerilogUvmActivityService activity{scheduler};
  objections.set_activity_service(activity);
  const auto leaf_source = objections.bind_source(leaf);
  const auto plain_source = objections.bind_source(plain, fixture.first_root);
  const auto other_source = objections.bind_source(other_root);
  objections.set_drain_time(fixture.run, leaf_source, 5);
  objections.set_drain_time(fixture.dormant, leaf_source, 2);
  objections.set_drain_time(fixture.run, plain_source, 3);
  objections.set_drain_time(fixture.run, other_source, 4);
  require(
      objections.drain_time(fixture.run, leaf_source) == 5
          && objections.drain_time(fixture.dormant, leaf_source) == 2
          && objections.drain_time(fixture.run, plain_source) == 3,
      "drain time must retain exact per-source and per-phase identity");

  std::vector<std::pair<SystemVerilogUvmRootHandle, SimulationTick>>
      all_dropped_callbacks;
  std::vector<std::pair<SystemVerilogUvmRootHandle, SimulationTick>>
      ready_callbacks;
  objections.set_all_dropped_callback(
      [&](const SystemVerilogUvmObjectionEvent& event) {
        all_dropped_callbacks.emplace_back(event.root, event.time);
      });
  objections.set_ready_to_end_callback(
      [&](const SystemVerilogUvmObjectionEvent& event) {
        ready_callbacks.emplace_back(event.root, event.time);
      });

  (void)objections.raise(fixture.run, leaf_source, "leaf", 1);
  (void)objections.raise(fixture.run, plain_source, "plain", 1);
  (void)objections.raise(fixture.run, other_source, "other", 1);
  const auto leaf_drop = objections.drop(
      fixture.run, leaf_source, "leaf", 1);
  const auto plain_drop = objections.drop(
      fixture.run, plain_source, "plain", 1);
  const auto other_drop = objections.drop(
      fixture.run, other_source, "other", 1);
  const auto drain_snapshots = objections.drain_snapshots();
  require(
      !leaf_drop.all_dropped && !plain_drop.all_dropped
          && !other_drop.all_dropped
          && objections.pending_drain_count() == 3
          && objections.has_pending_drain(fixture.run, leaf_source)
          && objections.has_pending_drain(fixture.run, plain_source)
          && objections.has_pending_drain(fixture.run, other_source)
          && std::ranges::any_of(
              drain_snapshots, [&](const auto& snapshot) {
                return snapshot.root == fixture.first_root
                    && snapshot.source == leaf && snapshot.delay == 5
                    && snapshot.pending && snapshot.due == 5;
              })
          && all_dropped_callbacks.empty() && ready_callbacks.empty(),
      "nonzero drains must delay all-dropped and ready-to-end callbacks");

  require(
      scheduler.run(2).status == RunStatus::time_limit
          && all_dropped_callbacks.empty(),
      "drain callbacks must not use wall-clock or fire before simulated time");
  require(
      scheduler.run(3).status == RunStatus::time_limit
          && objections.pending_drain_count() == 2
          && all_dropped_callbacks.empty(),
      "a completed source drain must still wait for sibling drains in its root");
  require(
      scheduler.run(4).status == RunStatus::time_limit
          && objections.pending_drain_count() == 1
          && all_dropped_callbacks
              == std::vector<std::pair<SystemVerilogUvmRootHandle,
                                      SimulationTick>>{
                  {fixture.second_root, 4}}
          && ready_callbacks == all_dropped_callbacks,
      "independent roots must finalize at their own simulated drain time");
  require(
      scheduler.run().status == RunStatus::completed
          && scheduler.now() == 5 && objections.pending_drain_count() == 0
          && all_dropped_callbacks
              == std::vector<std::pair<SystemVerilogUvmRootHandle,
                                      SimulationTick>>{
                  {fixture.second_root, 4}, {fixture.first_root, 5}}
          && ready_callbacks == all_dropped_callbacks,
      "simultaneous source drains must gate root completion deterministically");
  const auto first_drain_started = std::ranges::find_if(
      activity.events(), [](const auto& event) {
        return event.kind == SystemVerilogUvmActivityKind::Drain
            && event.action == SystemVerilogUvmActivityAction::Scheduled;
      });
  const auto first_drain_completed = std::ranges::find_if(
      activity.events(), [](const auto& event) {
        return event.kind == SystemVerilogUvmActivityKind::Drain
            && event.action == SystemVerilogUvmActivityAction::Completed;
      });
  require(
      first_drain_started != activity.events().end()
          && first_drain_completed != activity.events().end()
          && first_drain_started->sequence < first_drain_completed->sequence,
      "drain activity must publish scheduling before simulated completion");

  (void)objections.raise(fixture.run, leaf_source, "restart", 1);
  (void)objections.drop(fixture.run, leaf_source, "restart", 1);
  require(
      objections.has_pending_drain(fixture.run, leaf_source),
      "a final source drop must start its configured drain");
  require(
      scheduler.run(7).status == RunStatus::time_limit,
      "a pending drain must survive an intermediate time limit");
  const auto reraised = objections.raise(
      fixture.run, leaf_source, "restart", 1);
  require(
      !objections.has_pending_drain(fixture.run, leaf_source)
          && reraised.events.back().kind
              == SystemVerilogUvmObjectionEventKind::DrainCancelled,
      "a re-raise during drain must cancel the pending simulated-time task");
  (void)objections.drop(fixture.run, leaf_source, "restart", 1);
  require(
      objections.has_pending_drain(fixture.run, leaf_source)
          && scheduler.run(11).status == RunStatus::time_limit,
      "a later final drop must restart the complete drain interval");
  require(
      scheduler.run().status == RunStatus::completed
          && scheduler.now() == 12
          && !objections.has_pending_drain(fixture.run, leaf_source),
      "a restarted drain must complete from its new simulated-time origin");

  objections.set_drain_time(fixture.run, plain_source, 0);
  std::size_t all_reentries{};
  std::size_t ready_reentries{};
  objections.set_all_dropped_callback(
      [&](const SystemVerilogUvmObjectionEvent&) {
        ++all_reentries;
        if (all_reentries == 1) {
          (void)objections.raise(
              fixture.run, plain_source, "all reentry", 1);
        }
      });
  objections.set_ready_to_end_callback(
      [&](const SystemVerilogUvmObjectionEvent&) {
        ++ready_reentries;
        if (ready_reentries == 1) {
          (void)objections.raise(
              fixture.run, plain_source, "ready reentry", 1);
        }
      });
  (void)objections.raise(fixture.run, plain_source, "initial", 1);
  const auto first_zero = objections.drop(
      fixture.run, plain_source, "initial", 1);
  require(
      first_zero.all_dropped && all_reentries == 1 && ready_reentries == 0
          && objections.source_count(fixture.run, plain_source) == 1,
      "re-raise during all-dropped must suppress ready-to-end advancement");
  (void)objections.drop(fixture.run, plain_source, "all reentry", 1);
  require(
      all_reentries == 2 && ready_reentries == 1
          && objections.source_count(fixture.run, plain_source) == 1,
      "ready-to-end must re-enter after an all-dropped callback re-raise");
  (void)objections.drop(fixture.run, plain_source, "ready reentry", 1);
  require(
      all_reentries == 3 && ready_reentries == 2
          && objections.all_dropped(fixture.run, fixture.first_root),
      "a ready-to-end re-raise must force another stable all-dropped cycle");

  objections.set_all_dropped_callback(
      [](const SystemVerilogUvmObjectionEvent&) {
        throw std::runtime_error{"contained all-dropped callback"};
      });
  (void)objections.raise(fixture.run, plain_source, "failure", 1);
  (void)objections.drop(fixture.run, plain_source, "failure", 1);
  require(
      !objections.callback_failures().empty()
          && objections.callback_failures().back().diagnostic_code
              == "FSIM-UVM-OBJ-004",
      "all-dropped callback failures must be contained and retained");

  SystemVerilogUvmObjectionService no_scheduler{
      fixture.objects, fixture.components, fixture.phases};
  const auto no_scheduler_source = no_scheduler.bind_source(leaf);
  require(
      rejects("FSIM-UVM-OBJ-002", [&] {
        no_scheduler.set_drain_time(
            fixture.run, no_scheduler_source, 1);
      }),
      "nonzero drains without a simulation scheduler must reject");

  SystemVerilogUvmObjectionLimits setting_limits;
  setting_limits.maximum_drain_settings = 1;
  setting_limits.maximum_drain_time = 4;
  SystemVerilogUvmObjectionService setting_limited{
      fixture.objects, fixture.components, fixture.phases,
      scheduler, setting_limits};
  const auto setting_leaf = setting_limited.bind_source(leaf);
  const auto setting_plain = setting_limited.bind_source(
      plain, fixture.first_root);
  setting_limited.set_drain_time(fixture.run, setting_leaf, 4);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        setting_limited.set_drain_time(fixture.run, setting_plain, 1);
      })
          && rejects("FSIM-UVM-OBJ-003", [&] {
            setting_limited.set_drain_time(fixture.run, setting_leaf, 5);
          }),
      "drain setting and individual-delay ceilings must reject");

  SystemVerilogUvmObjectionLimits pending_limits;
  pending_limits.maximum_pending_drains = 1;
  SystemVerilogUvmObjectionService pending_limited{
      fixture.objects, fixture.components, fixture.phases,
      scheduler, pending_limits};
  const auto pending_leaf = pending_limited.bind_source(leaf);
  const auto pending_plain = pending_limited.bind_source(
      plain, fixture.first_root);
  pending_limited.set_drain_time(fixture.run, pending_leaf, 2);
  pending_limited.set_drain_time(fixture.run, pending_plain, 2);
  (void)pending_limited.raise(fixture.run, pending_leaf, "leaf", 1);
  (void)pending_limited.raise(fixture.run, pending_plain, "plain", 1);
  (void)pending_limited.drop(fixture.run, pending_leaf, "leaf", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)pending_limited.drop(
            fixture.run, pending_plain, "plain", 1);
      })
          && pending_limited.source_count(
                 fixture.run, pending_plain, "plain") == 1
          && pending_limited.propagated_count(
                 fixture.run, fixture.first_root) == 1,
      "pending-drain exhaustion must reject before changing count state");

  SystemVerilogUvmObjectionLimits aggregate_limits;
  aggregate_limits.maximum_aggregate_drain_time = 5;
  SystemVerilogUvmObjectionService aggregate_limited{
      fixture.objects, fixture.components, fixture.phases,
      scheduler, aggregate_limits};
  const auto aggregate_leaf = aggregate_limited.bind_source(leaf);
  const auto aggregate_plain = aggregate_limited.bind_source(
      plain, fixture.first_root);
  aggregate_limited.set_drain_time(fixture.run, aggregate_leaf, 4);
  aggregate_limited.set_drain_time(fixture.run, aggregate_plain, 4);
  (void)aggregate_limited.raise(fixture.run, aggregate_leaf, "leaf", 1);
  (void)aggregate_limited.raise(fixture.run, aggregate_plain, "plain", 1);
  (void)aggregate_limited.drop(fixture.run, aggregate_leaf, "leaf", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)aggregate_limited.drop(
            fixture.run, aggregate_plain, "plain", 1);
      })
          && aggregate_limited.source_count(
                 fixture.run, aggregate_plain) == 1,
      "aggregate drain delay must be bounded before mutation");

  SystemVerilogUvmObjectionLimits reserved_trace_limits;
  reserved_trace_limits.maximum_trace_records = 5;
  SystemVerilogUvmObjectionService trace_limited{
      fixture.objects, fixture.components, fixture.phases,
      scheduler, reserved_trace_limits};
  const auto trace_source = trace_limited.bind_source(
      plain, fixture.first_root);
  trace_limited.set_drain_time(fixture.run, trace_source, 1);
  (void)trace_limited.raise(fixture.run, trace_source, "trace", 1);
  require(
      rejects("FSIM-UVM-OBJ-003", [&] {
        (void)trace_limited.drop(fixture.run, trace_source, "trace", 1);
      })
          && trace_limited.source_count(fixture.run, trace_source) == 1,
      "future drain completion records must reserve bounded trace capacity");

  SystemVerilogUvmObjectionLimits reentry_limits;
  reentry_limits.maximum_callback_reentries = 1;
  SystemVerilogUvmObjectionService reentry_limited{
      fixture.objects, fixture.components, fixture.phases,
      scheduler, reentry_limits};
  const auto reentry_source = reentry_limited.bind_source(
      plain, fixture.first_root);
  reentry_limited.set_all_dropped_callback(
      [&](const SystemVerilogUvmObjectionEvent&) {
        (void)reentry_limited.raise(
            fixture.run, reentry_source, "loop", 1);
        (void)reentry_limited.drop(
            fixture.run, reentry_source, "loop", 1);
      });
  (void)reentry_limited.raise(fixture.run, reentry_source, "start", 1);
  (void)reentry_limited.drop(fixture.run, reentry_source, "start", 1);
  require(
      !reentry_limited.callback_failures().empty()
          && reentry_limited.callback_failures().back().diagnostic_code
              == "FSIM-UVM-OBJ-003",
      "all-dropped callback re-entry must stop at its deterministic ceiling");

  bool teardown_callback{};
  {
    SystemVerilogUvmObjectionService teardown{
        fixture.objects, fixture.components, fixture.phases, scheduler};
    const auto teardown_source = teardown.bind_source(
        plain, fixture.first_root);
    teardown.set_drain_time(fixture.run, teardown_source, 20);
    teardown.set_all_dropped_callback(
        [&](const SystemVerilogUvmObjectionEvent&) {
          teardown_callback = true;
        });
    (void)teardown.raise(fixture.run, teardown_source, "teardown", 1);
    (void)teardown.drop(fixture.run, teardown_source, "teardown", 1);
    require(
        teardown.pending_drain_count() == 1,
        "teardown proof must own one pending drain before destruction");
  }
  (void)scheduler.run();
  require(
      !teardown_callback,
      "objection destruction must cancel every pending scheduler callback");
}

}  // namespace fsim::tests::runtime
