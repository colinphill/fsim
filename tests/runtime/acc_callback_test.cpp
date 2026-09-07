// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Fixture {
  Registry registry{137};
  std::map<std::uint64_t, PLI_INT32> types;
  fsim_acc_handle_context_v3* context{};
  std::uint64_t last_link{};
  std::uint32_t registrations{};
  std::uint32_t cancellations{};
  bool reject_cancellation{};
  bool dispatch_during_cancellation{};
  PLI_INT32 late_dispatch_result{-1};
};

struct ConsumerState {
  Fixture* fixture{};
  handle object{};
  std::uint64_t link{};
  std::uint64_t next_sequence{100};
  std::uint32_t calls{};
  PLI_INT32 nested_result{-1};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] fsim_acc_vcl_event_v3 event(const std::uint64_t link,
                                           const std::uint64_t sequence) {
  fsim_acc_vcl_event_v3 result{};
  result.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.reason = logic_value_change;
  result.link_identity = link;
  result.sequence_identity = sequence;
  result.logic_value = acc1;
  return result;
}

PLI_INT32 simple_consumer(p_vc_record record) {
  auto& state = *reinterpret_cast<ConsumerState*>(record->user_data);
  ++state.calls;
  return 0;
}

PLI_INT32 reentrant_consumer(p_vc_record record) {
  auto& state = *reinterpret_cast<ConsumerState*>(record->user_data);
  ++state.calls;
  auto nested = event(state.link, state.next_sequence++);
  state.nested_result =
      fsim_acc_vcl_dispatch_v3(state.fixture->context, &nested);
  return 0;
}

PLI_INT32 self_cancel_consumer(p_vc_record record) {
  auto& state = *reinterpret_cast<ConsumerState*>(record->user_data);
  ++state.calls;
  acc_vcl_delete(state.object, self_cancel_consumer,
                 reinterpret_cast<PLI_BYTE8*>(&state), vcl_verilog_logic);
  return 0;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto found = fixture.types.find(object);
  if (!fixture.registry.lookup(object) || found == fixture.types.end() ||
      type == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  *type = found->second;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_lookup(
    void*, std::uint32_t, std::uint64_t, const PLI_BYTE8*, std::uint32_t,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_relation(
    void*, std::uint32_t, std::uint64_t, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_name(
    void*, std::uint64_t, PLI_BYTE8*, std::uint32_t, std::uint32_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_traverse(
    void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t*,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_object_query(
    void*, const fsim_acc_object_query_v3*, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL read_object(
    void* const user_data, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query == nullptr || result == nullptr ||
      query->operation != FSIM_ACC_READ_OBJECT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto found = fixture.types.find(query->object);
  if (found == fixture.types.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
  result->type = found->second == accWire ? accNet : found->second;
  result->full_type = found->second;
  result->width = 1;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_write(
    void*, const fsim_acc_write_query_v3*, fsim_acc_write_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_iterate(
    void*, const fsim_acc_iterator_query_v3*, fsim_acc_iterator_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_timing(
    void*, const fsim_acc_timing_query_v3*, fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL vcl_control(
    void* const user_data, const fsim_acc_vcl_query_v3* const query) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query == nullptr || query->reserved != 0 || query->reserved2 != 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_VCL_REGISTER) {
    ++fixture.registrations;
    fixture.last_link = query->link_identity;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation != FSIM_ACC_VCL_UNREGISTER) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  ++fixture.cancellations;
  if (fixture.dispatch_during_cancellation) {
    auto late = event(query->link_identity, UINT64_C(1000000));
    fixture.late_dispatch_result =
        fsim_acc_vcl_dispatch_v3(fixture.context, &late);
  }
  return fixture.reject_cancellation ? FSIM_ACC_VPI_OBJECT_INVALID
                                     : FSIM_ACC_VPI_OBJECT_VALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(Fixture& fixture,
                                                  const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          41,
          256,
          0,
          &fixture,
          resolve_object,
          root,
          root,
          root,
          unsupported_lookup,
          unsupported_relation,
          unsupported_name,
          unsupported_traverse,
          unsupported_object_query,
          read_object,
          unsupported_write,
          unsupported_iterate,
          unsupported_timing,
          vcl_control,
          nullptr};
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root_object = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root_object), "callback fixture creates root");
  const auto net_object =
      fixture.registry.create(Kind::Variable, root_object.value, "wire");
  require(static_cast<bool>(net_object), "callback fixture creates net");
  fixture.types.emplace(root_object.value, accTopModule);
  fixture.types.emplace(net_object.value, accWire);
  auto active = context(fixture, root_object.value);
  fixture.context = &active;
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "callback context enters");
  const auto object = fsim_acc_handle_from_vpi_v3(net_object.value);
  require(object != nullptr, "callback fixture maps net");

  ConsumerState ordered{&fixture, object};
  auto* const ordered_data = reinterpret_cast<PLI_BYTE8*>(&ordered);
  acc_vcl_add(object, simple_consumer, ordered_data, vcl_verilog_logic);
  ordered.link = fixture.last_link;
  require(acc_error_flag == 0, "ordered callback registers");
  acc_vcl_add(object, simple_consumer, ordered_data, vcl_verilog_logic);
  require(acc_error_flag == 1 && fixture.registrations == 1,
          "duplicate callback tuples reject before simulator dispatch");
  auto first = event(ordered.link, 10);
  require(fsim_acc_vcl_dispatch_v3(&active, &first) == 1 &&
              ordered.calls == 1,
          "first callback sequence dispatches");
  auto stale = event(ordered.link, 9);
  require(fsim_acc_vcl_dispatch_v3(&active, &stale) == 0 &&
              ordered.calls == 1,
          "out-of-order callback sequence is rejected");
  auto next = event(ordered.link, 11);
  require(fsim_acc_vcl_dispatch_v3(&active, &next) == 1 &&
              ordered.calls == 2,
          "next monotonic callback sequence dispatches");

  fixture.reject_cancellation = true;
  acc_vcl_delete(object, simple_consumer, ordered_data, vcl_verilog_logic);
  require(acc_error_flag == 1 && fixture.cancellations == 1,
          "rejected cancellation restores the active callback");
  auto after_reject = event(ordered.link, 12);
  require(fsim_acc_vcl_dispatch_v3(&active, &after_reject) == 1 &&
              ordered.calls == 3,
          "callback remains active after rejected cancellation");
  fixture.reject_cancellation = false;
  fixture.dispatch_during_cancellation = true;
  acc_vcl_delete(object, simple_consumer, ordered_data, vcl_verilog_logic);
  require(acc_error_flag == 0 && fixture.cancellations == 2 &&
              fixture.late_dispatch_result == 0,
          "cancellation blocks simulator re-entry before unregister returns");
  auto after_delete = event(ordered.link, 13);
  require(fsim_acc_vcl_dispatch_v3(&active, &after_delete) == 0 &&
              ordered.calls == 3,
          "removed callbacks cannot publish late observations");
  fixture.dispatch_during_cancellation = false;

  ConsumerState reentrant{&fixture, object};
  auto* const reentrant_data = reinterpret_cast<PLI_BYTE8*>(&reentrant);
  acc_vcl_add(object, reentrant_consumer, reentrant_data, vcl_verilog_logic);
  reentrant.link = fixture.last_link;
  auto outer = event(reentrant.link, 20);
  require(fsim_acc_vcl_dispatch_v3(&active, &outer) == 1 &&
              reentrant.calls == 1 && reentrant.nested_result == 0,
          "same-link consumer re-entry is contained");
  acc_vcl_delete(object, reentrant_consumer, reentrant_data,
                 vcl_verilog_logic);
  require(acc_error_flag == 0, "re-entry test callback cancels");

  ConsumerState self_cancel{&fixture, object};
  auto* const self_data = reinterpret_cast<PLI_BYTE8*>(&self_cancel);
  acc_vcl_add(object, self_cancel_consumer, self_data, vcl_verilog_logic);
  self_cancel.link = fixture.last_link;
  auto self_event = event(self_cancel.link, 30);
  require(fsim_acc_vcl_dispatch_v3(&active, &self_event) == 1 &&
              self_cancel.calls == 1,
          "self-cancellation returns without deadlock");
  auto after_self = event(self_cancel.link, 31);
  require(fsim_acc_vcl_dispatch_v3(&active, &after_self) == 0 &&
              self_cancel.calls == 1,
          "self-cancelled links retire after callback return");

  acc_vcl_delete(object, simple_consumer, ordered_data, vcl_verilog_logic);
  require(acc_error_flag == 1,
          "deleting an absent exact callback tuple fails deterministically");
  fsim_acc_handle_context_leave_v3(&active);
  return 0;
}
