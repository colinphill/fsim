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
  FSIM_OBJECT_DRIVER = 7
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
} fsim_object_info_t;

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

typedef struct fsim_callbacks {
  uint32_t struct_size;
  uint32_t api_version;
  void* user_data;
  fsim_safe_point_callback_t safe_point;
  fsim_value_change_callback_t value_change;
  fsim_assertion_callback_t assertion;
  fsim_lifecycle_callback_t lifecycle;
} fsim_callbacks_t;

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

// Values use fsim's canonical textual representation. If buffer is NULL or too
// small, out_required receives the required byte count including the trailing
// NUL and FSIM_STATUS_INVALID_ARGUMENT is returned.
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
