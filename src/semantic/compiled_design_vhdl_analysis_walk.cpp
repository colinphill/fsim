// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_vhdl_analysis_internal.hpp"

#include "fsim/support/symbol.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The walk of compile-time VHDL name analysis (gate BD build D3): every Name
// of a unit that visibility resolves (IEEE 1076-2019 12.3, 12.4), with the
// scope of its site, and the split of a selected name into simple names
// (8.3). Names resolved by context are not visited; each skip below says
// which HIR field makes the site context-resolved.

namespace fsim::semantic::vhdl_names {

namespace {

// Records of one HIR table by id. Ids are usually dense (record n has id n),
// which is checked first; any other layout is indexed once, on first need.
template <typename Record, typename IdType>
class Table {
public:
    explicit Table(std::vector<Record>& records) noexcept
        : records_(records)
    {
    }

    [[nodiscard]] Record* find(const IdType id)
    {
        if (!id.valid()) {
            return nullptr;
        }
        if (id.value() < records_.size() && records_[id.value()].id == id) {
            return &records_[id.value()];
        }
        if (!indexed_) {
            indexed_ = true;
            for (std::size_t index { }; index < records_.size(); ++index) {
                index_.emplace(records_[index].id.value(), index);
            }
        }
        const auto found = index_.find(id.value());
        return found == index_.end() ? nullptr : &records_[found->second];
    }

private:
    std::vector<Record>& records_;
    std::unordered_map<std::uint32_t, std::size_t> index_;
    bool indexed_ { };
};

class Walker {
public:
    Walker(vhdl::Hir& hir, const std::function<void(const NameSite&)>& visit)
        : visit_(visit)
        , units_(hir.mutable_units())
        , declarations_(hir.mutable_declarations())
        , types_(hir.mutable_types())
        , expressions_(hir.mutable_expressions())
        , statements_(hir.mutable_statements())
        , processes_(hir.mutable_processes())
        , instances_(hir.mutable_instances())
    {
    }

    void walk(const vhdl::Unit& unit)
    {
        auto* const owner = units_.find(unit.id);
        if (owner == nullptr) {
            return;
        }
        const auto scope = owner->scope;
        legacy_file_modes_ = owner->standard == "1987";
        // A use clause (and a context reference, 13.4) selects from its
        // library, never from the region it is written in. A library
        // clause's names are logical library names (13.2): context-resolved.
        for (auto& item : owner->context) {
            if (item.kind == vhdl::ContextKind::library_clause) {
                continue;
            }
            for (auto& name : item.selected_names) {
                emit(name, scope, NameRole::use_clause);
            }
        }
        for (const auto id : owner->declarations) {
            declaration(id);
        }
        for (auto& specification : owner->disconnection_specifications) {
            disconnection(specification, scope);
        }
        for (const auto id : owner->processes) {
            process(id);
        }
        for (const auto id : owner->instances) {
            instance(id);
        }
        for (const auto id : owner->concurrent_statements) {
            statement(id);
        }
        for (auto& region : owner->generates) {
            generate(region);
        }
        for (auto& component : owner->component_configurations) {
            component_configuration(component, scope);
        }
        if (owner->configuration) {
            block_configuration(*owner->configuration, scope);
        }
    }

private:
    bool legacy_file_modes_ { };

    void emit(vhdl::Name& name, const ScopeId scope, const NameRole role)
    {
        // A Name the front end left unset (no designator) is not a site.
        if (name.text().empty()) {
            return;
        }
        visit_(NameSite { &name, scope, role });
    }

    void emit(std::optional<vhdl::Name>& name, const ScopeId scope,
        const NameRole role)
    {
        if (name) {
            emit(*name, scope, role);
        }
    }

    // ---- expressions ----

    void expression(const std::optional<ExpressionId>& id)
    {
        if (id) {
            expression(*id);
        }
    }

    // `skip_own_name`: the node's own Name is context-resolved (see callers).
    void expression(const ExpressionId id, const bool skip_own_name = false)
    {
        auto* const node = expressions_.find(id);
        if (node == nullptr || !seen_expressions_.insert(id.value()).second) {
            return;
        }
        if (!skip_own_name) {
            own_name(*node);
        }
        // The type mark of an external name's subtype indication (8.7), as
        // written; set by analysis, a type mark is resolved by visibility.
        if (node->qualified_type_mark
            && node->marker != support::ExpressionMarker::vhdl_qualified) {
            emit(*node->qualified_type_mark, node->scope, NameRole::ordinary);
        }
        children(*node);
    }

    // The operands and associations of `node` (shared with its unfolded
    // call, whose own Names are const and kept as they are).
    void children(const vhdl::Expression& node)
    {
        for (std::size_t index { }; index < node.operands.size(); ++index) {
            // Operand 0 of an external name (marker vhdl_external) is the
            // path of the object (`.top.u1.sig`, `^.x`, `@lib.pkg.x`),
            // resolved at elaboration (8.7), not by visibility; only the
            // index expressions below it are visited.
            const bool path = node.marker == support::ExpressionMarker::vhdl_external
                && index == 0U;
            expression(node.operands[index], path);
        }
        for (const auto& association : node.associations) {
            // The choices of a group of individually associated formal
            // subelements (marker vhdl_formal_subelements, 6.5.7.1) are
            // formal designators: context-resolved. Only the actuals follow.
            if (node.marker != support::ExpressionMarker::vhdl_formal_subelements) {
                for (const auto choice : association.choices) {
                    // A choice that is a plain name (kind name, no operand)
                    // may be a record element (AggregateAssociation::
                    // choice_spelling, 9.3.3.3) or a constant of an array
                    // aggregate; the HIR does not tell them apart without
                    // the aggregate's type, so it is context-resolved.
                    // Every other choice (range, call, expression) is an
                    // expression whose names are visited.
                    const auto* const node_choice = expressions_.find(choice);
                    const bool plain_name = node_choice != nullptr
                        && node_choice->kind == vhdl::ExpressionKind::name
                        && node_choice->operands.empty();
                    expression(choice, plain_name);
                }
            }
            expression(association.value);
        }
        if (node.unfolded_call) {
            // Its operands are records of the shared expression table; its
            // own referenced_name is const (a copy for VHPI, 19.8) and is
            // not a site.
            children(*node.unfolded_call);
        }
    }

    // The Name `node` holds as its own: referenced_name, where the node is a
    // name use.
    void own_name(vhdl::Expression& node)
    {
        if (node.marker == support::ExpressionMarker::vhdl_qualified) {
            // A qualified expression T'(e) (9.3.5) names its type mark T:
            // qualified_type_mark when analysis has set it, else the
            // referenced_name the front end made of the mark.
            if (node.qualified_type_mark) {
                emit(*node.qualified_type_mark, node.scope, NameRole::ordinary);
            } else {
                emit(node.referenced_name, node.scope, NameRole::ordinary);
            }
            return;
        }
        // A synthesized node (a record member `.x`, a physical literal's
        // unit, a dereference, a null or new, an others choice, ...) has a
        // marker, and its referenced_name is not a name resolved by
        // visibility: member and unit names are selected by type or by the
        // literal (8.3, 5.2.4), the others choice is a reserved word.
        if (node.marker != support::ExpressionMarker::none) {
            return;
        }
        switch (node.kind) {
        case vhdl::ExpressionKind::name:
            // A simple name or a flat selected name `a.b.c`: the driver
            // stops at an object prefix.
            emit(node.referenced_name, node.scope, NameRole::ordinary);
            break;
        case vhdl::ExpressionKind::call:
            // The callee of `f(x)`. An attribute name `x'attr` is also a
            // call node whose referenced_name is the designator `'attr`
            // (the parser's form of an attribute name: operand 0 is the
            // prefix, operand 1 the optional parameter); the designator is
            // an attribute, not a declaration (16.2): context-resolved.
            if (!attribute_designator(node)) {
                emit(node.referenced_name, node.scope, NameRole::ordinary);
            }
            break;
        default:
            // Operators (unary, binary, concatenation: the predefined
            // operations of a type), character literals (logic_literal),
            // index and slice nodes (their prefix is operand 0, an ordinary
            // name node) and literals carry no visibility-resolved Name of
            // their own.
            break;
        }
    }

    [[nodiscard]] static bool attribute_designator(
        const vhdl::Expression& node) noexcept
    {
        // The front end names an attribute node by its designator with a
        // leading tick (vhdl_parser_expressions.cpp, "'" + designator).
        return !node.text.empty() && node.text.front() == '\'';
    }

    // ---- delays, ranges, subtypes ----

    void delay(const vhdl::Delay& value)
    {
        const auto one = [&](const std::optional<vhdl::DelayValue>& part) {
            if (part) {
                expression(part->expression);
            }
        };
        expression(value.primary.expression);
        one(value.minimum);
        one(value.typical);
        one(value.maximum);
        for (const auto& more : value.additional) {
            delay(more);
        }
    }

    void delay(const std::optional<vhdl::Delay>& value)
    {
        if (value) {
            delay(*value);
        }
    }

    void range(const vhdl::RangeConstraint& value)
    {
        expression(value.left_expression);
        expression(value.right_expression);
    }

    void subtype(vhdl::SubtypeIndication& value, const ScopeId scope)
    {
        emit(value.written_type_mark, scope, NameRole::ordinary);
        emit(value.resolution_function, scope, NameRole::ordinary);
        expression(value.predefined_attribute_dimension);
        for (const auto& constraint : value.constraints) {
            range(constraint);
        }
        for (const auto& element : value.element_constraints) {
            for (const auto& constraint : element.constraints) {
                range(constraint);
            }
        }
    }

    void subtype(std::optional<vhdl::SubtypeIndication>& value,
        const ScopeId scope)
    {
        if (value) {
            subtype(*value, scope);
        }
    }

    // ---- associations ----

    // Association::formal is a formal designator (6.5.7): context-resolved;
    // formal_conversion_calls are calls of the formal conversion function
    // f(s) whose `f` is not a name of the actual's scope (6.5.7.1).
    void association(vhdl::Association& value, const ScopeId scope)
    {
        expression(value.expression);
        subtype(value.type, scope);
    }

    void associations(std::vector<vhdl::Association>& values, const ScopeId scope)
    {
        for (auto& value : values) {
            association(value, scope);
        }
    }

    // ---- declarations and types ----

    void mode_view_elements(std::vector<vhdl::ModeViewElement>& elements,
        const ScopeId scope)
    {
        for (auto& element : elements) {
            // ModeViewElement::element is a record element designator of the
            // viewed subtype (6.5.2): context-resolved.
            emit(element.referenced_view, scope, NameRole::ordinary);
            subtype(element.subtype, scope);
            mode_view_elements(element.elements, scope);
        }
    }

    void type(const TypeId id, const DeclarationId owner, const ScopeId scope)
    {
        auto* const record = types_.find(id);
        if (record == nullptr) {
            return;
        }
        // A type is visited from its own declaration (or, anonymous, from
        // the declaration that holds it); a declaration that only uses a
        // named type of another unit does not walk into it.
        if (record->declaration.valid() && record->declaration != owner) {
            return;
        }
        if (!seen_types_.insert(id.value()).second) {
            return;
        }
        subtype(record->base, scope);
        for (auto& dimension : record->array_dimensions) {
            emit(dimension.index_subtype, scope, NameRole::ordinary);
            if (dimension.constraint) {
                range(*dimension.constraint);
            }
        }
        subtype(record->element_subtype, scope);
        for (auto& element : record->record_elements) {
            subtype(element.subtype, scope);
        }
        subtype(record->designated_subtype, scope);
        for (const auto& unit : record->physical_units) {
            expression(unit.scale);
        }
        if (record->scalar_range) {
            range(*record->scalar_range);
        }
        for (const auto member : record->protected_members) {
            declaration(member);
        }
    }

    void declaration(const std::optional<DeclarationId>& id)
    {
        if (id) {
            declaration(*id);
        }
    }

    void declaration(const DeclarationId id)
    {
        auto* const node = declarations_.find(id);
        if (node == nullptr || !seen_declarations_.insert(id.value()).second) {
            return;
        }
        const auto scope = node->scope;
        subtype(node->subtype, scope);
        subtype(node->default_type, scope);
        expression(node->initializer);
        // VHDL-1987 has no file open kind expression (its file declaration
        // takes the mode `in` or `out`, 4.3.1.4 of 1076-1987): there the
        // front end's stand-in for the mode is not a name of the source.
        if (!legacy_file_modes_) {
            expression(node->file_open_kind);
        }
        // The prefix of an alias target (6.6); the driver resolves the
        // selected parts.
        emit(node->alias_target, scope, NameRole::ordinary);
        if (node->alias_signature) {
            for (auto& parameter : node->alias_signature->parameters) {
                subtype(parameter, scope);
            }
            subtype(node->alias_signature->result, scope);
        }
        if (node->declared_type) {
            type(*node->declared_type, id, scope);
        }
        if (node->callable) {
            emit(node->callable->default_callable, scope, NameRole::ordinary);
            subtype(node->callable->return_type, scope);
            for (const auto formal : node->callable->formals) {
                declaration(formal);
            }
            declaration(node->callable->return_identifier);
        }
        if (node->component) {
            for (const auto generic : node->component->generics) {
                declaration(generic);
            }
            for (const auto port : node->component->ports) {
                declaration(port);
            }
        }
        if (node->package) {
            emit(node->package->template_name, scope, NameRole::ordinary);
            associations(node->package->generic_map, scope);
        }
        if (node->attribute) {
            subtype(node->attribute->subtype, scope);
            // An attribute specification's entity designators denote named
            // entities declared immediately within the same declarative
            // part (7.2): resolved there by context, not by visibility.
            expression(node->attribute->value);
        }
        if (node->group) {
            emit(node->group->template_name, scope, NameRole::ordinary);
            // A group template's entries are entity classes (6.9), not
            // names; a group's constituents are names (6.10).
            if (node->form == vhdl::DeclarationForm::group_instance) {
                for (auto& entry : node->group->entries) {
                    emit(entry, scope, NameRole::ordinary);
                }
            }
        }
        if (node->mode_view) {
            subtype(node->mode_view->record_subtype, scope);
            mode_view_elements(node->mode_view->elements, scope);
            emit(node->mode_view->converse_of, scope, NameRole::ordinary);
        }
        if (node->interface_view) {
            emit(node->interface_view->view, scope, NameRole::ordinary);
            mode_view_elements(node->interface_view->elements, scope);
        }
        for (const auto child : node->children) {
            declaration(child);
        }
        for (const auto child : node->statements) {
            statement(child);
        }
    }

    // ---- statements, processes, instances ----

    void statement(const StatementId id)
    {
        auto* const node = statements_.find(id);
        if (node == nullptr || !seen_statements_.insert(id.value()).second) {
            return;
        }
        const auto scope = node->scope;
        expression(node->target);
        expression(node->value);
        expression(node->condition);
        expression(node->loop_initial);
        expression(node->loop_limit);
        expression(node->guard);
        expression(node->report);
        expression(node->severity);
        for (const auto& sensitivity : node->sensitivities) {
            expression(sensitivity.expression);
        }
        // The called procedure (11.7); the formals of its association list
        // (ProcedureAssociation::formal) are context-resolved.
        if (node->kind == vhdl::StatementKind::procedure_call) {
            emit(node->procedure, scope, NameRole::ordinary);
        }
        for (const auto& argument : node->procedure_arguments) {
            expression(argument.actual);
        }
        delay(node->delay);
        delay(node->rejection_limit);
        delay(node->disconnection_delay);
        for (const auto& element : node->waveform) {
            expression(element.value);
            delay(element.delay);
        }
        for (const auto child : node->statements) {
            statement(child);
        }
        for (const auto child : node->else_statements) {
            statement(child);
        }
        for (const auto& alternative : node->alternatives) {
            for (const auto choice : alternative.choices) {
                expression(choice);
            }
            for (const auto child : alternative.statements) {
                statement(child);
            }
        }
        for (const auto child : node->declarations) {
            declaration(child);
        }
    }

    void process(const ProcessId id)
    {
        auto* const node = processes_.find(id);
        if (node == nullptr || !seen_processes_.insert(id.value()).second) {
            return;
        }
        for (const auto child : node->declarations) {
            declaration(child);
        }
        for (const auto& sensitivity : node->sensitivities) {
            expression(sensitivity.expression);
        }
        for (const auto child : node->statements) {
            statement(child);
        }
    }

    void instance(const InstanceId id)
    {
        auto* const node = instances_.find(id);
        if (node == nullptr || !seen_instances_.insert(id.value()).second) {
            return;
        }
        // The component, entity or configuration an instantiation names
        // (11.7.1); labels and formals are not names resolved here.
        if (!node->target.text().empty()) {
            visit_(NameSite { &node->target, node->scope,
                NameRole::unit_reference, node });
        }
        associations(node->generic_map, node->scope);
        associations(node->port_map, node->scope);
    }

    // ---- generates, disconnections, configurations ----

    void disconnection(vhdl::DisconnectionSpecification& value, const ScopeId scope)
    {
        for (auto& signal : value.signals) {
            emit(signal, scope, NameRole::ordinary);
        }
        emit(value.type_mark, scope, NameRole::ordinary);
        delay(value.delay);
    }

    void generate(vhdl::GenerateRegion& region)
    {
        const auto scope = region.scope;
        declaration(region.declaration);
        expression(region.initial);
        expression(region.condition);
        expression(region.iteration);
        associations(region.generic_map, scope);
        associations(region.port_map, scope);
        for (const auto id : region.declarations) {
            declaration(id);
        }
        for (auto& specification : region.disconnection_specifications) {
            disconnection(specification, scope);
        }
        for (const auto id : region.instances) {
            instance(id);
        }
        for (const auto id : region.processes) {
            process(id);
        }
        for (const auto id : region.concurrent_statements) {
            statement(id);
        }
        for (auto& nested : region.nested) {
            generate(nested);
        }
        for (const auto& alternative : region.alternatives) {
            for (const auto& choice : alternative.choices) {
                expression(choice.left);
                expression(choice.right);
            }
        }
    }

    void binding(vhdl::BindingIndication& value, const ScopeId scope)
    {
        // The architecture of a binding indication is a string: its name is
        // resolved against the entity's architectures (14.4.2), not by
        // visibility.
        emit(value.entity, scope, NameRole::unit_reference);
        emit(value.configuration, scope, NameRole::unit_reference);
        // The actuals of a binding indication's maps are the local generics
        // and ports of the component (7.3.2.2, 7.3.2.3): resolved against
        // the component declaration when the binding is linked, not by
        // visibility at the configuration.
    }

    void component_configuration(
        vhdl::ComponentConfiguration& value, const ScopeId scope)
    {
        // The instantiation labels are strings (7.3.1); the component name
        // of a component configuration is that of the configured block's
        // instances (3.4.2, 7.3.1), resolved with them when linked.
        binding(value.binding, scope);
        if (value.block_configuration) {
            block_configuration(*value.block_configuration, scope);
        }
    }

    void block_configuration(vhdl::BlockConfiguration& value, const ScopeId scope)
    {
        // A block specification names an architecture, block or generate
        // statement of the configured block, and its generate index is
        // read in that statement's region (3.4.2): resolved when linked.
        for (auto& component : value.components) {
            component_configuration(component, scope);
        }
        for (auto& nested : value.blocks) {
            block_configuration(nested, scope);
        }
    }

    const std::function<void(const NameSite&)>& visit_;
    Table<vhdl::Unit, UnitId> units_;
    Table<vhdl::Declaration, DeclarationId> declarations_;
    Table<vhdl::TypeDefinition, TypeId> types_;
    Table<vhdl::Expression, ExpressionId> expressions_;
    Table<vhdl::Statement, StatementId> statements_;
    Table<vhdl::Process, ProcessId> processes_;
    Table<vhdl::Instance, InstanceId> instances_;
    std::unordered_set<std::uint32_t> seen_declarations_;
    std::unordered_set<std::uint32_t> seen_types_;
    std::unordered_set<std::uint32_t> seen_expressions_;
    std::unordered_set<std::uint32_t> seen_statements_;
    std::unordered_set<std::uint32_t> seen_processes_;
    std::unordered_set<std::uint32_t> seen_instances_;
};

// The symbol of one part of a selected name.
support::SymbolId part_symbol(const std::string_view part)
{
    // An operator symbol part is written quoted (`P."+"`) but a function
    // that overloads it is declared under the symbol without quotes
    // (Declaration::name, 4.2.1): the symbol of the unquoted text.
    if (part.size() >= 2U && part.front() == '"' && part.back() == '"') {
        return support::vhdl_symbol(part.substr(1U, part.size() - 2U));
    }
    return support::vhdl_symbol(part);
}

} // namespace

void visit_names(vhdl::Hir& hir, const vhdl::Unit& unit,
    const std::function<void(const NameSite&)>& visit)
{
    Walker walker { hir, visit };
    walker.walk(unit);
}

std::vector<support::SymbolId> selected_parts(const vhdl::Name& name)
{
    // Name::canonical holds an extended identifier decoded (without its
    // backslashes), which vhdl_symbol cannot tell from a basic identifier;
    // the spelling keeps it as written (15.4.3), and vhdl_symbol folds the
    // basic parts of it.
    const auto text = name.spelling.find('\\') != std::string::npos
        ? std::string_view { name.spelling }
        : name.text();
    const auto size = text.size();
    std::vector<support::SymbolId> parts;
    std::size_t start { };
    std::size_t index { };
    const auto finish = [&](const std::size_t end) {
        if (end == start) {
            return false;
        }
        const auto part = text.substr(start, end - start);
        // An operator symbol must not be empty (`""`).
        if (part == "\"\"") {
            return false;
        }
        parts.push_back(part_symbol(part));
        return true;
    };
    // After an extended identifier, a string or a character literal the part
    // ends: a separator or the end of the text follows.
    const auto part_ends = [&](const std::size_t at) {
        return at == size || text[at] == '.';
    };
    while (index < size) {
        const auto character = text[index];
        if (character == '\\' || character == '"') {
            // An extended identifier (15.4.3) or an operator symbol (15.7):
            // a doubled delimiter is part of it.
            std::size_t cursor = index + 1U;
            bool closed { };
            while (cursor < size) {
                if (text[cursor] == character) {
                    if (cursor + 1U < size && text[cursor + 1U] == character) {
                        cursor += 2U;
                        continue;
                    }
                    closed = true;
                    break;
                }
                ++cursor;
            }
            if (index != start || !closed || !part_ends(cursor + 1U)) {
                return { };
            }
            index = cursor + 1U;
        } else if (character == '\'') {
            // A character literal (15.6) is a whole part.
            if (index != start || index + 2U >= size
                || text[index + 2U] != '\'' || !part_ends(index + 3U)) {
                return { };
            }
            index += 3U;
        } else if (character == '.') {
            if (!finish(index)) {
                return { };
            }
            ++index;
            start = index;
        } else {
            ++index;
        }
    }
    if (!finish(size)) {
        return { };
    }
    return parts;
}

} // namespace fsim::semantic::vhdl_names
