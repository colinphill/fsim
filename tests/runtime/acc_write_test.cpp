// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Record {
  PLI_INT32 type{accReg};
  PLI_INT32 full_type{accReg};
  PLI_INT32 width{40};
  std::uint32_t write_capabilities{
      FSIM_ACC_WRITE_CAP_DEPOSIT | FSIM_ACC_WRITE_CAP_FORCE |
      FSIM_ACC_WRITE_CAP_RELEASE | FSIM_ACC_WRITE_CAP_ASSIGN |
      FSIM_ACC_WRITE_CAP_DEASSIGN};
  std::vector<fsim_acc_logic_word_v3> words{{UINT32_C(0x76543210), 0},
                                             {UINT32_C(0xab), 0}};
  std::string binary{"1010101101110110010101000011001000010000"};
  std::string octal{"25335425062020"};
  std::string decimal{"736957280784"};
  std::string hexadecimal{"ab76543210"};
  std::string text{"returned"};
  double real{6.25};
  std::uint32_t value_kind{FSIM_ACC_READ_VALUE_LOGIC4};
};

struct Captured {
  std::uint64_t object{};
  PLI_INT32 type{};
  PLI_INT32 full_type{};
  PLI_INT32 model{};
  PLI_INT32 time_type{};
  std::uint64_t ticks{};
  double real_time{};
  PLI_INT32 format{};
  PLI_INT32 width{};
  std::vector<fsim_acc_logic_word_v3> words;
  std::string text;
  double real_value{};
};

struct Fixture {
  Registry registry{97};
  std::map<std::uint64_t, Record> records;
  Captured captured;
  std::uint32_t write_calls{};
  bool throw_write{};
  bool reject_write{};
  bool malformed_result{};
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

void set_string(const std::string& source, const PLI_BYTE8*& pointer,
                std::uint32_t& size) {
  pointer = source.data();
  size = static_cast<std::uint32_t>(source.size());
}

void publish_record(const Record& record, fsim_acc_read_result_v3& result) {
  result.type = record.type;
  result.full_type = record.full_type;
  result.width = record.width;
  result.write_capabilities = record.write_capabilities;
  result.value_kind = record.value_kind;
  result.logic_word_count = static_cast<std::uint32_t>(record.words.size());
  result.logic_words = record.words.data();
  result.real_value = record.real;
  set_string(record.binary, result.binary_value, result.binary_value_size);
  set_string(record.octal, result.octal_value, result.octal_value_size);
  set_string(record.decimal, result.decimal_value, result.decimal_value_size);
  set_string(record.hexadecimal, result.hexadecimal_value,
             result.hexadecimal_value_size);
  set_string(record.text, result.string_value, result.string_value_size);
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
  publish_record(found->second, *result);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL write_object(
    void* const user_data, const fsim_acc_write_query_v3* const query,
    fsim_acc_write_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  ++fixture.write_calls;
  if (fixture.throw_write) throw std::runtime_error("write failure");
  if (fixture.reject_write) return FSIM_ACC_VPI_OBJECT_INVALID;
  if (query == nullptr || result == nullptr ||
      query->abi_version != FSIM_ACC_WRITE_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) ||
      query->operation != FSIM_ACC_WRITE_SET_VALUE || query->reserved != 0 ||
      query->value.reserved != 0 || query->value.reserved2 != 0 ||
      result->abi_version != FSIM_ACC_WRITE_QUERY_ABI_VERSION ||
      result->struct_size < sizeof(*result) || result->reserved != 0 ||
      result->reserved2 != 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto found = fixture.records.find(query->object);
  if (found == fixture.records.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
  fixture.captured = {};
  fixture.captured.object = query->object;
  fixture.captured.type = query->object_type;
  fixture.captured.full_type = query->object_full_type;
  fixture.captured.model = query->model;
  fixture.captured.time_type = query->time_type;
  fixture.captured.ticks = query->time_ticks;
  fixture.captured.real_time = query->time_real;
  fixture.captured.format = query->value.format;
  fixture.captured.width = query->value.width;
  fixture.captured.real_value = query->value.real_value;
  if (query->value.logic_word_count != 0) {
    fixture.captured.words.assign(
        query->value.logic_words,
        query->value.logic_words + query->value.logic_word_count);
  }
  if (query->value.string_value_size != 0) {
    fixture.captured.text.assign(query->value.string_value,
                                 query->value.string_value_size);
  }
  if (query->model == accReleaseFlag || query->model == accDeassignFlag) {
    publish_record(found->second, result->current_value);
  }
  if (fixture.malformed_result) result->current_value.width = -1;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_iterate(
    void*, const fsim_acc_iterator_query_v3*, fsim_acc_iterator_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_timing(
    void*, const fsim_acc_timing_query_v3*, fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(
    Fixture& fixture, const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          23,
          128,
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
          write_object,
          unsupported_iterate,
          unsupported_timing,
          unsupported_vcl,
          nullptr};
}

[[nodiscard]] std::uint64_t add(Fixture& fixture, const Kind kind,
                                const std::uint64_t parent,
                                const std::string& name,
                                const Record& record) {
  const auto object = fixture.registry.create(kind, parent, name);
  require(static_cast<bool>(object), "write fixture creates VPI identity");
  fixture.records.emplace(object.value, record);
  return object.value;
}

[[nodiscard]] s_setval_delay delay(const PLI_INT32 model) {
  s_setval_delay result{};
  result.model = model;
  return result;
}

}  // namespace

int main() {
  Fixture fixture;
  Record root_record;
  root_record.type = accModule;
  root_record.full_type = accTopModule;
  root_record.width = 1;
  root_record.write_capabilities = 0;
  root_record.words = {{0, 0}};
  const auto root = add(fixture, Kind::Root, 0, "top", root_record);
  Record reg_record;
  const auto reg = add(fixture, Kind::Variable, root, "data", reg_record);
  Record net_record = reg_record;
  net_record.type = accNet;
  net_record.full_type = accWire;
  net_record.write_capabilities =
      FSIM_ACC_WRITE_CAP_FORCE | FSIM_ACC_WRITE_CAP_RELEASE;
  const auto net = add(fixture, Kind::Net, root, "wire_data", net_record);
  Record scalar_record = reg_record;
  scalar_record.width = 1;
  scalar_record.words = {{1, 1}};
  scalar_record.binary = "x";
  const auto scalar =
      add(fixture, Kind::Variable, root, "scalar", scalar_record);
  Record real_record = reg_record;
  real_record.type = accRealVar;
  real_record.full_type = accRealVar;
  real_record.width = 64;
  real_record.words.clear();
  real_record.value_kind = FSIM_ACC_READ_VALUE_REAL;
  const auto real = add(fixture, Kind::Variable, root, "real", real_record);
  Record parameter_record = reg_record;
  parameter_record.type = accParameter;
  parameter_record.full_type = accIntegerParam;
  parameter_record.write_capabilities = 0;
  const auto parameter =
      add(fixture, Kind::Parameter, root, "P", parameter_record);
  constexpr std::array write_subtypes{
      accBit, accPortBit, accNetBit, accRegBit, accBitSelect, accPartSelect};
  std::array<std::uint64_t, write_subtypes.size()> subtype_objects{};
  for (std::size_t index = 0; index < write_subtypes.size(); ++index) {
    Record subtype_record = scalar_record;
    subtype_record.type = write_subtypes[index];
    subtype_record.full_type = write_subtypes[index];
    subtype_record.write_capabilities =
        FSIM_ACC_WRITE_CAP_FORCE | FSIM_ACC_WRITE_CAP_RELEASE;
    if (write_subtypes[index] == accRegBit ||
        write_subtypes[index] == accBitSelect ||
        write_subtypes[index] == accPartSelect) {
      subtype_record.write_capabilities |= FSIM_ACC_WRITE_CAP_DEPOSIT |
                                           FSIM_ACC_WRITE_CAP_ASSIGN |
                                           FSIM_ACC_WRITE_CAP_DEASSIGN;
    }
    subtype_objects[index] = add(fixture, Kind::Variable, root,
                                 "subtype" + std::to_string(index),
                                 subtype_record);
  }

  auto active = context(fixture, root);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "write fixture enters its exact v3 context");
  auto reg_handle = fsim_acc_handle_from_vpi_v3(reg);
  auto net_handle = fsim_acc_handle_from_vpi_v3(net);
  auto scalar_handle = fsim_acc_handle_from_vpi_v3(scalar);
  auto real_handle = fsim_acc_handle_from_vpi_v3(real);
  auto parameter_handle = fsim_acc_handle_from_vpi_v3(parameter);
  require(reg_handle && net_handle && scalar_handle && real_handle &&
              parameter_handle,
          "write fixture maps every target through the VPI registry");
  std::array<handle, write_subtypes.size()> subtype_handles{};
  for (std::size_t index = 0; index < write_subtypes.size(); ++index) {
    subtype_handles[index] =
        fsim_acc_handle_from_vpi_v3(subtype_objects[index]);
    require(subtype_handles[index] != nullptr,
            "write subtype maps through the VPI registry");
  }

  std::array<s_acc_vecval, 2> words{{
      {std::bit_cast<PLI_INT32>(UINT32_C(0x89abcdef)),
       std::bit_cast<PLI_INT32>(UINT32_C(0x10203040))},
      {0x12, 0x34}}};
  s_setval_value value{};
  value.format = accVectorVal;
  value.value.vector = words.data();
  auto timing = delay(accNoDelay);
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              acc_error_flag == 0 && fixture.write_calls == 1 &&
              fixture.captured.object == reg &&
              fixture.captured.model == accNoDelay &&
              fixture.captured.time_type == FSIM_ACC_WRITE_TIME_NONE &&
              fixture.captured.words.size() == 2 &&
              fixture.captured.words[0].aval == UINT32_C(0x89abcdef) &&
              fixture.captured.words[0].bval == UINT32_C(0x10203040),
          "no-delay vector deposit copies every four-state word atomically");
  value.format = accScalarVal;
  value.value.scalar = accZ;
  require(acc_set_value(scalar_handle, &value, &timing) == 0 &&
              fixture.captured.words.size() == 1 &&
              fixture.captured.words[0].aval == 0 &&
              fixture.captured.words[0].bval == 1,
          "scalar deposit preserves high-impedance four-state encoding");
  constexpr std::array string_formats{
      accBinStrVal, accOctStrVal, accDecStrVal, accHexStrVal, accStringVal};
  constexpr std::array<const char*, string_formats.size()> string_values{
      "10xz", "17xz", "12345", "abxz", "payload"};
  for (std::size_t index = 0; index < string_formats.size(); ++index) {
    value.format = string_formats[index];
    value.value.str = const_cast<char*>(string_values[index]);
    require(acc_set_value(reg_handle, &value, &timing) == 0 &&
                fixture.captured.format == string_formats[index] &&
                fixture.captured.text == string_values[index],
            "every standardized string input format is copied exactly");
  }

  timing = delay(accInertialDelay);
  timing.time.type = accTime;
  timing.time.high = 1;
  timing.time.low = 2;
  value.format = accHexStrVal;
  value.value.str = const_cast<char*>("12_ab");
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              fixture.captured.time_type == accTime &&
              fixture.captured.ticks == UINT64_C(0x100000002) &&
              fixture.captured.text == "12_ab",
          "inertial deposits retain module-scaled integer delay and text");
  timing.model = accTransportDelay;
  timing.time.type = accSimTime;
  timing.time.high = 0;
  timing.time.low = 17;
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              fixture.captured.model == accTransportDelay &&
              fixture.captured.time_type == accSimTime &&
              fixture.captured.ticks == 17,
          "modified transport deposits retain simulator ticks");
  timing.model = accPureTransportDelay;
  timing.time.type = accRealTime;
  timing.time.real = 2.75;
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              fixture.captured.model == accPureTransportDelay &&
              std::abs(fixture.captured.real_time - 2.75) < 1e-9,
          "pure transport deposits retain finite real delay");

  timing = delay(accForceFlag);
  value.format = accIntVal;
  value.value.integer = std::bit_cast<PLI_INT32>(UINT32_C(0xf1234567));
  require(acc_set_value(net_handle, &value, &timing) == 0 &&
              fixture.captured.model == accForceFlag &&
              fixture.captured.words.size() == 1 &&
              fixture.captured.words[0].aval == UINT32_C(0xf1234567),
          "force accepts nets and preserves integer bit patterns");
  value.format = accScalarVal;
  value.value.scalar = acc1;
  for (std::size_t index = 0; index < subtype_handles.size(); ++index) {
    require(acc_set_value(subtype_handles[index], &value, &timing) == 0 &&
                fixture.captured.full_type == write_subtypes[index],
            "every standardized write subtype retains exact identity");
  }
  timing = delay(accAssignFlag);
  value.format = accRealVal;
  value.value.real = 9.5;
  require(acc_set_value(real_handle, &value, &timing) == 0 &&
              fixture.captured.model == accAssignFlag &&
              std::abs(fixture.captured.real_value - 9.5) < 1e-9,
          "procedural assign accepts real variables without conversion");

  timing = delay(accReleaseFlag);
  value.format = accVectorVal;
  value.value.vector = words.data();
  require(acc_set_value(net_handle, &value, &timing) == 0 &&
              words[0].aval == 0x76543210 && words[1].aval == 0xab,
          "release returns the post-release vector in the same transaction");
  timing = delay(accDeassignFlag);
  value.format = accHexStrVal;
  value.value.str = nullptr;
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              value.value.str != nullptr &&
              std::string{value.value.str} == reg_record.hexadecimal,
          "deassign returns copied formatted storage in the same transaction");
  timing = delay(accReleaseFlag);
  value.format = accScalarVal;
  require(acc_set_value(scalar_handle, &value, &timing) == 0 &&
              value.value.scalar == accX,
          "release preserves scalar unknown encoding");

  const auto calls_before_rejection = fixture.write_calls;
  timing = delay(accInertialDelay);
  timing.time.type = accRealTime;
  timing.time.real = -1.0;
  value.format = accBinStrVal;
  value.value.str = const_cast<char*>("1");
  require(acc_set_value(reg_handle, &value, &timing) != 0 &&
              fixture.write_calls == calls_before_rejection,
          "negative real delays fail before callback dispatch");
  timing = delay(accAssignFlag);
  require(acc_set_value(net_handle, &value, &timing) != 0 &&
              fixture.write_calls == calls_before_rejection,
          "procedural assign rejects net targets before dispatch");
  timing = delay(accForceFlag);
  require(acc_set_value(parameter_handle, &value, &timing) != 0 &&
              fixture.write_calls == calls_before_rejection,
          "force rejects parameter targets before dispatch");
  timing = delay(accNoDelay);
  value.format = accScalarVal;
  value.value.scalar = acc1;
  require(acc_set_value(reg_handle, &value, &timing) != 0 &&
              fixture.write_calls == calls_before_rejection,
          "scalar input rejects a nonscalar target before dispatch");
  timing = delay(accReleaseFlag);
  value.format = accVectorVal;
  value.value.vector = reinterpret_cast<p_acc_vecval>(1);
  require(acc_set_value(net_handle, &value, &timing) != 0 &&
              fixture.write_calls == calls_before_rejection,
          "release validates its return destination before dispatch");

  value.format = accHexStrVal;
  value.value.str = nullptr;
  fixture.malformed_result = true;
  require(acc_set_value(net_handle, &value, &timing) != 0 &&
              acc_error_flag == 1,
          "malformed post-release values fail without caller publication");
  fixture.malformed_result = false;
  timing = delay(accNoDelay);
  value.format = accStringVal;
  value.value.str = const_cast<char*>("");
  require(acc_set_value(reg_handle, &value, &timing) == 0 &&
              fixture.captured.text.empty(),
          "empty string deposits remain valid bounded values");
  fixture.reject_write = true;
  require(acc_set_value(reg_handle, &value, &timing) != 0 &&
              acc_error_flag == 1,
          "write callback rejection reports one deterministic failure");
  fixture.reject_write = false;
  fixture.throw_write = true;
  require(acc_set_value(reg_handle, &value, &timing) != 0 &&
              acc_error_flag == 1,
          "write callback exceptions are contained at the ACC boundary");
  fixture.throw_write = false;

  fsim_acc_handle_context_leave_v3(&active);
  active.write = nullptr;
  require(fsim_acc_handle_context_enter_v3(&active) == 0 &&
              acc_error_flag == 1,
          "context entry rejects a missing write callback");
  return 0;
}
