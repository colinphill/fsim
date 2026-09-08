// SPDX-License-Identifier: Apache-2.0

        void add_statement_regions(
            const std::span<const frontend::Statement> statements,
            const semantic::ScopeId enclosing_scope,
            const semantic::OriginId enclosing_origin)
        {
            for (const auto& statement : statements) {
                auto child_scope = enclosing_scope;
                auto child_origin = enclosing_origin;
                if (statement.kind == frontend::StatementKind::Block) {
                    const auto statement_source = source(statement.span);
                    const auto scope_name = statement.label.empty()
                        ? "<block>"
                        : statement.label;
                    child_origin = origin(
                        statement_source,
                        enclosing_origin,
                        statement.label.empty()
                            ? "VHDL statement"
                            : statement.label);
                    child_scope = nested_scope(
                        enclosing_scope,
                        scope_name,
                        statement_source,
                        child_origin);
                    std::vector<semantic::DeclarationId> children;
                    add_children(
                        statement.constants,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_parameter);
                    add_children(
                        statement.type_aliases,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_type_declaration);
                    add_children(
                        statement.signal_aliases,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_alias);
                    add_children(
                        statement.package_instances,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_package_instance);
                    add_children(
                        statement.functions,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_function);
                    add_children(
                        statement.procedures,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_procedure);
                    add_children(
                        statement.declarations,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_variable);
                    add_children(
                        statement.vhdl_attributes,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_attribute);
                    add_children(
                        statement.vhdl_groups,
                        child_scope,
                        child_origin,
                        children,
                        &VhdlHirBuilder::add_group);
                }
                add_statement_regions(
                    statement.statements, child_scope, child_origin);
                add_statement_regions(
                    statement.else_statements, child_scope, child_origin);
                for (const auto& alternative : statement.case_alternatives) {
                    add_statement_regions(
                        alternative.statements, child_scope, child_origin);
                }
            }
        }
