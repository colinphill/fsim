// SPDX-License-Identifier: Apache-2.0

// PLI 1.0 task/function tables (IEEE 1364-2005 Annex G, veriusertfs)
// registered over the VPI. Each table cell becomes a VPI system task or
// function whose routines run the cell's checktf, sizetf, calltf and
// misctf inside a TF call context, so the tf_* routines of tf_link.cpp
// see the call's arguments, time and instance. An ACC handle context over
// the VPI gives the access routines (IEEE 1364-2005 Clauses 22 and 23) the
// same call.

#include "vpi_user.h"
#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/tf_call_bridge.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Instance;

// An argument whose changes call misctf(reason_paramvc) once tf_asynchon
// enabled them.
struct Watch {
  Instance* instance{};
  PLI_INT32 parameter{};
};

struct Instance {
  p_tfcell cell{};
  vpiHandle call{};
  vpiHandle module{};
  fsim_tf_instance_bridge_v3 bridge{};
  PLI_BYTE8* work_area{};
  std::string module_name;
  std::string scope_name;
  std::string routine_name;
  std::vector<vpiHandle> handles;
  std::vector<fsim_tf_argument_bridge_v3> arguments;
  uint32_t result_width{};
  std::array<bool, 2> synchronization_pending{};
  bool asynchronous{};
  std::deque<Watch> watches;
};

// Routines run one at a time: the TF and ACC call contexts do not nest, so
// a parameter change a routine causes calls misctf after it returns.
bool routine_active{};
std::deque<Watch> pending_changes;

std::unordered_map<vpiHandle, std::unique_ptr<Instance>>& instances() {
  static std::unordered_map<vpiHandle, std::unique_ptr<Instance>> table;
  return table;
}

PLI_BYTE8* text(const char* const value) {
  return const_cast<PLI_BYTE8*>(value);
}

bool runtime_phase(const uint32_t phase) {
  return phase != FSIM_TF_CALL_PHASE_CHECK &&
         phase != FSIM_TF_CALL_PHASE_SIZE;
}

bool writable_phase(const uint32_t phase) {
  return phase == FSIM_TF_CALL_PHASE_CALL ||
         phase == FSIM_TF_CALL_PHASE_SYNCHRONIZE ||
         phase == FSIM_TF_CALL_PHASE_REACTIVATE;
}

bool writable_kind(const PLI_INT32 kind) {
  return kind == tf_readwrite || kind == tf_rwbitselect ||
         kind == tf_rwpartselect || kind == tf_rwmemselect ||
         kind == tf_readwritereal;
}

std::string full_name(const vpiHandle object, const char* const fallback) {
  const char* const name =
      object == nullptr ? nullptr : vpi_get_str(vpiFullName, object);
  return name == nullptr || *name == '\0' ? std::string{fallback}
                                          : std::string{name};
}

// The tf_typep class of an argument (IEEE 1364-2005 25.68).
fsim_tf_argument_bridge_v3 classify(const vpiHandle handle) {
  fsim_tf_argument_bridge_v3 argument{};
  if (handle == nullptr) {
    argument.kind = tf_nullparam;
    return argument;
  }
  const auto size = vpi_get(vpiSize, handle);
  argument.width = size > 0 ? static_cast<uint32_t>(size) : 32u;
  argument.is_signed = vpi_get(vpiSigned, handle) == 1 ? 1u : 0u;
  const auto type = vpi_get(vpiType, handle);
  const auto rw_select = [&](const PLI_INT32 kind) {
    const auto parent = vpi_handle(vpiParent, handle);
    const auto parent_type = parent == nullptr ? 0 : vpi_get(vpiType, parent);
    return parent_type == vpiNet || parent_type == vpiNetArray ? tf_readonly
                                                               : kind;
  };
  switch (type) {
  case vpiConstant:
  case vpiParameter:
    switch (vpi_get(vpiConstType, handle)) {
    case vpiStringConst:
      argument.kind = tf_string;
      argument.width = std::max(argument.width, 8u);
      break;
    case vpiRealConst:
      argument.kind = tf_readonlyreal;
      argument.width = 64u;
      break;
    default:
      argument.kind = tf_readonly;
      break;
    }
    break;
  case vpiRealVar:
    argument.kind = tf_readwritereal;
    argument.width = 64u;
    break;
  case vpiReg:
  case vpiIntegerVar:
  case vpiTimeVar: {
    // A word of an array is a memory select (IEEE 1800-2023 37.17).
    const auto parent = vpi_handle(vpiParent, handle);
    argument.kind = parent != nullptr && vpi_get(vpiType, parent) == vpiRegArray
        ? tf_rwmemselect
        : tf_readwrite;
    break;
  }
  case vpiMemoryWord:
    argument.kind = tf_rwmemselect;
    break;
  case vpiBitSelect:
    argument.kind = rw_select(tf_rwbitselect);
    break;
  case vpiPartSelect:
    argument.kind = rw_select(tf_rwpartselect);
    break;
  default: {
    // An expression is real when its value is (IEEE 1364-2005 25.68).
    s_vpi_value probe{vpiObjTypeVal, {}};
    vpi_get_value(handle, &probe);
    if (probe.format == vpiRealVal) {
      argument.kind = tf_readonlyreal;
      argument.width = 64u;
    } else {
      argument.kind = tf_readonly;
    }
    break;
  }
  }
  return argument;
}

Instance& instance_for(const vpiHandle call, const p_tfcell cell) {
  auto& table = instances();
  if (const auto found = table.find(call); found != table.end()) {
    return *found->second;
  }
  static uint64_t next_hierarchy_id{};
  auto created = std::make_unique<Instance>();
  created->cell = cell;
  created->call = call;
  created->bridge = {FSIM_TF_INSTANCE_ABI_VERSION,
                     sizeof(fsim_tf_instance_bridge_v3), 1u,
                     ++next_hierarchy_id, 1u, 0u};
  created->module = vpi_handle(vpiModule, call);
  created->module_name = full_name(created->module, "$root");
  created->scope_name = full_name(vpi_handle(vpiScope, call), "$root");
  created->routine_name =
      cell->tfname == nullptr || *cell->tfname == '\0' ? "$tf" : cell->tfname;
  if (const auto iterator = vpi_iterate(vpiArgument, call)) {
    while (const auto argument = vpi_scan(iterator)) {
      created->handles.push_back(argument);
      created->arguments.push_back(classify(argument));
    }
  }
  PLI_INT32 width = 32;
  if (cell->type == userfunction && cell->sizetf != nullptr) {
    width = cell->sizetf(cell->data, reason_sizetf);
  }
  created->result_width = width > 0 ? static_cast<uint32_t>(width) : 32u;
  return *table.emplace(call, std::move(created)).first->second;
}

vpiHandle vpi_object(const uint64_t object) {
  return reinterpret_cast<vpiHandle>(static_cast<std::uintptr_t>(object));
}

uint64_t acc_object(const vpiHandle object) {
  return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(object));
}

// The ACC type and full type of a VPI object; 0 when ACC has no such object.
std::pair<PLI_INT32, PLI_INT32> acc_types(const vpiHandle object) {
  switch (vpi_get(vpiType, object)) {
  case vpiModule:
    return {accModule, vpi_get(vpiTopModule, object) == 1 ? accTopModule
                                                          : accModuleInstance};
  case vpiReg: return {accReg, accReg};
  case vpiIntegerVar: return {accIntegerVar, accIntegerVar};
  case vpiTimeVar: return {accTimeVar, accTimeVar};
  case vpiRealVar: return {accRealVar, accRealVar};
  case vpiNamedEvent: return {accNamedEvent, accNamedEvent};
  case vpiNet:
    switch (vpi_get(vpiNetType, object)) {
    case vpiWand: return {accNet, accWand};
    case vpiWor: return {accNet, accWor};
    case vpiTri: return {accNet, accTri};
    case vpiTriAnd: return {accNet, accTriand};
    case vpiTriOr: return {accNet, accTrior};
    case vpiTri0: return {accNet, accTri0};
    case vpiTri1: return {accNet, accTri1};
    case vpiTriReg: return {accNet, accTrireg};
    case vpiSupply0: return {accNet, accSupply0};
    case vpiSupply1: return {accNet, accSupply1};
    default: return {accNet, accWire};
    }
  case vpiBitSelect: {
    const auto parent = vpi_handle(vpiParent, object);
    return parent != nullptr && vpi_get(vpiType, parent) == vpiNet
               ? std::pair{accNetBit, accNetBit}
               : std::pair{accRegBit, accRegBit};
  }
  case vpiParameter:
    switch (vpi_get(vpiConstType, object)) {
    case vpiRealConst: return {accParameter, accRealParam};
    case vpiStringConst: return {accParameter, accStringParam};
    default: return {accParameter, accIntegerParam};
    }
  default: return {0, 0};
  }
}

// Storage the last read result points into, valid until the next read.
struct AccReadStorage {
  std::vector<fsim_acc_logic_word_v3> words;
  std::array<std::string, 7> text;
};

AccReadStorage& acc_read_storage() {
  static AccReadStorage storage;
  return storage;
}

void read_text(const vpiHandle object, const PLI_INT32 format,
               std::string& text, const PLI_BYTE8*& pointer,
               uint32_t& size) {
  s_vpi_value value{format, {}};
  vpi_get_value(object, &value);
  text = value.format == format && value.value.str != nullptr
             ? value.value.str
             : "";
  pointer = text.c_str();
  size = static_cast<uint32_t>(text.size());
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_resolve(void* /*user_data*/,
                                             const uint64_t object,
                                             PLI_INT32* const type) {
  if (object == 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto full_type = acc_types(vpi_object(object)).second;
  if (full_type == 0) {
    return FSIM_ACC_VPI_OBJECT_WRONG_TYPE;
  }
  *type = full_type;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_lookup(
    void* /*user_data*/, const uint32_t mode, const uint64_t scope,
    const PLI_BYTE8* const name, const uint32_t name_size,
    uint64_t* const object) {
  const std::string path{name, name_size};
  const auto found = vpi_handle_by_name(
      const_cast<PLI_BYTE8*>(path.c_str()),
      mode == FSIM_ACC_LOOKUP_ABSOLUTE ? nullptr : vpi_object(scope));
  if (found == nullptr) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *object = acc_object(found);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_relation(void* /*user_data*/,
                                              const uint32_t relation,
                                              const uint64_t object,
                                              uint64_t* const related) {
  vpiHandle found{};
  switch (relation) {
  case FSIM_ACC_RELATION_PARENT:
    found = vpi_handle(vpiModule, vpi_object(object));
    break;
  case FSIM_ACC_RELATION_SCOPE:
    found = vpi_handle(vpiScope, vpi_object(object));
    break;
  case FSIM_ACC_RELATION_SIMULATED_NET:
    found = vpi_object(object);
    break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (found == nullptr) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *related = acc_object(found);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_name(void*, uint64_t, PLI_BYTE8*,
                                          uint32_t, uint32_t*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_traverse(void*, uint32_t, uint32_t,
                                              uint64_t, uint64_t*,
                                              uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_object_query(
    void*, const fsim_acc_object_query_v3*, uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

// An object's type, range, value and names (FSIM_ACC_READ_OBJECT).
uint32_t FSIM_NATIVE_PLUGIN_CALL acc_read(
    void* /*user_data*/, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  if (query->operation != FSIM_ACC_READ_OBJECT) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  const auto object = vpi_object(query->object);
  const auto [type, full_type] = acc_types(object);
  if (full_type == 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  auto& storage = acc_read_storage();
  result->type = type;
  result->full_type = full_type;
  const auto name = vpi_get_str(vpiName, object);
  storage.text[0] = name == nullptr ? "" : name;
  result->name = storage.text[0].c_str();
  result->name_size = static_cast<uint32_t>(storage.text[0].size());
  const auto path = vpi_get_str(vpiFullName, object);
  storage.text[1] = path == nullptr ? "" : path;
  result->full_name = storage.text[1].c_str();
  result->full_name_size = static_cast<uint32_t>(storage.text[1].size());
  if (type == accModule || type == accNamedEvent) {
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto size = vpi_get(vpiSize, object);
  result->width = size > 0 ? size : 1;
  const auto left = vpi_handle(vpiLeftRange, object);
  const auto right = vpi_handle(vpiRightRange, object);
  if (left != nullptr && right != nullptr) {
    s_vpi_value bound{vpiIntVal, {}};
    vpi_get_value(left, &bound);
    result->msb = bound.value.integer;
    bound = {vpiIntVal, {}};
    vpi_get_value(right, &bound);
    result->lsb = bound.value.integer;
    result->has_range = 1;
  }
  if (type != accParameter) {
    result->write_capabilities = FSIM_ACC_WRITE_CAP_DEPOSIT |
                                 FSIM_ACC_WRITE_CAP_FORCE |
                                 FSIM_ACC_WRITE_CAP_RELEASE;
  }
  if (full_type == accRealVar || full_type == accRealParam) {
    s_vpi_value value{vpiRealVal, {}};
    vpi_get_value(object, &value);
    result->value_kind = FSIM_ACC_READ_VALUE_REAL;
    result->real_value = value.value.real;
    result->parameter_value = value.value.real;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto words = static_cast<uint32_t>((result->width + 31) / 32);
  s_vpi_value value{vpiVectorVal, {}};
  vpi_get_value(object, &value);
  storage.words.assign(words, fsim_acc_logic_word_v3{});
  for (uint32_t word = 0; word < words && value.value.vector != nullptr;
       ++word) {
    storage.words[word] = {value.value.vector[word].aval,
                           value.value.vector[word].bval};
  }
  result->value_kind = FSIM_ACC_READ_VALUE_LOGIC4;
  result->logic_word_count = words;
  result->logic_words = storage.words.data();
  result->parameter_value = static_cast<double>(storage.words[0].aval);
  read_text(object, vpiBinStrVal, storage.text[2], result->binary_value,
            result->binary_value_size);
  read_text(object, vpiOctStrVal, storage.text[3], result->octal_value,
            result->octal_value_size);
  read_text(object, vpiDecStrVal, storage.text[4], result->decimal_value,
            result->decimal_value_size);
  read_text(object, vpiHexStrVal, storage.text[5], result->hexadecimal_value,
            result->hexadecimal_value_size);
  read_text(object, vpiStringVal, storage.text[6], result->string_value,
            result->string_value_size);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// acc_set_value: a deposit, delayed assignment, force or release.
uint32_t FSIM_NATIVE_PLUGIN_CALL acc_write(
    void* user_data, const fsim_acc_write_query_v3* const query,
    fsim_acc_write_result_v3* const result) {
  if (query->operation != FSIM_ACC_WRITE_SET_VALUE) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto object = vpi_object(query->object);
  PLI_INT32 flags{};
  switch (query->model) {
  case accNoDelay: flags = vpiNoDelay; break;
  case accInertialDelay: flags = vpiInertialDelay; break;
  case accTransportDelay: flags = vpiTransportDelay; break;
  case accPureTransportDelay: flags = vpiPureTransportDelay; break;
  case accForceFlag: flags = vpiForceFlag; break;
  case accReleaseFlag: flags = vpiReleaseFlag; break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  // accTime and accRealTime count the object's time units, accSimTime ticks.
  s_vpi_time time{vpiSimTime, 0, 0, 0.0};
  if (query->time_type == accSimTime) {
    time.high = static_cast<PLI_UINT32>(query->time_ticks >> 32u);
    time.low = static_cast<PLI_UINT32>(query->time_ticks);
  } else if (query->time_type == accTime) {
    time.type = vpiScaledRealTime;
    time.real = static_cast<double>(query->time_ticks);
  } else if (query->time_type == accRealTime) {
    time.type = vpiScaledRealTime;
    time.real = query->time_real;
  }
  s_vpi_value value{vpiVectorVal, {}};
  std::vector<s_vpi_vecval> words;
  std::string text;
  const auto& input = query->value;
  switch (flags == vpiReleaseFlag ? accVectorVal : input.format) {
  case accScalarVal:
  case accIntVal:
  case accVectorVal:
    words.resize(std::max<std::size_t>(
        input.logic_word_count,
        static_cast<std::size_t>((std::max(input.width, 1) + 31) / 32)));
    for (uint32_t word = 0; word < input.logic_word_count; ++word) {
      words[word] = {input.logic_words[word].aval,
                     input.logic_words[word].bval};
    }
    value.value.vector = words.data();
    break;
  case accRealVal:
    value.format = vpiRealVal;
    value.value.real = input.real_value;
    break;
  case accStringVal:
  case accBinStrVal:
  case accOctStrVal:
  case accDecStrVal:
  case accHexStrVal:
    value.format = input.format == accStringVal   ? vpiStringVal
                   : input.format == accBinStrVal ? vpiBinStrVal
                   : input.format == accOctStrVal ? vpiOctStrVal
                   : input.format == accDecStrVal ? vpiDecStrVal
                                                  : vpiHexStrVal;
    text.assign(input.string_value, input.string_value_size);
    value.value.str = text.data();
    break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const bool delayed = flags == vpiInertialDelay ||
                       flags == vpiTransportDelay ||
                       flags == vpiPureTransportDelay;
  (void)vpi_put_value(object, &value, delayed ? &time : nullptr, flags);
  fsim_acc_read_query_v3 read{};
  read.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  read.struct_size = sizeof(read);
  read.operation = FSIM_ACC_READ_OBJECT;
  read.object = query->object;
  return acc_read(user_data, &read, &result->current_value);
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_iterate(
    void*, const fsim_acc_iterator_query_v3*, fsim_acc_iterator_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_timing(void*,
                                            const fsim_acc_timing_query_v3*,
                                            fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_vcl(void*,
                                         const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

// The simulator's command line for acc_fetch_argc and acc_fetch_argv.
struct AccCommandLine {
  std::vector<PLI_BYTE8*> argv;
  std::vector<uint32_t> sizes;
};

const AccCommandLine& acc_command_line() {
  static const AccCommandLine line = [] {
    AccCommandLine result;
    s_vpi_vlog_info info{};
    if (vpi_get_vlog_info(&info) == 0 || info.argc <= 0 ||
        info.argv == nullptr ||
        static_cast<uint32_t>(info.argc) > FSIM_ACC_TF_MAXIMUM_ARGV) {
      return result;
    }
    for (PLI_INT32 index = 0; index < info.argc; ++index) {
      const auto size = info.argv[index] == nullptr
                            ? 0u
                            : static_cast<uint32_t>(
                                  std::strlen(info.argv[index]) + 1u);
      if (size == 0u || size > FSIM_ACC_TF_MAXIMUM_ARGV_BYTES) {
        return AccCommandLine{};
      }
      result.argv.push_back(info.argv[index]);
      result.sizes.push_back(size);
    }
    return result;
  }();
  return line;
}

// The ACC handle context of one routine call.
struct AccCall {
  std::vector<uint64_t> arguments;
  fsim_acc_tf_context_v3 tf{};
  fsim_acc_handle_context_v3 context{};
  bool entered{};

  AccCall(const Instance& instance, const fsim_tf_call_context_v3& call) {
    if (instance.module == nullptr) {
      return;
    }
    // A null argument names the call itself, which no ACC handle denotes.
    for (const auto handle : instance.handles) {
      arguments.push_back(acc_object(handle == nullptr ? instance.call
                                                       : handle));
    }
    const auto& line = acc_command_line();
    tf.abi_version = FSIM_ACC_TF_CONTEXT_ABI_VERSION;
    tf.struct_size = sizeof(tf);
    tf.call_context = &call;
    tf.argument_objects = arguments.empty() ? nullptr : arguments.data();
    tf.argument_count = static_cast<uint32_t>(arguments.size());
    tf.argc = static_cast<uint32_t>(line.argv.size());
    tf.argv = line.argv.empty() ? nullptr : line.argv.data();
    tf.argv_sizes = line.sizes.empty() ? nullptr : line.sizes.data();
    const auto scope = acc_object(instance.module);
    context.abi_version = FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION;
    context.struct_size = sizeof(context);
    context.simulation_identity = instance.bridge.design_id;
    context.hierarchy_generation = instance.bridge.generation;
    context.maximum_handles = FSIM_ACC_HANDLE_MAX_OBJECTS;
    context.user_data = const_cast<Instance*>(&instance);
    context.resolve = &acc_resolve;
    context.calling_scope = scope;
    context.default_scope = scope;
    context.interactive_scope = scope;
    context.lookup = &acc_lookup;
    context.relation = &acc_relation;
    context.name = &acc_name;
    context.traverse = &acc_traverse;
    context.object_query = &acc_object_query;
    context.read = &acc_read;
    context.write = &acc_write;
    context.iterate = &acc_iterate;
    context.timing = &acc_timing;
    context.vcl = &acc_vcl;
    context.tf = &tf;
    entered = fsim_acc_handle_context_enter_v3(&context) != 0;
  }
  AccCall(const AccCall&) = delete;
  AccCall& operator=(const AccCall&) = delete;
  ~AccCall() {
    if (entered) {
      fsim_acc_handle_context_leave_v3(&context);
    }
  }
};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL emit(
    void* /*user_data*/, const uint32_t kind, const PLI_INT32 channel,
    const PLI_INT32 /*level*/, const PLI_BYTE8* /*facility*/,
    const uint32_t /*facility_size*/, const PLI_BYTE8* /*message_number*/,
    const uint32_t /*message_number_size*/, const PLI_BYTE8* const message,
    const uint32_t size) {
  const auto length = static_cast<int>(size);
  switch (kind) {
  case FSIM_TF_CONTROL_OUTPUT:
    if (channel == 1) {
      (void)vpi_printf(text("%.*s"), length, message);
    } else {
      (void)vpi_mcd_printf(static_cast<PLI_UINT32>(channel), text("%.*s"),
                           length, message);
    }
    return 0;
  case FSIM_TF_CONTROL_WARNING:
  case FSIM_TF_CONTROL_ERROR:
  case FSIM_TF_CONTROL_MESSAGE:
    (void)vpi_printf(text("%.*s"), length, message);
    return 0;
  case FSIM_TF_CONTROL_FINISH:
    (void)vpi_control(vpiFinish, 1);
    return 0;
  case FSIM_TF_CONTROL_STOP:
    (void)vpi_control(vpiStop, 1);
    return 0;
  default:
    return 1;
  }
}

PLI_INT32 synchronized(p_cb_data data);
PLI_INT32 reactivated(p_cb_data data);
PLI_INT32 parameter_changed(p_cb_data data);
void drain_changes();

// Runs one routine of a cell in a TF call context, then applies what it
// assigned and scheduled.
void run(Instance& instance, const uint32_t phase, const PLI_INT32 reason,
         const PLI_INT32 parameter = 0) {
  const auto* const cell = instance.cell;
  const auto count = static_cast<uint32_t>(instance.arguments.size());
  fsim_tf_call_context_v3 context{};
  context.phase = phase;
  context.argument_count = count;
  context.arguments = count == 0 ? nullptr : instance.arguments.data();
  context.instance = &instance.bridge;
  context.user_data = cell->data;
  context.module_instance_name = instance.module_name.data();
  context.scope_name = instance.scope_name.data();
  context.routine_name = instance.routine_name.data();
  context.work_area = instance.work_area;
  context.control_user_data = &instance;
  context.control_emit = &emit;
  std::array<uint32_t, 8> synchronizations{};
  context.synchronization_capacity =
      static_cast<uint32_t>(synchronizations.size());
  context.synchronization_kinds = synchronizations.data();

  std::vector<PLI_UINT32> aval;
  std::vector<PLI_UINT32> bval;
  if (phase == FSIM_TF_CALL_PHASE_CALL && cell->type == userfunction) {
    context.kind = FSIM_TF_CALL_RESULT_INTEGRAL;
    context.width = instance.result_width;
    context.word_count = (context.width + 31u) / 32u;
    aval.assign(context.word_count, 0u);
    bval.assign(context.word_count, 0u);
    context.aval_words = aval.data();
    context.bval_words = bval.data();
  } else if (phase == FSIM_TF_CALL_PHASE_CALL &&
             cell->type == userrealfunction) {
    context.kind = FSIM_TF_CALL_RESULT_REAL;
    context.width = 64u;
  }

  std::vector<fsim_tf_value_bridge_v3> values(count);
  std::vector<std::vector<t_vecval>> vectors(count);
  std::vector<std::string> strings(count);
  fsim_tf_time_bridge_v3 time{};
  std::array<uint64_t, 16> delays{};
  if (runtime_phase(phase)) {
    const bool writable = writable_phase(phase);
    for (uint32_t index = 0; index < count; ++index) {
      const auto& argument = instance.arguments[index];
      const auto handle = instance.handles[index];
      auto& value = values[index];
      value.width = argument.width;
      value.writable =
          static_cast<uint32_t>(writable && writable_kind(argument.kind));
      if (argument.kind == tf_nullparam) {
        continue;
      }
      if (argument.kind == tf_string) {
        s_vpi_value read{vpiStringVal, {}};
        vpi_get_value(handle, &read);
        strings[index] = read.value.str == nullptr ? "" : read.value.str;
        strings[index].resize(
            std::min<std::size_t>(strings[index].size(), argument.width / 8u));
        value.kind = FSIM_TF_VALUE_STRING;
        value.string_size = static_cast<uint32_t>(strings[index].size());
        value.string_value = strings[index].data();
      } else if (argument.kind == tf_readonlyreal ||
                 argument.kind == tf_readwritereal) {
        s_vpi_value read{vpiRealVal, {}};
        vpi_get_value(handle, &read);
        value.kind = FSIM_TF_VALUE_REAL;
        value.real = read.value.real;
      } else {
        const auto words = (argument.width + 31u) / 32u;
        s_vpi_value read{vpiVectorVal, {}};
        vpi_get_value(handle, &read);
        vectors[index].assign(words, t_vecval{});
        for (uint32_t word = 0; word < words && read.value.vector != nullptr;
             ++word) {
          vectors[index][word].avalbits =
              static_cast<PLI_INT32>(read.value.vector[word].aval);
          vectors[index][word].bvalbits =
              static_cast<PLI_INT32>(read.value.vector[word].bval);
        }
        value.kind = FSIM_TF_VALUE_INTEGRAL;
        value.word_count = words;
        value.vector_words = vectors[index].data();
      }
    }
    context.values = count == 0 ? nullptr : values.data();

    s_vpi_time now{vpiSimTime, 0, 0, 0.0};
    vpi_get_time(nullptr, &now);
    const auto precision =
        std::clamp(vpi_get(vpiTimePrecision, nullptr), -15, 0);
    const auto unit = std::clamp(
        vpi_get(vpiTimeUnit, instance.module), precision, 0);
    time.unit_exponent = static_cast<int8_t>(unit);
    time.precision_exponent = static_cast<int8_t>(precision);
    time.tick_multiplier = 1u;
    time.scheduler_ticks = (static_cast<uint64_t>(now.high) << 32u) | now.low;
    time.delay_capacity = static_cast<uint32_t>(delays.size());
    time.delay_ticks = delays.data();
    context.time = &time;
  }

  if (fsim_tf_call_context_enter_v3(&context) != 0) {
    vpi_printf(text("ERROR: %s: PLI 1.0 call context rejected\n"),
               instance.routine_name.c_str());
    return;
  }
  routine_active = true;
  auto acc = std::make_unique<AccCall>(instance, context);
  PLI_INT32 result{};
  switch (phase) {
  case FSIM_TF_CALL_PHASE_CHECK:
    result = reason == reason_checktf
                 ? (cell->checktf == nullptr
                        ? 0
                        : cell->checktf(cell->data, reason))
                 : (cell->misctf == nullptr
                        ? 0
                        : cell->misctf(cell->data, reason, 0));
    break;
  case FSIM_TF_CALL_PHASE_CALL:
    result = cell->calltf == nullptr ? 0 : cell->calltf(cell->data, reason);
    break;
  default:
    result = cell->misctf == nullptr
                 ? 0
                 : cell->misctf(cell->data, reason, parameter);
    break;
  }
  (void)result;
  instance.work_area = context.work_area;
  acc.reset();
  fsim_tf_call_context_leave_v3(&context);
  routine_active = false;

  // The function result (tf_putp(0, ...)) and assigned arguments.
  if (context.assigned != 0 &&
      context.kind == FSIM_TF_CALL_RESULT_INTEGRAL) {
    std::vector<s_vpi_vecval> words(context.word_count);
    for (uint32_t word = 0; word < context.word_count; ++word) {
      words[word] = {aval[word], bval[word]};
    }
    s_vpi_value write{vpiVectorVal, {}};
    write.value.vector = words.data();
    (void)vpi_put_value(instance.call, &write, nullptr, vpiNoDelay);
  } else if (context.assigned != 0 &&
             context.kind == FSIM_TF_CALL_RESULT_REAL) {
    s_vpi_value write{vpiRealVal, {}};
    write.value.real = context.real;
    (void)vpi_put_value(instance.call, &write, nullptr, vpiNoDelay);
  }
  for (uint32_t index = 0; index < count && runtime_phase(phase); ++index) {
    const auto& value = values[index];
    if (value.assigned == 0) {
      continue;
    }
    if (value.kind == FSIM_TF_VALUE_REAL) {
      s_vpi_value write{vpiRealVal, {}};
      write.value.real = value.real;
      (void)vpi_put_value(instance.handles[index], &write, nullptr,
                          vpiNoDelay);
    } else if (value.kind == FSIM_TF_VALUE_INTEGRAL) {
      std::vector<s_vpi_vecval> words(value.word_count);
      for (uint32_t word = 0; word < value.word_count; ++word) {
        words[word] = {static_cast<PLI_UINT32>(vectors[index][word].avalbits),
                       static_cast<PLI_UINT32>(vectors[index][word].bvalbits)};
      }
      s_vpi_value write{vpiVectorVal, {}};
      write.value.vector = words.data();
      (void)vpi_put_value(instance.handles[index], &write, nullptr,
                          vpiNoDelay);
    }
  }

  // tf_synchronize and tf_rosynchronize: one misctf per time step.
  for (uint32_t index = 0; index < context.synchronization_count; ++index) {
    if (synchronizations[index] == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_OFF) {
      instance.asynchronous = false;
      continue;
    }
    if (synchronizations[index] == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_ON) {
      instance.asynchronous = true;
      const bool watched = !instance.watches.empty();
      for (uint32_t argument = 0; !watched && argument < count; ++argument) {
        const auto handle = instance.handles[argument];
        if (handle == nullptr || vpi_get(vpiType, handle) == vpiConstant) {
          continue;
        }
        auto& watch = instance.watches.emplace_back(
            Watch{&instance, static_cast<PLI_INT32>(argument + 1u)});
        s_vpi_time at{vpiSuppressTime, 0, 0, 0.0};
        s_vpi_value value{vpiSuppressVal, {}};
        s_cb_data callback{};
        callback.reason = cbValueChange;
        callback.cb_rtn = &parameter_changed;
        callback.obj = handle;
        callback.time = &at;
        callback.value = &value;
        callback.user_data = reinterpret_cast<PLI_BYTE8*>(&watch);
        (void)vpi_register_cb(&callback);
      }
      continue;
    }
    const bool read_only =
        synchronizations[index] == FSIM_TF_SYNCHRONIZATION_READ_ONLY;
    auto& pending = instance.synchronization_pending[read_only ? 1u : 0u];
    if (pending) {
      continue;
    }
    s_vpi_time at{vpiSimTime, 0, 0, 0.0};
    s_cb_data callback{};
    callback.reason = read_only ? cbReadOnlySynch : cbReadWriteSynch;
    callback.cb_rtn = &synchronized;
    callback.time = &at;
    callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
    pending = vpi_register_cb(&callback) != nullptr;
  }
  // tf_setdelay and friends: misctf(reason_reactivate) after each delay.
  for (uint32_t index = 0; index < time.delay_count; ++index) {
    s_vpi_time after{vpiSimTime, static_cast<PLI_UINT32>(delays[index] >> 32u),
                     static_cast<PLI_UINT32>(delays[index]), 0.0};
    s_cb_data callback{};
    callback.reason = cbAfterDelay;
    callback.cb_rtn = &reactivated;
    callback.time = &after;
    callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
    (void)vpi_register_cb(&callback);
  }
  drain_changes();
}

PLI_INT32 synchronized(const p_cb_data data) {
  auto& instance = *reinterpret_cast<Instance*>(data->user_data);
  const bool read_only = data->reason == cbReadOnlySynch;
  instance.synchronization_pending[read_only ? 1u : 0u] = false;
  run(instance,
      read_only ? FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE
                : FSIM_TF_CALL_PHASE_SYNCHRONIZE,
      read_only ? reason_rosynch : reason_synch);
  return 0;
}

// misctf(reason_paramvc) for a changed argument (tf_asynchon), after any
// routine in progress returns.
PLI_INT32 parameter_changed(const p_cb_data data) {
  const auto watch = *reinterpret_cast<Watch*>(data->user_data);
  if (!watch.instance->asynchronous) {
    return 0;
  }
  pending_changes.push_back(watch);
  if (!routine_active) {
    drain_changes();
  }
  return 0;
}

void drain_changes() {
  while (!pending_changes.empty()) {
    const auto next = pending_changes.front();
    pending_changes.pop_front();
    if (next.instance->asynchronous) {
      run(*next.instance, FSIM_TF_CALL_PHASE_SYNCHRONIZE, reason_paramvc,
          next.parameter);
    }
  }
}

PLI_INT32 reactivated(const p_cb_data data) {
  run(*reinterpret_cast<Instance*>(data->user_data),
      FSIM_TF_CALL_PHASE_REACTIVATE, reason_reactivate);
  return 0;
}

PLI_INT32 end_of_compile(const p_cb_data data) {
  run(*reinterpret_cast<Instance*>(data->user_data),
      FSIM_TF_CALL_PHASE_CHECK, reason_endofcompile);
  return 0;
}

PLI_INT32 finished(const p_cb_data data) {
  run(*reinterpret_cast<Instance*>(data->user_data),
      FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE, reason_finish);
  return 0;
}

PLI_INT32 compiletf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  const auto call = vpi_handle(vpiSysTfCall, nullptr);
  if (call == nullptr) {
    return 0;
  }
  auto& instance = instance_for(call, cell);
  if (cell->checktf != nullptr) {
    run(instance, FSIM_TF_CALL_PHASE_CHECK, reason_checktf);
  }
  if (cell->misctf != nullptr) {
    for (const auto reason : {cbEndOfCompile, cbEndOfSimulation}) {
      s_cb_data callback{};
      callback.reason = reason;
      callback.cb_rtn = reason == cbEndOfCompile ? &end_of_compile : &finished;
      callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
      (void)vpi_register_cb(&callback);
    }
  }
  return 0;
}

PLI_INT32 sizetf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  const auto width =
      cell->sizetf == nullptr ? 32 : cell->sizetf(cell->data, reason_sizetf);
  return width > 0 ? width : 32;
}

PLI_INT32 calltf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  const auto call = vpi_handle(vpiSysTfCall, nullptr);
  if (call != nullptr) {
    run(instance_for(call, cell), FSIM_TF_CALL_PHASE_CALL, reason_calltf);
  }
  return 0;
}

}  // namespace

void veriusertfs_register_table(const p_tfcell table) {
  for (auto cell = table; cell != nullptr && cell->type != 0; ++cell) {
    s_vpi_systf_data data{};
    data.type = cell->type == usertask ? vpiSysTask : vpiSysFunc;
    data.sysfunctype =
        cell->type == userrealfunction ? vpiRealFunc : vpiSizedFunc;
    data.tfname = cell->tfname;
    data.calltf = &calltf;
    data.compiletf = &compiletf;
    data.sizetf = cell->type == userfunction ? &sizetf : nullptr;
    data.user_data = reinterpret_cast<PLI_BYTE8*>(cell);
    (void)vpi_register_systf(&data);
  }
}
