// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_interface_ports_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace fsim::elaboration {

bool HierarchyBuilder::connect_compiled_systemverilog_interface_member(
    const std::string& source_name,
    const std::string_view formal_name,
    const std::string_view child_path,
    const std::string_view member,
    const bool input_direction,
    const bool clocking_event,
    SignalMap& child_aliases)
{
    const auto signal = design_.signal_by_name_.find(source_name);
    const auto event = systemverilog_clocking_event_signals_.find(
        source_name);
    if (signal == design_.signal_by_name_.end()
        && event == systemverilog_clocking_event_signals_.end()) {
        return false;
    }
    const auto signal_id = signal != design_.signal_by_name_.end()
        ? signal->second
        : event->second;
    const auto local_name = std::string { formal_name } + "."
        + std::string { member };
    const auto qualified_name = std::string { child_path } + "."
        + local_name;
    child_aliases.insert_or_assign(local_name, signal_id);
    child_aliases.insert_or_assign(qualified_name, signal_id);
    design_.signal_by_name_.insert_or_assign(qualified_name, signal_id);
    if (clocking_event) {
        systemverilog_clocking_event_signals_.insert_or_assign(
            qualified_name, signal_id);
    }
    if (input_direction
        || systemverilog_read_only_interface_member_paths_.contains(
            source_name)) {
        systemverilog_read_only_interface_member_paths_.insert(
            qualified_name);
    }
    return true;
}

std::optional<HierarchyBuilder::CompiledSystemVerilogInterfaceDiagnostic>
HierarchyBuilder::connect_compiled_systemverilog_modport_members(
    const semantic::sv::Unit& interface,
    const semantic::sv::Modport& modport,
    const std::string_view source_path,
    const std::string_view formal_name,
    const std::string_view child_path,
    SignalMap& child_aliases)
{
    const auto connect_member = [&](const std::string_view member,
                                    const semantic::sv::Direction direction,
                                    const semantic::SourceSpanId source,
                                    const bool clocking_event = false)
        -> std::optional<CompiledSystemVerilogInterfaceDiagnostic> {
        const auto source_name = std::string { source_path } + "."
            + std::string { member };
        if (!connect_compiled_systemverilog_interface_member(
                source_name, formal_name, child_path, member,
                direction == semantic::sv::Direction::input,
                clocking_event, child_aliases)) {
            return CompiledSystemVerilogInterfaceDiagnostic {
                "FSIM-ELAB-SVIFACE-005",
                "interface member signal '" + source_name
                    + "' was not elaborated",
                source,
            };
        }
        return std::nullopt;
    };
    for (const auto& member : modport.members) {
        if (member.kind
            == semantic::sv::ModportMemberKind::signal) {
            if (auto diagnostic = connect_member(
                    member.name.spelling,
                    member.direction,
                    member.source)) {
                return diagnostic;
            }
            continue;
        }
        if (member.kind
            != semantic::sv::ModportMemberKind::clocking) {
            continue;
        }
        const auto block = std::ranges::find(
            interface.clocking_blocks,
            member.name.spelling,
            &semantic::sv::ClockingBlock::name);
        if (block == interface.clocking_blocks.end()) {
            return CompiledSystemVerilogInterfaceDiagnostic {
                "FSIM-ELAB-CLOCK-007",
                "modport clocking member '" + member.name.spelling
                    + "' was not retained by interface '"
                    + interface.name + "'",
                member.source,
            };
        }
        if (auto diagnostic = connect_member(
                block->name,
                semantic::sv::Direction::input,
                block->source,
                true)) {
            return diagnostic;
        }
        for (const auto& clocking_signal : block->signals) {
            if (auto diagnostic = connect_member(
                    block->name + "." + clocking_signal.name.spelling,
                    clocking_signal.direction,
                    clocking_signal.source)) {
                return diagnostic;
            }
        }
    }
    return std::nullopt;
}

bool HierarchyBuilder::forward_compiled_systemverilog_interface_port(
    const semantic::sv::Declaration& formal,
    const semantic::sv::Unit& child_unit,
    const semantic::SpecializedHirUnit& child_specialization,
    const std::string& source_path,
    const std::string& child_path,
    std::vector<CompiledSystemVerilogInterfaceDiagnostic>& diagnostics,
    const CompiledSystemVerilogSpecializationFactory specialize,
    SignalMap& child_aliases)
{
    const auto diagnose = [&](
                              std::string code,
                              std::string message,
                              const semantic::SourceSpanId source) {
        diagnostics.push_back(
            { std::move(code), std::move(message), source });
    };
    const auto handle = systemverilog_interface_handles_.find(source_path);
    if (handle == systemverilog_interface_handles_.end()) {
        return false;
    }
    const auto actual_type
        = systemverilog_interface_types_.find(source_path);
    if (!formal.interface_type.empty()
        && actual_type != systemverilog_interface_types_.end()
        && actual_type->second != formal.interface_type) {
        diagnose(
            "FSIM-ELAB-SVIFACE-003",
            "interface port '" + child_path + "."
                + formal.name + "' requires type '"
                + formal.interface_type + "' but actual '"
                + source_path + "' has type '"
                + actual_type->second + "'",
            formal.source);
        return false;
    }
    const auto actual_view
        = systemverilog_interface_modport_views_.find(
            source_path);
    if (actual_view != systemverilog_interface_modport_views_.end()
        && !actual_view->second.empty()
        && (formal.modport.empty()
            || formal.modport != actual_view->second)) {
        diagnose(
            "FSIM-ELAB-SVIFACE-011",
            "restricted interface actual '" + source_path
                + "' exposes modport '" + actual_view->second
                + "' and cannot bind "
                + (formal.modport.empty()
                        ? "an unrestricted interface port"
                        : "different modport '" + formal.modport + "'"),
            formal.source);
        return false;
    }
    auto view = formal.modport;
    if (view.empty()) {
        if (const auto inherited
            = systemverilog_interface_modport_views_.find(
                source_path);
            inherited != systemverilog_interface_modport_views_.end()) {
            view = inherited->second;
        }
    }
    const auto record_forwarded_interface = [&] {
        const auto target_path = child_path + "." + formal.name;
        systemverilog_interface_handles_.insert_or_assign(
            target_path, handle->second);
        if (const auto parameter_identity
            = systemverilog_interface_parameter_identities_.find(
                source_path);
            parameter_identity
            != systemverilog_interface_parameter_identities_.end()) {
            systemverilog_interface_parameter_identities_.insert_or_assign(
                target_path, parameter_identity->second);
        }
        if (const auto type = systemverilog_interface_types_.find(
                source_path);
            type != systemverilog_interface_types_.end()) {
            systemverilog_interface_types_.insert_or_assign(
                target_path, type->second);
        }
        systemverilog_interface_port_paths_.insert(target_path);
        systemverilog_interface_modport_views_.insert_or_assign(
            target_path, view);
    };

    if (view.empty()) {
        record_forwarded_interface();
        std::vector<std::pair<std::string, SignalId>> members;
        const auto prefix = source_path + ".";
        for (const auto& [name, signal_id] : design_.signal_by_name_) {
            if (name.starts_with(prefix)) {
                members.emplace_back(name.substr(prefix.size()), signal_id);
            }
        }
        for (const auto& [member, signal_id] : members) {
            const auto local_name = formal.name + "." + member;
            const auto qualified_name = child_path + "." + local_name;
            child_aliases.insert_or_assign(local_name, signal_id);
            child_aliases.insert_or_assign(qualified_name, signal_id);
            design_.signal_by_name_.insert_or_assign(
                qualified_name, signal_id);
        }
        return true;
    }

    const auto library = child_unit.library.empty()
        ? std::string_view { "work" }
        : std::string_view { child_unit.library };
    const auto interface_unit = compiled_->find_unit(
        semantic::UnitKind::systemverilog_interface,
        library, formal.interface_type);
    const auto* interface = interface_unit
            && interface_unit->systemverilog != nullptr
        ? interface_unit->systemverilog
        : nullptr;
    if (interface == nullptr) {
        diagnose(
            "FSIM-ELAB-SVIFACE-003",
            "unknown interface type '" + formal.interface_type
                + "' for port '" + formal.name + "'",
            formal.source);
        return false;
    }
    const auto modport = std::ranges::find(
        interface->modports, view, &semantic::sv::Modport::name);
    if (modport == interface->modports.end()) {
        diagnose(
            "FSIM-ELAB-SVIFACE-004",
            "interface type '" + interface->name
                + "' has no modport '" + view + "'",
            formal.source);
        return false;
    }
    std::vector<semantic::SpecializedHirActualIdentity>
        source_interface_actuals;
    if (const auto identities
        = systemverilog_interface_parameter_identities_.find(
            source_path);
        identities != systemverilog_interface_parameter_identities_.end()) {
        source_interface_actuals.reserve(identities->second.size());
        for (const auto& [name, parameter_identity] : identities->second) {
            const auto matched_formal = std::ranges::find_if(
                interface->declarations,
                [&](const semantic::DeclarationId declaration_id) {
                    const auto declaration
                        = compiled_->find_declaration(declaration_id);
                    if (!declaration
                        || declaration->systemverilog == nullptr
                        || declaration->systemverilog->name != name) {
                        return false;
                    }
                    using Form = semantic::sv::DeclarationForm;
                    const auto form = declaration->systemverilog->form;
                    return form == Form::parameter
                        || form == Form::local_parameter
                        || form == Form::type_parameter;
                });
            if (matched_formal != interface->declarations.end()) {
                source_interface_actuals.push_back({
                    *matched_formal,
                    parameter_identity,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                });
            }
        }
    }
    auto source_interface_specialization = specialize(
        validated_compiled_, interface->id, source_interface_actuals);
    std::unordered_set<std::string> imported_functions;
    std::unordered_set<std::string> imported_tasks;
    bool callable_members_valid = true;
    for (const auto& member : modport->members) {
        const bool function
            = member.kind == semantic::sv::ModportMemberKind::function_import
            || member.kind
                == semantic::sv::ModportMemberKind::function_export;
        const bool imported
            = member.kind == semantic::sv::ModportMemberKind::function_import
            || member.kind == semantic::sv::ModportMemberKind::task_import;
        const bool exported
            = member.kind == semantic::sv::ModportMemberKind::function_export
            || member.kind == semantic::sv::ModportMemberKind::task_export;
        if (!imported && !exported) {
            continue;
        }
        auto expected = member.name.selected
            ? source_interface_specialization
                ? source_interface_specialization->find_declaration(
                      *member.name.selected)
                : compiled_->find_declaration(*member.name.selected)
            : std::nullopt;
        if (!expected) {
            const auto declaration = std::ranges::find_if(
                interface->declarations,
                [&](const semantic::DeclarationId id) {
                    const auto candidate = source_interface_specialization
                        ? source_interface_specialization->find_declaration(id)
                        : compiled_->find_declaration(id);
                    return candidate
                        && candidate->systemverilog != nullptr
                        && candidate->systemverilog->name
                        == member.name.spelling
                        && candidate->systemverilog->callable
                        && candidate->systemverilog->callable->function
                        == function;
                });
            if (declaration != interface->declarations.end()) {
                expected = source_interface_specialization
                    ? source_interface_specialization->find_declaration(
                          *declaration)
                    : compiled_->find_declaration(*declaration);
            }
        }
        if (imported) {
            auto& visible = function ? imported_functions : imported_tasks;
            if (!visible.insert(member.name.spelling).second) {
                diagnose(
                    "FSIM-ELAB-SVIFACE-008",
                    "interface callable '" + formal.name + "."
                        + member.name.spelling + "' is visible more than once",
                    member.source);
                callable_members_valid = false;
                continue;
            }
            if (!expected || expected->systemverilog == nullptr
                || !expected->systemverilog->callable
                || expected->systemverilog->callable->function != function) {
                diagnose(
                    "FSIM-ELAB-SVIFACE-007",
                    "interface "
                        + std::string { function ? "function '" : "task '" }
                        + member.name.spelling + "' was not retained",
                    member.source);
                callable_members_valid = false;
            }
            continue;
        }
        const auto supplied = std::ranges::any_of(
            child_unit.declarations,
            [&](const semantic::DeclarationId declaration_id) {
                const auto declaration
                    = child_specialization.find_declaration(declaration_id);
                return expected && expected->systemverilog != nullptr
                    && declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->name
                    == member.name.spelling
                    && declaration->systemverilog->callable
                    && declaration->systemverilog->callable->function
                    == function
                    && hierarchy_sv_interface_ports_detail::
                        compiled_systemverilog_callable_profile_matches(
                            *compiled_, source_interface_specialization,
                            child_specialization, *expected->systemverilog,
                            *declaration->systemverilog);
            });
        if (!supplied) {
            diagnose(
                "FSIM-ELAB-SVIFACE-009",
                "modport export '" + member.name.spelling
                    + "' has no matching module callable",
                member.source);
            callable_members_valid = false;
        }
    }

    record_forwarded_interface();
    if (const auto diagnostic = connect_compiled_systemverilog_modport_members(
            *interface, *modport, source_path, formal.name,
            child_path, child_aliases)) {
        diagnostics.push_back(*diagnostic);
        return false;
    }
    return callable_members_valid;
}

bool hierarchy_sv_interface_ports_detail::
    compiled_systemverilog_callable_profile_matches(
        const semantic::CompiledDesign& compiled,
        const std::optional<semantic::SpecializedHirUnit>&
            expected_specialization,
        const semantic::SpecializedHirUnit& actual_specialization,
        const semantic::sv::Declaration& expected,
        const semantic::sv::Declaration& actual)
{
    const auto same_type_profile = [](
                                       const semantic::SpecializedHirUnit& expected_specialization,
                                       const semantic::sv::Declaration& expected,
                                       const semantic::SpecializedHirUnit& actual_specialization,
                                       const semantic::sv::Declaration& actual) {
        if (expected.type.has_value() != actual.type.has_value()) {
            return false;
        }
        if (!expected.type) {
            return true;
        }
        if (hierarchy_sv_parameters_detail::
                compiled_systemverilog_type_identity(*expected.type)
            == hierarchy_sv_parameters_detail::
                compiled_systemverilog_type_identity(*actual.type)) {
            return true;
        }
        const auto expected_width
            = hierarchy_sv_type_layout_detail::
                systemverilog_declaration_width(
                    expected_specialization, expected);
        const auto actual_width
            = hierarchy_sv_type_layout_detail::
                systemverilog_declaration_width(
                    actual_specialization, actual);
        return expected_width && actual_width
            && *expected_width == *actual_width
            && expected.type->target.spelling
            == actual.type->target.spelling
            && expected.type->value_form == actual.type->value_form
            && expected.type->container_form
            == actual.type->container_form
            && expected.type->signed_value == actual.type->signed_value
            && expected.type->four_state == actual.type->four_state;
    };

    if (!expected.callable || !actual.callable
        || expected.callable->function != actual.callable->function
        || expected.callable->formals.size()
            != actual.callable->formals.size()) {
        return false;
    }
    if (expected.callable->function) {
        auto expected_result = expected;
        auto actual_result = actual;
        expected_result.type = expected.callable->return_type;
        actual_result.type = actual.callable->return_type;
        if (!expected_specialization
            || !same_type_profile(
                *expected_specialization, expected_result,
                actual_specialization, actual_result)) {
            return false;
        }
    }
    for (std::size_t index = 0U;
        index < expected.callable->formals.size(); ++index) {
        const auto expected_formal = expected_specialization
            ? expected_specialization->find_declaration(
                  expected.callable->formals[index])
            : compiled.find_declaration(expected.callable->formals[index]);
        const auto actual_formal = actual_specialization.find_declaration(
            actual.callable->formals[index]);
        if (!expected_formal
            || expected_formal->systemverilog == nullptr
            || !actual_formal
            || actual_formal->systemverilog == nullptr) {
            return false;
        }
        const auto& expected_record = *expected_formal->systemverilog;
        const auto& actual_record = *actual_formal->systemverilog;
        if (expected_record.direction != actual_record.direction
            || expected_record.const_reference
                != actual_record.const_reference
            || expected_record.static_reference
                != actual_record.static_reference
            || !expected_specialization
            || !same_type_profile(
                *expected_specialization, expected_record,
                actual_specialization, actual_record)) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::elaboration
