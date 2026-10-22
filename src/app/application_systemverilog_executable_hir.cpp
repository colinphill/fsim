// SPDX-License-Identifier: Apache-2.0
#include "application_systemverilog_hir_internal.hpp"
#include "../elaboration/hir_output_conversion.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {
namespace {

    namespace sv = semantic::sv;

    struct TransparentStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(
            const std::string_view value) const noexcept
        {
            return std::hash<std::string_view> { }(value);
        }
    };

    class SystemVerilogExecutableBuilder final {
    public:
        SystemVerilogExecutableBuilder(
            semantic::Model& model,
            sv::Hir& hir,
            systemverilog_hir_detail::SystemVerilogHirBuilder& type_builder)
            : model_(model)
            , hir_(hir)
            , type_builder_(type_builder)
        {
        }

        void add_design(const frontend::ParsedDesign& parsed)
        {
            for (std::size_t index = 0; index < parsed.units.size(); ++index) {
                const auto& input = parsed.units[index];
                if (input.language == frontend::Language::Vhdl2008) {
                    continue;
                }
                const auto unit_id = semantic::UnitId::from_index(
                    static_cast<std::uint32_t>(index));
                const auto found = std::ranges::find_if(
                    hir_.mutable_units(), [&](const sv::Unit& unit) {
                        return unit.id == unit_id;
                    });
                if (found == hir_.mutable_units().end()) {
                    throw std::logic_error { "missing SystemVerilog HIR unit" };
                }
                fill_unit(input, *found);
            }
            fill_classes(parsed.systemverilog_classes, std::nullopt);
            resolve_expression_names();
        }

    private:
        using ExpressionCandidateIndex
            = std::unordered_map<std::uint64_t, std::vector<std::size_t>>;

        static constexpr std::size_t expression_candidate_index_threshold { 64U };

        [[nodiscard]] static std::uint64_t expression_candidate_key(
            const semantic::ScopeId scope,
            const semantic::SourceSpanId source) noexcept
        {
            return (static_cast<std::uint64_t>(scope.value()) << 32U)
                | static_cast<std::uint64_t>(source.value());
        }

        template<typename CandidateRange>
        void index_expression_candidates(
            std::optional<ExpressionCandidateIndex>& index,
            const CandidateRange& candidates) const
        {
            if (index || candidates.size() < expression_candidate_index_threshold) {
                return;
            }

            ExpressionCandidateIndex ordered_index;
            ordered_index.reserve(candidates.size());
            for (std::size_t candidate_index = 0;
                candidate_index < candidates.size(); ++candidate_index) {
                const auto& candidate = candidates[candidate_index];
                ordered_index[expression_candidate_key(
                    candidate.scope, candidate.source)]
                    .push_back(candidate_index);
            }
            index.emplace(std::move(ordered_index));
        }

        [[nodiscard]] semantic::SourceSpanId source(
            const frontend::SourceSpan& span)
        {
            return intern_semantic_span(model_, span);
        }

        [[nodiscard]] semantic::OriginId origin(
            const semantic::SourceSpanId span,
            const semantic::OriginId parent,
            const std::string_view detail)
        {
            return model_.add_origin(
                semantic::OriginKind::parsed, span, parent, std::string { detail });
        }

        [[nodiscard]] std::optional<semantic::ScopeId> parent_scope(
            const semantic::ScopeId scope) const noexcept
        {
            return model_.scopes()[scope.value()].parent;
        }

        [[nodiscard]] semantic::UnitId scope_unit(
            const semantic::ScopeId scope) const noexcept
        {
            return model_.scopes()[scope.value()].unit;
        }

        [[nodiscard]] bool append_imported_name_candidates(
            sv::Name& result,
            const semantic::ScopeId scope,
            const std::uint64_t source_order) const
        {
            const auto owner = std::ranges::find(
                hir_.units(), scope_unit(scope), &sv::Unit::id);
            if (owner == hir_.units().end()) {
                return false;
            }
            const auto append_declarations
                = [&](const std::string_view package_name,
                      const std::string_view declaration_name) {
                for (const auto& package : hir_.units()) {
                    if (package.kind != sv::UnitKind::package
                        || package.library != owner->library
                        || package.name != package_name) {
                        continue;
                    }
                    for (const auto declaration_id : package.declarations) {
                        const auto* declaration
                            = hir_.find_declaration(declaration_id);
                        if (declaration == nullptr
                            || declaration->name != declaration_name
                            || std::ranges::find(
                                   result.overloads, declaration_id)
                                != result.overloads.end()) {
                            continue;
                        }
                        result.overloads.push_back(declaration_id);
                    }
                }
            };

            const auto qualifier = result.spelling.rfind("::");
            if (qualifier != std::string::npos) {
                append_declarations(
                    std::string_view { result.spelling }.substr(
                        0U, qualifier),
                    std::string_view { result.spelling }.substr(
                        qualifier + 2U));
                return true;
            }

            const auto visible_at_use = [&](const sv::Import& import) {
                return source_order == 0U || import.source_order == 0U
                    || import.source_order < source_order;
            };
            const auto imports_at_this_scope = [&](const auto& imports) {
                bool explicit_import { };
                for (const auto& import : imports) {
                    if (!visible_at_use(import)) {
                        continue;
                    }
                    if (import.wildcard || !import.member
                        || import.member->spelling != result.spelling) {
                        continue;
                    }
                    explicit_import = true;
                    append_declarations(
                        import.package.spelling, result.spelling);
                }
                if (explicit_import) {
                    return true;
                }
                const auto before = result.overloads.size();
                for (const auto& import : imports) {
                    if (visible_at_use(import) && import.wildcard) {
                        append_declarations(
                            import.package.spelling, result.spelling);
                    }
                }
                return result.overloads.size() != before;
            };

            const auto block = block_imports_.find(scope);
            if (block != block_imports_.end()
                && imports_at_this_scope(block->second)) {
                return true;
            }
            if (owner->scope == scope
                && imports_at_this_scope(owner->imports)) {
                return true;
            }
            return false;
        }

        [[nodiscard]] bool genvar_scope(
            const semantic::ScopeId scope, const std::string_view spelling)
        {
            if (!genvar_scopes_) {
                genvar_scopes_.emplace();
                const auto collect = [&](const auto& self,
                    const std::vector<sv::GenerateRegion>& regions) -> void {
                    for (const auto& region : regions) {
                        if (region.kind == sv::GenerateKind::iterative
                            && !region.iterator.empty()) {
                            genvar_scopes_->insert_or_assign(
                                region.scope, region.iterator);
                            // A process of the block has its own scope
                            // outside the block's scope chain.
                            for (const auto process : region.processes) {
                                const auto found = std::ranges::find(
                                    hir_.processes(), process,
                                    &sv::Process::id);
                                if (found != hir_.processes().end()) {
                                    genvar_scopes_->insert_or_assign(
                                        found->scope, region.iterator);
                                }
                            }
                        }
                        self(self, region.nested);
                    }
                };
                for (const auto& unit : hir_.units()) {
                    collect(collect, unit.generates);
                }
            }
            const auto found = genvar_scopes_->find(scope);
            return found != genvar_scopes_->end() && found->second == spelling;
        }

        // `blk.v` names `v` of the enclosing named block `blk`, found by
        // searching upward from the use (IEEE 1800-2017 23.8, 23.9). Only a
        // block that encloses the use is bound here, so an automatic has one
        // activation; a name whose head is a declaration is a member select.
        [[nodiscard]] std::optional<std::vector<semantic::DeclarationId>>
        enclosing_block_member(
            const std::string_view spelling,
            const semantic::ScopeId scope)
        {
            if (spelling.find('.') == std::string_view::npos
                || spelling.find("::") != std::string_view::npos
                || spelling.find_first_of("[(") != std::string_view::npos) {
                return std::nullopt;
            }
            std::vector<std::string_view> parts;
            for (std::size_t begin { };;) {
                const auto dot = spelling.find('.', begin);
                parts.push_back(spelling.substr(begin, dot - begin));
                if (dot == std::string_view::npos) {
                    break;
                }
                begin = dot + 1U;
            }
            if (std::ranges::any_of(parts, &std::string_view::empty)) {
                return std::nullopt;
            }
            std::vector<semantic::ScopeId> ancestors;
            for (auto visible = std::optional { scope }; visible;
                 visible = parent_scope(*visible)) {
                ancestors.push_back(*visible);
            }
            const auto scope_name = [&](const semantic::ScopeId id) {
                return std::string_view { model_.scopes()[id.value()].name };
            };
            std::size_t head = ancestors.size();
            for (std::size_t index { }; index < ancestors.size(); ++index) {
                if (!declarations_named(ancestors[index], parts.front())
                         .empty()) {
                    return std::nullopt;
                }
                // A design unit's own name is left to hierarchical
                // references.
                if (scope_name(ancestors[index]) == parts.front()) {
                    if (!parent_scope(ancestors[index])) {
                        return std::nullopt;
                    }
                    head = index;
                    break;
                }
            }
            if (head == ancestors.size()) {
                return std::nullopt;
            }
            auto target = head;
            for (std::size_t part = 1U; part + 1U < parts.size(); ++part) {
                do {
                    if (target == 0U) {
                        return std::nullopt;
                    }
                    --target;
                } while (scope_name(ancestors[target]).empty()
                    || scope_name(ancestors[target]).starts_with('<'));
                if (scope_name(ancestors[target]) != parts[part]) {
                    return std::nullopt;
                }
            }
            auto named = declarations_named(ancestors[target], parts.back());
            if (named.empty()) {
                return std::nullopt;
            }
            return std::vector<semantic::DeclarationId>(
                named.begin(), named.end());
        }

        [[nodiscard]] sv::Name name(
            const std::string_view spelling,
            const frontend::SourceSpan& span,
            const semantic::ScopeId scope,
            const std::uint64_t source_order = 0U)
        {
            sv::Name result { std::string { spelling }, source(span), std::nullopt, { } };
            result.source_order = source_order;
            if (const auto bound = enclosing_block_member(spelling, scope)) {
                // The bound name is the block's own declaration.
                result.spelling = std::string {
                    spelling.substr(spelling.rfind('.') + 1U) };
                result.overloads = *bound;
                if (result.overloads.size() == 1) {
                    result.selected = result.overloads.front();
                }
                return result;
            }
            auto visible_scope = std::optional<semantic::ScopeId> { scope };
            while (visible_scope) {
                const auto named = declarations_named(*visible_scope, spelling);
                result.overloads.insert(
                    result.overloads.end(), named.begin(), named.end());
                if (!result.overloads.empty()) {
                    break;
                }
                // A loop-generate block's genvar hides outer declarations of
                // its name (IEEE 1800-2017 27.4).
                if (genvar_scope(*visible_scope, spelling)) {
                    return result;
                }
                if (append_imported_name_candidates(
                        result, *visible_scope, source_order)) {
                    break;
                }
                visible_scope = parent_scope(*visible_scope);
            }
            if (result.overloads.size() == 1) {
                result.selected = result.overloads.front();
            }
            return result;
        }

        [[nodiscard]] sv::ExpressionKind expression_kind(
            const frontend::Expression& expression) const noexcept
        {
            if (expression.text == "@sv-null") {
                return sv::ExpressionKind::class_null;
            }
            if (expression.text.starts_with("@sv-new:")) {
                return sv::ExpressionKind::class_allocation;
            }
            if (expression.text.starts_with("@sv-dollar-cast:")) {
                return sv::ExpressionKind::class_cast;
            }
            if (expression.text.starts_with("@sv-property:")) {
                return sv::ExpressionKind::class_property;
            }
            if (expression.text.starts_with("@sv-static-property:")) {
                return sv::ExpressionKind::class_static_property;
            }
            if (expression.text.starts_with("@sv-method:")
                || expression.text.starts_with("@sv-base-method:")) {
                return sv::ExpressionKind::class_method_call;
            }
            if (expression.text.starts_with("@sv-static-method:")) {
                return sv::ExpressionKind::class_static_method_call;
            }
            switch (expression.kind) {
            case frontend::ExpressionKind::Invalid:
                return sv::ExpressionKind::invalid;
            case frontend::ExpressionKind::Identifier:
                return sv::ExpressionKind::name;
            case frontend::ExpressionKind::IntegerLiteral:
                return sv::ExpressionKind::integer_literal;
            case frontend::ExpressionKind::BooleanLiteral:
                return sv::ExpressionKind::boolean_literal;
            case frontend::ExpressionKind::LogicLiteral:
                return sv::ExpressionKind::logic_literal;
            case frontend::ExpressionKind::StringLiteral:
                return sv::ExpressionKind::string_literal;
            case frontend::ExpressionKind::Unary:
                return sv::ExpressionKind::unary;
            case frontend::ExpressionKind::Update:
                return sv::ExpressionKind::update;
            case frontend::ExpressionKind::Binary:
                return sv::ExpressionKind::binary;
            case frontend::ExpressionKind::Call:
                return sv::ExpressionKind::call;
            case frontend::ExpressionKind::Index:
                return sv::ExpressionKind::index;
            case frontend::ExpressionKind::Slice:
                return sv::ExpressionKind::slice;
            case frontend::ExpressionKind::Aggregate:
                return sv::ExpressionKind::assignment_pattern;
            case frontend::ExpressionKind::Concatenation:
                return sv::ExpressionKind::concatenation;
            case frontend::ExpressionKind::Replication:
                return sv::ExpressionKind::replication;
            case frontend::ExpressionKind::DefaultChoice:
                return sv::ExpressionKind::default_choice;
            case frontend::ExpressionKind::Conditional:
                return sv::ExpressionKind::invalid;
            }
            return sv::ExpressionKind::invalid;
        }

        [[nodiscard]] std::pair<semantic::ExpressionId, semantic::OriginId>
        expression_identity(
            const semantic::ScopeId scope,
            const semantic::SourceSpanId expression_source,
            const semantic::OriginId parent)
        {
            const auto& identities = model_.expression_identities();
            index_expression_candidates(
                expression_identity_candidate_index_, identities);
            const auto candidate_key
                = expression_candidate_key(scope, expression_source);
            const auto matching_identity = [&](const auto& identity)
                -> std::optional<std::pair<semantic::ExpressionId,
                    semantic::OriginId>> {
                if (identity.scope != scope || identity.source != expression_source
                    || claimed_expressions_.contains(identity.id.value())) {
                    return std::nullopt;
                }
                const auto& candidate_origin = model_.origins()[identity.origin.value()];
                if (candidate_origin.parent == parent
                    && candidate_origin.detail == "SystemVerilog expression") {
                    claimed_expressions_.insert(identity.id.value());
                    return std::pair { identity.id, identity.origin };
                }
                return std::nullopt;
            };
            if (expression_identity_candidate_index_) {
                const auto found = expression_identity_candidate_index_->find(
                    candidate_key);
                if (found != expression_identity_candidate_index_->end()) {
                    for (const auto candidate_index : found->second) {
                        if (const auto match = matching_identity(
                                identities[candidate_index])) {
                            return *match;
                        }
                    }
                }
            } else {
                for (const auto& identity : identities) {
                    if (const auto match = matching_identity(identity)) {
                        return *match;
                    }
                }
            }
            const auto expression_origin = origin(
                expression_source, parent, "SystemVerilog expression");
            const auto id = model_.add_expression_identity(
                scope, expression_source, expression_origin);
            // This new ID is claimed immediately and never unclaimed, so it
            // cannot become a reusable candidate in the one-time index.
            claimed_expressions_.insert(id.value());
            return { id, expression_origin };
        }

        [[nodiscard]] const sv::ClassDeclaration* visible_class(
            const std::string_view identity,
            const semantic::ScopeId scope) const
        {
            const auto visible = [&](const sv::ClassDeclaration& item) {
                if (item.canonical_identity == identity) {
                    return true;
                }
                if (item.name != identity || !item.scope.valid()) {
                    return false;
                }
                auto visible_scope = std::optional<semantic::ScopeId> { scope };
                const auto owner = parent_scope(item.scope);
                while (visible_scope) {
                    if (owner == visible_scope) {
                        return true;
                    }
                    visible_scope = parent_scope(*visible_scope);
                }
                return false;
            };
            const auto declaration = std::ranges::find_if(
                hir_.classes(), visible);
            return declaration != hir_.classes().end()
                ? &*declaration
                : nullptr;
        }

        void bind_class_allocation(sv::Expression& output) const
        {
            if (output.kind != sv::ExpressionKind::class_allocation
                || output.class_identity.empty()) {
                return;
            }
            const auto declaration = visible_class(
                output.class_identity, output.scope);
            if (declaration != nullptr) {
                output.class_identity = declaration->canonical_identity;
            }
        }

        void bind_implicit_class_method(sv::Expression& output)
        {
            if (output.kind != sv::ExpressionKind::call
                || output.text.size() < 2U || output.text.front() != '.'
                || output.operands.empty()) {
                return;
            }
            const auto receiver = std::ranges::find_if(
                hir_.expressions(), [&](const sv::Expression& candidate) {
                    return candidate.id == output.operands.front();
                });
            if (receiver == hir_.expressions().end()
                || !receiver->referenced_name
                || !receiver->referenced_name->selected) {
                return;
            }
            auto* object
                = declaration_by_id(*receiver->referenced_name->selected);
            if (object == nullptr || !object->type) {
                return;
            }
            const auto class_identity = !object->type->class_identity.empty()
                ? std::string_view { object->type->class_identity }
                : std::string_view { object->type->target.spelling };
            if (class_identity.empty()) {
                return;
            }
            const auto declaration = visible_class(
                class_identity, output.scope);
            if (declaration == nullptr) {
                return;
            }
            const auto member_name
                = std::string_view { output.text }.substr(1U);
            const auto method = std::ranges::find_if(
                declaration->methods,
                [&](const sv::ClassMethod& candidate) {
                    return candidate.name == member_name
                        && !candidate.static_method;
                });
            if (method == declaration->methods.end()
                || std::ranges::any_of(
                    std::next(method), declaration->methods.end(),
                    [&](const sv::ClassMethod& candidate) {
                        return candidate.name == member_name
                            && !candidate.static_method;
                    })) {
                return;
            }
            object->type->value_form = sv::TypeForm::class_handle;
            object->type->class_identity
                = declaration->canonical_identity;
            object->type->executable_width = 64U;
            object->type->four_state = false;
            output.kind = sv::ExpressionKind::class_method_call;
            output.class_identity = declaration->canonical_identity;
            output.class_member_identity = method->canonical_identity;
            output.class_checked = true;
            output.referenced_name.reset();
        }

        // `object.member` where `object` names one class-typed object: the
        // lowered receiver, the class it names and the member's name
        // (IEEE 1800-2017 8.4). `receiver_span_from_dot` narrows the
        // receiver's span to the text before the dot.
        struct ImplicitClassMember {
            semantic::ExpressionId receiver;
            const sv::ClassDeclaration* declaration { };
            std::string_view member;
        };
        [[nodiscard]] std::optional<ImplicitClassMember> implicit_class_member(
            const std::string_view dotted, const frontend::SourceSpan& span,
            const bool receiver_span_from_dot, const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto dot = dotted.rfind('.');
            if (dot == std::string_view::npos || dot == 0U
                || dot + 1U >= dotted.size()
                || dotted.substr(0U, dot).find('.')
                    != std::string_view::npos) {
                return std::nullopt;
            }
            frontend::Expression receiver_input {
                frontend::ExpressionKind::Identifier,
                std::string { dotted.substr(0U, dot) }, { }, span
            };
            if (receiver_span_from_dot
                && receiver_input.span.end.offset
                    >= receiver_input.span.begin.offset
                && dot <= receiver_input.span.end.offset
                        - receiver_input.span.begin.offset) {
                receiver_input.span.end.offset
                    = receiver_input.span.begin.offset + dot;
                if (receiver_input.span.end.line
                    == receiver_input.span.begin.line) {
                    receiver_input.span.end.column
                        = receiver_input.span.begin.column + dot;
                }
            }
            const auto receiver_id = expression(
                receiver_input, scope, parent);
            if (!receiver_id) {
                return std::nullopt;
            }
            const auto receiver = std::ranges::find(
                hir_.expressions(), *receiver_id, &sv::Expression::id);
            if (receiver == hir_.expressions().end()
                || !receiver->referenced_name
                || !receiver->referenced_name->selected) {
                return std::nullopt;
            }
            const auto* object
                = hir_.find_declaration(*receiver->referenced_name->selected);
            if (object == nullptr || !object->type) {
                return std::nullopt;
            }
            const auto identity = !object->type->class_identity.empty()
                ? std::string_view { object->type->class_identity }
                : std::string_view { object->type->target.spelling };
            const auto* declaration = visible_class(identity, scope);
            if (declaration == nullptr) {
                declaration = type_parameter_default_class(identity, scope);
            }
            if (declaration == nullptr) {
                return std::nullopt;
            }
            return ImplicitClassMember {
                *receiver_id, declaration, dotted.substr(dot + 1U) };
        }

        // The class a type parameter of an enclosing class defaults to:
        // `T rec` in `class C #(type T = D)` names D's members; elaboration
        // selects the member of each specialization's actual by name (IEEE
        // 1800-2017 8.25).
        [[nodiscard]] const sv::ClassDeclaration* type_parameter_default_class(
            const std::string_view name, const semantic::ScopeId scope) const
        {
            for (auto visible = std::optional<semantic::ScopeId> { scope };
                 visible; visible = parent_scope(*visible)) {
                for (const auto& item : hir_.classes()) {
                    if (item.scope != *visible) {
                        continue;
                    }
                    const auto parameter = std::ranges::find_if(
                        item.parameters, [&](const sv::ClassParameter& p) {
                            return p.type_parameter && p.name == name;
                        });
                    if (parameter == item.parameters.end()) {
                        continue;
                    }
                    if (!parameter->default_type) {
                        return nullptr;
                    }
                    const auto& type = *parameter->default_type;
                    return visible_class(!type.class_identity.empty()
                            ? std::string_view { type.class_identity }
                            : std::string_view { type.target.spelling },
                        item.scope);
                }
            }
            return nullptr;
        }

        // The one element of `range` satisfying `predicate`, or null.
        template <typename Range, typename Predicate>
        [[nodiscard]] static auto* unique_match(
            Range& range, const Predicate& predicate)
        {
            const auto found = std::ranges::find_if(range, predicate);
            return found == range.end()
                    || std::ranges::any_of(
                        std::next(found), range.end(), predicate)
                ? nullptr
                : &*found;
        }

        void bind_implicit_class_property(
            sv::Expression& output,
            const frontend::Expression& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (output.kind != sv::ExpressionKind::name
                || (output.referenced_name
                    && output.referenced_name->selected)) {
                return;
            }
            const auto access = implicit_class_member(
                input.text, input.span, true, scope, parent);
            if (!access) {
                return;
            }
            const auto* property = unique_match(
                access->declaration->properties,
                [&](const sv::ClassProperty& candidate) {
                    return candidate.name == access->member
                        && !candidate.static_storage
                        && candidate.visibility
                            == sv::ClassVisibility::public_access;
                });
            if (property == nullptr) {
                return;
            }
            output.kind = sv::ExpressionKind::class_property;
            output.text = "@sv-property:" + property->canonical_identity;
            output.class_identity = access->declaration->canonical_identity;
            output.class_member_identity = property->canonical_identity;
            output.class_checked = true;
            output.referenced_name.reset();
            output.operands = { access->receiver };
        }

        [[nodiscard]] std::optional<semantic::ExpressionId>
        bind_implicit_class_task_call(
            sv::Statement& output,
            const frontend::Statement& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (input.kind != frontend::StatementKind::TaskCall
                || input.task_name.empty()
                || input.task_name.front() == '$'
                || input.task_name.front() == '@') {
                return std::nullopt;
            }
            const auto access = implicit_class_member(
                input.task_name, input.span, false, scope, parent);
            if (!access) {
                return std::nullopt;
            }
            const auto* method = unique_match(
                access->declaration->methods,
                [&](const sv::ClassMethod& candidate) {
                    return candidate.name == access->member
                        && candidate.kind == sv::ClassMethodKind::task
                        && !candidate.static_method
                        && candidate.visibility
                            == sv::ClassVisibility::public_access;
                });
            if (method == nullptr) {
                return std::nullopt;
            }
            output.task = sv::Name {
                "@sv-task:" + method->canonical_identity,
                source(input.span), method->declaration,
                { method->declaration }
            };
            return access->receiver;
        }

        [[nodiscard]] std::optional<semantic::ExpressionId> expression(
            const frontend::Expression& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (input.kind == frontend::ExpressionKind::Invalid) {
                return std::nullopt;
            }
            const auto expression_source = source(input.span);
            const auto kind = expression_kind(input);
            const auto signed_value = input.kind
                    == frontend::ExpressionKind::IntegerLiteral
                || (input.kind == frontend::ExpressionKind::LogicLiteral
                    && (input.text.find("'s") != std::string::npos
                        || input.text.find("'S") != std::string::npos))
                || (input.call_result_width != 0U
                    && input.call_result_signed);
            const auto clocking_edge
                = input.kind == frontend::ExpressionKind::Call
                    && input.text == "@sv-clocking-event"
                ? edge_kind(static_cast<frontend::EdgeKind>(
                      input.call_result_width))
                : sv::EdgeKind::any;
            const auto& expressions = hir_.expressions();
            index_expression_candidates(
                hir_expression_candidate_index_, expressions);
            const auto candidate_key
                = expression_candidate_key(scope, expression_source);
            const auto matches_expression = [&](const sv::Expression& candidate) {
                if (claimed_expressions_.contains(candidate.id.value())
                    || candidate.scope != scope
                    || candidate.source != expression_source
                    || candidate.kind != kind
                    || candidate.text != input.text
                    || candidate.nominal_type != input.nominal_type
                    || candidate.decoded_string != input.decoded_string
                    || candidate.signed_value != signed_value
                    || candidate.clocking_edge != clocking_edge
                    || candidate.scalar_kind
                        != static_cast<sv::ScalarKind>(
                            input.systemverilog_scalar_kind)
                    || candidate.generated_text
                        != static_cast<sv::GeneratedTextKind>(
                            input.generated_text)) {
                    return false;
                }
                const auto& candidate_origin
                    = model_.origins()[candidate.origin.value()];
                return candidate_origin.parent == parent
                    && candidate_origin.detail == "SystemVerilog expression";
            };
            const sv::Expression* existing { nullptr };
            if (hir_expression_candidate_index_) {
                const auto found = hir_expression_candidate_index_->find(
                    candidate_key);
                if (found != hir_expression_candidate_index_->end()) {
                    for (const auto candidate_index : found->second) {
                        const auto& candidate = expressions[candidate_index];
                        if (matches_expression(candidate)) {
                            existing = &candidate;
                            break;
                        }
                    }
                }
            } else {
                const auto found = std::ranges::find_if(
                    expressions, matches_expression);
                if (found != expressions.end()) {
                    existing = &*found;
                }
            }
            if (existing != nullptr) {
                claimed_expressions_.insert(existing->id.value());
                return existing->id;
            }
            const auto [id, expression_origin] = expression_identity(
                scope, expression_source, parent);
            sv::Expression output;
            output.id = id;
            output.scope = scope;
            output.kind = kind;
            output.text = input.text;
            output.source = expression_source;
            output.origin = expression_origin;
            output.nominal_type = input.nominal_type;
            output.class_identity = input.nominal_type;
            output.signed_value = signed_value;
            output.clocking_edge = clocking_edge;
            systemverilog_hir_detail::bind_systemverilog_class_operation(
                output, input.text);
            bind_class_allocation(output);
            output.class_checked = output.kind == sv::ExpressionKind::class_cast
                || output.kind == sv::ExpressionKind::class_property
                || output.kind == sv::ExpressionKind::class_method_call;
            output.decoded_string = input.decoded_string;
            // `type(data_type)` (IEEE 1800-2017 6.23).
            if (input.systemverilog_type_operand) {
                output.type_operand.push_back(type_reference(
                    *input.systemverilog_type_operand, input.span, scope,
                    parent));
            }
            if (input.systemverilog_class_owner) {
                output.class_owner.push_back(type_reference(
                    *input.systemverilog_class_owner, input.span, scope,
                    parent));
            }
            output.decimal_literal
                = systemverilog_hir_detail::systemverilog_decimal_literal(
                    input.systemverilog_decimal_literal);
            output.scalar_kind = static_cast<sv::ScalarKind>(
                input.systemverilog_scalar_kind);
            output.generated_text
                = static_cast<sv::GeneratedTextKind>(input.generated_text);
            if (input.kind == frontend::ExpressionKind::Identifier
                || (input.kind == frontend::ExpressionKind::Call
                    && !input.text.starts_with("@sv-"))) {
                output.referenced_name = name(
                    input.text, input.span, scope,
                    input.systemverilog_source_order);
                // `blk.v` bound to the block's own `v` is that variable,
                // not a member select of it (IEEE 1800-2017 23.9).
                if (input.kind == frontend::ExpressionKind::Identifier
                    && output.referenced_name->selected
                    && enclosing_block_member(input.text, scope)) {
                    output.text = input.text.substr(
                        input.text.rfind('.') + 1U);
                }
            } else if (input.kind == frontend::ExpressionKind::Call
                && input.text.starts_with("@sv-cast:")
                && input.text != "@sv-cast:@type") {
                output.referenced_name = name(
                    input.text.substr(
                        std::string_view { "@sv-cast:" }.size()),
                    input.span,
                    scope,
                    input.systemverilog_source_order);
            }
            // A function named without parentheses is a call with no
            // arguments (IEEE 1800-2017 13.4.1), except inside its own body,
            // where the name denotes the result variable.
            if (input.kind == frontend::ExpressionKind::Identifier
                && output.kind == sv::ExpressionKind::name
                && output.referenced_name
                && output.referenced_name->selected) {
                const auto* callable
                    = hir_.find_declaration(*output.referenced_name->selected);
                if (callable != nullptr
                    && callable->callable && callable->callable->function
                    && callable->callable->formals.empty()) {
                    bool inside = false;
                    for (auto visible = std::optional<semantic::ScopeId> {
                             scope };
                        visible; visible = parent_scope(*visible)) {
                        if (callable->nested_scope
                            && *visible == *callable->nested_scope) {
                            inside = true;
                            break;
                        }
                    }
                    if (!inside) {
                        output.kind = sv::ExpressionKind::call;
                    }
                }
            }
            output.argument_names = input.call_argument_names;
            const auto omitted = systemverilog_hir_detail::omit_systemverilog_call_arguments(
                input, output.referenced_name, hir_, output.argument_names);
            for (std::size_t index = 0; index < input.operands.size(); ++index) {
                if (omitted[index]) {
                    continue;
                }
                const auto& operand = input.operands[index];
                const auto operand_id = expression(operand, scope, expression_origin);
                if (operand_id) {
                    output.operands.push_back(*operand_id);
                }
                if (input.kind == frontend::ExpressionKind::Call) {
                    sv::CallAssociation association;
                    // The kept operand's name, which an omitted argument before
                    // it may have supplied.
                    const auto kept = static_cast<std::size_t>(
                        std::count(omitted.begin(), omitted.begin()
                            + static_cast<std::ptrdiff_t>(index), false));
                    if (kept < output.argument_names.size()
                        && !output.argument_names[kept].empty()) {
                        association.formal = output.argument_names[kept];
                    }
                    association.actual = operand_id;
                    association.source = operand.valid()
                        ? source(operand.span)
                        : expression_source;
                    output.call_arguments.push_back(std::move(association));
                }
            }
            bind_implicit_class_method(output);
            bind_implicit_class_property(
                output, input, scope, expression_origin);
            if (input.kind == frontend::ExpressionKind::Aggregate) {
                for (std::size_t index = 0; index < output.operands.size(); ++index) {
                    sv::AssignmentPatternAssociation association;
                    association.value = output.operands[index];
                    association.source = index < input.operands.size()
                        ? source(input.operands[index].span)
                        : expression_source;
                    if (index < input.aggregate_choices.size()) {
                        association.choice_spelling = input.aggregate_choices[index];
                    }
                    if (index < input.aggregate_choice_expressions.size()) {
                        for (const auto& choice :
                            input.aggregate_choice_expressions[index]) {
                            if (const auto choice_id = expression(
                                    choice, scope, expression_origin)) {
                                association.choices.push_back(*choice_id);
                            }
                        }
                    }
                    output.associations.push_back(std::move(association));
                }
            }
            const auto inline_marker = std::ranges::find(
                input.aggregate_choices, "@sv-inline-constraint");
            if (inline_marker != input.aggregate_choices.end()) {
                const auto index = static_cast<std::size_t>(std::distance(
                    input.aggregate_choices.begin(), inline_marker));
                if (index < input.aggregate_choice_expressions.size()) {
                    sv::AssignmentPatternAssociation association;
                    association.choice_spelling
                        = "@sv-inline-constraint";
                    association.source = expression_source;
                    for (const auto& constraint :
                        input.aggregate_choice_expressions[index]) {
                        if (const auto constraint_id = expression(
                                constraint, scope, expression_origin)) {
                            association.choices.push_back(*constraint_id);
                        }
                    }
                    if (!association.choices.empty()) {
                        association.value = association.choices.front();
                        output.associations.push_back(
                            std::move(association));
                    }
                }
            }
            // `randomize() with (names)`: the names bound to the object.
            const auto names_marker = std::ranges::find(
                input.aggregate_choices, "@sv-inline-constraint-names");
            if (names_marker != input.aggregate_choices.end()) {
                const auto index = static_cast<std::size_t>(std::distance(
                    input.aggregate_choices.begin(), names_marker));
                if (index < input.aggregate_choice_expressions.size()) {
                    sv::AssignmentPatternAssociation association;
                    association.choice_spelling
                        = "@sv-inline-constraint-names";
                    association.source = expression_source;
                    for (const auto& name :
                        input.aggregate_choice_expressions[index]) {
                        if (const auto name_id = expression(
                                name, scope, expression_origin)) {
                            association.choices.push_back(*name_id);
                        }
                    }
                    if (!association.choices.empty()) {
                        association.value = association.choices.front();
                        output.associations.push_back(
                            std::move(association));
                    }
                }
            }
            // The expression identity was claimed above. Keeping this append
            // out of the one-time candidate index therefore preserves the
            // original claimed-ID predicate for every later lookup.
            hir_.mutable_expressions().push_back(std::move(output));
            return id;
        }

        [[nodiscard]] sv::DelayValue delay_value(
            const std::uint64_t magnitude,
            const std::uint64_t divisor,
            const std::string& unit,
            const std::optional<frontend::Expression>& input_expression,
            const frontend::SourceSpan& span,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return {
                magnitude,
                divisor,
                unit,
                input_expression
                    ? expression(*input_expression, scope, parent)
                    : std::nullopt,
                source(span)
            };
        }

        [[nodiscard]] sv::Delay delay(
            const frontend::Delay& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            sv::Delay output;
            output.primary = delay_value(
                input.magnitude, input.divisor, input.unit, input.expression,
                input.span, scope, parent);
            const auto alternative = [&](
                                         const std::optional<frontend::DelayAlternative>& value)
                -> std::optional<sv::DelayValue> {
                if (!value) {
                    return std::nullopt;
                }
                return delay_value(
                    value->magnitude, value->divisor, value->unit,
                    value->expression, value->span, scope, parent);
            };
            output.minimum = alternative(input.minimum);
            output.typical = alternative(input.typical);
            output.maximum = alternative(input.maximum);
            for (const auto& additional : input.additional_values) {
                output.additional.push_back(delay(
                    additional, scope, parent));
            }
            return output;
        }

        [[nodiscard]] sv::EdgeKind edge_kind(
            const frontend::EdgeKind kind) const noexcept
        {
            switch (kind) {
            case frontend::EdgeKind::Any:
                return sv::EdgeKind::any;
            case frontend::EdgeKind::Positive:
                return sv::EdgeKind::positive;
            case frontend::EdgeKind::Negative:
                return sv::EdgeKind::negative;
            }
            return sv::EdgeKind::any;
        }

        [[nodiscard]] sv::Sensitivity sensitivity(
            const frontend::Sensitivity& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return {
                edge_kind(input.edge),
                input.signal,
                expression(input.expression, scope, parent),
                source(input.span),
                input.systemverilog_source_order
            };
        }

        [[nodiscard]] sv::StatementKind statement_kind(
            const frontend::StatementKind kind) const noexcept
        {
            switch (kind) {
            case frontend::StatementKind::Assignment:
                return sv::StatementKind::assignment;
            case frontend::StatementKind::Force:
                return sv::StatementKind::force;
            case frontend::StatementKind::Release:
                return sv::StatementKind::release;
            case frontend::StatementKind::If:
                return sv::StatementKind::conditional;
            case frontend::StatementKind::Case:
                return sv::StatementKind::selection;
            case frontend::StatementKind::Loop:
                return sv::StatementKind::loop;
            case frontend::StatementKind::Break:
                return sv::StatementKind::break_loop;
            case frontend::StatementKind::Continue:
                return sv::StatementKind::continue_loop;
            case frontend::StatementKind::Return:
                return sv::StatementKind::return_statement;
            case frontend::StatementKind::TaskCall:
            case frontend::StatementKind::ProcedureCall:
                return sv::StatementKind::task_call;
            case frontend::StatementKind::Assert:
                return sv::StatementKind::assertion;
            case frontend::StatementKind::Delay:
                return sv::StatementKind::delay_control;
            case frontend::StatementKind::WaitOn:
                return sv::StatementKind::event_control;
            case frontend::StatementKind::WaitUntil:
                return sv::StatementKind::wait_statement;
            case frontend::StatementKind::EventTrigger:
                return sv::StatementKind::event_trigger;
            case frontend::StatementKind::Fork:
                return sv::StatementKind::fork;
            case frontend::StatementKind::WaitFork:
                return sv::StatementKind::wait_fork;
            case frontend::StatementKind::DisableFork:
                return sv::StatementKind::disable_fork;
            case frontend::StatementKind::Disable:
                return sv::StatementKind::disable;
            case frontend::StatementKind::Display:
                return sv::StatementKind::display;
            case frontend::StatementKind::Report:
                return sv::StatementKind::report;
            case frontend::StatementKind::FileClose:
                return sv::StatementKind::file_close;
            case frontend::StatementKind::FileFlush:
                return sv::StatementKind::file_flush;
            case frontend::StatementKind::FileDisplay:
                return sv::StatementKind::file_display;
            case frontend::StatementKind::MemoryLoad:
                return sv::StatementKind::memory_transfer;
            case frontend::StatementKind::ContainerMethod:
                return sv::StatementKind::container_method;
            case frontend::StatementKind::MonitorControl:
                return sv::StatementKind::monitor_control;
            case frontend::StatementKind::Pause:
                return sv::StatementKind::pause;
            case frontend::StatementKind::Finish:
                return sv::StatementKind::finish;
            case frontend::StatementKind::Exit:
                return sv::StatementKind::exit_program;
            case frontend::StatementKind::Block:
                return sv::StatementKind::block;
            case frontend::StatementKind::Null:
                return sv::StatementKind::null_statement;
            case frontend::StatementKind::ProceduralAssign:
                return sv::StatementKind::procedural_assign;
            case frontend::StatementKind::Deassign:
                return sv::StatementKind::deassign;
            case frontend::StatementKind::WaitOrder:
                return sv::StatementKind::wait_order;
            }
            return sv::StatementKind::null_statement;
        }

        [[nodiscard]] sv::AssignmentKind assignment_kind(
            const frontend::AssignmentKind kind) const noexcept
        {
            switch (kind) {
            case frontend::AssignmentKind::Blocking:
                return sv::AssignmentKind::blocking;
            case frontend::AssignmentKind::NonBlocking:
                return sv::AssignmentKind::nonblocking;
            case frontend::AssignmentKind::Continuous:
            case frontend::AssignmentKind::VhdlSignal:
                return sv::AssignmentKind::continuous;
            }
            return sv::AssignmentKind::blocking;
        }

        [[nodiscard]] sv::AssignmentControl assignment_control(
            const frontend::ProceduralAssignmentControl control) const noexcept
        {
            switch (control) {
            case frontend::ProceduralAssignmentControl::None:
                return sv::AssignmentControl::none;
            case frontend::ProceduralAssignmentControl::Delay:
                return sv::AssignmentControl::delay;
            case frontend::ProceduralAssignmentControl::Event:
                return sv::AssignmentControl::event;
            }
            return sv::AssignmentControl::none;
        }

        [[nodiscard]] sv::UpdateKind update_kind(
            const frontend::ProceduralUpdateKind kind) const noexcept
        {
            switch (kind) {
            case frontend::ProceduralUpdateKind::None:
                return sv::UpdateKind::none;
            case frontend::ProceduralUpdateKind::Compound:
                return sv::UpdateKind::compound;
            case frontend::ProceduralUpdateKind::Prefix:
                return sv::UpdateKind::prefix;
            case frontend::ProceduralUpdateKind::Postfix:
                return sv::UpdateKind::postfix;
            }
            return sv::UpdateKind::none;
        }

        [[nodiscard]] sv::ForkJoinKind fork_join_kind(
            const frontend::ForkJoinKind kind) const noexcept
        {
            switch (kind) {
            case frontend::ForkJoinKind::All:
                return sv::ForkJoinKind::all;
            case frontend::ForkJoinKind::Any:
                return sv::ForkJoinKind::any;
            case frontend::ForkJoinKind::None:
                return sv::ForkJoinKind::none;
            }
            return sv::ForkJoinKind::all;
        }

        [[nodiscard]] sv::CaseMatchKind case_match_kind(
            const frontend::CaseMatchKind kind) const noexcept
        {
            if (kind == frontend::CaseMatchKind::VhdlMatching) {
                return sv::CaseMatchKind::exact;
            }
            // The frontend and HIR enums deliberately share the executable
            // SystemVerilog values. Preserve out-of-range values as well so
            // the direct-HIR lowerer, rather than this projection, owns the
            // dedicated invalid-mode diagnostic.
            return static_cast<sv::CaseMatchKind>(kind);
        }

        [[nodiscard]] sv::CaseQualifier case_qualifier(
            const frontend::CaseQualifier qualifier) const noexcept
        {
            return static_cast<sv::CaseQualifier>(qualifier);
        }

        [[nodiscard]] semantic::TypeId find_type(
            const semantic::ScopeId scope,
            const std::string_view spelling,
            const std::uint64_t source_order = 0U) const noexcept
        {
            auto visible_scope = std::optional { scope };
            while (visible_scope) {
                const auto local = std::ranges::find_if(
                    model_.types(), [&](const semantic::Type& type) {
                        return type.scope == *visible_scope
                            && type.name == spelling;
                    });
                if (local != model_.types().end()) {
                    return local->id;
                }

                const auto block = block_imports_.find(*visible_scope);
                if (block != block_imports_.end()) {
                    const auto resolve_imports = [&](const bool explicit_only) {
                        std::vector<semantic::TypeId> candidates;
                        bool explicit_import { };
                        for (const auto& import : block->second) {
                            if (source_order != 0U
                                && import.source_order != 0U
                                && import.source_order >= source_order) {
                                continue;
                            }
                            if (explicit_only
                                ? !import.member
                                    || import.member->spelling != spelling
                                : !import.wildcard) {
                                continue;
                            }
                            explicit_import = explicit_import || explicit_only;
                            const auto qualified = import.package.spelling
                                + "::" + std::string { spelling };
                            const auto selected
                                = systemverilog_hir_detail::find_systemverilog_hir_type(
                                    model_, hir_, *visible_scope, qualified);
                            if (selected.valid()
                                && std::ranges::find(candidates, selected)
                                    == candidates.end()) {
                                candidates.push_back(selected);
                            }
                        }
                        return std::pair {
                            std::move(candidates), explicit_import };
                    };
                    auto [candidates, explicit_import]
                        = resolve_imports(true);
                    if (!explicit_import) {
                        candidates = resolve_imports(false).first;
                    }
                    if (explicit_import || !candidates.empty()) {
                        return candidates.size() == 1U
                            ? candidates.front()
                            : semantic::TypeId { };
                    }
                }
                visible_scope = parent_scope(*visible_scope);
            }
            return systemverilog_hir_detail::find_systemverilog_hir_type(
                model_, hir_, scope, spelling, source_order);
        }

        [[nodiscard]] sv::TypeReference type_reference(
            const frontend::Type& input,
            const frontend::SourceSpan& fallback,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto spelling = input.named_type.empty()
                ? input.spelling
                : input.named_type;
            sv::TypeReference output;
            output.source_order = input.systemverilog_source_order;
            output.target = {
                input.named_type.empty() ? semantic::TypeId { }
                                         : find_type(
                                               scope, input.named_type,
                                               input.systemverilog_source_order),
                source(input.named_type.empty() ? fallback : input.named_type_span),
                spelling
            };
            // A procedural typedef cache can carry the cached frontend shape
            // of an alias whose lexical spelling now resolves to a different
            // visible declaration. Once the exact TypeId is known, its
            // TypeDefinition supplies the packed shape and value category.
            // Explicit inline types and packed arrays of simple typedefs keep
            // an empty named_type and retain their use-site shape below.
            const bool resolved_named_type = !input.named_type.empty()
                && output.target.target.valid();
            const auto type_form
                = systemverilog_hir_detail::systemverilog_type_form(input);
            output.value_form = resolved_named_type
                ? std::optional<sv::TypeForm> { }
                : std::optional<sv::TypeForm> { type_form };
            if (!input.systemverilog_class_declaration.empty()) {
                output.value_form = sv::TypeForm::class_handle;
                output.class_identity = input.systemverilog_class_declaration;
            }
            output.virtual_interface = input.systemverilog_virtual_interface;
            output.interface_type = input.systemverilog_interface_type;
            output.interface_modport = input.systemverilog_interface_modport;
            output.interface_parameter_actuals = systemverilog_hir_detail::
                systemverilog_class_parameter_actuals(
                    input, [&](const auto& span) { return source(span); },
                    [&](const auto& type, const auto& span) {
                        return type_reference(type, span, scope, parent);
                    },
                    [&](const auto& value) {
                        return expression(value, scope, parent);
                    });
            output.systemverilog_net_type = input.systemverilog_net_type;
            output.systemverilog_resolution_function
                = input.systemverilog_resolution_function;
            output.signed_value = !resolved_named_type && input.is_signed;
            output.executable_width = resolved_named_type
                ? std::optional<std::uint64_t> { }
                : input.width();
            output.four_state = !resolved_named_type
                && (input.domain == frontend::ValueDomain::Logic4
                    || input.domain == frontend::ValueDomain::Integer);
            if (!resolved_named_type) {
                if (input.packed_range) {
                    output.packed_range = sv::PackedRange {
                        input.packed_range->left,
                        input.packed_range->right,
                        std::nullopt,
                        std::nullopt,
                        input.packed_range->descending,
                        source(fallback)
                    };
                } else if (input.packed_range_expression) {
                    const auto& range = *input.packed_range_expression;
                    output.packed_range = sv::PackedRange {
                        std::nullopt,
                        std::nullopt,
                        expression(range.left, scope, parent),
                        expression(range.right, scope, parent),
                        range.descending.value_or(true),
                        source(range.span)
                    };
                }
                if (input.systemverilog_packed_dimensions.size() > 1U) {
                    for (const auto& range :
                        input.systemverilog_packed_dimensions) {
                        output.packed_dimensions.push_back({
                            std::nullopt,
                            std::nullopt,
                            expression(range.left, scope, parent),
                            expression(range.right, scope, parent),
                            range.descending.value_or(true),
                            source(range.span)
                        });
                    }
                }
            }
            for (const auto& range :
                input.systemverilog_interface_array_dimensions) {
                output.unpacked_dimensions.push_back({
                    std::nullopt,
                    std::nullopt,
                    expression(range.left, scope, parent),
                    expression(range.right, scope, parent),
                    range.descending.value_or(true),
                    source(range.span)
                });
            }
            if (input.systemverilog_container) {
                const auto& container = *input.systemverilog_container;
                output.container_form = type_form;
                output.queue_maximum = container.queue_maximum
                    ? expression(*container.queue_maximum, scope, parent)
                    : std::nullopt;
                if (container.associative_index_type) {
                    auto index_type = type_reference(
                        *container.associative_index_type, container.span,
                        scope, parent);
                    systemverilog_hir_detail::inherit_named_systemverilog_type_shape(index_type, hir_);
                    output.associative_index = index_type.target;
                    output.associative_index_metadata
                        = sv::make_associative_index_metadata(index_type);
                }
                for (const auto& range : container.static_range_expressions) {
                    output.unpacked_dimensions.push_back({ std::nullopt,
                        std::nullopt,
                        expression(range.left, scope, parent),
                        expression(range.right, scope, parent),
                        range.descending.value_or(true),
                        source(range.span) });
                }
                if (container.static_range_expressions.empty()
                    && container.static_range) {
                    output.unpacked_dimensions.push_back({ container.static_range->left,
                        container.static_range->right,
                        std::nullopt,
                        std::nullopt,
                        container.static_range->descending,
                        source(container.span) });
                }
                for (const auto& element : container.element_types) {
                    output.container_element_types.push_back(type_reference(
                        element, container.span, scope, parent));
                }
            }
            // An anonymous structure or union of a block-local declaration
            // is a type of its own, as at module level (IEEE 1800-2017
            // 7.2, 7.3); its members name its fields.
            const auto aggregate_form = output.value_form.value_or(
                sv::TypeForm::unresolved);
            if (input.named_type.empty() && !input.packed_members.empty()
                && !input.systemverilog_container
                && (aggregate_form == sv::TypeForm::packed_structure
                    || aggregate_form == sv::TypeForm::packed_union
                    || aggregate_form == sv::TypeForm::tagged_union
                    || aggregate_form == sv::TypeForm::unpacked_structure
                    || aggregate_form == sv::TypeForm::unpacked_union)) {
                const auto signature = systemverilog_hir_detail::
                    anonymous_aggregate_signature(input, scope);
                if (signature) {
                    if (const auto existing
                        = anonymous_aggregate_types_.find(*signature);
                        existing != anonymous_aggregate_types_.end()) {
                        output.target.target = existing->second;
                        if (input.systemverilog_type_of) {
                            output.type_of = expression(
                                *input.systemverilog_type_of, scope, parent);
                        }
                        return output;
                    }
                }
                const auto type_source = output.target.source;
                const auto type_origin = origin(
                    type_source, parent,
                    "anonymous SystemVerilog aggregate type");
                const auto name = "@anonymous-aggregate:"
                    + std::to_string(type_source.value()) + ":"
                    + std::to_string(parent.value());
                const auto base = output;
                const auto type_id = model_.add_type(scope,
                    semantic::TypeKind::implicit, name, base.target,
                    type_source, type_origin);
                output.target.target = type_id;
                sv::TypeDefinition definition;
                definition.id = type_id;
                definition.form = aggregate_form;
                definition.name = name;
                definition.base = base;
                definition.source = type_source;
                definition.origin = type_origin;
                for (const auto& member : input.packed_members) {
                    frontend::Type member_type;
                    member_type.domain = member.domain;
                    member_type.spelling = member.spelling;
                    member_type.packed_range = member.packed_range;
                    member_type.is_signed = member.is_signed;
                    member_type.packed_range_expression
                        = member.packed_range_expression;
                    if (!member.nested_types.empty()) {
                        member_type = member.nested_types.front();
                    }
                    auto converted = type_reference(
                        member_type, member.span, scope, type_origin);
                    if (!converted.target.target.valid()
                        && member.nested_types.empty()) {
                        converted.target.target = find_type(
                            scope, member.spelling);
                    }
                    definition.members.push_back({ member.name,
                        std::move(converted),
                        member.lsb_offset,
                        source(member.span),
                        member.initializer
                            ? expression(
                                  *member.initializer, scope, type_origin)
                            : std::nullopt,
                        member.random });
                }
                hir_.mutable_types().push_back(std::move(definition));
                if (signature) {
                    anonymous_aggregate_types_.emplace(*signature, type_id);
                }
            }
            // `type(expression)` as a data type (IEEE 1800-2017 6.23).
            if (input.systemverilog_type_of) {
                output.type_of = expression(*input.systemverilog_type_of, scope, parent);
            }
            return output;
        }

        void fill_type_expressions(
            const frontend::Type& input,
            const frontend::SourceSpan& fallback,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            static_cast<void>(type_reference(input, fallback, scope, parent));
            for (const auto& member : input.packed_members) {
                if (!member.nested_types.empty()) {
                    fill_type_expressions(
                        member.nested_types.front(), member.span, scope, parent);
                } else if (member.packed_range_expression) {
                    static_cast<void>(expression(
                        member.packed_range_expression->left, scope, parent));
                    static_cast<void>(expression(
                        member.packed_range_expression->right, scope, parent));
                }
            }
        }

        [[nodiscard]] sv::Declaration* find_declaration(
            const semantic::ScopeId scope,
            const sv::DeclarationForm form,
            const std::string_view declaration_name,
            const frontend::SourceSpan& span)
        {
            const auto declaration_source = source(span);
            for (const auto id : declarations_named(scope, declaration_name)) {
                auto* declaration = declaration_by_id(id);
                if (declaration != nullptr && declaration->form == form
                    && declaration->source == declaration_source) {
                    return declaration;
                }
            }
            return nullptr;
        }

        [[nodiscard]] semantic::DeclarationId block_variable(
            const frontend::VariableDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto variable_source = source(input.span);
            const auto variable_origin = origin(variable_source, parent, input.name);
            const auto declaration_id = model_.add_declaration(
                scope, semantic::DeclarationKind::variable, input.name,
                variable_source, variable_origin);
            sv::Declaration output;
            output.id = declaration_id;
            output.scope = scope;
            output.form = sv::DeclarationForm::variable;
            output.name = input.name;
            output.lifetime = input.systemverilog_automatic
                ? sv::Lifetime::automatic
                : input.systemverilog_static
                ? sv::Lifetime::static_lifetime
                : sv::Lifetime::implicit;
            output.source = variable_source;
            output.origin = variable_origin;
            output.type = type_reference(
                input.type, input.span, scope, variable_origin);
            output.declared_value = model_.add_value(
                scope,
                semantic::ValueKind::variable,
                input.name,
                output.type->target,
                variable_source,
                variable_origin);
            if (input.initializer) {
                output.initializer = expression(
                    *input.initializer, scope, variable_origin);
            }
            hir_.mutable_declarations().push_back(std::move(output));
            return declaration_id;
        }

        [[nodiscard]] semantic::StatementId statement(
            const frontend::Statement& input,
            const semantic::ScopeId enclosing_scope,
            const semantic::OriginId parent)
        {
            const auto statement_source = source(input.span);
            const auto statement_origin = origin(
                statement_source, parent,
                input.label.empty() ? "SystemVerilog statement" : input.label);
            const auto id = model_.add_statement_identity(
                enclosing_scope, statement_source, statement_origin);
            sv::Statement output;
            output.id = id;
            output.scope = enclosing_scope;
            output.kind = statement_kind(input.kind);
            output.label = input.label;
            output.source = statement_source;
            output.origin = statement_origin;
            auto child_scope = enclosing_scope;
            const bool declares_loop_variable
                = input.kind == frontend::StatementKind::Loop
                && input.loop_variable_declared
                && !input.loop_variable.empty();
            if (input.kind == frontend::StatementKind::Block
                || input.kind == frontend::StatementKind::Fork
                || declares_loop_variable
                || !input.declarations.empty()
                || !input.systemverilog_imports.empty()) {
                const auto scope_name = input.label.empty()
                    ? (input.kind == frontend::StatementKind::Fork
                              ? "<fork>"
                          : declares_loop_variable
                              ? "<loop>"
                              : "<block>")
                    : input.label;
                child_scope = model_.add_scope(
                    scope_unit(enclosing_scope), enclosing_scope, scope_name,
                    statement_source, statement_origin);
                output.nested_scope = child_scope;
                if (!input.systemverilog_imports.empty()) {
                    auto& imports = block_imports_[child_scope];
                    imports.reserve(input.systemverilog_imports.size());
                    for (const auto& input_import
                        : input.systemverilog_imports) {
                        sv::Import converted {
                            name(input_import.package, input_import.span,
                                child_scope,
                                input_import.systemverilog_source_order),
                            input_import.name.empty()
                                ? std::nullopt
                                : std::optional<sv::Name> { name(
                                      input_import.name, input_import.span,
                                      child_scope,
                                      input_import.systemverilog_source_order) },
                            input_import.name.empty(),
                            source(input_import.span),
                        };
                        converted.source_order
                            = input_import.systemverilog_source_order;
                        imports.push_back(converted);
                        output.imports.push_back(std::move(converted));
                    }
                }
                if ((input.kind == frontend::StatementKind::Block
                        || input.kind == frontend::StatementKind::Fork)
                    && !input.type_aliases.empty()) {
                    std::vector<std::pair<semantic::ScopeId, sv::Import>>
                        visible_imports;
                    auto import_scope = std::optional { child_scope };
                    while (import_scope) {
                        const auto found = block_imports_.find(*import_scope);
                        if (found != block_imports_.end()) {
                            for (const auto& imported : found->second) {
                                visible_imports.emplace_back(
                                    *import_scope, imported);
                            }
                        }
                        import_scope = parent_scope(*import_scope);
                    }
                    const auto alias_declarations
                        = type_builder_.add_statement_type_aliases(
                            std::span<const frontend::TypeAliasDeclaration> {
                                input.type_aliases.data(),
                                input.type_aliases.size() },
                            std::span<const std::pair<
                                semantic::ScopeId, sv::Import>> {
                                visible_imports },
                            child_scope,
                            statement_origin);
                    output.declarations.insert(
                        output.declarations.end(),
                        alias_declarations.begin(), alias_declarations.end());
                }
                for (const auto& declaration : input.declarations) {
                    output.declarations.push_back(block_variable(
                        declaration, child_scope, statement_origin));
                }
                if (declares_loop_variable) {
                    frontend::VariableDeclaration loop_variable;
                    loop_variable.name = input.loop_variable;
                    // A declaration in the for initializer has automatic
                    // lifetime even inside a static callable (12.7.1).
                    loop_variable.systemverilog_automatic = true;
                    loop_variable.type = input.loop_variable_type
                        ? *input.loop_variable_type
                        : frontend::Type {
                              frontend::ValueDomain::Integer,
                              "int",
                              std::nullopt,
                              true,
                          };
                    loop_variable.span = input.target.valid()
                        ? input.target.span
                        : input.span;
                    output.declarations.push_back(block_variable(
                        loop_variable, child_scope, statement_origin));
                }
            }
            output.assignment_kind = assignment_kind(input.assignment_kind);
            output.target = expression(input.target, child_scope, statement_origin);
            output.value = expression(input.value, child_scope, statement_origin);
            output.condition = expression(
                input.condition, child_scope, statement_origin);
            output.task = name(
                input.task_name, input.span, child_scope,
                input.systemverilog_source_order);
            const auto implicit_class_receiver
                = bind_implicit_class_task_call(
                    output, input, child_scope, statement_origin);
            // A function called as a statement (IEEE 1800-2017 13.4.1)
            // discards its result; it lowers like `void'(f(...))`.
            const bool function_statement
                = input.kind == frontend::StatementKind::TaskCall
                && !input.task_name.empty()
                && input.task_name.front() != '$'
                && !output.task.overloads.empty()
                && std::ranges::all_of(output.task.overloads,
                    [&](const semantic::DeclarationId id) {
                        const auto* declaration = hir_.find_declaration(id);
                        return declaration != nullptr
                            && declaration->callable
                            && declaration->callable->function;
                    });
            if (function_statement) {
                frontend::Expression call;
                call.kind = frontend::ExpressionKind::Call;
                call.text = input.task_name;
                call.span = input.span;
                call.systemverilog_source_order
                    = input.systemverilog_source_order;
                for (const auto& argument : input.task_arguments) {
                    call.operands.push_back(argument);
                }
                for (const auto& argument_name : input.task_argument_names) {
                    call.call_argument_names.push_back(argument_name);
                }
                output.kind = sv::StatementKind::container_method;
                output.value = expression(call, child_scope, statement_origin);
            }
            for (std::size_t index = 0; index < input.task_arguments.size(); ++index) {
                const auto& argument = input.task_arguments[index];
                sv::TaskAssociation association;
                if (index < input.task_argument_names.size()
                    && !input.task_argument_names[index].empty()) {
                    association.formal = input.task_argument_names[index];
                }
                association.actual = expression(
                    argument, child_scope, statement_origin);
                association.source = argument.valid()
                    ? source(argument.span)
                    : statement_source;
                output.task_arguments.push_back(std::move(association));
            }
            if (implicit_class_receiver) {
                output.task_arguments.insert(
                    output.task_arguments.begin(),
                    sv::TaskAssociation {
                        std::nullopt, *implicit_class_receiver,
                        statement_source
                    });
            }
            output.loop_variable = input.loop_variable;
            output.loop_variable_declared = input.loop_variable_declared;
            output.loop_initial = expression(
                input.loop_initial, child_scope, statement_origin);
            output.loop_limit = expression(
                input.loop_limit, child_scope, statement_origin);
            output.loop_update_target = expression(
                input.loop_update_target, child_scope, statement_origin);
            for (const auto& update : input.loop_updates) {
                output.loop_updates.push_back(statement(
                    update, child_scope, statement_origin));
            }
            output.loop_descending = input.loop_descending;
            output.loop_limit_exclusive = input.loop_limit_exclusive;
            output.loop_repeat = input.loop_repeat;
            output.loop_runtime = input.loop_runtime;
            output.loop_post_test = input.loop_post_test;
            output.fork_join = fork_join_kind(input.fork_join_kind);
            output.fork_isolate_register_frame
                = input.fork_isolate_register_frame;
            output.fork_detached = input.fork_detached;
            output.assignment_control = assignment_control(
                input.procedural_assignment_control);
            output.assignment_control_repeated = input.procedural_assignment_repeat;
            output.update_kind = update_kind(input.procedural_update_kind);
            output.update_operator = input.procedural_update_operator;
            if (input.delay) {
                output.delay = delay(*input.delay, child_scope, statement_origin);
            }
            output.clocking_cycle_delay = input.clocking_cycle_delay;
            output.clocking_cycle_count = expression(
                input.clocking_cycle_count, child_scope, statement_origin);
            if (input.verilog_drive_strength) {
                output.drive_zero = static_cast<std::uint8_t>(
                    input.verilog_drive_strength->zero);
                output.drive_one = static_cast<std::uint8_t>(
                    input.verilog_drive_strength->one);
            }
            output.verilog_switch_driver = input.verilog_switch_driver;
            output.verilog_switch_bidirectional
                = input.verilog_switch_bidirectional;
            output.verilog_switch_resistive
                = input.verilog_switch_resistive;
            output.verilog_switch_source = expression(
                input.verilog_switch_source,
                child_scope,
                statement_origin);
            output.verilog_switch_control = expression(
                input.verilog_switch_control,
                child_scope,
                statement_origin);
            output.verilog_switch_active_high
                = input.verilog_switch_active_high;
            for (const auto& item : input.sensitivities) {
                output.sensitivities.push_back(sensitivity(
                    item, child_scope, statement_origin));
            }
            output.assertion_message = input.assertion_message;
            output.assertion_severity
                = elaboration::hir_assertion_severity(input.assertion_severity);
            output.assertion_has_pass_action = input.assertion_has_pass_action;
            output.assertion_has_failure_action = input.assertion_has_failure_action;
            output.output_text = input.output_text;
            output.output_generated_text = static_cast<sv::GeneratedTextKind>(
                input.output_generated_text);
            output.output_newline = input.output_newline;
            output.output_postponed = input.output_postponed;
            if (input.output_format) {
                output.output_format
                    = elaboration::hir_output_format(*input.output_format);
            }
            output.output_prefix = input.output_prefix;
            output.output_suffix = input.output_suffix;
            output.output_suppress_leading_zero = input.output_suppress_leading_zero;
            output.output_minimum_width = input.output_minimum_width;
            output.output_precision = input.output_precision;
            output.output_left_justify = input.output_left_justify;
            output.output_zero_pad = input.output_zero_pad;
            output.output_monitor = input.output_monitor;
            output.monitor_enabled = input.monitor_enabled;
            output.file_handle = expression(
                input.file_handle, child_scope, statement_origin);
            output.memory_hex = input.memory_hex;
            output.memory_write = input.memory_write;
            for (const auto& item : input.output_values) {
                const auto value = expression(
                    item.value, child_scope, statement_origin);
                if (value
                    || item.format == frontend::OutputFormat::Hierarchy
                    || item.format == frontend::OutputFormat::Library
                    || item.format == frontend::OutputFormat::Time) {
                    output.output_values.push_back({ value,
                        elaboration::hir_output_format(item.format),
                        item.prefix,
                        item.suppress_leading_zero,
                        item.minimum_width,
                        item.left_justify,
                        item.zero_pad,
                        item.precision });
                }
            }
            output.output_trailing_text = input.output_trailing_text;
            const auto transfer_type = !input.target.nominal_type.empty()
                ? input.target.nominal_type
                : input.value.nominal_type;
            if (!transfer_type.empty()
                && (input.kind == frontend::StatementKind::Assignment
                    || input.kind == frontend::StatementKind::Return)) {
                output.class_handle_transfer = true;
                output.class_handle_type = transfer_type;
            }
            for (const auto& child : input.statements) {
                output.statements.push_back(statement(
                    child, child_scope, statement_origin));
            }
            for (const auto& child : input.else_statements) {
                output.else_statements.push_back(statement(
                    child, child_scope, statement_origin));
            }
            output.case_match = case_match_kind(input.case_match_kind);
            output.matches_wildcard
                = case_match_kind(input.case_matches_wildcard);
            output.case_qualifier = case_qualifier(input.case_qualifier);
            for (const auto& alternative : input.case_alternatives) {
                sv::CaseAlternative converted;
                converted.is_default = alternative.is_default;
                converted.source = source(alternative.span);
                for (const auto& choice : alternative.choices) {
                    if (const auto choice_id = expression(
                            choice, child_scope, statement_origin)) {
                        converted.choices.push_back(*choice_id);
                    }
                }
                for (const auto& child : alternative.statements) {
                    converted.statements.push_back(statement(
                        child, child_scope, statement_origin));
                }
                output.case_alternatives.push_back(std::move(converted));
            }
            hir_.mutable_statements().push_back(std::move(output));
            return id;
        }

        [[nodiscard]] sv::ProcessKind process_kind(
            const frontend::ProcessKind kind) const noexcept
        {
            switch (kind) {
            case frontend::ProcessKind::VerilogAlways:
                return sv::ProcessKind::always;
            case frontend::ProcessKind::SystemVerilogAlwaysFF:
                return sv::ProcessKind::always_ff;
            case frontend::ProcessKind::SystemVerilogAlwaysComb:
                return sv::ProcessKind::always_comb;
            case frontend::ProcessKind::SystemVerilogAlwaysLatch:
                return sv::ProcessKind::always_latch;
            case frontend::ProcessKind::Initial:
                return sv::ProcessKind::initial;
            case frontend::ProcessKind::Final:
                return sv::ProcessKind::final;
            case frontend::ProcessKind::VhdlProcess:
                return sv::ProcessKind::always;
            }
            return sv::ProcessKind::always;
        }

        [[nodiscard]] const sv::Declaration& existing_variable(
            const frontend::VariableDeclaration& input,
            const semantic::ScopeId scope)
        {
            auto found = find_declaration(
                scope, sv::DeclarationForm::variable, input.name, input.span);
            // A block parameter is projected as a constant of the block
            // (IEEE 1800-2017 6.20.1).
            for (const auto form : { sv::DeclarationForm::local_parameter,
                     sv::DeclarationForm::parameter }) {
                if (found == nullptr && input.systemverilog_local_parameter) {
                    found = find_declaration(
                        scope, form, input.name, input.span);
                }
            }
            if (found == nullptr) {
                throw std::logic_error { "missing SystemVerilog HIR process variable" };
            }
            return *found;
        }

        void fill_process(
            const frontend::Process& input,
            const semantic::ProcessId id)
        {
            const auto& identity = model_.process_identities()[id.value()];
            sv::Process output;
            output.id = id;
            output.scope = identity.scope;
            output.kind = process_kind(input.kind);
            output.name = identity.name;
            output.source = identity.source;
            output.origin = identity.origin;
            output.concurrent_assertion
                = input.systemverilog_concurrent_assertion;
            for (const auto& declaration : input.variables) {
                const auto* hir_declaration
                    = &existing_variable(declaration, identity.scope);
                fill_type_expressions(
                    declaration.type, declaration.span, identity.scope,
                    hir_declaration->origin);
                if (declaration.initializer) {
                    static_cast<void>(expression(
                        *declaration.initializer, identity.scope,
                        hir_declaration->origin));
                }
            }
            for (const auto& parameter : input.constants) {
                fill_parameter(parameter, identity.scope);
            }
            for (const auto& type : input.type_aliases) {
                fill_type_declaration(type, identity.scope);
            }
            for (const auto& function : input.functions) {
                fill_function(function, identity.scope);
            }
            const auto in_scope = declarations_in_scope(identity.scope);
            output.declarations.insert(
                output.declarations.end(), in_scope.begin(), in_scope.end());
            for (const auto& item : input.sensitivities) {
                output.sensitivities.push_back(sensitivity(
                    item, identity.scope, identity.origin));
            }
            for (const auto& item : input.statements) {
                output.statements.push_back(statement(
                    item, identity.scope, identity.origin));
            }
            hir_.mutable_processes().push_back(std::move(output));
        }

        void fill_parameter(
            const frontend::ParameterDeclaration& input,
            const semantic::ScopeId scope)
        {
            const auto form = input.kind == frontend::ParameterKind::Type
                ? sv::DeclarationForm::type_parameter
                : (input.local ? sv::DeclarationForm::local_parameter
                               : sv::DeclarationForm::parameter);
            auto* declaration = find_declaration(scope, form, input.name, input.span);
            if (declaration == nullptr) {
                return;
            }
            const auto declaration_origin = declaration->origin;
            fill_type_expressions(
                input.type, input.span, scope, declaration_origin);
            if (input.default_type) {
                fill_type_expressions(
                    *input.default_type, input.span, scope, declaration_origin);
            }
            if (input.kind != frontend::ParameterKind::Type) {
                static_cast<void>(expression(
                    input.default_value, scope, declaration_origin));
            }
        }

        template <typename Input>
        void fill_object(
            const Input& input,
            const semantic::ScopeId scope,
            const sv::DeclarationForm form,
            const std::optional<frontend::Expression>& initializer)
        {
            auto* declaration = find_declaration(scope, form, input.name, input.span);
            if (declaration == nullptr) {
                return;
            }
            const auto id = declaration->id;
            const auto declaration_origin = declaration->origin;
            fill_type_expressions(
                input.type, input.span, scope, declaration_origin);
            if (initializer) {
                static_cast<void>(expression(
                    *initializer, scope, declaration_origin));
            }
            if constexpr (std::same_as<Input, frontend::SignalDeclaration>) {
                if (input.net_delay) {
                    const auto converted = delay(
                        *input.net_delay, scope, declaration_origin);
                    declaration_by_id(id)->delay = converted;
                }
            }
        }

        void fill_type_declaration(
            const frontend::TypeAliasDeclaration& input,
            const semantic::ScopeId scope)
        {
            auto* declaration = find_declaration(
                scope, sv::DeclarationForm::typedef_declaration,
                input.name, input.span);
            if (declaration == nullptr) {
                return;
            }
            const auto declaration_origin = declaration->origin;
            fill_type_expressions(
                input.type, input.span, scope, declaration_origin);
            for (const auto& member : input.type.packed_members) {
                if (member.initializer) {
                    static_cast<void>(expression(
                        *member.initializer, scope, declaration_origin));
                }
            }
            for (const auto& literal : input.enum_literals) {
                auto* literal_declaration = find_declaration(
                    scope, sv::DeclarationForm::enumeration_literal,
                    literal.name, literal.span);
                if (literal_declaration != nullptr) {
                    static_cast<void>(expression(
                        literal.value, scope, literal_declaration->origin));
                }
            }
        }

        void fill_function(
            const frontend::FunctionDeclaration& input,
            const semantic::ScopeId scope)
        {
            auto* declaration = find_declaration(
                scope, sv::DeclarationForm::function, input.name, input.span);
            if (declaration == nullptr || !declaration->nested_scope) {
                return;
            }
            const auto id = declaration->id;
            const auto callable_scope = *declaration->nested_scope;
            const auto callable_origin = declaration->origin;
            fill_type_expressions(
                input.return_type, input.span, scope, callable_origin);
            for (const auto& parameter : input.constants) {
                fill_parameter(parameter, callable_scope);
            }
            for (const auto& type : input.type_aliases) {
                fill_type_declaration(type, callable_scope);
            }
            for (const auto& argument : input.arguments) {
                fill_object(
                    argument, callable_scope, sv::DeclarationForm::port,
                    argument.default_value);
            }
            for (const auto& variable : input.variables) {
                fill_object(
                    variable, callable_scope, sv::DeclarationForm::variable,
                    variable.initializer);
            }
            for (const auto& nested : input.functions) {
                fill_function(nested, callable_scope);
            }
            std::vector<semantic::StatementId> statements;
            for (const auto& item : input.statements) {
                statements.push_back(statement(item, callable_scope, callable_origin));
            }
            declaration_by_id(id)->statements = std::move(statements);
        }

        void fill_task(
            const frontend::TaskDeclaration& input,
            const semantic::ScopeId scope)
        {
            auto* declaration = find_declaration(
                scope, sv::DeclarationForm::task, input.name, input.span);
            if (declaration == nullptr || !declaration->nested_scope) {
                return;
            }
            const auto id = declaration->id;
            const auto callable_scope = *declaration->nested_scope;
            const auto callable_origin = declaration->origin;
            for (const auto& argument : input.arguments) {
                fill_object(
                    argument, callable_scope, sv::DeclarationForm::port,
                    argument.default_value);
            }
            for (const auto& variable : input.variables) {
                fill_object(
                    variable, callable_scope, sv::DeclarationForm::variable,
                    variable.initializer);
            }
            std::vector<semantic::StatementId> statements;
            for (const auto& item : input.statements) {
                statements.push_back(statement(item, callable_scope, callable_origin));
            }
            declaration_by_id(id)->statements = std::move(statements);
        }

        void fill_class(
            const frontend::SystemVerilogClassDeclaration& input,
            const std::optional<semantic::ScopeId> parent_scope)
        {
            const auto input_source = source(input.span);
            const auto found = std::ranges::find_if(
                hir_.mutable_classes(), [&](const sv::ClassDeclaration& item) {
                    if (parent_scope && item.source == input_source
                        && item.scope.valid()
                        && model_.scopes()[item.scope.value()].parent
                            == parent_scope) {
                        return true;
                    }
                    return !parent_scope
                        && !input.canonical_identity.empty()
                        && item.canonical_identity
                            == input.canonical_identity;
                });
            if (found == hir_.mutable_classes().end())
                return;
            const auto class_scope = found->scope;
            for (const auto& method : input.methods) {
                if (method.kind
                    == frontend::SystemVerilogClassMethodKind::Task) {
                    fill_task(
                        systemverilog_hir_detail::class_task(method),
                        class_scope);
                } else {
                    fill_function(
                        systemverilog_hir_detail::class_function(method),
                        class_scope);
                }
            }
            fill_classes(input.nested_classes, class_scope);
        }

        void fill_classes(
            const std::vector<frontend::SystemVerilogClassDeclaration>& inputs,
            const std::optional<semantic::ScopeId> parent_scope)
        {
            for (const auto& input : inputs)
                fill_class(input, parent_scope);
        }

        void fill_generate_body(
            const frontend::GenerateBody& input,
            sv::GenerateRegion& output)
        {
            for (const auto& parameter : input.constants) {
                fill_parameter(parameter, output.scope);
            }
            for (const auto& type : input.type_aliases) {
                fill_type_declaration(type, output.scope);
            }
            for (const auto& signal : input.signals) {
                fill_object(
                    signal, output.scope,
                    signal.is_port ? sv::DeclarationForm::port
                                   : sv::DeclarationForm::net,
                    signal.default_value);
            }
            for (const auto& variable : input.variables) {
                fill_object(
                    variable, output.scope, sv::DeclarationForm::variable,
                    variable.initializer);
            }
            for (const auto& function : input.functions) {
                fill_function(function, output.scope);
            }
            for (const auto& task : input.tasks) {
                fill_task(task, output.scope);
            }
            fill_classes(input.systemverilog_classes, output.scope);
            for (std::size_t index = 0;
                index < input.processes.size() && index < output.processes.size();
                ++index) {
                fill_process(input.processes[index], output.processes[index]);
            }
            for (const auto& item : input.concurrent_statements) {
                output.concurrent_statements.push_back(statement(
                    item, output.scope, output.origin));
            }
            for (const auto& nested_input : input.generate_regions) {
                const auto nested_source = source(nested_input.span);
                const auto nested = std::ranges::find_if(
                    output.nested, [&](const sv::GenerateRegion& candidate) {
                        return candidate.source == nested_source;
                    });
                if (nested != output.nested.end()) {
                    fill_generate(nested_input, *nested);
                }
            }
        }

        void fill_generate(
            const frontend::GenerateRegion& input,
            sv::GenerateRegion& output)
        {
            static_cast<void>(expression(
                input.initial, output.scope, output.origin));
            static_cast<void>(expression(
                input.condition, output.scope, output.origin));
            static_cast<void>(expression(
                input.iteration, output.scope, output.origin));
            fill_generate_body(input.then_body, output);
            std::size_t nested_index = input.then_body.generate_regions.size();
            if (!input.else_scope.empty()
                || !input.else_body.constants.empty()
                || !input.else_body.type_aliases.empty()
                || !input.else_body.signals.empty()
                || !input.else_body.variables.empty()
                || !input.else_body.functions.empty()
                || !input.else_body.tasks.empty()
                || !input.else_body.systemverilog_aliases.empty()
                || !input.else_body.systemverilog_lets.empty()
                || !input.else_body.systemverilog_classes.empty()
                || !input.else_body.processes.empty()
                || !input.else_body.concurrent_statements.empty()
                || !input.else_body.instances.empty()
                || !input.else_body.generate_regions.empty()) {
                if (nested_index < output.nested.size()) {
                    fill_generate_body(input.else_body, output.nested[nested_index]);
                }
                ++nested_index;
            }
            for (std::size_t alternative_index = 0U;
                 alternative_index < input.alternatives.size();
                 ++alternative_index) {
                const auto& alternative
                    = input.alternatives[alternative_index];
                if (alternative_index >= output.alternatives.size()) {
                    ++nested_index;
                    continue;
                }
                auto& converted = output.alternatives[alternative_index];
                converted.source = source(alternative.span);
                converted.label = alternative.scope;
                converted.is_default = alternative.is_default;
                converted.choices.clear();
                for (const auto& choice : alternative.choices) {
                    const auto left = expression(
                        choice.left, output.scope, output.origin);
                    if (!left) {
                        continue;
                    }
                    sv::GenerateChoice converted_choice;
                    converted_choice.left = *left;
                    converted_choice.descending = choice.descending;
                    converted_choice.source = source(choice.span);
                    if (choice.right) {
                        converted_choice.right = expression(
                            *choice.right, output.scope, output.origin);
                    }
                    converted.choices.push_back(std::move(converted_choice));
                }
                if (nested_index < output.nested.size()) {
                    converted.scope = output.nested[nested_index].scope;
                    converted.alternative_discriminator
                        = output.nested[nested_index]
                              .alternative_discriminator;
                    fill_generate_body(
                        alternative.body, output.nested[nested_index]);
                    converted.class_declarations
                        = output.nested[nested_index].class_declarations;
                }
                ++nested_index;
            }
        }

        void fill_unit(
            const frontend::DesignUnit& input,
            sv::Unit& output)
        {
            for (std::size_t index = 0;
                index < input.systemverilog_aliases.size()
                && index < output.aliases.size();
                ++index) {
                const auto& input_alias = input.systemverilog_aliases[index];
                auto& alias = output.aliases[index];
                alias.terminals.clear();
                for (const auto& terminal : input_alias.terminals) {
                    if (const auto id = expression(
                            terminal, output.scope, alias.origin)) {
                        alias.terminals.push_back(*id);
                    }
                }
            }
            for (std::size_t index = 0;
                index < input.systemverilog_lets.size()
                && index < output.lets.size();
                ++index) {
                const auto& input_let = input.systemverilog_lets[index];
                auto& let = output.lets[index];
                for (std::size_t port_index = 0;
                    port_index < input_let.ports.size()
                    && port_index < let.ports.size();
                    ++port_index) {
                    const auto& input_port = input_let.ports[port_index];
                    auto& port = let.ports[port_index];
                    if (input_port.type) {
                        port.type = type_reference(
                            *input_port.type, input_port.span,
                            output.scope, let.origin);
                    }
                    if (input_port.default_value) {
                        port.default_value = expression(
                            *input_port.default_value,
                            output.scope, let.origin);
                    }
                }
                if (const auto id = expression(
                        input_let.expression, output.scope, let.origin)) {
                    let.expression = *id;
                }
            }
            for (const auto& parameter : input.parameters) {
                fill_parameter(parameter, output.scope);
            }
            for (const auto& type : input.type_aliases) {
                fill_type_declaration(type, output.scope);
            }
            for (const auto& port : input.ports) {
                fill_object(
                    port, output.scope, sv::DeclarationForm::port,
                    port.default_value);
            }
            for (const auto& signal : input.signals) {
                fill_object(
                    signal, output.scope,
                    signal.is_port ? sv::DeclarationForm::port
                                   : sv::DeclarationForm::net,
                    signal.default_value);
            }
            for (const auto& variable : input.variables) {
                fill_object(
                    variable, output.scope, sv::DeclarationForm::variable,
                    variable.initializer);
            }
            for (const auto& function : input.functions) {
                fill_function(function, output.scope);
            }
            for (const auto& task : input.tasks) {
                fill_task(task, output.scope);
            }
            fill_classes(input.systemverilog_classes, output.scope);
            for (std::size_t index = 0;
                index < input.processes.size() && index < output.processes.size();
                ++index) {
                fill_process(input.processes[index], output.processes[index]);
            }
            for (const auto& item : input.concurrent_statements) {
                output.concurrent_statements.push_back(statement(
                    item, output.scope, output.origin));
            }
            for (std::size_t index = 0;
                index < input.generate_regions.size()
                && index < output.generates.size();
                ++index) {
                fill_generate(input.generate_regions[index], output.generates[index]);
            }
        }

        void resolve_expression_names()
        {
            // The generate regions are complete now.
            genvar_scopes_.reset();
            for (auto& item : hir_.mutable_expressions()) {
                if (!item.referenced_name) {
                    continue;
                }
                item.referenced_name->selected.reset();
                item.referenced_name->overloads.clear();
                auto visible_scope = std::optional<semantic::ScopeId> { item.scope };
                bool genvar = false;
                while (visible_scope) {
                    const auto named = declarations_named(
                        *visible_scope, item.referenced_name->spelling);
                    item.referenced_name->overloads.insert(
                        item.referenced_name->overloads.end(),
                        named.begin(), named.end());
                    if (!item.referenced_name->overloads.empty()) {
                        break;
                    }
                    // A loop-generate genvar hides outer declarations of its
                    // name (IEEE 1800-2017 27.4).
                    if (genvar_scope(*visible_scope,
                            item.referenced_name->spelling)) {
                        genvar = true;
                        break;
                    }
                    if (append_imported_name_candidates(
                            *item.referenced_name, *visible_scope,
                            item.referenced_name->source_order)) {
                        break;
                    }
                    visible_scope = parent_scope(*visible_scope);
                }
                if (genvar) {
                    continue;
                }
                if (item.referenced_name->overloads.size() == 1) {
                    item.referenced_name->selected = item.referenced_name->overloads.front();
                }
            }
        }

        // Declarations by scope (and name) and by id, extended over the
        // declarations appended since the last lookup, in HIR order. The
        // builders only append declarations and never rescope or rename
        // one; a scan of every declaration per name made building
        // quadratic in the design's declarations.
        struct ScopeDeclarations {
            std::vector<semantic::DeclarationId> all;
            std::unordered_map<std::string,
                std::vector<semantic::DeclarationId>,
                TransparentStringHash, std::equal_to<>>
                by_name;
        };

        void index_declarations()
        {
            const auto& declarations = hir_.declarations();
            if (declarations.size() < indexed_declarations_) {
                declarations_by_scope_.clear();
                declaration_positions_.clear();
                indexed_declarations_ = 0U;
            }
            for (; indexed_declarations_ < declarations.size();
                ++indexed_declarations_) {
                const auto& declaration = declarations[indexed_declarations_];
                auto& scope = declarations_by_scope_[declaration.scope.value()];
                scope.all.push_back(declaration.id);
                scope.by_name[declaration.name].push_back(declaration.id);
                declaration_positions_.try_emplace(
                    declaration.id.value(), indexed_declarations_);
            }
        }

        [[nodiscard]] std::span<const semantic::DeclarationId>
        declarations_named(
            const semantic::ScopeId scope, const std::string_view spelling)
        {
            index_declarations();
            const auto in_scope = declarations_by_scope_.find(scope.value());
            if (in_scope == declarations_by_scope_.end()) {
                return { };
            }
            const auto found = in_scope->second.by_name.find(spelling);
            return found == in_scope->second.by_name.end()
                ? std::span<const semantic::DeclarationId> { }
                : std::span<const semantic::DeclarationId> { found->second };
        }

        [[nodiscard]] std::span<const semantic::DeclarationId>
        declarations_in_scope(const semantic::ScopeId scope)
        {
            index_declarations();
            const auto found = declarations_by_scope_.find(scope.value());
            return found == declarations_by_scope_.end()
                ? std::span<const semantic::DeclarationId> { }
                : std::span<const semantic::DeclarationId> {
                      found->second.all };
        }

        [[nodiscard]] sv::Declaration* declaration_by_id(
            const semantic::DeclarationId id)
        {
            index_declarations();
            const auto found = declaration_positions_.find(id.value());
            return found == declaration_positions_.end()
                ? nullptr
                : &hir_.mutable_declarations()[found->second];
        }

        semantic::Model& model_;
        sv::Hir& hir_;
        systemverilog_hir_detail::SystemVerilogHirBuilder& type_builder_;
        std::unordered_map<std::uint32_t, ScopeDeclarations>
            declarations_by_scope_;
        std::unordered_map<std::uint32_t, std::size_t> declaration_positions_;
        std::size_t indexed_declarations_ { };
        std::map<semantic::ScopeId, std::vector<sv::Import>> block_imports_;
        std::unordered_map<std::string, semantic::TypeId>
            anonymous_aggregate_types_;
        std::optional<std::map<semantic::ScopeId, std::string>> genvar_scopes_;
        std::set<std::uint32_t> claimed_expressions_;
        std::optional<ExpressionCandidateIndex> expression_identity_candidate_index_;
        std::optional<ExpressionCandidateIndex> hir_expression_candidate_index_;
    };

} // namespace

void complete_systemverilog_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::sv::Hir& hir,
    systemverilog_hir_detail::SystemVerilogHirBuilder& type_builder)
{
    SystemVerilogExecutableBuilder builder { semantics, hir, type_builder };
    builder.add_design(parsed);
}

} // namespace fsim::app::application_detail
