/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FSIM_COMPILER_JIT_RUNTIME_H
#define FSIM_COMPILER_JIT_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_JIT_RUNTIME_ABI_VERSION_V1 UINT32_C(1)
#define FSIM_JIT_FRAME_ABI_VERSION_V1 UINT32_C(1)
#define FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1 UINT32_C(1)

#define FSIM_JIT_FRAME_STATE_READY UINT32_C(0)
#define FSIM_JIT_FRAME_STATE_COMPLETED UINT32_C(1)
#define FSIM_JIT_FRAME_STATE_STOPPED UINT32_C(2)
#define FSIM_JIT_FRAME_STATE_ASSERTION_FAILED UINT32_C(3)
#define FSIM_JIT_FRAME_STATE_RUNTIME_ERROR UINT32_C(4)

#define FSIM_JIT_RESUME_STATUS_COMPLETED UINT32_C(0)
#define FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED UINT32_C(1)
#define FSIM_JIT_RESUME_STATUS_WAIT_FOR UINT32_C(2)
#define FSIM_JIT_RESUME_STATUS_YIELDED UINT32_C(3)
#define FSIM_JIT_RESUME_STATUS_STOPPED UINT32_C(4)
#define FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR UINT32_C(5)
#define FSIM_JIT_RESUME_STATUS_WAIT_ON UINT32_C(6)
#define FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY UINT32_C(7)
#define FSIM_JIT_RESUME_STATUS_DEBUG_POINT UINT32_C(8)
#define FSIM_JIT_RESUME_STATUS_WAIT_FOREVER UINT32_C(9)
#define FSIM_JIT_RESUME_STATUS_PAUSED UINT32_C(10)

#define FSIM_JIT_INVALID_INSTRUCTION UINT32_MAX
#define FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS UINT32_C(1)
#define FSIM_JIT_PROJECTED_TRANSPORT UINT32_C(0)
#define FSIM_JIT_PROJECTED_INERTIAL UINT32_C(1)

typedef struct fsim_jit_projected_element_v1 {
  uint64_t aval;
  uint64_t bval;
  uint64_t delay;
} fsim_jit_projected_element_v1;

/* Four ordinal bit planes carrying up to 64 IEEE std_logic values. */
typedef struct fsim_jit_logic9_word_v1 {
  uint64_t planes[4];
} fsim_jit_logic9_word_v1;

typedef struct fsim_jit_logic9_projected_element_v1 {
  fsim_jit_logic9_word_v1 value;
  uint64_t delay;
} fsim_jit_logic9_projected_element_v1;

/*
 * Versioned plain-C boundary used by generated process functions.
 * Signal values use aval/bval encoding in the low bits selected by the
 * elaborated signal width. Generated code initializes *bval to zero before
 * read_signal, so a two-state callback may leave it unchanged. Callbacks must
 * not unwind across this boundary.
 */
typedef struct fsim_jit_runtime_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  void* context;

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
   * Append-only v1 extension for writes committed by the simulator kernel.
   * Callers advertise availability through struct_size. Existing fields above
   * retain their original offsets.
   */
  void (*write_update)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval);
  void (*write_after)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay);

  /*
   * Append-only v1 execution controls. Generated O2 code tests the debug-point
   * flag before returning a source boundary; the zero default has no callback
   * or suspension overhead beyond that predictable branch.
   */
  uint32_t flags;
  uint32_t reserved;

  /*
   * Append-only v1 extension for packed partial writes. offset and width
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
      uint64_t bval);
  void (*write_after_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint64_t delay);

  /* Append-only delta-scoped signal event query. Returns zero or one. */
  uint32_t (*signal_event)(void* context, uint32_t signal);

  /*
   * Append-only effective value immediately before the latest signal event.
   * Uses the same aval/bval representation as read_signal.
   */
  uint64_t (*signal_last_value)(
      void* context, uint32_t signal, uint64_t* bval);

  /* Append-only elapsed global-resolution ticks since the latest event. */
  uint64_t (*signal_last_event)(void* context, uint32_t signal);

  /* Append-only delta-scoped signal transaction query. Returns zero or one. */
  uint32_t (*signal_active)(void* context, uint32_t signal);

  /*
   * Append-only synchronous language-output callback. text is valid only for
   * the duration of the call; newline is zero or one.
   */
  void (*write_output)(
      void* context,
      uint32_t process,
      const char* text,
      uint64_t text_size,
      uint32_t newline);

  /*
   * Append-only postponed language-output callback. The callback copies or
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
   * Append-only nonfatal report callback. instruction identifies immutable
   * SimIR report metadata owned by the embedding process descriptor.
   */
  void (*write_report)(
      void* context,
      uint32_t process,
      uint32_t instruction);

  /*
   * Append-only runtime-value formatting callback. instruction identifies
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
   * Append-only current-time output callback. instruction identifies
   * immutable TimeDisplay metadata owned by the embedding process.
   */
  void (*write_time)(
      void* context,
      uint32_t process,
      uint32_t instruction);

  /* Append-only monitor registration and on/off callbacks. */
  void (*install_monitor)(
      void* context,
      uint32_t process,
      uint32_t instruction);
  void (*control_monitor)(
      void* context,
      uint32_t process,
      uint32_t instruction);

  /*
   * Append-only deterministic random-value callback. Bound values and the
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
   * Append-only transition-specific inertial writes. A later invocation for
   * the same generated continuous driver supersedes its pending update.
   */
  void (*write_inertial)(
      void* context,
      uint32_t signal,
      uint64_t aval,
      uint64_t bval,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay);
  void (*write_inertial_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      uint64_t aval,
      uint64_t bval,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay);

  /*
   * Append-only VHDL projected-output-waveform writes. mode is one of the
   * FSIM_JIT_PROJECTED_* constants; rejection is ignored in transport mode.
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
   * Append-only atomic multi-element waveform writes. elements is valid only
   * for the duration of the synchronous callback and contains count entries.
   */
  void (*write_projected_waveform)(
      void* context,
      uint32_t signal,
      uint32_t width,
      const fsim_jit_projected_element_v1* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_projected_element_v1* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);

  /*
   * Append-only exact Logic9 extension. Values are passed by pointer so the
   * ABI does not depend on platform-specific aggregate calling conventions.
   */
  void (*read_signal_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v1* value);
  void (*write_signal_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v1* value);
  void (*write_update_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v1* value);
  void (*write_after_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      uint64_t delay);
  void (*write_signal_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value);
  void (*write_update_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value);
  void (*write_after_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      uint64_t delay);
  void (*signal_last_value_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v1* value);
  void (*write_inertial_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay);
  void (*write_inertial_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      uint64_t rise_delay,
      uint64_t fall_delay,
      uint64_t turnoff_delay);
  void (*write_projected_logic9)(
      void* context,
      uint32_t signal,
      const fsim_jit_logic9_word_v1* value,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value,
      uint64_t delay,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_logic9)(
      void* context,
      uint32_t signal,
      uint32_t width,
      const fsim_jit_logic9_projected_element_v1* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_projected_waveform_slice_logic9)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width,
      const fsim_jit_logic9_projected_element_v1* elements,
      uint32_t count,
      uint64_t rejection,
      uint32_t mode);
  void (*write_formatted_logic9)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t width,
      const fsim_jit_logic9_word_v1* value);

  /*
   * Append-only mutable-string helpers. Generated code passes only stable
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
} fsim_jit_runtime_v1;

/*
 * Caller-owned persistent process frame. register_aval and register_bval each
 * point to register_count uint64_t elements and register_initialized points to
 * register_count bytes supplied by the caller. layout_id is process-specific
 * and must come from the adapter's frame-layout query.
 */
typedef struct fsim_jit_frame_v1 {
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
} fsim_jit_frame_v1;

/*
 * Caller-owned result for one invocation. status mirrors the generated
 * function's return value. delay is meaningful only for WAIT_FOR. WAIT_ON,
 * WAIT_SENSITIVITY, WAIT_FOREVER, and DEBUG_POINT identify their immutable
 * SimIR boundary through instruction.
 */
typedef struct fsim_jit_resume_result_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t status;
  uint32_t instruction;
  uint64_t delay;
} fsim_jit_resume_result_v1;

typedef uint32_t fsim_jit_process_v1(
    const fsim_jit_runtime_v1* runtime,
    fsim_jit_frame_v1* frame,
    fsim_jit_resume_result_v1* result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FSIM_COMPILER_JIT_RUNTIME_H */
