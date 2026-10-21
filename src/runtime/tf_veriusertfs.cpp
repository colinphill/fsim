// SPDX-License-Identifier: Apache-2.0

// PLI 1.0 task/function tables (IEEE 1364-2005 Annex G, veriusertfs)
// registered over the VPI. Each table cell becomes a VPI system task or
// function whose routines run the cell's checktf, sizetf, calltf and
// misctf inside a TF call context, so the tf_* routines of tf_link.cpp
// see the call's arguments, time and instance.

#include "vpi_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/tf_call_bridge.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

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
};

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
  case vpiTimeVar:
    argument.kind = tf_readwrite;
    break;
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

// Runs one routine of a cell in a TF call context, then applies what it
// assigned and scheduled.
void run(Instance& instance, const uint32_t phase, const PLI_INT32 reason) {
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
    result = cell->misctf == nullptr ? 0
                                     : cell->misctf(cell->data, reason, 0);
    break;
  }
  (void)result;
  instance.work_area = context.work_area;
  fsim_tf_call_context_leave_v3(&context);

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
