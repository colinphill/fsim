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
#define FSIM_JIT_RESUME_STATUS_FORK UINT32_C(11)
#define FSIM_JIT_RESUME_STATUS_FORK_END UINT32_C(12)
#define FSIM_JIT_RESUME_STATUS_WAIT_FORK UINT32_C(13)
#define FSIM_JIT_RESUME_STATUS_DISABLE_FORK UINT32_C(14)
/* Simulation-owned SimIR service operation at result.instruction. */
#define FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY UINT32_C(15)

#define FSIM_JIT_INVALID_INSTRUCTION UINT32_MAX
#define FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1 UINT32_C(64)
#define FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS UINT32_C(1)
#define FSIM_JIT_PROJECTED_TRANSPORT UINT32_C(0)
#define FSIM_JIT_PROJECTED_INERTIAL UINT32_C(1)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING UINT32_C(0)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE UINT32_C(1)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER UINT32_C(2)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE UINT32_C(3)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE UINT32_C(4)
#define FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE UINT32_C(5)

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
 * One callback-free, zero-delay packed update accumulator owned by the host.
 * Generated code merges all writes to one signal during a native resume into
 * this slot.  mask selects the bits written at least once; later overlapping
 * writes replace earlier bits, preserving source order without one C callback
 * per assignment.  The host clears active/mask before each resume and commits
 * the accumulated patches at the suspension boundary. reserved bit 0 marks
 * aval/bval as a persistent shadow of a proven single-driver signal; generated
 * code may then leave an identical assignment inactive while retaining any
 * earlier active patch in the slot.
 */
typedef struct fsim_jit_update_slot_v1 {
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
} fsim_jit_update_slot_v1;

typedef struct fsim_jit_frame_v1 fsim_jit_frame_v1;

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
   * Append-only report callback. instruction identifies immutable SimIR
   * report metadata, including any evaluated string/severity register IDs,
   * owned by the embedding process descriptor.
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
  uint32_t (*string_replace_code_point)(
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
   * Append-only bounded text-file helpers. Each instruction's immutable
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
   * Append-only bounded-container helper. instruction identifies immutable
   * SimIR operation metadata. Dynamic storage and container registers remain
   * embedding-owned; only scalar operands and an optional scalar result cross
   * the ABI.
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

  /* Append-only procedural force/release callbacks for static packed slices. */
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
      const fsim_jit_logic9_word_v1* value);
  void (*release_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width);

  /* Append-only elapsed ticks since the latest committed transaction. */
  uint64_t (*signal_last_active)(void* context, uint32_t signal);

  /* Append-only current-process driver queries for VHDL signal attributes. */
  uint32_t (*signal_driving)(void* context, uint32_t signal);
  uint64_t (*signal_driving_value)(
      void* context, uint32_t signal, uint64_t* bval);
  void (*signal_driving_value_logic9)(
      void* context,
      uint32_t signal,
      fsim_jit_logic9_word_v1* value);

  /* Append-only current global simulation tick query. */
  uint64_t (*read_simulation_time)(void* context);

  /* Append-only intrinsic VITAL timing-check callback. */
  uint32_t (*vital_timing_check)(
      void* context, uint32_t process, uint32_t instruction);

  /* Append-only intrinsic VITAL path/wire-delay callback. */
  void (*vital_delay)(
      void* context, uint32_t process, uint32_t instruction);

  /*
   * Append-only VHDL driver-value force/release callbacks. These alter only
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
      const fsim_jit_logic9_word_v1* value);
  void (*release_driver_signal_slice)(
      void* context,
      uint32_t signal,
      uint32_t offset,
      uint32_t width);

  /*
   * Append-only exact-width signal callback. The immutable SimIR operation
   * identifies its registers and scheduling metadata; frame supplies their
   * arbitrary-width word planes.
   */
  uint32_t (*execute_signal_operation)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      fsim_jit_frame_v1* frame);

  /*
   * Append-only scalar packed-container fast paths. flags bit 0 selects a
   * pre-linearized index and bit 1 selects signed dynamic-index validation.
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
   * Append-only arbitrary-width packed-container fast paths. The word planes
   * contain ceil(element_width / 64) little-endian words and point directly
   * into the caller-owned process frame. Only the selected element crosses
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
   * Append-only arbitrary-width signal-read fast path. The destination planes
   * point directly into the generated process frame and contain
   * ceil(width / 64) little-endian words. Logic4 callers pass null for planes
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
   * Append-only arbitrary-width signal-write fast path. mode is one of the
   * FSIM_JIT_PACKED_SIGNAL_WRITE_* constants. The source planes contain
   * ceil(width / 64) little-endian words and point into the process frame.
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
      const uint64_t* logic9_plane3_words);

  /*
   * Append-only callback-free update staging.  Slots correspond to the
   * process-specific direct_update_signals frame-layout vector.  A null
   * pointer requests the ordinary callback path.
   */
  fsim_jit_update_slot_v1* direct_update_slots;
  uint32_t direct_update_slot_count;
  uint32_t direct_update_reserved;

  /*
   * Append-only callback-free current Logic4 reads.  The process-specific
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
   * Append-only flattened current Logic4 planes for arbitrary-width native
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
   * Append-only sparse direct-update activity bitmap. Generated code sets one
   * bit for every slot written during a resume. The host consumes set bits in
   * ascending slot order, then clears the bitmap. A null pointer retains the
   * original full-slot scan.
   */
  uint64_t* direct_update_active_words;
  uint32_t direct_update_active_word_count;
  uint32_t direct_update_active_reserved;

  /*
   * Append-only exact static-sensitivity activation mask. Bit 63 requests a
   * conservative full activation; bits 0..62 correspond to the process's
   * canonical static-sensitivity order.
   */
  uint64_t static_trigger_mask;

  /*
   * Append-only fused arbitrary-width signal part read. The callback applies
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
      fsim_jit_logic9_word_v1* result);

  /*
   * Append-only exact high ordinal planes for arbitrary-width Logic9 reads.
   * These use the same flattened offsets and word count as the existing
   * direct_wide_signal_aval/bval planes. Null pointers request callbacks.
   */
  const uint64_t* direct_wide_signal_logic9_plane2;
  const uint64_t* direct_wide_signal_logic9_plane3;

  /*
   * Append-only dense exact Logic9 planes for callback-free reads of signals
   * no wider than one word. Entries for non-Logic9 signals are zero.
   */
  const uint64_t* direct_signal_logic9_plane0;
  const uint64_t* direct_signal_logic9_plane1;
  const uint64_t* direct_signal_logic9_plane2;
  const uint64_t* direct_signal_logic9_plane3;

  /*
   * Append-only callback-free code-coverage counters. The hit map is owned by
   * the process executor so structurally shared native code can retain exact
   * per-instance counter identities. Generated code calls the checked service
   * only when a counter is saturated or direct storage is unavailable.
   */
  const uint32_t* code_coverage_hit_counters;
  uint64_t* code_coverage_counter_values;
  uint32_t code_coverage_hit_count;
  uint32_t code_coverage_counter_count;
  uint32_t (*record_code_coverage_counter)(
      void* context,
      uint32_t process,
      uint32_t instruction,
      uint32_t counter);

} fsim_jit_runtime_v1;

/*
 * Caller-owned persistent process frame. register_aval and register_bval each
 * point to the flattened uint64_t word plane described by the adapter's
 * process-specific frame-layout query. register_initialized points to
 * register_count bytes supplied by the caller. layout_id must come from that
 * same frame-layout query.
 */
struct fsim_jit_frame_v1 {
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
    uint32_t native_return_stack[FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1];
};

/*
 * Caller-owned result for one invocation. status mirrors the generated
 * function's return value. delay is meaningful only for WAIT_FOR. Every wait,
 * debug, fork, and process-control status identifies its immutable SimIR
 * boundary through instruction.
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
