// SPDX-License-Identifier: Apache-2.0
#include "api_abi_contract.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define FSIM_C_SIGNATURE(symbol, type) \
    _Static_assert(                    \
        _Generic(&(symbol), type: 1, default: 0), #symbol " signature changed")
#define FSIM_C_TYPE(actual, expected) \
    _Static_assert(                   \
        _Generic((actual)0, expected: 1, default: 0), #actual " changed")

FSIM_C_SIGNATURE(fsim_get_api_version, uint32_t (*)(void));
FSIM_C_SIGNATURE(fsim_status_string, const char* (*)(fsim_status_t));
FSIM_C_SIGNATURE(
    fsim_session_create,
    fsim_status_t (*)(const fsim_session_options_t*, fsim_session_t*));
FSIM_C_SIGNATURE(fsim_session_destroy, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(
    fsim_session_load_project, fsim_status_t (*)(fsim_session_t, const char*));
FSIM_C_SIGNATURE(fsim_session_check, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(fsim_session_build, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(
    fsim_session_configure_sdf,
    fsim_status_t (*)(fsim_session_t, const fsim_sdf_options_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_sdf_summary,
    fsim_status_t (*)(fsim_session_t, fsim_sdf_summary_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_sdf_report_entry,
    fsim_status_t (*)(fsim_session_t, size_t, fsim_sdf_report_entry_t*));
FSIM_C_SIGNATURE(
    fsim_session_configure_trace,
    fsim_status_t (*)(fsim_session_t, const fsim_trace_options_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_trace_status,
    fsim_status_t (*)(fsim_session_t, fsim_trace_status_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_trace_report_entry,
    fsim_status_t (*)(fsim_session_t, size_t, fsim_trace_report_entry_t*));
FSIM_C_SIGNATURE(fsim_session_flush_trace, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(fsim_session_close_trace, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(
    fsim_session_root, fsim_status_t (*)(fsim_session_t, fsim_object_t*));
FSIM_C_SIGNATURE(
    fsim_session_find_object,
    fsim_status_t (*)(fsim_session_t, fsim_string_view_t, fsim_object_t*));
FSIM_C_SIGNATURE(
    fsim_session_visit_children,
    fsim_status_t (*)(
        fsim_session_t, fsim_object_t, fsim_visit_object_callback_t, void*));
FSIM_C_SIGNATURE(
    fsim_session_get_object_info,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_object_info_t*));
FSIM_C_SIGNATURE(
    fsim_session_mapped_library_count,
    fsim_status_t (*)(fsim_session_t, size_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_mapped_library_info,
    fsim_status_t (*)(fsim_session_t, size_t, fsim_mapped_library_info_t*));
FSIM_C_SIGNATURE(
    fsim_session_read_value,
    fsim_status_t (*)(
        fsim_session_t, fsim_object_t, char*, size_t, size_t*));
FSIM_C_SIGNATURE(
    fsim_session_deposit,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_string_view_t));
FSIM_C_SIGNATURE(
    fsim_session_force,
    fsim_status_t (*)(fsim_session_t, fsim_object_t, fsim_string_view_t));
FSIM_C_SIGNATURE(
    fsim_session_release,
    fsim_status_t (*)(fsim_session_t, fsim_object_t));
FSIM_C_SIGNATURE(
    fsim_session_run, fsim_status_t (*)(fsim_session_t, fsim_time_t));
FSIM_C_SIGNATURE(
    fsim_session_step, fsim_status_t (*)(fsim_session_t, fsim_step_kind_t));
FSIM_C_SIGNATURE(
    fsim_session_request_stop, fsim_status_t (*)(fsim_session_t));
FSIM_C_SIGNATURE(
    fsim_session_set_callbacks,
    fsim_status_t (*)(fsim_session_t, const fsim_callbacks_t*));
FSIM_C_SIGNATURE(
    fsim_session_get_diagnostic,
    fsim_status_t (*)(fsim_session_t, size_t, fsim_diagnostic_t*));
FSIM_C_SIGNATURE(
    fsim_session_diagnostic_count,
    fsim_status_t (*)(fsim_session_t, size_t*));

FSIM_C_TYPE(
    fsim_safe_point_callback_t,
    void (*)(fsim_session_t, fsim_object_t, fsim_time_t, uint64_t, void*));
FSIM_C_TYPE(
    fsim_value_change_callback_t,
    void (*)(fsim_session_t, fsim_object_t, fsim_time_t, uint64_t, void*));
FSIM_C_TYPE(
    fsim_assertion_callback_t,
    void (*)(fsim_session_t, fsim_object_t, const fsim_diagnostic_t*, void*));
FSIM_C_TYPE(
    fsim_lifecycle_callback_t,
    void (*)(fsim_session_t, fsim_lifecycle_event_t, void*));
FSIM_C_TYPE(
    fsim_safe_point_info_callback_t,
    void (*)(fsim_session_t, const fsim_safe_point_info_t*, void*));
FSIM_C_TYPE(
    fsim_visit_object_callback_t,
    int (*)(fsim_session_t, fsim_object_t, void*));

#undef FSIM_C_TYPE
#undef FSIM_C_SIGNATURE

static int fsim_api_abi_visit(
    fsim_session_t session, fsim_object_t object, void* user_data)
{
    (void)session;
    (void)object;
    (void)user_data;
    return 1;
}

int fsim_api_abi_c_probe(void)
{
    fsim_session_options_t options = { 0 };
    fsim_session_t session = FSIM_INVALID_SESSION;
    fsim_trace_options_t trace = { 0 };
    fsim_trace_status_t status = { 0 };

    assert(fsim_get_api_version() == FSIM_API_VERSION);
    assert(fsim_status_string(FSIM_STATUS_OK) != NULL);

    options.struct_size = sizeof(options);
    options.api_version = FSIM_API_VERSION;
    assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
    assert(session != FSIM_INVALID_SESSION);

    trace.struct_size = sizeof(trace);
    trace.api_version = FSIM_API_VERSION;
    trace.lifecycle = FSIM_TRACE_LIFECYCLE_DISABLED;
    trace.report_limit = 4;
    assert(fsim_session_configure_trace(session, &trace) == FSIM_STATUS_OK);

    trace.api_version = FSIM_API_VERSION + 1;
    assert(
        fsim_session_configure_trace(session, &trace)
        == FSIM_STATUS_INCOMPATIBLE_ABI);
    trace.api_version = FSIM_API_VERSION;
    --trace.struct_size;
    assert(
        fsim_session_configure_trace(session, &trace)
        == FSIM_STATUS_INCOMPATIBLE_ABI);
    ++trace.struct_size;

    status.struct_size = sizeof(status);
    status.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_DISABLED);
    --status.struct_size;
    assert(
        fsim_session_get_trace_status(session, &status)
        == FSIM_STATUS_INCOMPATIBLE_ABI);
    ++status.struct_size;
    ++status.api_version;
    assert(
        fsim_session_get_trace_status(session, &status)
        == FSIM_STATUS_INCOMPATIBLE_ABI);

    assert(fsim_session_destroy(session) == FSIM_STATUS_OK);

    {
        fsim_sdf_options_t sdf = { 0 };
        fsim_sdf_summary_t sdf_summary = { 0 };
        fsim_sdf_report_entry_t sdf_entry = { 0 };
        fsim_trace_report_entry_t trace_entry = { 0 };
        fsim_object_t object = FSIM_INVALID_OBJECT;
        fsim_object_info_t object_info = { 0 };
        fsim_mapped_library_info_t library_info = { 0 };
        fsim_diagnostic_t diagnostic = { 0 };
        fsim_string_view_t empty = { 0 };
        size_t count = 0;
        size_t required = 0;
        char buffer[1] = { 0 };

        sdf.struct_size = sizeof(sdf);
        sdf.api_version = FSIM_API_VERSION;
        sdf_summary.struct_size = sizeof(sdf_summary);
        sdf_summary.api_version = FSIM_API_VERSION;
        sdf_entry.struct_size = sizeof(sdf_entry);
        sdf_entry.api_version = FSIM_API_VERSION;
        trace.struct_size = sizeof(trace);
        trace.api_version = FSIM_API_VERSION;
        status.struct_size = sizeof(status);
        status.api_version = FSIM_API_VERSION;
        trace_entry.struct_size = sizeof(trace_entry);
        trace_entry.api_version = FSIM_API_VERSION;
        object_info.struct_size = sizeof(object_info);
        object_info.api_version = FSIM_API_VERSION;
        library_info.struct_size = sizeof(library_info);
        library_info.api_version = FSIM_API_VERSION;
        diagnostic.struct_size = sizeof(diagnostic);
        diagnostic.api_version = FSIM_API_VERSION;

        assert(fsim_session_load_project(session, "missing") == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_check(session) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_build(session) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_configure_sdf(session, &sdf) == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_sdf_summary(session, &sdf_summary)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_sdf_report_entry(session, 0, &sdf_entry)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_configure_trace(session, &trace)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_trace_status(session, &status)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_trace_report_entry(session, 0, &trace_entry)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_flush_trace(session) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_close_trace(session) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_root(session, &object) == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_find_object(session, empty, &object)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_visit_children(
                session, object, fsim_api_abi_visit, NULL)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_object_info(session, object, &object_info)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_mapped_library_count(session, &count)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_mapped_library_info(session, 0, &library_info)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_read_value(
                session, object, buffer, sizeof(buffer), &required)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_deposit(session, object, empty)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_force(session, object, empty)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_release(session, object) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_run(session, 0) == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_step(session, FSIM_STEP_STATEMENT)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_request_stop(session) == FSIM_STATUS_INVALID_HANDLE);
        assert(fsim_session_set_callbacks(session, NULL) == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_get_diagnostic(session, 0, &diagnostic)
            == FSIM_STATUS_INVALID_HANDLE);
        assert(
            fsim_session_diagnostic_count(session, &count)
            == FSIM_STATUS_INVALID_HANDLE);
    }
    return 0;
}
