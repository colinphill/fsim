// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/uvm_roles.hpp"

#include <array>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::frontend {
namespace {

using support::UvmRole;

// The classes of the library whose objects the native UVM services own or
// answer for (IEEE 1800.2-2020 5-18, IEEE 1800.2-2017 the same clauses).
constexpr std::array class_roles {
    std::pair { std::string_view { "uvm_object" }, UvmRole::object },
    std::pair { std::string_view { "uvm_policy" }, UvmRole::policy },
    std::pair { std::string_view { "uvm_component" }, UvmRole::component },
    std::pair { std::string_view { "uvm_root" }, UvmRole::root },
    std::pair { std::string_view { "uvm_report_object" },
        UvmRole::report_object },
    std::pair { std::string_view { "uvm_phase" }, UvmRole::phase },
    std::pair { std::string_view { "uvm_factory" }, UvmRole::factory },
    std::pair { std::string_view { "uvm_coreservice_t" },
        UvmRole::coreservice },
    std::pair { std::string_view { "uvm_default_coreservice_t" },
        UvmRole::default_coreservice },
    std::pair { std::string_view { "uvm_cmdline_processor" },
        UvmRole::cmdline_processor },
    std::pair { std::string_view { "uvm_object_registry" },
        UvmRole::object_registry },
    std::pair { std::string_view { "uvm_component_registry" },
        UvmRole::component_registry },
    std::pair { std::string_view { "uvm_tree_printer" },
        UvmRole::tree_printer },
    std::pair { std::string_view { "uvm_line_printer" },
        UvmRole::line_printer },
    std::pair { std::string_view { "uvm_port_base" }, UvmRole::port_base },
    std::pair { std::string_view { "uvm_analysis_port" },
        UvmRole::analysis_port },
    std::pair { std::string_view { "uvm_analysis_imp" },
        UvmRole::analysis_imp },
    std::pair { std::string_view { "uvm_sequence_base" },
        UvmRole::sequence_base },
    std::pair { std::string_view { "uvm_seq_item_pull_port" },
        UvmRole::seq_item_pull_port },
    std::pair { std::string_view { "uvm_seq_item_pull_imp" },
        UvmRole::seq_item_pull_imp },
    std::pair { std::string_view { "uvm_test_done_objection" },
        UvmRole::test_done_objection },
    std::pair { std::string_view { "uvm_resource_pool" },
        UvmRole::resource_pool },
    std::pair { std::string_view { "uvm_config_db" }, UvmRole::config_db },
    std::pair { std::string_view { "uvm_resource_db" },
        UvmRole::resource_db },
    std::pair { std::string_view { "uvm_driver" }, UvmRole::driver },
    std::pair { std::string_view { "uvm_sequencer" }, UvmRole::sequencer },
    std::pair { std::string_view { "uvm_sequencer_param_base" },
        UvmRole::sequencer_param_base },
    std::pair { std::string_view { "uvm_seq_item_pull_export" },
        UvmRole::seq_item_pull_export },
    std::pair { std::string_view { "uvm_printer_knobs" },
        UvmRole::printer_knobs },
};

template <std::size_t Size>
[[nodiscard]] UvmRole find_role(
    const std::array<std::pair<std::string_view, UvmRole>, Size>& roles,
    const std::string_view name)
{
    for (const auto& [declared, role] : roles) {
        if (declared == name)
            return role;
    }
    return UvmRole::member;
}

// A member of a library class the native services implement or read, by
// the role of its class; `none` as the owner: a member of that name of any
// library class (an override keeps the role of the method it overrides,
// IEEE 1800-2023 8.20). A role of the class itself is found first.
struct MemberRole {
    UvmRole owner;
    std::string_view name;
    UvmRole role;
};

constexpr std::array method_roles {
    MemberRole { UvmRole::none, "uvm_report_enabled", UvmRole::report_enabled },
    MemberRole { UvmRole::none, "uvm_report_info", UvmRole::report_info },
    MemberRole { UvmRole::none, "uvm_report_warning", UvmRole::report_warning },
    MemberRole { UvmRole::none, "uvm_report_error", UvmRole::report_error },
    MemberRole { UvmRole::none, "uvm_report_fatal", UvmRole::report_fatal },
    MemberRole { UvmRole::none, "uvm_get_report_object", UvmRole::get_report_object },
    MemberRole { UvmRole::none, "get_report_verbosity_level", UvmRole::report_get_verbosity_level },
    MemberRole { UvmRole::none, "get_report_action", UvmRole::report_get_action },
    MemberRole { UvmRole::root, "get", UvmRole::singleton_get },
    MemberRole { UvmRole::cmdline_processor, "get_inst", UvmRole::singleton_get },
    MemberRole { UvmRole::test_done_objection, "get", UvmRole::singleton_get },
    MemberRole { UvmRole::resource_pool, "get", UvmRole::singleton_get },
    MemberRole { UvmRole::object_registry, "create", UvmRole::registry_create },
    MemberRole { UvmRole::component_registry, "create", UvmRole::registry_create },
    MemberRole { UvmRole::object_registry, "get", UvmRole::registry_get },
    MemberRole { UvmRole::component_registry, "get", UvmRole::registry_get },
    MemberRole { UvmRole::config_db, "set", UvmRole::database_set },
    MemberRole { UvmRole::config_db, "get", UvmRole::database_get },
    MemberRole { UvmRole::config_db, "exists", UvmRole::database_exists },
    MemberRole { UvmRole::resource_db, "set", UvmRole::database_set },
    MemberRole { UvmRole::resource_db, "read_by_name", UvmRole::database_read_by_name },
    MemberRole { UvmRole::resource_db, "write_by_name", UvmRole::database_write_by_name },
    MemberRole { UvmRole::factory, "create_object_by_type", UvmRole::factory_create_object_by_type },
    MemberRole { UvmRole::factory, "create_component_by_type", UvmRole::factory_create_component_by_type },
    MemberRole { UvmRole::factory, "create_object_by_name", UvmRole::factory_create_object_by_name },
    MemberRole { UvmRole::factory, "create_component_by_name", UvmRole::factory_create_component_by_name },
    MemberRole { UvmRole::factory, "set_type_override_by_type", UvmRole::factory_set_type_override_by_type },
    MemberRole { UvmRole::factory, "find_override_by_type", UvmRole::factory_find_override_by_type },
    MemberRole { UvmRole::factory, "print", UvmRole::factory_print },
    MemberRole { UvmRole::object, "clone", UvmRole::object_clone },
    MemberRole { UvmRole::object, "do_copy", UvmRole::object_do_copy },
    MemberRole { UvmRole::object, "get_inst_id", UvmRole::object_get_inst_id },
    MemberRole { UvmRole::object, "copy", UvmRole::object_copy },
    MemberRole { UvmRole::object, "compare", UvmRole::object_compare },
    MemberRole { UvmRole::object, "print", UvmRole::object_print },
    MemberRole { UvmRole::object, "record", UvmRole::object_record },
    MemberRole { UvmRole::object, "get_object_type", UvmRole::object_get_object_type },
    MemberRole { UvmRole::object, "get_type", UvmRole::object_get_type },
    MemberRole { UvmRole::none, "get_name", UvmRole::get_name },
    MemberRole { UvmRole::none, "get_full_name", UvmRole::get_full_name },
    MemberRole { UvmRole::none, "get_type_name", UvmRole::get_type_name },
    MemberRole { UvmRole::none, "get_parent", UvmRole::component_get_parent },
    MemberRole { UvmRole::none, "get_num_children", UvmRole::component_get_num_children },
    MemberRole { UvmRole::none, "get_child", UvmRole::component_get_child },
    MemberRole { UvmRole::none, "has_child", UvmRole::component_has_child },
    MemberRole { UvmRole::none, "get_first_child", UvmRole::component_get_first_child },
    MemberRole { UvmRole::none, "get_next_child", UvmRole::component_get_next_child },
    MemberRole { UvmRole::none, "get_children", UvmRole::component_get_children },
    MemberRole { UvmRole::none, "lookup", UvmRole::component_lookup },
    MemberRole { UvmRole::none, "find", UvmRole::root_find },
    MemberRole { UvmRole::none, "find_all", UvmRole::root_find_all },
    MemberRole { UvmRole::none, "print_topology", UvmRole::root_print_topology },
    MemberRole { UvmRole::phase, "raise_objection", UvmRole::phase_raise_objection },
    MemberRole { UvmRole::phase, "drop_objection", UvmRole::phase_drop_objection },
    MemberRole { UvmRole::none, "connect", UvmRole::port_connect },
    MemberRole { UvmRole::sequence_base, "start", UvmRole::sequence_start },
    MemberRole { UvmRole::sequence_base, "start_item", UvmRole::sequence_start_item },
    MemberRole { UvmRole::sequence_base, "finish_item", UvmRole::sequence_finish_item },
    MemberRole { UvmRole::sequence_base, "pre_start", UvmRole::sequence_pre_start },
    MemberRole { UvmRole::sequence_base, "pre_body", UvmRole::sequence_pre_body },
    MemberRole { UvmRole::sequence_base, "body", UvmRole::sequence_body },
    MemberRole { UvmRole::sequence_base, "post_body", UvmRole::sequence_post_body },
    MemberRole { UvmRole::sequence_base, "post_start", UvmRole::sequence_post_start },
    MemberRole { UvmRole::seq_item_pull_port, "get", UvmRole::pull_get },
    MemberRole { UvmRole::seq_item_pull_port, "get_next_item", UvmRole::pull_get_next_item },
    MemberRole { UvmRole::seq_item_pull_port, "try_next_item", UvmRole::pull_try_next_item },
    MemberRole { UvmRole::seq_item_pull_port, "peek", UvmRole::pull_peek },
    MemberRole { UvmRole::seq_item_pull_port, "item_done", UvmRole::pull_item_done },
    MemberRole { UvmRole::seq_item_pull_imp, "get", UvmRole::pull_get },
    MemberRole { UvmRole::seq_item_pull_imp, "get_next_item", UvmRole::pull_get_next_item },
    MemberRole { UvmRole::seq_item_pull_imp, "try_next_item", UvmRole::pull_try_next_item },
    MemberRole { UvmRole::seq_item_pull_imp, "peek", UvmRole::pull_peek },
    MemberRole { UvmRole::seq_item_pull_imp, "item_done", UvmRole::pull_item_done },
    MemberRole { UvmRole::seq_item_pull_export, "get", UvmRole::pull_get },
    MemberRole { UvmRole::seq_item_pull_export, "get_next_item", UvmRole::pull_get_next_item },
    MemberRole { UvmRole::seq_item_pull_export, "try_next_item", UvmRole::pull_try_next_item },
    MemberRole { UvmRole::seq_item_pull_export, "peek", UvmRole::pull_peek },
    MemberRole { UvmRole::seq_item_pull_export, "item_done", UvmRole::pull_item_done },
    MemberRole { UvmRole::analysis_port, "write", UvmRole::analysis_port_write },
    MemberRole { UvmRole::none, "phase_started", UvmRole::phase_started },
    MemberRole { UvmRole::none, "phase_ready_to_end", UvmRole::phase_ready_to_end },
    MemberRole { UvmRole::none, "phase_ended", UvmRole::phase_ended },
    MemberRole { UvmRole::none, "build_phase", UvmRole::build_phase },
    MemberRole { UvmRole::none, "connect_phase", UvmRole::connect_phase },
    MemberRole { UvmRole::none, "end_of_elaboration_phase", UvmRole::end_of_elaboration_phase },
    MemberRole { UvmRole::none, "start_of_simulation_phase", UvmRole::start_of_simulation_phase },
    MemberRole { UvmRole::none, "extract_phase", UvmRole::extract_phase },
    MemberRole { UvmRole::none, "check_phase", UvmRole::check_phase },
    MemberRole { UvmRole::none, "report_phase", UvmRole::report_phase },
    MemberRole { UvmRole::none, "final_phase", UvmRole::final_phase },
    MemberRole { UvmRole::none, "run_phase", UvmRole::run_phase },
    MemberRole { UvmRole::none, "pre_reset_phase", UvmRole::pre_reset_phase },
    MemberRole { UvmRole::none, "reset_phase", UvmRole::reset_phase },
    MemberRole { UvmRole::none, "post_reset_phase", UvmRole::post_reset_phase },
    MemberRole { UvmRole::none, "pre_configure_phase", UvmRole::pre_configure_phase },
    MemberRole { UvmRole::none, "configure_phase", UvmRole::configure_phase },
    MemberRole { UvmRole::none, "post_configure_phase", UvmRole::post_configure_phase },
    MemberRole { UvmRole::none, "pre_main_phase", UvmRole::pre_main_phase },
    MemberRole { UvmRole::none, "main_phase", UvmRole::main_phase },
    MemberRole { UvmRole::none, "post_main_phase", UvmRole::post_main_phase },
    MemberRole { UvmRole::none, "pre_shutdown_phase", UvmRole::pre_shutdown_phase },
    MemberRole { UvmRole::none, "shutdown_phase", UvmRole::shutdown_phase },
    MemberRole { UvmRole::none, "post_shutdown_phase", UvmRole::post_shutdown_phase },
};

constexpr std::array property_roles {
    MemberRole { UvmRole::coreservice, "inst", UvmRole::coreservice_inst },
    MemberRole { UvmRole::driver, "seq_item_port", UvmRole::driver_seq_item_port },
    MemberRole { UvmRole::driver, "rsp_port", UvmRole::driver_rsp_port },
    MemberRole { UvmRole::sequencer, "seq_item_export", UvmRole::sequencer_seq_item_export },
    MemberRole { UvmRole::sequencer_param_base, "rsp_export", UvmRole::sequencer_rsp_export },
    MemberRole { UvmRole::none, "newline", UvmRole::printer_newline },
    MemberRole { UvmRole::none, "knobs", UvmRole::printer_knobs_handle },
    MemberRole { UvmRole::printer_knobs, "header", UvmRole::knobs_header },
    MemberRole { UvmRole::printer_knobs, "footer", UvmRole::knobs_footer },
    MemberRole { UvmRole::printer_knobs, "full_name", UvmRole::knobs_full_name },
    MemberRole { UvmRole::printer_knobs, "identifier", UvmRole::knobs_identifier },
    MemberRole { UvmRole::printer_knobs, "type_name", UvmRole::knobs_type_name },
    MemberRole { UvmRole::printer_knobs, "size", UvmRole::knobs_size },
    MemberRole { UvmRole::printer_knobs, "depth", UvmRole::knobs_depth },
    MemberRole { UvmRole::printer_knobs, "reference", UvmRole::knobs_reference },
    MemberRole { UvmRole::printer_knobs, "prefix", UvmRole::knobs_prefix },
    MemberRole { UvmRole::printer_knobs, "indent", UvmRole::knobs_indent },
    MemberRole { UvmRole::printer_knobs, "show_root", UvmRole::knobs_show_root },
    MemberRole { UvmRole::printer_knobs, "separator", UvmRole::knobs_separator },
};

template <std::size_t Size>
[[nodiscard]] UvmRole member_role(const std::array<MemberRole, Size>& roles,
    const UvmRole owner, const std::string_view name)
{
    for (const auto& entry : roles) {
        if (entry.owner == owner && entry.name == name)
            return entry.role;
    }
    for (const auto& entry : roles) {
        if (entry.owner == UvmRole::none && entry.name == name)
            return entry.role;
    }
    return UvmRole::member;
}

void record_class(SystemVerilogClassDeclaration& declaration)
{
    declaration.uvm_role = find_role(class_roles, declaration.name);
    for (auto& method : declaration.methods) {
        method.uvm_role = member_role(
            method_roles, declaration.uvm_role, method.name);
    }
    for (auto& property : declaration.properties) {
        property.uvm_role = member_role(
            property_roles, declaration.uvm_role, property.declaration.name);
    }
    for (auto& nested : declaration.nested_classes)
        record_class(nested);
}

// The package-level functions, tasks, variables and types of the library
// the native services implement or read (IEEE 1800.2-2020 F.3, F.4).
constexpr std::array package_roles {
    std::pair { std::string_view { "run_test" }, UvmRole::run_test },
    std::pair { std::string_view { "uvm_report_enabled" },
        UvmRole::report_enabled },
    std::pair { std::string_view { "uvm_report_info" },
        UvmRole::report_info },
    std::pair { std::string_view { "uvm_report_warning" },
        UvmRole::report_warning },
    std::pair { std::string_view { "uvm_report_error" },
        UvmRole::report_error },
    std::pair { std::string_view { "uvm_report_fatal" },
        UvmRole::report_fatal },
    std::pair { std::string_view { "uvm_get_report_object" },
        UvmRole::get_report_object },
    std::pair { std::string_view { "uvm_top" }, UvmRole::top },
    std::pair { std::string_view { "uvm_cmdline_proc" },
        UvmRole::cmdline_proc },
    std::pair { std::string_view { "uvm_test_done" }, UvmRole::test_done },
    std::pair { std::string_view { "uvm_resources" }, UvmRole::resources },
    std::pair { std::string_view { "uvm_default_printer" },
        UvmRole::default_printer },
    std::pair { std::string_view { "uvm_port_type_e" },
        UvmRole::port_type_enum },
    std::pair { std::string_view { "uvm_active_passive_enum" },
        UvmRole::active_passive_enum },
};

// The roles of the virtual methods a class of the library has, its own and
// those it inherits, by method name. A library class that overrides one of
// them keeps its role (IEEE 1800-2023 8.20): `uvm_default_factory`'s methods
// are the factory's.
using VirtualRoles = std::map<std::string, UvmRole, std::less<>>;

struct RoleInheritance {
    std::map<std::string, SystemVerilogClassDeclaration*, std::less<>> classes;
    std::map<std::string, VirtualRoles, std::less<>> resolved;
    std::set<std::string, std::less<>> active;

    const VirtualRoles& roles(SystemVerilogClassDeclaration& declaration)
    {
        if (const auto found = resolved.find(declaration.name);
            found != resolved.end())
            return found->second;
        VirtualRoles result;
        // A cycle of bases is diagnosed by class resolution.
        if (active.insert(declaration.name).second) {
            if (declaration.base) {
                if (const auto base = classes.find(declaration.base->name);
                    base != classes.end())
                    result = roles(*base->second);
            }
            active.erase(declaration.name);
        }
        for (auto& method : declaration.methods) {
            if (method.is_static)
                continue;
            if (const auto inherited = result.find(method.name);
                inherited != result.end()
                && method.uvm_role == UvmRole::member) {
                method.uvm_role = inherited->second;
            } else if (method.is_virtual
                && method.uvm_role != UvmRole::member) {
                result.insert_or_assign(method.name, method.uvm_role);
            }
        }
        return resolved.emplace(declaration.name, std::move(result))
            .first->second;
    }
};

} // namespace

void record_uvm_library_roles(DesignUnit& package)
{
    package.uvm_role = UvmRole::member;
    package.uvm_declaration_roles.clear();
    for (const auto& [name, role] : package_roles)
        package.uvm_declaration_roles.push_back({ std::string { name }, role });
    for (auto& declaration : package.systemverilog_classes)
        record_class(declaration);
    RoleInheritance inheritance;
    for (auto& declaration : package.systemverilog_classes)
        inheritance.classes.emplace(declaration.name, &declaration);
    for (auto& declaration : package.systemverilog_classes)
        (void)inheritance.roles(declaration);
}

} // namespace fsim::frontend
