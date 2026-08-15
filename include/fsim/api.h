/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FSIM_API_H
#define FSIM_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(FSIM_SHARED)
#if defined(FSIM_BUILDING_LIBRARY)
#define FSIM_PUBLIC __declspec(dllexport)
#else
#define FSIM_PUBLIC __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(FSIM_SHARED)
#define FSIM_PUBLIC __attribute__((visibility("default")))
#else
#define FSIM_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_API_VERSION UINT32_C(1)
#define FSIM_INVALID_SESSION UINT64_C(0)
#define FSIM_INVALID_OBJECT UINT64_C(0)
#define FSIM_OBJECT_FLAG_FORCED UINT32_C(0x00000001)
#define FSIM_OBJECT_FLAG_HAS_SOURCE UINT32_C(0x00000002)
#define FSIM_OBJECT_FLAG_INITIALIZED UINT32_C(0x00000004)
#define FSIM_OBJECT_FLAG_ENTERED UINT32_C(0x00000008)
#define FSIM_OBJECT_FLAG_RESOLVED UINT32_C(0x00000010)
#define FSIM_OBJECT_FLAG_HAS_PROVENANCE UINT32_C(0x00000020)

typedef uint64_t fsim_session_t;
typedef uint64_t fsim_object_t;
typedef uint64_t fsim_time_t;

typedef struct fsim_string_view {
  const char* data;
  size_t size;
} fsim_string_view_t;

typedef enum fsim_status {
  FSIM_STATUS_OK = 0,
  FSIM_STATUS_INVALID_ARGUMENT = 1,
  FSIM_STATUS_INVALID_HANDLE = 2,
  FSIM_STATUS_INCOMPATIBLE_ABI = 3,
  FSIM_STATUS_IO_ERROR = 4,
  FSIM_STATUS_COMPILE_ERROR = 5,
  FSIM_STATUS_RUNTIME_ERROR = 6,
  FSIM_STATUS_STOPPED = 7,
  FSIM_STATUS_UNAVAILABLE = 8,
  FSIM_STATUS_INTERNAL_ERROR = 9
} fsim_status_t;

typedef enum fsim_severity {
  FSIM_SEVERITY_NOTE = 0,
  FSIM_SEVERITY_WARNING = 1,
  FSIM_SEVERITY_ERROR = 2,
  FSIM_SEVERITY_FATAL = 3
} fsim_severity_t;

typedef enum fsim_object_kind {
  FSIM_OBJECT_UNKNOWN = 0,
  FSIM_OBJECT_ROOT = 1,
  FSIM_OBJECT_SCOPE = 2,
  FSIM_OBJECT_PROCESS = 3,
  FSIM_OBJECT_SIGNAL = 4,
  FSIM_OBJECT_PORT = 5,
  FSIM_OBJECT_VARIABLE = 6,
  FSIM_OBJECT_DRIVER = 7,
  FSIM_OBJECT_EVENT = 8,
  FSIM_OBJECT_CHANNEL = 9,
  FSIM_OBJECT_EXPORT = 10
} fsim_object_kind_t;

typedef enum fsim_step_kind {
  FSIM_STEP_STATEMENT = 0,
  FSIM_STEP_PROCESS = 1,
  FSIM_STEP_DELTA = 2,
  FSIM_STEP_TIME = 3
} fsim_step_kind_t;

typedef enum fsim_lifecycle_event {
  FSIM_LIFECYCLE_DESIGN_LOADED = 0,
  FSIM_LIFECYCLE_SIMULATION_STARTED = 1,
  FSIM_LIFECYCLE_SIMULATION_STOPPED = 2,
  FSIM_LIFECYCLE_SIMULATION_FINISHED = 3
} fsim_lifecycle_event_t;

typedef enum fsim_safe_point_kind {
  FSIM_SAFE_POINT_SCHEDULER = 0,
  FSIM_SAFE_POINT_STATEMENT = 1,
  FSIM_SAFE_POINT_CALL = 2,
  FSIM_SAFE_POINT_WAIT = 3,
  FSIM_SAFE_POINT_ASSERTION = 4,
  FSIM_SAFE_POINT_PROCESS_ENTRY = 5,
  FSIM_SAFE_POINT_PROCESS_SUSPEND = 6
} fsim_safe_point_kind_t;

typedef enum fsim_scheduler_phase {
    FSIM_SCHEDULER_PHASE_UNKNOWN = 0,
    FSIM_SCHEDULER_PHASE_ACTIVE = 1,
    FSIM_SCHEDULER_PHASE_INACTIVE = 2,
    FSIM_SCHEDULER_PHASE_UPDATE = 3,
    FSIM_SCHEDULER_PHASE_POSTPONED = 4,
    FSIM_SCHEDULER_PHASE_REACTIVE = 5,
    FSIM_SCHEDULER_PHASE_OBSERVED = 6,
    FSIM_SCHEDULER_PHASE_RE_INACTIVE = 7,
    FSIM_SCHEDULER_PHASE_RE_UPDATE = 8
} fsim_scheduler_phase_t;

typedef enum fsim_sdf_delay_selection {
  FSIM_SDF_DELAY_MINIMUM = 0,
  FSIM_SDF_DELAY_TYPICAL = 1,
  FSIM_SDF_DELAY_MAXIMUM = 2
} fsim_sdf_delay_selection_t;

typedef enum fsim_sdf_phase {
  FSIM_SDF_PHASE_COMPILE = 0,
  FSIM_SDF_PHASE_ELABORATE = 1,
  FSIM_SDF_PHASE_SIMULATE = 2
} fsim_sdf_phase_t;

typedef enum fsim_sdf_report_kind {
  FSIM_SDF_REPORT_INPUT = 0,
  FSIM_SDF_REPORT_PATH = 1,
  FSIM_SDF_REPORT_TIMING_CHECK = 2
} fsim_sdf_report_kind_t;

typedef struct fsim_session_options {
  uint32_t struct_size;
  uint32_t api_version;
  uint64_t max_deltas;
  uint64_t seed;
  uint32_t flags;
  uint32_t reserved;
} fsim_session_options_t;

typedef struct fsim_diagnostic {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_severity_t severity;
  fsim_string_view_t code;
  fsim_string_view_t message;
  fsim_string_view_t path;
  uint32_t line;
  uint32_t column;
} fsim_diagnostic_t;

typedef struct fsim_object_info {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_object_t handle;
  fsim_object_t parent;
  fsim_object_kind_t kind;
  uint32_t flags;
  uint64_t width;
  fsim_string_view_t name;
  fsim_string_view_t full_name;
  fsim_string_view_t type_name;
  /*
   * Append-only v1 extension. Callers built against the original v1 prefix
   * may pass FSIM_OBJECT_INFO_V1_SIZE and these members are not accessed.
   */
  fsim_string_view_t source_path;
  uint32_t source_line;
  uint32_t source_column;
  /* Append-only semantic-owner provenance; cache/compiler details excluded. */
  fsim_string_view_t provenance_unit;
  fsim_string_view_t provenance_source_path;
  fsim_string_view_t provenance_language;
  fsim_string_view_t provenance_standard;
  fsim_string_view_t provenance_compatibility_profile;
  uint32_t provenance_unit_id;
  uint32_t provenance_source_id;
  uint32_t provenance_source_line;
  uint32_t provenance_source_column;
} fsim_object_info_t;

typedef struct fsim_mapped_library_info {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_string_view_t library;
  fsim_string_view_t metadata_digest;
  uint64_t unit_count;
  uint32_t native_accepted;
  uint32_t reserved;
  fsim_string_view_t native_kind;
  fsim_string_view_t native_fingerprint;
} fsim_mapped_library_info_t;

typedef struct fsim_sdf_input {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_string_view_t source_identity;
  fsim_string_view_t root;
  fsim_string_view_t cell_pattern;
  uint64_t file_precedence;
  uint64_t cell_precedence;
} fsim_sdf_input_t;

typedef struct fsim_sdf_options {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_sdf_delay_selection_t selection;
  fsim_sdf_phase_t phase;
  const fsim_sdf_input_t* inputs;
  size_t input_count;
  size_t report_limit;
} fsim_sdf_options_t;

typedef struct fsim_sdf_summary {
  uint32_t struct_size;
  uint32_t api_version;
  size_t input_count;
  size_t file_count;
  size_t applied_path_count;
  size_t applied_timing_check_count;
  size_t report_entry_count;
  size_t returned_report_entry_count;
  uint64_t generation;
  uint32_t effective;
  uint32_t report_truncated;
  fsim_string_view_t semantic_identity;
} fsim_sdf_summary_t;

typedef struct fsim_sdf_report_entry {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_sdf_report_kind_t kind;
  uint32_t reserved;
  fsim_string_view_t source_identity;
  fsim_string_view_t object_identity;
  fsim_string_view_t canonical_identity;
} fsim_sdf_report_entry_t;

typedef enum fsim_trace_format {
  FSIM_TRACE_FORMAT_AUTO = 0,
  FSIM_TRACE_FORMAT_VCD = 1,
  FSIM_TRACE_FORMAT_FST = 2
} fsim_trace_format_t;

typedef enum fsim_trace_compression {
  FSIM_TRACE_COMPRESSION_AUTO = 0,
  FSIM_TRACE_COMPRESSION_NONE = 1,
  FSIM_TRACE_COMPRESSION_DETERMINISTIC = 2
} fsim_trace_compression_t;

typedef enum fsim_trace_lifecycle {
  FSIM_TRACE_LIFECYCLE_DISABLED = 0,
  FSIM_TRACE_LIFECYCLE_CONFIGURED = 1,
  FSIM_TRACE_LIFECYCLE_OPEN = 2,
  FSIM_TRACE_LIFECYCLE_COMPLETE = 3,
  FSIM_TRACE_LIFECYCLE_FAILED = 4
} fsim_trace_lifecycle_t;

typedef enum fsim_trace_phase {
  FSIM_TRACE_PHASE_COMPILE = 0,
  FSIM_TRACE_PHASE_ELABORATE = 1,
  FSIM_TRACE_PHASE_SIMULATE = 2
} fsim_trace_phase_t;

typedef enum fsim_trace_report_kind {
  FSIM_TRACE_REPORT_OUTPUT = 0,
  FSIM_TRACE_REPORT_FORMAT = 1,
  FSIM_TRACE_REPORT_COMPRESSION = 2,
  FSIM_TRACE_REPORT_SELECTION = 3,
  FSIM_TRACE_REPORT_LIFECYCLE = 4
} fsim_trace_report_kind_t;

typedef struct fsim_trace_options {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_trace_format_t format;
  fsim_trace_compression_t compression;
  fsim_trace_lifecycle_t lifecycle;
  fsim_trace_phase_t phase;
  fsim_string_view_t output;
  const fsim_string_view_t* selections;
  size_t selection_count;
  size_t report_limit;
  uint64_t generation;
} fsim_trace_options_t;

typedef struct fsim_trace_status {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_trace_format_t requested_format;
  fsim_trace_format_t effective_format;
  fsim_trace_compression_t requested_compression;
  fsim_trace_compression_t effective_compression;
  fsim_trace_lifecycle_t lifecycle;
  uint32_t reserved;
  size_t selection_count;
  size_t report_entry_count;
  size_t returned_report_entry_count;
  uint64_t generation;
  uint32_t report_truncated;
  uint32_t reserved2;
  fsim_string_view_t output;
  fsim_string_view_t semantic_identity;
} fsim_trace_status_t;

typedef struct fsim_trace_report_entry {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_trace_report_kind_t kind;
  uint32_t reserved;
  fsim_string_view_t name;
  fsim_string_view_t value;
  fsim_string_view_t canonical_identity;
} fsim_trace_report_entry_t;

#define FSIM_STRUCT_HEADER_SIZE \
  (offsetof(fsim_session_options_t, max_deltas))
#define FSIM_OBJECT_INFO_V1_SIZE \
  (offsetof(fsim_object_info_t, source_path))

/*
 * Executable source safe points carry an FSIM_OBJECT_PROCESS handle.
 * Scheduler-only delta/time boundaries use FSIM_INVALID_OBJECT.
 */
typedef void (*fsim_safe_point_callback_t)(
    fsim_session_t session,
    fsim_object_t process,
    fsim_time_t time,
    uint64_t delta,
    void* user_data);

typedef void (*fsim_value_change_callback_t)(
    fsim_session_t session,
    fsim_object_t object,
    fsim_time_t time,
    uint64_t delta,
    void* user_data);

typedef void (*fsim_assertion_callback_t)(
    fsim_session_t session,
    fsim_object_t process,
    const fsim_diagnostic_t* diagnostic,
    void* user_data);

typedef void (*fsim_lifecycle_callback_t)(
    fsim_session_t session,
    fsim_lifecycle_event_t event,
    void* user_data);

typedef struct fsim_safe_point_info {
  uint32_t struct_size;
  uint32_t api_version;
  fsim_object_t process;
  fsim_time_t time;
  uint64_t delta;
  uint64_t instruction;
  fsim_safe_point_kind_t kind;
  fsim_scheduler_phase_t phase;
  fsim_string_view_t source_path;
  uint32_t source_line;
  uint32_t source_column;
  fsim_string_view_t provenance_unit;
  fsim_string_view_t provenance_source_path;
  fsim_string_view_t provenance_language;
  fsim_string_view_t provenance_standard;
  fsim_string_view_t provenance_compatibility_profile;
  uint32_t provenance_unit_id;
  uint32_t provenance_source_id;
  uint32_t provenance_source_line;
  uint32_t provenance_source_column;
} fsim_safe_point_info_t;

typedef void (*fsim_safe_point_info_callback_t)(
    fsim_session_t session,
    const fsim_safe_point_info_t* info,
    void* user_data);

typedef struct fsim_callbacks {
  uint32_t struct_size;
  uint32_t api_version;
  void* user_data;
  fsim_safe_point_callback_t safe_point;
  fsim_value_change_callback_t value_change;
  fsim_assertion_callback_t assertion;
  fsim_lifecycle_callback_t lifecycle;
  /* Append-only v1 extension; omitted by FSIM_CALLBACKS_V1_SIZE callers. */
  fsim_safe_point_info_callback_t safe_point_info;
} fsim_callbacks_t;

#define FSIM_CALLBACKS_V1_SIZE \
  (offsetof(fsim_callbacks_t, safe_point_info))

// Return nonzero to continue enumeration and zero to stop successfully.
typedef int (*fsim_visit_object_callback_t)(
    fsim_session_t session,
    fsim_object_t object,
    void* user_data);

FSIM_PUBLIC uint32_t fsim_get_api_version(void);
FSIM_PUBLIC const char* fsim_status_string(fsim_status_t status);

FSIM_PUBLIC fsim_status_t fsim_session_create(
    const fsim_session_options_t* options,
    fsim_session_t* out_session);
FSIM_PUBLIC fsim_status_t fsim_session_destroy(fsim_session_t session);

FSIM_PUBLIC fsim_status_t fsim_session_load_project(
    fsim_session_t session,
    const char* manifest_path);
FSIM_PUBLIC fsim_status_t fsim_session_check(fsim_session_t session);
FSIM_PUBLIC fsim_status_t fsim_session_build(fsim_session_t session);

FSIM_PUBLIC fsim_status_t fsim_session_configure_sdf(
    fsim_session_t session,
    const fsim_sdf_options_t* options);
FSIM_PUBLIC fsim_status_t fsim_session_get_sdf_summary(
    fsim_session_t session,
    fsim_sdf_summary_t* out_summary);
FSIM_PUBLIC fsim_status_t fsim_session_get_sdf_report_entry(
    fsim_session_t session,
    size_t index,
    fsim_sdf_report_entry_t* out_entry);

FSIM_PUBLIC fsim_status_t fsim_session_configure_trace(
    fsim_session_t session,
    const fsim_trace_options_t* options);
FSIM_PUBLIC fsim_status_t fsim_session_get_trace_status(
    fsim_session_t session,
    fsim_trace_status_t* out_status);
FSIM_PUBLIC fsim_status_t fsim_session_get_trace_report_entry(
    fsim_session_t session,
    size_t index,
    fsim_trace_report_entry_t* out_entry);
FSIM_PUBLIC fsim_status_t fsim_session_flush_trace(fsim_session_t session);
FSIM_PUBLIC fsim_status_t fsim_session_close_trace(fsim_session_t session);

FSIM_PUBLIC fsim_status_t fsim_session_root(
    fsim_session_t session,
    fsim_object_t* out_root);
FSIM_PUBLIC fsim_status_t fsim_session_find_object(
    fsim_session_t session,
    fsim_string_view_t path,
    fsim_object_t* out_object);
FSIM_PUBLIC fsim_status_t fsim_session_visit_children(
    fsim_session_t session,
    fsim_object_t parent,
    fsim_visit_object_callback_t callback,
    void* user_data);
FSIM_PUBLIC fsim_status_t fsim_session_get_object_info(
    fsim_session_t session,
    fsim_object_t object,
    fsim_object_info_t* out_info);
FSIM_PUBLIC fsim_status_t fsim_session_mapped_library_count(
    fsim_session_t session,
    size_t* out_count);
FSIM_PUBLIC fsim_status_t fsim_session_get_mapped_library_info(
    fsim_session_t session,
    size_t index,
    fsim_mapped_library_info_t* out_info);

// Values use fsim's canonical textual representation. If buffer is NULL or too
// small, out_required receives the required byte count including the trailing
// NUL and FSIM_STATUS_INVALID_ARGUMENT is returned.
// A process variable whose lexical block has never been entered returns
// FSIM_STATUS_UNAVAILABLE and sets out_required to zero.
FSIM_PUBLIC fsim_status_t fsim_session_read_value(
    fsim_session_t session,
    fsim_object_t object,
    char* buffer,
    size_t buffer_size,
    size_t* out_required);
FSIM_PUBLIC fsim_status_t fsim_session_deposit(
    fsim_session_t session,
    fsim_object_t object,
    fsim_string_view_t value);
FSIM_PUBLIC fsim_status_t fsim_session_force(
    fsim_session_t session,
    fsim_object_t object,
    fsim_string_view_t value);
FSIM_PUBLIC fsim_status_t fsim_session_release(
    fsim_session_t session,
    fsim_object_t object);

FSIM_PUBLIC fsim_status_t fsim_session_run(
    fsim_session_t session,
    fsim_time_t until_time);
FSIM_PUBLIC fsim_status_t fsim_session_step(
    fsim_session_t session,
    fsim_step_kind_t kind);
FSIM_PUBLIC fsim_status_t fsim_session_request_stop(fsim_session_t session);
FSIM_PUBLIC fsim_status_t fsim_session_set_callbacks(
    fsim_session_t session,
    const fsim_callbacks_t* callbacks);

// Returned string views remain valid until the next call that mutates the
// session. index 0 is the oldest diagnostic.
FSIM_PUBLIC fsim_status_t fsim_session_get_diagnostic(
    fsim_session_t session,
    size_t index,
    fsim_diagnostic_t* out_diagnostic);
FSIM_PUBLIC fsim_status_t fsim_session_diagnostic_count(
    fsim_session_t session,
    size_t* out_count);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // FSIM_API_H
