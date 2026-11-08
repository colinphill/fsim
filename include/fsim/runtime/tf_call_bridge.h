// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

#include <stdint.h>

#define FSIM_TF_CALL_RESULT_NONE 0u
#define FSIM_TF_CALL_RESULT_INTEGRAL 1u
#define FSIM_TF_CALL_RESULT_REAL 2u
#define FSIM_TF_CALL_PHASE_CHECK 1u
#define FSIM_TF_CALL_PHASE_SIZE 2u
#define FSIM_TF_CALL_PHASE_CALL 3u
#define FSIM_TF_CALL_PHASE_SYNCHRONIZE 4u
#define FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE 5u
#define FSIM_TF_CALL_PHASE_REACTIVATE 6u
#define FSIM_TF_VALUE_NONE 0u
#define FSIM_TF_VALUE_INTEGRAL 1u
#define FSIM_TF_VALUE_REAL 2u
#define FSIM_TF_VALUE_STRING 3u
#define FSIM_TF_INSTANCE_ABI_VERSION 3u
#define FSIM_TF_CONTROL_OUTPUT 1u
#define FSIM_TF_CONTROL_WARNING 2u
#define FSIM_TF_CONTROL_ERROR 3u
#define FSIM_TF_CONTROL_MESSAGE 4u
#define FSIM_TF_CONTROL_FINISH 5u
#define FSIM_TF_CONTROL_STOP 6u
// tf_write_save and tf_read_restart (IEEE 1364-2001 25.65, 25.41): the text
// of the request is the block of bytes to save, or the buffer to fill.
#define FSIM_TF_CONTROL_SAVE_WRITE 7u
#define FSIM_TF_CONTROL_RESTART_READ 8u
#define FSIM_TF_CONTROL_MAX_TEXT_SIZE 65536u
#define FSIM_TF_CONTROL_MAX_METADATA_SIZE 255u
#define FSIM_TF_SYNCHRONIZATION_READ_WRITE 1u
#define FSIM_TF_SYNCHRONIZATION_READ_ONLY 2u
// tf_asynchon and tf_asynchoff: misctf(reason_paramvc) on argument changes.
#define FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_ON 3u
#define FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_OFF 4u
#define FSIM_TF_SYNCHRONIZATION_MAX_REQUESTS 256u

#if defined(_WIN32)
#if defined(FSIM_TF_LINK_SURFACE_BUILD)
#define FSIM_TF_BRIDGE_API __declspec(dllexport)
#else
#define FSIM_TF_BRIDGE_API __declspec(dllimport)
#endif
#else
#define FSIM_TF_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fsim_tf_argument_bridge_v3 {
  PLI_INT32 kind;
  uint32_t width;
  uint32_t is_signed;
  PLI_INT32 lhs_select;
  PLI_INT32 rhs_select;
  uint32_t expression_size;
  PLI_BYTE8* expression;
} fsim_tf_argument_bridge_v3;

typedef struct fsim_tf_value_bridge_v3 {
  uint32_t kind;
  uint32_t width;
  uint32_t word_count;
  uint32_t writable;
  uint32_t assigned;
  uint32_t reserved;
  struct t_vecval* vector_words;
  double real;
  uint32_t string_size;
  PLI_BYTE8* string_value;
} fsim_tf_value_bridge_v3;

typedef struct fsim_tf_instance_bridge_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t design_id;
  uint64_t hierarchy_id;
  uint32_t generation;
  uint32_t reserved;
} fsim_tf_instance_bridge_v3;

typedef struct fsim_tf_time_bridge_v3 {
  int8_t unit_exponent;
  int8_t precision_exponent;
  uint16_t reserved16;
  uint32_t reserved32;
  uint64_t tick_multiplier;
  uint64_t scheduler_ticks;
  uint64_t next_event_ticks;
  uint32_t has_next_event;
  uint32_t delay_count;
  uint32_t delay_capacity;
  uint32_t reserved;
  uint64_t* delay_ticks;
} fsim_tf_time_bridge_v3;

typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL* fsim_tf_control_emit_v3)(
    void* user_data, uint32_t kind, PLI_INT32 channel, PLI_INT32 level,
    const PLI_BYTE8* facility, uint32_t facility_size,
    const PLI_BYTE8* message_number, uint32_t message_number_size,
    const PLI_BYTE8* text, uint32_t text_size);

typedef struct fsim_tf_call_context_v3 {
  uint32_t phase;
  uint32_t kind;
  uint32_t width;
  uint32_t assigned;
  uint32_t reserved;
  uint32_t word_count;
  PLI_UINT32* aval_words;
  PLI_UINT32* bval_words;
  double real;
  uint32_t argument_count;
  const fsim_tf_argument_bridge_v3* arguments;
  fsim_tf_value_bridge_v3* values;
  const fsim_tf_instance_bridge_v3* instance;
  fsim_tf_time_bridge_v3* time;
  PLI_INT32 user_data;
  uint32_t context_reserved;
  PLI_BYTE8* module_instance_name;
  PLI_BYTE8* scope_name;
  PLI_BYTE8* routine_name;
  PLI_BYTE8* work_area;
  void* control_user_data;
  fsim_tf_control_emit_v3 control_emit;
  uint32_t control_failed;
  uint32_t control_reserved;
  uint32_t synchronization_count;
  uint32_t synchronization_capacity;
  uint32_t synchronization_reserved;
  uint32_t* synchronization_kinds;
} fsim_tf_call_context_v3;

// Host services of one call that only some hosts provide, bound after the
// context is entered and dropped when it is left. The pvc arrays hold one
// byte per argument: the current and the saved parameter value change flag
// (IEEE 1364-2001 25.9, 25.33). put schedules a delayed string put of
// argument narg (25.52): format is b, o, d or h, delay_ticks counts the
// calling module's time precision, and delaytype is Table 196.
typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL* fsim_tf_host_put_v3)(
    void* user_data, PLI_INT32 narg, PLI_INT32 format, const PLI_BYTE8* text,
    uint64_t delay_ticks, PLI_INT32 delaytype);

// Node facts of argument narg that the argument bridge does not carry
// (tf_nodeinfo, IEEE 1364-2001 25.35). A node_type of 0 means the host gave
// none for that argument and the routine derives the node from the
// argument's class and width; otherwise node_type is a tf_*_node constant,
// ms_index and ls_index are the declared bounds of a vector node and
// mem_size is the element count of a memory node.
typedef struct fsim_tf_host_node_v3 {
  PLI_INT32 node_type;
  PLI_INT32 ms_index;
  PLI_INT32 ls_index;
  PLI_INT32 mem_size;
} fsim_tf_host_node_v3;

// strength reads the strength bit patterns of scalar net argument narg
// (25.35, 7.10): bit n of strength0 and of strength1 is set for strength
// level n of that scale; returns 0 when it filled both. memory reads the
// whole of memory argument narg, one element after another in address
// order, each element its aval bytes then its bval bytes (Figure 169); data
// stays valid until the call returns; returns 0 when it filled the outputs.
typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL* fsim_tf_host_strength_v3)(
    void* user_data, PLI_INT32 narg, PLI_INT32* strength0,
    PLI_INT32* strength1);
typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL* fsim_tf_host_memory_v3)(
    void* user_data, PLI_INT32 narg, uint32_t* group_bytes, uint32_t* count,
    const PLI_BYTE8** data);

// simulation_precision_exponent is the simulation time unit, the smallest
// precision of the design (25.26, 25.27), and simulation_ticks the current
// time counted in it (25.54), when has_simulation_precision is set; the time
// bridge's precision is then the calling module's own.
typedef struct fsim_tf_host_bridge_v3 {
  uint32_t pvc_count;
  uint32_t reserved;
  uint8_t* pvc_current;
  uint8_t* pvc_saved;
  void* put_user_data;
  fsim_tf_host_put_v3 put;
  uint32_t node_count;
  int16_t simulation_precision_exponent;
  uint16_t has_simulation_precision;
  uint64_t simulation_ticks;
  const fsim_tf_host_node_v3* nodes;
  fsim_tf_host_strength_v3 strength;
  fsim_tf_host_memory_v3 memory;
} fsim_tf_host_bridge_v3;

FSIM_TF_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_tf_call_context_enter_v3(fsim_tf_call_context_v3* context);
FSIM_TF_BRIDGE_API void FSIM_NATIVE_PLUGIN_CALL
fsim_tf_call_context_leave_v3(fsim_tf_call_context_v3* context);
FSIM_TF_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_tf_call_host_bind_v3(fsim_tf_call_context_v3* context,
                          const fsim_tf_host_bridge_v3* host);
FSIM_TF_BRIDGE_API const fsim_tf_call_context_v3* FSIM_NATIVE_PLUGIN_CALL
fsim_tf_current_call_context_v3(void);

#ifdef __cplusplus
}
#endif
