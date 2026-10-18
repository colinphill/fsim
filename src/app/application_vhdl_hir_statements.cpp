// SPDX-License-Identifier: Apache-2.0
#include "application_vhdl_hir_internal.hpp"

namespace fsim::app::application_detail {

void VhdlHirBuilder::add_statement_regions(
    const std::span<const frontend::Statement> statements,
    const semantic::ScopeId enclosing_scope,
    const semantic::OriginId enclosing_origin)
{
    // Whether a statement list contains a sequential block statement.
    const auto contains_block = [](const auto& self,
                                   const std::span<const frontend::Statement> list)
        -> bool {
        return std::ranges::any_of(list, [&](const frontend::Statement& item) {
            return item.kind == frontend::StatementKind::Block
                || self(self, std::span { item.statements })
                || self(self, std::span { item.else_statements })
                || std::ranges::any_of(item.case_alternatives,
                    [&](const auto& alternative) {
                        return self(self, std::span { alternative.statements });
                    });
        });
    };
    for (const auto& statement : statements) {
        auto child_scope = enclosing_scope;
        auto child_origin = enclosing_origin;
        // A block in a for loop is nested in the loop's parameter region
        // (IEEE 1076-2019 10.13): create that region here so the block can
        // see the parameter; the statement builder reuses it.
        if (statement.kind == frontend::StatementKind::Loop
            && !statement.loop_variable.empty()
            && contains_block(contains_block, std::span { statement.statements })) {
            const auto statement_source = source(statement.span);
            child_origin = origin(statement_source, enclosing_origin,
                statement.label.empty() ? "VHDL statement" : statement.label);
            child_scope = nested_scope(
                enclosing_scope, "<loop>", statement_source, child_origin);
        }
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
