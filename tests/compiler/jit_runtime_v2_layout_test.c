/* SPDX-License-Identifier: Apache-2.0 */
#include "fsim/compiler/jit_runtime_v2.h"

#include <stddef.h>

_Static_assert(
    sizeof(void*) == 8U,
    "ABI layout test requires 64-bit pointers");
_Static_assert(
    FSIM_JIT_SERVICES_ABI_VERSION_V2 == UINT32_C(2),
    "services v2 version changed");
_Static_assert(
    FSIM_JIT_RUNTIME_ABI_VERSION_V2 == UINT32_C(2),
    "runtime v2 version changed");
_Static_assert(
    FSIM_JIT_SIGNAL_UPDATE_DOMAIN_GENERIC_V2 == UINT32_C(0),
    "generic update domain changed");
_Static_assert(
    FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_ACTIVE_V2 == UINT32_C(1),
    "active update domain changed");
_Static_assert(
    FSIM_JIT_SIGNAL_UPDATE_DOMAIN_SYSTEMVERILOG_NBA_V2 == UINT32_C(2),
    "NBA update domain changed");
_Static_assert(
    sizeof(fsim_jit_services_v2) == 688U,
    "service table size changed");
_Static_assert(
    sizeof(fsim_jit_runtime_instance_v2) == 232U,
    "runtime instance size changed");
_Static_assert(
    sizeof(fsim_jit_frame_v2) == 344U,
    "frame v2 size changed");
_Static_assert(
    sizeof(fsim_jit_resume_result_v2) == 24U,
    "resume result v2 size changed");
_Static_assert(
    sizeof(fsim_jit_update_slot_v2) == 80U,
    "update slot v2 size changed");
#define FSIM_JIT_SERVICES_V2_INDEX_ENUM(name) \
    FSIM_JIT_SERVICES_V2_INDEX_##name,
enum {
    FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(
        FSIM_JIT_SERVICES_V2_INDEX_ENUM)
    FSIM_JIT_SERVICES_V2_CALLBACK_COUNT
};
#undef FSIM_JIT_SERVICES_V2_INDEX_ENUM

#define FSIM_JIT_ASSERT_SERVICE_OFFSET(name) \
    _Static_assert( \
        offsetof(fsim_jit_services_v2, name) \
            == 8U + FSIM_JIT_SERVICES_V2_INDEX_##name * sizeof( \
                ((fsim_jit_services_v2*)0)->name), \
        "fsim_jit_services_v2." #name " offset changed");
FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(
    FSIM_JIT_ASSERT_SERVICE_OFFSET)
#undef FSIM_JIT_ASSERT_SERVICE_OFFSET

_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, abi_version) == 0U,
    "fsim_jit_runtime_instance_v2.abi_version offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, struct_size) == 4U,
    "fsim_jit_runtime_instance_v2.struct_size offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, read_signal) == 8U,
    "fsim_jit_services_v2.read_signal offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, write_update) == 32U,
    "fsim_jit_services_v2.write_update offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, read_signal_packed) == 616U,
    "fsim_jit_services_v2.read_signal_packed offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, write_signal_packed) == 624U,
    "fsim_jit_services_v2.write_signal_packed offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, read_signal_dynamic_part) == 632U,
    "fsim_jit_services_v2.read_signal_dynamic_part offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, query_event_triggered) == 664U,
    "fsim_jit_services_v2.query_event_triggered offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, write_projected_signal_packed) == 672U,
    "fsim_jit_services_v2.write_projected_signal_packed offset changed");
_Static_assert(
    offsetof(fsim_jit_services_v2, container_read_packed_index64) == 680U,
    "fsim_jit_services_v2.container_read_packed_index64 offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, services) == 8U,
    "runtime.services offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, context) == 16U,
    "runtime.context offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, flags) == 24U,
    "runtime.flags offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, direct_update_slots) == 32U,
    "runtime.direct_update_slots offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, direct_signal_aval) == 48U,
    "runtime.direct_signal_aval offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, direct_wide_signal_aval) == 88U,
    "runtime.direct_wide_signal_aval offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, direct_update_active_words) == 120U,
    "runtime.direct_update_active_words offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, static_trigger_mask) == 136U,
    "runtime.static_trigger_mask offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, direct_signal_logic9_plane3) == 184U,
    "runtime.direct_signal_logic9_plane3 offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, code_coverage_counter_values) == 200U,
    "runtime.code_coverage_counter_values offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, fused_activation_words) == 216U,
    "runtime.fused_activation_words offset changed");
_Static_assert(
    offsetof(fsim_jit_runtime_instance_v2, fused_activation_reserved) == 228U,
    "runtime.fused_activation_reserved offset changed");
_Static_assert(
    offsetof(fsim_jit_frame_v2, layout_id_low) == 8U,
    "frame layout id offset changed");
_Static_assert(
    offsetof(fsim_jit_frame_v2, register_aval) == 40U,
    "frame register planes offset changed");
_Static_assert(
    offsetof(fsim_jit_frame_v2, native_return_stack) == 88U,
    "frame return stack offset changed");
_Static_assert(
    offsetof(fsim_jit_resume_result_v2, delay) == 16U,
    "result delay offset changed");

int main(void)
{
    return 0;
}
