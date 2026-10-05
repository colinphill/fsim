/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FSIM_COMPILER_JIT_RUNTIME_V2_H
#define FSIM_COMPILER_JIT_RUNTIME_V2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_JIT_FRAME_STATE_READY_V2 UINT32_C(0)
#define FSIM_JIT_FRAME_STATE_COMPLETED_V2 UINT32_C(1)
#define FSIM_JIT_FRAME_STATE_STOPPED_V2 UINT32_C(2)
#define FSIM_JIT_FRAME_STATE_ASSERTION_FAILED_V2 UINT32_C(3)
#define FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2 UINT32_C(4)
#define FSIM_JIT_RESUME_STATUS_COMPLETED_V2 UINT32_C(0)
#define FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED_V2 UINT32_C(1)
#define FSIM_JIT_RESUME_STATUS_WAIT_FOR_V2 UINT32_C(2)
#define FSIM_JIT_RESUME_STATUS_YIELDED_V2 UINT32_C(3)
#define FSIM_JIT_RESUME_STATUS_STOPPED_V2 UINT32_C(4)
#define FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2 UINT32_C(5)
#define FSIM_JIT_RESUME_STATUS_WAIT_ON_V2 UINT32_C(6)
#define FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2 UINT32_C(7)
#define FSIM_JIT_RESUME_STATUS_DEBUG_POINT_V2 UINT32_C(8)
#define FSIM_JIT_RESUME_STATUS_WAIT_FOREVER_V2 UINT32_C(9)
#define FSIM_JIT_RESUME_STATUS_PAUSED_V2 UINT32_C(10)
#define FSIM_JIT_RESUME_STATUS_FORK_V2 UINT32_C(11)
#define FSIM_JIT_RESUME_STATUS_FORK_END_V2 UINT32_C(12)
#define FSIM_JIT_RESUME_STATUS_WAIT_FORK_V2 UINT32_C(13)
#define FSIM_JIT_RESUME_STATUS_DISABLE_FORK_V2 UINT32_C(14)
#define FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY_V2 UINT32_C(15)
#define FSIM_JIT_INVALID_INSTRUCTION_V2 UINT32_MAX
#define FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS_V2 UINT32_C(1)
#define FSIM_JIT_PROJECTED_TRANSPORT_V2 UINT32_C(0)
#define FSIM_JIT_PROJECTED_INERTIAL_V2 UINT32_C(1)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_V2 UINT32_C(0)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_V2 UINT32_C(1)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_V2 UINT32_C(2)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE_V2 UINT32_C(3)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE_V2 UINT32_C(4)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE_V2 UINT32_C(5)

#define FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(X) \
  X(read_signal) \
  X(write_signal) \
  X(assert_failed) \
  X(write_update) \
  X(write_after) \
  X(write_signal_slice) \
  X(write_update_slice) \
  X(write_after_slice) \
  X(signal_event) \
  X(signal_last_value) \
  X(signal_last_event) \
  X(signal_active) \
  X(write_output) \
  X(schedule_output) \
  X(write_report) \
  X(write_formatted) \
  X(write_time) \
  X(install_monitor) \
  X(control_monitor) \
  X(random_value) \
  X(write_inertial) \
  X(write_inertial_slice) \
  X(write_projected) \
  X(write_projected_slice) \
  X(write_projected_waveform) \
  X(write_projected_waveform_slice) \
  X(read_signal_logic9) \
  X(write_signal_logic9) \
  X(write_update_logic9) \
  X(write_after_logic9) \
  X(write_signal_slice_logic9) \
  X(write_update_slice_logic9) \
  X(write_after_slice_logic9) \
  X(signal_last_value_logic9) \
  X(write_inertial_logic9) \
  X(write_inertial_slice_logic9) \
  X(write_projected_logic9) \
  X(write_projected_slice_logic9) \
  X(write_projected_waveform_logic9) \
  X(write_projected_waveform_slice_logic9) \
  X(write_formatted_logic9) \
  X(load_string) \
  X(copy_string) \
  X(read_string_object) \
  X(write_string_object) \
  X(concatenate_strings) \
  X(compare_strings) \
  X(string_length) \
  X(string_index) \
  X(string_replace_byte) \
  X(write_string_output) \
  X(file_open) \
  X(file_close) \
  X(file_write) \
  X(file_read_line) \
  X(file_end_of_file) \
  X(file_error) \
  X(container_operation) \
  X(force_signal_slice) \
  X(force_signal_slice_logic9) \
  X(release_signal_slice) \
  X(signal_last_active) \
  X(signal_driving) \
  X(signal_driving_value) \
  X(signal_driving_value_logic9) \
  X(read_simulation_time) \
  X(vital_timing_check) \
  X(vital_delay) \
  X(force_driver_signal_slice) \
  X(force_driver_signal_slice_logic9) \
  X(release_driver_signal_slice) \
  X(execute_signal_operation) \
  X(container_read_word) \
  X(container_write_word) \
  X(container_read_packed) \
  X(container_write_packed) \
  X(read_signal_packed) \
  X(write_signal_packed) \
  X(read_signal_dynamic_part) \
  X(record_code_coverage_counter) \
  X(sample_coverage) \
  X(execute_class_property_operation) \
  X(query_event_triggered) \
  X(write_projected_signal_packed) \
  X(container_read_packed_index64)

/* ABI v2 separates immutable callback services from per-executor state. */
#define FSIM_JIT_SERVICES_ABI_VERSION_V2 UINT32_C(2)
#define FSIM_JIT_RUNTIME_ABI_VERSION_V2 UINT32_C(2)
#define FSIM_JIT_FRAME_ABI_VERSION_V2 UINT32_C(2)
#define FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2 UINT32_C(2)
#define FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V2 UINT32_C(64)
#define FSIM_JIT_SIGNAL_UPDATE_DOMAIN_GENERIC_V2 UINT32_C(0)
#define FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_ACTIVE_V2 UINT32_C(1)
#define FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_NBA_V2 UINT32_C(2)

typedef struct fsim_jit_projected_element_v2 {
  uint64_t aval;
  uint64_t bval;
  uint64_t delay;
} fsim_jit_projected_element_v2;

typedef struct fsim_jit_logic9_word_v2 {
  uint64_t planes[4];
} fsim_jit_logic9_word_v2;

typedef struct fsim_jit_logic9_projected_element_v2 {
  fsim_jit_logic9_word_v2 value;
  uint64_t delay;
} fsim_jit_logic9_projected_element_v2;

/* Tagged updates use ordered callbacks; slot batching stays generic-only. */
typedef struct fsim_jit_update_slot_v2 {
  uint64_t aval;
  uint64_t bval;
  uint64_t logic9_plane2;
  uint64_t logic9_plane3;
  uint64_t mask;
  uint32_t active;
  uint32_t reserved;
  uint64_t* wide_aval;
  uint64_t* wide_bval;
  uint64_t* wide_mask;
  uint32_t word_count;
  uint32_t width;
} fsim_jit_update_slot_v2;

typedef struct fsim_jit_frame_v2 fsim_jit_frame_v2;
typedef struct fsim_jit_runtime_instance_v2 fsim_jit_runtime_instance_v2;

/*
 * Immutable callback table shared by executors with the same service family.
 * Callback context is per-instance and supplied explicitly. The table is
 * immutable after publication. Callbacks must not unwind across this C
 * boundary. Update-domain values use uint32_t: GENERIC=0,
 * SYSTEMVERILOG_ACTIVE=1, and SYSTEMVERILOG_NBA=2. Packed blocking writes pass
 * GENERIC and use the process origin region.
 */
typedef struct fsim_jit_services_v2 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t (*read_signal)(
      void* context, uint32_t signal, uint64_t* bval);
  void (*write_signal)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval);
  void (*assert_failed)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      const char* message,
      uint64_t message_size);
  /*
   * Version 2 update callback. The complete v2 table layout is required;
   * callbacks used by a process must be non-null before native entry. The
   * table offsets are independent of the v1 runtime.
   */
  void (*write_update)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint32_t update_domain);
  void (*write_after)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay,
      uint32_t update_domain);
  /*
   * Version 2 extension for packed partial writes. offset and width
   * select normalized low-bit-first positions within the target signal.
   */
  void (*write_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval);
  void (*write_update_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint32_t update_domain);
  void (*write_after_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay,
      uint32_t update_domain);
  /* Version 2 delta-scoped signal event query. Returns zero or one. */
  uint32_t (*signal_event)(void* context, uint32_t signal);
  /*
   * Version 2 effective value immediately before the latest signal event.
   * Uses the same aval/bval representation as read_signal.
   */
  uint64_t (*signal_last_value)(
      void* context, uint32_t signal, uint64_t* bval);
  /* Version 2 elapsed global-resolution ticks since the latest event. */
  uint64_t (*signal_last_event)(void* context, uint32_t signal);
  /* Version 2 delta-scoped signal transaction query. Returns zero or one. */
  uint32_t (*signal_active)(void* context, uint32_t signal);
  /*
   * Version 2 synchronous language-output callback. text is valid only for
   * the duration of the call; newline is zero or one.
   */
  void (*write_output)(
      void* context,
      uint32_t process,
      const char* text,
      uint64_t text_size,
      uint32_t newline);
  /*
   * Version 2 postponed language-output callback. The callback copies or
   * consumes text during the call and schedules publication in the current
   * timestamp's postponed phase.
   */
  void (*schedule_output)(
      void* context,
      uint32_t process,
      const char* text,
      uint64_t text_size,
      uint32_t newline);
  /*
   * Version 2 report callback. instruction identifies immutable SimIR
   * report metadata, including any evaluated string/severity register IDs,
   * owned by the embedding process descriptor.
   */
  void (*write_report)(
      void* context,
      uint32_t process,
      uint32_t instruction);
  /*
   * Version 2 runtime-value formatting callback. instruction identifies
   * immutable FormatDisplay metadata; aval/bval use the common packed word
   * representation.
   */
  void (*write_formatted)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t width,
      uint64_t aval,
      uint64_t bval);
  /*
   * Version 2 current-time output callback. instruction identifies
   * immutable TimeDisplay metadata owned by the embedding process.
   */
  void (*write_time)(
      void* context,
      uint32_t process,
      uint32_t instruction);
  /* Version 2 monitor registration and on/off callbacks. */
  void (*install_monitor)(
      void* context,
      uint32_t process,
      uint32_t instruction);
  void (*control_monitor)(
      void* context,
      uint32_t process,
      uint32_t instruction);
  /*
   * Version 2 deterministic random-value callback. Bound values and the
   * result use the common low-word aval/bval encoding.
   */
  uint64_t (*random_value)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t maximum_aval,
      uint64_t maximum_bval,
      uint64_t minimum_aval,
      uint64_t minimum_bval,
      uint64_t* result_bval);
  /*
   * Version 2 transition-specific inertial writes. A later invocation for
   * the same generated continuous driver supersedes its pending update.
   */
  void (*write_inertial)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay,
      uint32_t update_domain);
  void (*write_inertial_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay,
      uint32_t update_domain);
  /*
   * Version 2 VHDL projected-output-waveform writes. mode is one of the
   * FSIM_JIT_PROJECTED_*_V2 constants; rejection is ignored in transport mode.
   */
  void (*write_projected)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  /*
   * Version 2 atomic multi-element waveform writes. elements is valid only
   * for the duration of the synchronous callback and contains count entries.
   */
  void (*write_projected_waveform)(
      void* context,
      uint32_t signal,
      uint32_t width,
      const fsim_jit_projected_element_v2* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_projected_element_v2* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  /*
   * Version 2 exact Logic9 extension. Values are passed by pointer so the
   * ABI does not depend on platform-specific aggregate calling conventions.
   */
  void (*read_signal_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v2* value);
  void (*write_signal_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v2* value);
  void (*write_update_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v2* value,
      uint32_t update_domain);
  void (*write_after_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v2* value,
      uint64_t delay,
      uint32_t update_domain);
  void (*write_signal_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value);
  void (*write_update_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value,
      uint32_t update_domain);
  void (*write_after_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value,
      uint64_t delay,
      uint32_t update_domain);
  void (*signal_last_value_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v2* value);
  void (*write_inertial_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v2* value,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay,
      uint32_t update_domain);
  void (*write_inertial_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay,
      uint32_t update_domain);
  void (*write_projected_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v2* value,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_logic9)(
      void* context,
      uint32_t signal,
      uint32_t width,
      const fsim_jit_logic9_projected_element_v2* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_projected_element_v2* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_formatted_logic9)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value);
  /*
   * Version 2 mutable-string helpers. Generated code passes only stable
   * register/object IDs and immutable byte spans; storage ownership and
   * allocator layout remain entirely on the embedding side of this C ABI.
   * Every helper returns zero on success and nonzero after containing an
   * embedding failure. Scalar results are written through the final pointer.
   */
  uint32_t (*load_string)(
      void* context,
      uint32_t destination,
      const char* bytes,
      uint64_t byte_count);
  uint32_t (*copy_string)(
      void* context,
      uint32_t destination,
      uint32_t source);
  uint32_t (*read_string_object)(
      void* context,
      uint32_t destination,
      uint32_t object);
  uint32_t (*write_string_object)(
      void* context,
      uint32_t object,
      uint32_t source);
  uint32_t (*concatenate_strings)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t destination,
      const uint32_t* operands,
      uint32_t operand_count);
  uint32_t (*compare_strings)(
      void* context,
      uint32_t lhs,
      uint32_t rhs,
      uint32_t not_equal,
      uint32_t* result);
  uint32_t (*string_length)(
      void* context,
      uint32_t source,
      uint32_t* result);
  uint32_t (*string_index)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t source,
      uint64_t index_aval,
      uint64_t index_bval,
      uint32_t signed_index,
      uint32_t* result);
  uint32_t (*string_replace_byte)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t target,
      uint64_t index_aval,
      uint64_t index_bval,
      uint32_t signed_index,
      uint64_t source_aval,
      uint64_t source_bval);
  uint32_t (*write_string_output)(
      void* context,
      uint32_t process,
      uint32_t source,
      const char* prefix,
      uint64_t prefix_size,
      const char* suffix,
      uint64_t suffix_size,
      uint32_t newline,
      uint32_t postponed);
  /*
   * Version 2 bounded text-file helpers. Each instruction's immutable
   * operation metadata identifies string/value registers and formatting.
   * Only HDL handle values and scalar results cross this ABI; host streams,
   * descriptors, filesystem objects, and addresses remain embedding-owned.
   */
  uint32_t (*file_open)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t* result);
  uint32_t (*file_close)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t handle_aval,
      uint64_t handle_bval);
  uint32_t (*file_write)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t handle_aval,
      uint64_t handle_bval);
  uint32_t (*file_read_line)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t handle_aval,
      uint64_t handle_bval,
      uint32_t* result);
  uint32_t (*file_end_of_file)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t handle_aval,
      uint64_t handle_bval,
      uint32_t* result);
  uint32_t (*file_error)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t handle_aval,
      uint64_t handle_bval,
      uint32_t* result);
  /*
   * Version 2 bounded-container helper. instruction identifies immutable
   * SimIR operation metadata. Dynamic storage and container registers remain
   * embedding-owned; only scalar operands and an optional scalar result cross
   * the ABI. An opt-in compiled LoadConstant site uses the same indexed
   * callback: the embedding reads that instance's known narrow Logic4 value
   * and returns its aval and zero bval before any container state is touched.
   */
  uint32_t (*container_operation)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint64_t input0_aval,
      uint64_t input0_bval,
      uint64_t input1_aval,
      uint64_t input1_bval,
      uint64_t* result_aval,
      uint64_t* result_bval);
  /* Version 2 procedural force/release callbacks for static packed slices. */
  void (*force_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval);
  void (*force_signal_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value);
  void (*release_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width);
  /* Version 2 elapsed ticks since the latest committed transaction. */
  uint64_t (*signal_last_active)(void* context, uint32_t signal);
  /* Version 2 current-process driver queries for VHDL signal attributes. */
  uint32_t (*signal_driving)(void* context, uint32_t signal);
  uint64_t (*signal_driving_value)(
      void* context, uint32_t signal, uint64_t* bval);
  void (*signal_driving_value_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v2* value);
  /* Version 2 current global simulation tick query. */
  uint64_t (*read_simulation_time)(void* context);
  /* Version 2 intrinsic VITAL timing-check callback. */
  uint32_t (*vital_timing_check)(
      void* context, uint32_t process, uint32_t instruction);
  /* Version 2 intrinsic VITAL path/wire-delay callback. */
  void (*vital_delay)(
      void* context, uint32_t process, uint32_t instruction);
  /*
   * Version 2 VHDL driver-value force/release callbacks. These alter only
   * the current process's driver; the resolver still combines other drivers.
   */
  void (*force_driver_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval);
  void (*force_driver_signal_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v2* value);
  void (*release_driver_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width);
  /*
   * Version 2 exact-width signal callback. The immutable SimIR operation
   * identifies its registers and scheduling metadata; frame supplies their
   * arbitrary-width word planes.
   */
  uint32_t (*execute_signal_operation)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      fsim_jit_frame_v2* frame);
  /*
   * Version 2 scalar packed-container fast paths. flags bit 0 selects a
   * pre-linearized index and bit 1 selects signed dynamic-index validation.
   * For fused ReadContainerObject/ContainerRead pairs, bits 8 and above name
   * the source-operation distance and bit 2 marks a validated single-use
   * container register. Bit 2 is an optimization hint; callbacks may ignore
   * it and must not retain any borrowed object storage beyond the call.
   * More complex container kinds continue through container_operation.
   */
  uint32_t (*container_read_word)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t container,
      uint32_t flags,
      uint64_t index_aval,
      uint64_t index_bval,
      uint64_t* result_aval,
      uint64_t* result_bval);
  uint32_t (*container_write_word)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t container,
      uint32_t flags,
      uint64_t index_aval,
      uint64_t index_bval,
      uint64_t value_aval,
      uint64_t value_bval);
  /*
   * Version 2 arbitrary-width packed-container fast paths. Flags use the
   * same bit assignments as container_read_word. The word planes contain
   * ceil(element_width / 64) little-endian words and point directly
   * into call-scoped scratch storage. The pointers must not be retained.
   * Read output modifications are copied back even on failure. Only the selected element crosses
   * the runtime boundary.
   */
  uint32_t (*container_read_packed)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t container,
      uint32_t flags,
      uint64_t index_aval,
      uint64_t index_bval,
      uint64_t* result_aval_words,
      uint64_t* result_bval_words,
      uint32_t word_count);
  uint32_t (*container_write_packed)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t container,
      uint32_t flags,
      uint64_t index_aval,
      uint64_t index_bval,
      const uint64_t* value_aval_words,
      const uint64_t* value_bval_words,
      uint32_t word_count);
  /*
   * Version 2 arbitrary-width signal-read fast path. The destination planes
   * contain ceil(width / 64) little-endian words in call-scoped scratch.
   * Pointers must not be retained; partial output writes are copied back even
   * when the callback reports failure. Logic4 callers pass null for planes
   * 2 and 3; Logic9 callers provide all four planes.
   */
  uint32_t (*read_signal_packed)(
      void* context,
      uint32_t signal,
      uint32_t width,
      uint64_t* result_aval_words,
      uint64_t* result_bval_words,
      uint64_t* result_logic9_plane2_words,
      uint64_t* result_logic9_plane3_words);
  /*
   * Version 2 arbitrary-width signal-write fast path. mode is one of the
   * FSIM_JIT_PACKED_SIGNAL_WRITE_*_V2 constants. The source planes contain
   * ceil(width / 64) little-endian words in call-scoped scratch storage.
   * These read-only pointers must not be retained after the call.
   */
  uint32_t (*write_signal_packed)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint32_t mode,
      uint64_t delay,
      const uint64_t* aval_words,
      const uint64_t* bval_words,
      const uint64_t* logic9_plane2_words,
      const uint64_t* logic9_plane3_words,
      uint32_t update_domain);
  /*
   * Version 2 fused arbitrary-width signal part read. The callback applies
   * the complete DynamicPartSelect range and unknown-base semantics and
   * returns only the selected value, never a whole source-signal snapshot.
   * flags bit 0 is increasing, bit 1 is source_descending, and bit 2 is
   * two_state.
   */
  uint32_t (*read_signal_dynamic_part)(
      void* context,
      uint32_t signal,
      uint32_t source_width,
      uint64_t base_aval,
      uint64_t base_bval,
      int64_t left,
      int64_t right,
      uint32_t base_offset,
      uint32_t width,
      uint32_t flags,
      fsim_jit_logic9_word_v2* result);
  uint32_t (*record_code_coverage_counter)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t counter);
  /*
   * Version 2 direct SimIR service callbacks. The immutable process and
   * instruction identify the operation; frame carries arbitrary-width packed
   * registers to and from the synchronous callback.
   */
  uint32_t (*sample_coverage)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      fsim_jit_frame_v2* frame);
  uint32_t (*execute_class_property_operation)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      fsim_jit_frame_v2* frame);
  uint32_t (*query_event_triggered)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      fsim_jit_frame_v2* frame);
  /*
   * Optional tail callback for whole-signal, zero-delay inertial projected
   * writes whose value has arbitrary-width packed planes. Calls always use
   * inertial mode with delay and rejection both zero. A module requires this
   * field only when its validated process metadata says so. The callback
   * schedules a projected write (including equal-value transaction metadata
   * and replacement of pending same-owner waveforms); it is not a packed
   * update. Return zero on success and nonzero on failure. The callback must
   * not unwind across the C ABI. Plane pointers are call-scoped and must not
   * be retained. Logic4 callers pass null for planes 2 and 3; Logic9 callers
   * provide both.
   */
  uint32_t (*write_projected_signal_packed)(
      void* context,
      uint32_t signal,
      uint32_t width,
      const uint64_t* aval_words,
      const uint64_t* bval_words,
      const uint64_t* logic9_plane2_words,
      const uint64_t* logic9_plane3_words);
  /*
   * Optional version 2 tail callback for packed ContainerRead operations
   * whose Logic4 index register is exactly 64 bits. It has the same result
   * plane and word_count contract as container_read_packed, but index_aval
   * and index_bval carry all 64 index bits; callbacks must not truncate the
   * high 32 bits. flags bit 0 selects a linear offset. For a fixed
   * non-linear array index, interpret the value as a signed 64-bit coordinate
   * and return the element default when it is outside the declared range. For
   * a linear fixed-array index, a negative value returns the default. For a
   * dynamic index, flags bit 1 selects signed interpretation: negative values
   * and unknown values follow the existing dynamic-index error contract,
   * while unsigned indices use the full uint64_t range. The callback is
   * required only for processes whose validated native code uses this path;
   * older service-table prefixes remain valid for processes using only the
   * existing callbacks.
   */
  uint32_t (*container_read_packed_index64)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t container,
      uint32_t flags,
      uint64_t index_aval,
      uint64_t index_bval,
      uint64_t* result_aval_words,
      uint64_t* result_bval_words,
      uint32_t word_count);
} fsim_jit_services_v2;

/* Mutable simulator-side view owned by one executor instance. */
typedef struct fsim_jit_runtime_instance_v2 {
  uint32_t abi_version;
  uint32_t struct_size;
  const fsim_jit_services_v2* services;
  void* context;
  /*
   * Version 2 execution controls. Generated O2 code tests the debug-point
   * flag before returning a source boundary; the zero default has no callback
   * or suspension overhead beyond that predictable branch.
   */
  uint32_t flags;
  uint32_t reserved;
  /*
   * Version 2 callback-free update staging.  Slots correspond to the
   * process-specific direct_update_signals frame-layout vector.  A null
   * pointer requests the ordinary callback path. A non-null pointer's count
   * must cover every layout entry. If the activity bitmap is non-null, its
   * word count must cover ceil(slot_count / 64). Wide slots must report the
   * signal width, enough words for that width, and non-null wide planes.
   */
  /* Admission requires all operations using these slots to be generic. */
  fsim_jit_update_slot_v2* direct_update_slots;
  uint32_t direct_update_slot_count;
  uint32_t direct_update_reserved;
  /*
   * Version 2 callback-free current Logic4 reads.  The process-specific
   * direct_read_signals table maps generated read slots to simulator signal
   * indices in the dense aval/bval planes.  Null pointers request callbacks.
   */
  const uint64_t* direct_signal_aval;
  const uint64_t* direct_signal_bval;
  const uint32_t* direct_read_signals;
  uint32_t direct_read_signal_count;
  uint32_t direct_signal_count;
  uint32_t direct_signal_reserved;
  /*
   * Version 2 flattened current Logic4 planes for arbitrary-width native
   * reads. direct_wide_signal_offsets maps simulator signal indices to word
   * offsets; direct_wide_word_count bounds both planes. Null pointers request
   * the read_signal_packed callback.
   */
  const uint64_t* direct_wide_signal_aval;
  const uint64_t* direct_wide_signal_bval;
  const uint32_t* direct_wide_signal_offsets;
  uint32_t direct_wide_signal_offset_count;
  uint32_t direct_wide_word_count;
  /*
   * Version 2 sparse direct-update activity bitmap. Generated code sets one
   * bit for every slot written during a resume. The host consumes set bits in
   * ascending slot order, then clears the bitmap. A null pointer retains the
   * original full-slot scan.
   */
  uint64_t* direct_update_active_words;
  uint32_t direct_update_active_word_count;
  uint32_t direct_update_active_reserved;
  /*
   * Version 2 exact static-sensitivity activation mask. Bit 63 requests a
   * conservative full activation; bits 0..62 correspond to the process's
   * canonical static-sensitivity order.
   */
  uint64_t static_trigger_mask;
  /*
   * Version 2 exact high ordinal planes for arbitrary-width Logic9 reads.
   * These use the same flattened offsets and word count as the existing
   * direct_wide_signal_aval/bval planes. Null pointers request callbacks.
   */
  const uint64_t* direct_wide_signal_logic9_plane2;
  const uint64_t* direct_wide_signal_logic9_plane3;
  /*
   * Version 2 dense exact Logic9 planes for callback-free reads of signals
   * no wider than one word. Entries for non-Logic9 signals are zero.
   */
  const uint64_t* direct_signal_logic9_plane0;
  const uint64_t* direct_signal_logic9_plane1;
  const uint64_t* direct_signal_logic9_plane2;
  const uint64_t* direct_signal_logic9_plane3;
  /*
   * Version 2 callback-free code-coverage counters. The hit map is owned by
   * the process executor so structurally shared native code can retain exact
   * per-instance counter identities. Generated code calls the checked service
   * only when a counter is saturated or direct storage is unavailable.
   */
  const uint32_t* code_coverage_hit_counters;
  uint64_t* code_coverage_counter_values;
  uint32_t code_coverage_hit_count;
  uint32_t code_coverage_counter_count;
  /*
   * Version 2 activation mask for a graph-certified fused process. Bit n
   * selects canonical member n. This is separate from the per-output
   * direct_update_active_words bitmap. A masked entry checks struct_size and
   * fused_activation_word_count before reading any word; an ordinary process
   * leaves both fields zero.
   */
  const uint64_t* fused_activation_words;
  uint32_t fused_activation_word_count;
  uint32_t fused_activation_reserved;
} fsim_jit_runtime_instance_v2;

/* Caller-owned persistent process frame; layout semantics mirror v1. */
struct fsim_jit_frame_v2 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t layout_id_low;
  uint64_t layout_id_high;
  uint32_t register_count;
  uint32_t program_counter;
  uint32_t state;
  uint32_t last_instruction;
  uint64_t* register_aval;
  uint64_t* register_bval;
  uint8_t* register_initialized;
  uint64_t* register_logic9_plane2;
  uint64_t* register_logic9_plane3;
  uint32_t native_call_depth;
  uint32_t native_call_reserved;
  uint32_t native_return_stack[FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V2];
};

typedef struct fsim_jit_resume_result_v2 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t status;
  uint32_t instruction;
  uint64_t delay;
} fsim_jit_resume_result_v2;

typedef uint32_t fsim_jit_process_v2(
    const fsim_jit_runtime_instance_v2* instance,
    fsim_jit_frame_v2* frame,
    fsim_jit_resume_result_v2* result);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FSIM_COMPILER_JIT_RUNTIME_V2_H */
