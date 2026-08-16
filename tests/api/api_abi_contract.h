/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FSIM_TEST_API_ABI_CONTRACT_H
#define FSIM_TEST_API_ABI_CONTRACT_H

#include "fsim/api.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define FSIM_ABI_ASSERT static_assert
#define FSIM_ABI_ALIGNOF(type) alignof(type)
#else
#define FSIM_ABI_ASSERT _Static_assert
#define FSIM_ABI_ALIGNOF(type) _Alignof(type)
#endif

#define FSIM_ABI_LAYOUT(type, expected_size, expected_align)                 \
    FSIM_ABI_ASSERT(sizeof(type) == (expected_size), #type " size changed"); \
    FSIM_ABI_ASSERT(                                                         \
        FSIM_ABI_ALIGNOF(type) == (expected_align), #type " alignment changed")
#define FSIM_ABI_OFFSET(type, field, expected) \
    FSIM_ABI_ASSERT(                           \
        offsetof(type, field) == (expected), #type "." #field " moved")

FSIM_ABI_ASSERT(sizeof(void*) == 8, "fsim requires the x86-64 object model");
FSIM_ABI_ASSERT(sizeof(size_t) == 8, "fsim requires 64-bit size_t");
FSIM_ABI_ASSERT(sizeof(fsim_session_t) == 8, "session handle width changed");
FSIM_ABI_ASSERT(sizeof(fsim_object_t) == 8, "object handle width changed");
FSIM_ABI_ASSERT(sizeof(fsim_time_t) == 8, "time width changed");
FSIM_ABI_ASSERT(FSIM_API_VERSION == UINT32_C(1), "API version changed");
FSIM_ABI_ASSERT(FSIM_INVALID_SESSION == UINT64_C(0), "invalid session changed");
FSIM_ABI_ASSERT(FSIM_INVALID_OBJECT == UINT64_C(0), "invalid object changed");

FSIM_ABI_ASSERT(sizeof(fsim_status_t) == 4, "status enum width changed");
FSIM_ABI_ASSERT(FSIM_STATUS_OK == 0, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_INVALID_ARGUMENT == 1, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_INVALID_HANDLE == 2, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_INCOMPATIBLE_ABI == 3, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_IO_ERROR == 4, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_COMPILE_ERROR == 5, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_RUNTIME_ERROR == 6, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_STOPPED == 7, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_UNAVAILABLE == 8, "status values changed");
FSIM_ABI_ASSERT(FSIM_STATUS_INTERNAL_ERROR == 9, "status values changed");

FSIM_ABI_ASSERT(sizeof(fsim_severity_t) == 4, "severity enum width changed");
FSIM_ABI_ASSERT(FSIM_SEVERITY_NOTE == 0, "severity values changed");
FSIM_ABI_ASSERT(FSIM_SEVERITY_WARNING == 1, "severity values changed");
FSIM_ABI_ASSERT(FSIM_SEVERITY_ERROR == 2, "severity values changed");
FSIM_ABI_ASSERT(FSIM_SEVERITY_FATAL == 3, "severity values changed");

FSIM_ABI_ASSERT(sizeof(fsim_object_kind_t) == 4, "object enum width changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_UNKNOWN == 0, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_ROOT == 1, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_SCOPE == 2, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_PROCESS == 3, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_SIGNAL == 4, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_PORT == 5, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_VARIABLE == 6, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_DRIVER == 7, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_EVENT == 8, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_CHANNEL == 9, "object values changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_EXPORT == 10, "object values changed");

FSIM_ABI_ASSERT(sizeof(fsim_step_kind_t) == 4, "step enum width changed");
FSIM_ABI_ASSERT(FSIM_STEP_STATEMENT == 0, "step values changed");
FSIM_ABI_ASSERT(FSIM_STEP_PROCESS == 1, "step values changed");
FSIM_ABI_ASSERT(FSIM_STEP_DELTA == 2, "step values changed");
FSIM_ABI_ASSERT(FSIM_STEP_TIME == 3, "step values changed");

FSIM_ABI_ASSERT(
    sizeof(fsim_lifecycle_event_t) == 4, "lifecycle enum width changed");
FSIM_ABI_ASSERT(FSIM_LIFECYCLE_DESIGN_LOADED == 0, "lifecycle changed");
FSIM_ABI_ASSERT(FSIM_LIFECYCLE_SIMULATION_STARTED == 1, "lifecycle changed");
FSIM_ABI_ASSERT(FSIM_LIFECYCLE_SIMULATION_STOPPED == 2, "lifecycle changed");
FSIM_ABI_ASSERT(FSIM_LIFECYCLE_SIMULATION_FINISHED == 3, "lifecycle changed");

FSIM_ABI_ASSERT(sizeof(fsim_safe_point_kind_t) == 4, "safe-point width changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_SCHEDULER == 0, "safe-point values changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_STATEMENT == 1, "safe-point values changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_CALL == 2, "safe-point values changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_WAIT == 3, "safe-point values changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_ASSERTION == 4, "safe-point values changed");
FSIM_ABI_ASSERT(FSIM_SAFE_POINT_PROCESS_ENTRY == 5, "safe-point values changed");
FSIM_ABI_ASSERT(
    FSIM_SAFE_POINT_PROCESS_SUSPEND == 6, "safe-point values changed");

FSIM_ABI_ASSERT(
    sizeof(fsim_scheduler_phase_t) == 4, "scheduler enum width changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_UNKNOWN == 0, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_ACTIVE == 1, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_INACTIVE == 2, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_UPDATE == 3, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_POSTPONED == 4, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_REACTIVE == 5, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_OBSERVED == 6, "scheduler values changed");
FSIM_ABI_ASSERT(
    FSIM_SCHEDULER_PHASE_RE_INACTIVE == 7, "scheduler values changed");
FSIM_ABI_ASSERT(FSIM_SCHEDULER_PHASE_RE_UPDATE == 8, "scheduler values changed");

FSIM_ABI_ASSERT(
    sizeof(fsim_sdf_delay_selection_t) == 4, "SDF selection width changed");
FSIM_ABI_ASSERT(FSIM_SDF_DELAY_MINIMUM == 0, "SDF selection changed");
FSIM_ABI_ASSERT(FSIM_SDF_DELAY_TYPICAL == 1, "SDF selection changed");
FSIM_ABI_ASSERT(FSIM_SDF_DELAY_MAXIMUM == 2, "SDF selection changed");
FSIM_ABI_ASSERT(sizeof(fsim_sdf_phase_t) == 4, "SDF phase width changed");
FSIM_ABI_ASSERT(FSIM_SDF_PHASE_COMPILE == 0, "SDF phase changed");
FSIM_ABI_ASSERT(FSIM_SDF_PHASE_ELABORATE == 1, "SDF phase changed");
FSIM_ABI_ASSERT(FSIM_SDF_PHASE_SIMULATE == 2, "SDF phase changed");
FSIM_ABI_ASSERT(
    sizeof(fsim_sdf_report_kind_t) == 4, "SDF report width changed");
FSIM_ABI_ASSERT(FSIM_SDF_REPORT_INPUT == 0, "SDF report changed");
FSIM_ABI_ASSERT(FSIM_SDF_REPORT_PATH == 1, "SDF report changed");
FSIM_ABI_ASSERT(FSIM_SDF_REPORT_TIMING_CHECK == 2, "SDF report changed");

FSIM_ABI_ASSERT(sizeof(fsim_trace_format_t) == 4, "trace format width changed");
FSIM_ABI_ASSERT(FSIM_TRACE_FORMAT_AUTO == 0, "trace format changed");
FSIM_ABI_ASSERT(FSIM_TRACE_FORMAT_VCD == 1, "trace format changed");
FSIM_ABI_ASSERT(FSIM_TRACE_FORMAT_FST == 2, "trace format changed");
FSIM_ABI_ASSERT(
    sizeof(fsim_trace_compression_t) == 4, "trace compression width changed");
FSIM_ABI_ASSERT(FSIM_TRACE_COMPRESSION_AUTO == 0, "trace compression changed");
FSIM_ABI_ASSERT(FSIM_TRACE_COMPRESSION_NONE == 1, "trace compression changed");
FSIM_ABI_ASSERT(
    FSIM_TRACE_COMPRESSION_DETERMINISTIC == 2, "trace compression changed");
FSIM_ABI_ASSERT(
    sizeof(fsim_trace_lifecycle_t) == 4, "trace lifecycle width changed");
FSIM_ABI_ASSERT(FSIM_TRACE_LIFECYCLE_DISABLED == 0, "trace lifecycle changed");
FSIM_ABI_ASSERT(FSIM_TRACE_LIFECYCLE_CONFIGURED == 1, "trace lifecycle changed");
FSIM_ABI_ASSERT(FSIM_TRACE_LIFECYCLE_OPEN == 2, "trace lifecycle changed");
FSIM_ABI_ASSERT(FSIM_TRACE_LIFECYCLE_COMPLETE == 3, "trace lifecycle changed");
FSIM_ABI_ASSERT(FSIM_TRACE_LIFECYCLE_FAILED == 4, "trace lifecycle changed");
FSIM_ABI_ASSERT(sizeof(fsim_trace_phase_t) == 4, "trace phase width changed");
FSIM_ABI_ASSERT(FSIM_TRACE_PHASE_COMPILE == 0, "trace phase changed");
FSIM_ABI_ASSERT(FSIM_TRACE_PHASE_ELABORATE == 1, "trace phase changed");
FSIM_ABI_ASSERT(FSIM_TRACE_PHASE_SIMULATE == 2, "trace phase changed");
FSIM_ABI_ASSERT(
    sizeof(fsim_trace_report_kind_t) == 4, "trace report width changed");
FSIM_ABI_ASSERT(FSIM_TRACE_REPORT_OUTPUT == 0, "trace report changed");
FSIM_ABI_ASSERT(FSIM_TRACE_REPORT_FORMAT == 1, "trace report changed");
FSIM_ABI_ASSERT(FSIM_TRACE_REPORT_COMPRESSION == 2, "trace report changed");
FSIM_ABI_ASSERT(FSIM_TRACE_REPORT_SELECTION == 3, "trace report changed");
FSIM_ABI_ASSERT(FSIM_TRACE_REPORT_LIFECYCLE == 4, "trace report changed");

FSIM_ABI_LAYOUT(fsim_string_view_t, 16, 8);
FSIM_ABI_OFFSET(fsim_string_view_t, data, 0);
FSIM_ABI_OFFSET(fsim_string_view_t, size, 8);

FSIM_ABI_LAYOUT(fsim_session_options_t, 32, 8);
FSIM_ABI_OFFSET(fsim_session_options_t, struct_size, 0);
FSIM_ABI_OFFSET(fsim_session_options_t, api_version, 4);
FSIM_ABI_OFFSET(fsim_session_options_t, max_deltas, 8);
FSIM_ABI_OFFSET(fsim_session_options_t, seed, 16);
FSIM_ABI_OFFSET(fsim_session_options_t, flags, 24);
FSIM_ABI_OFFSET(fsim_session_options_t, reserved, 28);

FSIM_ABI_LAYOUT(fsim_diagnostic_t, 72, 8);
FSIM_ABI_OFFSET(fsim_diagnostic_t, severity, 8);
FSIM_ABI_OFFSET(fsim_diagnostic_t, code, 16);
FSIM_ABI_OFFSET(fsim_diagnostic_t, message, 32);
FSIM_ABI_OFFSET(fsim_diagnostic_t, path, 48);
FSIM_ABI_OFFSET(fsim_diagnostic_t, line, 64);
FSIM_ABI_OFFSET(fsim_diagnostic_t, column, 68);

FSIM_ABI_LAYOUT(fsim_object_info_t, 208, 8);
FSIM_ABI_OFFSET(fsim_object_info_t, handle, 8);
FSIM_ABI_OFFSET(fsim_object_info_t, parent, 16);
FSIM_ABI_OFFSET(fsim_object_info_t, kind, 24);
FSIM_ABI_OFFSET(fsim_object_info_t, flags, 28);
FSIM_ABI_OFFSET(fsim_object_info_t, width, 32);
FSIM_ABI_OFFSET(fsim_object_info_t, name, 40);
FSIM_ABI_OFFSET(fsim_object_info_t, full_name, 56);
FSIM_ABI_OFFSET(fsim_object_info_t, type_name, 72);
FSIM_ABI_OFFSET(fsim_object_info_t, source_path, 88);
FSIM_ABI_OFFSET(fsim_object_info_t, source_line, 104);
FSIM_ABI_OFFSET(fsim_object_info_t, source_column, 108);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_unit, 112);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_source_path, 128);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_language, 144);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_standard, 160);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_compatibility_profile, 176);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_unit_id, 192);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_source_id, 196);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_source_line, 200);
FSIM_ABI_OFFSET(fsim_object_info_t, provenance_source_column, 204);

FSIM_ABI_LAYOUT(fsim_mapped_library_info_t, 88, 8);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, library, 8);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, metadata_digest, 24);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, unit_count, 40);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, native_accepted, 48);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, reserved, 52);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, native_kind, 56);
FSIM_ABI_OFFSET(fsim_mapped_library_info_t, native_fingerprint, 72);

FSIM_ABI_LAYOUT(fsim_sdf_input_t, 72, 8);
FSIM_ABI_OFFSET(fsim_sdf_input_t, source_identity, 8);
FSIM_ABI_OFFSET(fsim_sdf_input_t, root, 24);
FSIM_ABI_OFFSET(fsim_sdf_input_t, cell_pattern, 40);
FSIM_ABI_OFFSET(fsim_sdf_input_t, file_precedence, 56);
FSIM_ABI_OFFSET(fsim_sdf_input_t, cell_precedence, 64);

FSIM_ABI_LAYOUT(fsim_sdf_options_t, 40, 8);
FSIM_ABI_OFFSET(fsim_sdf_options_t, selection, 8);
FSIM_ABI_OFFSET(fsim_sdf_options_t, phase, 12);
FSIM_ABI_OFFSET(fsim_sdf_options_t, inputs, 16);
FSIM_ABI_OFFSET(fsim_sdf_options_t, input_count, 24);
FSIM_ABI_OFFSET(fsim_sdf_options_t, report_limit, 32);

FSIM_ABI_LAYOUT(fsim_sdf_summary_t, 88, 8);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, input_count, 8);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, file_count, 16);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, applied_path_count, 24);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, applied_timing_check_count, 32);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, report_entry_count, 40);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, returned_report_entry_count, 48);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, generation, 56);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, effective, 64);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, report_truncated, 68);
FSIM_ABI_OFFSET(fsim_sdf_summary_t, semantic_identity, 72);

FSIM_ABI_LAYOUT(fsim_sdf_report_entry_t, 64, 8);
FSIM_ABI_OFFSET(fsim_sdf_report_entry_t, kind, 8);
FSIM_ABI_OFFSET(fsim_sdf_report_entry_t, reserved, 12);
FSIM_ABI_OFFSET(fsim_sdf_report_entry_t, source_identity, 16);
FSIM_ABI_OFFSET(fsim_sdf_report_entry_t, object_identity, 32);
FSIM_ABI_OFFSET(fsim_sdf_report_entry_t, canonical_identity, 48);

FSIM_ABI_LAYOUT(fsim_trace_options_t, 72, 8);
FSIM_ABI_OFFSET(fsim_trace_options_t, format, 8);
FSIM_ABI_OFFSET(fsim_trace_options_t, compression, 12);
FSIM_ABI_OFFSET(fsim_trace_options_t, lifecycle, 16);
FSIM_ABI_OFFSET(fsim_trace_options_t, phase, 20);
FSIM_ABI_OFFSET(fsim_trace_options_t, output, 24);
FSIM_ABI_OFFSET(fsim_trace_options_t, selections, 40);
FSIM_ABI_OFFSET(fsim_trace_options_t, selection_count, 48);
FSIM_ABI_OFFSET(fsim_trace_options_t, report_limit, 56);
FSIM_ABI_OFFSET(fsim_trace_options_t, generation, 64);

FSIM_ABI_LAYOUT(fsim_trace_status_t, 104, 8);
FSIM_ABI_OFFSET(fsim_trace_status_t, requested_format, 8);
FSIM_ABI_OFFSET(fsim_trace_status_t, effective_format, 12);
FSIM_ABI_OFFSET(fsim_trace_status_t, requested_compression, 16);
FSIM_ABI_OFFSET(fsim_trace_status_t, effective_compression, 20);
FSIM_ABI_OFFSET(fsim_trace_status_t, lifecycle, 24);
FSIM_ABI_OFFSET(fsim_trace_status_t, reserved, 28);
FSIM_ABI_OFFSET(fsim_trace_status_t, selection_count, 32);
FSIM_ABI_OFFSET(fsim_trace_status_t, report_entry_count, 40);
FSIM_ABI_OFFSET(fsim_trace_status_t, returned_report_entry_count, 48);
FSIM_ABI_OFFSET(fsim_trace_status_t, generation, 56);
FSIM_ABI_OFFSET(fsim_trace_status_t, report_truncated, 64);
FSIM_ABI_OFFSET(fsim_trace_status_t, reserved2, 68);
FSIM_ABI_OFFSET(fsim_trace_status_t, output, 72);
FSIM_ABI_OFFSET(fsim_trace_status_t, semantic_identity, 88);

FSIM_ABI_LAYOUT(fsim_trace_report_entry_t, 64, 8);
FSIM_ABI_OFFSET(fsim_trace_report_entry_t, kind, 8);
FSIM_ABI_OFFSET(fsim_trace_report_entry_t, reserved, 12);
FSIM_ABI_OFFSET(fsim_trace_report_entry_t, name, 16);
FSIM_ABI_OFFSET(fsim_trace_report_entry_t, value, 32);
FSIM_ABI_OFFSET(fsim_trace_report_entry_t, canonical_identity, 48);

FSIM_ABI_LAYOUT(fsim_safe_point_info_t, 168, 8);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, process, 8);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, time, 16);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, delta, 24);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, instruction, 32);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, kind, 40);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, phase, 44);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, source_path, 48);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, source_line, 64);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, source_column, 68);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_unit, 72);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_source_path, 88);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_language, 104);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_standard, 120);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_compatibility_profile, 136);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_unit_id, 152);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_source_id, 156);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_source_line, 160);
FSIM_ABI_OFFSET(fsim_safe_point_info_t, provenance_source_column, 164);

FSIM_ABI_LAYOUT(fsim_callbacks_t, 56, 8);
FSIM_ABI_OFFSET(fsim_callbacks_t, user_data, 8);
FSIM_ABI_OFFSET(fsim_callbacks_t, safe_point, 16);
FSIM_ABI_OFFSET(fsim_callbacks_t, value_change, 24);
FSIM_ABI_OFFSET(fsim_callbacks_t, assertion, 32);
FSIM_ABI_OFFSET(fsim_callbacks_t, lifecycle, 40);
FSIM_ABI_OFFSET(fsim_callbacks_t, safe_point_info, 48);

FSIM_ABI_ASSERT(FSIM_STRUCT_HEADER_SIZE == 8, "struct header changed");
FSIM_ABI_ASSERT(FSIM_OBJECT_INFO_V1_SIZE == 88, "object prefix changed");
FSIM_ABI_ASSERT(FSIM_CALLBACKS_V1_SIZE == 48, "callback prefix changed");

#undef FSIM_ABI_OFFSET
#undef FSIM_ABI_LAYOUT
#undef FSIM_ABI_ALIGNOF
#undef FSIM_ABI_ASSERT

#endif
