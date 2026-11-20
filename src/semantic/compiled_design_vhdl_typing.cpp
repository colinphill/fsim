// SPDX-License-Identifier: Apache-2.0
// Compile-time VHDL expression typing (gate BD build D4): the overload
// resolution of every complete context (IEEE 1076-2019 12.5) over the
// operand and operator rules of 9.3 and 9.2, on the names that name analysis
// resolved (compiled_design_vhdl_analysis*.cpp).
//
// Each node first gets every interpretation its operands allow (bottom up:
// a type and what the node is, with the type each operand must then have);
// each complete context then selects the one interpretation its own
// context allows (top down) and stores it. An interpretation's cost counts
// the implicit conversions of universal operands it needs (9.3.6): one is
// legal only when no interpretation needs fewer.
#include "compiled_design_vhdl_typing_internal.hpp"

#include "fsim/project/project.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/expression_marker.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/symbol.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::semantic::vhdl_names {
namespace {

using vhdl::Expression;
using vhdl::ExpressionKind;
using vhdl::Interpretation;
using Marker = support::ExpressionMarker;

/// The type of a literal, aggregate, null or allocator, which only its
/// context decides (9.3.2, 9.3.3, 9.3.7).
enum class Pseudo : std::uint8_t { none, string, aggregate, null_value, allocator };

/// One interpretation of a node.
struct Meaning {
    Type type;
    Pseudo pseudo { Pseudo::none };
    /// allocator: the designated base type.
    Type designated;
    Interpretation kind { Interpretation::none };
    DeclarationId declaration;
    support::StandardDeclaration standard { support::StandardDeclaration::none };
    vhdl::PredefinedOperation operation { vhdl::PredefinedOperation::none };
    std::optional<vhdl::PredefinedAttribute> attribute;
    /// The type each operand must have (none: the operand is typed without
    /// a context, or not at all), parallel to the node's operands.
    std::vector<Type> operands;
    /// The position of the formal each operand is associated with in the
    /// callee's formal list (calls; vhdl::no_formal for none).
    std::vector<std::uint32_t> formals;
    /// The implicit conversions this interpretation and its operands need.
    unsigned cost { };
    /// The node denotes a type or subtype (a type mark), a signal.
    bool type_mark { };
    bool signal { };
    /// The node denotes a range (an attribute 'RANGE, `L to R`).
    bool range { };
    /// A convertible universal operand (9.3.6): a numeric literal, an
    /// attribute, or the division of two values of one physical type; only
    /// such an operand is implicitly converted.
    bool convertible { };
    /// A parameter or index type this pass cannot determine: the node is
    /// not typed (census), rather than letting any operand fit.
    bool open { };
};

struct Interpretations {
    std::vector<Meaning> list;
    /// The node's form is not typed by this pass (yet): no error.
    bool unmodeled { };
};

/// How well an interpretation of type `have` fits a context of type
/// `want`: 0 not at all, 1 through the implicit conversion of a universal
/// operand (9.3.6), 2 exactly.
[[nodiscard]] int fit_types(const Types& types, const Type& have, const Type& want)
{
    if (!have || !want) {
        return 0;
    }
    if (have == want) {
        return 2;
    }
    if (have.kind == Type::Kind::universal_integer
        && types.class_of(want) == TypeClass::integer) {
        return 1;
    }
    if (have.kind == Type::Kind::universal_real
        && types.class_of(want) == TypeClass::floating) {
        return 1;
    }
    return 0;
}

/// The reserved words of a range's direction (5.2.1), interned once.
[[nodiscard]] support::SymbolId to_symbol()
{
    static const auto symbol = support::vhdl_symbol("to");
    return symbol;
}

[[nodiscard]] support::SymbolId downto_symbol()
{
    static const auto symbol = support::vhdl_symbol("downto");
    return symbol;
}

class Typing {
public:
    Typing(CompiledDesign& design, const Regions& regions, const Lookup& lookup,
        diagnostic::Engine& diagnostics)
        : design_ { design }
        , regions_ { regions }
        , lookup_ { lookup }
        , diagnostics_ { diagnostics }
        , types_ { regions }
        , operations_ { types_ }
    {
        auto& expressions = design_.vhdl_hir.mutable_expressions();
        for (std::size_t index { }; index < expressions.size(); ++index) {
            expression_index_.emplace(expressions[index].id.value(), index);
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            if (declaration.callable && declaration.nested_scope) {
                callables_.emplace(declaration.nested_scope->value(), declaration.id);
            }
        }
        for (const auto& type : design_.vhdl_hir.types()) {
            for (const auto& unit : type.physical_units) {
                physical_units_.emplace(unit.declaration.value(), type.id);
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            scope_declarations_[declaration.scope.value()].push_back(declaration.id);
        }
    }

    bool run(const std::span<const UnitId> units)
    {
        for (const auto unit : units) {
            if (unit.valid()) {
                units_.insert(unit.value());
            }
        }
        // Parameters first (10.10, 11.8): a parameter's type is that of
        // its range, which the statements of its region read. Generate
        // regions, then loops from the outermost in.
        for (const auto& unit : design_.vhdl_hir.units()) {
            if (unit.id.valid() && units_.contains(unit.id.value())) {
                for (const auto& region : unit.generates) {
                    type_generate(region);
                }
            }
        }
        std::vector<std::pair<std::size_t, const vhdl::Statement*>> loops;
        for (const auto& statement : design_.vhdl_hir.statements()) {
            if (statement.kind == vhdl::StatementKind::loop && statement.loop_initial
                && statement.nested_scope && !statement.loop_variable.empty()
                && owned(statement.scope)) {
                loops.emplace_back(depth_of(*statement.nested_scope), &statement);
            }
        }
        std::ranges::stable_sort(loops, { }, &std::pair<std::size_t, const vhdl::Statement*>::first);
        for (const auto& [depth, statement] : loops) {
            (void)depth;
            const auto type = select_bounds(*statement->loop_initial, statement->loop_limit);
            if (!type) {
                continue;
            }
            const auto parameter = support::vhdl_symbol(statement->loop_variable);
            if (const auto found = scope_declarations_.find(statement->nested_scope->value());
                found != scope_declarations_.end()) {
                for (const auto id : found->second) {
                    const auto* candidate = declaration(id);
                    if (candidate != nullptr && support::vhdl_symbol(candidate->name) == parameter) {
                        parameter_types_[id.value()] = type;
                    }
                }
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            if (owned(declaration.scope)) {
                type_declaration(declaration);
            }
        }
        for (const auto& type : design_.vhdl_hir.types()) {
            const auto* owner = design_.vhdl_hir.find_declaration(type.declaration);
            if (owner != nullptr && owned(owner->scope)) {
                type_definition(type, owner->scope);
            }
        }
        for (const auto& statement : design_.vhdl_hir.statements()) {
            if (owned(statement.scope)) {
                type_statement(statement);
            }
        }
        for (const auto& instance : design_.vhdl_hir.instances()) {
            if (owned(instance.scope)) {
                type_instance(instance);
            }
        }
        census();
        return !failed_;
    }

private:
    // ---- access ----------------------------------------------------------

    [[nodiscard]] bool owned(const ScopeId scope) const
    {
        const auto* unit = regions_.unit_of(scope);
        return unit != nullptr && unit->id.valid() && units_.contains(unit->id.value());
    }

    [[nodiscard]] Expression* node(const ExpressionId id)
    {
        const auto found = expression_index_.find(id.value());
        return found == expression_index_.end()
            ? nullptr
            : &design_.vhdl_hir.mutable_expressions()[found->second];
    }

    [[nodiscard]] const vhdl::Declaration* declaration(const DeclarationId id) const
    {
        return design_.vhdl_hir.find_declaration(id);
    }

    [[nodiscard]] std::uint16_t year(const ScopeId scope) const
    {
        return edition_of(regions_, scope);
    }

    [[nodiscard]] Type standard_type(const support::StandardDeclaration row) const
    {
        return Types::standard(row);
    }

    // ---- diagnostics -----------------------------------------------------

    void error(const char* code, const std::string& message, const SourceSpanId source,
        std::vector<std::string> notes = { })
    {
        if (quiet_ != 0U) {
            ++quiet_errors_;
            return;
        }
        failed_ = true;
        diagnostic::Diagnostic report;
        report.severity = diagnostic::Severity::error;
        report.code = code;
        report.message = message;
        if (const auto* const found = find_source_span(design_.semantics, source);
            found != nullptr) {
            report.span.path = found->logical_name;
            report.span.begin = { found->begin.line, found->begin.column, found->begin.offset };
            report.span.end = { found->end.line, found->end.column, found->end.offset };
        }
        for (auto& note : notes) {
            report.notes.push_back({ std::move(note), report.span });
        }
        diagnostics_.report(std::move(report));
    }

    [[nodiscard]] std::string describe_operands(const Expression& expression)
    {
        std::string text;
        for (std::size_t index { }; index < expression.operands.size(); ++index) {
            const auto& found = interpret(expression.operands[index]);
            if (index != 0U) {
                text += ", ";
            }
            std::vector<std::string> names;
            for (const auto& meaning : found.list) {
                std::string name = meaning.pseudo == Pseudo::string ? "a string literal"
                    : meaning.pseudo == Pseudo::aggregate           ? "an aggregate"
                    : meaning.pseudo == Pseudo::null_value          ? "null"
                    : meaning.pseudo == Pseudo::allocator           ? "an allocator"
                                                                    : types_.describe(meaning.type);
                if (std::ranges::find(names, name) == names.end()) {
                    names.push_back(std::move(name));
                }
            }
            if (names.empty()) {
                text += "?";
            }
            for (std::size_t n { }; n < names.size(); ++n) {
                text += (n == 0U ? "" : " | ") + names[n];
            }
        }
        return text;
    }

    [[nodiscard]] std::string describe_meaning(const Meaning& meaning) const
    {
        std::string text;
        if (meaning.declaration.valid()) {
            if (const auto* found = declaration(meaning.declaration)) {
                text += found->name;
            }
        } else if (meaning.standard != support::StandardDeclaration::none) {
            text += "STD.STANDARD";
        } else if (meaning.operation != vhdl::PredefinedOperation::none) {
            text += "predefined";
        }
        text += " (";
        for (std::size_t index { }; index < meaning.operands.size(); ++index) {
            text += (index == 0U ? "" : ", ") + types_.describe(meaning.operands[index]);
        }
        text += ") return " + types_.describe(meaning.type);
        return text;
    }

    [[nodiscard]] static std::string shown(const Expression& expression)
    {
        if (expression.referenced_name && !expression.referenced_name->spelling.empty()) {
            return expression.referenced_name->spelling;
        }
        return expression.text;
    }

    // ---- candidates ------------------------------------------------------

    /// The profiles of every subprogram, enumeration literal, STANDARD
    /// function and implicit operation `designator` denotes at the node.
    [[nodiscard]] std::vector<Profile> overloads(const support::SymbolId designator,
        const ScopeId scope, const SourceSpanId source, const vhdl::Name* name)
    {
        std::vector<Profile> result;
        const auto edition = year(scope);
        const auto add = [&](Profile profile) {
            // An explicit declaration hides an implicit predefined operation
            // it is a homograph of; the innermost of homographs hides the
            // others (12.3 b, the order Lookup gives).
            // A subprogram body and its declaration are one subprogram
            // (4.2.1, 4.3); D3 rejects other directly visible homographs.
            for (const auto& existing : result) {
                if (existing.parameters == profile.parameters
                    && existing.result == profile.result
                    && (existing.source != Profile::Source::implicit
                        || profile.source == Profile::Source::implicit)) {
                    return;
                }
            }
            result.push_back(std::move(profile));
        };
        // A subprogram or enumeration literal, or an alias of one, which
        // denotes it (6.6.3).
        const auto overloadable = [](const vhdl::Declaration* found) {
            return found != nullptr
                && (found->callable || found->form == vhdl::DeclarationForm::enumeration_literal);
        };
        const auto add_declaration = [&](const vhdl::Declaration* found) {
            if (found != nullptr && found->form == vhdl::DeclarationForm::alias
                && !found->subtype && found->alias_target) {
                // 6.6.3: the signature, if any, selects the one subprogram or
                // literal whose parameter and result type profile it matches.
                std::vector<DeclarationId> targets = found->alias_target->overloads;
                if (found->alias_target->selected
                    && std::ranges::find(targets, *found->alias_target->selected) == targets.end()) {
                    targets.push_back(*found->alias_target->selected);
                }
                for (const auto target_id : targets) {
                    const auto* target = declaration(target_id);
                    if (!overloadable(target)) {
                        continue;
                    }
                    auto profile = operations_.of_declaration(*target);
                    if (found->alias_signature) {
                        const auto& signature = *found->alias_signature;
                        if (signature.parameters.size() != profile.parameters.size()) {
                            continue;
                        }
                        bool same = true;
                        for (std::size_t index { }; index < signature.parameters.size(); ++index) {
                            same = same
                                && types_.of_subtype(signature.parameters[index])
                                    == profile.parameters[index];
                        }
                        if (signature.result) {
                            same = same && types_.of_subtype(*signature.result) == profile.result;
                        }
                        if (!same) {
                            continue;
                        }
                    }
                    add(std::move(profile));
                }
                return;
            }
            if (overloadable(found)) {
                add(operations_.of_declaration(*found));
            }
        };
        // An expanded name (`P.F`, `work.P.F`): what name analysis stored.
        if (name != nullptr && selected_parts(*name).size() > 1U) {
            for (const auto id : name->overloads) {
                add_declaration(declaration(id));
            }
            if (name->selected && name->overloads.empty()) {
                add_declaration(declaration(*name->selected));
            }
            return result;
        }
        const auto resolution = lookup_.visible(designator, scope, regions_.begin(source));
        for (const auto id : resolution.candidates) {
            add_declaration(declaration(id));
        }
        for (const auto& entry : resolution.standard) {
            if (entry.kind == StandardEntry::Kind::function) {
                add(operations_.of_standard(entry.declaration));
            } else if (entry.kind == StandardEntry::Kind::literal
                || entry.kind == StandardEntry::Kind::unit) {
                Profile literal;
                literal.source = Profile::Source::standard;
                literal.standard = entry.declaration;
                literal.result = types_.of_standard(entry.declaration);
                add(std::move(literal));
            }
        }
        for (const auto id : resolution.implicit) {
            if (const auto* type = declaration(id)) {
                for (auto& profile : operations_.implicit(*type, designator, edition)) {
                    add(std::move(profile));
                }
            }
        }
        return result;
    }

    /// The enumeration types having character literal `symbol` visible at
    /// the node (5.2.2.1, 12.3): declared literals by visibility, and those
    /// of STANDARD's BIT and CHARACTER, whose literals are not names.
    [[nodiscard]] std::vector<Meaning> character_literal(const Expression& expression)
    {
        std::vector<Meaning> result;
        const auto symbol = support::vhdl_symbol(expression.text);
        const auto resolution = lookup_.visible(symbol, expression.scope,
            regions_.begin(expression.source));
        for (const auto id : resolution.candidates) {
            const auto* found = declaration(id);
            if (found == nullptr || found->form != vhdl::DeclarationForm::enumeration_literal) {
                continue;
            }
            Meaning meaning;
            meaning.type = types_.of_literal(*found);
            meaning.kind = Interpretation::enumeration_literal;
            meaning.declaration = id;
            if (meaning.type && std::ranges::none_of(result,
                    [&](const Meaning& other) { return other.type == meaning.type; })) {
                result.push_back(std::move(meaning));
            }
        }
        const auto edition = year(expression.scope);
        for (const auto row : { support::StandardDeclaration::bit,
                 support::StandardDeclaration::character }) {
            const auto type = standard_type(row);
            if (types_.has_literal(type, symbol, edition)
                && std::ranges::none_of(result,
                    [&](const Meaning& other) { return other.type == type; })) {
                Meaning meaning;
                meaning.type = type;
                meaning.kind = Interpretation::enumeration_literal;
                meaning.standard = row;
                result.push_back(std::move(meaning));
            }
        }
        return result;
    }

    // ---- profile application ----------------------------------------------

    /// The cost of operand `operand` having type `want` (its cheapest
    /// interpretation of that type); nullopt when none fits.
    [[nodiscard]] std::optional<unsigned> operand_cost(const ExpressionId operand, const Type& want)
    {
        const auto& found = interpret(operand);
        if (found.unmodeled) {
            return 0U; // not typed here: no constraint (census)
        }
        if (!want) {
            ++open_operands_; // the operand's type is not known here (census)
            return 0U;
        }
        std::optional<unsigned> best;
        for (const auto& meaning : found.list) {
            const auto fit = fits(meaning, want);
            if (fit == 0) {
                continue;
            }
            const auto cost = meaning.cost + (fit == 1 ? 1U : 0U);
            if (!best || cost < *best) {
                best = cost;
            }
        }
        return best;
    }

    /// How well `meaning` fits a context of type `want` (see fit_types);
    /// a pseudo type fits every type of its kind (9.3.2, 9.3.3, 9.3.7).
    [[nodiscard]] int fits(const Meaning& meaning, const Type& want) const
    {
        if (!want) {
            return 0;
        }
        switch (meaning.pseudo) {
        case Pseudo::none: {
            const auto fit = fit_types(types_, meaning.type, want);
            return fit == 1 && !meaning.convertible ? 0 : fit;
        }
        case Pseudo::string:
            return types_.string_type(want) ? 2 : 0;
        case Pseudo::aggregate: {
            const auto type_class = types_.class_of(want);
            return type_class == TypeClass::array || type_class == TypeClass::record ? 2 : 0;
        }
        case Pseudo::null_value:
            return types_.class_of(want) == TypeClass::access ? 2 : 0;
        case Pseudo::allocator:
            return types_.class_of(want) == TypeClass::access
                    && types_.designated(want) == meaning.designated
                ? 2
                : 0;
        }
        return 0;
    }

    /// Applies `profile` to the actuals `operands` with the formal
    /// designators `names` (empty: positional; 6.5.7.1, 4.2.2.1); the
    /// interpretation, or nullopt when the actuals do not fit.
    [[nodiscard]] std::optional<Meaning> apply(const Profile& profile,
        const std::vector<ExpressionId>& operands, const std::vector<std::string>& names,
        const std::size_t first)
    {
        if (profile.source == Profile::Source::declaration) {
            if (const auto* found = declaration(profile.declaration);
                found != nullptr && generic_subprogram(*found)) {
                // A generic subprogram or an instance of one (4.2.1, 4.4):
                // its profile depends on the instantiation, re-analyzed in
                // gate BD2 (V4); the call is census-counted, not typed.
                Meaning meaning;
                meaning.type = profile.result;
                meaning.open = true;
                meaning.kind = Interpretation::function_call;
                meaning.declaration = profile.declaration;
                meaning.operands.resize(operands.size());
                meaning.formals.resize(operands.size(), vhdl::no_formal);
                return meaning;
            }
        }
        const auto count = operands.size() - first;
        std::vector<std::optional<std::size_t>> formal_of(count);
        std::vector<bool> taken(profile.parameters.size());
        bool named = false;
        for (std::size_t index { }; index < count; ++index) {
            const auto position = index + first;
            const bool is_named = position < names.size() && !names[position].empty();
            if (is_named) {
                named = true;
                const auto symbol = support::vhdl_symbol(names[position]);
                std::optional<std::size_t> match;
                for (std::size_t formal { }; formal < profile.formal_names.size(); ++formal) {
                    if (profile.formal_names[formal] == symbol) {
                        match = formal;
                    }
                }
                if (!match || taken[*match]) {
                    return std::nullopt;
                }
                formal_of[index] = match;
                taken[*match] = true;
            } else {
                if (named || index >= profile.parameters.size()) {
                    return std::nullopt; // positional after named (6.5.7.1)
                }
                formal_of[index] = index;
                taken[index] = true;
            }
        }
        for (std::size_t formal { }; formal < profile.parameters.size(); ++formal) {
            if (!taken[formal]
                && (formal >= profile.defaults.size() || !profile.defaults[formal])) {
                return std::nullopt;
            }
        }
        Meaning meaning;
        meaning.type = profile.result;
        meaning.open = profile.result && types_.class_of(profile.result) == TypeClass::none;
        meaning.operands.resize(operands.size());
        meaning.formals.resize(operands.size(), vhdl::no_formal);
        for (std::size_t index { }; index < count; ++index) {
            const auto formal = *formal_of[index];
            if (!profile.parameters[formal]
                || types_.class_of(profile.parameters[formal]) == TypeClass::none) {
                meaning.open = true; // an interface type (6.5.3): BD2 (V4)
            }
            const auto cost = operand_cost(operands[index + first], profile.parameters[formal]);
            if (!cost) {
                return std::nullopt;
            }
            meaning.cost += *cost;
            meaning.operands[index + first] = profile.parameters[formal];
            if (profile.source != Profile::Source::implicit) {
                meaning.formals[index + first] = static_cast<std::uint32_t>(formal);
            }
        }
        switch (profile.source) {
        case Profile::Source::declaration: {
            const auto* found = declaration(profile.declaration);
            meaning.kind = found != nullptr
                    && found->form == vhdl::DeclarationForm::enumeration_literal
                ? Interpretation::enumeration_literal
                : Interpretation::function_call;
            meaning.declaration = profile.declaration;
            break;
        }
        case Profile::Source::standard:
            meaning.kind = !profile.parameters.empty() || !types_.of_standard(profile.standard)
                ? Interpretation::function_call
                : types_.class_of(profile.result) == TypeClass::physical
                ? Interpretation::physical_literal
                : Interpretation::enumeration_literal;
            meaning.standard = profile.standard;
            break;
        case Profile::Source::implicit:
            meaning.kind = Interpretation::operation;
            meaning.declaration = profile.declaration;
            meaning.operation = profile.operation;
            // 9.3.6: P / P of one physical type is a convertible operand.
            break;
        }
        // 9.3.6: P / P of one physical type (universal_integer) is a
        // convertible universal operand; no other predefined operation has
        // that profile.
        meaning.convertible = profile.source != Profile::Source::declaration
            && profile.result.kind == Type::Kind::universal_integer
            && profile.parameters.size() == 2U && profile.parameters[0] == profile.parameters[1]
            && types_.class_of(profile.parameters[0]) == TypeClass::physical;
        return meaning;
    }

    // ---- bottom-up interpretation -------------------------------------------

    const Interpretations& interpret(const ExpressionId id)
    {
        if (const auto found = interpretations_.find(id.value());
            found != interpretations_.end()) {
            return found->second;
        }
        // A node is interpreted once; a cycle (impossible in a tree) would
        // see the placeholder.
        interpretations_.emplace(id.value(), Interpretations { { }, true });
        auto computed = compute(id);
        auto& slot = interpretations_[id.value()];
        slot = std::move(computed);
        return slot;
    }

    Interpretations compute(const ExpressionId id)
    {
        Interpretations result;
        const auto* expression = node(id);
        if (expression == nullptr) {
            result.unmodeled = true;
            return result;
        }
        const auto& e = *expression;
        if (e.user_attribute_value) {
            result.unmodeled = true;
            return result;
        }
        switch (e.marker) {
        case Marker::vhdl_physical:
            return physical(e);
        case Marker::vhdl_qualified:
            return qualified(e);
        case Marker::vhdl_null: {
            Meaning meaning;
            meaning.pseudo = Pseudo::null_value;
            meaning.kind = Interpretation::null_value;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case Marker::vhdl_member:
            return member(e);
        case Marker::vhdl_dereference:
            return dereference(e);
        case Marker::vhdl_new:
        case Marker::vhdl_new_qualified:
            return allocator(e);
        case Marker::none:
            break;
        default:
            result.unmodeled = true;
            return result;
        }
        switch (e.kind) {
        case ExpressionKind::integer_literal: {
            Meaning meaning;
            meaning.type = Types::universal_integer();
            meaning.kind = Interpretation::literal;
            meaning.convertible = true;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case ExpressionKind::real_literal: {
            Meaning meaning;
            meaning.type = Types::universal_real();
            meaning.kind = Interpretation::literal;
            meaning.convertible = true;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case ExpressionKind::boolean_literal: {
            Meaning meaning;
            meaning.type = standard_type(support::StandardDeclaration::boolean);
            meaning.kind = Interpretation::enumeration_literal;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case ExpressionKind::logic_literal:
            result.list = character_literal(e);
            return result;
        case ExpressionKind::string_literal: {
            Meaning meaning;
            meaning.pseudo = Pseudo::string;
            meaning.kind = Interpretation::literal;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case ExpressionKind::aggregate: {
            Meaning meaning;
            meaning.pseudo = Pseudo::aggregate;
            meaning.kind = Interpretation::aggregate;
            result.list.push_back(std::move(meaning));
            return result;
        }
        case ExpressionKind::name:
            return name(e);
        case ExpressionKind::unary:
            return operation(e, support::vhdl_symbol(e.text), 0U);
        case ExpressionKind::binary:
            if (range_node(e)) {
                return range(e);
            }
            return operation(e, support::vhdl_symbol(e.text), 0U);
        case ExpressionKind::call:
            return call(e);
        case ExpressionKind::index:
            return index(e);
        case ExpressionKind::slice:
            return slice(e);
        case ExpressionKind::conditional:
            return conditional(e);
        default:
            result.unmodeled = true;
            return result;
        }
    }

    /// What a declaration denotes as a primary (an object, a type mark).
    [[nodiscard]] std::optional<Meaning> denoted(const vhdl::Declaration& found, const ScopeId scope)
    {
        Meaning meaning;
        meaning.declaration = found.id;
        using Form = vhdl::DeclarationForm;
        switch (found.form) {
        case Form::type:
        case Form::subtype:
            meaning.type = types_.of_type_declaration(found);
            meaning.kind = Interpretation::type_mark;
            meaning.type_mark = true;
            return meaning.type ? std::optional { meaning } : std::nullopt;
        case Form::alias:
            if (!found.subtype && found.alias_target && found.alias_target->selected) {
                if (const auto* target = declaration(*found.alias_target->selected)) {
                    if (target->form == Form::type || target->form == Form::subtype) {
                        return denoted(*target, scope);
                    }
                }
            }
            [[fallthrough]];
        case Form::port:
        case Form::signal:
        case Form::constant:
        case Form::variable:
        case Form::file:
        case Form::generic_constant: {
            // A loop parameter has the type of its range (10.10).
            if (const auto parameter = parameter_types_.find(found.id.value());
                parameter != parameter_types_.end()) {
                meaning.type = parameter->second;
            } else {
                meaning.type = types_.of_object(found);
            }
            meaning.kind = Interpretation::object;
            meaning.signal = found.form == Form::signal || found.form == Form::port
                || found.object_class == vhdl::ObjectClass::signal;
            if (!meaning.type) {
                return std::nullopt;
            }
            return meaning;
        }
        default:
            (void)scope;
            return std::nullopt;
        }
    }

    Interpretations name(const Expression& e)
    {
        Interpretations result;
        if (!e.referenced_name) {
            result.unmodeled = true;
            return result;
        }
        const auto& name = *e.referenced_name;
        const auto parts = selected_parts(name);
        if (parts.empty()) {
            result.unmodeled = true;
            return result;
        }
        // A STANDARD type or subtype mark.
        if (name.standard != support::StandardDeclaration::none) {
            Meaning meaning;
            meaning.type = types_.of_standard(name.standard);
            meaning.kind = Interpretation::type_mark;
            meaning.type_mark = true;
            meaning.standard = name.standard;
            if (meaning.type) {
                result.list.push_back(std::move(meaning));
                return result;
            }
            result.unmodeled = true;
            return result;
        }
        // A member of a package instance or a generic subprogram (4.9,
        // 4.4): typed with the instance in BD2 (V4).
        if (generic_named(parts.back(), e.scope, e.source, name)) {
            result.unmodeled = true;
            return result;
        }
        // A selected name whose prefix denotes an object: the suffixes
        // select record elements (8.3).
        if (parts.size() > 1U && name.selected) {
            if (const auto* found = declaration(*name.selected);
                found != nullptr && found->form != vhdl::DeclarationForm::function
                && !found->callable) {
                if (auto object = denoted(*found, e.scope)) {
                    // How many parts name analysis consumed: those up to the
                    // object's designator (the first part matching it).
                    std::size_t consumed = parts.size();
                    const auto designator = support::vhdl_symbol(
                        !found->spelling.empty() && support::is_extended_vhdl_identifier(found->spelling)
                            ? std::string_view { found->spelling }
                            : std::string_view { found->name });
                    for (std::size_t index { }; index < parts.size(); ++index) {
                        if (parts[index] == designator) {
                            consumed = index + 1U;
                            break;
                        }
                    }
                    if (object->type_mark) {
                        result.list.push_back(std::move(*object));
                        return result;
                    }
                    auto type = object->type;
                    for (std::size_t index = consumed; index < parts.size() && type; ++index) {
                        if (types_.class_of(type) == TypeClass::access) {
                            type = types_.designated(type); // implicit dereference (8.3)
                        }
                        type = types_.record_element(type, parts[index]);
                    }
                    if (!type) {
                        result.unmodeled = true;
                        return result;
                    }
                    object->type = type;
                    object->kind = consumed == parts.size() ? Interpretation::object
                                                            : Interpretation::member;
                    result.list.push_back(std::move(*object));
                    return result;
                }
            }
        }
        // A non-overloadable declaration.
        if (name.selected && name.overloads.size() <= 1U) {
            if (const auto* found = declaration(*name.selected)) {
                if (!found->callable && found->form != vhdl::DeclarationForm::enumeration_literal) {
                    if (auto meaning = denoted(*found, e.scope)) {
                        result.list.push_back(std::move(*meaning));
                        return result;
                    }
                    result.unmodeled = true;
                    return result;
                }
            }
        }
        // Overloadable: every function called without actuals and every
        // enumeration literal (4.2.2.1, 5.2.2.1).
        const auto designator = parts.back();
        for (const auto& profile : overloads(designator, e.scope, e.source, &name)) {
            if (auto meaning = apply(profile, { }, { }, 0U); meaning && meaning->type) {
                result.list.push_back(std::move(*meaning));
            }
        }
        if (result.list.empty()) {
            // A generate parameter or a name the pass has no model for.
            if (const auto parameter = generate_parameter(designator, e.scope)) {
                Meaning meaning;
                meaning.type = *parameter;
                meaning.kind = Interpretation::object;
                result.list.push_back(std::move(meaning));
                return result;
            }
            result.unmodeled = true;
        }
        return result;
    }

    [[nodiscard]] std::size_t depth_of(ScopeId scope) const
    {
        std::size_t depth { };
        for (; depth < 4096U; ++depth) {
            const auto* region = regions_.find(scope);
            if (region == nullptr || !region->enclosing) {
                break;
            }
            scope = *region->enclosing;
        }
        return depth;
    }

    [[nodiscard]] std::optional<Type> generate_parameter(const support::SymbolId designator,
        ScopeId scope) const
    {
        for (std::size_t depth { }; depth < 256U; ++depth) {
            if (const auto found = generate_parameters_.find(scope.value());
                found != generate_parameters_.end() && found->second.first == designator) {
                return found->second.second;
            }
            const auto* region = regions_.find(scope);
            if (region == nullptr || !region->enclosing) {
                return std::nullopt;
            }
            scope = *region->enclosing;
        }
        return std::nullopt;
    }

    /// A generic subprogram or an instance of one (4.2.1, 4.4): its formal
    /// types depend on the instantiation, re-analyzed in gate BD2 (V4).
    [[nodiscard]] static bool generic_subprogram(const vhdl::Declaration& found)
    {
        using Form = vhdl::DeclarationForm;
        return found.form == Form::generic_function_template
            || found.form == Form::generic_procedure_template
            || found.form == Form::generic_function_instance
            || found.form == Form::generic_procedure_instance
            || found.form == Form::generic_function
            || found.form == Form::generic_procedure;
    }

    /// Whether `name` selects a method of a protected-type object (5.6.2):
    /// the prefix denotes an object.
    [[nodiscard]] bool method_name(const vhdl::Name& name) const
    {
        if (!name.selected || selected_parts(name).size() < 2U) {
            return false;
        }
        const auto* found = declaration(*name.selected);
        return found != nullptr && !found->callable
            && found->form != vhdl::DeclarationForm::enumeration_literal
            && found->form != vhdl::DeclarationForm::alias;
    }

    /// Whether `name` denotes members of a package fsim serves without
    /// declarations (Region::undeclared: VITAL, Synopsys; their sources
    /// are analyzed in gate BD2): the call is census-counted, not typed.
    [[nodiscard]] bool undeclared(const support::SymbolId designator,
        const ScopeId scope, const SourceSpanId source, const vhdl::Name& name) const
    {
        if (selected_parts(name).size() > 1U) {
            return !name.selected && name.overloads.empty();
        }
        return lookup_.visible(designator, scope, regions_.begin(source)).undeclared;
    }

    /// Whether `name` denotes a generic subprogram, or an instance of one
    /// (4.2.1, 4.4): its profile is known after instantiation (BD2, V4).
    [[nodiscard]] bool generic_named(const support::SymbolId designator,
        const ScopeId scope, const SourceSpanId source, const vhdl::Name& name) const
    {
        const auto generic = [&](const DeclarationId id) {
            const auto* found = declaration(id);
            if (found == nullptr) {
                return false;
            }
            // A member of a package instance's copy (4.9) is re-analyzed
            // with the instance in BD2 (V4), as D3 leaves it.
            const auto* unit = regions_.unit_of(found->scope);
            return generic_subprogram(*found)
                || (unit != nullptr && unit->package_instance_expansion);
        };
        const auto parts = selected_parts(name);
        if (parts.size() > 1U) {
            // `I.F` through a package instance (4.9) selects F of the
            // instance, whose profile follows the instance's generic map.
            const auto prefix = lookup_.visible(parts.front(), scope, regions_.begin(source));
            const bool instance = std::ranges::any_of(prefix.candidates, [&](const DeclarationId id) {
                const auto* found = declaration(id);
                return found != nullptr && found->form == vhdl::DeclarationForm::package_instance;
            });
            return instance || std::ranges::any_of(name.overloads, generic)
                || (name.selected && generic(*name.selected));
        }
        return std::ranges::any_of(
            lookup_.visible(designator, scope, regions_.begin(source)).candidates, generic);
    }

    /// Whether `e` is a range `L to R` / `L downto R` (5.2.1): the parser
    /// gives it as a binary node of the direction's reserved word.
    [[nodiscard]] static bool range_node(const Expression& e)
    {
        if (e.kind != ExpressionKind::binary || e.operands.size() != 2U) {
            return false;
        }
        if (support::vhdl_case_range(e.marker)) {
            return true;
        }
        if (e.marker != Marker::none) {
            return false;
        }
        const auto symbol = support::vhdl_symbol(e.text);
        return symbol == to_symbol() || symbol == downto_symbol();
    }

    /// A range's interpretations: one per type both bounds can have (5.2.1,
    /// 5.3.2.2); a universal_integer range is also one of INTEGER.
    Interpretations range(const Expression& e)
    {
        Interpretations result;
        if (unmodeled_operand(e, 0U)) {
            result.unmodeled = true;
            return result;
        }
        const auto& lefts = interpret(e.operands[0]);
        const auto& rights = interpret(e.operands[1]);
        for (const auto& type : bound_types(lefts, rights)) {
            Meaning meaning;
            meaning.type = type;
            meaning.kind = Interpretation::literal;
            meaning.range = true;
            // A range of universal bounds converts as its bounds do.
            meaning.convertible = type.kind == Type::Kind::universal_integer
                || type.kind == Type::Kind::universal_real;
            meaning.operands = { type, type };
            meaning.formals.resize(2U, vhdl::no_formal);
            result.list.push_back(std::move(meaning));
        }
        return result;
    }

    /// Whether every operand from `first` on is a discrete range: `L to R`,
    /// a range attribute or a discrete subtype mark (5.3.2.1, 8.5).
    [[nodiscard]] bool discrete_ranges(const Expression& e, const std::size_t first)
    {
        if (e.operands.size() <= first) {
            return false;
        }
        for (std::size_t index = first; index < e.operands.size(); ++index) {
            const auto* operand = node(e.operands[index]);
            if (operand != nullptr && range_node(*operand)) {
                continue;
            }
            const auto& found = interpret(e.operands[index]);
            if (found.unmodeled || found.list.empty()
                || std::ranges::any_of(found.list, [](const Meaning& meaning) {
                       return !meaning.range && !meaning.type_mark;
                   })) {
                return false;
            }
        }
        return true;
    }

    /// Whether an operand from `first` on is not typed by this pass: the
    /// node is then not typed either (no interpretation is decided on an
    /// operand that fits any type).
    [[nodiscard]] bool unmodeled_operand(const Expression& e, const std::size_t first)
    {
        for (std::size_t index = first; index < e.operands.size(); ++index) {
            if (interpret(e.operands[index]).unmodeled) {
                return true;
            }
        }
        return false;
    }

    Interpretations operation(const Expression& e, const support::SymbolId designator,
        const std::size_t first)
    {
        Interpretations result;
        if (unmodeled_operand(e, first)
            || lookup_.visible(designator, e.scope, regions_.begin(e.source)).undeclared) {
            // An operator a package without declarations supplies (the
            // Synopsys packages, BD2) is census-counted, not typed.
            result.unmodeled = true;
            return result;
        }
        for (const auto& profile : overloads(designator, e.scope, e.source, nullptr)) {
            if (profile.parameters.size() != e.operands.size() - first || !profile.result) {
                continue;
            }
            if (auto meaning = apply(profile, e.operands, { }, first)) {
                result.list.push_back(std::move(*meaning));
            }
        }
        return without_open(std::move(result));
    }

    /// A node one of whose interpretations has a parameter of unknown type
    /// is not typed (its operands could fit that parameter).
    [[nodiscard]] static Interpretations without_open(Interpretations result)
    {
        if (std::ranges::any_of(result.list, [](const Meaning& meaning) { return meaning.open; })) {
            result.list.clear();
            result.unmodeled = true;
        }
        return result;
    }

    Interpretations call(const Expression& e)
    {
        Interpretations result;
        // A predefined attribute name: the prefix is operand 0 (16.2).
        if (!e.text.empty() && e.text.front() == '\'') {
            return attribute(e);
        }
        if (e.operator_symbol_call) {
            return operation(e, support::vhdl_symbol(e.text), 0U);
        }
        if (!e.referenced_name) {
            result.unmodeled = true;
            return result;
        }
        const auto& name = *e.referenced_name;
        const auto parts = selected_parts(name);
        if (parts.empty()) {
            result.unmodeled = true;
            return result;
        }
        // A type conversion `T(x)` (9.3.6) or an indexed name of an object.
        if (name.standard != support::StandardDeclaration::none
            || (name.selected && name.overloads.size() <= 1U)) {
            std::optional<Meaning> prefix;
            if (name.standard != support::StandardDeclaration::none) {
                Meaning mark;
                mark.type = types_.of_standard(name.standard);
                mark.type_mark = true;
                prefix = mark;
            } else if (const auto* found = declaration(*name.selected);
                       found != nullptr && !found->callable
                       && found->form != vhdl::DeclarationForm::enumeration_literal) {
                // `rec.field(i)`: the object prefix with its element suffixes.
                Expression as_name = e;
                as_name.kind = ExpressionKind::name;
                as_name.operands.clear();
                auto named = this->name(as_name);
                if (named.list.size() == 1U) {
                    prefix = named.list.front();
                }
            }
            if (prefix && prefix->type_mark && discrete_ranges(e, 0U)) {
                // An index constraint (5.3.2.2): `T(1 to N)` is a subtype.
                if (types_.class_of(prefix->type) != TypeClass::array
                    || types_.dimensions(prefix->type) != e.operands.size()) {
                    result.unmodeled = true;
                    return result;
                }
                Meaning meaning;
                meaning.type = prefix->type;
                meaning.kind = Interpretation::type_mark;
                meaning.type_mark = true;
                meaning.standard = name.standard;
                for (std::size_t dimension { }; dimension < e.operands.size(); ++dimension) {
                    meaning.operands.push_back(index_of(prefix->type, dimension));
                }
                meaning.formals.resize(e.operands.size(), vhdl::no_formal);
                result.list.push_back(std::move(meaning));
                return result;
            }
            if (prefix && prefix->type_mark) {
                if (e.operands.size() != 1U) {
                    result.unmodeled = true;
                    return result;
                }
                Meaning meaning;
                meaning.type = prefix->type;
                meaning.kind = Interpretation::conversion;
                meaning.standard = name.standard;
                meaning.operands.resize(1U); // typed without context (9.3.6)
                meaning.formals.resize(1U, vhdl::no_formal);
                result.list.push_back(std::move(meaning));
                return result;
            }
            if (prefix) {
                auto type = prefix->type;
                if (types_.class_of(type) == TypeClass::access) {
                    type = types_.designated(type);
                }
                if (types_.class_of(type) == TypeClass::array && e.operands.size() == 1U
                    && discrete_ranges(e, 0U)) {
                    // A slice (8.5).
                    const auto want = index_of(type, 0U);
                    const auto cost = operand_cost(e.operands.front(), want);
                    if (!cost) {
                        return result;
                    }
                    Meaning meaning;
                    meaning.type = type;
                    meaning.kind = Interpretation::slice;
                    meaning.declaration = prefix->declaration;
                    meaning.signal = prefix->signal;
                    meaning.cost = *cost;
                    meaning.operands = { want };
                    meaning.formals.resize(1U, vhdl::no_formal);
                    result.list.push_back(std::move(meaning));
                    return result;
                }
                if (types_.class_of(type) == TypeClass::array
                    && types_.dimensions(type) == e.operands.size()) {
                    Meaning meaning;
                    meaning.type = types_.element(type);
                    meaning.kind = Interpretation::index;
                    meaning.declaration = prefix->declaration;
                    meaning.signal = prefix->signal;
                    for (std::size_t dimension { }; dimension < e.operands.size(); ++dimension) {
                        const auto want = index_of(type, dimension);
                        const auto cost = operand_cost(e.operands[dimension], want);
                        if (!cost) {
                            return result;
                        }
                        meaning.cost += *cost;
                        meaning.operands.push_back(want);
                    }
                    meaning.formals.resize(e.operands.size(), vhdl::no_formal);
                    result.list.push_back(std::move(meaning));
                    return result;
                }
                result.unmodeled = true;
                return result;
            }
        }
        // A function call.
        if (method_name(name) || undeclared(parts.back(), e.scope, e.source, name)
            || generic_named(parts.back(), e.scope, e.source, name)
            || unmodeled_operand(e, 0U)) {
            result.unmodeled = true;
            return result;
        }
        for (const auto& profile : overloads(parts.back(), e.scope, e.source, &name)) {
            if (!profile.result) {
                continue;
            }
            if (auto meaning = apply(profile, e.operands, e.argument_names, 0U)) {
                result.list.push_back(std::move(*meaning));
            }
        }
        // A call without actuals whose result is indexed: `f(i)` with an f
        // every formal of which has a default, returning an array (8.4,
        // 4.2.2.1).
        for (const auto& profile : overloads(parts.back(), e.scope, e.source, &name)) {
            if (!profile.result
                || !std::ranges::all_of(profile.defaults, [](const bool d) { return d; })
                || profile.defaults.size() != profile.parameters.size()
                || types_.class_of(profile.result) != TypeClass::array
                || types_.dimensions(profile.result) != e.operands.size()
                || !e.argument_names.empty()) {
                continue;
            }
            Meaning meaning;
            meaning.type = types_.element(profile.result);
            meaning.kind = Interpretation::index;
            meaning.declaration = profile.declaration;
            meaning.standard = profile.standard;
            bool ok = true;
            for (std::size_t dimension { }; dimension < e.operands.size(); ++dimension) {
                const auto want = index_of(profile.result, dimension);
                const auto cost = operand_cost(e.operands[dimension], want);
                if (!cost) {
                    ok = false;
                    break;
                }
                meaning.cost += *cost;
                meaning.operands.push_back(want);
            }
            meaning.formals.resize(e.operands.size(), vhdl::no_formal);
            if (ok) {
                result.list.push_back(std::move(meaning));
            }
        }
        return without_open(std::move(result));
    }

    Interpretations index(const Expression& e)
    {
        Interpretations result;
        if (e.operands.empty()) {
            result.unmodeled = true;
            return result;
        }
        const auto& prefix = interpret(e.operands.front());
        if (prefix.unmodeled) {
            result.unmodeled = true;
            return result;
        }
        for (const auto& candidate : prefix.list) {
            auto type = candidate.type;
            if (types_.class_of(type) == TypeClass::access) {
                type = types_.designated(type);
            }
            if (types_.class_of(type) == TypeClass::array && e.operands.size() == 2U
                && discrete_ranges(e, 1U)) {
                const auto want = index_of(type, 0U);
                const auto cost = operand_cost(e.operands[1], want);
                if (!cost) {
                    continue;
                }
                Meaning meaning;
                meaning.type = type;
                meaning.kind = Interpretation::slice;
                meaning.signal = candidate.signal;
                meaning.operands = { candidate.type, want };
                meaning.cost = candidate.cost + *cost;
                result.list.push_back(std::move(meaning));
                continue;
            }
            if (types_.class_of(type) != TypeClass::array
                || types_.dimensions(type) != e.operands.size() - 1U) {
                continue;
            }
            Meaning meaning;
            meaning.type = types_.element(type);
            meaning.kind = Interpretation::index;
            meaning.signal = candidate.signal;
            meaning.operands.push_back(candidate.type);
            meaning.cost = candidate.cost;
            bool ok = true;
            for (std::size_t dimension { }; dimension + 1U < e.operands.size(); ++dimension) {
                const auto want = index_of(type, dimension);
                const auto cost = operand_cost(e.operands[dimension + 1U], want);
                if (!cost) {
                    ok = false;
                    break;
                }
                meaning.cost += *cost;
                meaning.operands.push_back(want);
            }
            if (ok) {
                result.list.push_back(std::move(meaning));
            }
        }
        return result;
    }

    Interpretations slice(const Expression& e)
    {
        Interpretations result;
        if (e.operands.size() != 3U) {
            result.unmodeled = true;
            return result;
        }
        const auto& prefix = interpret(e.operands.front());
        if (prefix.unmodeled) {
            result.unmodeled = true;
            return result;
        }
        for (const auto& candidate : prefix.list) {
            auto type = candidate.type;
            if (types_.class_of(type) == TypeClass::access) {
                type = types_.designated(type);
            }
            if (types_.class_of(type) != TypeClass::array || types_.dimensions(type) != 1U) {
                continue;
            }
            const auto want = index_of(type, 0U);
            const auto left = operand_cost(e.operands[1], want);
            const auto right = operand_cost(e.operands[2], want);
            if (!left || !right) {
                continue;
            }
            Meaning meaning;
            meaning.type = type;
            meaning.kind = Interpretation::slice;
            meaning.signal = candidate.signal;
            meaning.operands = { candidate.type, want, want };
            meaning.cost = candidate.cost + *left + *right;
            result.list.push_back(std::move(meaning));
        }
        return result;
    }

    Interpretations conditional(const Expression& e)
    {
        Interpretations result;
        if (e.operands.size() != 3U) {
            result.unmodeled = true;
            return result;
        }
        const auto boolean = standard_type(support::StandardDeclaration::boolean);
        // The type of a conditional expression is that of its values, from
        // the context (9.3.8, 12.5): every type some value can have and every
        // value fits (a universal value converts, 9.3.6).
        std::vector<Type> types;
        // The pseudo kinds every value can have (string literals,
        // aggregates, null): the context then gives the type (9.3.2).
        std::optional<std::vector<Pseudo>> pseudos;
        for (const auto value : { e.operands[1], e.operands[2] }) {
            const auto found = interpret(value); // copy: the map may grow
            if (found.unmodeled) {
                result.unmodeled = true;
                return result;
            }
            std::vector<Pseudo> kinds;
            for (const auto& candidate : found.list) {
                if (candidate.pseudo == Pseudo::none && candidate.type
                    && std::ranges::find(types, candidate.type) == types.end()) {
                    types.push_back(candidate.type);
                }
                if (candidate.pseudo != Pseudo::none && candidate.pseudo != Pseudo::allocator
                    && (!pseudos || std::ranges::find(*pseudos, candidate.pseudo) != pseudos->end())) {
                    kinds.push_back(candidate.pseudo);
                }
            }
            pseudos = std::move(kinds);
        }
        if (types.empty() && pseudos) {
            for (const auto pseudo : *pseudos) {
                Meaning meaning;
                meaning.pseudo = pseudo;
                meaning.kind = Interpretation::operation;
                meaning.operands = { boolean, Type { }, Type { } };
                result.list.push_back(std::move(meaning));
            }
            return result;
        }
        for (const auto& type : types) {
            const auto then_cost = operand_cost(e.operands[1], type);
            const auto else_cost = then_cost ? operand_cost(e.operands[2], type) : std::nullopt;
            if (!then_cost || !else_cost) {
                continue;
            }
            Meaning meaning;
            meaning.type = type;
            meaning.kind = Interpretation::operation;
            meaning.operands = { boolean, type, type };
            meaning.cost = *then_cost + *else_cost;
            result.list.push_back(std::move(meaning));
        }
        return result;
    }

    Interpretations physical(const Expression& e)
    {
        Interpretations result;
        const auto symbol = support::vhdl_symbol(e.marker_argument);
        const auto resolution = lookup_.visible(symbol, e.scope, regions_.begin(e.source));
        std::vector<Type> found;
        for (const auto& entry : resolution.standard) {
            if (entry.kind == StandardEntry::Kind::unit) {
                found.push_back(types_.of_standard(entry.declaration));
            }
        }
        for (const auto id : resolution.candidates) {
            if (const auto unit = physical_units_.find(id.value()); unit != physical_units_.end()) {
                found.push_back(Types::declared(unit->second));
            }
        }
        for (const auto& type : found) {
            Meaning meaning;
            meaning.type = type;
            meaning.kind = Interpretation::physical_literal;
            meaning.operands.resize(e.operands.size()); // the abstract literal
            meaning.formals.resize(e.operands.size(), vhdl::no_formal);
            result.list.push_back(std::move(meaning));
        }
        if (result.list.empty()) {
            result.unmodeled = true;
        }
        return result;
    }

    /// The type a type mark name (a qualified expression's or allocator's)
    /// denotes.
    [[nodiscard]] Type mark_type(const vhdl::Name& mark)
    {
        if (mark.standard != support::StandardDeclaration::none) {
            return types_.of_standard(mark.standard);
        }
        if (mark.selected) {
            if (const auto* found = declaration(*mark.selected)) {
                return types_.of_type_declaration(*found);
            }
        }
        return { };
    }

    Interpretations qualified(const Expression& e)
    {
        Interpretations result;
        const auto* mark = e.qualified_type_mark ? &*e.qualified_type_mark
            : e.referenced_name ? &*e.referenced_name
                                : nullptr;
        const auto type = mark == nullptr ? Type { } : mark_type(*mark);
        if (!type || e.operands.size() != 1U) {
            result.unmodeled = true;
            return result;
        }
        Meaning meaning;
        meaning.type = type;
        meaning.kind = Interpretation::qualified;
        meaning.operands = { type };
        meaning.formals.resize(1U, vhdl::no_formal);
        result.list.push_back(std::move(meaning));
        return result;
    }

    Interpretations allocator(const Expression& e)
    {
        Interpretations result;
        if (e.operands.empty()) {
            result.unmodeled = true;
            return result;
        }
        const auto* subtype = node(e.operands.front());
        const auto* mark = subtype == nullptr ? nullptr
            : subtype->qualified_type_mark ? &*subtype->qualified_type_mark
            : subtype->referenced_name     ? &*subtype->referenced_name
                                           : nullptr;
        const auto type = mark == nullptr ? Type { } : mark_type(*mark);
        if (!type) {
            result.unmodeled = true;
            return result;
        }
        Meaning meaning;
        meaning.pseudo = Pseudo::allocator;
        meaning.designated = type;
        meaning.kind = Interpretation::allocator;
        meaning.operands.resize(e.operands.size());
        if (e.operands.size() > 1U) {
            meaning.operands[1] = type; // the qualified initial value
        }
        meaning.formals.resize(e.operands.size(), vhdl::no_formal);
        result.list.push_back(std::move(meaning));
        return result;
    }

    Interpretations member(const Expression& e)
    {
        Interpretations result;
        if (e.operands.size() != 1U) {
            result.unmodeled = true;
            return result;
        }
        const auto& prefix = interpret(e.operands.front());
        if (prefix.unmodeled) {
            result.unmodeled = true;
            return result;
        }
        const auto element = support::vhdl_symbol(e.marker_argument.empty()
                ? std::string_view { e.text }
                : std::string_view { e.marker_argument });
        for (const auto& candidate : prefix.list) {
            auto type = candidate.type;
            if (types_.class_of(type) == TypeClass::access) {
                type = types_.designated(type);
            }
            const auto selected = types_.record_element(type, element);
            if (!selected) {
                continue;
            }
            Meaning meaning;
            meaning.type = selected;
            meaning.kind = Interpretation::member;
            meaning.signal = candidate.signal;
            meaning.operands = { candidate.type };
            meaning.cost = candidate.cost;
            result.list.push_back(std::move(meaning));
        }
        if (result.list.empty()) {
            result.unmodeled = true; // e.g. a protected method (5.6.2)
        }
        return result;
    }

    Interpretations dereference(const Expression& e)
    {
        Interpretations result;
        if (e.operands.size() != 1U) {
            result.unmodeled = true;
            return result;
        }
        const auto& prefix = interpret(e.operands.front());
        if (prefix.unmodeled) {
            result.unmodeled = true;
            return result;
        }
        for (const auto& candidate : prefix.list) {
            if (types_.class_of(candidate.type) != TypeClass::access) {
                continue;
            }
            Meaning meaning;
            meaning.type = types_.designated(candidate.type);
            meaning.kind = Interpretation::dereference;
            meaning.operands = { candidate.type };
            meaning.cost = candidate.cost;
            result.list.push_back(std::move(meaning));
        }
        return result;
    }

    Interpretations attribute(const Expression& e)
    {
        Interpretations result;
        if (e.operands.empty()) {
            result.unmodeled = true;
            return result;
        }
        const auto edition = year(e.scope);
        const auto designator = support::vhdl_symbol(std::string_view { e.text }.substr(1U));
        const auto predefined = predefined_attribute(designator, edition);
        if (!predefined) {
            result.unmodeled = true; // a user-defined attribute (7.2)
            return result;
        }
        const auto& prefix = interpret(e.operands.front());
        if (prefix.unmodeled && e.operands.size() == 1U
            && (*predefined == vhdl::PredefinedAttribute::simple_name
                || *predefined == vhdl::PredefinedAttribute::instance_name
                || *predefined == vhdl::PredefinedAttribute::path_name)) {
            // 16.2.5: any named entity (a design unit, a label) is the
            // prefix; the result is STRING whatever it is.
            Meaning meaning;
            meaning.type = standard_type(support::StandardDeclaration::string);
            meaning.kind = Interpretation::attribute;
            meaning.attribute = *predefined;
            meaning.operands.resize(1U);
            meaning.formals.resize(1U, vhdl::no_formal);
            unmodeled_.insert(e.operands.front().value());
            result.list.push_back(std::move(meaning));
            return result;
        }
        if (prefix.unmodeled || prefix.list.size() != 1U) {
            // The prefix of an attribute name is typed without context
            // (16.2.1); an overloaded prefix is not modeled.
            result.unmodeled = true;
            return result;
        }
        const auto& candidate = prefix.list.front();
        const auto prefix_kind = candidate.type_mark ? AttributePrefix::type_mark
            : candidate.signal                       ? AttributePrefix::signal
            : candidate.kind == Interpretation::object
                || candidate.kind == Interpretation::index
                || candidate.kind == Interpretation::slice
                || candidate.kind == Interpretation::member
                || candidate.kind == Interpretation::dereference
                || candidate.kind == Interpretation::function_call
            ? AttributePrefix::object
            : AttributePrefix::named_entity;
        std::size_t dimension { };
        if (e.operands.size() > 1U) {
            if (const auto* argument = node(e.operands[1]);
                argument != nullptr && argument->kind == ExpressionKind::integer_literal) {
                const auto value = std::strtoull(argument->text.c_str(), nullptr, 10);
                dimension = value == 0U ? 0U : static_cast<std::size_t>(value - 1U);
            }
        }
        auto profile = attribute_profile(types_, *predefined, prefix_kind,
            candidate.type, dimension, edition);
        if (!profile && !candidate.type_mark
            && types_.class_of(candidate.type) == TypeClass::access) {
            // A prefix of an access type is appropriate for the designated
            // type (8.1): `p'length` of an access to an array.
            profile = attribute_profile(types_, *predefined, AttributePrefix::object,
                types_.designated(candidate.type), dimension, edition);
        }
        if (!profile) {
            Meaning none;
            (void)none;
            return result; // no interpretation: TYPE-001 at the context
        }
        Meaning meaning;
        meaning.type = profile->result;
        meaning.kind = Interpretation::attribute;
        meaning.convertible = true;
        meaning.attribute = profile->attribute;
        meaning.type_mark = profile->subtype;
        meaning.signal = profile->signal;
        meaning.range = profile->range;
        meaning.operands.resize(e.operands.size());
        meaning.formals.resize(e.operands.size(), vhdl::no_formal);
        meaning.operands[0] = Type { }; // the prefix: typed without context
        if (e.operands.size() > 1U) {
            switch (profile->argument) {
            case AttributeArgument::dimension:
                meaning.operands[1] = Types::universal_integer();
                break;
            case AttributeArgument::prefix_base:
                meaning.operands[1] = candidate.type;
                break;
            case AttributeArgument::string:
                meaning.operands[1] = standard_type(support::StandardDeclaration::string);
                break;
            case AttributeArgument::time:
                meaning.operands[1] = standard_type(support::StandardDeclaration::time);
                break;
            case AttributeArgument::any_integer:
            case AttributeArgument::none:
                break;
            }
        }
        if (!meaning.type && !meaning.range && !meaning.type_mark) {
            result.unmodeled = true; // 'REFLECT, 'CONVERSE
            return result;
        }
        if (meaning.range) {
            meaning.type = profile->result;
        }
        result.list.push_back(std::move(meaning));
        return result;
    }

    // ---- top-down selection ---------------------------------------------

    /// Selects the interpretation of complete context `id` that fits
    /// `expected` (none: the context gives no type; the node's own
    /// interpretations must agree), stores it and its operands'. Returns the
    /// selected type (none on error or for an untyped node).
    Type select(const ExpressionId id, const Type& expected, const bool condition = false)
    {
        auto* e = node(id);
        if (e == nullptr) {
            return { };
        }
        auto found = interpret(id); // copy: the map may grow
        if (expected && types_.class_of(expected) == TypeClass::none) {
            found.unmodeled = true; // an interface type (6.5.3): BD2 (V4)
        }
        if (found.unmodeled) {
            unmodeled_.insert(id.value());
            // Operands are still typed where they decide themselves; no
            // error is reported under a node the pass does not type.
            ++quiet_;
            for (const auto operand : e->operands) {
                select_free(operand);
            }
            --quiet_;
            return { };
        }
        if (found.list.empty()) {
            if (!reported_.contains(id.value())) {
                reported_.insert(id.value());
                error("FSIM-VHDL-TYPE-001",
                    "no interpretation of '" + shown(*e) + "' fits"
                        + (e->operands.empty() ? std::string { }
                                               : " its operands (" + describe_operands(*e) + ")")
                        + " (IEEE 1076-2019 12.5)",
                    e->source);
            }
            return { };
        }
        std::vector<const Meaning*> candidates;
        unsigned best = std::numeric_limits<unsigned>::max();
        const auto cost_of = [&](const Meaning& meaning) -> std::optional<unsigned> {
            if (!expected) {
                return meaning.cost;
            }
            const auto fit = fits(meaning, expected);
            if (fit == 0) {
                return std::nullopt;
            }
            return meaning.cost + (fit == 1 ? 1U : 0U);
        };
        for (const auto& meaning : found.list) {
            const auto cost = cost_of(meaning);
            if (!cost) {
                continue;
            }
            if (*cost < best) {
                best = *cost;
                candidates.clear();
            }
            if (*cost == best) {
                candidates.push_back(&meaning);
            }
        }
        // 2008 9.2.9: a condition that is not BOOLEAN is converted by the
        // condition operator ?? when exactly one such operator fits.
        if (candidates.empty() && condition && year(e->scope) >= 2008U) {
            static const auto symbol = support::vhdl_symbol("??"); // interned once
            std::vector<Type> converted;
            for (const auto& profile : overloads(symbol, e->scope, e->source, nullptr)) {
                if (profile.parameters.size() != 1U || profile.result != expected) {
                    continue;
                }
                for (const auto& meaning : found.list) {
                    if (fits(meaning, profile.parameters.front()) == 2
                        && std::ranges::find(converted, profile.parameters.front())
                            == converted.end()) {
                        converted.push_back(profile.parameters.front());
                    }
                }
            }
            if (converted.size() == 1U) {
                return select(id, converted.front(), false);
            }
        }
        if (candidates.empty()) {
            if (!reported_.contains(id.value())) {
                reported_.insert(id.value());
                error("FSIM-VHDL-TYPE-001",
                    "no interpretation of '" + shown(*e) + "' has type "
                        + types_.describe(expected)
                        + (e->operands.empty() ? std::string { }
                                               : " with operands (" + describe_operands(*e) + ")")
                        + " (IEEE 1076-2019 12.5)",
                    e->source);
            }
            return { };
        }
        if (!expected && candidates.size() > 1U) {
            // Without a context type, the interpretations must agree on one
            // type; a universal one gives way to none other here.
            std::vector<const Meaning*> distinct;
            for (const auto* candidate : candidates) {
                if (std::ranges::none_of(distinct, [&](const Meaning* other) {
                        return other->type == candidate->type && other->pseudo == candidate->pseudo;
                    })) {
                    distinct.push_back(candidate);
                }
            }
            if (distinct.size() == 1U && candidates.size() > 1U) {
                // One type, several meanings (homograph overloads): ambiguous.
            }
        }
        if (candidates.size() > 1U) {
            if (!reported_.contains(id.value())) {
                reported_.insert(id.value());
                std::vector<std::string> notes;
                for (const auto* candidate : candidates) {
                    notes.push_back("candidate: " + describe_meaning(*candidate));
                }
                error("FSIM-VHDL-TYPE-002",
                    "'" + shown(*e) + "' is ambiguous: "
                        + std::to_string(candidates.size())
                        + " interpretations fit"
                        + (expected ? " type " + types_.describe(expected) : std::string { })
                        + " (IEEE 1076-2019 12.5)",
                    e->source, std::move(notes));
            }
            return { };
        }
        const auto chosen = *candidates.front();
        if (chosen.pseudo != Pseudo::none && !expected) {
            if (!reported_.contains(id.value())) {
                reported_.insert(id.value());
                error("FSIM-VHDL-TYPE-003",
                    "the type of '" + shown(*e)
                        + "' is not determined by its context (IEEE 1076-2019 9.3.2, 9.3.3, 12.5)",
                    e->source);
            }
            return { };
        }
        store(*e, chosen, expected);
        // Operands, top down (an aggregate's operands are its element
        // values, typed by select_aggregate).
        if (chosen.kind == Interpretation::conversion && e->operands.size() == 1U) {
            // 9.3.6: the operand is typed alone; its type shall be closely
            // related to the target type.
            const auto operand = e->operands.front();
            const auto source = select(operand, { });
            if (source && types_.class_of(source) != TypeClass::none
                && types_.class_of(chosen.type) != TypeClass::none
                && !types_.closely_related(chosen.type, source, year(e->scope))) {
                error("FSIM-VHDL-TYPE-001",
                    "the operand of the conversion to " + types_.describe(chosen.type)
                        + " has type " + types_.describe(source)
                        + ", which is not closely related (IEEE 1076-2019 9.3.6)",
                    e->source);
            }
            return node(id)->type;
        }
        const auto operands = e->kind == ExpressionKind::aggregate
            ? std::vector<ExpressionId> { }
            : e->operands;
        for (std::size_t index { }; index < operands.size(); ++index) {
            auto want = index < chosen.operands.size() ? chosen.operands[index] : Type { };
            if (chosen.range && expected && range_node(*e)) {
                want = expected; // the bounds of a range have the range's type
            }
            if (e->kind == ExpressionKind::conditional && index > 0U && !want) {
                want = expected; // pseudo-typed values: the context's type (9.3.2)
            }
            if (e->kind == ExpressionKind::conditional && index == 0U) {
                this->condition(operands[index]); // 9.2.9: a condition (?? applies)
                continue;
            }
            if (want) {
                select(operands[index], want);
            } else {
                select_free(operands[index]);
            }
        }
        if (e->kind == ExpressionKind::aggregate) {
            select_aggregate(id, chosen.pseudo == Pseudo::aggregate ? expected : chosen.type, 0U);
        }
        return node(id)->type;
    }

    /// Types an operand that has no context type (a conversion operand, an
    /// attribute prefix): its own interpretations must decide (9.3.6, 16.2).
    void select_free(const ExpressionId id)
    {
        const auto& found = interpret(id);
        if (found.unmodeled) {
            (void)select(id, { });
            return;
        }
        // Prefer one interpretation of the cheapest cost; a universal type
        // stays universal.
        (void)select(id, { });
    }

    /// The elements and choices of aggregate `id` of type `type` (9.3.3);
    /// `dimension` > 0 for the subaggregates of a multidimensional array.
    void select_aggregate(const ExpressionId id, const Type& type, const std::size_t dimension)
    {
        auto* e = node(id);
        if (e == nullptr || !type) {
            return;
        }
        const auto associations = e->associations;
        const auto type_class = types_.class_of(type);
        if (type_class == TypeClass::record) {
            // 9.3.3.2: positional associations take the elements in order;
            // `others` the remaining elements, which shall have one type.
            const auto* definition = design_.vhdl_hir.find_type(type.declared);
            if (definition == nullptr) {
                return;
            }
            std::vector<Type> elements;
            for (const auto& element : definition->record_elements) {
                elements.push_back(types_.of_subtype(element.subtype));
            }
            std::vector<bool> named(elements.size());
            std::size_t position { };
            for (const auto& association : associations) {
                if (association.positional()) {
                    if (position < named.size()) {
                        named[position] = true;
                    }
                    ++position;
                } else if (!association.choice_spelling.empty()
                    && association.choice_marker == Marker::none) {
                    const auto symbol = support::vhdl_symbol(association.choice_spelling);
                    for (std::size_t index { }; index < definition->record_elements.size(); ++index) {
                        if (support::vhdl_symbol(definition->record_elements[index].name) == symbol) {
                            named[index] = true;
                        }
                    }
                }
            }
            position = 0U;
            for (const auto& association : associations) {
                Type element;
                if (association.positional()) {
                    if (position < elements.size()) {
                        element = elements[position];
                    }
                    ++position;
                } else if (association.choice_marker == Marker::vhdl_others_choice) {
                    for (std::size_t index { }; index < elements.size(); ++index) {
                        if (named[index]) {
                            continue;
                        }
                        if (!element) {
                            element = elements[index];
                        } else if (element != elements[index]) {
                            element = { };
                            break;
                        }
                    }
                } else if (!association.choice_spelling.empty()
                    && association.choice_marker == Marker::none) {
                    element = types_.record_element(type,
                        support::vhdl_symbol(association.choice_spelling));
                }
                if (element) {
                    select(association.value, element);
                } else {
                    unmodeled_.insert(association.value.value());
                }
            }
            return;
        }
        if (type_class != TypeClass::array) {
            return;
        }
        const auto dimensions = types_.dimensions(type);
        const auto index_type = index_of(type, dimension);
        for (const auto& association : associations) {
            for (const auto choice : association.choices) {
                select_range_or_value(choice, index_type);
            }
            if (dimension + 1U < dimensions) {
                const auto* value = node(association.value);
                if (value != nullptr && value->kind == ExpressionKind::aggregate) {
                    store_pseudo(association.value, Interpretation::aggregate);
                    select_aggregate(association.value, type, dimension + 1U);
                } else if (value != nullptr && value->kind == ExpressionKind::string_literal) {
                    store_pseudo(association.value, Interpretation::literal);
                } else {
                    unmodeled_.insert(association.value.value());
                }
                continue;
            }
            // 9.3.3.3 (2008): a positional or named element of a
            // one-dimensional aggregate is an element or a slice of the
            // aggregate's type.
            const auto element = types_.element(type);
            const auto value = interpret(association.value); // copy: the map may grow
            bool element_fits = value.unmodeled;
            bool array_fits = false;
            const bool slices = dimensions == 1U && year(e->scope) >= 2008U;
            for (const auto& meaning : value.list) {
                element_fits = element_fits || fits(meaning, element) != 0;
                // A nested aggregate or string literal (pseudo) as well.
                array_fits = array_fits || (slices && fits(meaning, type) != 0);
            }
            select(association.value, element_fits || !array_fits ? element : type);
        }
        (void)e;
    }

    void store_pseudo(const ExpressionId id, const Interpretation kind)
    {
        if (auto* e = node(id)) {
            e->interpretation = kind;
            typed_.insert(id.value());
        }
    }

    /// A choice or range bound: a range `L to R` / `L downto R` (a binary
    /// node of a choice), a range attribute, or a value, of type `want`.
    void select_range_or_value(const ExpressionId id, const Type& want)
    {
        auto* e = node(id);
        if (e == nullptr) {
            return;
        }
        if (e->kind == ExpressionKind::default_choice || e->marker == Marker::vhdl_others_choice) {
            store_pseudo(id, Interpretation::literal);
            return;
        }
        if (range_node(*e)) {
            (void)select(id, want);
            return;
        }
        // A discrete subtype indication or range attribute as a choice.
        const auto& found = interpret(id);
        if (!found.unmodeled && found.list.size() == 1U
            && (found.list.front().type_mark || found.list.front().range)) {
            (void)select(id, { });
            return;
        }
        select(id, want);
    }

    /// The types both bounds of a range can have (5.3.2.2): a type both
    /// have, or the type one has and the other's universal type converts
    /// to; a non-universal type wins over the universal one it converts.
    [[nodiscard]] std::vector<Type> bound_types(const Interpretations& lefts,
        const Interpretations& rights) const
    {
        std::vector<Type> common;
        const auto add = [&](const Type& type) {
            if (type && std::ranges::find(common, type) == common.end()) {
                common.push_back(type);
            }
        };
        for (const auto& l : lefts.list) {
            for (const auto& r : rights.list) {
                if (l.pseudo != Pseudo::none || r.pseudo != Pseudo::none) {
                    continue;
                }
                if (l.type == r.type) {
                    add(l.type);
                } else if (fit_types(types_, l.type, r.type) != 0) {
                    add(r.type);
                } else if (fit_types(types_, r.type, l.type) != 0) {
                    add(l.type);
                }
            }
        }
        // A non-universal type wins over the universal one it converts.
        if (common.size() > 1U) {
            std::erase_if(common, [&](const Type& type) {
                return type.kind == Type::Kind::universal_integer
                    || type.kind == Type::Kind::universal_real;
            });
        }
        return common;
    }

    /// The index type of dimension `dimension` of array type `array`: its
    /// index subtype's type, or for a discrete range in the array
    /// definition the type of the range's bounds (5.3.2.1, 5.3.2.2).
    [[nodiscard]] Type index_of(const Type& array, const std::size_t dimension)
    {
        if (auto type = types_.index(array, dimension)) {
            return type;
        }
        if (array.kind != Type::Kind::declared) {
            return { };
        }
        const auto* definition = design_.vhdl_hir.find_type(array.declared);
        if (definition == nullptr || dimension >= definition->array_dimensions.size()
            || !definition->array_dimensions[dimension].constraint) {
            return { };
        }
        const auto& range = *definition->array_dimensions[dimension].constraint;
        if (range.left_expression && range.right_expression) {
            const auto& lefts = interpret(*range.left_expression);
            const auto& rights = interpret(*range.right_expression);
            if (lefts.unmodeled || rights.unmodeled) {
                return { };
            }
            const auto common = bound_types(lefts, rights);
            if (common.size() != 1U) {
                return { };
            }
            return common.front().kind == Type::Kind::universal_integer
                ? standard_type(support::StandardDeclaration::integer)
                : common.front();
        }
        if (range.left_expression) {
            // A range attribute or discrete subtype name (5.3.2.1).
            const auto& found = interpret(*range.left_expression);
            return !found.unmodeled && found.list.size() == 1U ? found.list.front().type : Type { };
        }
        if (range.kind == vhdl::RangeKind::integer && range.left && range.right) {
            // Literal bounds folded by the front end: universal_integer,
            // so INTEGER (5.3.2.2).
            return standard_type(support::StandardDeclaration::integer);
        }
        return { };
    }

    /// The type of a discrete range given by two bounds (5.3.2.2, 10.10,
    /// 11.8): the one type both bounds can have; INTEGER when both are
    /// universal_integer (5.3.2.2 note) or `universal` when `universal_ok`.
    Type select_bounds(const ExpressionId left, const std::optional<ExpressionId> right)
    {
        if (!right) {
            // A discrete subtype indication or a range attribute.
            const auto& found = interpret(left);
            if (found.unmodeled || found.list.size() != 1U) {
                return select(left, { });
            }
            const auto type = found.list.front().type;
            (void)select(left, { });
            return type;
        }
        const auto& lefts = interpret(left);
        const auto& rights = interpret(*right);
        if (lefts.unmodeled || rights.unmodeled) {
            (void)select(left, { });
            (void)select(*right, { });
            return { };
        }
        const auto common = bound_types(lefts, rights);
        Type type = common.size() == 1U ? common.front() : Type { };
        if (type.kind == Type::Kind::universal_integer) {
            type = standard_type(support::StandardDeclaration::integer);
        }
        if (!type) {
            const auto* e = node(left);
            if (e != nullptr && !reported_.contains(left.value())) {
                reported_.insert(left.value());
                error(common.empty() ? "FSIM-VHDL-TYPE-001" : "FSIM-VHDL-TYPE-002",
                    common.empty()
                        ? "the bounds of the range have no common discrete type (IEEE 1076-2019 5.3.2.2, 12.5)"
                        : "the type of the range is ambiguous (IEEE 1076-2019 5.3.2.2, 12.5)",
                    e->source);
            }
            return { };
        }
        select(left, type);
        select(*right, type);
        return type;
    }

    void store(Expression& e, const Meaning& meaning, const Type& expected)
    {
        typed_.insert(e.id.value());
        e.interpretation = meaning.kind;
        e.type = meaning.pseudo == Pseudo::none ? meaning.type : expected;
        e.resolved_standard = meaning.standard;
        e.predefined_operation = meaning.operation;
        e.predefined_attribute = meaning.attribute;
        e.resolved_formals.clear();
        if (std::ranges::any_of(meaning.formals, [](const std::uint32_t formal) { return formal != vhdl::no_formal; })) {
            e.resolved_formals = meaning.formals;
        }
        if ((meaning.kind == Interpretation::function_call
                || meaning.kind == Interpretation::enumeration_literal)
            && meaning.declaration.valid()) {
            e.resolved_declaration = meaning.declaration;
        }
    }

    // ---- contexts ---------------------------------------------------------

    void condition(const ExpressionId id)
    {
        (void)select(id, standard_type(support::StandardDeclaration::boolean), true);
    }

    /// The constraints of a subtype indication (5.2.1, 5.3.2.2, 6.3).
    void type_subtype(const vhdl::SubtypeIndication& subtype)
    {
        const auto base = types_.of_subtype(subtype);
        if (!base) {
            return;
        }
        const auto type_class = types_.class_of(base);
        for (std::size_t index { }; index < subtype.constraints.size(); ++index) {
            const auto& range = subtype.constraints[index];
            const auto want = type_class == TypeClass::array ? index_of(base, index) : base;
            type_range(range, want);
        }
    }

    void type_range(const vhdl::RangeConstraint& range, const Type& want)
    {
        if (range.left_expression && range.right_expression && !want) {
            (void)select_bounds(*range.left_expression, range.right_expression);
            return;
        }
        for (const auto& bound : { range.left_expression, range.right_expression }) {
            if (bound) {
                select_range_or_value(*bound, want);
            }
        }
    }

    void type_declaration(const vhdl::Declaration& d)
    {
        using Form = vhdl::DeclarationForm;
        if (d.subtype) {
            type_subtype(*d.subtype);
        }
        if (d.callable && d.callable->return_type) {
            type_subtype(*d.callable->return_type);
        }
        if (d.initializer) {
            switch (d.form) {
            case Form::constant:
            case Form::signal:
            case Form::variable:
            case Form::port:
            case Form::generic_constant:
                if (d.subtype) {
                    const auto type = types_.of_subtype(*d.subtype);
                    if (type) {
                        (void)select(*d.initializer, type);
                    } else {
                        unmodeled_.insert(d.initializer->value());
                    }
                }
                break;
            case Form::file:
                // The file logical name (5.5.2).
                (void)select(*d.initializer, standard_type(support::StandardDeclaration::string));
                break;
            case Form::attribute_specification:
                if (d.attribute && d.attribute->subtype) {
                    const auto type = types_.of_subtype(*d.attribute->subtype);
                    if (type) {
                        (void)select(*d.initializer, type);
                    }
                } else {
                    unmodeled_.insert(d.initializer->value());
                }
                break;
            default:
                unmodeled_.insert(d.initializer->value());
                break;
            }
        }
        if (d.file_open_kind) {
            (void)select(*d.file_open_kind, standard_type(support::StandardDeclaration::file_open_kind));
        }
        if (d.attribute && d.attribute->value && d.attribute->subtype && !d.initializer) {
            const auto type = types_.of_subtype(*d.attribute->subtype);
            if (type) {
                (void)select(*d.attribute->value, type);
            }
        }
    }

    void type_definition(const vhdl::TypeDefinition& type, const ScopeId scope)
    {
        (void)scope;
        if (type.form == vhdl::TypeForm::subtype || type.form == vhdl::TypeForm::alias) {
            type_subtype(type.base);
        }
        if (type.scalar_range) {
            // An integer, floating or physical type definition: the bounds
            // have any integer (floating) type (5.2.3.1, 5.2.5.1).
            const auto& range = *type.scalar_range;
            if (range.left_expression && range.right_expression) {
                (void)select_bounds(*range.left_expression, range.right_expression);
            }
        }
        const auto self = Types::declared(type.id);
        for (std::size_t index { }; index < type.array_dimensions.size(); ++index) {
            const auto& dimension = type.array_dimensions[index];
            if (dimension.constraint) {
                type_range(*dimension.constraint, index_of(self, index));
            }
        }
        if (type.element_subtype) {
            type_subtype(*type.element_subtype);
        }
        for (const auto& element : type.record_elements) {
            type_subtype(element.subtype);
        }
        if (type.designated_subtype) {
            type_subtype(*type.designated_subtype);
        }
        for (const auto& unit : type.physical_units) {
            if (unit.scale) {
                (void)select(*unit.scale, self);
            }
        }
    }

    /// The result type of the function whose body holds `scope`.
    [[nodiscard]] Type enclosing_result(ScopeId scope) const
    {
        for (std::size_t depth { }; depth < 256U; ++depth) {
            if (const auto found = callables_.find(scope.value()); found != callables_.end()) {
                const auto* callable = design_.vhdl_hir.find_declaration(found->second);
                return callable == nullptr ? Type { } : types_.result_of(*callable);
            }
            const auto* region = regions_.find(scope);
            if (region == nullptr || !region->enclosing) {
                return { };
            }
            scope = *region->enclosing;
        }
        return { };
    }

    void type_delay(const std::optional<vhdl::Delay>& delay)
    {
        if (!delay) {
            return;
        }
        const auto time = standard_type(support::StandardDeclaration::time);
        const auto one = [&](const vhdl::DelayValue& value) {
            if (value.expression) {
                (void)select(*value.expression, time);
            }
        };
        one(delay->primary);
        for (const auto& value : { delay->minimum, delay->typical, delay->maximum }) {
            if (value) {
                one(*value);
            }
        }
        for (const auto& additional : delay->additional) {
            type_delay(additional);
        }
    }

    void type_statement(const vhdl::Statement& s)
    {
        using Kind = vhdl::StatementKind;
        switch (s.kind) {
        case Kind::signal_assignment:
        case Kind::variable_assignment:
        case Kind::force: {
            Type target;
            const auto* written = s.target ? node(*s.target) : nullptr;
            if (written != nullptr && written->kind == ExpressionKind::aggregate) {
                // 10.5.2.1, 10.6.2.1: an aggregate target's type is that of
                // the value, typed without it (a composite type).
                std::optional<ExpressionId> source = s.value;
                if (!source && !s.waveform.empty() && !s.waveform.front().disconnect) {
                    source = s.waveform.front().value;
                }
                if (source) {
                    target = select(*source, { });
                }
                const auto target_class = target ? types_.class_of(target) : TypeClass::none;
                if (target_class == TypeClass::array || target_class == TypeClass::record) {
                    store_pseudo(*s.target, Interpretation::aggregate);
                    if (auto* stored = node(*s.target)) {
                        stored->type = target;
                    }
                    select_aggregate(*s.target, target, 0U);
                } else {
                    unmodeled_.insert(s.target->value());
                }
                for (const auto& element : s.waveform) {
                    if (source && element.value == *source) {
                        type_delay(element.delay);
                        continue;
                    }
                    if (!element.disconnect && target) {
                        (void)select(element.value, target);
                    }
                    type_delay(element.delay);
                }
                break;
            }
            if (s.target) {
                target = select(*s.target, { });
            }
            if (s.value) {
                if (target) {
                    (void)select(*s.value, target);
                } else {
                    unmodeled_.insert(s.value->value());
                }
            }
            for (const auto& element : s.waveform) {
                if (!element.disconnect) {
                    if (target) {
                        (void)select(element.value, target);
                    } else {
                        unmodeled_.insert(element.value.value());
                    }
                } else {
                    store_pseudo(element.value, Interpretation::null_value);
                }
                type_delay(element.delay);
            }
            if (s.selected_assignment || !s.alternatives.empty()) {
                // A selected assignment: the selector is `condition`.
            }
            break;
        }
        case Kind::return_statement:
            if (s.value) {
                const auto result = enclosing_result(s.scope);
                if (result) {
                    (void)select(*s.value, result);
                } else {
                    unmodeled_.insert(s.value->value());
                }
            }
            break;
        case Kind::assertion:
        case Kind::report:
            if (s.report) {
                (void)select(*s.report, standard_type(support::StandardDeclaration::string));
            }
            if (s.severity) {
                (void)select(*s.severity, standard_type(support::StandardDeclaration::severity_level));
            }
            break;
        case Kind::procedure_call:
            type_procedure(s);
            break;
        case Kind::loop:
            break; // the range: typed first (run)
        default:
            break;
        }
        // Conditions and selectors.
        if (s.condition) {
            const bool selection = s.kind == Kind::selection || s.selected_assignment;
            if (selection) {
                const auto selector = select(*s.condition, { });
                for (const auto& alternative : s.alternatives) {
                    for (const auto choice : alternative.choices) {
                        if (selector) {
                            select_range_or_value(choice, selector);
                        } else {
                            unmodeled_.insert(choice.value());
                        }
                    }
                }
            } else if (s.kind == Kind::loop && s.loop_initial) {
                // A for loop's synthesized compare (the parser's form).
                unmodeled_.insert(s.condition->value());
            } else {
                condition(*s.condition);
            }
        } else {
            for (const auto& alternative : s.alternatives) {
                for (const auto choice : alternative.choices) {
                    unmodeled_.insert(choice.value());
                }
            }
        }
        if (s.guard) {
            condition(*s.guard);
        }
        type_delay(s.delay);
        type_delay(s.rejection_limit);
        type_delay(s.disconnection_delay);
        for (const auto& sensitivity : s.sensitivities) {
            if (sensitivity.expression) {
                (void)select(*sensitivity.expression, { });
            }
        }
    }

    void type_procedure(const vhdl::Statement& s)
    {
        const auto parts = selected_parts(s.procedure);
        if (parts.empty()) {
            return;
        }
        if (method_name(s.procedure)
            || undeclared(parts.back(), s.scope, s.source, s.procedure)
            || generic_named(parts.back(), s.scope, s.source, s.procedure)) {
            // A protected method call (5.6.2): census, not typed yet.
            for (const auto& argument : s.procedure_arguments) {
                unmodeled_.insert(argument.actual.value());
            }
            return;
        }
        std::vector<ExpressionId> actuals;
        std::vector<std::string> names;
        for (const auto& argument : s.procedure_arguments) {
            actuals.push_back(argument.actual);
            names.push_back(argument.formal ? std::string { argument.formal->text() } : std::string { });
        }
        std::vector<Meaning> fitting;
        unsigned best = std::numeric_limits<unsigned>::max();
        for (const auto& profile : overloads(parts.back(), s.scope, s.source, &s.procedure)) {
            if (profile.result) {
                continue;
            }
            auto meaning = apply(profile, actuals, names, 0U);
            if (!meaning) {
                continue;
            }
            meaning->declaration = profile.declaration;
            meaning->standard = profile.standard;
            meaning->operation = profile.operation;
            if (meaning->cost < best) {
                best = meaning->cost;
                fitting.clear();
            }
            if (meaning->cost == best) {
                fitting.push_back(std::move(*meaning));
            }
        }
        if (std::ranges::any_of(fitting, [](const Meaning& meaning) { return meaning.open; })) {
            for (const auto actual : actuals) {
                unmodeled_.insert(actual.value());
            }
            return; // a formal of unknown type (census)
        }
        if (fitting.size() != 1U) {
            // An actual the pass does not model fits every formal: no
            // decision (census), neither "none" nor "ambiguous".
            if (std::ranges::any_of(actuals, [&](const ExpressionId id) {
                    return interpret(id).unmodeled;
                })) {
                for (const auto actual : actuals) {
                    unmodeled_.insert(actual.value());
                }
                return;
            }
            error(fitting.empty() ? "FSIM-VHDL-TYPE-001" : "FSIM-VHDL-TYPE-002",
                fitting.empty()
                    ? "no visible procedure '" + s.procedure.spelling + "' fits the actuals (IEEE 1076-2019 12.5)"
                    : "the call of procedure '" + s.procedure.spelling + "' is ambiguous: "
                        + std::to_string(fitting.size()) + " procedures fit (IEEE 1076-2019 12.5)",
                s.source);
            return;
        }
        const auto& chosen = fitting.front();
        auto& statement = design_.vhdl_hir.mutable_statements()[statement_position(s)];
        if (chosen.declaration.valid() && chosen.operation == vhdl::PredefinedOperation::none) {
            statement.resolved_procedure = chosen.declaration;
        }
        statement.resolved_formals.clear();
        if (std::ranges::any_of(chosen.formals, [](const std::uint32_t formal) { return formal != vhdl::no_formal; })) {
            statement.resolved_formals = chosen.formals;
        }
        for (std::size_t index { }; index < actuals.size(); ++index) {
            if (chosen.operands[index]) {
                (void)select(actuals[index], chosen.operands[index]);
            }
        }
    }

    [[nodiscard]] std::size_t statement_position(const vhdl::Statement& s) const
    {
        const auto& statements = design_.vhdl_hir.statements();
        return static_cast<std::size_t>(&s - statements.data());
    }

    void type_instance(const vhdl::Instance& instance)
    {
        // The formals of a binding are not visible by selection here; the
        // actuals are typed by the formal's type in a later round (6.5.7).
        for (const auto* map : { &instance.generic_map, &instance.port_map }) {
            for (const auto& association : *map) {
                if (association.expression) {
                    unmodeled_.insert(association.expression->value());
                }
            }
        }
    }

    void type_generate(const vhdl::GenerateRegion& region)
    {
        switch (region.kind) {
        case vhdl::GenerateKind::conditional:
            if (region.condition) {
                condition(*region.condition);
            }
            break;
        case vhdl::GenerateKind::iterative:
            if (region.initial && region.condition) {
                // The parser gives the range as `initial` and the right
                // operand of the synthesized compare `condition`.
                const auto* compare = node(*region.condition);
                if (compare != nullptr && compare->operands.size() == 2U) {
                    const auto type = select_bounds(*region.initial, compare->operands[1]);
                    if (type && !region.iterator.empty()) {
                        generate_parameters_[region.scope.value()]
                            = { support::vhdl_symbol(region.iterator), type };
                    }
                }
            }
            break;
        case vhdl::GenerateKind::selection:
        case vhdl::GenerateKind::block:
            break;
        }
        for (const auto& nested : region.nested) {
            type_generate(nested);
        }
    }

    // ---- census -------------------------------------------------------------

    /// Development census (gate BD D4/D4b): with FSIM_D4_TYPING_CENSUS set
    /// to a file, every analyzed unit appends one line per expression the
    /// pass did not type (`unmodeled|unreached<TAB>kind<TAB>marker<TAB>text`)
    /// and a `summary<TAB>typed<TAB>unmodeled<TAB>unreached<TAB>open` line.
    /// It reads results only; analysis is the same with or without it.
    void census()
    {
        const char* path = std::getenv("FSIM_D4_TYPING_CENSUS");
        if (path == nullptr) {
            return;
        }
        auto out = support::native_fs::open_ofstream(std::filesystem::path { path },
            std::ios::app);
        if (!out) {
            return;
        }
        std::size_t typed { };
        std::size_t unmodeled { };
        std::size_t unreached { };
        for (const auto& e : design_.vhdl_hir.expressions()) {
            if (!owned(e.scope)) {
                continue;
            }
            if (typed_.contains(e.id.value())) {
                ++typed;
                continue;
            }
            const bool counted = unmodeled_.contains(e.id.value());
            ++(counted ? unmodeled : unreached);
            out << (counted ? "unmodeled\t" : "unreached\t") << static_cast<unsigned>(e.kind)
                << '\t' << static_cast<unsigned>(e.marker) << '\t' << e.text << '\n';
        }
        out << "summary\t" << typed << '\t' << unmodeled << '\t' << unreached << '\t'
            << open_operands_ << '\n';
    }

    CompiledDesign& design_;
    const Regions& regions_;
    const Lookup& lookup_;
    diagnostic::Engine& diagnostics_;
    Types types_;
    Operations operations_;
    std::unordered_set<std::uint32_t> units_;
    std::unordered_map<std::uint32_t, std::size_t> expression_index_;
    std::unordered_map<std::uint32_t, DeclarationId> callables_;
    std::unordered_map<std::uint32_t, TypeId> physical_units_;
    std::unordered_map<std::uint32_t, Type> parameter_types_;
    std::unordered_map<std::uint32_t, std::vector<DeclarationId>> scope_declarations_;
    std::unordered_map<std::uint32_t, std::pair<support::SymbolId, Type>> generate_parameters_;
    std::unordered_map<std::uint32_t, Interpretations> interpretations_;
    std::unordered_set<std::uint32_t> typed_;
    std::unordered_set<std::uint32_t> unmodeled_;
    std::unordered_set<std::uint32_t> reported_;
    std::size_t open_operands_ { };
    unsigned quiet_ { };
    std::size_t quiet_errors_ { };
    bool failed_ { };
};

} // namespace

bool type_vhdl_expressions(CompiledDesign& design, const Regions& regions, const Lookup& lookup,
    const std::span<const UnitId> units, diagnostic::Engine& diagnostics)
{
    Typing typing { design, regions, lookup, diagnostics };
    return typing.run(units);
}

} // namespace fsim::semantic::vhdl_names
