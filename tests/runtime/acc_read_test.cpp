// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
  PLI_INT32 direction{accInput};
  PLI_INT32 edge{accPosedge};
  PLI_INT32 index{3};
  PLI_INT32 parameter_type{accIntegerParam};
  PLI_INT32 width{40};
  PLI_INT32 has_range{1};
  PLI_INT32 msb{39};
  PLI_INT32 lsb{0};
  PLI_INT16 unit{-9};
  PLI_INT16 precision{-12};
  PLI_INT32 line{27};
  std::uint32_t value_kind{FSIM_ACC_READ_VALUE_LOGIC4};
  std::vector<fsim_acc_logic_word_v3> words{{UINT32_C(0x89abcdef), 0},
                                             {UINT32_C(0x12), 0}};
  double real_value{2.5};
  double parameter_value{17.0};
  std::string name{"data"};
  std::string full_name{"top.data"};
  std::string definition_name{"wire"};
  std::string source_file{"logical/read.sv"};
  std::string string_value{"payload"};
  std::string binary{"0001001010001001101010111100110111101111"};
  std::string octal{"022104657157"};
  std::string decimal{"79725358575"};
  std::string hexadecimal{"1289abcdef"};
  std::string strength{"St1"};
};

struct Fixture {
  Registry registry{83};
  std::map<std::uint64_t, Record> records;
  bool throw_resolve{};
  bool throw_read{};
  bool malformed_result{};
  std::uint32_t calls{};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (fixture.throw_resolve) throw std::runtime_error("resolve failure");
  if (!fixture.registry.lookup(object) || type == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto found = fixture.records.find(object);
  if (found == fixture.records.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
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
  result.direction = record.direction;
  result.edge = record.edge;
  result.index = record.index;
  result.parameter_type = record.parameter_type;
  result.width = record.width;
  result.has_range = record.has_range;
  result.msb = record.msb;
  result.lsb = record.lsb;
  result.timescale_unit = record.unit;
  result.timescale_precision = record.precision;
  result.source_line = record.line;
  result.value_kind = record.value_kind;
  result.logic_word_count = static_cast<std::uint32_t>(record.words.size());
  result.logic_words = record.words.data();
  result.real_value = record.real_value;
  result.parameter_value = record.parameter_value;
  set_string(record.name, result.name, result.name_size);
  set_string(record.full_name, result.full_name, result.full_name_size);
  set_string(record.definition_name, result.definition_name,
             result.definition_name_size);
  set_string(record.source_file, result.source_file, result.source_file_size);
  set_string(record.string_value, result.string_value,
             result.string_value_size);
  set_string(record.binary, result.binary_value, result.binary_value_size);
  set_string(record.octal, result.octal_value, result.octal_value_size);
  set_string(record.decimal, result.decimal_value, result.decimal_value_size);
  set_string(record.hexadecimal, result.hexadecimal_value,
             result.hexadecimal_value_size);
  set_string(record.strength, result.strength_value,
             result.strength_value_size);
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL read_object(
    void* const user_data, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  ++fixture.calls;
  if (fixture.throw_read) throw std::runtime_error("read failure");
  if (query == nullptr || result == nullptr ||
      query->abi_version != FSIM_ACC_READ_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->reserved2 != 0 ||
      result->abi_version != FSIM_ACC_READ_QUERY_ABI_VERSION ||
      result->struct_size < sizeof(*result)) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_READ_DESIGN) {
    result->design_precision = -12;
    result->timescale_unit = 0;
    result->timescale_precision = -12;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto found = fixture.records.find(query->object);
  if (found == fixture.records.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
  if (query->operation == FSIM_ACC_READ_ATTRIBUTE) {
    const std::string name{query->attribute_name, query->attribute_name_size};
    if (name != "present") return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    result->attribute_real = 3.5;
    result->attribute_integer = 7;
    static const std::string text{"seven"};
    set_string(text, result->attribute_string, result->attribute_string_size);
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation != FSIM_ACC_READ_OBJECT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  publish_record(found->second, *result);
  if (fixture.malformed_result) result->binary_value_size = UINT32_MAX;
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

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(
    Fixture& fixture, const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          19,
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
          unsupported_write,
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
  require(static_cast<bool>(object), "read fixture creates VPI identity");
  fixture.records.emplace(object.value, record);
  return object.value;
}

}  // namespace

int main() {
  Fixture fixture;
  Record root_record;
  root_record.type = accModule;
  root_record.full_type = accTopModule;
  const auto root = add(fixture, Kind::Root, 0, "top", root_record);
  Record net_record;
  const auto net = add(fixture, Kind::Net, root, "data", net_record);
  Record parameter_record;
  parameter_record.type = accParameter;
  parameter_record.full_type = accIntegerParam;
  parameter_record.parameter_type = accIntegerParam;
  parameter_record.parameter_value = 29.0;
  const auto parameter =
      add(fixture, Kind::Parameter, root, "WIDTH", parameter_record);
  Record scalar_record = net_record;
  scalar_record.width = 1;
  scalar_record.msb = 0;
  scalar_record.words = {{1, 1}};
  scalar_record.binary = "x";
  const auto scalar = add(fixture, Kind::Net, root, "flag", scalar_record);
  Record real_record = net_record;
  real_record.type = accRealVar;
  real_record.full_type = accRealVar;
  real_record.width = 64;
  real_record.words.clear();
  const auto real = add(fixture, Kind::Variable, root, "ratio", real_record);
  fixture.records.at(real).value_kind = FSIM_ACC_READ_VALUE_REAL;
  Record string_record = net_record;
  string_record.type = accReg;
  string_record.full_type = accReg;
  const auto string_object =
      add(fixture, Kind::Variable, root, "message", string_record);
  fixture.records.at(string_object).value_kind = FSIM_ACC_READ_VALUE_STRING;

  auto active = context(fixture, root);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "read fixture enters its exact v3 context");
  require(acc_initialize() == 1, "read fixture initializes ACC lifecycle");
  auto net_handle = fsim_acc_handle_from_vpi_v3(net);
  auto parameter_handle = fsim_acc_handle_from_vpi_v3(parameter);
  auto scalar_handle = fsim_acc_handle_from_vpi_v3(scalar);
  auto real_handle = fsim_acc_handle_from_vpi_v3(real);
  auto string_handle = fsim_acc_handle_from_vpi_v3(string_object);
  require(net_handle && parameter_handle && scalar_handle && real_handle &&
              string_handle,
          "read fixture maps all VPI values into ACC handles");

  require(std::strcmp(acc_fetch_name(net_handle), "data") == 0 &&
              std::strcmp(acc_fetch_fullname(net_handle), "top.data") == 0 &&
              std::strcmp(acc_fetch_defname(net_handle), "wire") == 0 &&
              acc_fetch_type(net_handle) == accNet &&
              acc_fetch_fulltype(net_handle) == accWire &&
              acc_fetch_direction(net_handle) == accInput &&
              acc_fetch_edge(net_handle) == accPosedge &&
              acc_fetch_index(net_handle) == 3 &&
              acc_fetch_size(net_handle) == 40,
          "object metadata reads preserve names types selectors and width");
  PLI_INT32 msb{};
  PLI_INT32 lsb{};
  s_location location{};
  s_timescale_info timescale{};
  require(acc_fetch_range(net_handle, &msb, &lsb) == 1 && msb == 39 &&
              lsb == 0 && acc_fetch_location(&location, net_handle) == 1 &&
              location.line_no == 27 &&
              std::strcmp(location.filename, "logical/read.sv") == 0,
          "range and source reads publish complete snapshots");
  acc_fetch_timescale_info(net_handle, &timescale);
  require(acc_error_flag == 0 && timescale.unit == -9 &&
              timescale.precision == -12 && acc_fetch_precision() == -12 &&
              acc_fetch_paramtype(parameter_handle) == accIntegerParam &&
              acc_fetch_paramval(parameter_handle) == 29.0,
          "time and parameter reads retain exact metadata");

  require(std::strcmp(acc_fetch_value(net_handle, const_cast<char*>("%b"),
                                      nullptr),
                      net_record.binary.c_str()) == 0 &&
              std::strcmp(acc_fetch_value(net_handle, const_cast<char*>("%o"),
                                          nullptr),
                          net_record.octal.c_str()) == 0 &&
              std::strcmp(acc_fetch_value(net_handle, const_cast<char*>("%d"),
                                          nullptr),
                          net_record.decimal.c_str()) == 0 &&
              std::strcmp(acc_fetch_value(net_handle, const_cast<char*>("%h"),
                                          nullptr),
                          net_record.hexadecimal.c_str()) == 0 &&
              std::strcmp(acc_fetch_value(net_handle, const_cast<char*>("%v"),
                                          nullptr),
                          net_record.strength.c_str()) == 0,
          "formatted value reads return bounded simulator snapshots");
  std::array<s_acc_vecval, 2> words{};
  s_acc_value value{};
  value.format = accVectorVal;
  value.value.vector = words.data();
  require(acc_fetch_value(net_handle, const_cast<char*>("%%"), &value) ==
                  nullptr &&
              acc_error_flag == 0 && words[0].aval ==
                  static_cast<PLI_INT32>(UINT32_C(0x89abcdef)) &&
              words[1].aval == 0x12,
          "wide four-state values preserve every aval and bval word");
  value.format = accScalarVal;
  require(acc_fetch_value(scalar_handle, const_cast<char*>("%%"), &value) ==
                  nullptr &&
              acc_error_flag == 0 && value.value.scalar == accX,
          "scalar four-state encoding preserves unknown values");
  value.format = accRealVal;
  require(acc_fetch_value(real_handle, const_cast<char*>("%%"), &value) ==
                  nullptr &&
              acc_error_flag == 0 && std::abs(value.value.real - 2.5) < 1e-9,
          "real values retain their native representation");
  value.format = accStringVal;
  require(acc_fetch_value(string_handle, const_cast<char*>("%%"), &value) ==
                  nullptr &&
              acc_error_flag == 0 &&
              std::strcmp(value.value.str, "payload") == 0,
          "string values use the bounded ACC temporary buffer");

  require(acc_fetch_attribute(net_handle, const_cast<char*>("present"), 9.0) ==
                  3.5 &&
              acc_fetch_attribute_int(net_handle,
                                      const_cast<char*>("present"), 9) == 7 &&
              std::strcmp(acc_fetch_attribute_str(
                              net_handle, const_cast<char*>("present"),
                              const_cast<char*>("nine")),
                          "seven") == 0,
          "typed attribute reads return the matching stored value");
  require(acc_fetch_attribute(net_handle, const_cast<char*>("missing"), 9.0) ==
                  9.0 &&
              acc_fetch_attribute_int(net_handle,
                                      const_cast<char*>("missing"), 9) == 9 &&
              std::strcmp(acc_fetch_attribute_str(
                              net_handle, const_cast<char*>("missing"),
                              const_cast<char*>("nine")),
                          "nine") == 0,
          "missing attributes return the configured caller defaults");
  require(acc_configure(accDefaultAttr0, const_cast<char*>("true")) == 1 &&
              acc_fetch_attribute(net_handle,
                                  const_cast<char*>("missing")) == 0.0 &&
              acc_fetch_attribute_int(net_handle,
                                      const_cast<char*>("missing")) == 0 &&
              std::strcmp(acc_fetch_attribute_str(
                              net_handle, const_cast<char*>("missing")),
                          "0") == 0,
          "zero-default mode does not consume omitted variadic defaults");

  constexpr std::array representative_types{
      accModule, accScope, accNet, accReg, accPort, accTerminal, accPrimitive,
      accTchk, accModPath, accWire, accScalar, accBit, accSetup, accInput,
      accPathTerminal, accTask, accConstant, accMinTypMax};
  for (const auto type : representative_types) {
    require(acc_fetch_type_str(type) != nullptr && acc_error_flag == 0,
            "type strings cover every standardized object family");
  }
  require(acc_fetch_type_str(-1) == nullptr && acc_error_flag == 1,
          "type strings reject values outside the standard inventory");

  const auto calls_before = fixture.calls;
  require(acc_fetch_range(net_handle, reinterpret_cast<PLI_INT32*>(1), &lsb) ==
                  0 &&
              acc_error_flag == 1 && fixture.calls == calls_before,
          "read destinations are validated before callback dispatch");
  fixture.malformed_result = true;
  require(acc_fetch_value(net_handle, const_cast<char*>("%b"), nullptr) ==
                  nullptr &&
              acc_error_flag == 1,
          "oversized callback strings are rejected before copying");
  value.format = accBinStrVal;
  require(acc_fetch_value(net_handle, const_cast<char*>("%%"), &value) ==
                  nullptr &&
              acc_error_flag == 1,
          "structured string-copy failures retain the ACC error state");
  fixture.malformed_result = false;
  fixture.throw_read = true;
  require(acc_fetch_size(net_handle) == 0 && acc_error_flag == 1,
          "read callback exceptions are contained at the ACC boundary");
  fixture.throw_read = false;
  fixture.throw_resolve = true;
  require(acc_fetch_size(net_handle) == 0 && acc_error_flag == 1,
          "resolver exceptions are contained at the ACC boundary");
  fixture.throw_resolve = false;

  acc_close();
  fsim_acc_handle_context_leave_v3(&active);
  active.read = nullptr;
  require(fsim_acc_handle_context_enter_v3(&active) == 0 &&
              acc_error_flag == 1,
          "context entry rejects a missing read callback");
  return 0;
}
