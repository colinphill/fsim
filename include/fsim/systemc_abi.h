// SPDX-License-Identifier: Apache-2.0
#ifndef FSIM_SYSTEMC_ABI_H
#define FSIM_SYSTEMC_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define FSIM_SC_EXPORT __declspec(dllexport)
#else
#  define FSIM_SC_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_SYSTEMC_ABI_VERSION 1u

typedef uint64_t fsim_sc_handle_v1;

typedef enum fsim_sc_status_v1 {
    FSIM_SC_OK = 0,
    FSIM_SC_INVALID_ARGUMENT = 1,
    FSIM_SC_ABI_MISMATCH = 2,
    FSIM_SC_NOT_SUPPORTED = 3,
    FSIM_SC_RUNTIME_ERROR = 4
} fsim_sc_status_v1;

typedef enum fsim_sc_process_kind_v1 {
    FSIM_SC_METHOD = 0,
    FSIM_SC_THREAD = 1,
    FSIM_SC_CTHREAD = 2
} fsim_sc_process_kind_v1;

typedef enum fsim_sc_edge_kind_v1 {
    FSIM_SC_ANY_EDGE = 0,
    FSIM_SC_POSEDGE = 1,
    FSIM_SC_NEGEDGE = 2
} fsim_sc_edge_kind_v1;

typedef enum fsim_sc_port_direction_v1 {
    FSIM_SC_INPUT = 0,
    FSIM_SC_OUTPUT = 1,
    FSIM_SC_INOUT = 2
} fsim_sc_port_direction_v1;

typedef enum fsim_sc_value_encoding_v1 {
    FSIM_SC_BIT2 = 0,
    FSIM_SC_LOGIC4 = 1,
    FSIM_SC_SIGNED = 2,
    FSIM_SC_UNSIGNED = 3
} fsim_sc_value_encoding_v1;

typedef enum fsim_sc_notification_kind_v1 {
    FSIM_SC_NOTIFY_IMMEDIATE = 0,
    FSIM_SC_NOTIFY_DELTA = 1,
    FSIM_SC_NOTIFY_TIMED = 2
} fsim_sc_notification_kind_v1;

typedef struct fsim_sc_value_view_v1 {
    /*
     * Packed planes are byte-addressed, least-significant bit first.
     * FSIM_SC_BIT2 uses one value plane. LOGIC4/SIGNED/UNSIGNED use an
     * aval plane followed by an equally sized bval plane: 00=0, 10=1,
     * 11=X, 01=Z for each corresponding bit.
     */
    uint32_t struct_size;
    fsim_sc_value_encoding_v1 encoding;
    uint32_t width;
    const uint8_t* data;
    size_t data_size;
} fsim_sc_value_view_v1;

typedef void (*fsim_sc_process_entry_v1)(void* user);
typedef void* (*fsim_sc_module_factory_v1)(
    void* user, const char* instance_name, fsim_sc_handle_v1 parent);
typedef void (*fsim_sc_module_destroy_v1)(void* user, void* module);
typedef fsim_sc_status_v1 (*fsim_sc_module_elaborate_v1)(
    void* user,
    const char* instance_name,
    fsim_sc_handle_v1 module,
    fsim_sc_handle_v1 parent,
    void** result);

typedef struct fsim_sc_host_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;

    fsim_sc_status_v1 (*register_port)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_port_direction_v1 direction,
        fsim_sc_value_encoding_v1 encoding,
        uint32_t width,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*register_process)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_process_kind_v1 kind,
        fsim_sc_process_entry_v1 entry,
        void* user,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*add_sensitivity)(
        void* context,
        fsim_sc_handle_v1 process,
        fsim_sc_handle_v1 object,
        fsim_sc_edge_kind_v1 edge);
    fsim_sc_status_v1 (*read_value)(
        void* context, fsim_sc_handle_v1 object, fsim_sc_value_view_v1* result);
    fsim_sc_status_v1 (*write_value)(
        void* context, fsim_sc_handle_v1 object, const fsim_sc_value_view_v1* value);
    fsim_sc_status_v1 (*wait_time)(
        void* context, uint64_t delay_femtoseconds);
    fsim_sc_status_v1 (*wait_event)(void* context, fsim_sc_handle_v1 event);
    fsim_sc_status_v1 (*notify_event)(
        void* context,
        fsim_sc_handle_v1 event,
        uint64_t delay_femtoseconds);
    void (*report)(void* context, int severity, const char* message);

    /*
     * Append-only v1 elaboration extension. A foreign child is a named,
     * elaboration-time placeholder whose implementation is selected by the
     * manifest. Its ports connect to objects already registered on `module`.
     */
    fsim_sc_status_v1 (*register_foreign_child)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*connect_foreign_port)(
        void* context,
        fsim_sc_handle_v1 child,
        const char* name,
        fsim_sc_port_direction_v1 direction,
        fsim_sc_value_encoding_v1 encoding,
        uint32_t width,
        fsim_sc_handle_v1 object);

    /*
     * Append-only process property. SC_METHOD processes initialize once at
     * time zero by default; a module calls this with zero for
     * dont_initialize().
     */
    fsim_sc_status_v1 (*set_process_initialize)(
        void* context,
        fsim_sc_handle_v1 process,
        uint8_t initialize);

    /*
     * Append-only event surface. Time arguments are exact femtoseconds at
     * this ABI; the host converts them to the elaborated global tick.
     */
    fsim_sc_status_v1 (*register_event)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*notify_event_mode)(
        void* context,
        fsim_sc_handle_v1 event,
        uint64_t delay_femtoseconds,
        fsim_sc_notification_kind_v1 kind);
} fsim_sc_host_v1;

typedef struct fsim_sc_registrar_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;
    fsim_sc_status_v1 (*register_factory)(
        void* context,
        const char* name,
        fsim_sc_module_factory_v1 factory,
        fsim_sc_module_destroy_v1 destroy,
        void* user);

    /*
     * Append-only v1 factory form for integrated elaboration. Unlike the
     * legacy factory, this callback receives the newly allocated module
     * handle separately from its parent and returns status explicitly.
     */
    fsim_sc_status_v1 (*register_elaboration_factory)(
        void* context,
        const char* name,
        fsim_sc_module_elaborate_v1 factory,
        fsim_sc_module_destroy_v1 destroy,
        void* user);
} fsim_sc_registrar_v1;

typedef fsim_sc_status_v1 (*fsim_plugin_init_v1_fn)(
    const fsim_sc_host_v1* host, fsim_sc_registrar_v1* registrar);

/*
 * Every fsim SystemC plug-in implements this single exported entry point.
 * Keeping the declaration in the ABI header ensures it remains visible even
 * when plug-ins are compiled with -fvisibility=hidden.
 */
FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host, fsim_sc_registrar_v1* registrar);

#ifdef __cplusplus
}
#endif

#endif
