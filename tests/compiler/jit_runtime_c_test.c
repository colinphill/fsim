/* SPDX-License-Identifier: Apache-2.0 */
#include "fsim/compiler/jit_runtime.h"

#include <stddef.h>

_Static_assert(FSIM_JIT_RUNTIME_ABI_VERSION_V1 == UINT32_C(1),
               "unexpected JIT runtime ABI version");
_Static_assert(FSIM_JIT_RESUME_STATUS_COMPLETED == UINT32_C(0),
               "completed resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED == UINT32_C(1),
               "assertion resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_WAIT_FOR == UINT32_C(2),
               "WaitFor resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_YIELDED == UINT32_C(3),
               "yield resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_STOPPED == UINT32_C(4),
               "stop resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR == UINT32_C(5),
               "runtime-error resume status changed");
_Static_assert(FSIM_JIT_RESUME_STATUS_WAIT_ON == UINT32_C(6),
               "WaitOn resume status was not appended");
_Static_assert(FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY == UINT32_C(7),
               "WaitSensitivity resume status was not appended");
_Static_assert(FSIM_JIT_RESUME_STATUS_DEBUG_POINT == UINT32_C(8),
               "debug-point resume status was not appended");
_Static_assert(FSIM_JIT_RESUME_STATUS_WAIT_FOREVER == UINT32_C(9),
               "permanent-wait resume status was not appended");
_Static_assert(FSIM_JIT_RESUME_STATUS_PAUSED == UINT32_C(10),
               "language pause resume status was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, abi_version) == 0,
               "runtime ABI version offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, struct_size) == 4,
               "runtime structure size offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, context) == 8,
               "runtime context offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, read_signal) == 16,
               "runtime read callback offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_signal) == 24,
               "runtime blocking-write callback offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, assert_failed) == 32,
               "runtime assertion callback offset changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_update) == 40,
               "runtime update callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_after) == 48,
               "runtime delayed-write callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, flags) == 56,
               "runtime execution flags were not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, reserved) == 60,
               "runtime reserved flags tail changed");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_signal_slice) == 64,
               "runtime partial blocking-write callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_update_slice) == 72,
               "runtime partial update callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_after_slice) == 80,
               "runtime delayed partial-write callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, signal_event) == 88,
               "runtime signal-event callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, signal_last_value) == 96,
               "runtime signal-last-value callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, signal_last_event) == 104,
               "runtime signal-last-event callback was not appended");
_Static_assert(sizeof(fsim_jit_runtime_v1) == 112,
               "unexpected extended runtime ABI size");

typedef struct callback_state {
  uint32_t update_count;
  uint32_t after_count;
  uint32_t update_signal;
  uint64_t update_aval;
  uint64_t update_bval;
  uint32_t after_signal;
  uint64_t after_aval;
  uint64_t after_bval;
  uint64_t after_delay;
  uint32_t slice_count;
  uint32_t slice_signal;
  uint32_t slice_offset;
  uint32_t slice_width;
  uint64_t slice_aval;
  uint64_t slice_bval;
  uint64_t slice_delay;
} callback_state;

static uint64_t read_signal(
    void* context, uint32_t signal, uint64_t* bval) {
  (void)context;
  (void)signal;
  *bval = 0;
  return 1;
}

static void write_signal(
    void* context,
    uint32_t signal,
    uint64_t aval,
    uint64_t bval) {
  (void)context;
  (void)signal;
  (void)aval;
  (void)bval;
}

static void assert_failed(
    void* context,
    uint32_t process,
    uint32_t instruction,
    const char* message,
    uint64_t message_size) {
  (void)context;
  (void)process;
  (void)instruction;
  (void)message;
  (void)message_size;
}

static void write_update(
    void* context,
    uint32_t signal,
    uint64_t aval,
    uint64_t bval) {
  callback_state* state = (callback_state*)context;
  ++state->update_count;
  state->update_signal = signal;
  state->update_aval = aval;
  state->update_bval = bval;
}

static void write_after(
    void* context,
    uint32_t signal,
    uint64_t aval,
    uint64_t bval,
    uint64_t delay) {
  callback_state* state = (callback_state*)context;
  ++state->after_count;
  state->after_signal = signal;
  state->after_aval = aval;
  state->after_bval = bval;
  state->after_delay = delay;
}

static void write_slice(
    void* context,
    uint32_t signal,
    uint32_t offset,
    uint32_t width,
    uint64_t aval,
    uint64_t bval) {
  callback_state* state = (callback_state*)context;
  ++state->slice_count;
  state->slice_signal = signal;
  state->slice_offset = offset;
  state->slice_width = width;
  state->slice_aval = aval;
  state->slice_bval = bval;
}

static void write_after_slice(
    void* context,
    uint32_t signal,
    uint32_t offset,
    uint32_t width,
    uint64_t aval,
    uint64_t bval,
    uint64_t delay) {
  write_slice(context, signal, offset, width, aval, bval);
  ((callback_state*)context)->slice_delay = delay;
}

static uint32_t signal_event(void* context, uint32_t signal) {
  (void)context;
  return signal == UINT32_C(9);
}

static uint64_t signal_last_value(
    void* context, uint32_t signal, uint64_t* bval) {
  (void)context;
  *bval = UINT64_C(0x80);
  return UINT64_C(0xa5) + signal;
}

static uint64_t signal_last_event(void* context, uint32_t signal) {
  (void)context;
  return UINT64_C(1000) + signal;
}

int main(void) {
  callback_state state = {0};
  fsim_jit_runtime_v1 runtime = {
      FSIM_JIT_RUNTIME_ABI_VERSION_V1,
      (uint32_t)sizeof(fsim_jit_runtime_v1),
      &state,
      read_signal,
      write_signal,
      assert_failed,
      write_update,
      write_after,
      0,
      0,
      write_slice,
      write_slice,
      write_after_slice,
      signal_event,
      signal_last_value,
      signal_last_event};
  uint64_t bval = UINT64_MAX;
  const uint64_t aval = runtime.read_signal(runtime.context, 0, &bval);
  runtime.write_signal(runtime.context, 0, aval, bval);
  runtime.assert_failed(runtime.context, 0, 0, NULL, 0);
  runtime.write_update(
      runtime.context, UINT32_C(3), UINT64_C(0x35), UINT64_C(0x01));
  runtime.write_after(
      runtime.context, UINT32_C(5), UINT64_C(0xa5), UINT64_C(0x80),
      UINT64_MAX);
  runtime.write_signal_slice(
      runtime.context, UINT32_C(6), UINT32_C(7), UINT32_C(8),
      UINT64_C(0x5a), UINT64_C(0x24));
  runtime.write_update_slice(
      runtime.context, UINT32_C(7), UINT32_C(9), UINT32_C(10),
      UINT64_C(0x155), UINT64_C(0x080));
  runtime.write_after_slice(
      runtime.context, UINT32_C(8), UINT32_C(11), UINT32_C(12),
      UINT64_C(0xabc), UINT64_C(0x400), UINT64_C(13));
  const uint32_t event_active =
      runtime.signal_event(runtime.context, UINT32_C(9));
  uint64_t last_bval = 0;
  const uint64_t last_aval = runtime.signal_last_value(
      runtime.context, UINT32_C(2), &last_bval);
  const uint64_t last_event = runtime.signal_last_event(
      runtime.context, UINT32_C(7));

  if (runtime.abi_version != UINT32_C(1) ||
      runtime.struct_size != sizeof(fsim_jit_runtime_v1)) {
    return 1;
  }
  if (aval != UINT64_C(1) || bval != UINT64_C(0)) {
    return 2;
  }
  if (event_active != UINT32_C(1)) {
    return 5;
  }
  if (last_aval != UINT64_C(0xa7)
      || last_bval != UINT64_C(0x80)) {
    return 6;
  }
  if (last_event != UINT64_C(1007)) {
    return 7;
  }
  if (state.update_count != UINT32_C(1) ||
      state.after_count != UINT32_C(1) ||
      state.update_signal != UINT32_C(3) ||
      state.update_aval != UINT64_C(0x35) ||
      state.update_bval != UINT64_C(0x01) ||
      state.after_signal != UINT32_C(5) ||
      state.after_aval != UINT64_C(0xa5) ||
      state.after_bval != UINT64_C(0x80) ||
      state.after_delay != UINT64_MAX ||
      state.slice_count != UINT32_C(3) ||
      state.slice_signal != UINT32_C(8) ||
      state.slice_offset != UINT32_C(11) ||
      state.slice_width != UINT32_C(12) ||
      state.slice_aval != UINT64_C(0xabc) ||
      state.slice_bval != UINT64_C(0x400) ||
      state.slice_delay != UINT64_C(13)) {
    return 3;
  }
  return 0;
}
