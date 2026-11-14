// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace fsim::support {

/// What a declaration of the governed UVM library is to the simulation's
/// native UVM services (IEEE 1800.2-2017, IEEE 1800.2-2020). The role is
/// recorded on the declaration once, when the package `uvm_pkg` of a source
/// set compiled with a governed UVM release (`--uvm-release`) is parsed;
/// later stages read the role from the declaration (the frontend class, the
/// HIR class, method and declaration, the class specialization) and never
/// identify a library declaration by its spelling. `none` marks every
/// declaration outside the governed library, `member` one of the library
/// with no role of its own.
enum class UvmRole : std::uint8_t {
    none,
    member,
    // Classes.
    object,
    policy,
    component,
    root,
    report_object,
    phase,
    factory,
    coreservice,
    default_coreservice,
    cmdline_processor,
    object_registry,
    component_registry,
    tree_printer,
    line_printer,
    port_base,
    analysis_port,
    analysis_imp,
    sequence_base,
    seq_item_pull_port,
    seq_item_pull_imp,
    test_done_objection,
    resource_pool,
    config_db,
    resource_db,
    driver,
    sequencer,
    sequencer_param_base,
    seq_item_pull_export,
    printer_knobs,
    // Methods of the library classes and functions of the package.
    report_enabled,
    report_info,
    report_warning,
    report_error,
    report_fatal,
    get_report_object,
    run_test,
    singleton_get,
    registry_create,
    database_set,
    database_get,
    database_exists,
    database_read_by_name,
    database_write_by_name,
    object_clone,
    object_do_copy,
    analysis_port_write,
    // The phase callbacks and hooks of uvm_component (IEEE 1800.2-2020
    // 13.1.3, 13.1.4), in support::uvm_api_function_methods order.
    phase_started,
    phase_ready_to_end,
    phase_ended,
    build_phase,
    connect_phase,
    end_of_elaboration_phase,
    start_of_simulation_phase,
    extract_phase,
    check_phase,
    report_phase,
    final_phase,
    run_phase,
    pre_reset_phase,
    reset_phase,
    post_reset_phase,
    pre_configure_phase,
    configure_phase,
    post_configure_phase,
    pre_main_phase,
    main_phase,
    post_main_phase,
    pre_shutdown_phase,
    shutdown_phase,
    post_shutdown_phase,
    // The factory methods that create an object (IEEE 1800.2-2020 8.3.1).
    factory_create_object_by_type,
    factory_create_component_by_type,
    factory_create_object_by_name,
    factory_create_component_by_name,
    factory_set_type_override_by_type,
    factory_find_override_by_type,
    factory_print,
    object_get_inst_id,
    object_copy,
    object_compare,
    object_print,
    object_record,
    object_get_object_type,
    object_get_type,
    get_name,
    get_full_name,
    get_type_name,
    component_get_parent,
    component_get_num_children,
    component_get_child,
    component_has_child,
    component_get_first_child,
    component_get_next_child,
    component_get_children,
    component_lookup,
    root_find,
    root_find_all,
    root_print_topology,
    phase_raise_objection,
    phase_drop_objection,
    port_connect,
    sequence_start,
    sequence_start_item,
    sequence_finish_item,
    sequence_pre_start,
    sequence_pre_body,
    sequence_body,
    sequence_post_body,
    sequence_post_start,
    pull_get,
    pull_get_next_item,
    pull_try_next_item,
    pull_peek,
    pull_item_done,
    report_get_verbosity_level,
    report_get_action,
    // Class properties the native services read.
    coreservice_inst,
    driver_seq_item_port,
    driver_rsp_port,
    sequencer_seq_item_export,
    sequencer_rsp_export,
    printer_newline,
    printer_knobs_handle,
    knobs_header,
    knobs_footer,
    knobs_full_name,
    knobs_identifier,
    knobs_type_name,
    knobs_size,
    knobs_depth,
    knobs_reference,
    knobs_prefix,
    knobs_indent,
    knobs_show_root,
    knobs_separator,
    // Variables and types of the package.
    top,
    cmdline_proc,
    test_done,
    resources,
    default_printer,
    port_type_enum,
    active_passive_enum,
    // The registry's static `get` (IEEE 1800.2-2020 8.2.4.2.1, 8.2.5.2.1),
    // after the package's roles: an artifact keeps each role's value.
    registry_get,
};

/// Whether `role` is a value of the enumeration (a decoded artifact field).
[[nodiscard]] constexpr bool valid_uvm_role(const UvmRole role) noexcept
{
    return role <= UvmRole::registry_get;
}

/// The role of a package-level declaration of the library package, by its
/// declared name in that package.
struct UvmDeclarationRole {
    std::string name;
    UvmRole role { };
};

/// The role `name` has in `roles`: `member` when it has none of its own.
template <typename Roles, typename Name>
[[nodiscard]] UvmRole declared_uvm_role(const Roles& roles, const Name& name)
{
    for (const auto& entry : roles) {
        if (entry.name == name)
            return entry.role;
    }
    return UvmRole::member;
}

/// Whether a declaration with role `role` belongs to the governed library.
[[nodiscard]] constexpr bool uvm_library(const UvmRole role) noexcept
{
    return role != UvmRole::none;
}

} // namespace fsim::support
