// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_sequence.hpp"

#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kAgentType{"work::agent"};
constexpr std::string_view kDriverType{"work::driver"};
constexpr std::string_view kMonitorType{"work::monitor"};
constexpr std::string_view kSubscriberType{"work::subscriber"};
constexpr std::string_view kScoreboardType{"work::scoreboard"};
constexpr std::string_view kSequencerType{"work::role_sequencer#(32)"};
constexpr std::string_view kSequenceType{"work::role_sequence#(32)"};
constexpr std::string_view kItemType{"work::role_item#(32)"};
constexpr std::string_view kAnalysisType{"work::observed_item#(32)"};

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

SystemVerilogClassDescriptor
class_descriptor(const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  if (specialization == kSequenceType) {
    result.declared_type = "uvm_pkg::uvm_sequence";
    result.assignable_declared_types = {
        std::string{kSequenceType}, "uvm_pkg::uvm_sequence",
        "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"};
  } else if (specialization == kItemType) {
    result.declared_type = "uvm_pkg::uvm_sequence_item";
    result.assignable_declared_types = {std::string{kItemType},
                                        "uvm_pkg::uvm_sequence_item",
                                        "uvm_pkg::uvm_object"};
  } else {
    result.declared_type = "uvm_pkg::uvm_component";
    result.assignable_declared_types = {std::string{specialization},
                                        "uvm_pkg::uvm_component",
                                        "uvm_pkg::uvm_object"};
    if (specialization == kSequencerType) {
      result.assignable_declared_types.push_back("uvm_pkg::uvm_sequencer");
    }
  }
  return result;
}

struct RoleFixture {
  SystemVerilogClassHeap heap{{1'024, 1U << 20U}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases;
  SystemVerilogUvmObjectionService objections;
  SystemVerilogUvmTlm1Service tlm1;
  SystemVerilogUvmResourcePoolService resources;
  SystemVerilogUvmConfigDbService configuration;
  SystemVerilogUvmSequenceService sequences;
  SystemVerilogUvmRootHandle active_root{};
  SystemVerilogUvmRootHandle passive_root{};
  SystemVerilogUvmStandardSchedule schedule;

  explicit RoleFixture(SystemVerilogUvmSequenceLimits limits = {})
      : objects(heap,
                [this](const std::string_view specialization,
                       const std::string_view, const std::string_view) {
                  return heap.allocate(class_descriptor(specialization));
                }),
        components(heap, objects), phases(components, scheduler),
        objections(objects, components, phases, scheduler),
        tlm1(heap, components), resources(&heap), configuration(resources),
        sequences(heap, objects, components, phases, objections,
                  std::move(limits)) {
    for (const auto type :
         {kAgentType, kDriverType, kMonitorType, kSubscriberType,
          kScoreboardType, kSequencerType, kSequenceType, kItemType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    active_root = components.create_root("active-root");
    passive_root = components.create_root("passive-root");
    const std::array roots{active_root, passive_root};
    schedule = phases.create_standard_schedule(roots);
    phases.set_objection_service(objections);
    tlm1.set_scheduler(scheduler);
    tlm1.set_phase_service(phases);
    sequences.set_role_services(tlm1, configuration);
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_object(const std::string_view type, std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_component(const std::string_view type, std::string name,
                 const SystemVerilogClassHandle parent,
                 const SystemVerilogUvmRootHandle root = {}) {
    const auto result = make_object(type, name);
    components.initialize(result, std::move(name), parent, root);
    return result;
  }

  void configure_agent(const SystemVerilogClassHandle agent,
                       const SystemVerilogUvmAgentMode mode,
                       const std::uint64_t unknown_mask = 0) {
    const auto snapshot = components.snapshot(agent);
    const SystemVerilogUvmConfigContext context{
        std::string{components.root_identity(snapshot.root)} + ":" +
            snapshot.full_name,
        snapshot.depth};
    (void)configuration.set(
        context, {}, "is_active",
        {"uvm_pkg::uvm_active_passive_enum",
         SystemVerilogUvmResourceValueKind::Packed, 1},
        PackedLogic4::from_aval_bval(
            1, mode == SystemVerilogUvmAgentMode::Active ? 1 : 0, unknown_mask),
        SystemVerilogUvmConfigPhase::Build);
  }

  [[nodiscard]] SystemVerilogUvmPhaseExecutionResult
  execute_function(const SystemVerilogUvmPhaseKind kind) {
    const auto phase = schedule.phase(kind);
    return phases.execute_function_phase(phase, [&](const auto component,
                                                    const auto callback_phase,
                                                    const auto callback) {
      sequences.dispatch_role_function(component, callback_phase, callback);
    });
  }

  [[nodiscard]] SystemVerilogUvmPhaseExecutionResult
  execute_task(const SystemVerilogUvmPhaseKind kind) {
    const auto phase = schedule.phase(kind);
    return phases.execute_task_phase(
        phase,
        [&](const auto component, const auto callback_phase,
            const auto callback) {
          sequences.dispatch_role_function(component, callback_phase, callback);
        },
        [&](const auto component, const auto callback_phase,
            const auto process) {
          return sequences
              .dispatch_role_task(component, callback_phase, process)
              .value_or(SystemVerilogUvmTaskPhaseStatus::Completed);
        });
  }
};

SystemVerilogUvmSequenceRoleDescriptor
agent_descriptor(const SystemVerilogClassHandle component,
                 const SystemVerilogUvmAgentMode mode,
                 std::size_t &function_calls) {
  SystemVerilogUvmSequenceRoleDescriptor result;
  result.component = component;
  result.kind = SystemVerilogUvmSequenceRoleKind::Agent;
  result.default_agent_mode = mode;
  result.function_dispatch = [&](const auto, const auto, const auto) {
    ++function_calls;
  };
  return result;
}

SystemVerilogUvmSequenceRoleDescriptor
monitor_descriptor(const SystemVerilogClassHandle component,
                   const SystemVerilogUvmSequenceRoleHandle agent,
                   std::size_t &function_calls, std::size_t &task_calls,
                   std::function<void()> body = {}) {
  SystemVerilogUvmSequenceRoleDescriptor result;
  result.component = component;
  result.kind = SystemVerilogUvmSequenceRoleKind::Monitor;
  result.agent = agent;
  result.analysis_type = std::string{kAnalysisType};
  result.function_dispatch = [&](const auto, const auto, const auto) {
    ++function_calls;
  };
  result.task_dispatch = [&, body = std::move(body)](const auto, const auto,
                                                     const auto) {
    ++task_calls;
    if (body)
      body();
    return SystemVerilogUvmTaskPhaseStatus::Completed;
  };
  return result;
}

SystemVerilogUvmSequenceRoleDescriptor
sink_descriptor(const SystemVerilogClassHandle component,
                const SystemVerilogUvmSequenceRoleKind kind,
                std::vector<std::uint64_t> &values) {
  SystemVerilogUvmSequenceRoleDescriptor result;
  result.component = component;
  result.kind = kind;
  result.analysis_type = std::string{kAnalysisType};
  result.analysis_dispatch = [&](SystemVerilogUvmTlm1Payload payload) {
    values.push_back(payload.value.low_word().aval);
  };
  return result;
}

} // namespace

void test_systemverilog_uvm_sequence_roles() {
  RoleFixture fixture;
  auto &service = fixture.sequences;
  std::size_t agent_functions{};
  std::size_t passive_agent_functions{};
  std::size_t driver_functions{};
  std::size_t driver_tasks{};
  std::size_t monitor_functions{};
  std::size_t monitor_tasks{};
  std::size_t passive_monitor_functions{};
  std::size_t passive_monitor_tasks{};
  std::vector<std::uint64_t> subscriber_values;
  std::vector<std::uint64_t> scoreboard_values;
  std::vector<std::uint64_t> passive_values;

  const auto active_agent_component =
      fixture.make_component(kSequencerType, "agent", 0, fixture.active_root);
  const auto driver_component =
      fixture.make_component(kDriverType, "driver", active_agent_component);
  const auto monitor_component =
      fixture.make_component(kMonitorType, "monitor", active_agent_component);
  const auto subscriber_component = fixture.make_component(
      kSubscriberType, "subscriber", 0, fixture.active_root);
  const auto scoreboard_component = fixture.make_component(
      kScoreboardType, "scoreboard", 0, fixture.active_root);
  const auto passive_agent_component =
      fixture.make_component(kAgentType, "agent", 0, fixture.passive_root);
  const auto passive_monitor_component =
      fixture.make_component(kMonitorType, "monitor", passive_agent_component);
  const auto passive_subscriber_component = fixture.make_component(
      kSubscriberType, "subscriber", 0, fixture.passive_root);

  const SystemVerilogUvmSequenceProfile profile{std::string{kItemType},
                                                std::string{kItemType}};
  const auto sequencer = service.register_sequencer(
      {active_agent_component, std::string{kSequencerType}, profile});
  const auto active_agent = service.register_role(
      agent_descriptor(active_agent_component,
                       SystemVerilogUvmAgentMode::Passive, agent_functions));
  const auto passive_agent = service.register_role(agent_descriptor(
      passive_agent_component, SystemVerilogUvmAgentMode::Active,
      passive_agent_functions));

  SystemVerilogUvmSequenceRoleHandle driver;
  SystemVerilogUvmSequenceTransactionHandle driven_transaction;
  SystemVerilogUvmSequenceRoleDescriptor driver_role;
  driver_role.component = driver_component;
  driver_role.kind = SystemVerilogUvmSequenceRoleKind::Driver;
  driver_role.agent = active_agent;
  driver_role.sequencer = sequencer;
  driver_role.automatic_objection = true;
  driver_role.function_dispatch = [&](const auto, const auto, const auto) {
    ++driver_functions;
  };
  driver_role.task_dispatch = [&](const auto role, const auto phase,
                                  const auto process) {
    ++driver_tasks;
    const auto acquired =
        service.driver_get_next_item(role, {phase, process, 0});
    require(acquired.status ==
                    SystemVerilogUvmSequenceAcquireStatus::Acquired &&
                acquired.transaction,
            "active UVM driver must acquire its sequencer request");
    driven_transaction = acquired.transaction->handle;
    service.driver_item_done(role, driven_transaction);
    return SystemVerilogUvmTaskPhaseStatus::Completed;
  };
  driver = service.register_role(std::move(driver_role));

  SystemVerilogUvmSequenceRoleHandle monitor;
  monitor = service.register_role(monitor_descriptor(
      monitor_component, active_agent, monitor_functions, monitor_tasks, [&] {
        auto payload = SystemVerilogUvmTlm1Payload{
            std::string{kAnalysisType},
            PackedLogic4::from_aval_bval(32, 0x1616, 0),
            0,
            fixture.active_root,
            {},
            0};
        const auto result =
            service.publish_monitor(monitor, std::move(payload));
        require(result.success() && result.deliveries.size() == 2,
                "UVM monitor publication must reach subscriber and scoreboard");
      }));
  const auto subscriber = service.register_role(sink_descriptor(
      subscriber_component, SystemVerilogUvmSequenceRoleKind::Subscriber,
      subscriber_values));
  const auto scoreboard = service.register_role(sink_descriptor(
      scoreboard_component, SystemVerilogUvmSequenceRoleKind::Scoreboard,
      scoreboard_values));

  SystemVerilogUvmSequenceRoleHandle passive_monitor;
  passive_monitor = service.register_role(monitor_descriptor(
      passive_monitor_component, passive_agent, passive_monitor_functions,
      passive_monitor_tasks, [&] {
        const auto result = service.publish_monitor(
            passive_monitor, {std::string{kAnalysisType},
                              PackedLogic4::from_aval_bval(32, 0x6060, 0),
                              0,
                              fixture.passive_root,
                              {},
                              0});
        require(result.success() && result.deliveries.size() == 1,
                "passive UVM monitor must remain operational");
      }));
  const auto passive_subscriber = service.register_role(sink_descriptor(
      passive_subscriber_component,
      SystemVerilogUvmSequenceRoleKind::Subscriber, passive_values));

  service.connect_analysis(monitor, subscriber);
  service.connect_analysis(monitor, scoreboard);
  service.connect_analysis(passive_monitor, passive_subscriber);
  require_error(
      "FSIM-UVM-SEQ-012",
      [&] { service.connect_analysis(monitor, passive_subscriber); },
      "UVM role analysis connectivity must reject cross-root sinks");
  require(fixture.tlm1.connection_count() == 3,
          "rejected UVM role connectivity must not mutate TLM1");

  fixture.configure_agent(active_agent_component,
                          SystemVerilogUvmAgentMode::Active);
  fixture.configure_agent(passive_agent_component,
                          SystemVerilogUvmAgentMode::Passive);
  const auto build = fixture.execute_function(SystemVerilogUvmPhaseKind::Build);
  const auto connect =
      fixture.execute_function(SystemVerilogUvmPhaseKind::Connect);
  require(build.success() && connect.success() &&
              service.role_snapshot(active_agent).agent_mode ==
                  SystemVerilogUvmAgentMode::Active &&
              service.role_snapshot(passive_agent).agent_mode ==
                  SystemVerilogUvmAgentMode::Passive &&
              service.role_snapshot(driver).state ==
                  SystemVerilogUvmSequenceRoleState::Connected &&
              service.role_snapshot(monitor).state ==
                  SystemVerilogUvmSequenceRoleState::Connected,
          "build/connect must resolve root-qualified agent configuration and "
          "roles");

  const auto sequence_object = fixture.make_object(kSequenceType, "sequence");
  const auto sequence = service.register_sequence({sequence_object,
                                                   "sequence",
                                                   std::string{kSequenceType},
                                                   profile,
                                                   {},
                                                   sequencer,
                                                   {}});
  const auto item_object = fixture.make_object(kItemType, "request");
  const auto item = service.register_item(
      {item_object, "request", std::string{kItemType},
       SystemVerilogUvmSequenceItemRole::Request, sequence, sequencer});
  const auto request = service.macro_send(item, 161);
  const auto run = fixture.execute_task(SystemVerilogUvmPhaseKind::Run);
  require(run.success() && driver_tasks == 1 && monitor_tasks == 1 &&
              passive_monitor_tasks == 1 &&
              service.transaction_snapshot(driven_transaction).request.handle ==
                  request &&
              service.transaction_snapshot(driven_transaction).state ==
                  SystemVerilogUvmSequenceTransactionState::Completed &&
              subscriber_values == std::vector<std::uint64_t>{0x1616} &&
              scoreboard_values == std::vector<std::uint64_t>{0x1616} &&
              passive_values == std::vector<std::uint64_t>{0x6060} &&
              fixture.objections.phase_quiescent(
                  fixture.schedule.phase(SystemVerilogUvmPhaseKind::Run)) &&
              service.role_snapshot(driver).task_dispatches == 1 &&
              service.role_snapshot(monitor).publications == 1,
          "driver, monitors, sinks, handshake, analysis, and objections must "
          "integrate");
  require(agent_functions != 0 && passive_agent_functions != 0 &&
              driver_functions != 0 && monitor_functions != 0 &&
              passive_monitor_functions != 0,
          "role function callbacks must dispatch through real phase callbacks");

  const auto late_driver_component = fixture.make_component(
      kDriverType, "late_driver", passive_agent_component);
  const auto late_sequencer_component = fixture.make_component(
      kSequencerType, "late_sequencer", passive_agent_component);
  const auto late_sequencer = service.register_sequencer(
      {late_sequencer_component, std::string{kSequencerType}, profile});
  SystemVerilogUvmSequenceRoleDescriptor late_driver_descriptor;
  late_driver_descriptor.component = late_driver_component;
  late_driver_descriptor.kind = SystemVerilogUvmSequenceRoleKind::Driver;
  late_driver_descriptor.agent = passive_agent;
  late_driver_descriptor.sequencer = late_sequencer;
  const auto late_driver =
      service.register_role(std::move(late_driver_descriptor));
  require_error(
      "FSIM-UVM-SEQ-012",
      [&] {
        service.dispatch_role_function(
            late_driver_component,
            fixture.schedule.phase(SystemVerilogUvmPhaseKind::Build),
            SystemVerilogUvmPhaseCallbackKind::Execute);
      },
      "passive UVM agents must reject driver build dispatch");
  require(service.role_snapshot(late_driver).state ==
              SystemVerilogUvmSequenceRoleState::Constructed,
          "rejected passive-driver build must preserve lifecycle state");
  service.release_role(late_driver);

  require_error(
      "FSIM-UVM-SEQ-005",
      [&] {
        (void)service.register_role(agent_descriptor(
            active_agent_component, SystemVerilogUvmAgentMode::Active,
            agent_functions));
      },
      "a UVM component must own at most one role");
  require_error(
      "FSIM-UVM-SEQ-012",
      [&] {
        auto malformed = sink_descriptor(
            scoreboard_component, SystemVerilogUvmSequenceRoleKind::Scoreboard,
            scoreboard_values);
        malformed.component = fixture.make_component(
            kScoreboardType, "malformed", 0, fixture.active_root);
        malformed.analysis_type.clear();
        (void)service.register_role(std::move(malformed));
      },
      "UVM analysis roles must reject empty nominal types");

  const auto passive_role_count = service.roles(fixture.passive_root).size();
  const auto endpoint_count = fixture.tlm1.endpoints().size();
  service.teardown_roles(fixture.passive_root);
  require(
      passive_role_count == 3 && service.roles(fixture.passive_root).empty() &&
          service.roles(fixture.active_root).size() == 5 &&
          fixture.tlm1.endpoints().size() == endpoint_count - 2 &&
          fixture.tlm1.connection_count() == 2 &&
          fixture.components.contains(passive_agent_component) &&
          !service.role_for_component(passive_agent_component),
      "role teardown must be leaf-first, endpoint-clean, and root isolated");
  require_error(
      "FSIM-UVM-SEQ-001", [&] { (void)service.role_snapshot(passive_monitor); },
      "released UVM role handles must become stale");
  fixture.components.destroy_root(fixture.passive_root);
  require(fixture.components.contains(active_agent_component) &&
              !fixture.components.contains_root(fixture.passive_root),
          "role cleanup must permit deterministic peer-isolated component "
          "teardown");

  std::vector<SystemVerilogUvmSequenceRoleHandle> teardown_order;
  for (const auto &event : service.role_events()) {
    if (event.root == fixture.passive_root &&
        event.kind == SystemVerilogUvmSequenceRoleEventKind::Teardown) {
      teardown_order.push_back(event.role);
    }
  }
  require(teardown_order ==
              std::vector<SystemVerilogUvmSequenceRoleHandle>{
                  passive_monitor, passive_agent, passive_subscriber},
          "multi-root UVM role teardown must retain deterministic leaf-first "
          "order");
}

void test_systemverilog_uvm_sequence_role_limits() {
  SystemVerilogUvmSequenceLimits one_role_limits;
  one_role_limits.maximum_roles = 1;
  RoleFixture one_role{one_role_limits};
  std::size_t calls{};
  const auto first =
      one_role.make_component(kAgentType, "first", 0, one_role.active_root);
  const auto second =
      one_role.make_component(kAgentType, "second", 0, one_role.active_root);
  (void)one_role.sequences.register_role(
      agent_descriptor(first, SystemVerilogUvmAgentMode::Active, calls));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)one_role.sequences.register_role(
            agent_descriptor(second, SystemVerilogUvmAgentMode::Active, calls));
      },
      "UVM role count ceilings must reject transactionally");
  require(one_role.sequences.role_count() == 1,
          "rejected UVM role registration must preserve role count");

  SystemVerilogUvmSequenceLimits dispatch_limits;
  dispatch_limits.maximum_role_dispatches = 1;
  RoleFixture dispatch_bounded{dispatch_limits};
  const auto agent_component = dispatch_bounded.make_component(
      kAgentType, "agent", 0, dispatch_bounded.active_root);
  const auto agent = dispatch_bounded.sequences.register_role(agent_descriptor(
      agent_component, SystemVerilogUvmAgentMode::Active, calls));
  const auto build =
      dispatch_bounded.schedule.phase(SystemVerilogUvmPhaseKind::Build);
  dispatch_bounded.sequences.dispatch_role_function(
      agent_component, build, SystemVerilogUvmPhaseCallbackKind::PhaseStarted);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        dispatch_bounded.sequences.dispatch_role_function(
            agent_component, build, SystemVerilogUvmPhaseCallbackKind::Execute);
      },
      "UVM source-level role dispatch must be bounded");
  require(dispatch_bounded.sequences.role_snapshot(agent).state ==
              SystemVerilogUvmSequenceRoleState::Constructed,
          "bounded role dispatch rejection must preserve state");

  SystemVerilogUvmSequenceLimits publication_limits;
  publication_limits.maximum_role_publications = 1;
  RoleFixture publication_bounded{publication_limits};
  std::size_t monitor_functions{};
  std::size_t monitor_tasks{};
  std::vector<std::uint64_t> received;
  const auto publication_agent_component = publication_bounded.make_component(
      kAgentType, "agent", 0, publication_bounded.active_root);
  const auto publication_agent = publication_bounded.sequences.register_role(
      agent_descriptor(publication_agent_component,
                       SystemVerilogUvmAgentMode::Active, calls));
  const auto monitor_component = publication_bounded.make_component(
      kMonitorType, "monitor", publication_agent_component);
  const auto sink_component = publication_bounded.make_component(
      kSubscriberType, "subscriber", 0, publication_bounded.active_root);
  const auto monitor = publication_bounded.sequences.register_role(
      monitor_descriptor(monitor_component, publication_agent,
                         monitor_functions, monitor_tasks));
  const auto sink = publication_bounded.sequences.register_role(sink_descriptor(
      sink_component, SystemVerilogUvmSequenceRoleKind::Subscriber, received));
  publication_bounded.sequences.connect_analysis(monitor, sink);
  require(publication_bounded.execute_function(SystemVerilogUvmPhaseKind::Build)
                  .success() &&
              publication_bounded
                  .execute_function(SystemVerilogUvmPhaseKind::Connect)
                  .success(),
          "bounded publication fixture must reach connected state");
  const auto payload = [] {
    return SystemVerilogUvmTlm1Payload{std::string{kAnalysisType},
                                       PackedLogic4::from_aval_bval(32, 1, 0),
                                       0,
                                       0,
                                       {},
                                       0};
  };
  require(publication_bounded.sequences.publish_monitor(monitor, payload())
              .success(),
          "first bounded UVM role publication must succeed");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)publication_bounded.sequences.publish_monitor(monitor, payload());
      },
      "UVM role publication ceilings must reject before fanout");
  require(received == std::vector<std::uint64_t>{1},
          "rejected bounded publication must have no subscriber side effect");

  for (const auto zero_member : {0U, 1U, 2U, 3U, 4U, 5U, 6U}) {
    auto limits = SystemVerilogUvmSequenceLimits{};
    switch (zero_member) {
    case 0:
      limits.maximum_roles = 0;
      break;
    case 1:
      limits.maximum_roles_per_root = 0;
      break;
    case 2:
      limits.maximum_role_dispatches = 0;
      break;
    case 3:
      limits.maximum_role_failures = 0;
      break;
    case 4:
      limits.maximum_role_events = 0;
      break;
    case 5:
      limits.maximum_analysis_connections_per_monitor = 0;
      break;
    case 6:
      limits.maximum_role_publications = 0;
      break;
    default:
      break;
    }
    bool rejected{};
    try {
      RoleFixture invalid{limits};
    } catch (const SystemVerilogUvmSequenceError &error) {
      rejected = error.diagnostic_code() == "FSIM-UVM-SEQ-006";
    }
    require(rejected, "all UVM sequence role limits must be nonzero");
  }
}

} // namespace fsim::tests::runtime
