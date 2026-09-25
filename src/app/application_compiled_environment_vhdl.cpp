// SPDX-License-Identifier: Apache-2.0
#include "application_compiled_environment_vhdl.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {
namespace {

    namespace vh = semantic::vhdl;

    std::string identifier_key(const std::string_view name)
    {
        std::string key { name };
        if (!key.empty() && (key.front() == '\\' || key.front() == '\'')) {
            return key;
        }
        for (auto& character : key) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return key;
    }

    std::string package_key(const vh::Unit& unit)
    {
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        return identifier_key(library) + '\n' + identifier_key(unit.name);
    }

    diagnostic::SourceSpan source_span(const semantic::CompiledDesign& design,
        const semantic::SourceSpanId source)
    {
        diagnostic::SourceSpan result;
        if (!source.valid() || source.value() >= design.semantics.source_spans().size()) {
            return result;
        }
        const auto& span = design.semantics.source_spans()[source.value()];
        if (!span.file.valid()
            || span.file.value() >= design.semantics.source_files().size()) {
            return result;
        }
        const auto& file = design.semantics.source_files()[span.file.value()];
        result.path = span.logical_name.empty() ? file.physical_name : span.logical_name;
        result.begin = { span.begin.line, span.begin.column, span.begin.offset };
        result.end = { span.end.line, span.end.column, span.end.offset };
        return result;
    }

    const vh::Declaration* declaration(const semantic::CompiledDesign& design,
        const semantic::DeclarationId id)
    {
        const auto found = design.find_declaration(id);
        return found ? found->vhdl : nullptr;
    }

    const vh::Declaration* package_constant(const semantic::CompiledDesign& design,
        const vh::Unit& package, const std::string_view name)
    {
        const auto key = identifier_key(name);
        for (const auto id : package.declarations) {
            const auto* candidate = declaration(design, id);
            if (candidate != nullptr && candidate->form == vh::DeclarationForm::constant
                && identifier_key(candidate->name) == key) {
                return candidate;
            }
        }
        return nullptr;
    }

    bool same_expression(const semantic::CompiledDesign& design,
        const std::optional<semantic::ExpressionId> left,
        const std::optional<semantic::ExpressionId> right)
    {
        if (!left || !right) {
            return left.has_value() == right.has_value();
        }
        using Pair = std::pair<semantic::ExpressionId, semantic::ExpressionId>;
        std::vector<Pair> pending { { *left, *right } };
        std::set<Pair> visited;
        while (!pending.empty()) {
            const auto pair = pending.back();
            pending.pop_back();
            if (pair.first == pair.second || !visited.insert(pair).second) {
                continue;
            }
            const auto a = design.find_expression(pair.first);
            const auto b = design.find_expression(pair.second);
            if (!a || !b || a->vhdl == nullptr || b->vhdl == nullptr) {
                return false;
            }
            const auto& x = *a->vhdl;
            const auto& y = *b->vhdl;
            if (x.kind != y.kind || x.decoded_string != y.decoded_string
                || x.operands.size() != y.operands.size()
                || x.associations.size() != y.associations.size()
                || x.argument_names.size() != y.argument_names.size()) {
                return false;
            }
            if (x.kind == vh::ExpressionKind::string_literal) {
                if (x.text != y.text) {
                    return false;
                }
            } else if (identifier_key(x.text) != identifier_key(y.text)) {
                return false;
            }
            for (std::size_t index = 0; index < x.argument_names.size(); ++index) {
                if (identifier_key(x.argument_names[index])
                    != identifier_key(y.argument_names[index])) {
                    return false;
                }
            }
            for (std::size_t index = 0; index < x.operands.size(); ++index) {
                pending.emplace_back(x.operands[index], y.operands[index]);
            }
            for (std::size_t index = 0; index < x.associations.size(); ++index) {
                const auto& x_association = x.associations[index];
                const auto& y_association = y.associations[index];
                if (x_association.choices.size() != y_association.choices.size()
                    || identifier_key(x_association.choice_spelling)
                        != identifier_key(y_association.choice_spelling)) {
                    return false;
                }
                pending.emplace_back(x_association.value, y_association.value);
                for (std::size_t choice = 0; choice < x_association.choices.size(); ++choice) {
                    pending.emplace_back(x_association.choices[choice],
                        y_association.choices[choice]);
                }
            }
        }
        return true;
    }

    bool same_constraints(const semantic::CompiledDesign& design,
        const vh::SubtypeIndication& left, const vh::SubtypeIndication& right)
    {
        if (left.constraints.size() != right.constraints.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.constraints.size(); ++index) {
            const auto& a = left.constraints[index];
            const auto& b = right.constraints[index];
            if (a.kind != b.kind || a.left != b.left || a.right != b.right
                || a.descending != b.descending || a.null != b.null
                || !same_expression(design, a.left_expression, b.left_expression)
                || !same_expression(design, a.right_expression, b.right_expression)) {
                return false;
            }
        }
        return true;
    }

    bool subtype_conforms(const semantic::CompiledDesign& design,
        const semantic::CompiledDesignResolver& resolver,
        const vh::Declaration& specification, const vh::Declaration& body)
    {
        if (!specification.subtype || !body.subtype) {
            return false;
        }
        const auto& a = *specification.subtype;
        const auto& b = *body.subtype;
        if (!resolver.vhdl_subtype_profiles_match(a, specification.scope, b, body.scope)
            || identifier_key(a.type_mark.spelling) != identifier_key(b.type_mark.spelling)
            || identifier_key(a.resolution_function.spelling)
                != identifier_key(b.resolution_function.spelling)) {
            return false;
        }
        const auto effective_a = resolver.effective_vhdl_subtype(a, specification.scope).value_or(a);
        const auto effective_b = resolver.effective_vhdl_subtype(b, body.scope).value_or(b);
        return same_constraints(design, effective_a, effective_b);
    }

    struct Package {
        const vh::Unit* specification { };
        const vh::Unit* body { };
    };

    struct Completion {
        semantic::DeclarationId declaration;
        semantic::DeclarationId completion;
        semantic::SourceSpanId source;
    };

    void validate_constant(const semantic::CompiledDesign& design,
        const vh::Unit& package, const vh::Unit* body,
        const vh::Declaration& specification, diagnostic::Engine& diagnostics,
        std::vector<Completion>& completions)
    {
        const auto* completion = body != nullptr
            ? package_constant(design, *body, specification.name)
            : nullptr;
        if (!specification.deferred) {
            if (completion != nullptr) {
                diagnostics.error("FSIM-FE-VHDECL-003",
                    "VHDL package body redeclares nondeferred constant '"
                        + specification.name + "' from the package declaration",
                    source_span(design, completion->source));
            }
            return;
        }
        if (completion == nullptr || completion->deferred || !completion->initializer) {
            diagnostics.error("FSIM-FE-VHDECL-001",
                "deferred VHDL package constant '" + specification.name
                    + "' has no full declaration in the package body",
                source_span(design, specification.source));
            return;
        }
        const semantic::CompiledDesignResolver resolver { design, package.id };
        if (!subtype_conforms(design, resolver, specification, *completion)) {
            diagnostics.error("FSIM-FE-VHDECL-002",
                "full declaration of deferred VHDL package constant '"
                    + specification.name
                    + "' does not conform to its subtype indication",
                source_span(design, completion->source));
            return;
        }
        completions.push_back({ specification.id, completion->id, completion->source });
    }

    bool callable_names_conform(const semantic::CompiledDesign& design,
        const vh::Declaration& specification, const vh::Declaration& body)
    {
        const auto& a = *specification.callable;
        const auto& b = *body.callable;
        if (a.pure != b.pure || a.formals.size() != b.formals.size()) {
            return false;
        }
        for (std::size_t index = 0; index < a.formals.size(); ++index) {
            const auto* left = declaration(design, a.formals[index]);
            const auto* right = declaration(design, b.formals[index]);
            if (left == nullptr || right == nullptr
                || identifier_key(left->name) != identifier_key(right->name)) {
                return false;
            }
        }
        return true;
    }

    void validate_callable(const semantic::CompiledDesign& design,
        const vh::Unit& package, const vh::Declaration& specification,
        diagnostic::Engine& diagnostics)
    {
        const semantic::CompiledDesignResolver resolver { design, package.id };
        const auto resolution = resolver.resolve_vhdl_callable(specification.id);
        const vh::Declaration* body = nullptr;
        if (resolution.status == semantic::CompiledResolutionStatus::unique
            && resolution.candidates.size() == 1U) {
            body = declaration(design, resolution.candidates.front().body);
        }
        if (body == nullptr || !body->callable
            || !callable_names_conform(design, specification, *body)) {
            diagnostics.error("FSIM-FE-VHDECL-004",
                "VHDL package subprogram '" + specification.name
                    + "' has no unique conforming body",
                source_span(design, specification.source));
        }
    }

} // namespace

bool validate_and_link_vhdl_compiled_environment(
    semantic::CompiledDesign& design, diagnostic::Engine& diagnostics,
    const bool allow_missing_package_bodies)
{
    std::map<std::string, Package> packages;
    for (const auto& unit : design.vhdl_units()) {
        if (unit.kind != vh::UnitKind::package
            || !unit.standard_package_revision.empty()) {
            continue;
        }
        auto& package = packages[package_key(unit)];
        if (unit.primary_name.empty()) {
            package.specification = &unit;
        } else {
            package.body = &unit;
        }
    }
    std::vector<Completion> completions;
    for (const auto& [key, package] : packages) {
        if (package.specification == nullptr) {
            diagnostics.error("FSIM-FE-VHORDER-002",
                "VHDL package '" + package.body->library + "."
                    + package.body->name + "' must be analyzed before its body",
                source_span(design, package.body->source));
            continue;
        }
        const auto& specification = *package.specification;
        if (package.body == nullptr && allow_missing_package_bodies) {
            continue;
        }
        if (package.body != nullptr
            && (specification.standard != package.body->standard
                || specification.compatibility_profile != package.body->compatibility_profile)) {
            diagnostics.error("FSIM-FE-VHORDER-011",
                "VHDL package body '" + specification.library + "."
                    + specification.name
                    + "' uses a different standard or compatibility profile from its declaration",
                source_span(design, package.body->source));
            continue;
        }
        for (const auto id : specification.declarations) {
            const auto* item = declaration(design, id);
            if (item == nullptr) {
                continue;
            }
            if (item->form == vh::DeclarationForm::constant) {
                validate_constant(design, specification, package.body, *item,
                    diagnostics, completions);
            } else if ((item->form == vh::DeclarationForm::function
                           || item->form == vh::DeclarationForm::procedure)
                && item->callable && !item->callable->defined) {
                validate_callable(design, specification, *item, diagnostics);
            }
        }
    }
    if (diagnostics.has_error()) {
        return false;
    }
    if (!completions.empty()) {
        std::map<semantic::DeclarationId, Completion> indexed;
        for (const auto& completion : completions) {
            indexed.emplace(completion.declaration, completion);
        }
        for (auto& item : design.mutable_vhdl().mutable_declarations()) {
            if (const auto found = indexed.find(item.id); found != indexed.end()) {
                item.completion = found->second.completion;
                item.completion_source = found->second.source;
            }
        }
        design.refresh_lookup_indexes();
    }
    return true;
}

} // namespace fsim::app::application_detail
