/* SPDX-License-Identifier: Apache-2.0 */
#include "fsim/compiler/jit_runtime.h"

#include <stddef.h>

_Static_assert(FSIM_JIT_RUNTIME_ABI_VERSION_V1 == UINT32_C(1),
               "unexpected JIT runtime ABI version");

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

int main(void) {
  fsim_jit_runtime_v1 runtime = {
      FSIM_JIT_RUNTIME_ABI_VERSION_V1,
      (uint32_t)sizeof(fsim_jit_runtime_v1),
      NULL,
      read_signal,
      write_signal,
      assert_failed};
  uint64_t bval = UINT64_MAX;
  const uint64_t aval = runtime.read_signal(runtime.context, 0, &bval);
  runtime.write_signal(runtime.context, 0, aval, bval);
  runtime.assert_failed(runtime.context, 0, 0, NULL, 0);

  if (runtime.abi_version != UINT32_C(1) ||
      runtime.struct_size != sizeof(fsim_jit_runtime_v1)) {
    return 1;
  }
  return aval == UINT64_C(1) && bval == UINT64_C(0) ? 0 : 2;
}
