// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_vhdl_resolution.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/vhdl_standard_enumeration.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsim::semantic {
namespace {

using vhdl::ExpressionKind;

[[nodiscard]] std::string lowered(const std::string_view text)
{
    std::string result { text };
    std::ranges::transform(result, result.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

[[nodiscard]] std::string simple_name(const std::string_view spelling)
{
    const auto separator = spelling.find_last_of('.');
    return lowered(separator == std::string_view::npos
            ? spelling
            : spelling.substr(separator + 1U));
}

// A type: the root of a resolved subtype chain, or a predefined type by name.
// The pseudo types stand for the types of literals and aggregates, which only
// a context decides (IEEE 1076-2008 9.3.2, 9.3.3).
struct TypeKey {
    TypeId id;
    std::string builtin;

    [[nodiscard]] bool known() const noexcept
    {
        return id.valid() || !builtin.empty();
    }

    bool operator==(const TypeKey&) const = default;
};

constexpr std::string_view universal_integer { "universal_integer" };
constexpr std::string_view universal_real { "universal_real" };
constexpr std::string_view string_literal_type { "@string-literal" };
constexpr std::string_view aggregate_type { "@aggregate" };
constexpr std::string_view null_type { "@null" };

// The predefined types named by STD.STANDARD and IEEE.STD_LOGIC_1164. A
// subtype names its base type (5.2.6, 16.3, 16.7).
[[nodiscard]] std::string predefined_base(const std::string_view name)
{
    if (name == "natural" || name == "positive") {
        return "integer";
    }
    if (name == "delay_length") {
        return "time";
    }
    if (name == "std_logic") {
        return "std_ulogic";
    }
    // The IEEE library's STD_LOGIC_VECTOR is a resolved subtype of
    // STD_ULOGIC_VECTOR (IEEE 1076-2008 16.7).
    if (name == "std_logic_vector") {
        return "std_ulogic_vector";
    }
    return std::string { name };
}

[[nodiscard]] bool predefined_name(const std::string_view name)
{
    static constexpr std::array names {
        std::string_view { "boolean" }, std::string_view { "bit" },
        std::string_view { "character" }, std::string_view { "severity_level" },
        std::string_view { "integer" }, std::string_view { "natural" },
        std::string_view { "positive" }, std::string_view { "real" },
        std::string_view { "time" }, std::string_view { "delay_length" },
        std::string_view { "string" }, std::string_view { "bit_vector" },
        std::string_view { "boolean_vector" },
        std::string_view { "integer_vector" },
        std::string_view { "real_vector" }, std::string_view { "time_vector" },
        std::string_view { "std_ulogic" }, std::string_view { "std_logic" },
        std::string_view { "std_ulogic_vector" },
        std::string_view { "std_logic_vector" },
        // IEEE.NUMERIC_STD (16.8.5), whose declarations a design may not
        // retain.
        std::string_view { "unsigned" }, std::string_view { "signed" },
    };
    return std::ranges::find(names, name) != names.end()
        || support::find_vhdl_standard_enumeration(name) != nullptr;
}

// IEEE.NUMERIC_BIT's UNSIGNED and SIGNED are array types of BIT, distinct
// from IEEE.NUMERIC_STD's over STD_ULOGIC (IEEE 1076-2019 16.8.5).
[[nodiscard]] std::string_view numeric_bit_vector_name(
    const std::string_view spelling)
{
    const auto text = lowered(spelling);
    const auto names = [&](const std::string_view member) {
        const auto qualified = "numeric_bit." + std::string { member };
        return text == qualified || text.ends_with("." + qualified);
    };
    if (names("unsigned")) {
        return "bit_unsigned";
    }
    return names("signed") ? "bit_signed" : std::string_view { };
}

[[nodiscard]] std::string_view predefined_element(const std::string_view name)
{
    if (name == "string") {
        return "character";
    }
    if (name == "bit_vector" || name == "bit_unsigned"
        || name == "bit_signed") {
        return "bit";
    }
    if (name == "std_ulogic_vector" || name == "std_logic_vector"
        || name == "unsigned" || name == "signed") {
        return "std_ulogic";
    }
    if (name == "boolean_vector") {
        return "boolean";
    }
    if (name == "integer_vector") {
        return "integer";
    }
    if (name == "real_vector") {
        return "real";
    }
    if (name == "time_vector") {
        return "time";
    }
    return { };
}

[[nodiscard]] std::string_view standard_scalar_name(
    const CompiledVhdlStandardScalarIdentity identity)
{
    using Identity = CompiledVhdlStandardScalarIdentity;
    switch (identity) {
    case Identity::bit: return "bit";
    case Identity::boolean: return "boolean";
    case Identity::character: return "character";
    case Identity::file_open_kind: return "file_open_kind";
    case Identity::file_open_status: return "file_open_status";
    case Identity::range_direction: return "range_direction";
    case Identity::integer: return "integer";
    case Identity::real: return "real";
    case Identity::severity_level: return "severity_level";
    case Identity::time: return "time";
    case Identity::none: break;
    }
    return { };
}

[[nodiscard]] bool time_unit(const std::string_view unit)
{
    static constexpr std::array units {
        std::string_view { "fs" }, std::string_view { "ps" },
        std::string_view { "ns" }, std::string_view { "us" },
        std::string_view { "ms" }, std::string_view { "sec" },
        std::string_view { "min" }, std::string_view { "hr" },
    };
    return std::ranges::find(units, unit) != units.end();
}

[[nodiscard]] bool object_form(const vhdl::DeclarationForm form)
{
    using Form = vhdl::DeclarationForm;
    return form == Form::port || form == Form::signal
        || form == Form::constant || form == Form::variable
        || form == Form::generic_constant || form == Form::alias
        || form == Form::file;
}

class VhdlResolution final {
public:
    VhdlResolution(CompiledDesign& design,
        std::vector<vhdl::Expression>& expressions,
        std::vector<vhdl::Statement>& statements)
        : design_ { design }
        , trace_ { std::getenv("FSIM_VHDL_RESOLVE_TRACE") != nullptr }
        , expressions_ { &expressions }
        , statements_ { &statements }
    {
    }

    void run()
    {
        auto& expressions = *expressions_;
        if (trace_) {
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE pass expressions=%zu statements=%zu\n",
                expressions.size(), design_.vhdl_hir.statements().size());
        }
        for (std::size_t index = 0; index < statements_->size(); ++index) {
            (*statements_)[index].resolved_procedure.reset();
            statement_index_.emplace(
                (*statements_)[index].id.value(), index);
        }
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            expressions[index].resolved_declaration.reset();
            expressions[index].resolved_type.reset();
            expressions[index].resolved_builtin_type.clear();
            expression_index_.emplace(expressions[index].id.value(), index);
        }
        for (const auto& type : design_.vhdl_hir.types()) {
            for (const auto& literal : type.enumeration_literals) {
                literal_types_.emplace(literal.declaration.value(), type.id);
            }
            for (const auto& unit : type.physical_units) {
                const auto [entry, inserted] = physical_units_.emplace(
                    lowered(unit.name), type.id);
                if (!inserted && entry->second != type.id) {
                    // An ambiguous unit name stays undecided here.
                    entry->second = TypeId { };
                }
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            if (declaration.callable && declaration.nested_scope) {
                callable_scopes_.emplace(
                    declaration.nested_scope->value(), declaration.id);
            }
        }
        std::unordered_map<std::uint32_t, const vhdl::Statement*> loops;
        for (const auto& statement : design_.vhdl_hir.statements()) {
            if (statement.kind == vhdl::StatementKind::loop
                && statement.nested_scope && !statement.loop_variable.empty()
                && statement.loop_initial) {
                loops.emplace(statement.nested_scope->value(), &statement);
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            const auto found = loops.find(declaration.scope.value());
            if (found != loops.end()
                && lowered(declaration.name)
                    == lowered(found->second->loop_variable)) {
                loop_parameters_.emplace(
                    declaration.id.value(), found->second);
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            if (declaration.initializer && object_form(declaration.form)
                && declaration.subtype) {
                resolve(*declaration.initializer,
                    subtype_key(*declaration.subtype, declaration.scope));
            }
        }
        // The bounds of a range constraint on an enumeration subtype denote
        // literals of that type (IEEE 1076-2008 5.2.1, 9.3.2): '0' in
        // `subtype b is four_value range '0' to '1'` is four_value's '0'.
        const auto range_bounds = [&](const vhdl::SubtypeIndication& subtype,
                                      const ScopeId scope) {
            if (subtype.constraints.size() != 1U) {
                return;
            }
            const auto key = subtype_key(subtype, scope);
            const auto* root = key.id.valid() ? type(key.id) : nullptr;
            if (root == nullptr || root->form != vhdl::TypeForm::enumeration) {
                return;
            }
            const auto& range = subtype.constraints.front();
            for (const auto& bound :
                { range.left_expression, range.right_expression }) {
                if (bound) {
                    resolve(*bound, key);
                }
            }
        };
        for (const auto& type : design_.vhdl_hir.types()) {
            const auto* owner = declaration(type.declaration);
            if (owner == nullptr) {
                continue;
            }
            range_bounds(type.base, owner->scope);
            if (type.element_subtype) {
                range_bounds(*type.element_subtype, owner->scope);
            }
            for (const auto& element : type.record_elements) {
                range_bounds(element.subtype, owner->scope);
            }
        }
        for (const auto& declaration : design_.vhdl_hir.declarations()) {
            if (declaration.subtype) {
                range_bounds(*declaration.subtype, declaration.scope);
            }
            if (declaration.callable && declaration.callable->return_type) {
                range_bounds(
                    *declaration.callable->return_type, declaration.scope);
            }
        }
        for (const auto& statement : design_.vhdl_hir.statements()) {
            resolve_statement(statement);
        }
    }

private:
    // ---- Records -----------------------------------------------------------

    [[nodiscard]] const vhdl::Expression* expression(
        const ExpressionId id) const
    {
        const auto found = expression_index_.find(id.value());
        return found == expression_index_.end()
            ? nullptr
            : &(*expressions_)[found->second];
    }

    [[nodiscard]] const vhdl::Declaration* declaration(
        const DeclarationId id) const
    {
        const auto view = design_.find_declaration(id);
        return view ? view->vhdl : nullptr;
    }

    [[nodiscard]] const vhdl::TypeDefinition* type(const TypeId id) const
    {
        const auto view = design_.find_type(id);
        return view ? view->vhdl : nullptr;
    }

    [[nodiscard]] const CompiledDesignResolver* resolver(const ScopeId scope)
    {
        const auto* record = design_.find_scope(scope);
        if (record == nullptr) {
            return nullptr;
        }
        auto found = resolvers_.find(record->unit.value());
        if (found == resolvers_.end()) {
            found = resolvers_.emplace(record->unit.value(),
                CompiledDesignResolver { design_, record->unit }).first;
        }
        return &found->second;
    }

    void annotate(const ExpressionId id, const DeclarationId selected,
        const char* const reason)
    {
        const auto found = expression_index_.find(id.value());
        if (found == expression_index_.end()) {
            return;
        }
        auto& target = (*expressions_)[found->second];
        target.resolved_declaration = selected;
        if (trace_) {
            const auto* chosen = declaration(selected);
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE expression=%u text=%s selected=%u (%s) "
                "by=%s\n",
                id.value(), target.text.c_str(), selected.value(),
                chosen != nullptr ? chosen->name.c_str() : "?", reason);
        }
    }

    void annotate_type(const ExpressionId id, const TypeKey& key)
    {
        const auto found = expression_index_.find(id.value());
        if (found == expression_index_.end() || !key.known()) {
            return;
        }
        auto& target = (*expressions_)[found->second];
        if (key.id.valid()) {
            target.resolved_type = key.id;
        } else {
            target.resolved_builtin_type = key.builtin;
        }
        if (trace_) {
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE expression=%u text=%s type=%s/%u\n",
                id.value(), target.text.c_str(), key.builtin.c_str(),
                key.id.value());
        }
    }

    // ---- Types -------------------------------------------------------------

    [[nodiscard]] TypeKey builtin(const std::string_view name) const
    {
        return TypeKey { { }, predefined_base(name) };
    }

    [[nodiscard]] TypeKey type_key(const TypeId id)
    {
        const auto root = vhdl_root_type(design_, id);
        const auto* definition = type(root);
        if (definition == nullptr) {
            return { };
        }
        // A subtype of a predefined type whose declaration is not retained
        // has that type's identity (6.3).
        if (definition->form == vhdl::TypeForm::subtype
            || definition->form == vhdl::TypeForm::alias) {
            const auto* owner = declaration(definition->declaration);
            return owner == nullptr
                ? TypeKey { }
                : subtype_key(definition->base, owner->scope);
        }
        // A predefined type declared in STD.STANDARD or IEEE.STD_LOGIC_1164
        // keeps the identity it has where those packages are not retained.
        const auto name = lowered(definition->name);
        if (predefined_name(name)) {
            if (const auto* owner = declaration(definition->declaration)) {
                if (const auto* unit = design_.vhdl_scope_unit(owner->scope);
                    unit != nullptr
                    && ((lowered(unit->library) == "std"
                            && lowered(unit->name) == "standard")
                        || (lowered(unit->library) == "ieee"
                            && (lowered(unit->name) == "std_logic_1164"
                                || lowered(unit->name) == "numeric_std")))) {
                    return builtin(name);
                }
            }
        }
        return TypeKey { root, { } };
    }

    [[nodiscard]] TypeKey subtype_key(
        const vhdl::SubtypeIndication& subtype, const ScopeId scope)
    {
        if (subtype.type_mark.target.valid()) {
            return type_key(subtype.type_mark.target);
        }
        if (subtype.builtin_type != vhdl::BuiltinTypeIdentity::none) {
            return builtin(vhdl::builtin_type_name(subtype.builtin_type));
        }
        return named_type_key(subtype.type_mark.spelling, scope);
    }

    [[nodiscard]] TypeKey named_type_key(
        const std::string_view spelling, const ScopeId scope)
    {
        if (spelling.empty()) {
            return { };
        }
        if (const auto* lookup = resolver(scope)) {
            const auto identity
                = lookup->resolve_vhdl_type_identity(spelling, scope);
            if (identity.type) {
                return type_key(*identity.type);
            }
            if (identity.builtin_type != vhdl::BuiltinTypeIdentity::none) {
                return builtin(vhdl::builtin_type_name(identity.builtin_type));
            }
            if (identity.standard_scalar_type
                != CompiledVhdlStandardScalarIdentity::none) {
                return builtin(
                    standard_scalar_name(identity.standard_scalar_type));
            }
        }
        if (const auto bit_vector = numeric_bit_vector_name(spelling);
            !bit_vector.empty()) {
            return builtin(bit_vector);
        }
        const auto name = simple_name(spelling);
        if (predefined_name(name)) {
            return builtin(name);
        }
        // A type declared in an enclosing declarative region (12.1).
        std::unordered_set<std::uint32_t> seen;
        for (auto current = std::optional { scope };
             current && current->valid()
             && seen.insert(current->value()).second;) {
            if (const auto declared
                = design_.vhdl_declarations_named_in_scope(*current, name)) {
                for (const auto id : *declared) {
                    const auto* candidate = declaration(id);
                    if (candidate != nullptr && candidate->declared_type
                        && (candidate->form == vhdl::DeclarationForm::type
                            || candidate->form
                                == vhdl::DeclarationForm::subtype)) {
                        return type_key(*candidate->declared_type);
                    }
                }
            }
            const auto* record = design_.find_scope(*current);
            current = record != nullptr ? record->parent : std::nullopt;
        }
        return { };
    }

    [[nodiscard]] bool floating_scalar(const vhdl::TypeDefinition& definition)
    {
        if (!definition.scalar_range) {
            return false;
        }
        const auto real_bound = [&](const std::optional<ExpressionId> bound) {
            const auto* value = bound ? expression(*bound) : nullptr;
            return value != nullptr
                && value->kind == ExpressionKind::real_literal;
        };
        return real_bound(definition.scalar_range->left_expression)
            || real_bound(definition.scalar_range->right_expression);
    }

    [[nodiscard]] bool integer_class(const TypeKey& key)
    {
        if (!key.builtin.empty()) {
            return key.builtin == "integer" || key.builtin == universal_integer;
        }
        const auto* definition = type(key.id);
        return definition != nullptr
            && definition->form == vhdl::TypeForm::scalar
            && !floating_scalar(*definition);
    }

    [[nodiscard]] bool floating_class(const TypeKey& key)
    {
        if (!key.builtin.empty()) {
            return key.builtin == "real" || key.builtin == universal_real;
        }
        const auto* definition = type(key.id);
        return definition != nullptr
            && definition->form == vhdl::TypeForm::scalar
            && floating_scalar(*definition);
    }

    [[nodiscard]] bool array_class(const TypeKey& key)
    {
        if (!key.builtin.empty()) {
            return !predefined_element(key.builtin).empty();
        }
        const auto* definition = type(key.id);
        return definition != nullptr
            && definition->form == vhdl::TypeForm::array;
    }

    [[nodiscard]] bool record_class(const TypeKey& key)
    {
        const auto* definition = key.id.valid() ? type(key.id) : nullptr;
        return definition != nullptr
            && definition->form == vhdl::TypeForm::record;
    }

    [[nodiscard]] bool access_class(const TypeKey& key)
    {
        const auto* definition = key.id.valid() ? type(key.id) : nullptr;
        return definition != nullptr
            && definition->form == vhdl::TypeForm::access;
    }

    /// The type of record element `name` of a record type (8.3).
    [[nodiscard]] TypeKey member_key(const TypeKey& key,
        const std::string_view name)
    {
        const auto* definition = record_class(key) ? type(key.id) : nullptr;
        const auto* owner = definition != nullptr
            ? declaration(definition->declaration)
            : nullptr;
        if (owner == nullptr) {
            return { };
        }
        const auto wanted = lowered(name);
        const auto found = std::ranges::find_if(definition->record_elements,
            [&](const vhdl::RecordElement& element) {
                return lowered(element.name) == wanted;
            });
        return found != definition->record_elements.end()
            ? subtype_key(found->subtype, owner->scope)
            : TypeKey { };
    }

    [[nodiscard]] TypeKey element_key(const TypeKey& key)
    {
        if (!key.builtin.empty()) {
            const auto element = predefined_element(key.builtin);
            return element.empty() ? TypeKey { } : builtin(element);
        }
        const auto* definition = type(key.id);
        if (definition == nullptr || !definition->element_subtype) {
            return { };
        }
        const auto* owner = declaration(definition->declaration);
        return owner == nullptr
            ? TypeKey { }
            : subtype_key(*definition->element_subtype, owner->scope);
    }

    /// The index type of an array type's dimension (5.3.2.1).
    [[nodiscard]] TypeKey index_key(const TypeKey& key,
        const std::size_t dimension)
    {
        const auto* definition = key.builtin.empty() ? type(key.id) : nullptr;
        if (definition == nullptr
            || dimension >= definition->array_dimensions.size()) {
            return { };
        }
        const auto* owner = declaration(definition->declaration);
        const auto& index = definition->array_dimensions[dimension]
                                .index_subtype;
        if (index.selected) {
            if (const auto* named = declaration(*index.selected);
                named != nullptr && named->declared_type) {
                return type_key(*named->declared_type);
            }
        }
        return owner == nullptr
            ? TypeKey { }
            : named_type_key(index.text(), owner->scope);
    }

    /// Whether a value of type `actual` suits a context of type `target`:
    /// the same type, or a universal or literal type the target class
    /// accepts (9.3.2, 9.3.3, 9.3.6).
    [[nodiscard]] bool compatible(const TypeKey& target, const TypeKey& actual)
    {
        if (!target.known() || !actual.known() || target == actual) {
            return true;
        }
        const auto accepts = [&](const TypeKey& pseudo, const TypeKey& other) {
            if (pseudo.builtin == universal_integer) {
                return integer_class(other);
            }
            if (pseudo.builtin == universal_real) {
                return floating_class(other);
            }
            if (pseudo.builtin == string_literal_type) {
                return array_class(other);
            }
            if (pseudo.builtin == aggregate_type) {
                return array_class(other) || record_class(other);
            }
            if (pseudo.builtin == null_type) {
                return access_class(other);
            }
            return false;
        };
        return accepts(actual, target) || accepts(target, actual);
    }

    [[nodiscard]] bool any_compatible(
        const TypeKey& target, const std::vector<TypeKey>& actual)
    {
        return actual.empty()
            || std::ranges::any_of(actual, [&](const TypeKey& candidate) {
                   return compatible(target, candidate);
               });
    }

    // ---- Candidate types (bottom-up) ---------------------------------------

    struct Callable {
        DeclarationId key;
        const vhdl::Declaration* declaration { };
        // A subprogram instance, whose generic map gives the types of its
        // template's generic-type formals (4.9).
        const vhdl::Declaration* instance { };
    };

    /// The callable a resolution candidate denotes: the declaration itself,
    /// or for a subprogram instance its template with the instance's map.
    [[nodiscard]] std::optional<Callable> callable_of(
        const CompiledVhdlCallableResolution& candidate, const bool procedures)
    {
        const auto* key = declaration(candidate.key);
        const auto* body = declaration(candidate.body);
        const auto* profile = key != nullptr && key->callable ? key
            : body != nullptr && body->callable                ? body
                                                               : nullptr;
        if (profile == nullptr || profile->callable->function == procedures) {
            return std::nullopt;
        }
        const auto* instance = key != nullptr && key->package
                && (key->form == vhdl::DeclarationForm::generic_function_instance
                    || key->form
                        == vhdl::DeclarationForm::generic_procedure_instance)
            ? key
            : nullptr;
        return Callable { candidate.key, profile, instance };
    }

    /// The visible functions a call or name may denote (12.3, 12.4).
    [[nodiscard]] std::vector<Callable> functions(
        const vhdl::Expression& record, const bool procedures = false)
    {
        std::vector<Callable> result;
        if (!record.referenced_name) {
            return result;
        }
        const auto* lookup = resolver(record.scope);
        if (lookup == nullptr) {
            return result;
        }
        for (const auto& candidate
            : lookup->resolve_vhdl_callables(
                          *record.referenced_name, record.scope)
                  .candidates) {
            const auto callable = callable_of(candidate, procedures);
            if (callable
                && std::ranges::none_of(result, [&](const Callable& known) {
                       return known.key == candidate.key;
                   })) {
                result.push_back(*callable);
            }
        }
        if (result.empty()) {
            result = protected_methods(
                *record.referenced_name, record.scope, procedures);
        }
        return result;
    }

    /// The methods a selected name denotes through a protected object
    /// (IEEE 1076-2008 5.6.2): the body's subprograms of that simple name.
    [[nodiscard]] std::vector<Callable> protected_methods(
        const vhdl::Name& name, const ScopeId scope, const bool procedures)
    {
        std::vector<Callable> result;
        const auto text = name.text();
        const auto dot = text.rfind('.');
        const auto* lookup = resolver(scope);
        if (dot == std::string_view::npos || lookup == nullptr) {
            return result;
        }
        vhdl::Name prefix;
        prefix.spelling = std::string { text.substr(0, dot) };
        const auto object = lookup->resolve_vhdl(prefix, scope).unique();
        const auto* record = object ? declaration(*object) : nullptr;
        if (record == nullptr || !object_form(record->form)
            || !record->subtype) {
            return result;
        }
        const auto key = subtype_key(*record->subtype, record->scope);
        const auto* public_type = key.id.valid() ? type(key.id) : nullptr;
        if (public_type == nullptr
            || (public_type->form != vhdl::TypeForm::protected_type
                && public_type->form != vhdl::TypeForm::protected_body)) {
            return result;
        }
        const vhdl::TypeDefinition* body = nullptr;
        if (public_type->form == vhdl::TypeForm::protected_body) {
            body = public_type;
        } else {
            for (const auto& stored : design_.vhdl_hir.types()) {
                if (stored.form != vhdl::TypeForm::protected_body
                    || lowered(stored.name) != lowered(public_type->name)) {
                    continue;
                }
                if (body != nullptr && body->id != stored.id) {
                    return result;
                }
                body = &stored;
            }
        }
        if (body == nullptr) {
            return result;
        }
        const auto member = lowered(text.substr(dot + 1));
        for (const auto id : body->protected_members) {
            const auto* method = declaration(id);
            if (method != nullptr && method->callable
                && method->callable->function != procedures
                && method->callable->defined
                && lowered(method->name) == member) {
                result.push_back(Callable { id, method, nullptr });
            }
        }
        return result;
    }

    /// The type of `subtype`, declared in `scope`; in a subprogram instance
    /// a generic-type formal of its template has the actual type (4.9).
    [[nodiscard]] TypeKey instance_subtype_key(
        const vhdl::SubtypeIndication& subtype, const ScopeId scope,
        const vhdl::Declaration* const instance)
    {
        if (instance != nullptr && instance->package) {
            const auto mark = simple_name(subtype.type_mark.spelling);
            for (const auto& association : instance->package->generic_map) {
                if (!association.formal
                    || simple_name(association.formal->text()) != mark) {
                    continue;
                }
                if (association.type) {
                    return subtype_key(*association.type, instance->scope);
                }
                // A type actual may be retained as a name (6.5.7.2).
                if (const auto* actual = association.expression
                        ? expression(*association.expression)
                        : nullptr;
                    actual != nullptr && actual->kind == ExpressionKind::name) {
                    const auto key
                        = named_type_key(actual->text, instance->scope);
                    if (key.known()) {
                        return key;
                    }
                }
            }
        }
        return subtype_key(subtype, scope);
    }

    [[nodiscard]] TypeKey result_key(const vhdl::Declaration& callable,
        const vhdl::Declaration* const instance = nullptr)
    {
        return callable.callable && callable.callable->return_type
            ? instance_subtype_key(
                  *callable.callable->return_type, callable.scope, instance)
            : TypeKey { };
    }

    [[nodiscard]] TypeKey formal_key(const DeclarationId formal,
        const vhdl::Declaration* const instance = nullptr)
    {
        const auto* record = declaration(formal);
        if (record == nullptr || !record->subtype) {
            return { };
        }
        return instance_subtype_key(*record->subtype, record->scope, instance);
    }

    /// The formal each actual of a call associates with, positionally or by
    /// name, or nullopt when the call does not fit the profile (4.2.2.1).
    [[nodiscard]] std::optional<std::vector<DeclarationId>> associate(
        const vhdl::Declaration& callable,
        const std::span<const ExpressionId> actuals,
        const std::span<const std::string> names)
    {
        const auto& formals = callable.callable->formals;
        std::vector<DeclarationId> result;
        std::vector<bool> used(formals.size(), false);
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto name = index < names.size() ? lowered(names[index])
                                                   : std::string { };
            std::optional<std::size_t> formal;
            if (name.empty()) {
                if (index >= formals.size()) {
                    return std::nullopt;
                }
                formal = index;
            } else {
                for (std::size_t candidate = 0; candidate < formals.size();
                     ++candidate) {
                    const auto* record = declaration(formals[candidate]);
                    if (record != nullptr && lowered(record->name) == name) {
                        formal = candidate;
                        break;
                    }
                }
                if (!formal) {
                    return std::nullopt;
                }
            }
            if (used[*formal]) {
                return std::nullopt;
            }
            used[*formal] = true;
            result.push_back(formals[*formal]);
        }
        for (std::size_t index = 0; index < formals.size(); ++index) {
            const auto* record = declaration(formals[index]);
            if (!used[index]
                && (record == nullptr || !record->initializer)) {
                return std::nullopt;
            }
        }
        return result;
    }

    /// Whether `callable` applies to this call and gives a result of the
    /// context type (12.5). With `strict`, every actual and formal type must
    /// be known to this pass.
    [[nodiscard]] bool applicable(const Callable& callable,
        const vhdl::Expression& call, const std::optional<TypeKey>& expected,
        const bool strict = false)
    {
        return applicable(callable, call.operands, call.argument_names,
            expected, strict);
    }

    [[nodiscard]] bool applicable(const Callable& callable,
        const std::span<const ExpressionId> operands,
        const std::span<const std::string> names,
        const std::optional<TypeKey>& expected, const bool strict)
    {
        const auto formals
            = associate(*callable.declaration, operands, names);
        if (!formals) {
            return false;
        }
        for (std::size_t index = 0; index < formals->size(); ++index) {
            // An `open` actual leaves the formal its default (6.5.7.1).
            if (const auto* actual_record = expression(operands[index]);
                actual_record != nullptr
                && actual_record->kind == ExpressionKind::name
                && lowered(actual_record->text) == "open"
                && !(actual_record->referenced_name
                    && (actual_record->referenced_name->selected
                        || !actual_record->referenced_name->overloads
                                .empty()))) {
                const auto* formal_record = declaration((*formals)[index]);
                if (formal_record == nullptr || !formal_record->initializer) {
                    return false;
                }
                continue;
            }
            const auto formal
                = formal_key((*formals)[index], callable.instance);
            const auto& actual = candidates(operands[index]);
            if (strict && (!formal.known() || actual.empty())) {
                return false;
            }
            if (!any_compatible(formal, actual)) {
                return false;
            }
        }
        return !expected
            || compatible(*expected,
                result_key(*callable.declaration, callable.instance));
    }

    [[nodiscard]] bool function_call(const vhdl::Expression& record) const
    {
        return (record.kind == ExpressionKind::call
                   || record.kind == ExpressionKind::name)
            && record.referenced_name && !record.text.starts_with("@")
            && !record.text.starts_with("'");
    }

    /// The types an expression may have before its context is known (12.5).
    /// An empty result means the type is unknown to this pass.
    [[nodiscard]] const std::vector<TypeKey>& candidates(
        const ExpressionId id)
    {
        if (const auto found = candidates_.find(id.value());
            found != candidates_.end()) {
            return found->second;
        }
        candidates_.emplace(id.value(), std::vector<TypeKey> { });
        auto result = compute_candidates(id);
        auto& stored = candidates_[id.value()];
        stored = std::move(result);
        return stored;
    }

    void add(std::vector<TypeKey>& result, const TypeKey& key) const
    {
        if (key.known() && std::ranges::find(result, key) == result.end()) {
            result.push_back(key);
        }
    }

    [[nodiscard]] std::vector<TypeKey> declaration_types(
        const vhdl::Name& name)
    {
        std::vector<TypeKey> result;
        std::vector<DeclarationId> declarations = name.overloads;
        if (name.selected
            && std::ranges::find(declarations, *name.selected)
                == declarations.end()) {
            declarations.push_back(*name.selected);
        }
        for (const auto id : declarations) {
            const auto* record = declaration(id);
            if (record == nullptr) {
                return { };
            }
            if (record->form == vhdl::DeclarationForm::enumeration_literal) {
                const auto found = literal_types_.find(id.value());
                if (found == literal_types_.end()) {
                    return { };
                }
                add(result, type_key(found->second));
            } else if (record->callable && record->callable->function) {
                add(result, result_key(*record));
            } else if (const auto loop = loop_parameters_.find(id.value());
                loop != loop_parameters_.end()
                && loop_parameter_type(*loop->second).known()) {
                // The range decides a loop parameter's type, whatever subtype
                // the declaration carries (10.10).
                add(result, loop_parameter_type(*loop->second));
            } else if (object_form(record->form) && record->subtype) {
                add(result, subtype_key(*record->subtype, record->scope));
            } else {
                return { };
            }
        }
        return result;
    }

    /// A loop parameter has the type of its discrete range; a range of
    /// universal integers is INTEGER (IEEE 1076-2008 10.10, 5.3.2.2).
    [[nodiscard]] TypeKey loop_parameter_type(const vhdl::Statement& loop)
    {
        std::vector<TypeKey> bounds;
        for (const auto bound : { loop.loop_initial, loop.loop_limit }) {
            if (!bound) {
                continue;
            }
            const auto& types = candidates(*bound);
            if (types.size() != 1U) {
                return { };
            }
            bounds.push_back(types.front());
        }
        if (bounds.empty()) {
            return { };
        }
        std::optional<TypeKey> result;
        for (const auto& bound : bounds) {
            if (bound.builtin == universal_integer) {
                continue;
            }
            if (result && !(*result == bound)) {
                return { };
            }
            result = bound;
        }
        return result ? *result : builtin("integer");
    }

    /// The type a predefined attribute name gives (16.2).
    [[nodiscard]] std::vector<TypeKey> attribute_types(
        const vhdl::Expression& record)
    {
        const auto attribute = lowered(std::string_view { record.text }
                                           .substr(1U));
        if (attribute == "pos" || attribute == "length"
            || attribute == "ascending_index" || attribute == "count") {
            return { builtin(universal_integer) };
        }
        if (attribute == "image" || attribute == "instance_name"
            || attribute == "path_name" || attribute == "simple_name") {
            return { builtin("string") };
        }
        if (attribute == "event" || attribute == "active"
            || attribute == "stable" || attribute == "quiet"
            || attribute == "ascending" || attribute == "driving") {
            return { builtin("boolean") };
        }
        if (attribute != "low" && attribute != "high" && attribute != "left"
            && attribute != "right" && attribute != "val"
            && attribute != "succ" && attribute != "pred"
            && attribute != "leftof" && attribute != "rightof"
            && attribute != "value" && attribute != "base") {
            return { };
        }
        if (record.operands.empty()) {
            return { };
        }
        // The prefix is a type mark or an object of the type.
        const auto* prefix = expression(record.operands.front());
        if (prefix == nullptr || !prefix->referenced_name) {
            return { };
        }
        const auto& name = *prefix->referenced_name;
        if (name.selected) {
            if (const auto* declared = declaration(*name.selected);
                declared != nullptr && declared->declared_type
                && (declared->form == vhdl::DeclarationForm::type
                    || declared->form == vhdl::DeclarationForm::subtype)) {
                const auto key = type_key(*declared->declared_type);
                return key.known() ? std::vector { key }
                                   : std::vector<TypeKey> { };
            }
        }
        const auto named = named_type_key(name.spelling, prefix->scope);
        if (named.known()) {
            return { named };
        }
        return candidates(record.operands.front());
    }

    /// The types of a name: its declarations' types, or for `obj.m1.m2`
    /// the type of the selected record element (8.3); the dots before the
    /// object name a library or package.
    [[nodiscard]] std::vector<TypeKey> name_types(const vhdl::Expression& record)
    {
        auto result = declaration_types(*record.referenced_name);
        const auto text = std::string_view { record.text };
        const auto* lookup = resolver(record.scope);
        if (text.find('.') == std::string_view::npos || lookup == nullptr
            || result.size() > 1U) {
            return result;
        }
        // The longest prefix naming an object is the object; the rest
        // selects its elements.
        for (auto dot = text.rfind('.'); dot != std::string_view::npos;
             dot = dot == 0U ? std::string_view::npos
                             : text.rfind('.', dot - 1U)) {
            vhdl::Name prefix;
            prefix.spelling = std::string { text.substr(0U, dot) };
            const auto object = lookup->resolve_vhdl(prefix, record.scope)
                                    .unique();
            const auto* declared = object ? declaration(*object) : nullptr;
            if (declared == nullptr) {
                continue;
            }
            if (!object_form(declared->form) || !declared->subtype) {
                return result;
            }
            auto key = subtype_key(*declared->subtype, declared->scope);
            for (auto rest = text.substr(dot + 1U); key.known();) {
                const auto separator = rest.find('.');
                key = member_key(key, rest.substr(0U, separator));
                if (separator == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(separator + 1U);
            }
            // An access dereference or an unmodelled selection keeps the
            // name's own candidates.
            return key.known() ? std::vector { key } : result;
        }
        return result;
    }

    [[nodiscard]] std::vector<TypeKey> compute_candidates(
        const ExpressionId id)
    {
        const auto* record = expression(id);
        if (record == nullptr) {
            return { };
        }
        std::vector<TypeKey> result;
        switch (record->kind) {
        case ExpressionKind::integer_literal:
            return { builtin(universal_integer) };
        case ExpressionKind::real_literal:
            return { builtin(universal_real) };
        case ExpressionKind::boolean_literal:
            return { builtin("boolean") };
        case ExpressionKind::string_literal:
            return { builtin(string_literal_type) };
        case ExpressionKind::aggregate:
            return { builtin(aggregate_type) };
        case ExpressionKind::logic_literal: {
            if (record->referenced_name) {
                result = declaration_types(*record->referenced_name);
            }
            // CHARACTER and BIT need no retained declarations (16.3).
            add(result, builtin("character"));
            const auto text = std::string_view { record->text };
            if (text == "'0'" || text == "'1'" || text == "0"
                || text == "1") {
                add(result, builtin("bit"));
            }
            return result;
        }
        case ExpressionKind::name: {
            if (record->text == "@vhdl-null") {
                return { builtin(null_type) };
            }
            if (!record->referenced_name) {
                return result;
            }
            return name_types(*record);
        }
        case ExpressionKind::call: {
            constexpr std::string_view physical { "@vhdl-physical:" };
            constexpr std::string_view qualified { "@vhdl-qualified:" };
            // An external name has the subtype it declares (8.7).
            if (record->text == "@vhdl-external"
                && record->operands.size() == 2U) {
                constexpr std::string_view subtype_prefix { "@vhdl-subtype:" };
                const auto* subtype = expression(record->operands[1]);
                if (subtype != nullptr
                    && subtype->text.starts_with(subtype_prefix)) {
                    add(result, named_type_key(
                                    std::string_view { subtype->text }
                                        .substr(subtype_prefix.size()),
                                    record->scope));
                }
                return result;
            }
            if (record->text.starts_with(physical)) {
                const auto unit = lowered(std::string_view { record->text }
                                              .substr(physical.size()));
                if (const auto found = physical_units_.find(unit);
                    found != physical_units_.end()) {
                    if (found->second.valid()) {
                        add(result, type_key(found->second));
                    }
                    return result;
                }
                return time_unit(unit) ? std::vector { builtin("time") }
                                       : result;
            }
            if (record->text.starts_with(qualified)) {
                add(result, named_type_key(std::string_view { record->text }
                                               .substr(qualified.size()),
                    record->scope));
                return result;
            }
            if (record->text.starts_with("'")) {
                return attribute_types(*record);
            }
            // A record element selected from an indexed or called prefix.
            if (constexpr std::string_view member { "@vhdl-member:" };
                record->text.starts_with(member)
                && record->operands.size() == 1U) {
                const auto name
                    = std::string_view { record->text }.substr(member.size());
                for (const auto& prefix : candidates(record->operands.front())) {
                    add(result, member_key(prefix, name));
                }
                return result;
            }
            if (!function_call(*record)) {
                return result;
            }
            const auto visible = functions(*record);
            if (trace_) {
                std::fprintf(stderr,
                    "FSIM_VHDL_RESOLVE call expression=%u text=%s visible=%zu "
                    "overloads=%zu\n",
                    record->id.value(), record->text.c_str(), visible.size(),
                    record->referenced_name->overloads.size());
            }
            // A type mark applied to one operand is a type conversion (9.3.6).
            if (visible.empty() && record->operands.size() == 1U
                && declaration_types(*record->referenced_name).empty()) {
                const auto converted
                    = named_type_key(record->text, record->scope);
                if (converted.known()) {
                    add(result, converted);
                    return result;
                }
            }
            if (visible.empty()) {
                // An object name with an actual part is an indexed name, or
                // a slice when its argument is a range (8.4, 8.5).
                const auto prefix = name_types(*record);
                const bool range_argument = record->operands.size() == 1U
                    && [&] {
                           const auto* argument
                               = expression(record->operands.front());
                           return argument != nullptr
                               && (lowered(argument->text) == "'range"
                                   || lowered(argument->text)
                                       == "'reverse_range");
                       }();
                for (const auto& type : prefix) {
                    if (array_class(type)) {
                        add(result, range_argument ? type : element_key(type));
                    }
                }
                return result;
            }
            for (const auto& callable : visible) {
                if (applicable(callable, *record, std::nullopt)) {
                    add(result,
                        result_key(*callable.declaration, callable.instance));
                }
            }
            return result;
        }
        case ExpressionKind::index:
        case ExpressionKind::slice: {
            if (record->operands.empty()) {
                return result;
            }
            // An index by a range attribute is a slice (8.5).
            const bool range_index = record->kind == ExpressionKind::index
                && record->operands.size() == 2U && [&] {
                       const auto* argument = expression(record->operands[1]);
                       return argument != nullptr
                           && (lowered(argument->text) == "'range"
                               || lowered(argument->text) == "'reverse_range");
                   }();
            for (const auto& prefix : candidates(record->operands.front())) {
                add(result, record->kind == ExpressionKind::index
                            && !range_index
                        ? element_key(prefix)
                        : prefix);
            }
            return result;
        }
        case ExpressionKind::unary:
            if (record->operands.size() == 1U
                && (record->text == "-" || record->text == "+"
                    || record->text == "abs" || record->text == "not")) {
                return candidates(record->operands.front());
            }
            return result;
        case ExpressionKind::binary:
            return binary_candidates(*record);
        default:
            return result;
        }
    }

    [[nodiscard]] static bool relational(const std::string_view op)
    {
        return op == "=" || op == "/=" || op == "<" || op == "<="
            || op == ">" || op == ">=";
    }

    [[nodiscard]] std::vector<TypeKey> binary_candidates(
        const vhdl::Expression& record)
    {
        std::vector<TypeKey> result;
        if (record.operands.size() != 2U) {
            return result;
        }
        const auto op = lowered(record.text);
        const bool user_overloads
            = record.referenced_name && !functions(record).empty();
        if (relational(record.text) && !user_overloads) {
            return { builtin("boolean") };
        }
        // The operands and the result of these predefined operators have
        // one type (9.2.2, 9.2.6, 9.2.7); a physical operand is not modelled.
        const bool same_type = op == "+" || op == "-" || op == "and"
            || op == "or" || op == "nand" || op == "nor" || op == "xor"
            || op == "xnor";
        const bool multiplying
            = op == "*" || op == "/" || op == "mod" || op == "rem";
        const auto& left = candidates(record.operands.front());
        const auto& right = candidates(record.operands.back());
        if (left.empty() || right.empty()) {
            return result;
        }
        const auto physical = [&](const TypeKey& key) {
            const auto* definition = key.id.valid() ? type(key.id) : nullptr;
            return key.builtin == "time"
                || (definition != nullptr
                    && definition->form == vhdl::TypeForm::physical);
        };
        if (multiplying
            && (std::ranges::any_of(left, physical)
                || std::ranges::any_of(right, physical))) {
            return result;
        }
        for (const auto& candidate : left) {
            if (!any_compatible(candidate, right)) {
                continue;
            }
            if (relational(record.text)) {
                add(result, builtin("boolean"));
            } else if (same_type
                || (multiplying
                    && (integer_class(candidate)
                        || (floating_class(candidate)
                            && (op == "*" || op == "/"))))) {
                add(result, candidate);
            }
        }
        if (!user_overloads) {
            return result;
        }
        // A user overload adds its result type when it accepts the
        // operands. Where the predefined forms are not modelled, or an
        // overload's result type is unknown, the result stays unknown.
        if (!relational(record.text) && !same_type && !multiplying) {
            return { };
        }
        for (const auto& callable : functions(record)) {
            if (!applicable(callable, record, std::nullopt)) {
                continue;
            }
            const auto key
                = result_key(*callable.declaration, callable.instance);
            if (!key.known()) {
                return { };
            }
            add(result, key);
        }
        return result;
    }

    // ---- Resolution (top-down) ---------------------------------------------

    void resolve_call(const vhdl::Expression& record,
        const std::optional<TypeKey>& expected)
    {
        const auto visible = functions(record);
        std::vector<const Callable*> chosen;
        for (const auto& callable : visible) {
            if (applicable(callable, record, expected)) {
                chosen.push_back(&callable);
            }
        }
        if (trace_ && !visible.empty()) {
            for (const auto operand : record.operands) {
                const auto* argument = expression(operand);
                const auto& types = candidates(operand);
                std::fprintf(stderr,
                    "FSIM_VHDL_RESOLVE   argument=%u kind=%d text=%s "
                    "selected=%d overloads=%zu types=%zu first=%s/%u\n",
                    operand.value(),
                    argument != nullptr ? static_cast<int>(argument->kind)
                                        : -1,
                    argument != nullptr ? argument->text.c_str() : "?",
                    argument != nullptr && argument->referenced_name
                            && argument->referenced_name->selected
                        ? static_cast<int>(
                              argument->referenced_name->selected->value())
                        : -1,
                    argument != nullptr && argument->referenced_name
                        ? argument->referenced_name->overloads.size()
                        : 0U,
                    types.size(),
                    types.empty() ? "" : types.front().builtin.c_str(),
                    types.empty() ? 0U : types.front().id.value());
            }
            for (const auto& callable : visible) {
                const auto& formals = callable.declaration->callable->formals;
                for (const auto formal : formals) {
                    const auto key = formal_key(formal);
                    std::fprintf(stderr,
                        "FSIM_VHDL_RESOLVE   formal=%u type=%s/%u\n",
                        formal.value(), key.builtin.c_str(), key.id.value());
                }
            }
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE call expression=%u text=%s visible=%zu "
                "applicable=%zu expected=%s/%u\n",
                record.id.value(), record.text.c_str(), visible.size(),
                chosen.size(),
                expected ? expected->builtin.c_str() : "-",
                expected ? expected->id.value() : 0U);
        }
        // A sole visible function is recorded only when every actual's type
        // is known to fit, so that an inapplicable call stays an error.
        if (chosen.size() == 1U
            && (visible.size() > 1U
                || applicable(*chosen.front(), record, expected, true))) {
            annotate(record.id, chosen.front()->key,
                visible.size() > 1U ? "context" : "profile");
        }
        if (chosen.size() != 1U) {
            for (const auto operand : record.operands) {
                resolve(operand, std::nullopt);
            }
            return;
        }
        const auto formals = associate(*chosen.front()->declaration,
            record.operands, record.argument_names);
        for (std::size_t index = 0; index < record.operands.size(); ++index) {
            resolve(record.operands[index],
                formals ? std::optional { formal_key(
                              (*formals)[index], chosen.front()->instance) }
                        : std::nullopt);
        }
    }

    void resolve_literal(const vhdl::Expression& record,
        const std::optional<TypeKey>& expected)
    {
        if (!expected || !expected->known()) {
            return;
        }
        // A character literal takes the enumeration type of its context
        // (9.3.2).
        if (record.kind == ExpressionKind::logic_literal) {
            const auto& types = candidates(record.id);
            if (std::ranges::find(types, *expected) != types.end()) {
                annotate_type(record.id, *expected);
            }
        }
        if (!record.referenced_name) {
            return;
        }
        std::vector<DeclarationId> declarations
            = record.referenced_name->overloads;
        if (record.referenced_name->selected
            && std::ranges::find(declarations,
                   *record.referenced_name->selected)
                == declarations.end()) {
            declarations.push_back(*record.referenced_name->selected);
        }
        if (declarations.size() < 2U) {
            return;
        }
        std::optional<DeclarationId> chosen;
        for (const auto id : declarations) {
            const auto* candidate = declaration(id);
            if (candidate == nullptr
                || candidate->form
                    != vhdl::DeclarationForm::enumeration_literal) {
                continue;
            }
            const auto found = literal_types_.find(id.value());
            if (found == literal_types_.end()
                || !(type_key(found->second) == *expected)) {
                continue;
            }
            if (chosen) {
                return;
            }
            chosen = id;
        }
        if (chosen) {
            annotate(record.id, *chosen, "literal context");
        }
    }

    void resolve(const ExpressionId id, const std::optional<TypeKey>& expected)
    {
        if (!visited_.insert(id.value()).second) {
            return;
        }
        const auto* record = expression(id);
        if (record == nullptr) {
            return;
        }
        const auto known_expected = expected && expected->known()
            ? expected
            : std::optional<TypeKey> { };
        switch (record->kind) {
        case ExpressionKind::call:
            if (function_call(*record) && !functions(*record).empty()) {
                resolve_call(*record, known_expected);
                return;
            }
            if (record->text.starts_with("@vhdl-qualified:")
                && !record->operands.empty()) {
                const auto& types = candidates(id);
                resolve(record->operands.front(), types.size() == 1U
                        ? std::optional { types.front() }
                        : std::nullopt);
                for (std::size_t index = 1; index < record->operands.size();
                     ++index) {
                    resolve(record->operands[index], std::nullopt);
                }
                return;
            }
            break;
        case ExpressionKind::name:
            if (function_call(*record) && functions(*record).size() > 1U) {
                resolve_call(*record, known_expected);
                return;
            }
            resolve_literal(*record, known_expected);
            return;
        case ExpressionKind::logic_literal:
            resolve_literal(*record, known_expected);
            return;
        case ExpressionKind::unary:
            if (record->operands.size() == 1U
                && (record->text == "-" || record->text == "+"
                    || record->text == "abs" || record->text == "not")) {
                resolve(record->operands.front(), known_expected);
                return;
            }
            break;
        case ExpressionKind::binary:
            resolve_binary(*record, known_expected);
            return;
        case ExpressionKind::conditional:
            for (std::size_t index = 0; index < record->operands.size();
                 ++index) {
                // The condition comes first; both alternatives take the
                // context's type (9.3.8).
                resolve(record->operands[index],
                    index == 0U ? std::optional { builtin("boolean") }
                                : known_expected);
            }
            return;
        case ExpressionKind::aggregate:
            resolve_aggregate(*record, known_expected);
            return;
        case ExpressionKind::index: {
            // Each index takes its dimension's index type as its context
            // (8.4, 12.5), which selects an overloaded literal.
            if (record->operands.size() < 2U) {
                break;
            }
            const auto* prefix = expression(record->operands.front());
            if (prefix == nullptr
                || (function_call(*prefix) && !functions(*prefix).empty())) {
                break;
            }
            resolve(record->operands.front(), std::nullopt);
            const auto& prefixes = candidates(record->operands.front());
            const auto* array = prefixes.size() == 1U
                    && prefixes.front().builtin.empty()
                ? type(prefixes.front().id)
                : nullptr;
            for (std::size_t index = 1; index < record->operands.size();
                 ++index) {
                auto context = array != nullptr
                        && array->form == vhdl::TypeForm::array
                    ? index_key(prefixes.front(), index - 1U)
                    : TypeKey { };
                resolve(record->operands[index], context.known()
                        ? std::optional { std::move(context) }
                        : std::nullopt);
            }
            return;
        }
        default:
            break;
        }
        for (const auto operand : record->operands) {
            resolve(operand, std::nullopt);
        }
    }

    void resolve_binary(const vhdl::Expression& record,
        const std::optional<TypeKey>& expected)
    {
        if (record.operands.size() != 2U) {
            for (const auto operand : record.operands) {
                resolve(operand, std::nullopt);
            }
            return;
        }
        std::optional<TypeKey> operand_type;
        const auto op = lowered(record.text);
        bool user_overloads
            = record.referenced_name && !functions(record).empty();
        // An arithmetic operator no user overload accepts in its context
        // is the predefined operator of its integer operands' type (9.2.6,
        // 12.5): inside `function "+" (a, b : integer) return bit`,
        // `d := a + b` is the predefined "+". The operator's type records
        // that decision and types both operands.
        const bool arithmetic = op == "+" || op == "-" || op == "*"
            || op == "/" || op == "mod" || op == "rem" || op == "**";
        if (user_overloads && arithmetic
            && std::ranges::none_of(functions(record),
                [&](const Callable& callable) {
                    return applicable(callable, record, expected);
                })) {
            std::vector<TypeKey> common;
            for (const auto& left : candidates(record.operands.front())) {
                if (integer_class(left)
                    && any_compatible(left, candidates(record.operands.back()))
                    && !candidates(record.operands.back()).empty()
                    && (!expected || compatible(*expected, left))) {
                    add(common, left);
                }
            }
            // An operand that is itself an overloaded operator has no type
            // before its context gives one; an integer context does.
            if (common.empty() && expected && integer_class(*expected)
                && op != "**"
                && std::ranges::all_of(record.operands,
                    [&](const ExpressionId operand) {
                        return any_compatible(*expected, candidates(operand));
                    })) {
                add(common, *expected);
            }
            if (common.size() == 1U) {
                annotate_type(record.id, common.front());
                user_overloads = false;
                if (op != "**") {
                    operand_type = common.front();
                }
            }
        }
        // A relation in a BOOLEAN context that no user overload returning
        // BOOLEAN accepts is the predefined one (`if a = b` with a user
        // "=" returning BIT, IEEE 1076-2008 9.2.3, 12.5).
        if (user_overloads && relational(record.text) && expected
            && expected->builtin == "boolean"
            && std::ranges::none_of(functions(record),
                [&](const Callable& callable) {
                    return applicable(callable, record, expected);
                })) {
            annotate_type(record.id, *expected);
            user_overloads = false;
        }
        if (!user_overloads && relational(record.text)) {
            // Both operands of a predefined relation have one type (9.2.3).
            std::vector<TypeKey> common;
            for (const auto& left : candidates(record.operands.front())) {
                if (any_compatible(left, candidates(record.operands.back()))) {
                    add(common, left);
                }
            }
            const auto concrete = std::ranges::count_if(common,
                [](const TypeKey& key) {
                    return key.id.valid()
                        || (!key.builtin.empty() && key.builtin[0] != '@'
                            && key.builtin != universal_integer
                            && key.builtin != universal_real);
                });
            if (concrete == 1) {
                operand_type = *std::ranges::find_if(common,
                    [](const TypeKey& key) {
                        return key.id.valid()
                            || (!key.builtin.empty() && key.builtin[0] != '@'
                                && key.builtin != universal_integer
                                && key.builtin != universal_real);
                    });
            }
        } else if (!user_overloads && !operand_type
            && (op == "+" || op == "-" || op == "and" || op == "or"
                || op == "nand" || op == "nor" || op == "xor"
                || op == "xnor")) {
            operand_type = expected;
        }
        resolve(record.operands.front(), operand_type);
        resolve(record.operands.back(), operand_type);
    }

    void resolve_aggregate(const vhdl::Expression& record,
        const std::optional<TypeKey>& expected)
    {
        // An aggregate takes the composite type of its context (9.3.3.1),
        // a declared or a predefined array type.
        if (expected && (array_class(*expected) || record_class(*expected))) {
            annotate_type(record.id, *expected);
        }
        std::optional<TypeKey> element;
        if (expected && array_class(*expected)) {
            const auto key = element_key(*expected);
            if (key.known()) {
                element = key;
            }
        }
        // A record aggregate gives each element association its element's
        // type, by position or by name (9.3.3.2).
        const auto* definition = expected && record_class(*expected)
            ? type(expected->id)
            : nullptr;
        const auto* owner = definition != nullptr
            ? declaration(definition->declaration)
            : nullptr;
        const auto record_element = [&](const std::size_t position,
                                        const vhdl::AggregateAssociation& association)
            -> std::optional<TypeKey> {
            if (definition == nullptr || owner == nullptr) {
                return std::nullopt;
            }
            const auto& elements = definition->record_elements;
            if (association.positional()) {
                return position < elements.size()
                    ? std::optional { subtype_key(
                          elements[position].subtype, owner->scope) }
                    : std::nullopt;
            }
            const auto name = lowered(association.choice_spelling);
            const auto found = std::ranges::find_if(elements,
                [&](const vhdl::RecordElement& candidate) {
                    return lowered(candidate.name) == name;
                });
            return found != elements.end()
                ? std::optional { subtype_key(found->subtype, owner->scope) }
                : std::nullopt;
        };
        for (std::size_t position = 0;
             position < record.associations.size(); ++position) {
            const auto& association = record.associations[position];
            resolve(association.value, definition != nullptr
                    ? record_element(position, association)
                    : element);
            for (const auto choice : association.choices) {
                resolve(choice, std::nullopt);
            }
        }
        for (const auto operand : record.operands) {
            resolve(operand, element);
        }
    }

    // ---- Contexts ----------------------------------------------------------

    [[nodiscard]] std::optional<TypeKey> target_type(const ExpressionId id)
    {
        const auto& types = candidates(id);
        return types.size() == 1U ? std::optional { types.front() }
                                  : std::nullopt;
    }

    [[nodiscard]] std::optional<TypeKey> enclosing_result(ScopeId scope)
    {
        std::unordered_set<std::uint32_t> seen;
        while (scope.valid() && seen.insert(scope.value()).second) {
            if (const auto found = callable_scopes_.find(scope.value());
                found != callable_scopes_.end()) {
                const auto* callable = declaration(found->second);
                if (callable == nullptr || !callable->callable
                    || !callable->callable->function) {
                    return std::nullopt;
                }
                const auto key = result_key(*callable);
                return key.known() ? std::optional { key } : std::nullopt;
            }
            const auto* record = design_.find_scope(scope);
            if (record == nullptr || !record->parent) {
                return std::nullopt;
            }
            scope = *record->parent;
        }
        return std::nullopt;
    }

    /// A procedure call selects the procedure whose profile its actuals
    /// fit (12.5); a sole visible procedure is recorded only when every
    /// actual's type is known to fit.
    void resolve_procedure(const vhdl::Statement& statement)
    {
        const auto* lookup = resolver(statement.scope);
        if (lookup == nullptr) {
            return;
        }
        std::vector<Callable> visible;
        for (const auto& candidate
            : lookup->resolve_vhdl_callables(
                          statement.procedure, statement.scope)
                  .candidates) {
            const auto callable = callable_of(candidate, true);
            if (callable
                && std::ranges::none_of(visible, [&](const Callable& known) {
                       return known.key == candidate.key;
                   })) {
                visible.push_back(*callable);
            }
        }
        if (visible.empty()) {
            visible = protected_methods(
                statement.procedure, statement.scope, true);
        }
        if (visible.empty()) {
            return;
        }
        std::vector<ExpressionId> operands;
        std::vector<std::string> names;
        for (const auto& argument : statement.procedure_arguments) {
            operands.push_back(argument.actual);
            names.push_back(argument.formal ? std::string {
                                                  argument.formal->text() }
                                            : std::string { });
        }
        std::vector<const Callable*> chosen;
        for (const auto& callable : visible) {
            if (applicable(callable, operands, names, std::nullopt, false)) {
                chosen.push_back(&callable);
            }
        }
        if (trace_) {
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE procedure statement=%u visible=%zu "
                "applicable=%zu\n",
                statement.id.value(), visible.size(), chosen.size());
            for (const auto operand : operands) {
                const auto* argument = expression(operand);
                const auto& types = candidates(operand);
                std::fprintf(stderr,
                    "FSIM_VHDL_RESOLVE   argument=%u kind=%d text=%s "
                    "types=%zu first=%s/%u\n",
                    operand.value(),
                    argument != nullptr ? static_cast<int>(argument->kind)
                                        : -1,
                    argument != nullptr ? argument->text.c_str() : "?",
                    types.size(),
                    types.empty() ? "" : types.front().builtin.c_str(),
                    types.empty() ? 0U : types.front().id.value());
            }
        }
        if (chosen.size() != 1U
            || (visible.size() == 1U
                && !applicable(*chosen.front(), operands, names,
                    std::nullopt, true))) {
            return;
        }
        if (const auto found = statement_index_.find(statement.id.value());
            found != statement_index_.end()) {
            (*statements_)[found->second].resolved_procedure
                = chosen.front()->key;
            if (trace_) {
                std::fprintf(stderr,
                    "FSIM_VHDL_RESOLVE statement=%u procedure=%u (%s)\n",
                    statement.id.value(), chosen.front()->key.value(),
                    chosen.front()->declaration->name.c_str());
            }
        }
        const auto formals = associate(
            *chosen.front()->declaration, operands, names);
        for (std::size_t index = 0; formals && index < operands.size();
             ++index) {
            resolve(operands[index],
                formal_key((*formals)[index], chosen.front()->instance));
        }
    }

    void resolve_statement(const vhdl::Statement& statement)
    {
        using Kind = vhdl::StatementKind;
        const auto boolean = std::optional { builtin("boolean") };
        switch (statement.kind) {
        case Kind::signal_assignment:
        case Kind::variable_assignment:
        case Kind::force: {
            const auto target = statement.target
                ? target_type(*statement.target)
                : std::nullopt;
            if (trace_ && statement.target && !target) {
                const auto* unresolved = expression(*statement.target);
                std::fprintf(stderr,
                    "FSIM_VHDL_RESOLVE untyped-target=%u kind=%u text=%s "
                    "candidates=%zu\n",
                    statement.target->value(),
                    unresolved != nullptr
                        ? static_cast<unsigned>(unresolved->kind) : 0U,
                    unresolved != nullptr ? unresolved->text.c_str() : "",
                    candidates(*statement.target).size());
            }
            if (statement.target) {
                resolve(*statement.target, std::nullopt);
            }
            if (statement.value) {
                resolve(*statement.value, target);
            }
            for (const auto& element : statement.waveform) {
                resolve(element.value, target);
            }
            break;
        }
        case Kind::return_statement:
            if (statement.value) {
                resolve(*statement.value, enclosing_result(statement.scope));
            }
            break;
        case Kind::assertion:
        case Kind::report:
            if (statement.report) {
                resolve(*statement.report, builtin("string"));
            }
            if (statement.severity) {
                resolve(*statement.severity, builtin("severity_level"));
            }
            break;
        default:
            break;
        }
        if (statement.condition) {
            resolve(*statement.condition, boolean);
        }
        for (const auto expression_id : { statement.target, statement.value,
                 statement.loop_initial, statement.loop_limit,
                 statement.guard }) {
            if (expression_id) {
                resolve(*expression_id, std::nullopt);
            }
        }
        if (statement.kind == Kind::procedure_call) {
            resolve_procedure(statement);
        }
        for (const auto& argument : statement.procedure_arguments) {
            resolve(argument.actual, std::nullopt);
        }
        for (const auto& alternative : statement.alternatives) {
            for (const auto choice : alternative.choices) {
                resolve(choice, std::nullopt);
            }
        }
    }

    CompiledDesign& design_;
    bool trace_ { };
    std::vector<vhdl::Expression>* expressions_ { };
    std::vector<vhdl::Statement>* statements_ { };
    std::unordered_map<std::uint32_t, std::size_t> statement_index_;
    std::unordered_map<std::uint32_t, std::size_t> expression_index_;
    std::unordered_map<std::uint32_t, TypeId> literal_types_;
    std::unordered_map<std::uint32_t, DeclarationId> callable_scopes_;
    std::unordered_map<std::uint32_t, const vhdl::Statement*>
        loop_parameters_;
    std::unordered_map<std::string, TypeId> physical_units_;
    std::map<std::uint32_t, CompiledDesignResolver> resolvers_;
    std::unordered_map<std::uint32_t, std::vector<TypeKey>> candidates_;
    std::unordered_set<std::uint32_t> visited_;
};

} // namespace

bool resolve_vhdl_expression_types(CompiledDesign& design)
{
    if (design.vhdl_hir.expressions().empty()) {
        return true;
    }
    // Mutable access advances the HIR revision, so the lookup indexes the
    // resolver reads are refreshed before any lookup. The pass only writes
    // resolution results, which no index reads.
    auto& expressions = design.vhdl_hir.mutable_expressions();
    auto& statements = design.vhdl_hir.mutable_statements();
    try {
        design.refresh_lookup_indexes();
    } catch (...) {
        return false;
    }
    VhdlResolution { design, expressions, statements }.run();
    return true;
}

} // namespace fsim::semantic
