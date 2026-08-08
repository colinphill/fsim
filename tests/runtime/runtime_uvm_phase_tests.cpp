// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_phase.hpp"

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
bool rejects(
    const std::string_view code,
    Callback&& callback) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const SystemVerilogUvmPhaseError& error) {
    return error.diagnostic_code() == code;
  }
  return false;
}

struct PhaseFixture {
  SystemVerilogClassHeap heap{{64, 1'024}};
  SystemVerilogUvmObjectService objects{
      heap,
      [](const std::string_view,
         const std::string_view,
         const std::string_view) {
        return SystemVerilogClassHandle{};
      }};
  SystemVerilogUvmComponentService components{heap, objects};

  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};
  SystemVerilogUvmRootHandle third_root{};

  PhaseFixture() {
    SystemVerilogUvmObjectDescriptor object_type;
    object_type.specialization_identity = "work::phase_component";
    object_type.type_name = "phase_component";
    object_type.fields = {
        {"payload", SystemVerilogUvmFieldFlag::None}};
    objects.register_type(std::move(object_type));
    first_root = components.create_root("first");
    second_root = components.create_root("second");
    third_root = components.create_root("third");
  }

  [[nodiscard]] static SystemVerilogClassDescriptor descriptor() {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_component";
    result.dynamic_type = "work::phase_component";
    result.specialization_identity = "work::phase_component";
    result.assignable_declared_types = {
        "work::phase_component", "uvm_pkg::uvm_component",
        "uvm_pkg::uvm_object"};
    result.properties = {
        {"payload", SystemVerilogClassPropertyKind::Logic4, 32}};
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_component(
      std::string name,
      const SystemVerilogClassHandle parent = 0,
      const SystemVerilogUvmRootHandle root = 0) {
    const auto result = heap.allocate(descriptor());
    objects.initialize(result);
    components.initialize(result, std::move(name), parent, root);
    return result;
  }
};

}  // namespace

void test_systemverilog_uvm_phase() {
  using namespace fsim::runtime;

  static_assert(
      static_cast<std::uint16_t>(SystemVerilogUvmPhaseState::Dormant) == 1U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Scheduled) == 2U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Syncing) == 4U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Started) == 8U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Executing) == 16U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::ReadyToEnd) == 32U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Ended) == 64U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Cleanup) == 128U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Done) == 256U
      && static_cast<std::uint16_t>(
             SystemVerilogUvmPhaseState::Jumping) == 512U);
  static_assert(
      kSystemVerilogUvmOwnershipContract.phases
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.component_paths
          == SystemVerilogUvmStateScope::Root);

  PhaseFixture fixture;
  SystemVerilogUvmPhaseLimits limits;
  limits.maximum_domains = 8;
  limits.maximum_phases = 32;
  limits.maximum_phases_per_domain = 16;
  limits.maximum_edges = 32;
  limits.maximum_roots_per_domain = 2;
  limits.maximum_depth = 4;
  limits.maximum_identity_bytes = 64;
  limits.maximum_traversal_work = 256;
  limits.maximum_mutations = 256;
  SystemVerilogUvmPhaseService phases{fixture.components, limits};

  const auto common = phases.create_domain(
      "common", SystemVerilogUvmDomainKind::Common);
  const auto runtime = phases.create_domain(
      "uvm", SystemVerilogUvmDomainKind::Runtime);
  const auto custom = phases.create_domain(
      "user", SystemVerilogUvmDomainKind::Custom);
  phases.participate(common, fixture.first_root);
  phases.participate(common, fixture.second_root);
  phases.participate(runtime, fixture.first_root);
  phases.participate(custom, fixture.second_root);

  const auto build = phases.create_standard_phase(
      common, SystemVerilogUvmPhaseKind::Build);
  const auto connect = phases.create_standard_phase(
      common, SystemVerilogUvmPhaseKind::Connect);
  const auto run = phases.create_standard_phase(
      common, SystemVerilogUvmPhaseKind::Run);
  const auto custom_function = phases.create_custom_phase(
      common, "audit", SystemVerilogUvmPhaseExecutionKind::Function, run);
  const auto pre_reset = phases.create_standard_phase(
      runtime, SystemVerilogUvmPhaseKind::PreReset);
  const auto reset = phases.create_standard_phase(
      runtime, SystemVerilogUvmPhaseKind::Reset);
  const auto custom_task = phases.create_custom_phase(
      custom, "user_task", SystemVerilogUvmPhaseExecutionKind::Task);

  phases.connect(build, connect);
  phases.connect(connect, run);
  phases.connect(run, custom_function);
  phases.connect(pre_reset, reset);

  require(
      phases.domains()
          == std::vector<SystemVerilogUvmDomainHandle>{
              common, runtime, custom}
      && phases.phases(common)
          == std::vector<SystemVerilogUvmPhaseHandle>{
              build, connect, run, custom_function}
      && phases.topological_order(common)
          == std::vector<SystemVerilogUvmPhaseHandle>{
              build, connect, run, custom_function},
      "UVM phase domains, standard nodes, custom nodes, and graph traversal "
      "must retain source registration order");
  const auto common_snapshot = phases.snapshot(common);
  const auto build_snapshot = phases.snapshot(build);
  const auto run_snapshot = phases.snapshot(run);
  const auto custom_snapshot = phases.snapshot(custom_function);
  require(
      common_snapshot.identity == "common"
          && common_snapshot.kind == SystemVerilogUvmDomainKind::Common
          && common_snapshot.roots
              == std::vector<SystemVerilogUvmRootHandle>{
                  fixture.first_root, fixture.second_root}
          && build_snapshot.identity == "build"
          && build_snapshot.kind == SystemVerilogUvmPhaseKind::Build
          && build_snapshot.execution
              == SystemVerilogUvmPhaseExecutionKind::Function
          && build_snapshot.state == SystemVerilogUvmPhaseState::Dormant
          && build_snapshot.successors
              == std::vector<SystemVerilogUvmPhaseHandle>{connect}
          && run_snapshot.identity == "run"
          && run_snapshot.execution
              == SystemVerilogUvmPhaseExecutionKind::Task
          && run_snapshot.predecessors
              == std::vector<SystemVerilogUvmPhaseHandle>{connect}
          && custom_snapshot.parent
              == std::optional<SystemVerilogUvmPhaseHandle>{run}
          && custom_snapshot.roots == common_snapshot.roots
          && phases.snapshot(custom_task).execution
              == SystemVerilogUvmPhaseExecutionKind::Task,
      "UVM phase snapshots must freeze kind, state, execution, parent, edge, "
      "domain, and per-root participation identity");

  require(
      systemverilog_uvm_phase_identity(
          SystemVerilogUvmPhaseKind::EndOfElaboration)
              == "end_of_elaboration"
          && systemverilog_uvm_phase_identity(
                 SystemVerilogUvmPhaseKind::PostShutdown)
              == "post_shutdown"
          && systemverilog_uvm_phase_domain_kind(
                 SystemVerilogUvmPhaseKind::Main)
              == SystemVerilogUvmDomainKind::Runtime
          && systemverilog_uvm_phase_execution_kind(
                 SystemVerilogUvmPhaseKind::Final)
              == SystemVerilogUvmPhaseExecutionKind::Function,
      "standard UVM common/runtime identities and execution kinds must remain "
      "stable");

  const auto edge_count = phases.edge_count();
  phases.disconnect(run, custom_function);
  require(
      phases.edge_count() + 1U == edge_count
          && phases.snapshot(run).successors.empty()
          && phases.snapshot(custom_function).predecessors.empty(),
      "UVM phase edge removal must update both directions atomically");
  phases.connect(run, custom_function);

  const auto mutation_before_negatives = phases.mutation_count();
  const auto phase_count_before_negatives = phases.phases(common).size();
  require(
      rejects("FSIM-UVM-PHASE-001", [&] {
        (void)phases.snapshot(SystemVerilogUvmPhaseHandle{});
      })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               (void)phases.create_domain(
                   "common", SystemVerilogUvmDomainKind::Custom);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               (void)phases.create_domain(
                   "another_common", SystemVerilogUvmDomainKind::Common);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               (void)phases.create_standard_phase(
                   common, SystemVerilogUvmPhaseKind::Build);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               (void)phases.create_custom_phase(
                   common, "build",
                   SystemVerilogUvmPhaseExecutionKind::Function);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               (void)phases.create_custom_phase(
                   common, "",
                   SystemVerilogUvmPhaseExecutionKind::Function);
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               (void)phases.create_standard_phase(
                   runtime, SystemVerilogUvmPhaseKind::Extract);
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.connect(build, build);
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.connect(custom_function, build);
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.connect(build, pre_reset);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               phases.connect(build, connect);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               phases.disconnect(build, run);
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               phases.participate(common, fixture.first_root);
             })
          && rejects("FSIM-UVM-PHASE-005", [&] {
               phases.participate(runtime, 999'999);
             })
          && rejects("FSIM-UVM-PHASE-004", [&] {
               phases.participate(common, fixture.third_root);
             }),
      "UVM phase diagnostics must distinguish invalid handles, duplicates, "
      "graph structure, roots, and resource ceilings");
  require(
      phases.mutation_count() == mutation_before_negatives
          && phases.phases(common).size() == phase_count_before_negatives
          && phases.edge_count() == edge_count,
      "failed UVM phase graph operations must not publish partial mutations");

  SystemVerilogUvmPhaseService peer{fixture.components, limits};
  const auto peer_common = peer.create_domain(
      "common", SystemVerilogUvmDomainKind::Common);
  require(
      peer_common != common
          && rejects("FSIM-UVM-PHASE-001", [&] {
               (void)peer.snapshot(common);
             })
          && rejects("FSIM-UVM-PHASE-001", [&] {
               (void)phases.snapshot(peer_common);
             }),
      "owner-token phase handles must reject across concurrent or restarted "
      "simulation services without process-global generation state");

  auto depth_limits = limits;
  depth_limits.maximum_depth = 1;
  SystemVerilogUvmPhaseService depth_service{
      fixture.components, depth_limits};
  const auto depth_domain = depth_service.create_domain(
      "depth", SystemVerilogUvmDomainKind::Custom);
  const auto depth_root = depth_service.create_custom_phase(
      depth_domain, "root", SystemVerilogUvmPhaseExecutionKind::Function);
  const auto depth_child = depth_service.create_custom_phase(
      depth_domain, "child", SystemVerilogUvmPhaseExecutionKind::Function,
      depth_root);
  const auto depth_mutations = depth_service.mutation_count();
  require(
      rejects("FSIM-UVM-PHASE-003", [&] {
        (void)depth_service.create_custom_phase(
            depth_domain, "too_deep",
            SystemVerilogUvmPhaseExecutionKind::Function, depth_child);
      })
          && depth_service.mutation_count() == depth_mutations
          && depth_service.phases(depth_domain).size() == 2,
      "UVM phase parent depth must reject before node publication");

  auto node_limits = limits;
  node_limits.maximum_phases = 1;
  SystemVerilogUvmPhaseService node_service{
      fixture.components, node_limits};
  const auto node_domain = node_service.create_domain(
      "nodes", SystemVerilogUvmDomainKind::Custom);
  (void)node_service.create_custom_phase(
      node_domain, "one", SystemVerilogUvmPhaseExecutionKind::Function);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)node_service.create_custom_phase(
            node_domain, "two",
            SystemVerilogUvmPhaseExecutionKind::Function);
      }),
      "the global UVM phase node ceiling must reject transactionally");

  auto edge_limits = limits;
  edge_limits.maximum_edges = 1;
  SystemVerilogUvmPhaseService edge_service{
      fixture.components, edge_limits};
  const auto edge_domain = edge_service.create_domain(
      "edges", SystemVerilogUvmDomainKind::Custom);
  const auto edge_a = edge_service.create_custom_phase(
      edge_domain, "a", SystemVerilogUvmPhaseExecutionKind::Function);
  const auto edge_b = edge_service.create_custom_phase(
      edge_domain, "b", SystemVerilogUvmPhaseExecutionKind::Function);
  const auto edge_c = edge_service.create_custom_phase(
      edge_domain, "c", SystemVerilogUvmPhaseExecutionKind::Function);
  edge_service.connect(edge_a, edge_b);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        edge_service.connect(edge_b, edge_c);
      })
          && edge_service.edge_count() == 1
          && edge_service.snapshot(edge_b).successors.empty(),
      "the UVM phase edge ceiling must reject without one-sided publication");

  auto traversal_limits = limits;
  traversal_limits.maximum_traversal_work = 3;
  SystemVerilogUvmPhaseService traversal_service{
      fixture.components, traversal_limits};
  const auto traversal_domain = traversal_service.create_domain(
      "walk", SystemVerilogUvmDomainKind::Custom);
  const auto traversal_a = traversal_service.create_custom_phase(
      traversal_domain, "a", SystemVerilogUvmPhaseExecutionKind::Function);
  const auto traversal_b = traversal_service.create_custom_phase(
      traversal_domain, "b", SystemVerilogUvmPhaseExecutionKind::Function);
  traversal_service.connect(traversal_a, traversal_b);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)traversal_service.topological_order(traversal_domain);
      }),
      "UVM phase traversal work must remain explicitly bounded");

  auto mutation_limits = limits;
  mutation_limits.maximum_mutations = 2;
  SystemVerilogUvmPhaseService mutation_service{
      fixture.components, mutation_limits};
  const auto mutation_domain = mutation_service.create_domain(
      "mutations", SystemVerilogUvmDomainKind::Custom);
  (void)mutation_service.create_custom_phase(
      mutation_domain, "one",
      SystemVerilogUvmPhaseExecutionKind::Function);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        mutation_service.participate(
            mutation_domain, fixture.first_root);
      })
          && mutation_service.mutation_count() == 2,
      "UVM phase mutation work must reject before changing graph state");

  auto identity_limits = limits;
  identity_limits.maximum_identity_bytes = 5;
  SystemVerilogUvmPhaseService identity_service{
      fixture.components, identity_limits};
  const auto identity_domain = identity_service.create_domain(
      "short", SystemVerilogUvmDomainKind::Custom);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)identity_service.create_custom_phase(
            identity_domain, "longer",
            SystemVerilogUvmPhaseExecutionKind::Function);
      }),
      "UVM phase identities must be bounded before allocation");
}

void test_systemverilog_uvm_standard_schedule() {
  using namespace fsim::runtime;

  PhaseFixture fixture;
  SystemVerilogUvmPhaseLimits limits;
  limits.maximum_domains = 12;
  limits.maximum_phases = 64;
  limits.maximum_phases_per_domain = 24;
  limits.maximum_edges = 128;
  limits.maximum_roots_per_domain = 4;
  limits.maximum_depth = 8;
  limits.maximum_identity_bytes = 64;
  limits.maximum_traversal_work = 2'048;
  limits.maximum_mutations = 512;
  SystemVerilogUvmPhaseService phases{fixture.components, limits};
  const std::array roots{fixture.first_root, fixture.second_root};
  const auto schedule = phases.create_standard_schedule(roots);

  const std::vector common_order{
      schedule.phase(SystemVerilogUvmPhaseKind::Build),
      schedule.phase(SystemVerilogUvmPhaseKind::Connect),
      schedule.phase(SystemVerilogUvmPhaseKind::EndOfElaboration),
      schedule.phase(SystemVerilogUvmPhaseKind::StartOfSimulation),
      schedule.phase(SystemVerilogUvmPhaseKind::Run),
      schedule.phase(SystemVerilogUvmPhaseKind::Extract),
      schedule.phase(SystemVerilogUvmPhaseKind::Check),
      schedule.phase(SystemVerilogUvmPhaseKind::Report),
      schedule.phase(SystemVerilogUvmPhaseKind::Final)};
  const std::vector runtime_order{
      schedule.phase(SystemVerilogUvmPhaseKind::PreReset),
      schedule.phase(SystemVerilogUvmPhaseKind::Reset),
      schedule.phase(SystemVerilogUvmPhaseKind::PostReset),
      schedule.phase(SystemVerilogUvmPhaseKind::PreConfigure),
      schedule.phase(SystemVerilogUvmPhaseKind::Configure),
      schedule.phase(SystemVerilogUvmPhaseKind::PostConfigure),
      schedule.phase(SystemVerilogUvmPhaseKind::PreMain),
      schedule.phase(SystemVerilogUvmPhaseKind::Main),
      schedule.phase(SystemVerilogUvmPhaseKind::PostMain),
      schedule.phase(SystemVerilogUvmPhaseKind::PreShutdown),
      schedule.phase(SystemVerilogUvmPhaseKind::Shutdown),
      schedule.phase(SystemVerilogUvmPhaseKind::PostShutdown)};
  require(
      phases.standard_schedule()
          == std::optional<SystemVerilogUvmStandardSchedule>{schedule}
          && phases.domains()
              == std::vector<SystemVerilogUvmDomainHandle>{
                  schedule.common_domain, schedule.runtime_domain}
          && phases.snapshot(schedule.common_domain).identity == "common"
          && phases.snapshot(schedule.runtime_domain).identity == "uvm"
          && phases.snapshot(schedule.common_domain).roots
              == std::vector<SystemVerilogUvmRootHandle>{
                  fixture.first_root, fixture.second_root}
          && phases.snapshot(schedule.runtime_domain).roots
              == phases.snapshot(schedule.common_domain).roots
          && phases.snapshot(schedule.runtime_domain).with_phase
              == std::optional<SystemVerilogUvmPhaseHandle>{
                  schedule.phase(SystemVerilogUvmPhaseKind::Run)}
          && phases.phases(schedule.common_domain) == common_order
          && phases.phases(schedule.runtime_domain) == runtime_order
          && phases.topological_order(schedule.common_domain) == common_order
          && phases.topological_order(schedule.runtime_domain) == runtime_order
          && !schedule.phase(SystemVerilogUvmPhaseKind::Custom),
      "the standard common/runtime UVM domains must retain exact identities, "
      "root participation, run-parallel placement, and linear phase order");

  const auto post_build = phases.insert_custom_phase(
      schedule.common_domain, "post_build_audit",
      SystemVerilogUvmPhaseExecutionKind::Function,
      SystemVerilogUvmPhasePlacement::after(
          schedule.phase(SystemVerilogUvmPhaseKind::Build)));
  const auto parallel_run = phases.insert_custom_phase(
      schedule.common_domain, "parallel_run",
      SystemVerilogUvmPhaseExecutionKind::Task,
      SystemVerilogUvmPhasePlacement::parallel_with(
          schedule.phase(SystemVerilogUvmPhaseKind::Run)));
  const auto check_audit = phases.insert_custom_phase(
      schedule.common_domain, "check_audit",
      SystemVerilogUvmPhaseExecutionKind::Function,
      SystemVerilogUvmPhasePlacement::between(
          schedule.phase(SystemVerilogUvmPhaseKind::Check),
          schedule.phase(SystemVerilogUvmPhaseKind::Report)));
  const auto pre_final = phases.insert_custom_phase(
      schedule.common_domain, "pre_final_audit",
      SystemVerilogUvmPhaseExecutionKind::Function,
      SystemVerilogUvmPhasePlacement::before(
          schedule.phase(SystemVerilogUvmPhaseKind::Final)));
  const auto runtime_tail = phases.insert_custom_phase(
      schedule.runtime_domain, "runtime_tail",
      SystemVerilogUvmPhaseExecutionKind::Task);
  const std::vector expected_common{
      schedule.phase(SystemVerilogUvmPhaseKind::Build),
      post_build,
      schedule.phase(SystemVerilogUvmPhaseKind::Connect),
      schedule.phase(SystemVerilogUvmPhaseKind::EndOfElaboration),
      schedule.phase(SystemVerilogUvmPhaseKind::StartOfSimulation),
      schedule.phase(SystemVerilogUvmPhaseKind::Run),
      parallel_run,
      schedule.phase(SystemVerilogUvmPhaseKind::Extract),
      schedule.phase(SystemVerilogUvmPhaseKind::Check),
      check_audit,
      schedule.phase(SystemVerilogUvmPhaseKind::Report),
      pre_final,
      schedule.phase(SystemVerilogUvmPhaseKind::Final)};
  auto expected_runtime = runtime_order;
  expected_runtime.push_back(runtime_tail);
  require(
      phases.topological_order(schedule.common_domain) == expected_common
          && phases.topological_order(schedule.runtime_domain)
              == expected_runtime
          && phases.snapshot(post_build).predecessors
              == std::vector<SystemVerilogUvmPhaseHandle>{
                  schedule.phase(SystemVerilogUvmPhaseKind::Build)}
          && phases.snapshot(post_build).successors
              == std::vector<SystemVerilogUvmPhaseHandle>{
                  schedule.phase(SystemVerilogUvmPhaseKind::Connect)}
          && phases.snapshot(parallel_run).predecessors
              == phases.snapshot(
                     schedule.phase(
                         SystemVerilogUvmPhaseKind::Run)).predecessors
          && phases.snapshot(parallel_run).successors
              == phases.snapshot(
                     schedule.phase(
                         SystemVerilogUvmPhaseKind::Run)).successors,
      "custom UVM insertion must support after, before, between, parallel, "
      "and source-ordered tail placement with deterministic rewiring");

  const auto producer = phases.create_domain(
      "producer", SystemVerilogUvmDomainKind::Custom);
  const auto consumer = phases.create_domain(
      "consumer", SystemVerilogUvmDomainKind::Custom);
  const auto producer_transfer = phases.create_custom_phase(
      producer, "transfer", SystemVerilogUvmPhaseExecutionKind::Task);
  const auto producer_flush = phases.create_custom_phase(
      producer, "flush", SystemVerilogUvmPhaseExecutionKind::Task);
  phases.connect(producer_transfer, producer_flush);
  const auto consumer_transfer = phases.create_custom_phase(
      consumer, "transfer", SystemVerilogUvmPhaseExecutionKind::Task);
  const auto consumer_flush = phases.create_custom_phase(
      consumer, "flush", SystemVerilogUvmPhaseExecutionKind::Task);
  phases.connect(consumer_transfer, consumer_flush);
  phases.synchronize_domains(producer, consumer);
  require(
      phases.snapshot(producer_transfer).synchronized
              == std::vector<SystemVerilogUvmPhaseHandle>{consumer_transfer}
          && phases.snapshot(producer_flush).synchronized
              == std::vector<SystemVerilogUvmPhaseHandle>{consumer_flush}
          && phases.snapshot(consumer_transfer).synchronized
              == std::vector<SystemVerilogUvmPhaseHandle>{producer_transfer},
      "whole-domain synchronization must pair matching phases two ways");
  const auto synchronized_edges = phases.edge_count();
  const auto synchronized_mutations = phases.mutation_count();
  require(
      rejects("FSIM-UVM-PHASE-002", [&] {
        phases.synchronize_domains(producer, consumer);
      })
          && phases.edge_count() == synchronized_edges
          && phases.mutation_count() == synchronized_mutations,
      "duplicate synchronization must reject without partial edges");
  phases.unsynchronize_domains(producer, consumer);
  require(
      phases.snapshot(producer_transfer).synchronized.empty()
          && phases.snapshot(consumer_flush).synchronized.empty()
          && rejects("FSIM-UVM-PHASE-002", [&] {
               phases.unsynchronize_domains(producer, consumer);
             }),
      "whole-domain unsynchronization must remove both sides atomically");
  phases.synchronize_domains(
      producer, consumer, producer_transfer, consumer_flush);
  require(
      phases.snapshot(producer_transfer).synchronized
          == std::vector<SystemVerilogUvmPhaseHandle>{consumer_flush},
      "explicit synchronization may pair differently named domain phases");
  phases.unsynchronize_domains(
      producer, consumer, producer_transfer, consumer_flush);

  require(!phases.snapshot(producer).with_phase,
          "custom UVM domains must remain independent until placed");
  phases.place_domain_with(
      producer, schedule.phase(SystemVerilogUvmPhaseKind::Main));
  require(
      phases.snapshot(producer).with_phase
          == std::optional<SystemVerilogUvmPhaseHandle>{
              schedule.phase(SystemVerilogUvmPhaseKind::Main)},
      "a shared custom UVM domain must retain its exact parallel anchor");
  phases.clear_domain_placement(producer);
  require(!phases.snapshot(producer).with_phase,
          "clearing domain placement must restore independent execution");

  const auto domains_before_negatives = phases.domains().size();
  const auto common_nodes_before_negatives =
      phases.phases(schedule.common_domain).size();
  const auto edges_before_negatives = phases.edge_count();
  const auto mutations_before_negatives = phases.mutation_count();
  require(
      rejects("FSIM-UVM-PHASE-002", [&] {
        (void)phases.create_standard_schedule();
      })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               (void)phases.insert_custom_phase(
                   schedule.common_domain, "bad_mixed_placement",
                   SystemVerilogUvmPhaseExecutionKind::Function,
                   [&] {
                     auto placement =
                         SystemVerilogUvmPhasePlacement::parallel_with(
                             schedule.phase(SystemVerilogUvmPhaseKind::Run));
                     placement.after_phase =
                         schedule.phase(SystemVerilogUvmPhaseKind::Build);
                     return placement;
                   }());
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               (void)phases.insert_custom_phase(
                   schedule.common_domain, "bad_cross_domain",
                   SystemVerilogUvmPhaseExecutionKind::Function,
                   SystemVerilogUvmPhasePlacement::after(
                       schedule.phase(SystemVerilogUvmPhaseKind::Reset)));
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               (void)phases.insert_custom_phase(
                   schedule.common_domain, "bad_reverse_order",
                   SystemVerilogUvmPhaseExecutionKind::Function,
                   SystemVerilogUvmPhasePlacement::between(
                       schedule.phase(SystemVerilogUvmPhaseKind::Report),
                       schedule.phase(SystemVerilogUvmPhaseKind::Connect)));
             })
          && rejects("FSIM-UVM-PHASE-002", [&] {
               phases.place_domain_with(
                   schedule.runtime_domain,
                   schedule.phase(SystemVerilogUvmPhaseKind::Run));
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.place_domain_with(
                   schedule.common_domain,
                   schedule.phase(SystemVerilogUvmPhaseKind::Build));
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.place_domain_with(
                   schedule.common_domain,
                   schedule.phase(SystemVerilogUvmPhaseKind::Reset));
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.synchronize_domains(producer, producer);
             })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               phases.synchronize_domains(
                   producer, consumer, std::nullopt, consumer_transfer);
             })
          && phases.domains().size() == domains_before_negatives
          && phases.phases(schedule.common_domain).size()
              == common_nodes_before_negatives
          && phases.edge_count() == edges_before_negatives
          && phases.mutation_count() == mutations_before_negatives,
      "invalid standard construction, placement, and synchronization must "
      "reject without publishing graph state");

  auto edge_limits = limits;
  edge_limits.maximum_edges = 19;
  SystemVerilogUvmPhaseService edge_limited{
      fixture.components, edge_limits};
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)edge_limited.create_standard_schedule();
      })
          && edge_limited.domains().empty()
          && edge_limited.edge_count() == 0
          && edge_limited.mutation_count() == 0
          && !edge_limited.standard_schedule(),
      "standard schedule edge exhaustion must roll back every domain and node");

  auto node_limits = limits;
  node_limits.maximum_phases = 20;
  SystemVerilogUvmPhaseService node_limited{
      fixture.components, node_limits};
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)node_limited.create_standard_schedule();
      })
          && node_limited.domains().empty()
          && node_limited.mutation_count() == 0,
      "standard schedule node exhaustion must roll back every mutation");

  SystemVerilogUvmPhaseService root_rollback{
      fixture.components, limits};
  const std::array invalid_roots{fixture.first_root,
                                 SystemVerilogUvmRootHandle{999'999}};
  require(
      rejects("FSIM-UVM-PHASE-005", [&] {
        (void)root_rollback.create_standard_schedule(invalid_roots);
      })
          && root_rollback.domains().empty()
          && root_rollback.edge_count() == 0
          && root_rollback.mutation_count() == 0,
      "late root validation failure must roll back standard construction");

  SystemVerilogUvmPhaseService participation_baseline{
      fixture.components, limits};
  (void)participation_baseline.create_standard_schedule();
  const auto standard_mutations = participation_baseline.mutation_count();

  auto participation_reject_limits = limits;
  participation_reject_limits.maximum_mutations = standard_mutations + 3U;
  SystemVerilogUvmPhaseService participation_reject{
      fixture.components, participation_reject_limits};
  const auto participation_reject_schedule =
      participation_reject.create_standard_schedule();
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        participation_reject.participate_standard_root(fixture.first_root);
      })
          && participation_reject.snapshot(
                 participation_reject_schedule.common_domain).roots.empty()
          && participation_reject.snapshot(
                 participation_reject_schedule.runtime_domain).roots.empty()
          && participation_reject.mutation_count() == standard_mutations,
      "standard root participation must reserve transactional cleanup work "
      "before publishing either domain record");

  auto participation_cleanup_limits = limits;
  participation_cleanup_limits.maximum_mutations = standard_mutations + 4U;
  SystemVerilogUvmPhaseService participation_cleanup{
      fixture.components, participation_cleanup_limits};
  const auto participation_cleanup_schedule =
      participation_cleanup.create_standard_schedule();
  participation_cleanup.participate_standard_root(fixture.first_root);
  participation_cleanup.unparticipate_standard_root(fixture.first_root);
  require(
      participation_cleanup.snapshot(
          participation_cleanup_schedule.common_domain).roots.empty()
          && participation_cleanup.snapshot(
                 participation_cleanup_schedule.runtime_domain).roots.empty()
          && participation_cleanup.mutation_count()
              == participation_cleanup_limits.maximum_mutations,
      "a standard root admitted at the mutation boundary must retain enough "
      "capacity for complete two-domain cleanup");

  auto insertion_limits = limits;
  insertion_limits.maximum_edges = 1;
  SystemVerilogUvmPhaseService insertion_rollback{
      fixture.components, insertion_limits};
  const auto insertion_domain = insertion_rollback.create_domain(
      "insert", SystemVerilogUvmDomainKind::Custom);
  const auto insertion_a = insertion_rollback.create_custom_phase(
      insertion_domain, "a", SystemVerilogUvmPhaseExecutionKind::Function);
  const auto insertion_b = insertion_rollback.create_custom_phase(
      insertion_domain, "b", SystemVerilogUvmPhaseExecutionKind::Function);
  insertion_rollback.connect(insertion_a, insertion_b);
  const auto insertion_mutations = insertion_rollback.mutation_count();
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)insertion_rollback.insert_custom_phase(
            insertion_domain, "between",
            SystemVerilogUvmPhaseExecutionKind::Function,
            SystemVerilogUvmPhasePlacement::between(
                insertion_a, insertion_b));
      })
          && insertion_rollback.phases(insertion_domain)
              == std::vector<SystemVerilogUvmPhaseHandle>{
                  insertion_a, insertion_b}
          && insertion_rollback.snapshot(insertion_a).successors
              == std::vector<SystemVerilogUvmPhaseHandle>{insertion_b}
          && insertion_rollback.edge_count() == 1
          && insertion_rollback.mutation_count() == insertion_mutations,
      "custom insertion exhaustion must restore the original adjacency and "
      "registration state");
}

void test_systemverilog_uvm_function_phases() {
  using namespace fsim::runtime;

  PhaseFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto alpha = fixture.make_component("alpha", top);
  const auto leaf = fixture.make_component("leaf", alpha);
  const auto beta = fixture.make_component("beta", top);
  const auto isolated = fixture.make_component(
      "isolated", 0, fixture.second_root);

  SystemVerilogUvmPhaseLimits limits;
  limits.maximum_domains = 4;
  limits.maximum_phases = 32;
  limits.maximum_phases_per_domain = 24;
  limits.maximum_edges = 64;
  limits.maximum_roots_per_domain = 4;
  limits.maximum_depth = 8;
  limits.maximum_identity_bytes = 64;
  limits.maximum_traversal_work = 256;
  limits.maximum_mutations = 256;
  SystemVerilogUvmPhaseService phases{fixture.components, limits};
  const std::array roots{fixture.first_root, fixture.second_root};
  const auto schedule = phases.create_standard_schedule(roots);
  const auto build = schedule.phase(SystemVerilogUvmPhaseKind::Build);
  const auto connect = schedule.phase(SystemVerilogUvmPhaseKind::Connect);
  require(
      phases.snapshot(build).traversal
              == SystemVerilogUvmPhaseTraversal::TopDown
          && phases.snapshot(connect).traversal
              == SystemVerilogUvmPhaseTraversal::BottomUp,
      "build must traverse top-down while later UVM function phases traverse "
      "bottom-up");

  SystemVerilogClassHandle late{};
  const auto build_result = phases.execute_function_phase(
      build,
      [&](const auto component, const auto, const auto callback) {
        if (callback == SystemVerilogUvmPhaseCallbackKind::Execute
            && component == alpha) {
          late = fixture.make_component("late", alpha);
        }
        if (callback == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd
            && component == beta) {
          throw std::runtime_error{"contained ready-to-end failure"};
        }
        if (callback == SystemVerilogUvmPhaseCallbackKind::PhaseEnded
            && component == leaf) {
          (void)fixture.components.create_root("blocked_during_build_hook");
        }
      });
  require(
      late != 0 && fixture.components.parent(late) == alpha
          && fixture.components.contains(isolated)
          && build_result.final_state == SystemVerilogUvmPhaseState::Done
          && phases.snapshot(build).state == SystemVerilogUvmPhaseState::Done
          && build_result.failures.size() == 2
          && !build_result.success()
          && build_result.failures[0].diagnostic_code
              == "FSIM-UVM-PHASE-006"
          && build_result.failures[1].diagnostic_code
              == "FSIM-UVM-PHASE-006"
          && fixture.components.roots()
              == std::vector<SystemVerilogUvmRootHandle>{
                  fixture.first_root, fixture.second_root,
                  fixture.third_root},
      "function-phase callback failures must be contained, reported, and "
      "leave the phase done without publishing forbidden root mutations");

  const auto names_for = [&](const auto& result, const auto callback) {
    std::vector<std::string> names;
    for (const auto& event : result.events) {
      if (event.callback == callback) {
        names.push_back(fixture.components.full_name(event.component));
      }
    }
    return names;
  };
  const std::vector<std::string> initial_top_down{
      "top", "top.alpha", "top.alpha.leaf", "top.beta", "isolated"};
  const std::vector<std::string> complete_top_down{
      "top", "top.alpha", "top.alpha.leaf", "top.alpha.late",
      "top.beta", "isolated"};
  require(
      names_for(
          build_result,
          SystemVerilogUvmPhaseCallbackKind::PhaseStarted)
              == initial_top_down
          && names_for(
                 build_result,
                 SystemVerilogUvmPhaseCallbackKind::Execute)
              == complete_top_down
          && names_for(
                 build_result,
                 SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd)
              == complete_top_down
          && names_for(
                 build_result,
                 SystemVerilogUvmPhaseCallbackKind::PhaseEnded)
              == complete_top_down,
      "build callbacks must run root, top, sibling, and child creation order "
      "top-down and include children created by the active parent callback");

  const auto blocked_candidate = fixture.heap.allocate(
      PhaseFixture::descriptor());
  fixture.objects.initialize(blocked_candidate);
  const auto connect_result = phases.execute_function_phase(
      connect,
      [&](const auto component, const auto, const auto callback) {
        if (callback == SystemVerilogUvmPhaseCallbackKind::Execute
            && component == alpha) {
          fixture.components.initialize(
              blocked_candidate, "blocked", alpha);
        }
        if (callback == SystemVerilogUvmPhaseCallbackKind::Execute
            && component == leaf) {
          throw std::runtime_error{"contained connect failure"};
        }
      });
  require(
      !fixture.components.contains(blocked_candidate)
          && connect_result.failures.size() == 2
          && connect_result.final_state == SystemVerilogUvmPhaseState::Done,
      "non-build phases must reject hierarchy growth and continue after each "
      "per-component exception");
  fixture.objects.erase(blocked_candidate);
  require(
      fixture.heap.release(blocked_candidate),
      "a hierarchy candidate rejected by phase policy must remain owned by "
      "its caller");

  const std::vector<std::string> bottom_up{
      "top.alpha.leaf", "top.alpha.late", "top.alpha", "top.beta",
      "top", "isolated"};
  for (const auto callback : {
           SystemVerilogUvmPhaseCallbackKind::PhaseStarted,
           SystemVerilogUvmPhaseCallbackKind::Execute,
           SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd,
           SystemVerilogUvmPhaseCallbackKind::PhaseEnded}) {
    require(
        names_for(connect_result, callback) == bottom_up,
        "connect callbacks must retain deterministic bottom-up root and "
        "component order");
  }

  for (const auto kind : {
           SystemVerilogUvmPhaseKind::EndOfElaboration,
           SystemVerilogUvmPhaseKind::StartOfSimulation,
           SystemVerilogUvmPhaseKind::Extract,
           SystemVerilogUvmPhaseKind::Check,
           SystemVerilogUvmPhaseKind::Report,
           SystemVerilogUvmPhaseKind::Final}) {
    const auto result = phases.execute_function_phase(
        schedule.phase(kind),
        [](const auto, const auto, const auto) {});
    require(
        result.success()
            && result.final_state == SystemVerilogUvmPhaseState::Done
            && names_for(
                   result, SystemVerilogUvmPhaseCallbackKind::Execute)
                == bottom_up,
        "every post-build standard function phase must execute bottom-up "
        "through every participating root");
  }

  require(
      rejects("FSIM-UVM-PHASE-002", [&] {
        (void)phases.execute_function_phase(
            build, [](const auto, const auto, const auto) {});
      })
          && rejects("FSIM-UVM-PHASE-003", [&] {
               (void)phases.execute_function_phase(
                   schedule.phase(SystemVerilogUvmPhaseKind::Run),
                   [](const auto, const auto, const auto) {});
             }),
      "completed function phases and task phases must reject the synchronous "
      "function execution path with stable diagnostics");
}

void test_systemverilog_uvm_task_phases() {
  using namespace fsim::runtime;

  PhaseFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto alpha = fixture.make_component("alpha", top);
  const auto leaf = fixture.make_component("leaf", alpha);
  const auto beta = fixture.make_component("beta", top);
  const auto isolated = fixture.make_component(
      "isolated", 0, fixture.second_root);
  SystemVerilogUvmPhaseLimits limits;
  limits.maximum_domains = 12;
  limits.maximum_phases = 64;
  limits.maximum_phases_per_domain = 24;
  limits.maximum_edges = 128;
  limits.maximum_roots_per_domain = 4;
  limits.maximum_depth = 8;
  limits.maximum_identity_bytes = 64;
  limits.maximum_traversal_work = 512;
  limits.maximum_mutations = 512;
  limits.maximum_phase_processes = 256;
  SystemVerilogUvmPhaseService phases{fixture.components, limits};
  const std::array roots{fixture.first_root, fixture.second_root};
  const auto schedule = phases.create_standard_schedule(roots);
  const auto run = schedule.phase(SystemVerilogUvmPhaseKind::Run);

  std::vector<SystemVerilogClassHandle> hook_order;
  const auto hooks = [&](const auto component, const auto, const auto kind) {
    hook_order.push_back(component);
    if (kind == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd
        && component == beta) {
      throw std::runtime_error{"contained task ready failure"};
    }
  };
  const auto suspended = phases.execute_task_phase(
      run,
      hooks,
      [&](const auto component, const auto, const auto) {
        if (component == leaf) {
          (void)fixture.components.create_root("blocked_task_root");
        }
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  const std::vector<SystemVerilogClassHandle> top_down{
      top, alpha, leaf, beta, isolated};
  require(
      suspended.final_state == SystemVerilogUvmPhaseState::Executing
          && phases.snapshot(run).state
              == SystemVerilogUvmPhaseState::Executing
          && suspended.processes.size() == top_down.size()
          && suspended.failures.size() == 1
          && suspended.failures.front().diagnostic_code
              == "FSIM-UVM-PHASE-006"
          && hook_order == top_down,
      "task phases must launch top-down, contain callback failures, reject "
      "hierarchy mutation, and remain executing while processes suspend");
  for (std::size_t index{}; index < suspended.processes.size(); ++index) {
    const auto snapshot = phases.process_snapshot(
        suspended.processes[index]);
    require(
        snapshot.phase == run && snapshot.component == top_down[index]
            && snapshot.state
                == (top_down[index] == leaf
                        ? SystemVerilogUvmPhaseProcessState::Completed
                        : SystemVerilogUvmPhaseProcessState::Running),
        "phase-local process snapshots must retain phase, component, source "
        "order, and contained completion state");
    if (snapshot.state == SystemVerilogUvmPhaseProcessState::Running) {
      phases.complete_task_process(snapshot.handle);
    }
  }
  require(
      rejects("FSIM-UVM-PHASE-007", [&] {
        phases.complete_task_process(suspended.processes.front());
      }),
      "completed UVM phase processes must reject duplicate completion");
  const auto completed = phases.complete_task_phase(run, hooks);
  require(
      completed.final_state == SystemVerilogUvmPhaseState::Done
          && completed.failures.size() == 2
          && phases.snapshot(run).state == SystemVerilogUvmPhaseState::Done
          && hook_order.size() == top_down.size() * 3U,
      "task phase completion must wait for every owned process and then run "
      "ready-to-end and ended hooks despite contained hook failures");

  for (const auto kind : {
           SystemVerilogUvmPhaseKind::PreReset,
           SystemVerilogUvmPhaseKind::Reset,
           SystemVerilogUvmPhaseKind::PostReset,
           SystemVerilogUvmPhaseKind::PreConfigure,
           SystemVerilogUvmPhaseKind::Configure,
           SystemVerilogUvmPhaseKind::PostConfigure,
           SystemVerilogUvmPhaseKind::PreMain,
           SystemVerilogUvmPhaseKind::Main,
           SystemVerilogUvmPhaseKind::PostMain,
           SystemVerilogUvmPhaseKind::PreShutdown,
           SystemVerilogUvmPhaseKind::Shutdown,
           SystemVerilogUvmPhaseKind::PostShutdown}) {
    const auto result = phases.execute_task_phase(
        schedule.phase(kind),
        [](const auto, const auto, const auto) {},
        [](const auto, const auto, const auto) {
          return SystemVerilogUvmTaskPhaseStatus::Completed;
        });
    require(
        result.success()
            && result.final_state == SystemVerilogUvmPhaseState::Done
            && result.processes.size() == top_down.size(),
        "every standard runtime task phase must launch and complete every "
        "participating component process");
  }

  const auto producer = phases.create_domain(
      "task_producer", SystemVerilogUvmDomainKind::Custom);
  const auto consumer = phases.create_domain(
      "task_consumer", SystemVerilogUvmDomainKind::Custom);
  phases.participate(producer, fixture.first_root);
  phases.participate(consumer, fixture.second_root);
  const auto produce = phases.create_custom_phase(
      producer, "transfer", SystemVerilogUvmPhaseExecutionKind::Task);
  const auto consume = phases.create_custom_phase(
      consumer, "transfer", SystemVerilogUvmPhaseExecutionKind::Task);
  phases.synchronize_domains(producer, consumer);
  const std::array synchronized{produce, consume};
  const auto concurrent = phases.execute_synchronized_task_phases(
      synchronized,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(
      concurrent.size() == 2
          && concurrent[0].final_state
              == SystemVerilogUvmPhaseState::Executing
          && concurrent[1].final_state
              == SystemVerilogUvmPhaseState::Executing
          && concurrent[0].processes.size() == 4
          && concurrent[1].processes.size() == 1,
      "synchronized sibling domains must become concurrently executing while "
      "retaining independent root and process ownership");
  for (const auto& execution : concurrent) {
    for (const auto& process : execution.processes) {
      phases.complete_task_process(process);
    }
    const auto finished = phases.complete_task_phase(
        execution.phase, [](const auto, const auto, const auto) {});
    require(
        finished.final_state == SystemVerilogUvmPhaseState::Done,
        "each synchronized domain must complete independently after its own "
        "processes settle");
  }

  PhaseFixture jump_fixture;
  const auto jump_top = jump_fixture.make_component(
      "jump_top", 0, jump_fixture.first_root);
  (void)jump_top;
  SystemVerilogUvmPhaseService jump_phases{
      jump_fixture.components, limits};
  const std::array jump_roots{jump_fixture.first_root};
  const auto jump_schedule = jump_phases.create_standard_schedule(jump_roots);
  const auto jump_run = jump_schedule.phase(SystemVerilogUvmPhaseKind::Run);
  const auto jump_final = jump_schedule.phase(SystemVerilogUvmPhaseKind::Final);
  const auto jump_execution = jump_phases.execute_task_phase(
      jump_run,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  const auto forward = jump_phases.jump(jump_run, jump_final);
  require(
      forward.kind == SystemVerilogUvmPhaseJumpKind::Forward
          && forward.skipped
              == std::vector<SystemVerilogUvmPhaseHandle>{
                  jump_schedule.phase(SystemVerilogUvmPhaseKind::Extract),
                  jump_schedule.phase(SystemVerilogUvmPhaseKind::Check),
                  jump_schedule.phase(SystemVerilogUvmPhaseKind::Report)}
          && forward.cancelled == jump_execution.processes
          && jump_phases.snapshot(jump_final).state
              == SystemVerilogUvmPhaseState::Dormant,
      "forward phase jumps must cancel source processes, mark intervening "
      "phases skipped, and leave the target executable");
  (void)jump_phases.execute_function_phase(
      jump_final, [](const auto, const auto, const auto) {});

  const auto pre_reset = jump_schedule.phase(
      SystemVerilogUvmPhaseKind::PreReset);
  const auto reset = jump_schedule.phase(SystemVerilogUvmPhaseKind::Reset);
  const auto post_reset = jump_schedule.phase(
      SystemVerilogUvmPhaseKind::PostReset);
  for (const auto& phase : {pre_reset, reset}) {
    (void)jump_phases.execute_task_phase(
        phase,
        [](const auto, const auto, const auto) {},
        [](const auto, const auto, const auto) {
          return SystemVerilogUvmTaskPhaseStatus::Completed;
        });
  }
  (void)jump_phases.execute_task_phase(
      post_reset,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  const auto backward = jump_phases.jump(post_reset, pre_reset);
  require(
      backward.kind == SystemVerilogUvmPhaseJumpKind::Backward
          && backward.reset
              == std::vector<SystemVerilogUvmPhaseHandle>{
                  pre_reset, reset, post_reset}
          && jump_phases.snapshot(pre_reset).state
              == SystemVerilogUvmPhaseState::Dormant
          && jump_phases.snapshot(reset).state
              == SystemVerilogUvmPhaseState::Dormant
          && jump_phases.snapshot(post_reset).state
              == SystemVerilogUvmPhaseState::Dormant,
      "backward phase jumps must cancel active work and make the complete "
      "target-through-source interval restartable");
  require(
      rejects("FSIM-UVM-PHASE-007", [&] {
        (void)jump_phases.jump(pre_reset, jump_final);
      })
          && rejects("FSIM-UVM-PHASE-007", [&] {
               (void)jump_phases.complete_task_phase(
                   pre_reset, [](const auto, const auto, const auto) {});
             }),
      "dormant and cross-domain jump/completion endpoints must reject with a "
      "stable task-control diagnostic");

  auto process_limits = limits;
  process_limits.maximum_phase_processes = 1;
  SystemVerilogUvmPhaseService process_limited{
      fixture.components, process_limits};
  const auto limited_schedule = process_limited.create_standard_schedule(roots);
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)process_limited.execute_task_phase(
            limited_schedule.phase(SystemVerilogUvmPhaseKind::Run),
            [](const auto, const auto, const auto) {},
            [](const auto, const auto, const auto) {
              return SystemVerilogUvmTaskPhaseStatus::Completed;
            });
      }),
      "phase-process exhaustion must reject before publishing phase state or "
      "partial process ownership");
}

void test_systemverilog_uvm_phase_lifetime() {
  using namespace fsim::runtime;

  PhaseFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto child = fixture.make_component("child", top);
  const auto isolated = fixture.make_component(
      "isolated", 0, fixture.second_root);
  (void)child;
  (void)isolated;
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases{fixture.components, scheduler};
  const std::array roots{fixture.first_root, fixture.second_root};
  const auto schedule = phases.create_standard_schedule(roots);
  const auto run = schedule.phase(SystemVerilogUvmPhaseKind::Run);
  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, phases, scheduler};
  phases.set_objection_service(objections);

  const auto execution = phases.execute_task_phase(
      run,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(
      execution.processes.size() == 3 && phases.process_count() == 3,
      "lifetime proof must begin with one owned process per component");
  const auto source = objections.bind_source(top);
  objections.set_drain_time(run, source, 5);
  (void)objections.raise(run, source, "phase lifetime", 1);
  for (const auto& process : execution.processes) {
    phases.complete_task_process(process);
  }
  require(
      rejects("FSIM-UVM-PHASE-007", [&] {
        (void)phases.complete_task_phase(
            run, [](const auto, const auto, const auto) {});
      }),
      "task-phase completion must wait for live objection counts");
  (void)objections.drop(run, source, "phase lifetime", 1);
  require(
      objections.has_pending_drain(run, source)
          && rejects("FSIM-UVM-PHASE-007", [&] {
               (void)phases.complete_task_phase(
                   run, [](const auto, const auto, const auto) {});
             }),
      "task-phase completion must wait for pending objection drains");
  require(
      scheduler.run().status == RunStatus::completed
          && scheduler.now() == 5 && objections.phase_quiescent(run),
      "phase objection state must become quiescent at simulated drain time");
  const auto completed = phases.complete_task_phase(
      run, [](const auto, const auto, const auto) {});
  require(
      completed.final_state == SystemVerilogUvmPhaseState::Done
          && phases.process_count() == 0
          && std::none_of(
              execution.processes.begin(), execution.processes.end(),
              [&](const auto& process) { return phases.contains(process); }),
      "completed task phases must reclaim every owned process handle");

  const auto tree_domain = phases.create_domain(
      "tree", SystemVerilogUvmDomainKind::Custom);
  phases.participate(tree_domain, fixture.first_root);
  const auto tree_phase = phases.create_custom_phase(
      tree_domain, "tree", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle tree_parent;
  const auto tree_execution = phases.execute_task_phase(
      tree_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == top) tree_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(
      static_cast<bool>(tree_parent),
      "tree proof must retain the top process handle");
  SystemVerilogUvmPhaseProcessHandle grandchild;
  bool child_ran{};
  bool grandchild_ran{};
  const auto tree_child = phases.schedule_child_process(
      tree_parent,
      3,
      [&](const auto process) {
        child_ran = true;
        grandchild = phases.schedule_child_process(
            process, 2, [&](const auto) { grandchild_ran = true; });
      });
  const auto child_snapshot = phases.process_snapshot(tree_child);
  require(
      child_snapshot.parent
              == std::optional<SystemVerilogUvmPhaseProcessHandle>{tree_parent}
          && child_snapshot.depth == 1 && child_snapshot.scheduled,
      "scheduled children must retain parent, depth, and scheduler ownership");
  require(
      rejects("FSIM-UVM-PHASE-007", [&] {
        phases.complete_task_process(tree_parent);
      }),
      "a parent process must await running descendants");
  require(
      scheduler.run(8).status == RunStatus::time_limit && child_ran
          && !grandchild_ran && grandchild
          && rejects("FSIM-UVM-PHASE-007", [&] {
               phases.complete_task_process(tree_parent);
             }),
      "parent completion must continue awaiting a scheduled grandchild");
  require(
      scheduler.run().status == RunStatus::completed
          && scheduler.now() == 10 && grandchild_ran,
      "nested process trees must execute at stable simulated times");
  phases.complete_task_process(tree_parent);
  for (const auto& process : tree_execution.processes) {
    if (phases.contains(process)
        && phases.process_snapshot(process).state
            == SystemVerilogUvmPhaseProcessState::Running) {
      phases.complete_task_process(process);
    }
  }
  const auto tree_done = phases.complete_task_phase(
      tree_phase, [](const auto, const auto, const auto) {});
  require(
      tree_done.final_state == SystemVerilogUvmPhaseState::Done
          && phases.process_count() == 0 && !phases.contains(tree_child)
          && !phases.contains(grandchild),
      "phase completion must reclaim nested scheduled process trees");

  const auto failure_phase = phases.create_custom_phase(
      tree_domain, "failure", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle failure_parent;
  const auto failure_execution = phases.execute_task_phase(
      failure_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == top) failure_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool cancelled_descendant_ran{};
  (void)phases.schedule_child_process(
      failure_parent,
      1,
      [&](const auto process) {
        (void)phases.schedule_child_process(
            process, 10,
            [&](const auto) { cancelled_descendant_ran = true; });
        throw std::runtime_error{"contained scheduled failure"};
      });
  const auto failure_run = scheduler.run(11);
  require(
      failure_run.time == 11 && scheduler.now() == 11,
      "scheduled failure must execute at its exact tick");
  phases.complete_task_process(failure_parent);
  for (const auto& process : failure_execution.processes) {
    if (phases.contains(process)
        && phases.process_snapshot(process).state
            == SystemVerilogUvmPhaseProcessState::Running) {
      phases.complete_task_process(process);
    }
  }
  const auto failure_done = phases.complete_task_phase(
      failure_phase, [](const auto, const auto, const auto) {});
  require(
      failure_done.failures.size() == 1
          && failure_done.failures.front().diagnostic_code
              == "FSIM-UVM-PHASE-006",
      "scheduled process failures must be contained in the owning phase");
  (void)scheduler.run();
  require(
      !cancelled_descendant_ran && phases.process_count() == 0,
      "failed scheduled parents must cancel and reclaim descendants");

  const auto timeout_phase = phases.create_custom_phase(
      tree_domain, "timeout", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle timeout_parent;
  (void)phases.execute_task_phase(
      timeout_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == top) timeout_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool timed_out_child_ran{};
  bool caller_owned_ran{};
  (void)phases.schedule_child_process(
      timeout_parent, 20,
      [&](const auto) { timed_out_child_ran = true; });
  scheduler.schedule_after(
      20, SchedulerPhase::reactive, 99,
      [&](Scheduler&) { caller_owned_ran = true; });
  const auto timeout_source = objections.bind_source(top);
  objections.set_drain_time(timeout_phase, timeout_source, 20);
  (void)objections.raise(timeout_phase, timeout_source, "timeout", 1);
  (void)objections.drop(timeout_phase, timeout_source, "timeout", 1);
  const auto timed_out = phases.timeout_task_phase(timeout_phase);
  require(
      !timed_out.empty() && phases.process_count() == 0
          && objections.phase_quiescent(timeout_phase),
      "timeout must cancel phase processes, waits, and pending drains");
  (void)scheduler.run();
  require(
      !timed_out_child_ran && caller_owned_ran,
      "phase timeout must not disturb caller-owned scheduler work");

  PhaseFixture jump_fixture;
  const auto jump_top = jump_fixture.make_component(
      "jump_top", 0, jump_fixture.first_root);
  Scheduler jump_scheduler;
  SystemVerilogUvmPhaseService jump_phases{
      jump_fixture.components, jump_scheduler};
  const std::array jump_roots{jump_fixture.first_root};
  const auto jump_schedule = jump_phases.create_standard_schedule(jump_roots);
  const auto jump_run = jump_schedule.phase(SystemVerilogUvmPhaseKind::Run);
  const auto jump_final = jump_schedule.phase(SystemVerilogUvmPhaseKind::Final);
  SystemVerilogUvmObjectionService jump_objections{
      jump_fixture.objects,
      jump_fixture.components,
      jump_phases,
      jump_scheduler};
  jump_phases.set_objection_service(jump_objections);
  SystemVerilogUvmPhaseProcessHandle jump_parent;
  (void)jump_phases.execute_task_phase(
      jump_run,
      [](const auto, const auto, const auto) {},
      [&](const auto, const auto, const auto process) {
        jump_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool jumped_child_ran{};
  bool jump_caller_ran{};
  bool sibling_domain_ran{};
  (void)jump_phases.schedule_child_process(
      jump_parent, 10, [&](const auto) { jumped_child_ran = true; });
  const auto sibling_domain = jump_phases.create_domain(
      "sibling", SystemVerilogUvmDomainKind::Custom);
  jump_phases.participate(sibling_domain, jump_fixture.first_root);
  const auto sibling_phase = jump_phases.create_custom_phase(
      sibling_domain, "sibling", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle sibling_parent;
  (void)jump_phases.execute_task_phase(
      sibling_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto, const auto, const auto process) {
        sibling_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  (void)jump_phases.schedule_child_process(
      sibling_parent, 10, [&](const auto) { sibling_domain_ran = true; });
  jump_scheduler.schedule_after(
      10, SchedulerPhase::reactive, 100,
      [&](Scheduler&) { jump_caller_ran = true; });
  const auto jump_source = jump_objections.bind_source(jump_top);
  jump_objections.set_drain_time(jump_run, jump_source, 10);
  (void)jump_objections.raise(jump_run, jump_source, "jump", 1);
  (void)jump_objections.drop(jump_run, jump_source, "jump", 1);
  const auto jumped = jump_phases.jump(jump_run, jump_final);
  require(
      !jumped.cancelled.empty() && jump_phases.process_count() == 2
          && jump_objections.phase_quiescent(jump_run),
      "phase jump must reclaim process trees and cancel pending drains");
  (void)jump_scheduler.run();
  require(
      !jumped_child_ran && jump_caller_ran && sibling_domain_ran,
      "phase jump must prevent post-phase callbacks without disturbing caller or sibling-domain work");
  jump_phases.complete_task_process(sibling_parent);
  (void)jump_phases.complete_task_phase(
      sibling_phase, [](const auto, const auto, const auto) {});
  require(
      jump_phases.process_count() == 0,
      "independent sibling-domain processes must remain independently reclaimable");

  bool destroyed_child_ran{};
  bool destruction_caller_ran{};
  Scheduler destruction_scheduler;
  {
    SystemVerilogUvmPhaseService destruction_phases{
        jump_fixture.components, destruction_scheduler};
    const auto destruction_domain = destruction_phases.create_domain(
        "destruction", SystemVerilogUvmDomainKind::Custom);
    destruction_phases.participate(
        destruction_domain, jump_fixture.first_root);
    const auto destruction_phase = destruction_phases.create_custom_phase(
        destruction_domain,
        "destruction",
        SystemVerilogUvmPhaseExecutionKind::Task);
    SystemVerilogUvmPhaseProcessHandle destruction_parent;
    (void)destruction_phases.execute_task_phase(
        destruction_phase,
        [](const auto, const auto, const auto) {},
        [&](const auto, const auto, const auto process) {
          destruction_parent = process;
          return SystemVerilogUvmTaskPhaseStatus::Suspended;
        });
    (void)destruction_phases.schedule_child_process(
        destruction_parent, 3,
        [&](const auto) { destroyed_child_ran = true; });
    destruction_scheduler.schedule_after(
        3, SchedulerPhase::reactive, 101,
        [&](Scheduler&) { destruction_caller_ran = true; });
  }
  (void)destruction_scheduler.run();
  require(
      !destroyed_child_ran && destruction_caller_ran,
      "simulation destruction must cancel only phase-owned scheduled work");

  PhaseFixture teardown_fixture;
  const auto first = teardown_fixture.make_component(
      "first", 0, teardown_fixture.first_root);
  const auto second = teardown_fixture.make_component(
      "second", 0, teardown_fixture.second_root);
  Scheduler teardown_scheduler;
  SystemVerilogUvmPhaseService teardown_phases{
      teardown_fixture.components, teardown_scheduler};
  const std::array teardown_roots{
      teardown_fixture.first_root, teardown_fixture.second_root};
  const auto teardown_schedule = teardown_phases.create_standard_schedule(
      teardown_roots);
  const auto teardown_run = teardown_schedule.phase(
      SystemVerilogUvmPhaseKind::Run);
  SystemVerilogUvmObjectionService teardown_objections{
      teardown_fixture.objects,
      teardown_fixture.components,
      teardown_phases,
      teardown_scheduler};
  teardown_phases.set_objection_service(teardown_objections);
  SystemVerilogUvmPhaseProcessHandle first_process;
  SystemVerilogUvmPhaseProcessHandle second_process;
  (void)teardown_phases.execute_task_phase(
      teardown_run,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == first) first_process = process;
        if (component == second) second_process = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool first_child_ran{};
  bool second_child_ran{};
  (void)teardown_phases.schedule_child_process(
      first_process, 4, [&](const auto) { first_child_ran = true; });
  (void)teardown_phases.schedule_child_process(
      second_process, 4, [&](const auto) { second_child_ran = true; });
  const auto first_cancelled = teardown_phases.teardown_root(
      teardown_fixture.first_root);
  teardown_fixture.components.destroy_root(teardown_fixture.first_root);
  require(
      !first_cancelled.empty() && !teardown_phases.contains(first_process)
          && teardown_phases.contains(second_process),
      "root teardown must reclaim only processes owned by that root");
  (void)teardown_scheduler.run();
  require(
      !first_child_ran && second_child_ran,
      "root teardown must preserve sibling-root scheduled processes");

  SystemVerilogUvmPhaseLimits process_limits;
  process_limits.maximum_process_depth = 1;
  process_limits.maximum_scheduled_processes = 1;
  Scheduler limited_scheduler;
  SystemVerilogUvmPhaseService limited{
      teardown_fixture.components, limited_scheduler, process_limits};
  const auto limited_domain = limited.create_domain(
      "limited", SystemVerilogUvmDomainKind::Custom);
  limited.participate(limited_domain, teardown_fixture.second_root);
  const auto limited_phase = limited.create_custom_phase(
      limited_domain, "limited", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle limited_parent;
  (void)limited.execute_task_phase(
      limited_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto, const auto, const auto process) {
        limited_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  const auto limited_child = limited.schedule_child_process(
      limited_parent, 1, [](const auto) {});
  require(
      rejects("FSIM-UVM-PHASE-004", [&] {
        (void)limited.schedule_child_process(
            limited_parent, 1, [](const auto) {});
      })
          && rejects("FSIM-UVM-PHASE-004", [&] {
            (void)limited.schedule_child_process(
                limited_child, 1, [](const auto) {});
          }),
      "scheduled-process count and tree depth must reject before publication");

  SystemVerilogUvmPhaseService unscheduled{teardown_fixture.components};
  const auto unscheduled_domain = unscheduled.create_domain(
      "unscheduled", SystemVerilogUvmDomainKind::Custom);
  unscheduled.participate(
      unscheduled_domain, teardown_fixture.second_root);
  const auto unscheduled_phase = unscheduled.create_custom_phase(
      unscheduled_domain,
      "unscheduled",
      SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle unscheduled_parent;
  (void)unscheduled.execute_task_phase(
      unscheduled_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto, const auto, const auto process) {
        unscheduled_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(
      rejects("FSIM-UVM-PHASE-007", [&] {
        (void)unscheduled.schedule_child_process(
            unscheduled_parent, 1, [](const auto) {});
      }),
      "scheduler-owned child work must reject without a simulation scheduler");
}

}  // namespace fsim::tests::runtime
