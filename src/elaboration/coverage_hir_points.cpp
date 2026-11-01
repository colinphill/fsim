// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_hir_points.hpp"

#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    bool executable(const semantic::sv::StatementKind kind) noexcept
    {
        using Kind = semantic::sv::StatementKind;
        switch (kind) {
        case Kind::block:
        case Kind::null_statement:
            return false;
        case Kind::assignment:
        case Kind::force:
        case Kind::release:
        case Kind::conditional:
        case Kind::selection:
        case Kind::loop:
        case Kind::break_loop:
        case Kind::continue_loop:
        case Kind::return_statement:
        case Kind::task_call:
        case Kind::assertion:
        case Kind::delay_control:
        case Kind::event_control:
        case Kind::wait_statement:
        case Kind::event_trigger:
        case Kind::fork:
        case Kind::wait_fork:
        case Kind::disable_fork:
        case Kind::disable:
        case Kind::display:
        case Kind::file_close:
        case Kind::file_flush:
        case Kind::file_display:
        case Kind::memory_transfer:
        case Kind::container_method:
        case Kind::monitor_control:
        case Kind::pause:
        case Kind::finish:
        case Kind::exit_program:
        case Kind::procedural_assign:
        case Kind::deassign:
        case Kind::wait_order:
        case Kind::report:
            return true;
        }
        return false;
    }

    bool executable(const semantic::vhdl::StatementKind kind) noexcept
    {
        using Kind = semantic::vhdl::StatementKind;
        switch (kind) {
        case Kind::block:
        case Kind::null_statement:
            return false;
        case Kind::signal_assignment:
        case Kind::variable_assignment:
        case Kind::conditional:
        case Kind::selection:
        case Kind::loop:
        case Kind::exit_loop:
        case Kind::next_loop:
        case Kind::return_statement:
        case Kind::procedure_call:
        case Kind::assertion:
        case Kind::report:
        case Kind::wait_statement:
        case Kind::force:
        case Kind::release:
            return true;
        }
        return false;
    }

    struct CaseArmView {
        semantic::SourceSpanId source;
        bool is_default { };
        std::span<const semantic::StatementId> statements;
    };

    // The language-neutral parts of one HIR statement that discovery needs.
    struct StatementView {
        frontend::CodeCoverageLanguage language {
            frontend::CodeCoverageLanguage::SystemVerilog
        };
        semantic::OriginId origin;
        semantic::SourceSpanId source;
        bool executable { };
        bool conditional { };
        bool selection { };
        bool conditional_assignment { };
        bool selected_assignment { };
        // A conditional assignment's condition, or an assignment's value.
        std::optional<semantic::ExpressionId> condition;
        std::optional<semantic::ExpressionId> value;
        // The condition an if or loop statement decides on.
        std::optional<semantic::ExpressionId> decision;
        // An SV continuous assignment's value.
        std::optional<semantic::ExpressionId> continuous_value;
        std::span<const semantic::StatementId> statements;
        std::span<const semantic::StatementId> else_statements;
        std::span<const semantic::StatementId> loop_updates;
        std::vector<CaseArmView> arms;
    };

    std::optional<StatementView> view_statement(
        const semantic::CompiledDesign& compiled,
        const semantic::StatementId id)
    {
        const auto record = compiled.find_statement(id);
        if (!record) {
            return std::nullopt;
        }
        StatementView view;
        if (record->systemverilog != nullptr) {
            const auto& statement = *record->systemverilog;
            view.origin = statement.origin;
            view.source = statement.source;
            view.executable = executable(statement.kind);
            view.conditional
                = statement.kind == semantic::sv::StatementKind::conditional;
            view.selection
                = statement.kind == semantic::sv::StatementKind::selection;
            if (view.conditional
                || statement.kind == semantic::sv::StatementKind::loop) {
                view.decision = statement.condition;
            }
            if (statement.kind == semantic::sv::StatementKind::assignment
                && statement.assignment_kind
                    == semantic::sv::AssignmentKind::continuous) {
                view.continuous_value = statement.value;
            }
            view.statements = statement.statements;
            view.else_statements = statement.else_statements;
            view.loop_updates = statement.loop_updates;
            for (const auto& alternative : statement.case_alternatives) {
                view.arms.push_back({ alternative.source,
                    alternative.is_default, alternative.statements });
            }
            return view;
        }
        if (record->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& statement = *record->vhdl;
        view.language = frontend::CodeCoverageLanguage::Vhdl;
        view.origin = statement.origin;
        view.source = statement.source;
        view.executable = executable(statement.kind);
        view.conditional
            = statement.kind == semantic::vhdl::StatementKind::conditional;
        view.selection
            = statement.kind == semantic::vhdl::StatementKind::selection;
        view.conditional_assignment
            = view.conditional && statement.conditional_assignment;
        view.selected_assignment
            = view.selection && statement.selected_assignment;
        view.condition = statement.condition;
        if (view.conditional
            || statement.kind == semantic::vhdl::StatementKind::loop) {
            view.decision = statement.condition;
        }
        view.value = statement.waveform.empty()
            ? statement.value
            : std::optional { statement.waveform.front().value };
        view.statements = statement.statements;
        view.else_statements = statement.else_statements;
        for (const auto& alternative : statement.alternatives) {
            view.arms.push_back({ alternative.source,
                alternative.is_default, alternative.statements });
        }
        return view;
    }

} // namespace

namespace {

    using Operator = CoverageConditionOperator;

    std::string lower_ascii(std::string_view text)
    {
        std::string result { text };
        for (auto& character : result) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return result;
    }

    // System functions with no side effect, which atoms may repeat.
    bool pure_system_function(const std::string_view name) noexcept
    {
        static constexpr std::string_view pure[] {
            "$bits", "$clog2", "$countbits", "$countones", "$high",
            "$increment", "$isunknown", "$left", "$low", "$onehot",
            "$onehot0", "$right", "$signed", "$size", "$unsigned",
        };
        return std::ranges::find(pure, name) != std::end(pure);
    }

    // Whether a declaration's subtype is scalar BOOLEAN, BIT or STD_ULOGIC.
    bool scalar_logic_declaration(const semantic::DeclarationId id,
        const CoverageDeclarationLookup& declarations)
    {
        const auto declaration = declarations(id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return false;
        }
        using Domain = semantic::vhdl::ValueDomain;
        const auto& subtype = *declaration->vhdl->subtype;
        return (subtype.domain == Domain::boolean
                   || subtype.domain == Domain::bit2
                   || subtype.domain == Domain::logic4
                   || subtype.domain == Domain::logic9)
            && !subtype.unconstrained
            && subtype.executable_width.value_or(1U) == 1U;
    }

    // Whether a callable's formals are all scalar logic.
    bool scalar_logic_callable(const semantic::DeclarationId id,
        const CoverageDeclarationLookup& declarations)
    {
        const auto found = declarations(id);
        if (!found || found->vhdl == nullptr || !found->vhdl->callable
            || found->vhdl->callable->formals.empty()) {
            return false;
        }
        return std::ranges::all_of(found->vhdl->callable->formals,
            [&](const semantic::DeclarationId formal) {
                return scalar_logic_declaration(formal, declarations);
            });
    }

    bool logical_text(const std::string_view text) noexcept
    {
        return text == "not" || text == "and" || text == "or"
            || text == "nand" || text == "nor" || text == "xor"
            || text == "xnor";
    }

    // Whether a VHDL operand has a scalar logic type by its form: a
    // relation, a literal, a scalar logic object, or a logical operator
    // over such operands.
    bool scalar_logic_operand(const semantic::ExpressionId id,
        const CoverageExpressionLookup& expressions,
        const CoverageDeclarationLookup& declarations, const std::size_t depth)
    {
        const auto view = expressions(id);
        if (!view || view->vhdl == nullptr || depth > 64U) {
            return false;
        }
        const auto& expression = *view->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        const auto text = lower_ascii(expression.text);
        switch (expression.kind) {
        case Kind::boolean_literal:
            return true;
        case Kind::name:
            return expression.referenced_name
                && expression.referenced_name->selected
                && scalar_logic_declaration(
                    *expression.referenced_name->selected, declarations);
        case Kind::binary:
            if (text == "=" || text == "/=" || text == "<" || text == "<="
                || text == ">" || text == ">=") {
                return true;
            }
            [[fallthrough]];
        case Kind::unary:
            return logical_text(text)
                && std::ranges::any_of(expression.operands,
                    [&](const semantic::ExpressionId operand) {
                        return scalar_logic_operand(operand, expressions,
                            declarations, depth + 1U);
                    });
        default:
            return false;
        }
    }

    std::optional<Operator> logical_operator(
        const semantic::CompiledExpressionView& view,
        const CoverageExpressionLookup& expressions,
        const CoverageDeclarationLookup& declarations)
    {
        if (view.systemverilog != nullptr) {
            const auto& expression = *view.systemverilog;
            if (expression.kind == semantic::sv::ExpressionKind::unary
                && expression.text == "!" && expression.operands.size() == 1U) {
                return Operator::logical_not;
            }
            if (expression.kind == semantic::sv::ExpressionKind::binary
                && expression.operands.size() == 2U) {
                if (expression.text == "&&") {
                    return Operator::logical_and;
                }
                if (expression.text == "||") {
                    return Operator::logical_or;
                }
            }
            return std::nullopt;
        }
        const auto& expression = *view.vhdl;
        // A visible user overload of the operator over other types makes
        // it an ordinary call unless an operand is proven scalar logic, for
        // which only the predefined operator applies.
        const auto selected = expression.resolved_declaration
            ? expression.resolved_declaration
            : expression.referenced_name
            ? expression.referenced_name->selected
            : std::nullopt;
        if (expression.builtin_operator
                == semantic::vhdl::BuiltinOperatorIdentity::none
            && selected && !scalar_logic_callable(*selected, declarations)
            && std::ranges::none_of(expression.operands,
                [&](const semantic::ExpressionId operand) {
                    return scalar_logic_operand(
                        operand, expressions, declarations, 0U);
                })) {
            return std::nullopt;
        }
        const auto text = lower_ascii(expression.text);
        if (expression.kind == semantic::vhdl::ExpressionKind::unary
            && text == "not" && expression.operands.size() == 1U) {
            return Operator::logical_not;
        }
        if (expression.kind != semantic::vhdl::ExpressionKind::binary
            || expression.operands.size() != 2U) {
            return std::nullopt;
        }
        if (text == "and") {
            return Operator::logical_and;
        }
        if (text == "or") {
            return Operator::logical_or;
        }
        if (text == "nand") {
            return Operator::logical_nand;
        }
        if (text == "nor") {
            return Operator::logical_nor;
        }
        if (text == "xor") {
            return Operator::logical_xor;
        }
        if (text == "xnor") {
            return Operator::logical_xnor;
        }
        return std::nullopt;
    }

    // Whether evaluating `id` again could have an effect: an update, a
    // non-pure call, or a class method call.
    bool repeatable(const semantic::ExpressionId id,
        const CoverageExpressionLookup& expressions,
        const CoverageDeclarationLookup& declarations, const std::size_t depth)
    {
        if (depth > 256U) {
            return false;
        }
        const auto view = expressions(id);
        if (!view) {
            return false;
        }
        std::span<const semantic::ExpressionId> operands;
        if (view->systemverilog != nullptr) {
            const auto& expression = *view->systemverilog;
            using Kind = semantic::sv::ExpressionKind;
            if (expression.kind == Kind::update
                || expression.kind == Kind::class_allocation
                || expression.kind == Kind::class_method_call
                || expression.kind == Kind::class_static_method_call
                || (expression.kind == Kind::call
                    && !pure_system_function(expression.text))) {
                return false;
            }
            operands = expression.operands;
        } else {
            const auto& expression = *view->vhdl;
            if (expression.kind == semantic::vhdl::ExpressionKind::update) {
                return false;
            }
            const auto declaration = expression.resolved_declaration
                ? expression.resolved_declaration
                : expression.referenced_name
                ? expression.referenced_name->selected
                : std::nullopt;
            if (expression.kind == semantic::vhdl::ExpressionKind::call
                && declaration) {
                const auto found = declarations(*declaration);
                if (found && found->vhdl != nullptr
                    && found->vhdl->callable
                    && found->vhdl->callable->function
                    && !found->vhdl->callable->pure) {
                    return false;
                }
            }
            operands = expression.operands;
        }
        return std::ranges::all_of(operands,
            [&](const semantic::ExpressionId operand) {
                return repeatable(operand, expressions, declarations,
                    depth + 1U);
            });
    }

} // namespace

std::optional<CoverageConditionTree> coverage_condition_tree(
    const semantic::ExpressionId decision,
    const CoverageExpressionLookup& expressions,
    const CoverageDeclarationLookup& declarations)
{
    CoverageConditionTree tree;
    bool logical { };
    bool valid = true;
    const auto build = [&](const auto& self, const semantic::ExpressionId id,
                           const std::size_t depth) -> std::uint32_t {
        const auto view = expressions(id);
        const auto index = static_cast<std::uint32_t>(tree.nodes.size());
        tree.nodes.push_back({ Operator::atom, id });
        const auto op = view && depth < 64U
            ? logical_operator(*view, expressions, declarations)
            : std::nullopt;
        if (!op) {
            if (tree.atoms.size() >= kCoverageConditionMaximumAtoms) {
                valid = false;
                return index;
            }
            tree.nodes[index].atom
                = static_cast<std::uint32_t>(tree.atoms.size());
            tree.atoms.push_back(id);
            return index;
        }
        logical = true;
        const auto& operands = view->systemverilog != nullptr
            ? view->systemverilog->operands
            : view->vhdl->operands;
        tree.nodes[index].op = *op;
        const auto left = self(self, operands.front(), depth + 1U);
        tree.nodes[index].left = left;
        if (*op != Operator::logical_not) {
            const auto right = self(self, operands.back(), depth + 1U);
            tree.nodes[index].right = right;
        }
        return index;
    };
    tree.root = build(build, decision, 0U);
    // Each operand's points are named by its source span.
    std::set<std::uint32_t> atom_sources;
    for (const auto atom : tree.atoms) {
        const auto view = expressions(atom);
        const auto source = !view ? semantic::SourceSpanId { }
            : view->systemverilog != nullptr ? view->systemverilog->source
                                             : view->vhdl->source;
        valid = valid && source.valid()
            && atom_sources.insert(source.value()).second;
    }
    if (!valid || !logical
        || !repeatable(decision, expressions, declarations, 0U)) {
        return std::nullopt;
    }
    return tree;
}

std::optional<CoverageTernary> coverage_ternary(
    const semantic::ExpressionId id,
    const CoverageExpressionLookup& expressions,
    const CoverageDeclarationLookup& declarations)
{
    const auto view = expressions(id);
    if (!view || view->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& expression = *view->systemverilog;
    if (expression.kind != semantic::sv::ExpressionKind::call
        || expression.text != "?:" || expression.operands.size() != 3U
        || !repeatable(expression.operands[0], expressions, declarations, 0U)) {
        return std::nullopt;
    }
    return CoverageTernary { expression.operands[0], expression.operands[1],
        expression.operands[2] };
}

std::vector<CoverageExpressionRow> coverage_expression_rows(
    const CoverageConditionTree& tree)
{
    using Rows = std::vector<CoverageExpressionRow>;
    bool exceeded { };
    const auto rows = [&](const auto& self, const std::uint32_t index) -> Rows {
        const auto& node = tree.nodes[index];
        if (exceeded) {
            return { };
        }
        if (node.op == Operator::atom) {
            return { { { { node.atom, false } }, false },
                { { { node.atom, true } }, true } };
        }
        auto left = self(self, node.left);
        if (node.op == Operator::logical_not) {
            for (auto& row : left) {
                row.result = !row.result;
            }
            return left;
        }
        const auto right = self(self, node.right);
        const bool conjunction = node.op == Operator::logical_and
            || node.op == Operator::logical_nand;
        const bool disjunction = node.op == Operator::logical_or
            || node.op == Operator::logical_nor;
        const bool inverted = node.op == Operator::logical_nand
            || node.op == Operator::logical_nor
            || node.op == Operator::logical_xnor;
        Rows result;
        for (const auto& first : left) {
            // A conjunction is decided by a false left operand, a
            // disjunction by a true one.
            if ((conjunction && !first.result) || (disjunction && first.result)) {
                result.push_back({ first.steps, first.result != inverted });
                continue;
            }
            for (const auto& second : right) {
                auto steps = first.steps;
                steps.insert(steps.end(), second.steps.begin(),
                    second.steps.end());
                const bool value = conjunction || disjunction
                    ? second.result
                    : first.result != second.result;
                result.push_back({ std::move(steps), value != inverted });
            }
            if (result.size() > kCoverageExpressionMaximumRows) {
                exceeded = true;
                return { };
            }
        }
        return result;
    };
    auto result = rows(rows, tree.root);
    return exceeded ? Rows { } : result;
}

CoverageHirPointResult discover_hir_module_coverage_points(
    const semantic::CompiledDesign& compiled,
    const SpecializationInfo& specialization,
    const std::span<const CoverageHirSource> sources,
    const std::size_t maximum_statements,
    const CoverageExternalExclusionPlan* const external_exclusions) noexcept
{
    CoverageHirPointResult result;
    result.draft.specialization = specialization.id;
    const auto reject = [&](const CoverageHirPointError error) {
        result.draft.points.clear();
        result.error = error;
        return result;
    };
    if (!specialization.source_unit) {
        return result;
    }
    const auto unit = compiled.find_unit(*specialization.source_unit);
    if (!unit) {
        return result;
    }
    const bool vhdl = specialization.language == frontend::Language::Vhdl2008
        && unit->vhdl != nullptr;
    if (!vhdl
        && (specialization.language != frontend::Language::SystemVerilog2017
            || unit->systemverilog == nullptr
            || !covered_systemverilog_unit(unit->systemverilog->kind))) {
        return result;
    }

    try {
        const auto& model = compiled.semantics;
        const auto& spans = model.source_spans();
        const auto& files = model.source_files();
        const auto& origins = model.origins();
        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            source_by_name.emplace(sources[index].inventory.source_name, index);
        }
        std::set<std::pair<std::uint64_t, std::uint64_t>> point_ids;
        const auto source_span = [&](const semantic::SourceSpanId id)
            -> const semantic::SourceSpan* {
            return id.valid() && id.value() < spans.size()
                ? &spans[id.value()] : nullptr;
        };
        const auto append = [&](const frontend::CodeCoverageLanguage language,
                                const semantic::SourceSpan& first,
                                const semantic::SourceSpan& last,
                                const runtime::CodeCoverageMetric metric,
                                const frontend::CodeCoverageConstructKind kind,
                                const std::uint64_t ordinal = 0U,
                                const std::string_view reason = { },
                                const bool counted = true,
                                const std::string_view object = { }) {
            if (!first.file.valid() || first.file != last.file
                || first.file.value() >= files.size()
                || first.begin.offset >= last.end.offset) {
                return CoverageHirPointError::InvalidSpan;
            }
            if (result.draft.points.size() + result.draft.exclusions.size()
                >= maximum_statements) {
                return CoverageHirPointError::ResourceLimit;
            }
            const auto source = source_by_name.find(
                files[first.file.value()].physical_name);
            if (source == source_by_name.end()) {
                return CoverageHirPointError::InvalidSource;
            }
            const frontend::CodeCoverageSourceSpan span {
                first.begin.offset, last.end.offset };
            const auto identity = frontend::make_code_coverage_point_identity(
                sources[source->second].inventory.identity,
                language, kind, span, ordinal);
            if (!identity.ok()) {
                return CoverageHirPointError::InvalidSpan;
            }
            if (!point_ids.emplace(identity.identity->high,
                    identity.identity->low).second) {
                return CoverageHirPointError::DuplicatePoint;
            }
            // A point in a source-comment exclusion region is an uncounted
            // row that carries the comment's reason; an uncounted point
            // carries its own. A counted point with a reason is
            // informational.
            const auto source_metric
                = static_cast<frontend::CoverageSourceMetric>(metric);
            const auto* commented = frontend::coverage_source_exclusion_at(
                sources[source->second].exclusions, source_metric,
                first.begin.offset);
            if (commented != nullptr || !counted) {
                std::string why { reason };
                if (commented != nullptr) {
                    why = commented->reason.empty()
                        ? std::string { "excluded by a source comment" }
                        : commented->reason;
                }
                result.draft.exclusions.push_back({ *identity.identity,
                    metric, source->second, span, first.begin.line,
                    std::move(why) });
                return CoverageHirPointError::None;
            }
            // A point an external exclusion selects counts, and is
            // reported as excluded for the rule's reason.
            std::string why { reason };
            if (why.empty() && external_exclusions != nullptr
                && !external_exclusions->rules.empty()) {
                const std::array targets { CoverageExternalExclusionTarget {
                    sources[source->second].inventory.identity.logical_path,
                    specialization.instance, object, source_metric } };
                const auto matched = match_coverage_external_exclusions(
                    *external_exclusions, targets);
                if (matched.ok() && !matched.targets.front().rule_indices.empty()) {
                    why = external_exclusions
                              ->rules[matched.targets.front().rule_indices.front()]
                              .reason;
                }
            }
            result.draft.points.push_back({ *identity.identity, metric,
                source->second, span, first.begin.line, std::move(why) });
            return CoverageHirPointError::None;
        };

        const CoverageExpressionLookup expressions
            = [&](const semantic::ExpressionId id) {
                  return compiled.find_expression(id);
              };
        const CoverageDeclarationLookup declarations
            = [&](const semantic::DeclarationId id) {
                  return compiled.find_declaration(id);
              };
        // A decision with logical operators has a true and a false point
        // per operand and one point per short-circuit evaluation path.
        const auto append_decision = [&](
            const frontend::CodeCoverageLanguage language,
            const semantic::ExpressionId decision) {
            const auto expression_span = [&](const semantic::ExpressionId id)
                -> const semantic::SourceSpan* {
                const auto found = compiled.find_expression(id);
                return !found ? nullptr
                              : source_span(found->systemverilog != nullptr
                                        ? found->systemverilog->source
                                        : found->vhdl->source);
            };
            const auto tree = coverage_condition_tree(
                decision, expressions, declarations);
            const auto rows = tree ? coverage_expression_rows(*tree)
                                   : std::vector<CoverageExpressionRow> { };
            if (rows.empty()) {
                return CoverageHirPointError::None;
            }
            const auto* decision_span = expression_span(decision);
            if (decision_span == nullptr) {
                return CoverageHirPointError::InvalidSpan;
            }
            for (const auto atom : tree->atoms) {
                const auto* atom_span = expression_span(atom);
                if (atom_span == nullptr) {
                    return CoverageHirPointError::InvalidSpan;
                }
                for (const auto kind : {
                         frontend::CodeCoverageConstructKind::ConditionTrue,
                         frontend::CodeCoverageConstructKind::ConditionFalse }) {
                    const auto error = append(language, *atom_span,
                        *atom_span, runtime::CodeCoverageMetric::Condition,
                        kind);
                    if (error != CoverageHirPointError::None) {
                        return error;
                    }
                }
            }
            for (std::size_t row = 0U; row < rows.size(); ++row) {
                const auto error = append(language, *decision_span,
                    *decision_span, runtime::CodeCoverageMetric::Expression,
                    frontend::CodeCoverageConstructKind::ExpressionRow,
                    row + 1U);
                if (error != CoverageHirPointError::None) {
                    return error;
                }
            }
            return CoverageHirPointError::None;
        };
        // The flag marks the parts of a VHDL sequential conditional
        // assignment, which are not separate statements.
        std::vector<std::pair<semantic::StatementId, bool>> pending;
        std::size_t scheduled { };
        const auto enqueue = [&](const std::span<const semantic::StatementId> ids,
                                 const bool chain = false) {
            if (ids.size() > maximum_statements - scheduled) {
                return false;
            }
            scheduled += ids.size();
            for (auto item = ids.rbegin(); item != ids.rend(); ++item) {
                pending.emplace_back(*item, chain);
            }
            return true;
        };
        std::set<std::uint32_t> visited_processes;
        std::vector<std::span<const semantic::StatementId>> bodies;
        for (const auto& [runtime_process, semantic_process] :
            specialization.semantic_processes) {
            (void)runtime_process;
            if (!visited_processes.insert(semantic_process.value()).second) {
                continue;
            }
            const auto process = compiled.find_process(semantic_process);
            if (!process
                || (vhdl ? process->vhdl == nullptr
                         : process->systemverilog == nullptr)) {
                return reject(CoverageHirPointError::InvalidProcess);
            }
            // Concurrent assertion monitor processes are synthesized from
            // source actions and are not module procedural source processes.
            if (!vhdl && process->systemverilog->concurrent_assertion) {
                continue;
            }
            const auto owner_origin = vhdl ? process->vhdl->origin
                                           : process->systemverilog->origin;
            if (!owner_origin.valid() || owner_origin.value() >= origins.size()) {
                return reject(CoverageHirPointError::InvalidProcess);
            }
            if (origins[owner_origin.value()].kind
                != semantic::OriginKind::parsed) {
                continue;
            }
            bodies.push_back(vhdl ? std::span<const semantic::StatementId> {
                                        process->vhdl->statements }
                                  : std::span<const semantic::StatementId> {
                                        process->systemverilog->statements });
        }
        // A covered concurrent or continuous assignment is its own body.
        for (const auto& statement :
            specialization.coverage_concurrent_statements) {
            bodies.emplace_back(&statement, 1U);
        }
        for (const auto body : bodies) {
            if (!enqueue(body)) {
                return reject(CoverageHirPointError::ResourceLimit);
            }
            while (!pending.empty()) {
                const auto [id, chain_part] = pending.back();
                pending.pop_back();
                const auto statement = view_statement(compiled, id);
                if (!statement) {
                    return reject(CoverageHirPointError::InvalidStatement);
                }
                if (!statement->origin.valid()
                    || statement->origin.value() >= origins.size()) {
                    return reject(CoverageHirPointError::InvalidStatement);
                }
                const bool parsed = origins[statement->origin.value()].kind
                    == semantic::OriginKind::parsed;
                const auto* own_span = source_span(statement->source);
                if (parsed && own_span == nullptr) {
                    return reject(CoverageHirPointError::InvalidSpan);
                }
                const auto language = statement->language;
                if (parsed && statement->executable && !chain_part) {
                    const auto error = append(language, *own_span, *own_span,
                        runtime::CodeCoverageMetric::Statement,
                        frontend::CodeCoverageConstructKind::Statement);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
                if (parsed && statement->conditional) {
                    const auto append_arm = [&](const std::span<const semantic::StatementId> body,
                                                const frontend::CodeCoverageConstructKind kind) {
                        if (body.empty()) {
                            return CoverageHirPointError::MissingBranchArm;
                        }
                        const auto first = view_statement(compiled, body.front());
                        const auto last = view_statement(compiled, body.back());
                        if (!first || !last) {
                            return CoverageHirPointError::InvalidStatement;
                        }
                        // The parts of a conditional assignment are
                        // generated; its condition and final value name
                        // its outcomes.
                        if (statement->conditional_assignment) {
                            const auto expression = kind
                                    == frontend::CodeCoverageConstructKind::BranchTrueArm
                                ? statement->condition
                                : first->value;
                            const auto found = expression
                                ? compiled.find_expression(*expression)
                                : std::nullopt;
                            const auto* named = found && found->vhdl != nullptr
                                ? source_span(found->vhdl->source)
                                : nullptr;
                            if (named == nullptr) {
                                return CoverageHirPointError::InvalidSpan;
                            }
                            return append(language, *named, *named,
                                runtime::CodeCoverageMetric::Branch, kind);
                        }
                        const auto* begin = source_span(first->source);
                        const auto* end = source_span(last->source);
                        if (begin == nullptr || end == nullptr) {
                            return CoverageHirPointError::InvalidSpan;
                        }
                        return append(language, *begin, *end,
                            runtime::CodeCoverageMetric::Branch, kind);
                    };
                    auto error = append_arm(statement->statements,
                        frontend::CodeCoverageConstructKind::BranchTrueArm);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                    // A conditional assignment chain continues in its else
                    // part, whose last part has the chain's false outcome.
                    const auto continues = [&] {
                        if (!statement->conditional_assignment
                            || statement->else_statements.empty()) {
                            return false;
                        }
                        const auto next = view_statement(
                            compiled, statement->else_statements.front());
                        return next && next->conditional_assignment;
                    };
                    error = statement->else_statements.empty()
                        ? append(language, *own_span, *own_span,
                            runtime::CodeCoverageMetric::Branch,
                            frontend::CodeCoverageConstructKind::BranchImplicitArm)
                        : continues()
                        ? CoverageHirPointError::None
                        : append_arm(statement->else_statements,
                            frontend::CodeCoverageConstructKind::BranchFalseArm);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
                if (parsed && statement->decision) {
                    const auto error = append_decision(language,
                        *statement->decision);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
                // Each outcome of a conditional operator in a continuous
                // assignment's value, named by its operand.
                if (parsed && statement->continuous_value) {
                    std::vector<std::pair<semantic::ExpressionId, std::size_t>>
                        values { { *statement->continuous_value, 0U } };
                    while (!values.empty()) {
                        const auto [value, depth] = values.back();
                        values.pop_back();
                        const auto ternary = depth < 64U
                            ? coverage_ternary(value, expressions, declarations)
                            : std::nullopt;
                        if (!ternary) {
                            continue;
                        }
                        for (const auto& [operand, kind] : {
                                 std::pair { ternary->when_true,
                                     frontend::CodeCoverageConstructKind::
                                         BranchTrueArm },
                                 std::pair { ternary->when_false,
                                     frontend::CodeCoverageConstructKind::
                                         BranchFalseArm } }) {
                            const auto found = compiled.find_expression(operand);
                            const auto* operand_span
                                = found && found->systemverilog != nullptr
                                ? source_span(found->systemverilog->source)
                                : nullptr;
                            if (operand_span == nullptr) {
                                return reject(CoverageHirPointError::InvalidSpan);
                            }
                            const auto error = append(language, *operand_span,
                                *operand_span,
                                runtime::CodeCoverageMetric::Branch, kind);
                            if (error != CoverageHirPointError::None) {
                                return reject(error);
                            }
                            values.emplace_back(operand, depth + 1U);
                        }
                    }
                }
                if (parsed && statement->selection) {
                    bool has_default = false;
                    for (const auto& arm : statement->arms) {
                        const auto* arm_span = source_span(arm.source);
                        if (arm_span == nullptr) {
                            return reject(CoverageHirPointError::InvalidSpan);
                        }
                        has_default = has_default || arm.is_default;
                        const auto error = append(language, *arm_span,
                            *arm_span, runtime::CodeCoverageMetric::Branch,
                            arm.is_default
                                ? frontend::CodeCoverageConstructKind::BranchDefaultArm
                                : frontend::CodeCoverageConstructKind::BranchCaseArm);
                        if (error != CoverageHirPointError::None) {
                            return reject(error);
                        }
                    }
                    // A VHDL case statement covers every choice, so only an
                    // SV case without a default has a no-match outcome.
                    if (!has_default
                        && language != frontend::CodeCoverageLanguage::Vhdl) {
                        const auto error = append(language, *own_span,
                            *own_span, runtime::CodeCoverageMetric::Branch,
                            frontend::CodeCoverageConstructKind::BranchImplicitArm);
                        if (error != CoverageHirPointError::None) {
                            return reject(error);
                        }
                    }
                }
                for (auto arm = statement->arms.rbegin();
                    arm != statement->arms.rend(); ++arm) {
                    if (!enqueue(arm->statements,
                            chain_part || statement->selected_assignment)) {
                        return reject(CoverageHirPointError::ResourceLimit);
                    }
                }
                const bool chain = chain_part
                    || statement->conditional_assignment;
                if (!enqueue(statement->loop_updates, chain)
                    || !enqueue(statement->else_statements, chain)
                    || !enqueue(statement->statements, chain)) {
                    return reject(CoverageHirPointError::ResourceLimit);
                }
            }
        }
        // Each bit of a monitored object has a rise and a fall point, named
        // by the declaration and the bit's ordinal.
        const auto object_language = vhdl
            ? frontend::CodeCoverageLanguage::Vhdl
            : frontend::CodeCoverageLanguage::SystemVerilog;
        for (const auto& object : specialization.coverage_toggle_objects) {
            const auto* declaration = source_span(object.declaration);
            if (declaration == nullptr) {
                return reject(CoverageHirPointError::InvalidSpan);
            }
            const auto object_base
                = coverage_object_ordinal_base(object.declared_together);
            // An excluded object has one uncounted row.
            if (!object.exclusion_reason.empty()) {
                const auto error = append(object_language, *declaration,
                    *declaration, runtime::CodeCoverageMetric::Toggle,
                    frontend::CodeCoverageConstructKind::ToggleRise,
                    object_base, object.exclusion_reason, false, object.name);
                if (error != CoverageHirPointError::None) {
                    return reject(error);
                }
                continue;
            }
            for (std::uint64_t bit = 0U; bit < object.width; ++bit) {
                for (const auto kind : {
                         frontend::CodeCoverageConstructKind::ToggleRise,
                         frontend::CodeCoverageConstructKind::ToggleFall }) {
                    const auto error = append(object_language, *declaration,
                        *declaration, runtime::CodeCoverageMetric::Toggle,
                        kind, object_base + bit + 1U, { }, true,
                        object.name);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
            }
        }
        // A state machine has a point per state and per legal transition,
        // named by the state object's declaration and an ordinal.
        for (const auto& machine : specialization.coverage_fsm_machines) {
            const auto* declaration = source_span(machine.declaration);
            if (declaration == nullptr) {
                return reject(CoverageHirPointError::InvalidSpan);
            }
            const auto states = machine.values.size();
            const auto base
                = coverage_object_ordinal_base(machine.declared_together);
            for (std::uint64_t state = 0U; state < states; ++state) {
                const auto error = append(object_language, *declaration,
                    *declaration, runtime::CodeCoverageMetric::FsmState,
                    frontend::CodeCoverageConstructKind::FsmState,
                    base + state + 1U, { }, true, machine.name);
                if (error != CoverageHirPointError::None) {
                    return reject(error);
                }
            }
            for (const auto& transition : machine.transitions) {
                const auto error = append(object_language, *declaration,
                    *declaration, runtime::CodeCoverageMetric::FsmTransition,
                    frontend::CodeCoverageConstructKind::FsmTransition,
                    base + std::uint64_t { transition.from } * states
                        + transition.to + 1U,
                    { }, true, machine.name);
                if (error != CoverageHirPointError::None) {
                    return reject(error);
                }
            }
            // Changes between states that no arm assigns: counted, but
            // reported as excluded.
            const auto error = append(object_language, *declaration,
                *declaration, runtime::CodeCoverageMetric::FsmTransition,
                frontend::CodeCoverageConstructKind::FsmOtherTransition,
                base + states * states + 1U,
                kCoverageFsmOtherTransitionReason, true, machine.name);
            if (error != CoverageHirPointError::None) {
                return reject(error);
            }
        }
        return result;
    } catch (const std::bad_alloc&) {
        return reject(CoverageHirPointError::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(CoverageHirPointError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
