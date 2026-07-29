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
_Static_assert(offsetof(fsim_jit_runtime_v1, signal_active) == 112,
               "runtime signal-active callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_output) == 120,
               "runtime language-output callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, schedule_output) == 128,
               "runtime postponed-output callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_report) == 136,
               "runtime report callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_formatted) == 144,
               "runtime formatted-output callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_time) == 152,
               "runtime time-output callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, install_monitor) == 160,
               "runtime monitor-install callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, control_monitor) == 168,
               "runtime monitor-control callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, random_value) == 176,
               "runtime random-value callback was not appended");
_Static_assert(offsetof(fsim_jit_runtime_v1, write_inertial) == 184,
               "runtime inertial-write callback was not appended");
_Static_assert(
    offsetof(fsim_jit_runtime_v1, write_inertial_slice) == 192,
    "runtime partial inertial-write callback was not appended");
_Static_assert(sizeof(fsim_jit_runtime_v1) == 200,
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
  uint32_t output_count;
  uint32_t output_process;
  uint32_t output_newline;
  uint64_t output_size;
  uint32_t scheduled_output_count;
  uint32_t report_count;
  uint32_t report_instruction;
  uint32_t formatted_count;
  uint32_t formatted_width;
  uint64_t formatted_aval;
  uint64_t formatted_bval;
  uint32_t time_count;
  uint32_t monitor_install_count;
  uint32_t monitor_control_count;
  uint32_t random_count;
  uint32_t inertial_count;
  uint64_t inertial_rise;
  uint64_t inertial_fall;
  uint64_t inertial_turnoff;
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

static uint32_t signal_active(void* context, uint32_t signal) {
  (void)context;
  return signal == UINT32_C(11);
}

static void write_output(
    void* context,
    uint32_t process,
    const char* text,
    uint64_t text_size,
    uint32_t newline) {
  callback_state* state = (callback_state*)context;
  ++state->output_count;
  state->output_process = process;
  state->output_newline = newline;
  state->output_size = text_size;
  if (text_size != UINT64_C(5)
      || text == NULL
      || text[0] != 'h'
      || text[4] != 'o') {
    state->output_size = 0;
  }
}

static void schedule_output(
    void* context,
    uint32_t process,
    const char* text,
    uint64_t text_size,
    uint32_t newline) {
  callback_state* state = (callback_state*)context;
  ++state->scheduled_output_count;
  write_output(context, process, text, text_size, newline);
}

static void write_report(
    void* context,
    uint32_t process,
    uint32_t instruction) {
  callback_state* state = (callback_state*)context;
  ++state->report_count;
  state->output_process = process;
  state->report_instruction = instruction;
}

static void write_formatted(
    void* context,
    uint32_t process,
    uint32_t instruction,
    uint32_t width,
    uint64_t aval,
    uint64_t bval) {
  callback_state* state = (callback_state*)context;
  ++state->formatted_count;
  state->output_process = process;
  state->report_instruction = instruction;
  state->formatted_width = width;
  state->formatted_aval = aval;
  state->formatted_bval = bval;
}

static void write_time(
    void* context,
    uint32_t process,
    uint32_t instruction) {
  callback_state* state = (callback_state*)context;
  ++state->time_count;
  state->output_process = process;
  state->report_instruction = instruction;
}

static void install_monitor(
    void* context,
    uint32_t process,
    uint32_t instruction) {
  callback_state* state = (callback_state*)context;
  ++state->monitor_install_count;
  state->output_process = process;
  state->report_instruction = instruction;
}

static void control_monitor(
    void* context,
    uint32_t process,
    uint32_t instruction) {
  callback_state* state = (callback_state*)context;
  ++state->monitor_control_count;
  state->output_process = process;
  state->report_instruction = instruction;
}

static uint64_t random_value(
    void* context,
    uint32_t process,
    uint32_t instruction,
    uint64_t maximum_aval,
    uint64_t maximum_bval,
    uint64_t minimum_aval,
    uint64_t minimum_bval,
    uint64_t* result_bval) {
  callback_state* state = (callback_state*)context;
  ++state->random_count;
  state->output_process = process;
  state->report_instruction = instruction;
  (void)maximum_aval;
  (void)maximum_bval;
  (void)minimum_aval;
  (void)minimum_bval;
  *result_bval = 0;
  return UINT64_C(0x12345678);
}

static void write_inertial(
    void* context,
    uint32_t signal,
    uint64_t aval,
    uint64_t bval,
    uint64_t rise_delay,
    uint64_t fall_delay,
    uint64_t turnoff_delay) {
  callback_state* state = (callback_state*)context;
  ++state->inertial_count;
  (void)signal;
  (void)aval;
  (void)bval;
  state->inertial_rise = rise_delay;
  state->inertial_fall = fall_delay;
  state->inertial_turnoff = turnoff_delay;
}

static void write_inertial_slice(
    void* context,
    uint32_t signal,
    uint32_t offset,
    uint32_t width,
    uint64_t aval,
    uint64_t bval,
    uint64_t rise_delay,
    uint64_t fall_delay,
    uint64_t turnoff_delay) {
  (void)offset;
  (void)width;
  write_inertial(
      context,
      signal,
      aval,
      bval,
      rise_delay,
      fall_delay,
      turnoff_delay);
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
      signal_last_event,
      signal_active,
      write_output,
      schedule_output,
      write_report,
      write_formatted,
      write_time,
      install_monitor,
      control_monitor,
      random_value,
      write_inertial,
      write_inertial_slice};
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
  const uint32_t active = runtime.signal_active(
      runtime.context, UINT32_C(11));
  runtime.write_output(
      runtime.context, UINT32_C(12), "hello", UINT64_C(5), UINT32_C(1));
  runtime.schedule_output(
      runtime.context, UINT32_C(12), "hello", UINT64_C(5), UINT32_C(1));
  runtime.write_report(
      runtime.context, UINT32_C(12), UINT32_C(19));
  runtime.write_formatted(
      runtime.context,
      UINT32_C(12),
      UINT32_C(20),
      UINT32_C(8),
      UINT64_C(0xa5),
      UINT64_C(0x81));
  runtime.write_time(
      runtime.context, UINT32_C(12), UINT32_C(21));
  runtime.install_monitor(
      runtime.context, UINT32_C(12), UINT32_C(22));
  runtime.control_monitor(
      runtime.context, UINT32_C(12), UINT32_C(23));
  uint64_t random_bval = UINT64_MAX;
  const uint64_t random_aval = runtime.random_value(
      runtime.context,
      UINT32_C(12),
      UINT32_C(24),
      UINT64_C(9),
      UINT64_C(1),
      UINT64_C(3),
      UINT64_C(0),
      &random_bval);
  runtime.write_inertial(
      runtime.context,
      UINT32_C(13),
      UINT64_C(1),
      UINT64_C(0),
      UINT64_C(2),
      UINT64_C(3),
      UINT64_C(4));
  runtime.write_inertial_slice(
      runtime.context,
      UINT32_C(14),
      UINT32_C(2),
      UINT32_C(1),
      UINT64_C(0),
      UINT64_C(0),
      UINT64_C(5),
      UINT64_C(6),
      UINT64_C(7));

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
  if (active != UINT32_C(1)) {
    return 8;
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
      state.slice_delay != UINT64_C(13) ||
      state.output_count != UINT32_C(2) ||
      state.scheduled_output_count != UINT32_C(1) ||
      state.report_count != UINT32_C(1) ||
      state.formatted_count != UINT32_C(1) ||
      state.formatted_width != UINT32_C(8) ||
      state.formatted_aval != UINT64_C(0xa5) ||
      state.formatted_bval != UINT64_C(0x81) ||
      state.time_count != UINT32_C(1) ||
      state.monitor_install_count != UINT32_C(1) ||
      state.monitor_control_count != UINT32_C(1) ||
      state.random_count != UINT32_C(1) ||
      state.inertial_count != UINT32_C(2) ||
      state.inertial_rise != UINT64_C(5) ||
      state.inertial_fall != UINT64_C(6) ||
      state.inertial_turnoff != UINT64_C(7) ||
      state.report_instruction != UINT32_C(24) ||
      random_aval != UINT64_C(0x12345678) ||
      random_bval != UINT64_C(0) ||
      state.output_process != UINT32_C(12) ||
      state.output_newline != UINT32_C(1) ||
      state.output_size != UINT64_C(5)) {
    return 3;
  }
  return 0;
}
