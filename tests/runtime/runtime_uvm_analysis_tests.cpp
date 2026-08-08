// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

template <typename Callback>
void require_error(const std::string_view code, Callback &&callback,
                   const std::string_view message) {
  try {
    callback();
  } catch (const SystemVerilogUvmTlm1Error &error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogClassDescriptor component_descriptor() {
  SystemVerilogClassDescriptor result;
  result.declared_type = "uvm_pkg::uvm_component";
  result.dynamic_type = "work::component";
  result.specialization_identity = "work::component";
  result.assignable_declared_types = {
      "work::component", "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"};
  return result;
}

SystemVerilogClassDescriptor item_descriptor() {
  SystemVerilogClassDescriptor result;
  result.declared_type = "work::analysis_item";
  result.dynamic_type = "work::analysis_item";
  result.specialization_identity = "work::analysis_item";
  result.assignable_declared_types = {"work::analysis_item",
                                      "uvm_pkg::uvm_object"};
  result.properties = {{"value", SystemVerilogClassPropertyKind::Logic4, 32}};
  return result;
}

struct AnalysisFixture {
  SystemVerilogClassHeap heap{{128, 16'384}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmTlm1Service tlm1;
  SystemVerilogUvmRootHandle root;
  SystemVerilogUvmRootHandle peer_root;

  AnalysisFixture()
      : objects(heap,
                [&](const std::string_view, const std::string_view,
                    const std::string_view) {
                  return heap.allocate(component_descriptor());
                }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}),
        tlm1(heap, components), root(components.create_root("analysis")),
        peer_root(components.create_root("peer")) {
    SystemVerilogUvmObjectDescriptor component_type;
    component_type.specialization_identity = "work::component";
    component_type.type_name = "component";
    objects.register_type(std::move(component_type));
  }

  [[nodiscard]] SystemVerilogClassHandle
  component(std::string name, const SystemVerilogClassHandle parent = 0,
            const SystemVerilogUvmRootHandle selected_root = 0) {
    const auto result = heap.allocate(component_descriptor());
    objects.initialize(result);
    components.initialize(result, std::move(name), parent,
                          selected_root == 0 ? root : selected_root);
    return result;
  }
};

SystemVerilogUvmTlm1Profile analysis_profile() {
  return {SystemVerilogUvmTlm1Interface::Analysis,
          SystemVerilogUvmTlm1Direction::Forward,
          "work::analysis_item",
          {}};
}

SystemVerilogUvmTlm1EndpointDescriptor
analysis_endpoint(const SystemVerilogUvmTlm1EndpointKind kind,
                  const SystemVerilogClassHandle component, std::string name,
                  const std::size_t minimum, const std::size_t maximum) {
  return {kind,   analysis_profile(), component, std::move(name), minimum,
          maximum};
}

SystemVerilogUvmTlm1Payload analysis_payload(const std::uint64_t value) {
  return {"work::analysis_item",
          PackedLogic4::from_aval_bval(32, value, 0),
          0,
          0,
          {},
          0};
}

} // namespace

void test_systemverilog_uvm_analysis() {
  AnalysisFixture fixture;
  const auto top = fixture.component("top");
  const auto publisher = fixture.component("publisher", top);
  const auto bridge = fixture.component("bridge", top);
  const auto subscriber = fixture.component("subscriber", top);
  const auto peer = fixture.component("subscriber", 0, fixture.peer_root);

  const auto port = fixture.tlm1.register_endpoint(
      analysis_endpoint(SystemVerilogUvmTlm1EndpointKind::Port, publisher,
                        "analysis_port", 1, 4));
  const auto export_endpoint = fixture.tlm1.register_endpoint(
      analysis_endpoint(SystemVerilogUvmTlm1EndpointKind::Export, bridge,
                        "analysis_export", 1, 4));
  std::vector<unsigned> callback_order;
  std::vector<std::uint64_t> second_values;
  bool throw_second{};
  bool mutate_graph{};
  SystemVerilogUvmTlm1EndpointHandle late_imp;
  std::size_t late_calls{};
  const auto first_imp = fixture.tlm1.register_analysis_implementation(
      subscriber, "write_first", "work::analysis_item",
      [&](SystemVerilogUvmTlm1Payload delivered) {
        callback_order.push_back(1);
        delivered.value.set(0, Logic4::zero);
        if (mutate_graph && !late_imp) {
          late_imp = fixture.tlm1.register_analysis_implementation(
              subscriber, "write_late", "work::analysis_item",
              [&](SystemVerilogUvmTlm1Payload) { ++late_calls; });
          fixture.tlm1.connect(port, late_imp);
        }
      });
  const auto second_imp = fixture.tlm1.register_analysis_implementation(
      subscriber, "write_second", "work::analysis_item",
      [&](SystemVerilogUvmTlm1Payload delivered) {
        callback_order.push_back(2);
        second_values.push_back(delivered.value.low_word().aval);
        if (throw_second)
          throw std::runtime_error{"contained analysis failure"};
      });
  const auto fifo_imp = fixture.tlm1.register_endpoint(
      analysis_endpoint(SystemVerilogUvmTlm1EndpointKind::Implementation,
                        subscriber, "analysis_fifo", 0, 0));
  const auto peer_imp = fixture.tlm1.register_analysis_implementation(
      peer, "write_peer", "work::analysis_item",
      [](SystemVerilogUvmTlm1Payload) {});
  fixture.tlm1.connect(port, export_endpoint);
  fixture.tlm1.connect(port, fifo_imp);
  fixture.tlm1.connect(export_endpoint, first_imp);
  fixture.tlm1.connect(export_endpoint, second_imp);
  require_error(
      "FSIM-UVM-TLM1-003", [&] { fixture.tlm1.connect(port, peer_imp); },
      "analysis fanout must still reject cross-root endpoints atomically");
  fixture.tlm1.resolve_all();
  fixture.tlm1.configure_analysis_fifo(fifo_imp, 1);

  const auto first = fixture.tlm1.write_analysis(port, analysis_payload(0x55));
  require(
      first.success() && first.deliveries.size() == 3 &&
          first.deliveries[0].implementation == first_imp &&
          first.deliveries[1].implementation == second_imp &&
          first.deliveries[2].implementation == fifo_imp &&
          callback_order == std::vector<unsigned>{1, 2} &&
          second_values == std::vector<std::uint64_t>{0x55} &&
          first.deliveries[0].payload.sequence ==
              first.deliveries[2].payload.sequence,
      "analysis broadcast must preserve resolution order and value snapshots");
  const auto fifo_value = fixture.tlm1.analysis_fifo_try_get(fifo_imp);
  require(fifo_value && fifo_value->value.low_word().aval == 0x55 &&
              fifo_value->owner_endpoint == fifo_imp,
          "analysis FIFO must retain an independent publication snapshot");

  const auto object = fixture.heap.allocate(item_descriptor());
  fixture.heap.property(object, "value").packed =
      PackedLogic4::from_aval_bval(32, 0x11, 0);
  bool second_saw_object_mutation{};
  fixture.tlm1.set_analysis_subscriber(
      first_imp, [&](SystemVerilogUvmTlm1Payload delivered) {
        fixture.heap.property(delivered.object, "value").packed =
            PackedLogic4::from_aval_bval(32, 0x99, 0);
      });
  fixture.tlm1.set_analysis_subscriber(
      second_imp, [&](SystemVerilogUvmTlm1Payload delivered) {
        second_saw_object_mutation =
            fixture.heap.property(delivered.object, "value")
                .packed.low_word()
                .aval == 0x99;
      });
  auto object_payload = analysis_payload(0);
  object_payload.object = object;
  const auto object_result = fixture.tlm1.write_analysis(port, object_payload);
  require(object_result.success() && second_saw_object_mutation,
          "analysis object subscribers must share object identity in delivery "
          "order");
  (void)fixture.tlm1.analysis_fifo_try_get(fifo_imp);

  fixture.tlm1.set_analysis_subscriber(
      first_imp, [&](SystemVerilogUvmTlm1Payload) {
        if (mutate_graph && !late_imp) {
          late_imp = fixture.tlm1.register_analysis_implementation(
              subscriber, "write_late", "work::analysis_item",
              [&](SystemVerilogUvmTlm1Payload) { ++late_calls; });
          fixture.tlm1.connect(port, late_imp);
        }
      });
  fixture.tlm1.set_analysis_subscriber(
      second_imp, [&](SystemVerilogUvmTlm1Payload) {
        if (throw_second)
          throw std::runtime_error{"contained analysis failure"};
      });
  mutate_graph = true;
  const auto mutation_result =
      fixture.tlm1.write_analysis(port, analysis_payload(0x66));
  require(mutation_result.success() && late_imp && late_calls == 0 &&
              !fixture.tlm1.binding_valid() &&
              mutation_result.deliveries.size() == 3,
          "current analysis publication must use immutable target and callback "
          "snapshots");
  (void)fixture.tlm1.analysis_fifo_try_get(fifo_imp);
  fixture.tlm1.resolve_all();
  mutate_graph = false;
  const auto rebound =
      fixture.tlm1.write_analysis(port, analysis_payload(0x67));
  require(rebound.success() && late_calls == 1 &&
              rebound.deliveries.size() == 4,
          "the next bound publication must observe graph mutations in stable "
          "order");
  (void)fixture.tlm1.analysis_fifo_try_get(fifo_imp);

  throw_second = true;
  const auto contained =
      fixture.tlm1.write_analysis(port, analysis_payload(0x77));
  require(!contained.success() && contained.failures.size() == 1 &&
              contained.failures.front().diagnostic_code ==
                  "FSIM-UVM-TLM1-007" &&
              contained.failures.front().implementation == second_imp &&
              contained.deliveries.size() == 3 && late_calls == 2,
          "analysis subscriber exceptions must be contained while fanout "
          "continues");
  const auto full_fifo =
      fixture.tlm1.write_analysis(port, analysis_payload(0x78));
  require(
      full_fifo.failures.size() == 2 &&
          std::ranges::all_of(full_fifo.failures,
                              [](const auto &failure) {
                                return failure.diagnostic_code ==
                                       "FSIM-UVM-TLM1-007";
                              }) &&
          full_fifo.deliveries.size() == 2 && late_calls == 3,
      "full analysis FIFOs and throwing subscribers must fail independently");
  (void)fixture.tlm1.analysis_fifo_try_get(fifo_imp);

  SystemVerilogUvmTlm1Limits recursive_limits;
  recursive_limits.maximum_analysis_recursion_depth = 2;
  SystemVerilogUvmTlm1Service recursive{fixture.heap, fixture.components,
                                        recursive_limits};
  const auto recursive_port = recursive.register_endpoint(
      analysis_endpoint(SystemVerilogUvmTlm1EndpointKind::Port, publisher,
                        "recursive_port", 1, 1));
  std::size_t recursive_calls{};
  std::size_t recursive_failures{};
  const auto recursive_imp = recursive.register_analysis_implementation(
      subscriber, "write_recursive", "work::analysis_item",
      [&](SystemVerilogUvmTlm1Payload delivered) {
        ++recursive_calls;
        const auto nested =
            recursive.write_analysis(recursive_port, std::move(delivered));
        recursive_failures += nested.failures.size();
      });
  recursive.connect(recursive_port, recursive_imp);
  recursive.resolve_all();
  const auto recursive_result =
      recursive.write_analysis(recursive_port, analysis_payload(0x88));
  require(recursive_result.success() && recursive_calls == 2 &&
              recursive_failures == 1,
          "recursive analysis publication must stop at its depth bound and "
          "contain failure");

  SystemVerilogUvmTlm1Limits callback_limits;
  callback_limits.maximum_analysis_callbacks_per_publication = 1;
  SystemVerilogUvmTlm1Service callback_bounded{fixture.heap, fixture.components,
                                               callback_limits};
  const auto bounded_port = callback_bounded.register_endpoint(
      analysis_endpoint(SystemVerilogUvmTlm1EndpointKind::Port, publisher,
                        "callback_port", 2, 2));
  std::size_t bounded_calls{};
  const auto bounded_first = callback_bounded.register_analysis_implementation(
      subscriber, "bounded_first", "work::analysis_item",
      [&](SystemVerilogUvmTlm1Payload) { ++bounded_calls; });
  const auto bounded_second = callback_bounded.register_analysis_implementation(
      subscriber, "bounded_second", "work::analysis_item",
      [&](SystemVerilogUvmTlm1Payload) { ++bounded_calls; });
  callback_bounded.connect(bounded_port, bounded_first);
  callback_bounded.connect(bounded_port, bounded_second);
  callback_bounded.resolve_all();
  require_error(
      "FSIM-UVM-TLM1-008",
      [&] {
        (void)callback_bounded.write_analysis(bounded_port,
                                              analysis_payload(0x99));
      },
      "analysis callback ceilings must reject before subscriber side effects");
  require(bounded_calls == 0, "bounded analysis rejection must be atomic");
}

} // namespace fsim::tests::runtime
