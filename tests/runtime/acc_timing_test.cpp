// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

constexpr std::array<PLI_INT32, 20> kTimingInventory{
    accSetup,       accHold,       accWidth,        accPeriod,
    accRecovery,    accSkew,       accNochange,     accSetuphold,
    accInput,       accOutput,     accInout,        accMixedIo,
    accPositive,    accNegative,   accUnknown,      accPathTerminal,
    accPathInput,   accPathOutput, accDataPath,     accTchkTerminal,
};
static_assert(kTimingInventory.size() == 20);

struct Record {
  PLI_INT32 type{accModPath};
  PLI_INT32 full_type{accModPath};
  PLI_INT32 direction{};
  std::uint32_t capabilities{FSIM_ACC_TIMING_CAP_ALL};
  std::uint32_t delay_count{FSIM_ACC_TIMING_MAXIMUM_DELAYS};
};

struct Captured {
  std::uint32_t operation{};
  std::uint64_t object{};
  PLI_INT32 type{};
  PLI_INT32 full_type{};
  std::uint32_t delay_count{};
  std::uint32_t min_typ_max{};
  PLI_INT32 to_hiz_policy{};
  std::vector<double> values;
};

struct Fixture {
  Registry registry{127};
  std::map<std::uint64_t, Record> records;
  Captured captured;
  std::vector<double> output;
  std::uint32_t calls{};
  bool throw_timing{};
  bool reject_timing{};
  bool malformed_result{};
  PLI_INT32 delay_mode{accDelayModePath};
  PLI_INT32 polarity{accNegative};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
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
  result->direction = found->second.direction;
  result->timing_capabilities = found->second.capabilities;
  result->timing_delay_count = found->second.delay_count;
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

std::uint32_t FSIM_NATIVE_PLUGIN_CALL timing(
    void* const user_data, const fsim_acc_timing_query_v3* const query,
    fsim_acc_timing_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  ++fixture.calls;
  if (fixture.throw_timing) throw std::runtime_error("timing failure");
  if (fixture.reject_timing || query == nullptr || result == nullptr ||
      query->abi_version != FSIM_ACC_TIMING_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->reserved2 != 0 || query->delay_count == 0 ||
      query->delay_count > FSIM_ACC_TIMING_MAXIMUM_DELAYS ||
      query->min_typ_max > 1 ||
      result->abi_version != FSIM_ACC_TIMING_QUERY_ABI_VERSION ||
      result->struct_size < sizeof(*result)) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  fixture.captured = {};
  fixture.captured.operation = query->operation;
  fixture.captured.object = query->object;
  fixture.captured.type = query->object_type;
  fixture.captured.full_type = query->object_full_type;
  fixture.captured.delay_count = query->delay_count;
  fixture.captured.min_typ_max = query->min_typ_max;
  fixture.captured.to_hiz_policy = query->to_hiz_policy;
  if (query->value_count != 0) {
    if (query->values == nullptr) return FSIM_ACC_VPI_OBJECT_INVALID;
    fixture.captured.values.assign(query->values,
                                   query->values + query->value_count);
  }
  fixture.output.clear();
  switch (query->operation) {
    case FSIM_ACC_TIMING_FETCH_DELAYS: {
      const auto count = query->delay_count * (query->min_typ_max ? 3U : 1U);
      for (std::uint32_t index = 0; index < count; ++index) {
        fixture.output.push_back(0.25 + static_cast<double>(index));
      }
      result->value_count = count;
      result->values = fixture.output.data();
      break;
    }
    case FSIM_ACC_TIMING_FETCH_PULSE:
      for (std::uint32_t index = 0; index < query->delay_count; ++index) {
        fixture.output.push_back(0.5 + static_cast<double>(index));
        fixture.output.push_back(0.75 + static_cast<double>(index));
      }
      result->value_count = static_cast<std::uint32_t>(fixture.output.size());
      result->values = fixture.output.data();
      break;
    case FSIM_ACC_TIMING_FETCH_DELAY_MODE:
      result->delay_mode = fixture.delay_mode;
      break;
    case FSIM_ACC_TIMING_FETCH_POLARITY:
      result->polarity = fixture.polarity;
      break;
    case FSIM_ACC_TIMING_APPEND_DELAYS:
    case FSIM_ACC_TIMING_REPLACE_DELAYS:
    case FSIM_ACC_TIMING_APPEND_PULSE:
    case FSIM_ACC_TIMING_REPLACE_PULSE:
    case FSIM_ACC_TIMING_SET_PULSE_PERCENT: break;
    default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (fixture.malformed_result) result->reserved = 1;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(Fixture& fixture,
                                                  const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          31,
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
          timing,
          unsupported_vcl,
          nullptr};
}

[[nodiscard]] std::uint64_t add(Fixture& fixture, const std::uint64_t parent,
                                const std::string& name,
                                const Record& record) {
  const auto object = fixture.registry.create(Kind::Variable, parent, name);
  require(static_cast<bool>(object), "timing fixture creates VPI identity");
  fixture.records.emplace(object.value, record);
  return object.value;
}

[[nodiscard]] handle mapped(const std::uint64_t object) {
  auto result = fsim_acc_handle_from_vpi_v3(object);
  require(result != nullptr, "timing fixture maps VPI identity");
  return result;
}

void configure(const PLI_INT32 item, const char* const value) {
  require(acc_configure(item, const_cast<PLI_BYTE8*>(value)) == 1,
          "timing configuration accepted");
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root_object = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root_object), "timing fixture creates root");
  const auto root = root_object.value;
  fixture.records.emplace(root, Record{accTopModule, accTopModule, 0, 0, 0});
  const auto path_object = add(fixture, root, "path", Record{});
  const auto primitive_object = add(
      fixture, root, "gate",
      Record{accPrimitive, accAndGate, 0, FSIM_ACC_TIMING_CAP_DELAYS, 2});
  const auto check_object = add(
      fixture, root, "setup",
      Record{accTchk, accSetup, 0, FSIM_ACC_TIMING_CAP_DELAYS, 1});
  const auto port_object = add(
      fixture, root, "input",
      Record{accPort, accScalarPort, accInput, FSIM_ACC_TIMING_CAP_DELAYS, 1});
  const auto net_object = add(
      fixture, root, "net",
      Record{accNet, accWire, 0, FSIM_ACC_TIMING_CAP_DELAYS, 1});

  require(acc_initialize() == 1, "ACC lifecycle initializes");
  auto active = context(fixture, root);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "timing context enters");
  const auto path = mapped(path_object);
  const auto primitive = mapped(primitive_object);
  const auto check = mapped(check_object);
  const auto port = mapped(port_object);
  const auto net = mapped(net_object);

  require(acc_fetch_delay_mode(path) == accDelayModePath &&
              fixture.captured.delay_count == 6,
          "default path count and delay mode are visible");
  require(acc_fetch_polarity(path) == accNegative,
          "path polarity is visible");

  configure(accPathDelayCount, "2");
  configure(accToHiZDelay, "maximum");
  double first{};
  double second{};
  require(acc_fetch_delays(path, &first, &second) == 1 && first == 0.25 &&
              second == 1.25 &&
              fixture.captured.to_hiz_policy ==
                  static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_MAXIMUM),
          "single delay values publish after validation");
  require(acc_append_delays(path, 1.0, 2.0) == 1 &&
              fixture.captured.operation == FSIM_ACC_TIMING_APPEND_DELAYS &&
              fixture.captured.values == std::vector<double>({1.0, 2.0}),
          "path delays append as one transaction");
  require(acc_replace_delays(primitive, 3.0, 4.0) == 1 &&
              fixture.captured.operation == FSIM_ACC_TIMING_REPLACE_DELAYS,
          "primitive delay arity comes from object metadata");
  require(acc_fetch_delays(check, &first) == 1 &&
              acc_fetch_delays(port, &second) == 1,
          "timing checks and input ports expose delays");
  require(acc_fetch_delays(net, &first) == 0 && acc_error_flag == 1,
          "non-timing nets reject delay access");

  double r0{};
  double e0{};
  double r1{};
  double e1{};
  require(acc_fetch_pulsere(path, &r0, &e0, &r1, &e1) == 1 && r0 == 0.5 &&
              e0 == 0.75 && r1 == 1.5 && e1 == 1.75,
          "pulse reject and error pairs publish atomically");
  require(acc_append_pulsere(path, 1.0, 2.0, 3.0, 4.0) == 1 &&
              fixture.captured.operation == FSIM_ACC_TIMING_APPEND_PULSE,
          "pulse pairs append");
  require(acc_replace_pulsere(path, 2.0, 3.0, 4.0, 5.0) == 1 &&
              fixture.captured.operation == FSIM_ACC_TIMING_REPLACE_PULSE,
          "pulse pairs replace");
  require(acc_set_pulsere(path, 25.0, 50.0) == 1 &&
              fixture.captured.operation ==
                  FSIM_ACC_TIMING_SET_PULSE_PERCENT,
          "pulse percentages are bounded and ordered");
  require(acc_set_pulsere(path, 70.0, 50.0) == 0 &&
              acc_set_pulsere(path, 10.0, 101.0) == 0,
          "invalid pulse percentages fail before callback");

  configure(accMinTypMaxDelays, "true");
  std::array<double, 6> triplets{};
  require(acc_fetch_delays(path, triplets.data()) == 1 &&
              triplets.front() == 0.25 && triplets.back() == 5.25 &&
              fixture.captured.min_typ_max == 1,
          "minimum typical maximum delays use one bounded array");
  const std::array<double, 6> replacement{1.0, 2.0, 3.0,
                                          4.0, 5.0, 6.0};
  require(acc_replace_delays(path, replacement.data()) == 1 &&
              fixture.captured.values ==
                  std::vector<double>(replacement.begin(), replacement.end()),
          "minimum typical maximum replacement copies input");

  triplets.fill(77.0);
  fixture.malformed_result = true;
  require(acc_fetch_delays(path, triplets.data()) == 0 &&
              triplets.front() == 77.0 && triplets.back() == 77.0,
          "malformed results cannot partially publish");
  fixture.malformed_result = false;
  fixture.throw_timing = true;
  require(acc_fetch_delay_mode(path) == accDelayModeNone &&
              acc_error_flag == 1,
          "callback exceptions are contained");
  fixture.throw_timing = false;
  fixture.reject_timing = true;
  require(acc_fetch_polarity(path) == accUnknown && acc_error_flag == 1,
          "callback rejection has a deterministic fallback");
  fixture.reject_timing = false;

  configure(accPathDelayCount, "4");
  require(acc_fetch_delay_mode(path) == accDelayModeNone &&
              acc_error_flag == 1,
          "unsupported path delay counts reject before callback");

  fsim_acc_handle_context_leave_v3(&active);
  acc_close();
  return 0;
}
