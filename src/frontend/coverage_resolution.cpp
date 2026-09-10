// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_resolution.hpp"
#include "fsim/frontend/coverage_cross_inventory.hpp"
#include "fsim/frontend/coverage_limits.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string_view>

namespace fsim::frontend {

namespace {

    struct CovergroupEntry {
        SystemVerilogCovergroupDeclaration* declaration { };
        std::string owner_identity;
        std::string lexical_identity;
        std::string class_identity;
        std::string base_class_identity;
        std::set<std::string> owner_objects;
    };

    struct ClassCoverageEntry {
        std::string identity;
        std::string base_identity;
        std::set<std::string> objects;
    };

    void diagnose(
        std::vector<Diagnostic>& diagnostics,
        std::string code,
        std::string message,
        const SourceSpan& span)
    {
        diagnostics.push_back({ DiagnosticSeverity::Error,
            std::move(code),
            std::move(message),
            span,
            span.expansion_stack });
    }

    [[nodiscard]] std::string unit_identity(const DesignUnit& unit)
    {
        return (unit.library.empty() ? std::string { "work" } : unit.library)
            + "." + unit.name;
    }

    [[nodiscard]] std::string token_spelling(
        const std::span<const Token> tokens)
    {
        std::string result;
        for (const auto& token : tokens)
            result += token.text;
        return result;
    }

    [[nodiscard]] std::string expression_identity(
        const Expression& expression)
    {
        std::ostringstream result;
        result << static_cast<unsigned>(expression.kind) << ':' << expression.text;
        if (!expression.call_argument_names.empty()) {
            result << '[';
            for (const auto& name : expression.call_argument_names) {
                result << name << ';';
            }
            result << ']';
        }
        result << '(';
        for (const auto& operand : expression.operands) {
            result << expression_identity(operand) << ';';
        }
        result << ')';
        return result.str();
    }

    [[nodiscard]] bool coverage_keyword(const std::string_view text)
    {
        static const std::set<std::string_view> keywords {
            "iff", "with", "function", "sample", "posedge", "negedge",
            "edge", "or", "and", "not", "inside", "intersect", "binsof",
            "bins", "illegal_bins", "ignore_bins", "option", "type_option",
            "this", "super"
        };
        return keywords.contains(text)
            || (!text.empty() && text.front() == '$');
    }

    [[nodiscard]] std::set<std::string> unit_objects(
        const DesignUnit& unit)
    {
        std::set<std::string> result;
        for (const auto& port : unit.ports)
            result.insert(port.name);
        for (const auto& signal : unit.signals)
            result.insert(signal.name);
        for (const auto& variable : unit.variables)
            result.insert(variable.name);
        for (const auto& parameter : unit.parameters)
            result.insert(parameter.name);
        for (const auto& function : unit.functions)
            result.insert(function.name);
        for (const auto& task : unit.tasks)
            result.insert(task.name);
        return result;
    }

    void refresh_specialization_identity(
        SystemVerilogCovergroupDeclaration& declaration)
    {
        std::string profile;
        for (const auto& formal : declaration.formals) {
            profile += formal.name + ":"
                + token_spelling(formal.type_tokens) + "="
                + token_spelling(formal.default_tokens) + ";";
        }
        if (declaration.sampling) {
            profile += declaration.sampling->kind
                    == SystemVerilogCovergroupSamplingKind::WithFunctionSample
                ? "sample:"
                : "event:";
            for (const auto& formal : declaration.sampling->formals) {
                profile += formal.name + ":"
                    + token_spelling(formal.type_tokens) + ";";
            }
            if (declaration.sampling->kind
                == SystemVerilogCovergroupSamplingKind::Event) {
                profile += token_spelling(declaration.sampling->tokens);
            }
        }
        declaration.specialization_identity
            = declaration.canonical_identity + "<" + profile + ">";
    }

    void add_entry(
        std::vector<CovergroupEntry>& entries,
        SystemVerilogCovergroupDeclaration& declaration,
        std::string owner,
        std::string lexical,
        std::set<std::string> objects,
        std::string class_identity = { },
        std::string base_class_identity = { })
    {
        declaration.owner_identity = owner;
        declaration.canonical_identity = owner + "::" + declaration.name;
        declaration.runtime_identity_prefix = declaration.canonical_identity + "@";
        refresh_specialization_identity(declaration);
        entries.push_back({ &declaration,
            std::move(owner),
            std::move(lexical),
            std::move(class_identity),
            std::move(base_class_identity),
            std::move(objects) });
    }

    void collect_class_entries(
        SystemVerilogClassDeclaration& class_declaration,
        const std::string& library,
        const std::set<std::string>& enclosing_objects,
        std::vector<CovergroupEntry>& entries,
        std::vector<ClassCoverageEntry>& classes)
    {
        auto objects = enclosing_objects;
        for (const auto& property : class_declaration.properties) {
            objects.insert(property.declaration.name);
        }
        for (const auto& method : class_declaration.methods) {
            objects.insert(method.name);
        }
        const auto owner = library + "." + class_declaration.canonical_identity;
        const auto class_identity = class_declaration.canonical_identity;
        const auto base_class_identity = class_declaration.base
            ? class_declaration.base->declaration_identity
            : std::string { };
        classes.push_back(
            { class_identity, base_class_identity, objects });
        for (auto& covergroup : class_declaration.covergroups) {
            add_entry(
                entries,
                covergroup,
                owner,
                class_declaration.canonical_identity,
                objects,
                class_identity,
                base_class_identity);
        }
        for (auto& nested : class_declaration.nested_classes) {
            collect_class_entries(
                nested, library, objects, entries, classes);
        }
    }

    [[nodiscard]] std::vector<CovergroupEntry*> find_covergroups(
        std::vector<CovergroupEntry>& entries,
        const std::string_view type_name,
        const std::string_view owner_identity)
    {
        std::vector<CovergroupEntry*> result;
        for (auto& entry : entries) {
            const auto& declaration = *entry.declaration;
            const bool exact = declaration.canonical_identity == type_name
                || entry.lexical_identity + "::" + declaration.name == type_name;
            const bool local = entry.owner_identity == owner_identity
                && declaration.name == type_name;
            const bool qualified_suffix = type_name.find("::") != std::string_view::npos
                && declaration.canonical_identity.ends_with(
                    std::string { type_name });
            if (exact || local || qualified_suffix)
                result.push_back(&entry);
        }
        return result;
    }

    void collect_references(
        SystemVerilogCovergroupDeclaration& declaration,
        SystemVerilogCoverageDeclaration& item,
        const std::span<const Token> tokens,
        const std::set<std::string>& constructor_formals,
        const std::set<std::string>& sample_formals,
        const std::map<std::string, std::size_t>& coverpoints,
        const std::set<std::string>& owner_objects,
        std::vector<Diagnostic>& diagnostics)
    {
        std::size_t position { };
        while (position < tokens.size()) {
            if (tokens[position].kind != TokenKind::Identifier
                || coverage_keyword(tokens[position].text)) {
                ++position;
                continue;
            }
            const auto begin = position;
            std::string name = tokens[position].text;
            bool qualified { };
            while (position + 2U < tokens.size()
                && (tokens[position + 1U].kind == TokenKind::Dot
                    || tokens[position + 1U].kind == TokenKind::Scope)
                && tokens[position + 2U].kind == TokenKind::Identifier) {
                name += tokens[position + 1U].kind == TokenKind::Dot ? "." : "::";
                name += tokens[position + 2U].text;
                position += 2U;
                qualified = true;
            }
            SystemVerilogCoverageReference reference;
            if (qualified) {
                reference.kind = SystemVerilogCoverageReferenceKind::Qualified;
                reference.canonical_name = name;
            } else if (constructor_formals.contains(name)) {
                reference.kind = SystemVerilogCoverageReferenceKind::ConstructorFormal;
                reference.canonical_name = declaration.canonical_identity + "::" + name;
            } else if (sample_formals.contains(name)) {
                reference.kind = SystemVerilogCoverageReferenceKind::SampleFormal;
                reference.canonical_name = declaration.canonical_identity + "::sample::" + name;
            } else if (const auto found = coverpoints.find(name);
                found != coverpoints.end()) {
                reference.kind = SystemVerilogCoverageReferenceKind::Coverpoint;
                reference.canonical_name = declaration.canonical_identity + "::" + name;
            } else if (owner_objects.contains(name)) {
                reference.kind = SystemVerilogCoverageReferenceKind::OwnerObject;
                reference.canonical_name = declaration.owner_identity + "::" + name;
            } else {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-208",
                    "coverage expression name '" + name
                        + "' is not visible from covergroup '"
                        + declaration.canonical_identity + "'",
                    tokens[begin].span);
                ++position;
                continue;
            }
            reference.tokens.assign(
                tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                tokens.begin() + static_cast<std::ptrdiff_t>(position + 1U));
            reference.span = cover(
                reference.tokens.front().span,
                reference.tokens.back().span);
            item.references.push_back(std::move(reference));
            ++position;
        }
    }

    void resolve_declaration(
        CovergroupEntry& entry,
        std::vector<Diagnostic>& diagnostics)
    {
        auto& declaration = *entry.declaration;
        std::set<std::string> constructor_formals;
        for (const auto& formal : declaration.formals) {
            constructor_formals.insert(formal.name);
        }
        std::set<std::string> sample_formals;
        if (declaration.sampling) {
            for (const auto& formal : declaration.sampling->formals) {
                sample_formals.insert(formal.name);
            }
        }
        std::map<std::string, std::size_t> coverpoints;
        for (const auto& item : declaration.coverage_declarations) {
            if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
                coverpoints.emplace(item.name, item.declaration_index);
            }
        }
        for (auto& item : declaration.coverage_declarations) {
            item.references.clear();
            if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
                collect_references(
                    declaration,
                    item,
                    item.expression_tokens,
                    constructor_formals,
                    sample_formals,
                    coverpoints,
                    entry.owner_objects,
                    diagnostics);
            } else {
                for (auto& operand : item.cross_operands) {
                    if (item.inherited
                        && operand.resolved_declaration_index) {
                        continue;
                    }
                    if (const auto found = coverpoints.find(operand.name);
                        found != coverpoints.end()) {
                        operand.resolved_declaration_index = found->second;
                    } else if (
                        constructor_formals.contains(operand.name)
                        || sample_formals.contains(operand.name)
                        || entry.owner_objects.contains(operand.name)) {
                        operand.implicit_coverpoint = true;
                    } else {
                        diagnose(
                            diagnostics,
                            "FSIM-SV-SEM-209",
                            "cross operand '" + operand.name
                                + "' does not name a coverpoint or visible expression",
                            operand.span);
                    }
                }
                for (const auto& bin : item.bins) {
                    const auto& tokens = bin.cross_selection_tokens;
                    bool saw_selection = std::ranges::any_of(
                        tokens,
                        [&](const Token& token) {
                            return token.kind == TokenKind::Identifier
                                && token.text == item.name;
                        });
                    for (std::size_t index = 0; index < tokens.size(); ++index) {
                        if (tokens[index].text != "binsof")
                            continue;
                        saw_selection = true;
                        if (index + 3U >= tokens.size()
                            || tokens[index + 1U].kind != TokenKind::LeftParen) {
                            diagnose(
                                diagnostics,
                                "FSIM-SV-SEM-216",
                                "cross bin '" + bin.name
                                    + "' has an empty or malformed binsof selection",
                                bin.span);
                            break;
                        }
                        std::size_t right = index + 2U;
                        int depth { 1 };
                        for (; right < tokens.size(); ++right) {
                            if (tokens[right].kind == TokenKind::LeftParen)
                                ++depth;
                            if (tokens[right].kind == TokenKind::RightParen)
                                --depth;
                            if (depth == 0)
                                break;
                        }
                        if (right >= tokens.size() || right == index + 2U
                            || tokens[index + 2U].kind != TokenKind::Identifier) {
                            diagnose(
                                diagnostics,
                                "FSIM-SV-SEM-216",
                                "cross bin '" + bin.name
                                    + "' has an empty or malformed binsof selection",
                                bin.span);
                            break;
                        }
                        const auto operand_name = tokens[index + 2U].text;
                        const auto operand_count = std::ranges::count(
                            item.cross_operands,
                            operand_name,
                            &SystemVerilogCoverageCrossOperand::name);
                        if (operand_count != 1) {
                            diagnose(
                                diagnostics,
                                "FSIM-SV-SEM-216",
                                "binsof selection '" + operand_name
                                    + "' is empty or ambiguous in cross '" + item.name + "'",
                                tokens[index + 2U].span);
                            break;
                        }
                        std::optional<std::string> bin_name;
                        if (index + 4U < right
                            && tokens[index + 3U].kind == TokenKind::Dot
                            && tokens[index + 4U].kind == TokenKind::Identifier) {
                            bin_name = tokens[index + 4U].text;
                        } else if (right + 2U < tokens.size()
                            && tokens[right + 1U].kind == TokenKind::Dot
                            && tokens[right + 2U].kind == TokenKind::Identifier) {
                            bin_name = tokens[right + 2U].text;
                        }
                        if (bin_name) {
                            const auto operand = std::ranges::find(
                                item.cross_operands,
                                operand_name,
                                &SystemVerilogCoverageCrossOperand::name);
                            if (operand == item.cross_operands.end()
                                || !operand->resolved_declaration_index) {
                                diagnose(
                                    diagnostics,
                                    "FSIM-SV-SEM-216",
                                    "named bin selection '" + operand_name + "." + *bin_name
                                        + "' has no resolved coverpoint",
                                    bin.span);
                                break;
                            }
                            const auto& coverpoint = declaration.coverage_declarations[*operand->resolved_declaration_index];
                            const auto found_bin = std::ranges::find_if(
                                coverpoint.bins,
                                [&](const SystemVerilogCoverageBin& candidate) {
                                    return candidate.name == *bin_name
                                        || candidate.source_name == *bin_name;
                                });
                            if (found_bin == coverpoint.bins.end()) {
                                diagnose(
                                    diagnostics,
                                    "FSIM-SV-SEM-216",
                                    "named bin selection '" + operand_name + "." + *bin_name
                                        + "' is empty",
                                    bin.span);
                                break;
                            }
                        }
                        index = right;
                    }
                    if (!saw_selection) {
                        diagnose(
                            diagnostics,
                            "FSIM-SV-SEM-216",
                            "cross bin '" + bin.name
                                + "' has no binsof selection",
                            bin.span);
                    }
                }
            }
            collect_references(
                declaration,
                item,
                item.iff_tokens,
                constructor_formals,
                sample_formals,
                coverpoints,
                entry.owner_objects,
                diagnostics);
        }
    }

    void apply_inherited_covergroup_option(
        SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCovergroupOptionAssignment& option)
    {
        if (option.name == "real_interval"
            && option.evaluated_real_bits) {
            declaration.effective_real_interval_bits
                = option.evaluated_real_bits;
            return;
        }
        if (!option.evaluated_value) {
            return;
        }
        const auto value = *option.evaluated_value;
        if (option.name == "weight") {
            auto& target
                = option.scope == SystemVerilogCovergroupOptionScope::Type
                ? declaration.effective_type_weight
                : declaration.effective_instance_weight;
            target = static_cast<std::uint32_t>(value);
        } else if (option.name == "goal") {
            auto& target
                = option.scope == SystemVerilogCovergroupOptionScope::Type
                ? declaration.effective_type_goal
                : declaration.effective_instance_goal;
            target = static_cast<std::uint32_t>(value);
        } else if (option.name == "per_instance") {
            declaration.effective_per_instance = value != 0U;
        } else if (option.name == "merge_instances") {
            declaration.effective_merge_instances = value != 0U;
        } else if (option.name == "cross_retain_auto_bins") {
            declaration.effective_cross_retain_auto_bins = value != 0U;
        }
    }

    [[nodiscard]] std::optional<std::size_t> find_inherited_covergroup(
        std::vector<CovergroupEntry>& entries,
        const std::vector<ClassCoverageEntry>& classes,
        CovergroupEntry& entry)
    {
        auto class_identity = entry.base_class_identity;
        std::set<std::string> visited;
        while (!class_identity.empty() && visited.insert(class_identity).second) {
            const auto class_entry = std::ranges::find(
                classes, class_identity, &ClassCoverageEntry::identity);
            if (class_entry == classes.end()) {
                break;
            }
            entry.owner_objects.insert(
                class_entry->objects.begin(), class_entry->objects.end());
            if (const auto found = std::ranges::find_if(
                    entries,
                    [&](const CovergroupEntry& candidate) {
                        return candidate.class_identity == class_identity
                            && candidate.declaration->name
                                == entry.declaration->name;
                    });
                found != entries.end()) {
                return static_cast<std::size_t>(
                    std::distance(entries.begin(), found));
            }
            class_identity = class_entry->base_identity;
        }
        return std::nullopt;
    }

    bool resolve_entry(
        const std::size_t index,
        std::vector<CovergroupEntry>& entries,
        const std::vector<ClassCoverageEntry>& classes,
        std::vector<unsigned char>& states,
        std::vector<Diagnostic>& diagnostics)
    {
        if (states[index] == 2U) {
            return true;
        }
        auto& entry = entries[index];
        auto& declaration = *entry.declaration;
        if (states[index] == 1U) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-263",
                "covergroup inheritance forms a cycle at '"
                    + declaration.canonical_identity + "'",
                declaration.span);
            return false;
        }
        states[index] = 1U;

        std::erase_if(
            declaration.coverage_declarations,
            [](const SystemVerilogCoverageDeclaration& item) {
                return item.inherited;
            });
        std::erase_if(
            declaration.option_assignments,
            [](const SystemVerilogCovergroupOptionAssignment& option) {
                return option.inherited;
            });
        for (std::size_t item_index = 0U;
             item_index < declaration.coverage_declarations.size();
             ++item_index) {
            auto& item = declaration.coverage_declarations[item_index];
            item.declaration_index = item_index;
            if (item.origin_covergroup_identity.empty()) {
                item.origin_covergroup_identity
                    = declaration.canonical_identity;
            }
        }

        if (declaration.extends_parent) {
            const auto base_index
                = find_inherited_covergroup(entries, classes, entry);
            if (!base_index) {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-262",
                    "covergroup '" + declaration.name
                        + "' has no declaration in a parent class",
                    declaration.name_span);
                states[index] = 2U;
                return false;
            }
            if (!resolve_entry(
                    *base_index, entries, classes, states, diagnostics)) {
                states[index] = 2U;
                return false;
            }
            const auto& base = *entries[*base_index].declaration;
            declaration.resolved_base_identity = base.canonical_identity;
            declaration.formals = base.formals;
            declaration.sampling = base.sampling;
            declaration.effective_instance_weight
                = base.effective_instance_weight;
            declaration.effective_instance_goal
                = base.effective_instance_goal;
            declaration.effective_type_weight = base.effective_type_weight;
            declaration.effective_type_goal = base.effective_type_goal;
            declaration.effective_per_instance = base.effective_per_instance;
            declaration.effective_merge_instances
                = base.effective_merge_instances;
            declaration.effective_cross_retain_auto_bins
                = base.effective_cross_retain_auto_bins;
            declaration.effective_real_interval_bits
                = base.effective_real_interval_bits;

            auto local_options = std::move(declaration.option_assignments);
            declaration.option_assignments = base.option_assignments;
            for (auto& option : declaration.option_assignments) {
                option.inherited = true;
            }
            for (auto& option : local_options) {
                option.inherited = false;
                apply_inherited_covergroup_option(declaration, option);
                declaration.option_assignments.push_back(std::move(option));
            }

            const auto offset = declaration.coverage_declarations.size();
            std::map<std::size_t, std::size_t> inherited_indices;
            for (std::size_t base_item_index = 0U;
                 base_item_index < base.coverage_declarations.size();
                 ++base_item_index) {
                inherited_indices.emplace(
                    base.coverage_declarations[base_item_index].declaration_index,
                    offset + base_item_index);
            }
            for (const auto& base_item : base.coverage_declarations) {
                auto inherited = base_item;
                inherited.inherited = true;
                inherited.declaration_index
                    = declaration.coverage_declarations.size();
                for (auto& operand : inherited.cross_operands) {
                    if (!operand.resolved_declaration_index) {
                        continue;
                    }
                    const auto remapped = inherited_indices.find(
                        *operand.resolved_declaration_index);
                    operand.resolved_declaration_index
                        = remapped == inherited_indices.end()
                        ? std::optional<std::size_t> { }
                        : std::optional<std::size_t> { remapped->second };
                }
                declaration.coverage_declarations.push_back(
                    std::move(inherited));
            }
        } else {
            declaration.resolved_base_identity.clear();
        }

        refresh_specialization_identity(declaration);
        resolve_declaration(entry, diagnostics);
        states[index] = 2U;
        return true;
    }

    void validate_actuals(
        const SystemVerilogCovergroupDeclaration& declaration,
        const Expression& construction,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto& formals = declaration.formals;
        const auto& actuals = construction.operands;
        std::vector<bool> assigned(formals.size(), false);
        bool malformed { };
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            std::size_t formal_index = index;
            if (index < construction.call_argument_names.size()
                && !construction.call_argument_names[index].empty()) {
                const auto found = std::ranges::find(
                    formals,
                    construction.call_argument_names[index],
                    &SystemVerilogCovergroupFormal::name);
                if (found == formals.end()) {
                    malformed = true;
                    continue;
                }
                formal_index = static_cast<std::size_t>(
                    std::distance(formals.begin(), found));
            }
            if (formal_index >= formals.size() || assigned[formal_index]) {
                malformed = true;
                continue;
            }
            assigned[formal_index] = true;
        }
        for (std::size_t index = 0; index < formals.size(); ++index) {
            if (!assigned[index] && formals[index].default_tokens.empty()) {
                malformed = true;
            }
        }
        if (malformed) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-210",
                "covergroup constructor actuals do not match profile '"
                    + declaration.canonical_identity + "'",
                construction.span);
        }
    }

    [[nodiscard]] SystemVerilogScalarKind coverage_formal_scalar_kind(
        const std::span<const Token> tokens)
    {
        if (std::ranges::any_of(tokens, [](const Token& token) {
                return token.text == "shortreal";
            })) {
            return SystemVerilogScalarKind::ShortReal;
        }
        if (std::ranges::any_of(tokens, [](const Token& token) {
                return token.text == "realtime";
            })) {
            return SystemVerilogScalarKind::Realtime;
        }
        return std::ranges::any_of(tokens, [](const Token& token) {
                   return token.text == "real";
               })
            ? SystemVerilogScalarKind::Real
            : SystemVerilogScalarKind::None;
    }

    void add_instance(
        ParsedDesign& design,
        const std::string& owner,
        const VariableDeclaration& variable,
        CovergroupEntry& entry,
        std::vector<Diagnostic>& diagnostics)
    {
        SystemVerilogCovergroupInstance instance;
        instance.name = variable.name;
        instance.owner_identity = owner;
        instance.declaration_identity = entry.declaration->canonical_identity;
        instance.initial_option_state = entry.declaration->option_assignments;
        instance.span = variable.span;
        std::string actual_identity;
        if (variable.initializer
            && variable.initializer->kind == ExpressionKind::Call
            && variable.initializer->text == "@sv-new") {
            validate_actuals(*entry.declaration, *variable.initializer, diagnostics);
            instance.constructor_actuals = variable.initializer->operands;
            actual_identity = expression_identity(*variable.initializer);
        } else if (!entry.declaration->formals.empty()) {
            const auto missing_required = std::ranges::any_of(
                entry.declaration->formals,
                [](const SystemVerilogCovergroupFormal& formal) {
                    return formal.default_tokens.empty();
                });
            if (missing_required) {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-210",
                    "covergroup instance '" + variable.name
                        + "' has no constructor actuals",
                    variable.span);
            }
        }
        instance.specialization_identity = entry.declaration->specialization_identity + "("
            + actual_identity + ")";
        instance.runtime_identity = owner + "." + variable.name + "@"
            + instance.specialization_identity;
        if (!initialize_systemverilog_cross_inventory(
                instance, *entry.declaration, diagnostics)) {
            return;
        }
        design.systemverilog_covergroup_instances.push_back(
            std::move(instance));
    }

    void collect_sample_calls(
        ParsedDesign& design,
        const std::string& owner,
        std::vector<Statement>& statements,
        const std::vector<CovergroupEntry>& entries,
        std::vector<Diagnostic>& diagnostics);

    [[nodiscard]] std::optional<Expression> coverage_scalar_expression(
        std::span<const Token> tokens)
    {
        const auto span = [](const std::span<const Token> value) {
            return SourceSpan {
                value.front().span.source_name,
                value.front().span.begin,
                value.back().span.end,
                value.front().span.physical_source_name,
                value.front().span.expansion_stack
            };
        };
        while (tokens.size() >= 2U
            && tokens.front().kind == TokenKind::LeftParen
            && tokens.back().kind == TokenKind::RightParen) {
            int depth { };
            bool encloses { true };
            for (std::size_t index = 0U; index < tokens.size(); ++index) {
                if (tokens[index].kind == TokenKind::LeftParen) {
                    ++depth;
                } else if (tokens[index].kind == TokenKind::RightParen) {
                    --depth;
                    if (depth == 0 && index + 1U != tokens.size()) {
                        encloses = false;
                        break;
                    }
                }
            }
            if (!encloses || depth != 0) {
                break;
            }
            tokens = tokens.subspan(1U, tokens.size() - 2U);
        }
        if (tokens.empty()) {
            return std::nullopt;
        }
        if (tokens.size() == 1U
            && (tokens.front().kind == TokenKind::Identifier
                || tokens.front().kind == TokenKind::Number)) {
            return Expression {
                tokens.front().kind == TokenKind::Identifier
                    ? ExpressionKind::Identifier
                    : tokens.front().text.find('\'') == std::string::npos
                    ? ExpressionKind::IntegerLiteral
                    : ExpressionKind::LogicLiteral,
                tokens.front().text,
                { },
                tokens.front().span
            };
        }
        const auto precedence = [](const std::string_view operation)
            -> std::optional<unsigned> {
            if (operation == "||" || operation == "or")
                return 1U;
            if (operation == "&&" || operation == "and")
                return 2U;
            if (operation == "|")
                return 3U;
            if (operation == "^" || operation == "^~" || operation == "~^")
                return 4U;
            if (operation == "&")
                return 5U;
            if (operation == "==" || operation == "!="
                || operation == "===" || operation == "!==")
                return 6U;
            if (operation == "<" || operation == "<="
                || operation == ">" || operation == ">=")
                return 7U;
            if (operation == "<<" || operation == ">>"
                || operation == "<<<" || operation == ">>>")
                return 8U;
            if (operation == "+" || operation == "-")
                return 9U;
            if (operation == "*" || operation == "/" || operation == "%")
                return 10U;
            return std::nullopt;
        };
        std::optional<std::size_t> split;
        unsigned split_precedence { };
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            const auto& token = tokens[index];
            if (token.kind == TokenKind::LeftParen)
                ++parentheses;
            else if (token.kind == TokenKind::RightParen)
                --parentheses;
            else if (token.kind == TokenKind::LeftBracket)
                ++brackets;
            else if (token.kind == TokenKind::RightBracket)
                --brackets;
            else if (token.kind == TokenKind::LeftBrace)
                ++braces;
            else if (token.kind == TokenKind::RightBrace)
                --braces;
            if (index == 0U || parentheses != 0 || brackets != 0 || braces != 0)
                continue;
            const auto candidate = precedence(token.text);
            if (candidate
                && (!split || *candidate <= split_precedence)) {
                split = index;
                split_precedence = *candidate;
            }
        }
        if (split && *split + 1U < tokens.size()) {
            auto left = coverage_scalar_expression(tokens.first(*split));
            auto right = coverage_scalar_expression(tokens.subspan(*split + 1U));
            if (left && right) {
                const auto operation = tokens[*split].text == "and"
                    ? "&&"
                    : tokens[*split].text == "or"
                    ? "||"
                    : tokens[*split].text;
                return Expression {
                    ExpressionKind::Binary,
                    operation,
                    { std::move(*left), std::move(*right) },
                    span(tokens)
                };
            }
        }
        if (tokens.size() >= 2U
            && (tokens.front().kind == TokenKind::Bang
                || tokens.front().kind == TokenKind::Tilde
                || tokens.front().kind == TokenKind::Plus
                || tokens.front().kind == TokenKind::Minus
                || tokens.front().text == "not")) {
            auto operand = coverage_scalar_expression(tokens.subspan(1U));
            if (operand) {
                return Expression {
                    ExpressionKind::Unary,
                    tokens.front().text == "not"
                        ? "!"
                        : tokens.front().text,
                    { std::move(*operand) },
                    span(tokens)
                };
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<Expression>>
    direct_coverpoint_actuals(
        const SystemVerilogCovergroupDeclaration& declaration,
        std::vector<Diagnostic>& diagnostics,
        const SourceSpan& span)
    {
        std::vector<Expression> actuals;
        for (const auto& item : declaration.coverage_declarations) {
            if (item.kind
                != SystemVerilogCoverageDeclarationKind::Coverpoint) {
                continue;
            }
            auto expression = coverage_scalar_expression(item.expression_tokens);
            if (!expression) {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-244",
                    "executable explicit/event covergroup sampling currently "
                    "requires each coverpoint to be a scalar expression composed "
                    "of supported unary and binary operators",
                    item.expression_span.source_name.empty()
                        ? span
                        : item.expression_span);
                return std::nullopt;
            }
            expression->span = item.expression_span;
            actuals.push_back(std::move(*expression));
        }
        return actuals;
    }

    void add_event_sample_process(
        DesignUnit& unit,
        const SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCovergroupDeclaration& declaration,
        std::vector<Diagnostic>& diagnostics)
    {
        if (!declaration.sampling
            || declaration.sampling->kind
                != SystemVerilogCovergroupSamplingKind::Event) {
            return;
        }
        auto task_identity = "@sv-coverage-sample-event:"
            + instance.runtime_identity;
        const auto contains_task
            = [&](const auto& self, const std::vector<Statement>& statements) -> bool {
            return std::ranges::any_of(
                statements, [&](const Statement& statement) {
                    if (statement.task_name == task_identity) {
                        return true;
                    }
                    if (self(self, statement.statements)
                        || self(self, statement.else_statements)) {
                        return true;
                    }
                    return std::ranges::any_of(
                        statement.case_alternatives,
                        [&](const CaseAlternative& alternative) {
                            return self(self, alternative.statements);
                        });
                });
        };
        const auto already_synthesized = std::ranges::any_of(
            unit.processes, [&](const Process& process) {
                return contains_task(contains_task, process.statements);
            });
        if (already_synthesized) {
            return;
        }
        const auto& sampling = *declaration.sampling;
        const auto guard_position = std::ranges::find_if(
            sampling.tokens, [](const Token& token) {
                return token.text == "iff";
            });
        const auto event_end = guard_position == sampling.tokens.end()
            ? sampling.tokens.size()
            : static_cast<std::size_t>(
                  guard_position - sampling.tokens.begin());
        std::vector<const Token*> event_objects;
        for (std::size_t position = 0U; position < event_end; ++position) {
            const auto& token = sampling.tokens[position];
            if (token.kind == TokenKind::Identifier
                && !coverage_keyword(token.text)) {
                event_objects.push_back(&token);
            }
        }
        std::optional<Expression> guard;
        if (guard_position != sampling.tokens.end()) {
            auto guard_tokens = std::span<const Token> { sampling.tokens }.subspan(
                event_end + 1U);
            if (!guard_tokens.empty()
                && guard_tokens.back().kind == TokenKind::RightParen) {
                guard_tokens = guard_tokens.first(guard_tokens.size() - 1U);
            }
            if (guard_tokens.size() >= 2U
                && guard_tokens.front().kind == TokenKind::LeftParen
                && guard_tokens.back().kind == TokenKind::RightParen) {
                guard_tokens = guard_tokens.subspan(
                    1U, guard_tokens.size() - 2U);
            }
            guard = coverage_scalar_expression(guard_tokens);
        }
        if (event_objects.empty()
            || (guard_position != sampling.tokens.end() && !guard)) {
            diagnose(
                diagnostics,
                "FSIM-SV-SEM-244",
                "executable event covergroup sampling currently requires direct "
                "owner event objects with optional edges and a scalar iff guard",
                sampling.span);
            return;
        }
        auto actuals = direct_coverpoint_actuals(
            declaration, diagnostics, sampling.span);
        if (!actuals) {
            return;
        }
        Process process;
        process.kind = ProcessKind::VerilogAlways;
        process.name = "$coverage$" + instance.name;
        process.span = sampling.span;
        for (const auto* event_object : event_objects) {
            const auto position = static_cast<std::size_t>(
                event_object - sampling.tokens.data());
            Sensitivity sensitivity;
            sensitivity.signal = event_object->text;
            sensitivity.span = sampling.span;
            sensitivity.edge = position != 0U
                    && sampling.tokens[position - 1U].text == "posedge"
                ? EdgeKind::Positive
                : position != 0U
                    && sampling.tokens[position - 1U].text == "negedge"
                ? EdgeKind::Negative
                : EdgeKind::Any;
            process.sensitivities.push_back(std::move(sensitivity));
        }
        Statement sample;
        sample.kind = StatementKind::TaskCall;
        sample.task_name = std::move(task_identity);
        sample.task_arguments = std::move(*actuals);
        sample.span = sampling.span;
        if (guard) {
            Statement gated;
            gated.kind = StatementKind::If;
            gated.condition = std::move(*guard);
            gated.span = sampling.span;
            gated.statements.push_back(std::move(sample));
            process.statements.push_back(std::move(gated));
        } else {
            process.statements.push_back(std::move(sample));
        }
        unit.processes.push_back(std::move(process));
    }

    void collect_class_instances(
        ParsedDesign& design,
        SystemVerilogClassDeclaration& declaration,
        const std::string& library,
        std::vector<CovergroupEntry>& entries,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto owner = library + "." + declaration.canonical_identity;
        for (auto& covergroup : declaration.covergroups) {
            SystemVerilogCovergroupInstance instance;
            instance.name = covergroup.name;
            instance.owner_identity = owner;
            instance.declaration_identity = covergroup.canonical_identity;
            instance.specialization_identity = covergroup.specialization_identity;
            instance.initial_option_state = covergroup.option_assignments;
            instance.runtime_identity = covergroup.runtime_identity_prefix + "<object>";
            instance.class_member_template = true;
            instance.span = covergroup.span;
            if (!initialize_systemverilog_cross_inventory(
                    instance, covergroup, diagnostics)) {
                continue;
            }
            design.systemverilog_covergroup_instances.push_back(
                std::move(instance));
        }
        for (const auto& property : declaration.properties) {
            auto candidates = find_covergroups(
                entries, property.declaration.type.named_type, owner);
            if (candidates.size() == 1U) {
                add_instance(
                    design,
                    owner,
                    property.declaration,
                    *candidates.front(),
                    diagnostics);
            } else if (candidates.size() > 1U) {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-211",
                    "covergroup type '" + property.declaration.type.named_type
                        + "' is ambiguous",
                    property.span);
            }
        }
        for (auto& method : declaration.methods) {
            collect_sample_calls(
                design, owner, method.statements, entries, diagnostics);
        }
        for (auto& nested : declaration.nested_classes) {
            collect_class_instances(
                design, nested, library, entries, diagnostics);
        }
    }

    void collect_sample_calls(
        ParsedDesign& design,
        const std::string& owner,
        std::vector<Statement>& statements,
        const std::vector<CovergroupEntry>& entries,
        std::vector<Diagnostic>& diagnostics)
    {
        for (auto& statement : statements) {
            const auto sample_call = statement.task_name.ends_with(".sample");
            const auto start_call = statement.task_name.ends_with(".start");
            const auto stop_call = statement.task_name.ends_with(".stop");
            if (sample_call || start_call || stop_call) {
                const auto suffix_size = sample_call ? 7U : start_call ? 6U
                                                                       : 5U;
                auto receiver = statement.task_name.substr(
                    0U, statement.task_name.size() - suffix_size);
                const auto dot = receiver.rfind('.');
                if (dot != std::string::npos)
                    receiver = receiver.substr(dot + 1U);
                const auto instance = std::ranges::find_if(
                    design.systemverilog_covergroup_instances,
                    [&](const SystemVerilogCovergroupInstance& candidate) {
                        return candidate.owner_identity == owner
                            && candidate.name == receiver;
                    });
                if (instance
                    != design.systemverilog_covergroup_instances.end()) {
                    if (!sample_call) {
                        if (!statement.task_arguments.empty()) {
                            diagnose(
                                diagnostics,
                                "FSIM-SV-SEM-210",
                                "covergroup start/stop methods do not accept actuals",
                                statement.span);
                        }
                        statement.task_name = start_call
                            ? "@sv-coverage-control-start:"
                            : "@sv-coverage-control-stop:";
                        statement.task_name += instance->runtime_identity;
                        statement.task_arguments.clear();
                        statement.task_argument_names.clear();
                        continue;
                    }
                    bool procedural_sample { };
                    const auto entry = std::ranges::find_if(
                        entries,
                        [&](const CovergroupEntry& candidate) {
                            return candidate.declaration->canonical_identity
                                == instance->declaration_identity;
                        });
                    if (entry != entries.end()) {
                        procedural_sample = entry->declaration->sampling
                            && entry->declaration->sampling->kind
                                == SystemVerilogCovergroupSamplingKind::WithFunctionSample;
                        const auto expected = procedural_sample
                            ? entry->declaration->sampling->formals.size()
                            : 0U;
                        if (statement.task_arguments.size() != expected) {
                            diagnose(
                                diagnostics,
                                "FSIM-SV-SEM-210",
                                "covergroup sample actuals do not match profile '"
                                    + entry->declaration->canonical_identity + "'",
                                statement.span);
                        } else if (procedural_sample) {
                            for (std::size_t index = 0U;
                                 index < statement.task_arguments.size();
                                 ++index) {
                                const auto scalar_kind
                                    = coverage_formal_scalar_kind(
                                        entry->declaration->sampling
                                            ->formals[index].type_tokens);
                                if (scalar_kind
                                    != SystemVerilogScalarKind::None) {
                                    statement.task_arguments[index]
                                        .systemverilog_scalar_kind
                                        = scalar_kind;
                                }
                            }
                        }
                    }
                    SystemVerilogCovergroupSampleCall call;
                    call.declaration_identity = instance->declaration_identity;
                    call.instance_identity = instance->runtime_identity;
                    call.actuals = statement.task_arguments;
                    call.span = statement.span;
                    instance->sample_calls.push_back(std::move(call));
                    if (!procedural_sample && entry != entries.end()) {
                        if (auto actuals = direct_coverpoint_actuals(
                                *entry->declaration, diagnostics, statement.span)) {
                            statement.task_arguments = std::move(*actuals);
                        }
                    }
                    statement.task_name = procedural_sample
                        ? "@sv-coverage-sample-procedural:"
                        : "@sv-coverage-sample-explicit:";
                    statement.task_name += instance->runtime_identity;
                }
            }
            collect_sample_calls(
                design, owner, statement.statements, entries, diagnostics);
            collect_sample_calls(
                design, owner, statement.else_statements, entries, diagnostics);
            for (auto& alternative : statement.case_alternatives) {
                collect_sample_calls(
                    design, owner, alternative.statements, entries, diagnostics);
            }
        }
    }

} // namespace

bool resolve_systemverilog_covergroups(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics)
{
    const auto initial_diagnostic_count = diagnostics.size();
    design.systemverilog_covergroup_instances.clear();
    std::vector<CovergroupEntry> entries;
    std::vector<ClassCoverageEntry> classes;
    for (auto& unit : design.units) {
        if (unit.language != Language::SystemVerilog2017)
            continue;
        const auto owner = unit_identity(unit);
        const auto objects = unit_objects(unit);
        for (auto& covergroup : unit.systemverilog_covergroups) {
            add_entry(entries, covergroup, owner, unit.name, objects);
        }
        const auto library = unit.library.empty() ? std::string { "work" } : unit.library;
        for (auto& declaration : unit.systemverilog_classes) {
            collect_class_entries(
                declaration, library, objects, entries, classes);
        }
    }
    for (auto& declaration : design.systemverilog_classes) {
        collect_class_entries(
            declaration, "work", { }, entries, classes);
    }
    std::vector<unsigned char> resolution_states(entries.size(), 0U);
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        (void)resolve_entry(
            index, entries, classes, resolution_states, diagnostics);
        (void)validate_systemverilog_coverage_resources(
            *entries[index].declaration, diagnostics);
    }
    for (auto& unit : design.units) {
        if (unit.language != Language::SystemVerilog2017)
            continue;
        const auto owner = unit_identity(unit);
        for (const auto& variable : unit.variables) {
            auto candidates = find_covergroups(
                entries, variable.type.named_type, owner);
            if (candidates.size() == 1U) {
                add_instance(
                    design,
                    owner,
                    variable,
                    *candidates.front(),
                    diagnostics);
            } else if (candidates.size() > 1U) {
                diagnose(
                    diagnostics,
                    "FSIM-SV-SEM-211",
                    "covergroup type '" + variable.type.named_type
                        + "' is ambiguous",
                    variable.span);
            }
        }
        const auto library = unit.library.empty() ? std::string { "work" } : unit.library;
        for (auto& declaration : unit.systemverilog_classes) {
            collect_class_instances(
                design, declaration, library, entries, diagnostics);
        }
        for (const auto& instance : design.systemverilog_covergroup_instances) {
            if (instance.owner_identity != owner)
                continue;
            const auto entry = std::ranges::find_if(
                entries, [&](const CovergroupEntry& candidate) {
                    return candidate.declaration->canonical_identity
                        == instance.declaration_identity;
                });
            if (entry != entries.end()) {
                add_event_sample_process(
                    unit, instance, *entry->declaration, diagnostics);
            }
        }
        for (auto& process : unit.processes) {
            collect_sample_calls(
                design, owner, process.statements, entries, diagnostics);
        }
    }
    for (auto& declaration : design.systemverilog_classes) {
        collect_class_instances(
            design, declaration, "work", entries, diagnostics);
    }
    return diagnostics.size() == initial_diagnostic_count;
}

} // namespace fsim::frontend
