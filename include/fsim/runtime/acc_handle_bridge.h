// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/tf_call_bridge.h"

#include <stdint.h>

#define FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION 3u
#define FSIM_ACC_HANDLE_MAX_OBJECTS (1u << 20u)
#define FSIM_ACC_VPI_OBJECT_VALID 0u
#define FSIM_ACC_VPI_OBJECT_INVALID 1u
#define FSIM_ACC_VPI_OBJECT_STALE 2u
#define FSIM_ACC_VPI_OBJECT_RELEASED 3u
#define FSIM_ACC_VPI_OBJECT_NOT_FOUND 4u
#define FSIM_ACC_VPI_OBJECT_WRONG_TYPE 5u
#define FSIM_ACC_LOOKUP_ABSOLUTE 1u
#define FSIM_ACC_LOOKUP_RELATIVE 2u
#define FSIM_ACC_LOOKUP_PLI_SCOPE 3u
#define FSIM_ACC_RELATION_PARENT 1u
#define FSIM_ACC_RELATION_SCOPE 2u
#define FSIM_ACC_RELATION_SIMULATED_NET 3u
#define FSIM_ACC_NAME_MAXIMUM_BYTES 4096u
#define FSIM_ACC_TRAVERSE_BEGIN 1u
#define FSIM_ACC_TRAVERSE_NEXT 2u
#define FSIM_ACC_TRAVERSE_END 3u
#define FSIM_ACC_TRAVERSE_CELL 1u
#define FSIM_ACC_TRAVERSE_CHILD 2u
#define FSIM_ACC_TRAVERSE_NET 3u
#define FSIM_ACC_TRAVERSE_PARAMETER 4u
#define FSIM_ACC_TRAVERSE_PORT 5u
#define FSIM_ACC_TRAVERSE_PRIMITIVE 6u
#define FSIM_ACC_TRAVERSE_SCOPE 7u
#define FSIM_ACC_TRAVERSE_SPECPARAM 8u
#define FSIM_ACC_TRAVERSE_TOP_MODULE 9u
#define FSIM_ACC_COLLECTION_MAXIMUM_OBJECTS (1u << 20u)
#define FSIM_ACC_OBJECT_QUERY_ABI_VERSION 3u
#define FSIM_ACC_OBJECT_CONDITION 1u
#define FSIM_ACC_OBJECT_CONNECTION 2u
#define FSIM_ACC_OBJECT_DATAPATH 3u
#define FSIM_ACC_OBJECT_HICONN 4u
#define FSIM_ACC_OBJECT_LOCONN 5u
#define FSIM_ACC_OBJECT_MODULE_PATH 6u
#define FSIM_ACC_OBJECT_NOTIFIER 7u
#define FSIM_ACC_OBJECT_INTERMODULE_PATH 8u
#define FSIM_ACC_OBJECT_PATH_INPUT 9u
#define FSIM_ACC_OBJECT_PATH_OUTPUT 10u
#define FSIM_ACC_OBJECT_PORT_INDEX 11u
#define FSIM_ACC_OBJECT_TIMING_CHECK 12u
#define FSIM_ACC_OBJECT_TIMING_CHECK_ARG1 13u
#define FSIM_ACC_OBJECT_TIMING_CHECK_ARG2 14u
#define FSIM_ACC_OBJECT_TERMINAL_INDEX 15u
#define FSIM_ACC_READ_QUERY_ABI_VERSION 3u
#define FSIM_ACC_READ_OBJECT 1u
#define FSIM_ACC_READ_ATTRIBUTE 2u
#define FSIM_ACC_READ_DESIGN 3u
#define FSIM_ACC_READ_VALUE_NONE 0u
#define FSIM_ACC_READ_VALUE_LOGIC4 1u
#define FSIM_ACC_READ_VALUE_REAL 2u
#define FSIM_ACC_READ_VALUE_STRING 3u
#define FSIM_ACC_READ_MAXIMUM_BITS (1u << 20u)
#define FSIM_ACC_READ_MAXIMUM_STRING_BYTES (1u << 20u)
#define FSIM_ACC_WRITE_QUERY_ABI_VERSION 3u
#define FSIM_ACC_WRITE_SET_VALUE 1u
#define FSIM_ACC_WRITE_TIME_NONE 0u
#define FSIM_ACC_WRITE_CAP_DEPOSIT (1u << 0u)
#define FSIM_ACC_WRITE_CAP_FORCE (1u << 1u)
#define FSIM_ACC_WRITE_CAP_RELEASE (1u << 2u)
#define FSIM_ACC_WRITE_CAP_ASSIGN (1u << 3u)
#define FSIM_ACC_WRITE_CAP_DEASSIGN (1u << 4u)
#define FSIM_ACC_WRITE_CAP_ALL ((1u << 5u) - 1u)
#define FSIM_ACC_ITERATOR_QUERY_ABI_VERSION 3u
#define FSIM_ACC_ITERATOR_BEGIN 1u
#define FSIM_ACC_ITERATOR_NEXT 2u
#define FSIM_ACC_ITERATOR_END 3u
#define FSIM_ACC_ITERATOR_GENERIC 1u
#define FSIM_ACC_ITERATOR_BIT 2u
#define FSIM_ACC_ITERATOR_CELL_LOAD 3u
#define FSIM_ACC_ITERATOR_DRIVER 4u
#define FSIM_ACC_ITERATOR_HICONN 5u
#define FSIM_ACC_ITERATOR_INPUT 6u
#define FSIM_ACC_ITERATOR_LOAD 7u
#define FSIM_ACC_ITERATOR_LOCONN 8u
#define FSIM_ACC_ITERATOR_MODPATH 9u
#define FSIM_ACC_ITERATOR_OUTPUT 10u
#define FSIM_ACC_ITERATOR_PORTOUT 11u
#define FSIM_ACC_ITERATOR_TCHK 12u
#define FSIM_ACC_ITERATOR_TERMINAL 13u
#define FSIM_ACC_ITERATOR_MAXIMUM_TYPES 256u
#define FSIM_ACC_TIMING_QUERY_ABI_VERSION 3u
#define FSIM_ACC_TIMING_FETCH_DELAYS 1u
#define FSIM_ACC_TIMING_APPEND_DELAYS 2u
#define FSIM_ACC_TIMING_REPLACE_DELAYS 3u
#define FSIM_ACC_TIMING_FETCH_PULSE 4u
#define FSIM_ACC_TIMING_APPEND_PULSE 5u
#define FSIM_ACC_TIMING_REPLACE_PULSE 6u
#define FSIM_ACC_TIMING_SET_PULSE_PERCENT 7u
#define FSIM_ACC_TIMING_FETCH_DELAY_MODE 8u
#define FSIM_ACC_TIMING_FETCH_POLARITY 9u
#define FSIM_ACC_TIMING_CAP_DELAYS (1u << 0u)
#define FSIM_ACC_TIMING_CAP_PULSE (1u << 1u)
#define FSIM_ACC_TIMING_CAP_POLARITY (1u << 2u)
#define FSIM_ACC_TIMING_CAP_ALL ((1u << 3u) - 1u)
#define FSIM_ACC_TIMING_MAXIMUM_DELAYS 12u
#define FSIM_ACC_TIMING_TO_HIZ_FROM_USER 0u
#define FSIM_ACC_TIMING_TO_HIZ_AVERAGE 1u
#define FSIM_ACC_TIMING_TO_HIZ_MAXIMUM 2u
#define FSIM_ACC_TIMING_TO_HIZ_MINIMUM 3u
#define FSIM_ACC_VCL_QUERY_ABI_VERSION 3u
#define FSIM_ACC_VCL_REGISTER 1u
#define FSIM_ACC_VCL_UNREGISTER 2u
#define FSIM_ACC_VCL_MAXIMUM_LINKS (1u << 20u)
#define FSIM_ACC_SAFE_POINT_ABI_VERSION 3u
#define FSIM_ACC_TF_CONTEXT_ABI_VERSION 3u
#define FSIM_ACC_TF_MAXIMUM_ARGUMENTS 4096u
#define FSIM_ACC_TF_MAXIMUM_ARGV 4096u
#define FSIM_ACC_TF_MAXIMUM_ARGV_BYTES 4096u

#if defined(_WIN32)
#if defined(FSIM_ACC_LINK_SURFACE_BUILD)
#define FSIM_ACC_BRIDGE_API __declspec(dllexport)
#else
#define FSIM_ACC_BRIDGE_API __declspec(dllimport)
#endif
#else
#define FSIM_ACC_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_resolve_v3_fn)(
    void* user_data, uint64_t vpi_handle, PLI_INT32* acc_type);
typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_lookup_v3_fn)(
    void* user_data, uint32_t mode, uint64_t scope, const PLI_BYTE8* name,
    uint32_t name_size, uint64_t* vpi_handle);
typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_relation_v3_fn)(
    void* user_data, uint32_t relation, uint64_t object,
    uint64_t* vpi_handle);
typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_name_v3_fn)(
    void* user_data, uint64_t object, PLI_BYTE8* buffer,
    uint32_t buffer_capacity, uint32_t* name_size);
typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_traverse_v3_fn)(
    void* user_data, uint32_t operation, uint32_t family, uint64_t scope,
    uint64_t* cursor, uint64_t* vpi_handle);

typedef struct fsim_acc_object_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t reserved;
  uint64_t owner;
  uint64_t first;
  uint64_t second;
  PLI_INT32 index;
  PLI_INT32 object_type;
  PLI_INT32 first_edge;
  PLI_INT32 second_edge;
  const PLI_BYTE8* first_name;
  uint32_t first_name_size;
  const PLI_BYTE8* second_name;
  uint32_t second_name_size;
} fsim_acc_object_query_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_object_query_v3_fn)(
    void* user_data, const fsim_acc_object_query_v3* query,
    uint64_t* vpi_handle);

typedef struct fsim_acc_logic_word_v3 {
  uint32_t aval;
  uint32_t bval;
} fsim_acc_logic_word_v3;

typedef struct fsim_acc_read_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t reserved;
  uint64_t object;
  const PLI_BYTE8* attribute_name;
  uint32_t attribute_name_size;
  uint32_t reserved2;
} fsim_acc_read_query_v3;

typedef struct fsim_acc_read_result_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  PLI_INT32 type;
  PLI_INT32 full_type;
  PLI_INT32 direction;
  PLI_INT32 edge;
  PLI_INT32 index;
  PLI_INT32 parameter_type;
  PLI_INT32 width;
  uint32_t write_capabilities;
  uint32_t timing_capabilities;
  uint32_t timing_delay_count;
  PLI_INT32 has_range;
  PLI_INT32 msb;
  PLI_INT32 lsb;
  PLI_INT16 timescale_unit;
  PLI_INT16 timescale_precision;
  PLI_INT32 design_precision;
  PLI_INT32 source_line;
  uint32_t value_kind;
  uint32_t logic_word_count;
  const fsim_acc_logic_word_v3* logic_words;
  double real_value;
  double parameter_value;
  double attribute_real;
  PLI_INT32 attribute_integer;
  uint32_t reserved;
  const PLI_BYTE8* name;
  uint32_t name_size;
  const PLI_BYTE8* full_name;
  uint32_t full_name_size;
  const PLI_BYTE8* definition_name;
  uint32_t definition_name_size;
  const PLI_BYTE8* source_file;
  uint32_t source_file_size;
  const PLI_BYTE8* string_value;
  uint32_t string_value_size;
  const PLI_BYTE8* binary_value;
  uint32_t binary_value_size;
  const PLI_BYTE8* octal_value;
  uint32_t octal_value_size;
  const PLI_BYTE8* decimal_value;
  uint32_t decimal_value_size;
  const PLI_BYTE8* hexadecimal_value;
  uint32_t hexadecimal_value_size;
  const PLI_BYTE8* strength_value;
  uint32_t strength_value_size;
  const PLI_BYTE8* attribute_string;
  uint32_t attribute_string_size;
} fsim_acc_read_result_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_read_v3_fn)(
    void* user_data, const fsim_acc_read_query_v3* query,
    fsim_acc_read_result_v3* result);

typedef struct fsim_acc_write_value_v3 {
  PLI_INT32 format;
  PLI_INT32 width;
  uint32_t logic_word_count;
  uint32_t reserved;
  const fsim_acc_logic_word_v3* logic_words;
  double real_value;
  const PLI_BYTE8* string_value;
  uint32_t string_value_size;
  uint32_t reserved2;
} fsim_acc_write_value_v3;

typedef struct fsim_acc_write_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t reserved;
  uint64_t object;
  PLI_INT32 object_type;
  PLI_INT32 object_full_type;
  PLI_INT32 model;
  PLI_INT32 time_type;
  uint64_t time_ticks;
  double time_real;
  fsim_acc_write_value_v3 value;
} fsim_acc_write_query_v3;

typedef struct fsim_acc_write_result_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t reserved2;
  fsim_acc_read_result_v3 current_value;
} fsim_acc_write_result_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_write_v3_fn)(
    void* user_data, const fsim_acc_write_query_v3* query,
    fsim_acc_write_result_v3* result);

typedef struct fsim_acc_iterator_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t family;
  uint64_t owner;
  uint64_t cursor;
  const PLI_INT32* types;
  uint32_t type_count;
  uint32_t reserved;
} fsim_acc_iterator_query_v3;

typedef struct fsim_acc_iterator_result_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t reserved2;
  uint64_t cursor;
  uint64_t object;
} fsim_acc_iterator_result_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_iterate_v3_fn)(
    void* user_data, const fsim_acc_iterator_query_v3* query,
    fsim_acc_iterator_result_v3* result);

typedef struct fsim_acc_timing_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t reserved;
  uint64_t object;
  PLI_INT32 object_type;
  PLI_INT32 object_full_type;
  uint32_t delay_count;
  uint32_t min_typ_max;
  PLI_INT32 to_hiz_policy;
  uint32_t value_count;
  uint32_t reserved2;
  const double* values;
} fsim_acc_timing_query_v3;

typedef struct fsim_acc_timing_result_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t reserved2;
  PLI_INT32 delay_mode;
  PLI_INT32 polarity;
  uint32_t value_count;
  uint32_t reserved3;
  const double* values;
} fsim_acc_timing_result_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_timing_v3_fn)(
    void* user_data, const fsim_acc_timing_query_v3* query,
    fsim_acc_timing_result_v3* result);

typedef struct fsim_acc_vcl_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t operation;
  uint32_t reserved;
  uint64_t link_identity;
  uint64_t object;
  PLI_INT32 object_type;
  PLI_INT32 object_full_type;
  PLI_INT32 flags;
  uint32_t reserved2;
} fsim_acc_vcl_query_v3;

typedef uint32_t(FSIM_NATIVE_PLUGIN_CALL *fsim_acc_vpi_vcl_v3_fn)(
    void* user_data, const fsim_acc_vcl_query_v3* query);

typedef struct fsim_acc_vcl_event_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  PLI_INT32 reason;
  uint64_t link_identity;
  uint64_t sequence_identity;
  PLI_INT32 time_high;
  PLI_INT32 time_low;
  uint32_t logic_value;
  uint32_t strength1;
  uint32_t strength2;
  uint32_t vector_width;
  uint32_t logic_word_count;
  uint32_t reserved2;
  const fsim_acc_logic_word_v3* logic_words;
  double real_value;
} fsim_acc_vcl_event_v3;

typedef struct fsim_acc_safe_point_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t reserved2;
  uint64_t identity;
} fsim_acc_safe_point_v3;

typedef struct fsim_acc_tf_context_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t reserved2;
  const fsim_tf_call_context_v3* call_context;
  const uint64_t* argument_objects;
  uint32_t argument_count;
  uint32_t argc;
  PLI_BYTE8* const* argv;
  const uint32_t* argv_sizes;
} fsim_acc_tf_context_v3;

typedef struct fsim_acc_handle_context_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t simulation_identity;
  uint64_t hierarchy_generation;
  uint32_t maximum_handles;
  uint32_t reserved;
  void* user_data;
  fsim_acc_vpi_resolve_v3_fn resolve;
  uint64_t calling_scope;
  uint64_t default_scope;
  uint64_t interactive_scope;
  fsim_acc_vpi_lookup_v3_fn lookup;
  fsim_acc_vpi_relation_v3_fn relation;
  fsim_acc_vpi_name_v3_fn name;
  fsim_acc_vpi_traverse_v3_fn traverse;
  fsim_acc_vpi_object_query_v3_fn object_query;
  fsim_acc_vpi_read_v3_fn read;
  fsim_acc_vpi_write_v3_fn write;
  fsim_acc_vpi_iterate_v3_fn iterate;
  fsim_acc_vpi_timing_v3_fn timing;
  fsim_acc_vpi_vcl_v3_fn vcl;
  const fsim_acc_tf_context_v3* tf;
} fsim_acc_handle_context_v3;

FSIM_ACC_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_acc_handle_context_enter_v3(fsim_acc_handle_context_v3* context);
FSIM_ACC_BRIDGE_API void FSIM_NATIVE_PLUGIN_CALL
fsim_acc_handle_context_leave_v3(fsim_acc_handle_context_v3* context);
FSIM_ACC_BRIDGE_API handle FSIM_NATIVE_PLUGIN_CALL
fsim_acc_handle_from_vpi_v3(uint64_t vpi_handle);
FSIM_ACC_BRIDGE_API uint64_t FSIM_NATIVE_PLUGIN_CALL
fsim_acc_handle_to_vpi_v3(handle object);
FSIM_ACC_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_acc_vcl_dispatch_v3(fsim_acc_handle_context_v3* context,
                         const fsim_acc_vcl_event_v3* event);
FSIM_ACC_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_acc_safe_point_advance_v3(fsim_acc_handle_context_v3* context,
                               const fsim_acc_safe_point_v3* safe_point);
FSIM_ACC_BRIDGE_API PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_acc_vpi_same_object_v3(handle object, uint64_t vpi_handle);

#define FSIM_ACC_STANDARD_QUERY_ABI_VERSION 3u
#define FSIM_ACC_STANDARD_NAME_MAXIMUM_BYTES 128u

enum fsim_acc_standard_query_kind_v3 {
  FSIM_ACC_STANDARD_ROUTINE = 1,
  FSIM_ACC_STANDARD_OBJECT_TYPE = 2,
  FSIM_ACC_STANDARD_CONFIGURATION = 3,
  FSIM_ACC_STANDARD_EDGE = 4,
  FSIM_ACC_STANDARD_DELAY_MODE = 5,
  FSIM_ACC_STANDARD_UPDATE = 6,
  FSIM_ACC_STANDARD_VALUE_FORMAT = 7,
  FSIM_ACC_STANDARD_VCL_REASON = 8,
  FSIM_ACC_STANDARD_TIME_TYPE = 9,
  FSIM_ACC_STANDARD_PRODUCT_TYPE = 10
};

enum fsim_acc_standard_query_status_v3 {
  FSIM_ACC_STANDARD_SUPPORTED = 0,
  FSIM_ACC_STANDARD_INVALID_QUERY = 1,
  FSIM_ACC_STANDARD_UNSUPPORTED_ROUTINE = 2,
  FSIM_ACC_STANDARD_UNSUPPORTED_OBJECT = 3,
  FSIM_ACC_STANDARD_UNSUPPORTED_BEHAVIOR = 4,
  FSIM_ACC_STANDARD_DISPATCH_REJECTED = 5,
  FSIM_ACC_STANDARD_DISPATCH_EXCEPTION = 6
};

typedef struct fsim_acc_standard_query_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t kind;
  uint32_t name_size;
  const PLI_BYTE8* name;
  PLI_INT32 value;
  uint32_t reserved;
} fsim_acc_standard_query_v3;

typedef struct fsim_acc_standard_query_result_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t status;
  uint32_t diagnostic_size;
  const PLI_BYTE8* diagnostic;
  uint32_t dispatched;
  uint32_t reserved;
} fsim_acc_standard_query_result_v3;

typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL*
                      fsim_acc_standard_dispatch_v3_fn)(
    void* user_data, const fsim_acc_standard_query_v3* query);

FSIM_ACC_BRIDGE_API uint32_t FSIM_NATIVE_PLUGIN_CALL
fsim_acc_validate_and_dispatch_standard_v3(
    const fsim_acc_standard_query_v3* query,
    fsim_acc_standard_dispatch_v3_fn dispatch, void* user_data,
    fsim_acc_standard_query_result_v3* result);

#ifdef __cplusplus
}
#endif
