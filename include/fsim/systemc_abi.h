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

#define FSIM_SYSTEMC_ABI_VERSION 4u

typedef uint64_t fsim_sc_handle_v1;

typedef enum fsim_sc_status_v1 {
    FSIM_SC_OK = 0,
    FSIM_SC_INVALID_ARGUMENT = 1,
    FSIM_SC_ABI_MISMATCH = 2,
    FSIM_SC_NOT_SUPPORTED = 3,
    FSIM_SC_RUNTIME_ERROR = 4
} fsim_sc_status_v1;

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
typedef void (*fsim_sc_lifecycle_entry_v1)(void* user);
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
    void (*report)(void* context, int severity, const char* message);

    /*
     * Register one typed Accellera signal exposed to the common application
     * runtime. Native channel scheduling remains owned by Accellera SystemC.
     */
    fsim_sc_status_v1 (*register_signal)(
        void* context,
        fsim_sc_handle_v1 module,
        const char* name,
        fsim_sc_value_encoding_v1 encoding,
        uint32_t width,
        const fsim_sc_value_view_v1* initial_value,
        fsim_sc_handle_v1* result);

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
     * Export capability metadata. Facade code sets zero for read-only signal
     * interfaces and one for inout interfaces before binding the export.
     */
    fsim_sc_status_v1 (*set_export_writable)(
        void* context,
        fsim_sc_handle_v1 export_handle,
        uint8_t writable);

    /* Suspend until a boundary input changes or Accellera next has activity. */
    fsim_sc_status_v1 (*wait_for_input_or_native_activity)(
        void* context, uint64_t delay_femtoseconds);

    /* Exact common-kernel time visible during an active process callback. */
    fsim_sc_status_v1 (*current_time_femtoseconds)(
        void* context,
        uint64_t* result);

    /* Complete SCV/SystemC/TLM/compiler producer identity for this process. */
    const char* scv_compatibility_identity;
} fsim_sc_host_v1;

typedef struct fsim_sc_registrar_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;

    /*
     * Integrated factory construction receives the newly allocated module
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
