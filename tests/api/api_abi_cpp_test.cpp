// SPDX-License-Identifier: Apache-2.0
#include "api_abi_contract.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

extern "C" int fsim_api_abi_c_probe(void);

#define FSIM_CPP_SIGNATURE(symbol, type) \
    static_assert(                       \
        std::is_same_v<decltype(&(symbol)), type>, #symbol " signature changed")

static_assert(std::is_same_v<fsim_session_t, std::uint64_t>);
static_assert(std::is_same_v<fsim_object_t, std::uint64_t>);
static_assert(std::is_same_v<fsim_time_t, std::uint64_t>);
static_assert(std::is_standard_layout_v<fsim_string_view_t>);
static_assert(std::is_trivially_copyable_v<fsim_string_view_t>);
static_assert(std::is_standard_layout_v<fsim_session_options_t>);
static_assert(std::is_trivially_copyable_v<fsim_session_options_t>);
static_assert(std::is_standard_layout_v<fsim_diagnostic_t>);
static_assert(std::is_trivially_copyable_v<fsim_diagnostic_t>);
static_assert(std::is_standard_layout_v<fsim_object_info_t>);
static_assert(std::is_trivially_copyable_v<fsim_object_info_t>);
static_assert(std::is_standard_layout_v<fsim_callbacks_t>);
static_assert(std::is_trivially_copyable_v<fsim_callbacks_t>);

FSIM_CPP_SIGNATURE(fsim_get_api_version, std::uint32_t (*)(void));
FSIM_CPP_SIGNATURE(fsim_status_string, const char* (*)(fsim_status_t));
FSIM_CPP_SIGNATURE(
    fsim_session_create,
    fsim_status_t (*)(const fsim_session_options_t*, fsim_session_t*));
FSIM_CPP_SIGNATURE(fsim_session_destroy, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(
    fsim_session_load_project,
    fsim_status_t (*)(fsim_session_t, const char*));
FSIM_CPP_SIGNATURE(fsim_session_check, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(fsim_session_build, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(
    fsim_session_configure_sdf,
    fsim_status_t (*)(fsim_session_t, const fsim_sdf_options_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_sdf_summary,
    fsim_status_t (*)(fsim_session_t, fsim_sdf_summary_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_sdf_report_entry,
    fsim_status_t (*)(fsim_session_t, std::size_t, fsim_sdf_report_entry_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_configure_trace,
    fsim_status_t (*)(fsim_session_t, const fsim_trace_options_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_trace_status,
    fsim_status_t (*)(fsim_session_t, fsim_trace_status_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_trace_report_entry,
    fsim_status_t (*)(fsim_session_t, std::size_t, fsim_trace_report_entry_t*));
FSIM_CPP_SIGNATURE(fsim_session_flush_trace, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(fsim_session_close_trace, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(
    fsim_session_root, fsim_status_t (*)(fsim_session_t, fsim_object_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_find_object,
    fsim_status_t (*)(fsim_session_t, fsim_string_view_t, fsim_object_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_visit_children,
    fsim_status_t (*)(
        fsim_session_t, fsim_object_t, fsim_visit_object_callback_t, void*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_object_info,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_object_info_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_mapped_library_count,
    fsim_status_t (*)(fsim_session_t, std::size_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_mapped_library_info,
    fsim_status_t (*)(
        fsim_session_t, std::size_t, fsim_mapped_library_info_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_read_value,
    fsim_status_t (*)(
        fsim_session_t, fsim_object_t, char*, std::size_t, std::size_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_deposit,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_string_view_t));
FSIM_CPP_SIGNATURE(
    fsim_session_force,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_string_view_t));
FSIM_CPP_SIGNATURE(
    fsim_session_release,
    fsim_status_t (*)(fsim_session_t, fsim_object_t));
FSIM_CPP_SIGNATURE(
    fsim_session_run, fsim_status_t (*)(fsim_session_t, fsim_time_t));
FSIM_CPP_SIGNATURE(
    fsim_session_step,
    fsim_status_t (*)(fsim_session_t, fsim_step_kind_t));
FSIM_CPP_SIGNATURE(
    fsim_session_request_stop, fsim_status_t (*)(fsim_session_t));
FSIM_CPP_SIGNATURE(
    fsim_session_set_callbacks,
    fsim_status_t (*)(fsim_session_t, const fsim_callbacks_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_get_diagnostic,
    fsim_status_t (*)(fsim_session_t, std::size_t, fsim_diagnostic_t*));
FSIM_CPP_SIGNATURE(
    fsim_session_diagnostic_count,
    fsim_status_t (*)(fsim_session_t, std::size_t*));

static_assert(std::is_same_v<fsim_safe_point_callback_t,
    void (*)(fsim_session_t, fsim_object_t, fsim_time_t, std::uint64_t, void*)>);
static_assert(std::is_same_v<fsim_value_change_callback_t,
    void (*)(fsim_session_t, fsim_object_t, fsim_time_t, std::uint64_t, void*)>);
static_assert(std::is_same_v<fsim_assertion_callback_t,
    void (*)(fsim_session_t, fsim_object_t, const fsim_diagnostic_t*, void*)>);
static_assert(std::is_same_v<fsim_lifecycle_callback_t,
    void (*)(fsim_session_t, fsim_lifecycle_event_t, void*)>);
static_assert(std::is_same_v<fsim_safe_point_info_callback_t,
    void (*)(fsim_session_t, const fsim_safe_point_info_t*, void*)>);
static_assert(std::is_same_v<fsim_visit_object_callback_t,
    int (*)(fsim_session_t, fsim_object_t, void*)>);

#undef FSIM_CPP_SIGNATURE

int main()
{
    assert(fsim_api_abi_c_probe() == 0);

    try {
        fsim_session_options_t options { };
        fsim_session_t session = UINT64_C(0xfeedface);
        options.struct_size = FSIM_STRUCT_HEADER_SIZE - 1;
        options.api_version = FSIM_API_VERSION;
        assert(
            fsim_session_create(&options, &session)
            == FSIM_STATUS_INCOMPATIBLE_ABI);
        assert(session == FSIM_INVALID_SESSION);

        options.struct_size = FSIM_STRUCT_HEADER_SIZE;
        ++options.api_version;
        session = UINT64_C(0xfeedface);
        assert(
            fsim_session_create(&options, &session)
            == FSIM_STATUS_INCOMPATIBLE_ABI);
        assert(session == FSIM_INVALID_SESSION);

        assert(
            fsim_session_destroy(FSIM_INVALID_SESSION)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_diagnostic_count(FSIM_INVALID_SESSION, nullptr)
            == FSIM_STATUS_INVALID_ARGUMENT);
    } catch (...) {
        assert(false && "a C++ exception crossed the core C ABI boundary");
    }
    return 0;
}
