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

typedef enum fsim_sc_construction_type_v1 {
    FSIM_SC_CONSTRUCTION_INTEGER = 0,
    FSIM_SC_CONSTRUCTION_NATURAL = 1,
    FSIM_SC_CONSTRUCTION_POSITIVE = 2,
    FSIM_SC_CONSTRUCTION_BOOLEAN = 3,
    FSIM_SC_CONSTRUCTION_BIT = 4
} fsim_sc_construction_type_v1;

typedef enum fsim_sc_notification_kind_v1 {
    FSIM_SC_NOTIFY_IMMEDIATE = 0,
    FSIM_SC_NOTIFY_DELTA = 1,
    FSIM_SC_NOTIFY_TIMED = 2
} fsim_sc_notification_kind_v1;

typedef enum fsim_sc_event_list_kind_v1 {
    FSIM_SC_EVENT_OR_LIST = 0,
    FSIM_SC_EVENT_AND_LIST = 1
} fsim_sc_event_list_kind_v1;

typedef enum fsim_sc_metadata_category_v1 {
    FSIM_SC_METADATA_PORT = 0,
    FSIM_SC_METADATA_EXPORT = 1
} fsim_sc_metadata_category_v1;

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
typedef void (*fsim_sc_channel_update_v1)(void* user);
typedef void (*fsim_sc_lifecycle_entry_v1)(void* user);
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
    fsim_sc_status_v1 (*cancel_event)(
        void* context,
        fsim_sc_handle_v1 event);
    fsim_sc_status_v1 (*wait_event_list)(
        void* context,
        const fsim_sc_handle_v1* events,
        size_t event_count,
        fsim_sc_event_list_kind_v1 kind);

    /*
     * Append-only delayed-notification and primitive-channel surface.
     * notify_event_delayed differs from notify_event_mode by rejecting an
     * event which already has a pending delta or timed notification.
     * Primitive-channel updates are deduplicated and run in the common update
     * phase. The callback must not throw across this C ABI.
     */
    fsim_sc_status_v1 (*notify_event_delayed)(
        void* context,
        fsim_sc_handle_v1 event,
        uint64_t delay_femtoseconds);
    fsim_sc_status_v1 (*register_primitive_channel)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_channel_update_v1 update,
        void* user,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*request_update)(
        void* context,
        fsim_sc_handle_v1 channel);

    /*
     * Append-only typed primitive-signal attachment. `channel` must have
     * already been registered through register_primitive_channel; attaching
     * value metadata makes the same handle a common-runtime signal object.
     */
    fsim_sc_status_v1 (*register_signal)(
        void* context,
        fsim_sc_handle_v1 module,
        fsim_sc_handle_v1 channel,
        const char* name,
        fsim_sc_value_encoding_v1 encoding,
        uint32_t width,
        const fsim_sc_value_view_v1* initial_value);
    fsim_sc_status_v1 (*value_changed)(
        void* context,
        fsim_sc_handle_v1 object,
        uint8_t* result);

    /*
     * Append-only elaboration-time port/object binding. Handles must have
     * identical value metadata. A port may bind a signal owned by the same
     * module or its direct native parent, or a port owned by that parent.
     */
    fsim_sc_status_v1 (*bind_port)(
        void* context,
        fsim_sc_handle_v1 port,
        fsim_sc_handle_v1 channel);

    /*
     * Append-only native SystemC hierarchy construction. The child remains
     * owned by the root factory object; this callback only allocates its
     * stable hierarchy handle and establishes the parent relationship.
     */
    fsim_sc_status_v1 (*register_native_module)(
        void* context,
        fsim_sc_handle_v1 parent,
        const char* name,
        fsim_sc_handle_v1* result);

    /*
     * Append-only lifecycle registration for one factory root. The callbacks
     * must not throw across the C ABI. Native children are dispatched
     * recursively by the root callback.
     */
    fsim_sc_status_v1 (*register_lifecycle)(
        void* context,
        fsim_sc_handle_v1 module,
        fsim_sc_lifecycle_entry_v1 before_end_of_elaboration,
        fsim_sc_lifecycle_entry_v1 end_of_elaboration,
        fsim_sc_lifecycle_entry_v1 start_of_simulation,
        fsim_sc_lifecycle_entry_v1 end_of_simulation,
        void* user);

    /*
     * Append-only typed export hierarchy. An export may bind a compatible
     * signal or another export in the same module or its direct parent.
     */
    fsim_sc_status_v1 (*register_export)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_value_encoding_v1 encoding,
        uint32_t width,
        fsim_sc_handle_v1* result);
    fsim_sc_status_v1 (*bind_export)(
        void* context,
        fsim_sc_handle_v1 export_handle,
        fsim_sc_handle_v1 target);

    /* Append-only fiber suspension extension for plain wait(). */
    fsim_sc_status_v1 (*wait_static)(void* context);

    /*
     * Append-only foreign-HDL construction actual. Values are immutable
     * signed scalar constants consumed during common elaboration and become
     * part of the selected HDL specialization identity.
     */
    fsim_sc_status_v1 (*set_foreign_child_actual)(
        void* context,
        fsim_sc_handle_v1 child,
        const char* name,
        int64_t value);

    /*
     * Append-only lookup of the canonical construction value selected for
     * the module currently being built. Factory code may query only names
     * declared in its registered schema.
     */
    fsim_sc_status_v1 (*get_construction_value)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        int64_t* result);

    /*
     * Append-only export capability metadata. Legacy plug-ins which omit this
     * callback retain the original read/write-capable behavior. New facade
     * code sets zero for read-only signal interfaces and one for inout
     * interfaces before binding the export.
     */
    fsim_sc_status_v1 (*set_export_writable)(
        void* context,
        fsim_sc_handle_v1 export_handle,
        uint8_t writable);

    /*
     * Append-only metadata-only custom-interface objects. These records have
     * hierarchy identity but deliberately have no common-kernel value or
     * binding behavior.
     */
    fsim_sc_status_v1 (*register_metadata_object)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_metadata_category_v1 category,
        const char* kind,
        fsim_sc_handle_v1* result);

    /* Append-only custom kind label for a metadata-only primitive channel. */
    fsim_sc_status_v1 (*set_primitive_channel_kind)(
        void* context,
        fsim_sc_handle_v1 channel,
        const char* kind);

    /* Append-only timed event/list wait used by next_trigger and wait. */
    fsim_sc_status_v1 (*wait_event_timeout)(
        void* context,
        uint64_t femtoseconds,
        const fsim_sc_handle_v1* events,
        size_t event_count,
        fsim_sc_event_list_kind_v1 kind);
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

    /*
     * Append-only typed construction schema. Register the factory first,
     * then its parameters in declaration order. A missing default is encoded
     * by has_default == 0; default_value is ignored in that case.
     */
    fsim_sc_status_v1 (*register_factory_parameter)(
        void* context,
        const char* factory,
        const char* name,
        fsim_sc_construction_type_v1 type,
        uint8_t has_default,
        int64_t default_value);
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
