// SPDX-License-Identifier: Apache-2.0

        [[nodiscard]] semantic::DeclarationId add_function(
            const frontend::FunctionDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::function,
                semantic::DeclarationKind::function,
                input.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto function_scope = nested_scope(
                scope, input.name, declaration_span, declaration_origin);
            auto result_type = subtype(
                input.return_type, input.span, scope, declaration_origin);
            auto& output = declaration(id);
            output.nested_scope = function_scope;
            output.subtype = result_type;
            output.declared_value = ensure_value(
                input.name,
                scope,
                semantic::ValueKind::function,
                result_type,
                declaration_span,
                declaration_origin);
            vh::CallableProfile profile;
            profile.function = true;
            profile.pure = input.pure;
            profile.defined = input.defined;
            profile.return_type = result_type;
            std::vector<semantic::DeclarationId> children;
            if (!input.vhdl_return_identifier.empty()) {
                const auto implicit_subtype = std::ranges::find(
                    input.type_aliases,
                    input.vhdl_return_identifier,
                    &frontend::TypeAliasDeclaration::name);
                if (implicit_subtype != input.type_aliases.end()) {
                    const auto subtype_id = add_type_declaration(
                        *implicit_subtype,
                        function_scope,
                        declaration_origin);
                    profile.return_identifier = subtype_id;
                    children.push_back(subtype_id);
                }
            }
            for (const auto& argument : input.arguments) {
                profile.formals.push_back(add_function_argument(
                    argument, function_scope, declaration_origin));
            }
            declaration(id).callable = std::move(profile);
            children.insert(
                children.end(),
                declaration(id).callable->formals.begin(),
                declaration(id).callable->formals.end());
            add_children(
                input.constants,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_parameter);
            for (const auto& alias : input.type_aliases) {
                if (alias.name == input.vhdl_return_identifier) {
                    continue;
                }
                children.push_back(add_type_declaration(
                    alias, function_scope, declaration_origin));
            }
            add_children(
                input.signal_aliases,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_alias);
            add_children(
                input.package_instances,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_package_instance);
            add_children(
                input.variables,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.functions,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_function);
            add_children(
                input.procedures,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.vhdl_attributes,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_attribute);
            add_children(
                input.vhdl_groups,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_group);
            declaration(id).children = std::move(children);
            add_statement_regions(
                input.statements, function_scope, declaration_origin);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_procedure(
            const frontend::ProcedureDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::procedure,
                semantic::DeclarationKind::procedure,
                input.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto procedure_scope = nested_scope(
                scope, input.name, declaration_span, declaration_origin);
            vh::SubtypeIndication no_type;
            no_type.type_mark.source = declaration_span;
            auto& output = declaration(id);
            output.nested_scope = procedure_scope;
            output.declared_value = ensure_value(
                input.name,
                scope,
                semantic::ValueKind::procedure,
                no_type,
                declaration_span,
                declaration_origin);
            vh::CallableProfile profile;
            profile.function = false;
            profile.pure = false;
            profile.defined = input.defined;
            for (const auto& argument : input.arguments) {
                profile.formals.push_back(add_procedure_argument(
                    argument, procedure_scope, declaration_origin));
            }
            declaration(id).callable = std::move(profile);
            auto children = declaration(id).callable->formals;
            add_children(
                input.constants,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_parameter);
            add_children(
                input.type_aliases,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_type_declaration);
            add_children(
                input.signal_aliases,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_alias);
            add_children(
                input.package_instances,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_package_instance);
            add_children(
                input.variables,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.functions,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_function);
            add_children(
                input.procedures,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.vhdl_attributes,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_attribute);
            add_children(
                input.vhdl_groups,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_group);
            declaration(id).children = std::move(children);
            add_statement_regions(
                input.statements, procedure_scope, declaration_origin);
            return id;
        }
