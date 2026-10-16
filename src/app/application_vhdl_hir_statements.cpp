// SPDX-License-Identifier: Apache-2.0
#include "application_vhdl_hir_internal.hpp"

namespace fsim::app::application_detail {

void VhdlHirBuilder::add_statement_regions(
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
            const auto span = [](const auto& values) {
                return std::span { values.data(), values.size() };
            };
            queue_vhdl_local_declarations(
                span(statement.constants),
                span(statement.type_aliases),
                span(statement.signal_aliases),
                span(statement.package_instances),
                span(statement.functions),
                span(statement.procedures),
                span(statement.declarations),
                span(statement.vhdl_attributes),
                span(statement.vhdl_groups),
                std::nullopt,
                child_scope,
                child_origin,
                children);
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
} // namespace fsim::app::application_detail
