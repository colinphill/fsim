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

#define FSIM_JIT_INVALID_INSTRUCTION UINT32_MAX
#define FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS UINT32_C(1)

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
} fsim_jit_runtime_v1;

/*
 * Caller-owned persistent process frame. register_aval and register_bval each
 * point to register_count uint64_t elements supplied by the caller. layout_id
 * is process-specific and must come from the adapter's frame-layout query.
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
} fsim_jit_frame_v1;

/*
 * Caller-owned result for one invocation. status mirrors the generated
 * function's return value. delay is meaningful only for WAIT_FOR. WAIT_ON and
 * WAIT_SENSITIVITY identify their immutable SimIR operands through instruction.
 * DEBUG_POINT identifies its immutable source boundary the same way.
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
