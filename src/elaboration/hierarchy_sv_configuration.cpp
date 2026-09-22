// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {
namespace {
    std::string_view normalized_library(const semantic::sv::Unit& unit)
    {
        return unit.library.empty() ? std::string_view { "work" }
                                    : std::string_view { unit.library };
    }

    frontend::SourceSpan compiled_source_span(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        frontend::SourceSpan result;
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return result;
        }
        const auto& span = spans[source.value()];
        result.source_name = span.logical_name;
        result.begin = { static_cast<std::size_t>(span.begin.offset),
            span.begin.line, span.begin.column };
        result.end = { static_cast<std::size_t>(span.end.offset),
            span.end.line, span.end.column };
        const auto& files = compiled.semantics.source_files();
        if (span.file.valid() && span.file.value() < files.size()) {
            result.physical_source_name
                = files[span.file.value()].physical_name;
        }
        const auto& expansions = compiled.semantics.expansions();
        auto expansion = span.expansion;
        while (expansion && expansion->valid()
            && expansion->value() < expansions.size()) {
            const auto& record = expansions[expansion->value()];
            result.expansion_stack.push_back(record.description);
            expansion = record.parent;
        }
        std::ranges::reverse(result.expansion_stack);
        return result;
    }

    bool same_compiled_extern_expression(
        const semantic::CompiledDesign& compiled,
        const semantic::ExpressionId prototype,
        const semantic::ExpressionId definition,
        const std::size_t depth = 0U)
    {
        if (depth > compiled.semantics.expression_identities().size()) {
            return false;
        }
        const auto left = compiled.find_expression(prototype);
        const auto right = compiled.find_expression(definition);
        if (!left || !right || left->systemverilog == nullptr
            || right->systemverilog == nullptr) {
            return false;
        }
        const auto& left_expression = *left->systemverilog;
        const auto& right_expression = *right->systemverilog;
        if (left_expression.kind != right_expression.kind
            || left_expression.text != right_expression.text
            || left_expression.argument_names
                != right_expression.argument_names
            || left_expression.nominal_type
                != right_expression.nominal_type
            || left_expression.class_identity
                != right_expression.class_identity
            || left_expression.class_member_identity
                != right_expression.class_member_identity
            || left_expression.operands.size()
                != right_expression.operands.size()) {
            return false;
        }
        for (std::size_t index = 0U;
            index < left_expression.operands.size(); ++index) {
            if (!same_compiled_extern_expression(compiled,
                    left_expression.operands[index],
                    right_expression.operands[index], depth + 1U)) {
                return false;
            }
        }
        return true;
    }

    bool same_compiled_extern_range(
        const semantic::CompiledDesign& compiled,
        const semantic::sv::PackedRange& prototype,
        const semantic::sv::PackedRange& definition)
    {
        const auto same_expression = [&](const std::optional<
                                             semantic::ExpressionId>& left,
                                         const std::optional<
                                             semantic::ExpressionId>& right) {
            return (!left && !right)
                || (left && right
                    && same_compiled_extern_expression(
                        compiled, *left, *right));
        };
        return prototype.left == definition.left
            && prototype.right == definition.right
            && prototype.descending == definition.descending
            && same_expression(
                prototype.left_expression, definition.left_expression)
            && same_expression(
                prototype.right_expression, definition.right_expression);
    }

    bool same_compiled_extern_type(
        const semantic::CompiledDesign& compiled,
        const semantic::sv::TypeReference& prototype,
        const semantic::sv::TypeReference& definition)
    {
        if (prototype.target.spelling != definition.target.spelling
            || prototype.value_form != definition.value_form
            || prototype.class_identity != definition.class_identity
            || prototype.systemverilog_net_type
                != definition.systemverilog_net_type
            || prototype.systemverilog_resolution_function
                != definition.systemverilog_resolution_function
            || prototype.signed_value != definition.signed_value
            || prototype.container_form != definition.container_form
            || prototype.executable_width != definition.executable_width
            || prototype.four_state != definition.four_state
            || prototype.unpacked_dimensions.size()
                != definition.unpacked_dimensions.size()
            || prototype.container_element_types.size()
                != definition.container_element_types.size()
            || prototype.packed_range.has_value()
                != definition.packed_range.has_value()
            || prototype.associative_index.has_value()
                != definition.associative_index.has_value()) {
            return false;
        }
        if (prototype.packed_range
            && !same_compiled_extern_range(compiled,
                *prototype.packed_range, *definition.packed_range)) {
            return false;
        }
        if (prototype.associative_index
            && prototype.associative_index->spelling
                != definition.associative_index->spelling) {
            return false;
        }
        for (std::size_t index = 0U;
            index < prototype.unpacked_dimensions.size(); ++index) {
            if (!same_compiled_extern_range(compiled,
                    prototype.unpacked_dimensions[index],
                    definition.unpacked_dimensions[index])) {
                return false;
            }
        }
        for (std::size_t index = 0U;
            index < prototype.container_element_types.size(); ++index) {
            if (!same_compiled_extern_type(compiled,
                    prototype.container_element_types[index],
                    definition.container_element_types[index])) {
                return false;
            }
        }
        return true;
    }

    bool same_compiled_extern_type(
        const semantic::CompiledDesign& compiled,
        const std::optional<semantic::sv::TypeReference>& prototype,
        const std::optional<semantic::sv::TypeReference>& definition)
    {
        return (!prototype && !definition)
            || (prototype && definition
                && same_compiled_extern_type(
                    compiled, *prototype, *definition));
    }

    void append_identity_field(
        std::string& identity,
        const std::string_view field)
    {
        identity += std::to_string(field.size());
        identity += ':';
        identity += field;
        identity += ';';
    }

} // namespace

void HierarchyBuilder::validate_systemverilog_extern_declarations()
{
    const auto declarations = [&](const semantic::sv::Unit& unit,
                                  const bool parameters) {
        std::vector<const semantic::sv::Declaration*> result;
        for (const auto declaration_id : unit.declarations) {
            const auto declaration
                = compiled_->find_declaration(declaration_id);
            if (!declaration
                || declaration->systemverilog == nullptr) {
                result.push_back(nullptr);
                continue;
            }
            const auto& record = *declaration->systemverilog;
            const bool parameter
                = record.form
                        == semantic::sv::DeclarationForm::parameter
                || record.form
                        == semantic::sv::DeclarationForm::local_parameter
                || record.form
                        == semantic::sv::DeclarationForm::type_parameter;
            if ((parameters && parameter)
                || (!parameters
                    && record.form
                        == semantic::sv::DeclarationForm::port)) {
                result.push_back(&record);
            }
        }
        return result;
    };
    const auto validate_compiled_header
        = [&](const semantic::sv::Unit& prototype,
              const semantic::sv::Unit& definition) {
        const auto prototype_parameters
            = declarations(prototype, true);
        const auto definition_parameters
            = declarations(definition, true);
        const auto prototype_ports = declarations(prototype, false);
        const auto definition_ports = declarations(definition, false);
        bool matches = prototype_parameters.size()
                == definition_parameters.size()
            && prototype_ports.size() == definition_ports.size();
        for (std::size_t index = 0U;
            matches && index < prototype_parameters.size(); ++index) {
            const auto* left = prototype_parameters[index];
            const auto* right = definition_parameters[index];
            matches = left != nullptr && right != nullptr
                && left->name == right->name
                && left->form == right->form
                && same_compiled_extern_type(
                    *compiled_, left->type, right->type)
                && same_compiled_extern_type(
                    *compiled_, left->default_type,
                    right->default_type);
        }
        for (std::size_t index = 0U;
            matches && index < prototype_ports.size(); ++index) {
            const auto* left = prototype_ports[index];
            const auto* right = definition_ports[index];
            matches = left != nullptr && right != nullptr
                && left->name == right->name
                && left->direction == right->direction
                && left->interface_type == right->interface_type
                && left->modport == right->modport
                && same_compiled_extern_type(
                    *compiled_, left->type, right->type);
        }
        if (!matches) {
            report(
                "FSIM-ELAB-SVEXTERN-002",
                "extern declaration '" + prototype.name
                    + "' does not match its definition header",
                compiled_source_span(*compiled_, prototype.source));
        }
    };
    for (const auto& prototype_unit :
        compiled_->systemverilog_units()) {
        if (!prototype_unit.external) {
            continue;
        }
        const auto prototype = compiled_->find_unit(prototype_unit.id);
        if (!prototype || prototype->identity == nullptr) {
            report(
                "FSIM-ELAB-SVEXTERN-001",
                "extern declaration '" + prototype_unit.name
                    + "' has no matching definition",
                compiled_source_span(
                    *compiled_, prototype_unit.source));
            continue;
        }
        const semantic::sv::Unit* definition = nullptr;
        bool ambiguous = false;
        for (const auto candidate : compiled_->find_units(
                 prototype->identity->kind,
                 normalized_library(prototype_unit),
                 prototype_unit.name)) {
            if (candidate.systemverilog == nullptr
                || candidate.systemverilog->external) {
                continue;
            }
            if (definition != nullptr) {
                report(
                    "FSIM-ELAB-SVEXTERN-001",
                    "extern declaration '" + prototype_unit.name
                        + "' matches multiple definitions",
                    compiled_source_span(
                        *compiled_, prototype_unit.source));
                ambiguous = true;
                break;
            }
            definition = candidate.systemverilog;
        }
        if (ambiguous) {
            continue;
        }
        if (definition == nullptr) {
            report(
                "FSIM-ELAB-SVEXTERN-001",
                "extern declaration '" + prototype_unit.name
                    + "' has no matching definition",
                compiled_source_span(
                    *compiled_, prototype_unit.source));
            continue;
        }
        validate_compiled_header(prototype_unit, *definition);
    }
}

std::string HierarchyBuilder::systemverilog_configuration_identity(
    const semantic::sv::Unit& configuration) const
{
    std::string identity { "sv-config-v1;" };
    append_identity_field(identity, normalized_library(configuration));
    append_identity_field(identity, configuration.name);
    if (!configuration.configuration) {
        return identity;
    }
    const auto& declaration = *configuration.configuration;
    for (const auto& design : declaration.designs) {
        append_identity_field(identity, design.library);
        append_identity_field(identity, design.cell);
    }
    for (const auto& library : declaration.default_liblist) {
        append_identity_field(identity, library);
    }
    for (const auto& rule : declaration.rules) {
        append_identity_field(identity,
            rule.kind == semantic::sv::ConfigurationRuleKind::instance
                ? "instance"
                : "cell");
        append_identity_field(identity, rule.selector);
        append_identity_field(identity,
            rule.selection
                    == semantic::sv::ConfigurationSelectionKind::use
                ? "use"
                : "liblist");
        append_identity_field(identity, rule.use_library);
        append_identity_field(identity, rule.use_cell);
        append_identity_field(
            identity, rule.use_configuration ? "config" : "cell");
        for (const auto& library : rule.liblist) {
            append_identity_field(identity, library);
        }
    }
    return identity;
}

} // namespace fsim::elaboration
