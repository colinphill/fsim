// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_tlm2.hpp"

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
  } catch (const SystemVerilogUvmTlm2Error &error) {
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

struct Tlm2Fixture {
  Scheduler scheduler;
  SystemVerilogClassHeap heap{{128, 16'384}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmTlm2Service tlm2;
  SystemVerilogUvmRootHandle root;
  SystemVerilogUvmRootHandle peer_root;

  Tlm2Fixture()
      : objects(heap,
                [&](const std::string_view, const std::string_view,
                    const std::string_view) {
                  return heap.allocate(component_descriptor());
                }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}),
        tlm2(components, scheduler), root(components.create_root("tlm2")),
        peer_root(components.create_root("peer")) {
    SystemVerilogUvmObjectDescriptor type;
    type.specialization_identity = "work::component";
    type.type_name = "component";
    objects.register_type(std::move(type));
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

SystemVerilogUvmTlm2Profile profile() {
  return {SystemVerilogUvmTlm2Protocol::Combined, "work::generic_payload#(bus)",
          "work::protocol_phase", 8};
}

SystemVerilogUvmTlm2SocketDescriptor
socket_descriptor(const SystemVerilogUvmTlm2SocketKind kind,
                  const SystemVerilogClassHandle component, std::string name,
                  SystemVerilogUvmTlm2Profile socket_profile = profile()) {
  const bool terminal = kind == SystemVerilogUvmTlm2SocketKind::Target;
  return {kind,
          std::move(socket_profile),
          component,
          std::move(name),
          terminal ? 0U : 1U,
          terminal ? 0U : 2U};
}

SystemVerilogUvmTlm2GenericPayload payload() {
  SystemVerilogUvmTlm2GenericPayload result;
  result.nominal_type = "work::generic_payload#(bus)";
  result.command = SystemVerilogUvmTlm2Command::Write;
  result.address = 0x1000;
  result.data = {0, 1, 2, 3, 4, 5, 6, 7};
  result.byte_enables = {0xff, 0x00, 0xff, 0xff};
  result.streaming_width = 8;
  result.extensions = {{"work::route_extension", {1, 2, 3}},
                       {"work::security_extension", {4, 5}}};
  return result;
}

SystemVerilogUvmTlm2Phase
protocol_phase(const SystemVerilogUvmTlm2PhaseKind kind,
               std::string identity = {}) {
  return {kind, std::move(identity), "work::protocol_phase"};
}

} // namespace

void test_systemverilog_uvm_tlm2() {
  static_assert(kSystemVerilogUvmOwnershipContract.tlm2 ==
                SystemVerilogUvmStateScope::Simulation);
  Tlm2Fixture fixture;
  const auto top = fixture.component("top");
  const auto initiator_component = fixture.component("initiator", top);
  const auto first_bridge = fixture.component("first_bridge", top);
  const auto second_bridge = fixture.component("second_bridge", top);
  const auto target_component = fixture.component("target", top);
  const auto peer_component = fixture.component("target", 0, fixture.peer_root);

  const auto initiator = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Initiator,
                        initiator_component, "initiator_socket"));
  const auto target_passthrough = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::TargetPassthrough,
                        first_bridge, "target_passthrough"));
  const auto initiator_passthrough = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::InitiatorPassthrough,
                        second_bridge, "initiator_passthrough"));
  const auto target = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Target,
                        target_component, "target_socket"));
  const auto peer_target = fixture.tlm2.register_socket(socket_descriptor(
      SystemVerilogUvmTlm2SocketKind::Target, peer_component, "target_socket"));
  auto wrong_profile = profile();
  wrong_profile.bus_width_bytes = 4;
  const auto wrong_target = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Target,
                        target_component, "wrong_target", wrong_profile));

  fixture.tlm2.connect(initiator, target_passthrough);
  fixture.tlm2.connect(target_passthrough, initiator_passthrough);
  fixture.tlm2.connect(initiator_passthrough, target);
  require_error(
      "FSIM-UVM-TLM2-002",
      [&] { fixture.tlm2.connect(initiator, peer_target); },
      "TLM2 connections must reject cross-root targets");
  require_error(
      "FSIM-UVM-TLM2-002",
      [&] { fixture.tlm2.connect(initiator, wrong_target); },
      "TLM2 connections must reject nominal profile mismatches");
  require_error(
      "FSIM-UVM-TLM2-002",
      [&] { fixture.tlm2.connect(initiator_passthrough, target_passthrough); },
      "TLM2 passthrough connections must reject cycles atomically");
  fixture.tlm2.bind_all();
  const auto initiator_snapshot = fixture.tlm2.snapshot(initiator);
  require(
      fixture.tlm2.binding_valid() &&
          initiator_snapshot.full_name == "top.initiator.initiator_socket" &&
          initiator_snapshot.debug_name ==
              "tlm2:top.initiator.initiator_socket" &&
          initiator_snapshot.resolved_targets ==
              std::vector<SystemVerilogUvmTlm2SocketHandle>{target},
      "TLM2 socket binding must preserve hierarchy and passthrough resolution");

  fixture.tlm2.set_blocking_handler(target, [](auto &transaction, auto &delay) {
    transaction.address += 0x20;
    transaction.data[0] = 0xa5;
    transaction.response_status = SystemVerilogUvmTlm2ResponseStatus::Ok;
    transaction.dmi_allowed = true;
    delay += 5;
  });
  const auto blocking = fixture.tlm2.b_transport(initiator, payload(), 2);
  require(blocking.target == target && blocking.delay == 7 &&
              blocking.completion_time == 7 &&
              blocking.payload.address == 0x1020 &&
              blocking.payload.data[0] == 0xa5 &&
              blocking.payload.extensions.size() == 2 &&
              blocking.payload.response_status ==
                  SystemVerilogUvmTlm2ResponseStatus::Ok &&
              blocking.payload.owner_root == fixture.root &&
              blocking.payload.owner_socket == initiator &&
              blocking.payload.transaction_id != 0,
          "blocking TLM2 transport must preserve payload state and annotated "
          "delay");

  auto wrong_payload = payload();
  wrong_payload.nominal_type = "work::other_payload";
  require_error(
      "FSIM-UVM-TLM2-003",
      [&] { (void)fixture.tlm2.b_transport(initiator, wrong_payload); },
      "TLM2 payload nominal mismatches must reject before callbacks");
  auto wrong_enable = payload();
  wrong_enable.byte_enables = {0x7f};
  require_error(
      "FSIM-UVM-TLM2-003",
      [&] { (void)fixture.tlm2.b_transport(initiator, wrong_enable); },
      "TLM2 malformed byte enables must reject before callbacks");

  fixture.tlm2.set_debug_handler(target, [](auto &transaction) {
    transaction.data[1] = 0x5a;
    return std::size_t{4};
  });
  auto debug_payload = payload();
  const auto debug_bytes = fixture.tlm2.transport_dbg(initiator, debug_payload);
  require(debug_bytes == 4 && debug_payload.data[1] == 0x5a,
          "debug transport must retain payload mutation and byte count");

  fixture.tlm2.set_dmi_handler(
      target, [](auto &) -> std::optional<SystemVerilogUvmTlm2Dmi> {
        return SystemVerilogUvmTlm2Dmi{0x1000,
                                       0x100f,
                                       true,
                                       true,
                                       2,
                                       3,
                                       std::vector<std::uint8_t>(16, 0xcc)};
      });
  auto dmi_payload = payload();
  const auto dmi = fixture.tlm2.get_direct_mem_ptr(initiator, dmi_payload);
  require(dmi && dmi->start_address == 0x1000 && dmi->end_address == 0x100f &&
              dmi->data.size() == 16 && dmi->read_latency == 2 &&
              dmi->write_latency == 3 && dmi_payload.dmi_allowed,
          "TLM2 DMI must preserve address, access, latency, and backing bytes");
  std::uint64_t invalidated_start{};
  std::uint64_t invalidated_end{};
  fixture.tlm2.set_dmi_invalidation_handler(
      initiator, [&](const auto start, const auto end) {
        invalidated_start = start;
        invalidated_end = end;
      });
  fixture.tlm2.invalidate_direct_memory(initiator, 0x1004, 0x1007);
  require(invalidated_start == 0x1004 && invalidated_end == 0x1007,
          "TLM2 DMI invalidation must preserve exact inclusive range");
  fixture.tlm2.set_dmi_handler(
      target, [](auto &) -> std::optional<SystemVerilogUvmTlm2Dmi> {
        return SystemVerilogUvmTlm2Dmi{9, 8, true, false, 0, 0, {1}};
      });
  require_error(
      "FSIM-UVM-TLM2-003",
      [&] {
        auto selected = payload();
        (void)fixture.tlm2.get_direct_mem_ptr(initiator, selected);
      },
      "malformed TLM2 DMI ranges must reject deterministically");
  fixture.tlm2.set_debug_handler(target, [](auto &) { return std::size_t{9}; });
  require_error(
      "FSIM-UVM-TLM2-004",
      [&] {
        auto selected = payload();
        (void)fixture.tlm2.transport_dbg(initiator, selected);
      },
      "debug transport byte counts cannot exceed payload storage");

  fixture.tlm2.set_forward_handler(
      target, [](auto &, auto &phase, auto &delay) {
        phase.kind = SystemVerilogUvmTlm2PhaseKind::EndRequest;
        delay += 2;
        return SystemVerilogUvmTlm2Sync::Updated;
      });
  fixture.tlm2.set_backward_handler(
      initiator, [](auto &transaction, auto &phase, auto &delay) {
        transaction.response_status = SystemVerilogUvmTlm2ResponseStatus::Ok;
        phase.kind = SystemVerilogUvmTlm2PhaseKind::EndResponse;
        delay += 3;
        return SystemVerilogUvmTlm2Sync::Completed;
      });
  auto wrong_phase =
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest);
  wrong_phase.nominal_type = "work::other_phase";
  require_error(
      "FSIM-UVM-TLM2-004",
      [&] {
        (void)fixture.tlm2.nb_transport_fw(initiator, payload(), wrong_phase);
      },
      "TLM2 phase nominal mismatches must reject before callbacks");
  const auto forward = fixture.tlm2.nb_transport_fw(
      initiator, payload(),
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest), 1);
  require(forward.sync == SystemVerilogUvmTlm2Sync::Updated &&
              forward.phase.kind == SystemVerilogUvmTlm2PhaseKind::EndRequest &&
              forward.delay == 3 && fixture.tlm2.outstanding_count() == 1 &&
              fixture.tlm2.transaction_snapshot(forward.transaction).hops == 3,
          "forward nonblocking transport must retain phase, delay, and hop "
          "identity");
  const auto backward = fixture.tlm2.nb_transport_bw(
      forward.transaction,
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginResponse), 4);
  const auto completed = fixture.tlm2.transaction_snapshot(forward.transaction);
  require(
      backward.sync == SystemVerilogUvmTlm2Sync::Completed &&
          backward.phase.kind == SystemVerilogUvmTlm2PhaseKind::EndResponse &&
          backward.delay == 7 &&
          completed.state == SystemVerilogUvmTlm2TransactionState::Completed &&
          completed.payload.response_status ==
              SystemVerilogUvmTlm2ResponseStatus::Ok &&
          completed.callbacks == 2 && fixture.tlm2.outstanding_count() == 0,
      "backward nonblocking transport must complete the exact transaction");

  fixture.tlm2.set_forward_handler(target, [](auto &, auto &, auto &) {
    return SystemVerilogUvmTlm2Sync::Accepted;
  });
  const auto accepted = fixture.tlm2.nb_transport_fw(
      initiator, payload(),
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::Custom, "BEGIN_DATA"));
  require(
      fixture.tlm2.transaction_snapshot(accepted.transaction).phase.identity ==
          "BEGIN_DATA",
      "custom nominal TLM2 phase identity must survive accepted transport");
  fixture.tlm2.cancel_transaction(accepted.transaction);

  fixture.tlm2.set_forward_handler(
      target, [](auto &transaction, auto &phase, auto &) {
        transaction.response_status = SystemVerilogUvmTlm2ResponseStatus::Ok;
        phase.kind = SystemVerilogUvmTlm2PhaseKind::EndResponse;
        return SystemVerilogUvmTlm2Sync::Completed;
      });
  const auto immediate = fixture.tlm2.nb_transport_fw(
      initiator, payload(),
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest));
  require(fixture.tlm2.transaction_snapshot(immediate.transaction).state ==
              SystemVerilogUvmTlm2TransactionState::Completed,
          "completed forward transport must publish a stable final snapshot");

  fixture.tlm2.set_forward_handler(target, [](auto &, auto &phase, auto &) {
    phase.kind = SystemVerilogUvmTlm2PhaseKind::EndRequest;
    return SystemVerilogUvmTlm2Sync::Accepted;
  });
  const auto transaction_count = fixture.tlm2.outstanding_count();
  require_error(
      "FSIM-UVM-TLM2-004",
      [&] {
        (void)fixture.tlm2.nb_transport_fw(
            initiator, payload(),
            protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest));
      },
      "accepted transport must reject illegal phase mutation");
  require(fixture.tlm2.outstanding_count() == transaction_count,
          "invalid forward callback result must not publish a transaction");

  fixture.tlm2.set_blocking_handler(target, [](auto &, auto &) {
    throw std::runtime_error{"contained target failure"};
  });
  require_error(
      "FSIM-UVM-TLM2-004",
      [&] { (void)fixture.tlm2.b_transport(initiator, payload()); },
      "TLM2 target callback exceptions must be contained diagnostically");

  SystemVerilogUvmTlm2Limits bounded_limits;
  bounded_limits.maximum_payload_bytes = 4;
  bounded_limits.maximum_extensions = 1;
  bounded_limits.maximum_callbacks = 1;
  bounded_limits.maximum_outstanding_transactions = 1;
  SystemVerilogUvmTlm2Service bounded{fixture.components, fixture.scheduler,
                                      bounded_limits};
  const auto bounded_initiator = bounded.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Initiator,
                        initiator_component, "bounded_initiator"));
  const auto bounded_target = bounded.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Target,
                        target_component, "bounded_target"));
  bounded.connect(bounded_initiator, bounded_target);
  bounded.bind_all();
  bounded.set_blocking_handler(bounded_target, [](auto &transaction, auto &) {
    transaction.response_status = SystemVerilogUvmTlm2ResponseStatus::Ok;
  });
  require_error(
      "FSIM-UVM-TLM2-005",
      [&] { (void)bounded.b_transport(bounded_initiator, payload()); },
      "TLM2 payload and extension ceilings must reject before callbacks");
  auto small_payload = payload();
  small_payload.data.resize(4);
  small_payload.byte_enables = {0xff};
  small_payload.streaming_width = 4;
  small_payload.extensions.clear();
  (void)bounded.b_transport(bounded_initiator, small_payload);
  bounded.set_debug_handler(bounded_target,
                            [](auto &) { return std::size_t{0}; });
  require_error(
      "FSIM-UVM-TLM2-005",
      [&] {
        auto selected = small_payload;
        (void)bounded.transport_dbg(bounded_initiator, selected);
      },
      "TLM2 callback ceilings must reject before another handler runs");

  SystemVerilogUvmTlm2Limits outstanding_limits;
  outstanding_limits.maximum_outstanding_transactions = 1;
  SystemVerilogUvmTlm2Service outstanding{fixture.components, fixture.scheduler,
                                          outstanding_limits};
  const auto outstanding_initiator = outstanding.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Initiator,
                        initiator_component, "outstanding_initiator"));
  const auto outstanding_target = outstanding.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Target,
                        target_component, "outstanding_target"));
  outstanding.connect(outstanding_initiator, outstanding_target);
  outstanding.bind_all();
  outstanding.set_forward_handler(outstanding_target,
                                  [](auto &, auto &, auto &) {
                                    return SystemVerilogUvmTlm2Sync::Accepted;
                                  });
  const auto held = outstanding.nb_transport_fw(
      outstanding_initiator, payload(),
      protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest));
  require_error(
      "FSIM-UVM-TLM2-005",
      [&] {
        (void)outstanding.nb_transport_fw(
            outstanding_initiator, payload(),
            protocol_phase(SystemVerilogUvmTlm2PhaseKind::BeginRequest));
      },
      "TLM2 outstanding transaction ceiling must reject before callbacks");
  outstanding.cancel_root(fixture.root);
  require(outstanding.transaction_snapshot(held.transaction).state ==
              SystemVerilogUvmTlm2TransactionState::Cancelled,
          "root teardown must cancel matching outstanding TLM2 transactions");

  SystemVerilogUvmTlm2Limits hop_limits;
  hop_limits.maximum_hops = 2;
  SystemVerilogUvmTlm2Service hop_bounded{fixture.components, hop_limits};
  const auto hop_initiator = hop_bounded.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Initiator,
                        initiator_component, "hop_initiator"));
  const auto hop_passthrough = hop_bounded.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::TargetPassthrough,
                        first_bridge, "hop_passthrough"));
  const auto hop_target = hop_bounded.register_socket(socket_descriptor(
      SystemVerilogUvmTlm2SocketKind::Target, target_component, "hop_target"));
  hop_bounded.connect(hop_passthrough, hop_target);
  hop_bounded.connect(hop_initiator, hop_passthrough);
  require_error(
      "FSIM-UVM-TLM2-005", [&] { hop_bounded.bind_all(); },
      "TLM2 hop ceilings must reject binding without publication");
  require(!hop_bounded.binding_valid(),
          "failed bounded TLM2 binding must remain unpublished");

  SystemVerilogUvmTlm2Service foreign_owner{fixture.components};
  require_error(
      "FSIM-UVM-TLM2-001", [&] { (void)foreign_owner.snapshot(initiator); },
      "TLM2 handles must reject in a different service owner");
  const auto doomed_component = fixture.component("doomed", top);
  const auto stale_socket = fixture.tlm2.register_socket(
      socket_descriptor(SystemVerilogUvmTlm2SocketKind::Target,
                        doomed_component, "stale_target"));
  fixture.components.release(doomed_component);
  require_error(
      "FSIM-UVM-TLM2-001", [&] { (void)fixture.tlm2.snapshot(stale_socket); },
      "component teardown must stale its TLM2 socket ownership");
}

} // namespace fsim::tests::runtime
