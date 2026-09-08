// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {

void HierarchyBuilder::merge_vhdl_protected_types(
    DesignUnit& package,
    const DesignUnit& package_body)
{
    const auto conforming_function = [&](
                                         const frontend::FunctionDeclaration& declaration,
                                         const frontend::FunctionDeclaration& definition) {
        if (declaration.name != definition.name
            || declaration.pure != definition.pure
            || declaration.arguments.size()
                != definition.arguments.size()
            || !frontend::vhdl_base_type_profiles_match(
                declaration.return_type, definition.return_type)) {
            return false;
        }
        for (std::size_t index = 0;
            index < declaration.arguments.size(); ++index) {
            const auto& left = declaration.arguments[index];
            const auto& right = definition.arguments[index];
            if (left.direction != right.direction
                || left.vhdl_file != right.vhdl_file
                || !frontend::vhdl_parameter_type_profiles_match(
                    left.type, right.type)) {
                return false;
            }
        }
        return true;
    };
    const auto conforming_procedure = [&](
                                          const frontend::ProcedureDeclaration& declaration,
                                          const frontend::ProcedureDeclaration& definition) {
        if (declaration.name != definition.name
            || declaration.arguments.size()
                != definition.arguments.size()) {
            return false;
        }
        for (std::size_t index = 0;
            index < declaration.arguments.size(); ++index) {
            const auto& left = declaration.arguments[index];
            const auto& right = definition.arguments[index];
            if (left.direction != right.direction
                || left.object_class != right.object_class
                || !frontend::vhdl_parameter_type_profiles_match(
                    left.type, right.type)) {
                return false;
            }
        }
        return true;
    };

    for (const auto& body_alias : package_body.type_aliases) {
        if (body_alias.declaration_kind
            != frontend::TypeDeclarationKind::VhdlProtectedBody) {
            continue;
        }
        const auto declaration = std::ranges::find_if(
            package.type_aliases,
            [&](const auto& candidate) {
                return candidate.name == body_alias.name
                    && candidate.declaration_kind
                    == frontend::TypeDeclarationKind::VhdlProtected;
            });
        if (declaration == package.type_aliases.end()
            || !declaration->type.vhdl_protected
            || !body_alias.type.vhdl_protected) {
            report(
                "FSIM-ELAB-VHPROTECTED-001",
                "protected body '" + body_alias.name
                    + "' has no visible protected type declaration",
                body_alias.span);
            continue;
        }
        auto& public_info = *declaration->type.vhdl_protected;
        const auto& body_info = *body_alias.type.vhdl_protected;
        bool conformant = true;
        for (const auto& method : public_info.functions) {
            if (!std::ranges::any_of(
                    body_info.functions,
                    [&](const auto& candidate) {
                        return conforming_function(method, candidate);
                    })) {
                report(
                    "FSIM-ELAB-VHPROTECTED-002",
                    "protected function '" + method.name
                        + "' has no conforming body",
                    method.span);
                conformant = false;
            }
        }
        for (const auto& method : body_info.functions) {
            if (!std::ranges::any_of(
                    public_info.functions,
                    [&](const auto& candidate) {
                        return conforming_function(candidate, method);
                    })) {
                report(
                    "FSIM-ELAB-VHPROTECTED-003",
                    "protected function body '" + method.name
                        + "' does not conform to a public profile",
                    method.span);
                conformant = false;
            }
        }
        for (const auto& method : public_info.procedures) {
            if (!std::ranges::any_of(
                    body_info.procedures,
                    [&](const auto& candidate) {
                        return conforming_procedure(method, candidate);
                    })) {
                report(
                    "FSIM-ELAB-VHPROTECTED-004",
                    "protected procedure '" + method.name
                        + "' has no conforming body",
                    method.span);
                conformant = false;
            }
        }
        for (const auto& method : body_info.procedures) {
            if (!std::ranges::any_of(
                    public_info.procedures,
                    [&](const auto& candidate) {
                        return conforming_procedure(candidate, method);
                    })) {
                report(
                    "FSIM-ELAB-VHPROTECTED-005",
                    "protected procedure body '" + method.name
                        + "' does not conform to a public profile",
                    method.span);
                conformant = false;
            }
        }
        public_info.has_body = true;
        public_info.body_conformant = conformant;
        if (conformant) {
            public_info.variables.insert(
                public_info.variables.end(),
                body_info.variables.begin(),
                body_info.variables.end());
            public_info.functions = body_info.functions;
            public_info.procedures = body_info.procedures;
            public_info.method_aliases.insert(
                public_info.method_aliases.end(),
                body_info.method_aliases.begin(),
                body_info.method_aliases.end());
        }
    }
    std::erase_if(
        package.type_aliases,
        [](const auto& alias) {
            return alias.declaration_kind
                == frontend::TypeDeclarationKind::VhdlProtectedBody;
        });
    for (const auto& alias : package.type_aliases) {
        if (alias.declaration_kind
                == frontend::TypeDeclarationKind::VhdlProtected
            && alias.type.vhdl_protected
            && !alias.type.vhdl_protected->has_body) {
            report(
                "FSIM-ELAB-VHPROTECTED-006",
                "protected type '" + alias.name + "' has no body",
                alias.span);
        }
    }
}

void HierarchyBuilder::materialize_vhdl_shared_variable(
    const frontend::VariableDeclaration& variable,
    const std::string& path,
    SignalMap& signals)
{
    const auto width = variable.type.width();
    if (!width || *width == 0U) {
        report(
            "FSIM-ELAB-VHPROTECTED-023",
            "VHDL shared variable '" + path + "."
                + variable.name
                + "' requires a bounded executable scalar or packed subtype",
            variable.span);
        return;
    }
    const frontend::SignalDeclaration declaration {
        variable.name,
        variable.type,
        frontend::PortDirection::Unknown,
        false,
        variable.span
    };
    const auto signal = add_owned_signal(declaration, path, signals);
    if (!signal) {
        return;
    }
    vhdl_1993_shared_signals_.insert(*signal);
    if (!variable.initializer) {
        return;
    }
    std::string error;
    const auto initial = static_vhdl_value(
        *variable.initializer, variable.type, error);
    if (!initial || initial->width() != *width) {
        report(
            "FSIM-ELAB-VHPROTECTED-023",
            "VHDL shared-variable initializer for '" + path + "."
                + variable.name
                + "' is not a static value compatible with its declared subtype: "
                + error,
            variable.initializer->span);
        return;
    }
    design_.signals_[*signal].initial_value = *initial;
}

} // namespace fsim::elaboration
