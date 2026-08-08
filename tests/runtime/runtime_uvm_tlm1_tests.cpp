// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

void require_error(
    const std::string_view code,
    const std::function<void()>& operation,
    const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmTlm1Error& error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

struct TlmFixture {
  SystemVerilogClassHeap heap{{128, 16'384}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle first_root;
  SystemVerilogUvmRootHandle second_root;

  static SystemVerilogClassDescriptor descriptor() {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_component";
    result.dynamic_type = "work::component";
    result.specialization_identity = "work::component";
    result.assignable_declared_types = {
        "work::component", "uvm_pkg::uvm_component",
        "uvm_pkg::uvm_object"};
    return result;
  }

  TlmFixture()
      : objects(
            heap,
            [&](const std::string_view, const std::string_view,
                const std::string_view) {
              return heap.allocate(descriptor());
            }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}),
        first_root(components.create_root("first")),
        second_root(components.create_root("second")) {
    SystemVerilogUvmObjectDescriptor type;
    type.specialization_identity = "work::component";
    type.type_name = "component";
    objects.register_type(std::move(type));
  }

  [[nodiscard]] SystemVerilogClassHandle make_component(
      std::string name,
      const SystemVerilogClassHandle parent = 0,
      const SystemVerilogUvmRootHandle root = 0) {
    const auto object = heap.allocate(descriptor());
    objects.initialize(object);
    components.initialize(object, std::move(name), parent, root);
    return object;
  }
};

SystemVerilogUvmTlm1Profile put_profile() {
  return {
      SystemVerilogUvmTlm1Interface::Put,
      SystemVerilogUvmTlm1Direction::Forward,
      "work::request#(32)",
      {}};
}

SystemVerilogUvmTlm1EndpointDescriptor endpoint(
    const SystemVerilogUvmTlm1EndpointKind kind,
    const SystemVerilogClassHandle component,
    std::string name,
    const std::size_t minimum,
    const std::size_t maximum,
    SystemVerilogUvmTlm1Profile profile = put_profile()) {
  return {kind, std::move(profile), component, std::move(name), minimum, maximum};
}

}  // namespace

void test_systemverilog_uvm_tlm1() {
  static_assert(
      kSystemVerilogUvmOwnershipContract.tlm1
      == SystemVerilogUvmStateScope::Simulation);

  TlmFixture fixture;
  const auto top = fixture.make_component("top", 0, fixture.first_root);
  const auto producer = fixture.make_component("producer", top);
  const auto bridge = fixture.make_component("bridge", top);
  const auto consumer = fixture.make_component("consumer", top);
  const auto peer_top = fixture.make_component(
      "top", 0, fixture.second_root);
  SystemVerilogUvmTlm1Service graph{fixture.components};

  const auto port = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "out", 2, 2));
  const auto export_endpoint = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Export,
      bridge, "forward", 1, 1));
  const auto first_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "first_imp", 0, 0));
  const auto second_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "second_imp", 0, 0));
  const auto peer_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      peer_top, "first_imp", 0, 0));

  graph.connect(port, export_endpoint);
  graph.connect(port, second_imp);
  graph.connect(export_endpoint, first_imp);
  require(!graph.binding_valid(), "graph mutation must invalidate binding");
  graph.resolve_all();
  const auto port_snapshot = graph.snapshot(port);
  require(
      graph.binding_valid()
          && graph.endpoints()
              == std::vector<SystemVerilogUvmTlm1EndpointHandle>{
                  port, export_endpoint, first_imp, second_imp, peer_imp}
          && port_snapshot.declaration_order == 0
          && port_snapshot.root == fixture.first_root
          && port_snapshot.full_name == "top.producer.out"
          && port_snapshot.debug_name == "first:top.producer.out"
          && port_snapshot.outbound
              == std::vector<SystemVerilogUvmTlm1EndpointHandle>{
                  export_endpoint, second_imp}
          && port_snapshot.resolved
              == std::vector<SystemVerilogUvmTlm1EndpointHandle>{
                  first_imp, second_imp}
          && graph.resolved(port).size() == 2
          && graph.resolved(port).front() == first_imp
          && graph.snapshot(first_imp).inbound
              == std::vector<SystemVerilogUvmTlm1EndpointHandle>{
                  export_endpoint}
          && graph.resolved(export_endpoint).front() == first_imp
          && graph.resolved(first_imp).front() == first_imp,
      "TLM1 binding must preserve roots, hierarchy, source order, chains, and fanout");

  SystemVerilogUvmTlm1Service foreign{fixture.components};
  const auto foreign_imp = foreign.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "foreign_imp", 0, 0));
  require_error(
      "FSIM-UVM-TLM1-001",
      [&] { graph.connect(port, foreign_imp); },
      "foreign endpoint handles must reject");
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(port, peer_imp); },
      "cross-root connections must reject");
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(first_imp, second_imp); },
      "implementation sources must reject");
  require_error(
      "FSIM-UVM-TLM1-002",
      [&] { graph.connect(port, second_imp); },
      "duplicate connections must reject");
  require(
      graph.connection_count() == 3 && graph.binding_valid(),
      "rejected connections must preserve a stable graph");

  auto wrong_profile = put_profile();
  wrong_profile.request_type = "work::request#(64)";
  const auto wrong_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "wrong_imp", 0, 0, wrong_profile));
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(export_endpoint, wrong_imp); },
      "nominal profile mismatches must reject");
  auto backward_profile = put_profile();
  backward_profile.direction = SystemVerilogUvmTlm1Direction::Backward;
  const auto backward_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "backward_imp", 0, 0, backward_profile));
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(export_endpoint, backward_imp); },
      "direction mismatches must reject");
  auto get_profile = put_profile();
  get_profile.interface_kind = SystemVerilogUvmTlm1Interface::Get;
  const auto get_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "get_imp", 0, 0, get_profile));
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(export_endpoint, get_imp); },
      "interface mismatches must reject");
  const auto excess_imp = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "excess_imp", 0, 0));
  require_error(
      "FSIM-UVM-TLM1-004",
      [&] { graph.connect(port, excess_imp); },
      "endpoint maximum fanout must reject before mutation");

  const auto cycle_a = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Export,
      bridge, "cycle_a", 0, 1));
  const auto cycle_b = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Export,
      bridge, "cycle_b", 0, 1));
  graph.connect(cycle_a, cycle_b);
  require_error(
      "FSIM-UVM-TLM1-003",
      [&] { graph.connect(cycle_b, cycle_a); },
      "connection cycles must reject atomically");
  require(graph.snapshot(cycle_b).outbound.empty(), "cycle rejection must not publish");
  graph.connect(cycle_b, first_imp);

  const auto required = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "required", 1, 1));
  require_error(
      "FSIM-UVM-TLM1-002",
      [&] { graph.resolve_all(); },
      "unconnected required endpoints must reject binding");
  require(!graph.binding_valid(), "failed binding must not publish a new revision");
  graph.connect(required, first_imp);
  graph.resolve_all();
  require(
      graph.resolved(required).front() == first_imp,
      "a corrected graph must bind stably");

  require_error(
      "FSIM-UVM-TLM1-002",
      [&] {
        (void)graph.register_endpoint(endpoint(
            SystemVerilogUvmTlm1EndpointKind::Port,
            producer, "required", 0, 1));
      },
      "duplicate component endpoint names must reject");
  require_error(
      "FSIM-UVM-TLM1-002",
      [&] {
        (void)graph.register_endpoint(endpoint(
            SystemVerilogUvmTlm1EndpointKind::Implementation,
            consumer, "bad_cardinality", 1, 1));
      },
      "implementation cardinality must be exact");

  SystemVerilogUvmTlm1Limits tiny_limits;
  tiny_limits.maximum_endpoints = 1;
  SystemVerilogUvmTlm1Service tiny{fixture.components, tiny_limits};
  (void)tiny.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "tiny", 0, 0));
  require_error(
      "FSIM-UVM-TLM1-004",
      [&] {
        (void)tiny.register_endpoint(endpoint(
            SystemVerilogUvmTlm1EndpointKind::Implementation,
            consumer, "excess", 0, 0));
      },
      "endpoint count ceilings must reject before mutation");

  SystemVerilogUvmTlm1Limits shallow_limits;
  shallow_limits.maximum_depth = 1;
  SystemVerilogUvmTlm1Service shallow{fixture.components, shallow_limits};
  const auto shallow_port = shallow.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "shallow_port", 1, 1));
  const auto shallow_export = shallow.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Export,
      bridge, "shallow_export", 1, 1));
  const auto shallow_imp = shallow.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "shallow_imp", 0, 0));
  shallow.connect(shallow_port, shallow_export);
  shallow.connect(shallow_export, shallow_imp);
  require_error(
      "FSIM-UVM-TLM1-004",
      [&] { shallow.resolve_all(); },
      "binding depth ceilings must reject without publication");
  require(!shallow.binding_valid(), "failed depth binding must remain unpublished");

  const auto doomed = fixture.make_component("doomed", top);
  const auto stale = graph.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      doomed, "imp", 0, 0));
  fixture.components.release(doomed);
  require_error(
      "FSIM-UVM-TLM1-001",
      [&] { (void)graph.snapshot(stale); },
      "component teardown must stale its endpoint ownership");
}

}  // namespace fsim::tests::runtime
