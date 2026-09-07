// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Record {
  PLI_INT32 type{accNet};
  PLI_INT32 full_type{accWire};
  PLI_INT32 width{1};
};

struct CapturedEvent {
  PLI_INT32 reason{};
  PLI_INT32 high{};
  PLI_INT32 low{};
  PLI_BYTE8* user_data{};
  PLI_UBYTE8 logic{};
  PLI_UBYTE8 strength1{};
  PLI_UBYTE8 strength2{};
  double real{};
  handle vector{};
};

struct Fixture {
  Registry registry{131};
  std::map<std::uint64_t, Record> records;
  fsim_acc_vcl_query_v3 captured{};
  std::uint64_t last_link{};
  std::uint32_t registrations{};
  bool reject_vcl{};
  bool throw_vcl{};
};

std::vector<CapturedEvent> events;
bool throw_consumer{};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

PLI_INT32 consumer(p_vc_record record) {
  if (throw_consumer) throw std::runtime_error("consumer failure");
  require(record != nullptr, "consumer receives a record");
  CapturedEvent event;
  event.reason = record->vc_reason;
  event.high = record->vc_hightime;
  event.low = record->vc_lowtime;
  event.user_data = record->user_data;
  switch (record->vc_reason) {
    case logic_value_change:
    case event_value_change: event.logic = record->out_value.logic_value; break;
    case strength_value_change:
      event.logic = record->out_value.strengths_s.logic_value;
      event.strength1 = record->out_value.strengths_s.strength1;
      event.strength2 = record->out_value.strengths_s.strength2;
      break;
    case real_value_change:
    case realtime_value_change: event.real = record->out_value.real_value; break;
    default: event.vector = record->out_value.vector_handle; break;
  }
  events.push_back(event);
  return 0;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto found = fixture.records.find(object);
  if (!fixture.registry.lookup(object) || found == fixture.records.end() ||
      type == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  *type = found->second.full_type;
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
      query->operation != FSIM_ACC_READ_OBJECT || query->reserved != 0 ||
      query->reserved2 != 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto found = fixture.records.find(query->object);
  if (found == fixture.records.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
  result->type = found->second.type;
  result->full_type = found->second.full_type;
  result->width = found->second.width;
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
  ++fixture.registrations;
  if (fixture.throw_vcl) throw std::runtime_error("registration failure");
  if (fixture.reject_vcl || query == nullptr ||
      query->abi_version != FSIM_ACC_VCL_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->reserved2 != 0 || query->operation != FSIM_ACC_VCL_REGISTER ||
      query->link_identity == 0 || query->object == 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  fixture.captured = *query;
  fixture.last_link = query->link_identity;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(Fixture& fixture,
                                                  const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          37,
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

[[nodiscard]] std::uint64_t add(Fixture& fixture, const std::uint64_t parent,
                                const std::string& name,
                                const Record& record) {
  const auto object = fixture.registry.create(Kind::Variable, parent, name);
  require(static_cast<bool>(object), "VCL fixture creates VPI identity");
  fixture.records.emplace(object.value, record);
  return object.value;
}

[[nodiscard]] handle mapped(const std::uint64_t object) {
  auto result = fsim_acc_handle_from_vpi_v3(object);
  require(result != nullptr, "VCL fixture maps VPI identity");
  return result;
}

[[nodiscard]] fsim_acc_vcl_event_v3 event(
    const std::uint64_t link, const PLI_INT32 reason) {
  static std::uint64_t sequence{};
  fsim_acc_vcl_event_v3 result{};
  result.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.reason = reason;
  result.link_identity = link;
  result.sequence_identity = ++sequence;
  result.time_high = 3;
  result.time_low = 17;
  return result;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root_object = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root_object), "VCL fixture creates root");
  const auto root = root_object.value;
  fixture.records.emplace(root, Record{accTopModule, accTopModule, 1});
  const auto scalar_object = add(fixture, root, "wire", Record{});
  const auto vector_object =
      add(fixture, root, "reg", Record{accReg, accReg, 40});
  const auto real_object =
      add(fixture, root, "real", Record{accRealVar, accRealVar, 64});
  const auto constant_object =
      add(fixture, root, "constant", Record{accConstant, accConstant, 1});

  auto active = context(fixture, root);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "VCL context enters");
  const auto scalar = mapped(scalar_object);
  const auto vector = mapped(vector_object);
  const auto real = mapped(real_object);
  const auto constant = mapped(constant_object);
  PLI_BYTE8 marker[] = "retained-user-data";

  acc_vcl_add(scalar, consumer, marker, vcl_verilog_logic);
  require(acc_error_flag == 0 && fixture.captured.object == scalar_object &&
              fixture.captured.object_type == accNet &&
              fixture.captured.object_full_type == accWire &&
              fixture.captured.flags == vcl_verilog_logic,
          "logic value-change registration publishes exact identity");
  auto logic = event(fixture.last_link, logic_value_change);
  logic.logic_value = acc1;
  require(fsim_acc_vcl_dispatch_v3(&active, &logic) == 1 &&
              events.back().reason == logic_value_change &&
              events.back().high == 3 && events.back().low == 17 &&
              events.back().user_data == marker && events.back().logic == acc1,
          "logic callback retains time value and user data");

  acc_vcl_add(scalar, consumer, marker, vcl_verilog_strength);
  require(acc_error_flag == 0, "strength registration is accepted for a net");
  auto strength = event(fixture.last_link, strength_value_change);
  strength.logic_value = accX;
  strength.strength1 = vclStrong;
  strength.strength2 = vclWeak;
  require(fsim_acc_vcl_dispatch_v3(&active, &strength) == 1 &&
              events.back().logic == accX &&
              events.back().strength1 == vclStrong &&
              events.back().strength2 == vclWeak,
          "strength callback retains logic and both strengths");

  acc_vcl_add(vector, consumer, marker, vcl_verilog_logic);
  const auto vector_link = fixture.last_link;
  std::array<fsim_acc_logic_word_v3, 2> words{{{0x12345678U, 0U},
                                               {0x5U, 0x2U}}};
  auto vector_event = event(vector_link, vector_value_change);
  vector_event.vector_width = 40;
  vector_event.logic_word_count = static_cast<std::uint32_t>(words.size());
  vector_event.logic_words = words.data();
  require(fsim_acc_vcl_dispatch_v3(&active, &vector_event) == 1 &&
              events.back().vector == vector,
          "vector callback retains the exact generation-qualified handle");

  acc_vcl_add(real, consumer, marker, vcl_verilog_logic);
  auto real_event = event(fixture.last_link, real_value_change);
  real_event.real_value = 8.5;
  require(fsim_acc_vcl_dispatch_v3(&active, &real_event) == 1 &&
              events.back().real == 8.5,
          "real callback retains its value");

  const auto calls_before_invalid = fixture.registrations;
  acc_vcl_add(constant, consumer, marker, vcl_verilog_logic);
  require(acc_error_flag == 1 && fixture.registrations == calls_before_invalid,
          "unsupported objects fail before registration dispatch");
  acc_vcl_add(vector, consumer, marker, vcl_verilog_strength);
  require(acc_error_flag == 1 && fixture.registrations == calls_before_invalid,
          "register strength links fail before registration dispatch");
  acc_vcl_add(scalar, nullptr, marker, vcl_verilog_logic);
  require(acc_error_flag == 1 && fixture.registrations == calls_before_invalid,
          "null consumers fail before registration dispatch");

  const auto events_before_invalid = events.size();
  vector_event.vector_width = 39;
  require(fsim_acc_vcl_dispatch_v3(&active, &vector_event) == 0 &&
              events.size() == events_before_invalid,
          "malformed vector events do not invoke the consumer");
  vector_event.vector_width = 40;
  vector_event.reserved = 1;
  require(fsim_acc_vcl_dispatch_v3(&active, &vector_event) == 0 &&
              events.size() == events_before_invalid,
          "malformed event records do not invoke the consumer");

  fixture.reject_vcl = true;
  acc_vcl_add(scalar, consumer, marker, vcl_verilog_logic);
  require(acc_error_flag == 1, "registration rejection is contained");
  fixture.reject_vcl = false;
  fixture.throw_vcl = true;
  acc_vcl_add(scalar, consumer, marker, vcl_verilog_logic);
  require(acc_error_flag == 1, "registration exceptions are contained");
  fixture.throw_vcl = false;

  throw_consumer = true;
  logic.reserved = 0;
  require(fsim_acc_vcl_dispatch_v3(&active, &logic) == 0 &&
              acc_error_flag == 1,
          "consumer exceptions are contained at the ACC boundary");
  throw_consumer = false;

  fsim_acc_handle_context_leave_v3(&active);
  require(fsim_acc_vcl_dispatch_v3(&active, &logic) == 0,
          "dispatch requires the exact active context");
  return 0;
}
