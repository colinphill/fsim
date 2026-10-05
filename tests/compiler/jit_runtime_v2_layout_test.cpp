/* SPDX-License-Identifier: Apache-2.0 */
#include "fsim/compiler/jit_runtime_v2.h"

#include <cstddef>

static_assert(sizeof(void*) == 8U);
static_assert(FSIM_JIT_SERVICES_ABI_VERSION_V2 == UINT32_C(2));
static_assert(FSIM_JIT_SIGNAL_UPDATE_DOMAIN_GENERIC_V2 == UINT32_C(0));
static_assert(
    FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_ACTIVE_V2 == UINT32_C(1));
static_assert(
    FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_NBA_V2 == UINT32_C(2));
static_assert(sizeof(fsim_jit_services_v2) == 688U);
#define FSIM_JIT_SERVICES_V2_INDEX_ENUM(name) \
    FSIM_JIT_SERVICES_V2_INDEX_##name,
enum {
    FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(
        FSIM_JIT_SERVICES_V2_INDEX_ENUM)
    FSIM_JIT_SERVICES_V2_CALLBACK_COUNT
};
#undef FSIM_JIT_SERVICES_V2_INDEX_ENUM

#define FSIM_JIT_ASSERT_SERVICE_OFFSET(name) \
    static_assert( \
        offsetof(fsim_jit_services_v2, name) \
            == 8U + FSIM_JIT_SERVICES_V2_INDEX_##name * sizeof( \
                static_cast<fsim_jit_services_v2*>(nullptr)->name));
FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(
    FSIM_JIT_ASSERT_SERVICE_OFFSET);
#undef FSIM_JIT_ASSERT_SERVICE_OFFSET

static_assert(offsetof(fsim_jit_services_v2, read_signal) == 8U);
static_assert(offsetof(fsim_jit_services_v2, write_update) == 32U);
static_assert(offsetof(fsim_jit_services_v2, read_signal_packed) == 616U);
static_assert(offsetof(fsim_jit_services_v2, query_event_triggered) == 664U);
static_assert(
    offsetof(fsim_jit_services_v2, write_projected_signal_packed) == 672U);
static_assert(
    offsetof(fsim_jit_services_v2, container_read_packed_index64) == 680U);
static_assert(sizeof(fsim_jit_runtime_instance_v2) == 232U);
static_assert(offsetof(fsim_jit_runtime_instance_v2, services) == 8U);
static_assert(offsetof(fsim_jit_runtime_instance_v2, context) == 16U);
static_assert(offsetof(fsim_jit_runtime_instance_v2, direct_update_slots) == 32U);
static_assert(sizeof(fsim_jit_frame_v2) == 344U);
static_assert(offsetof(fsim_jit_frame_v2, native_return_stack) == 88U);
static_assert(sizeof(fsim_jit_resume_result_v2) == 24U);
static_assert(sizeof(fsim_jit_update_slot_v2) == 80U);

int main()
{
    return 0;
}
