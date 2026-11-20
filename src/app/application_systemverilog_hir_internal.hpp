// SPDX-License-Identifier: Apache-2.0
#pragma once


#include "fsim/frontend/class_specialization.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/design_core.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/frontend/systemverilog_scalars.hpp"
#include "fsim/frontend/token.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/support/symbol.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app::application_detail::systemverilog_hir_detail {

namespace sv = semantic::sv;

// Copy effective type properties for a named typedef use while retaining the
// selected typedef identity in reference.target.
// The kind of the callee `name` of a source call or task call: a system
// task or function when it is a system_tf_identifier, which begins with `$`
// (IEEE 1800-2023 5.6.3). Read here once, where the HIR is built.
[[nodiscard]] inline sv::CallKind systemverilog_call_kind(
    const std::string_view name) noexcept
{
    return name.starts_with('$') ? sv::CallKind::system : sv::CallKind::user;
}

// The callee kind of `input`: `system` for an unmarked node whose name is a
// system_tf_identifier (a call, or a system function named without an
// argument list, 20.3), `user` otherwise.
[[nodiscard]] inline sv::CallKind systemverilog_call_kind(
    const frontend::Expression& input) noexcept
{
    // The generated assertion monitor calls are system functions of the
    // generated assertion processes.
    if (support::sva_monitor_call(input.marker)) {
        return sv::CallKind::system;
    }
    return input.marker == support::ExpressionMarker::none
        ? systemverilog_call_kind(input.text)
        : sv::CallKind::user;
}

// The system task or function a `system` node of `input` names (IEEE
// 1800-2023 20.1, 21.1); none for a user-defined one and any other node.
[[nodiscard]] inline support::SystemTf systemverilog_system_tf(
    const frontend::Expression& input) noexcept
{
    return input.marker == support::ExpressionMarker::none
            && systemverilog_call_kind(input) == sv::CallKind::system
        ? support::system_tf_named(input.text)
        : support::SystemTf::none;
}

// The result a system function call of `input` has as the parser typed it
// (frontend Expression::call_result_width and call_result_domain), on the
// HIR node `output`; nothing for any other node.
inline void set_systemverilog_call_result(
    sv::Expression& output, const frontend::Expression& input) noexcept
{
    if (output.system_tf == support::SystemTf::none) {
        return;
    }
    switch (input.call_result_domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Boolean:
        output.call_result_values = sv::CallResultValues::two_state;
        break;
    case frontend::ValueDomain::Logic4:
    case frontend::ValueDomain::Integer:
        output.call_result_values = sv::CallResultValues::four_state;
        break;
    case frontend::ValueDomain::String:
        output.call_result_values = sv::CallResultValues::string;
        return;
    case frontend::ValueDomain::Logic9:
    case frontend::ValueDomain::Unknown:
        return;
    }
    output.call_result_width = static_cast<std::uint32_t>(input.call_result_width);
}

inline void inherit_named_systemverilog_type_shape(
    sv::TypeReference& reference, const sv::Hir& hir)
{
    if (!reference.target.target.valid())
        return;

    std::vector<semantic::TypeId> visited;
    auto target = reference.target.target;
    while (target.valid()
        && std::ranges::find(visited, target) == visited.end()) {
        visited.push_back(target);
        const auto* definition = hir.find_type(target);
        if (definition == nullptr)
            return;

        const auto& base = definition->base;
        const bool has_shape = base.value_form.has_value()
            || !base.class_identity.empty()
            || base.virtual_interface
            || !base.interface_type.empty()
            || !base.interface_modport.empty()
            || !base.systemverilog_net_type.empty()
            || !base.systemverilog_resolution_function.empty()
            || base.packed_range.has_value()
            || !base.packed_dimensions.empty()
            || base.signed_value
            || base.container_form.has_value()
            || !base.unpacked_dimensions.empty()
            || base.executable_width.has_value()
            || base.four_state;
        if (has_shape) {
            reference.value_form = base.value_form;
            if (!reference.value_form
                && definition->form != sv::TypeForm::unresolved) {
                reference.value_form = definition->form;
            }
            reference.class_identity = base.class_identity;
            reference.virtual_interface = base.virtual_interface;
            reference.interface_type = base.interface_type;
            reference.interface_modport = base.interface_modport;
            reference.systemverilog_net_type = base.systemverilog_net_type;
            reference.net_type_kind = base.net_type_kind;
            reference.systemverilog_resolution_function
                = base.systemverilog_resolution_function;
            reference.packed_range = base.packed_range;
            reference.packed_dimensions = base.packed_dimensions;
            reference.signed_value = base.signed_value;
            reference.container_form = base.container_form;
            reference.unpacked_dimensions = base.unpacked_dimensions;
            reference.executable_width = base.executable_width;
            reference.four_state = base.four_state;
            return;
        }
        if (!base.target.target.valid()) {
            if (definition->form != sv::TypeForm::unresolved)
                reference.value_form = definition->form;
            return;
        }
        target = base.target.target;
    }
}

// The HIR type form a frontend type spells.
[[nodiscard]] inline sv::TypeForm systemverilog_type_form(
    const frontend::Type& input) noexcept
{
    if (input.systemverilog_container) {
        switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
            return sv::TypeForm::dynamic_array;
        case frontend::SystemVerilogContainerKind::Queue:
            return sv::TypeForm::queue;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
            return sv::TypeForm::associative_array;
        case frontend::SystemVerilogContainerKind::StaticArray:
            return sv::TypeForm::static_array;
        }
    }
    switch (input.packed_aggregate) {
    case frontend::PackedAggregateKind::Struct:
        return sv::TypeForm::packed_structure;
    case frontend::PackedAggregateKind::Union:
        return sv::TypeForm::packed_union;
    case frontend::PackedAggregateKind::TaggedUnion:
        return sv::TypeForm::tagged_union;
    case frontend::PackedAggregateKind::UnpackedStruct:
        return sv::TypeForm::unpacked_structure;
    case frontend::PackedAggregateKind::UnpackedUnion:
        return sv::TypeForm::unpacked_union;
    case frontend::PackedAggregateKind::None:
        break;
    }
    if (!input.enumeration_literals.empty()) {
        return sv::TypeForm::enumeration;
    }
    if (input.domain == frontend::ValueDomain::String) {
        return sv::TypeForm::string;
    }
    return sv::TypeForm::packed_integral;
}

// Records the class member a class-operation call (sv_new_constructor,
// sv_method, ...) names, and the member's class when the expression has
// none.
inline void bind_systemverilog_class_operation(sv::Expression& output,
    const frontend::Expression& input)
{
    if (!support::sv_class_member_operation(input.marker)) {
        output.class_member_identity.clear();
        return;
    }
    output.class_member_identity = input.marker_argument;
    // The class the frontend bound the operation to (IEEE 1800-2023 8.5,
    // 8.7, 8.16), carried with the marker.
    if (output.class_identity.empty()) {
        output.class_identity = input.marker_owner;
    }
}

[[nodiscard]] inline std::optional<sv::DecimalLiteral>
systemverilog_decimal_literal(
    const std::optional<frontend::SystemVerilogDecimalLiteral>& literal)
{
    if (!literal) {
        return std::nullopt;
    }
    return sv::DecimalLiteral {
        literal->kind == frontend::SystemVerilogDecimalLiteralKind::Time
            ? sv::DecimalLiteralKind::time
            : sv::DecimalLiteralKind::real,
        literal->digits,
        literal->decimal_exponent,
        literal->time_unit,
    };
}

// The class and member symbols of `C::m` written with the unadorned name of
// class C (no parameter value assignment, IEEE 1800-2023 8.23, 8.25.1), as
// the parser recorded its components; none for any other name.
[[nodiscard]] inline std::optional<
    std::pair<support::SymbolId, support::SymbolId>>
systemverilog_unadorned_class_scoped_name(const frontend::Expression& input)
{
    const auto& components = input.name_components;
    if (input.systemverilog_class_selection != nullptr
        || components.size() != 2U || components[0].scoped
        || !components[1].scoped) {
        return std::nullopt;
    }
    // The parser's components, interned once (IEEE 1800-2023 5.6).
    return std::pair { support::systemverilog_symbol(components[0].name),
        support::systemverilog_symbol(components[1].name) };
}

// `C::T`, the unadorned name of parameterized class C and its type
// parameter T, written in C or in a class nested in it (its members and
// out-of-block declarations): T itself, as though named inside C (IEEE
// 1800-2023 8.23, 8.25.1). C is the nearest enclosing class body whose
// class's symbol is C's; T is then looked up in that body's declarations
// (`named(scope, symbol)`) among its type parameters. None when no
// enclosing class C declares a type parameter T. `class_symbol(scope)` is
// the symbol of the class whose body `scope` is (none for another scope).
template <typename ClassSymbol, typename Named>
[[nodiscard]] std::optional<semantic::DeclarationId>
systemverilog_enclosing_class_type_parameter(const support::SymbolId class_name,
    const support::SymbolId parameter_name, const semantic::ScopeId scope,
    const semantic::Model& model, const sv::Hir& hir,
    const ClassSymbol& class_symbol, Named&& named)
{
    if (!class_name.valid() || !parameter_name.valid()) {
        return std::nullopt;
    }
    for (auto visible = std::optional<semantic::ScopeId> { scope }; visible;
        visible = model.scopes()[visible->value()].parent) {
        if (class_symbol(*visible) != class_name) {
            continue;
        }
        for (const auto id : named(*visible, parameter_name)) {
            const auto* declaration = hir.find_declaration(id);
            if (declaration != nullptr
                && declaration->form == sv::DeclarationForm::type_parameter) {
                return id;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The class parameter actuals of a type, projected with the builder's
// `source(span)`, `type(type, span)` and `expression(expression)`.
template <typename Source, typename Type, typename Expression>
[[nodiscard]] std::vector<sv::ActualAssociation>
systemverilog_class_parameter_actuals(const frontend::Type& input,
    const Source& source, const Type& type, const Expression& expression)
{
    std::vector<sv::ActualAssociation> result;
    for (const auto& input_actual :
        input.systemverilog_class_parameter_actuals) {
        sv::ActualAssociation actual;
        actual.formal = input_actual.name;
        actual.source = source(input_actual.span);
        if (input_actual.type_actual) {
            actual.kind = sv::ActualKind::type;
            actual.type = type(*input_actual.type_actual, input_actual.span);
        } else if (input_actual.value.kind
            == frontend::ExpressionKind::Invalid) {
            actual.kind = sv::ActualKind::default_value;
        } else {
            actual.expression = expression(input_actual.value);
        }
        result.push_back(std::move(actual));
    }
    return result;
}

[[nodiscard]] sv::UnitKind unit_kind(frontend::UnitKind kind) noexcept;
[[nodiscard]] sv::Direction direction(frontend::PortDirection value) noexcept;
[[nodiscard]] sv::EdgeKind edge_kind(frontend::EdgeKind value) noexcept;
[[nodiscard]] sv::ConcurrentAssertionKind concurrent_assertion_kind(
    frontend::SystemVerilogConcurrentAssertionKind kind) noexcept;
[[nodiscard]] sv::AssertionRegion assertion_region(
    frontend::SystemVerilogAssertionRegion region) noexcept;
[[nodiscard]] sv::Lifetime lifetime(
    bool automatic, bool explicit_lifetime) noexcept;
[[nodiscard]] frontend::FunctionDeclaration class_function(
    const frontend::SystemVerilogClassMethod& input);
[[nodiscard]] frontend::TaskDeclaration class_task(
    const frontend::SystemVerilogClassMethod& input);

/// An omitted call argument, positional `f(, 1)` or named `f(.s())`,
/// takes the formal's default (IEEE 1800-2017 13.5.3). The returned flags
/// mark the operands to drop; `names` is replaced by the names of the kept
/// operands, with positional actuals after an omitted one named by their
/// formals. Nothing changes when the callee's formals are unknown.
inline std::vector<bool> omit_systemverilog_call_arguments(
    const frontend::Expression& input,
    const std::optional<semantic::sv::Name>& referenced_name,
    const semantic::sv::Hir& hir,
    std::vector<std::string>& names)
{
    std::vector<bool> omitted(input.operands.size());
    if (input.kind != frontend::ExpressionKind::Call
        || systemverilog_call_kind(input) == sv::CallKind::system
        || !referenced_name
        || referenced_name->overloads.size() != 1U
        || std::ranges::none_of(input.operands,
            [](const frontend::Expression& operand) {
                return operand.kind == frontend::ExpressionKind::Invalid;
            })) {
        return omitted;
    }
    const auto* callable
        = hir.find_declaration(referenced_name->overloads.front());
    if (callable == nullptr || !callable->callable) {
        return omitted;
    }
    std::vector<std::string> formal_names;
    for (const auto formal : callable->callable->formals) {
        const auto* record = hir.find_declaration(formal);
        if (record == nullptr) {
            return omitted;
        }
        formal_names.push_back(record->name);
    }
    // More actuals than formals, even empty ones, stay an error (13.5).
    if (input.operands.size() > formal_names.size()) {
        return omitted;
    }
    std::vector<std::string> spelled(input.operands.size());
    for (std::size_t index = 0U;
        index < spelled.size() && index < input.call_argument_names.size();
        ++index) {
        spelled[index] = input.call_argument_names[index];
    }
    std::vector<std::string> kept;
    bool shifted = false;
    std::size_t position = 0U;
    for (std::size_t index = 0U; index < spelled.size(); ++index) {
        const bool empty = input.operands[index].kind
            == frontend::ExpressionKind::Invalid;
        if (spelled[index].empty()) {
            if (empty) {
                shifted = true;
            } else if (shifted && position < formal_names.size()) {
                spelled[index] = formal_names[position];
            }
            ++position;
        }
        omitted[index] = empty;
        if (!empty) {
            kept.push_back(spelled[index]);
        }
    }
    names = std::move(kept);
    return omitted;
}

// Declarations by scope and interned name (IEEE 1800-2023 3.13, 5.6), and
// by id, extended over the declarations appended since the last lookup, in
// HIR order. The builders only append declarations and never rescope or
// rename one. Indexing interns a declaration's name once (its symbol).
class SystemVerilogScopeSymbols final {
public:
    explicit SystemVerilogScopeSymbols(sv::Hir& hir) noexcept
        : hir_ { hir }
    {
    }

    [[nodiscard]] std::span<const semantic::DeclarationId> named(
        const semantic::ScopeId scope, const support::SymbolId symbol)
    {
        extend();
        const auto in_scope = scopes_.find(scope.value());
        if (in_scope == scopes_.end() || !symbol.valid()) {
            return { };
        }
        const auto found = in_scope->second.by_symbol.find(symbol.index());
        return found == in_scope->second.by_symbol.end()
            ? std::span<const semantic::DeclarationId> { }
            : std::span<const semantic::DeclarationId> { found->second };
    }

    [[nodiscard]] std::span<const semantic::DeclarationId> in_scope(
        const semantic::ScopeId scope)
    {
        extend();
        const auto found = scopes_.find(scope.value());
        return found == scopes_.end()
            ? std::span<const semantic::DeclarationId> { }
            : std::span<const semantic::DeclarationId> { found->second.all };
    }

    // The interned name of a design unit, once per unit.
    [[nodiscard]] support::SymbolId unit_symbol(const sv::Unit& unit) const
    {
        const auto [found, inserted] = unit_symbols_.try_emplace(unit.id.value());
        if (inserted) {
            found->second = support::systemverilog_symbol(unit.name);
        }
        return found->second;
    }

    [[nodiscard]] sv::Declaration* by_id(const semantic::DeclarationId id)
    {
        extend();
        const auto found = positions_.find(id.value());
        return found == positions_.end()
            ? nullptr
            : &hir_.mutable_declarations()[found->second];
    }

private:
    struct Scope {
        std::vector<semantic::DeclarationId> all;
        std::unordered_map<std::uint32_t, std::vector<semantic::DeclarationId>>
            by_symbol;
    };

    void extend()
    {
        const auto& declarations = hir_.declarations();
        if (declarations.size() < indexed_) {
            scopes_.clear();
            positions_.clear();
            indexed_ = 0U;
        }
        if (indexed_ == declarations.size()) {
            return;
        }
        auto& mutable_declarations = hir_.mutable_declarations();
        for (; indexed_ < mutable_declarations.size(); ++indexed_) {
            auto& declaration = mutable_declarations[indexed_];
            if (!declaration.symbol.valid() && !declaration.name.empty()) {
                declaration.symbol = support::systemverilog_symbol(declaration.name);
            }
            auto& scope = scopes_[declaration.scope.value()];
            scope.all.push_back(declaration.id);
            scope.by_symbol[declaration.symbol.index()].push_back(declaration.id);
            positions_.try_emplace(declaration.id.value(), indexed_);
        }
    }

    sv::Hir& hir_;
    std::unordered_map<std::uint32_t, Scope> scopes_;
    std::unordered_map<std::uint32_t, std::size_t> positions_;
    std::size_t indexed_ { };
    mutable std::unordered_map<std::uint32_t, support::SymbolId> unit_symbols_;
};

// A written type name `T`, `p::T`, or `lib::p::T` as linked HIR spells a
// package of another library (IEEE 1800-2023 26.3), split once at its last
// `::` and interned (5.6: exact text). `whole` is the full text.
struct SystemVerilogTypeName {
    support::SymbolId whole;
    support::SymbolId qualifier;
    support::SymbolId simple;
};

[[nodiscard]] inline SystemVerilogTypeName systemverilog_type_name(
    const std::string_view spelling)
{
    SystemVerilogTypeName result;
    if (spelling.empty()) {
        return result;
    }
    result.whole = support::systemverilog_symbol(spelling);
    const auto separator = spelling.rfind("::");
    if (separator == std::string_view::npos) {
        result.simple = result.whole;
        return result;
    }
    result.qualifier = support::systemverilog_symbol(spelling.substr(0U, separator));
    result.simple = support::systemverilog_symbol(spelling.substr(separator + 2U));
    return result;
}

// Model types by scope and interned name, and by source and name; Model
// values and instances by scope, source and name; HIR packages by library
// and name. Extended over the records appended since the last lookup (the
// builders only append). Each name is interned once, when indexed.
class SystemVerilogModelSymbols final {
public:
    SystemVerilogModelSymbols(
        const semantic::Model& model, const sv::Hir& hir) noexcept
        : model_ { model }
        , hir_ { hir }
    {
    }

    // The types named `name` declared in `scope`, in model order.
    [[nodiscard]] std::span<const semantic::TypeId> named(
        const semantic::ScopeId scope, const support::SymbolId name)
    {
        extend_types();
        const auto found = by_scope_.find(key(scope.value(), name));
        return found == by_scope_.end()
            ? std::span<const semantic::TypeId> { }
            : std::span<const semantic::TypeId> { found->second };
    }

    // The types named `name` declared at `source`, in model order.
    [[nodiscard]] std::span<const semantic::TypeId> from_source(
        const semantic::SourceSpanId source, const support::SymbolId name)
    {
        extend_types();
        const auto found = by_source_.find(key(source.value(), name));
        return found == by_source_.end()
            ? std::span<const semantic::TypeId> { }
            : std::span<const semantic::TypeId> { found->second };
    }

    // The first package, in HIR order, that `reference` names from a unit
    // of `library`: a package of that library with that name, or one
    // spelled `library::package` (any library).
    [[nodiscard]] const sv::Unit* package(
        const support::SymbolId library, const support::SymbolId reference)
    {
        extend_units();
        const auto local = local_packages_.find(key(library.index(), reference));
        const auto qualified = qualified_packages_.find(reference.index());
        std::size_t position = hir_.units().size();
        if (local != local_packages_.end()) {
            position = local->second;
        }
        if (qualified != qualified_packages_.end()) {
            position = std::min(position, qualified->second);
        }
        return position == hir_.units().size() ? nullptr : &hir_.units()[position];
    }

    // The Model value or instance named `name` that the semantic pass
    // recorded in `scope` for the declaration at `source`.
    [[nodiscard]] semantic::ValueId value(const semantic::ScopeId scope,
        const semantic::SourceSpanId source, const support::SymbolId name)
    {
        const auto& values = model_.values();
        for (; values_indexed_ < values.size(); ++values_indexed_) {
            const auto& value = values[values_indexed_];
            values_.try_emplace(record_key(value.scope, value.source,
                                    support::systemverilog_symbol(value.name)),
                value.id);
        }
        const auto found = values_.find(record_key(scope, source, name));
        return found == values_.end() ? semantic::ValueId { } : found->second;
    }

    [[nodiscard]] semantic::InstanceId instance(const semantic::ScopeId scope,
        const semantic::SourceSpanId source, const support::SymbolId name)
    {
        const auto& instances = model_.instances();
        for (; instances_indexed_ < instances.size(); ++instances_indexed_) {
            const auto& instance = instances[instances_indexed_];
            instances_.try_emplace(record_key(instance.scope, instance.source,
                                       support::systemverilog_symbol(instance.name)),
                instance.id);
        }
        const auto found = instances_.find(record_key(scope, source, name));
        return found == instances_.end() ? semantic::InstanceId { } : found->second;
    }

    // The interned library name of a Model unit, once per unit.
    [[nodiscard]] support::SymbolId library(const semantic::Unit& unit)
    {
        const auto [found, inserted] = model_libraries_.try_emplace(unit.id.value());
        if (inserted) {
            found->second = support::systemverilog_symbol(unit.library);
        }
        return found->second;
    }

private:
    [[nodiscard]] static std::uint64_t key(
        const std::uint32_t owner, const support::SymbolId name) noexcept
    {
        return (static_cast<std::uint64_t>(owner) << 32U) | name.index();
    }

    struct RecordKey {
        std::uint32_t scope;
        std::uint32_t source;
        std::uint32_t name;
        bool operator==(const RecordKey&) const = default;
    };
    struct RecordKeyHash {
        std::size_t operator()(const RecordKey& key) const noexcept
        {
            return std::hash<std::uint64_t> { }(
                       (static_cast<std::uint64_t>(key.scope) << 32U) | key.source)
                ^ (std::hash<std::uint32_t> { }(key.name) * 0x9e3779b97f4a7c15ULL);
        }
    };
    [[nodiscard]] static RecordKey record_key(const semantic::ScopeId scope,
        const semantic::SourceSpanId source, const support::SymbolId name) noexcept
    {
        return { scope.value(), source.value(), name.index() };
    }

    void extend_types()
    {
        const auto& types = model_.types();
        if (types.size() < types_indexed_) {
            by_scope_.clear();
            by_source_.clear();
            types_indexed_ = 0U;
        }
        for (; types_indexed_ < types.size(); ++types_indexed_) {
            const auto& type = types[types_indexed_];
            if (type.name.empty()) {
                continue;
            }
            const auto name = support::systemverilog_symbol(type.name);
            by_scope_[key(type.scope.value(), name)].push_back(type.id);
            by_source_[key(type.source.value(), name)].push_back(type.id);
        }
    }

    void extend_units()
    {
        const auto& units = hir_.units();
        if (units.size() < units_indexed_) {
            local_packages_.clear();
            qualified_packages_.clear();
            units_indexed_ = 0U;
        }
        for (; units_indexed_ < units.size(); ++units_indexed_) {
            const auto& unit = units[units_indexed_];
            if (unit.kind != sv::UnitKind::package) {
                continue;
            }
            local_packages_.try_emplace(
                key(support::systemverilog_symbol(unit.library).index(),
                    support::systemverilog_symbol(unit.name)),
                units_indexed_);
            qualified_packages_.try_emplace(
                support::systemverilog_symbol(unit.library + "::" + unit.name).index(),
                units_indexed_);
        }
    }

    const semantic::Model& model_;
    const sv::Hir& hir_;
    std::unordered_map<std::uint64_t, std::vector<semantic::TypeId>> by_scope_;
    std::unordered_map<std::uint64_t, std::vector<semantic::TypeId>> by_source_;
    std::size_t types_indexed_ { };
    std::unordered_map<std::uint64_t, std::size_t> local_packages_;
    std::unordered_map<std::uint32_t, std::size_t> qualified_packages_;
    std::size_t units_indexed_ { };
    std::unordered_map<std::uint32_t, support::SymbolId> model_libraries_;
    std::unordered_map<RecordKey, semantic::ValueId, RecordKeyHash> values_;
    std::size_t values_indexed_ { };
    std::unordered_map<RecordKey, semantic::InstanceId, RecordKeyHash> instances_;
    std::size_t instances_indexed_ { };
};

// A named type visible from `scope` (IEEE 1800-2023 6.18, 26.3): declared
// in the scope or an enclosing one; `p::T` from package p (also through
// its exports, 26.6); otherwise imported into the owning unit before
// `source_order` (explicit imports hide wildcard ones). None when no or
// more than one type is found.
[[nodiscard]] semantic::TypeId find_systemverilog_hir_type(
    const semantic::Model& model,
    const sv::Hir& hir,
    SystemVerilogModelSymbols& symbols,
    semantic::ScopeId scope,
    const SystemVerilogTypeName& name,
    std::uint64_t source_order = 0U) noexcept;

// The type `package::simple` of the package `package` names from `scope`'s
// unit (26.3), as find_systemverilog_hir_type finds a qualified name.
[[nodiscard]] semantic::TypeId find_systemverilog_package_type(
    const semantic::Model& model,
    const sv::Hir& hir,
    SystemVerilogModelSymbols& symbols,
    semantic::ScopeId scope,
    support::SymbolId package,
    support::SymbolId simple) noexcept;

// A Name of `spelling` with its interned symbol (IEEE 1800-2023 5.6).
[[nodiscard]] inline sv::Name systemverilog_name(
    std::string spelling, const semantic::SourceSpanId source)
{
    sv::Name result;
    result.symbol = support::systemverilog_symbol(spelling);
    result.spelling = std::move(spelling);
    result.source = source;
    return result;
}

class SystemVerilogHirBuilder final {
public:
    SystemVerilogHirBuilder(
        semantic::Model& model,
        sv::Hir& hir,
        std::span<const frontend::SystemVerilogClassSpecialization>
            class_specializations);

    void add_design(const frontend::ParsedDesign& parsed);
    [[nodiscard]] std::vector<semantic::DeclarationId>
    add_statement_type_aliases(
        std::span<const frontend::TypeAliasDeclaration> aliases,
        std::span<const std::pair<semantic::ScopeId, sv::Import>> imports,
        semantic::ScopeId scope,
        semantic::OriginId parent);

private:
    // Keep anonymous enumerations from resolving to their hidden typedef
    // while projecting the typedef base or an object using the enum.
    bool suppress_hidden_anonymous_enum_typedef_ = false;
    // Only a typedef declaration base discards cached named-type shape: the
    // lexical TypeId is authoritative if that cache names another typedef.
    bool projecting_typedef_declaration_base_ = false;
    struct Pending {
        std::string_view physical_source;
        std::size_t offset { };
        std::size_t category { };
        std::size_t index { };
        std::function<semantic::DeclarationId()> build;
    };

    [[nodiscard]] semantic::SourceSpanId source(
        const frontend::SourceSpan& span);
    [[nodiscard]] semantic::OriginId origin(
        semantic::SourceSpanId span,
        semantic::OriginId parent,
        std::string_view detail);
    [[nodiscard]] semantic::UnitId scope_unit(
        semantic::ScopeId scope) const;
    [[nodiscard]] semantic::ScopeId nested_scope(
        semantic::ScopeId parent_scope,
        std::string_view name,
        semantic::SourceSpanId span,
        semantic::OriginId scope_origin,
        semantic::ScopeRole role = semantic::ScopeRole::named);
    [[nodiscard]] sv::Name name(
        std::string_view spelling,
        const frontend::SourceSpan& span,
        semantic::ScopeId scope,
        std::uint64_t source_order = 0U);
    [[nodiscard]] std::optional<semantic::ExpressionId> expression(
        const frontend::Expression& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] static sv::ExpressionKind expression_kind(
        const frontend::Expression& expression) noexcept;
    [[nodiscard]] semantic::TypeId find_type(
        semantic::ScopeId scope,
        std::string_view type_name,
        std::optional<semantic::SourceSpanId> exact_source = std::nullopt,
        std::uint64_t source_order = 0U)
        const noexcept;
    [[nodiscard]] semantic::ValueId find_value(
        semantic::ScopeId scope,
        std::string_view value_name,
        semantic::SourceSpanId exact_source) const noexcept;
    [[nodiscard]] sv::PackedRange packed_range(
        const frontend::PackedRange& range,
        semantic::SourceSpanId span) const;
    [[nodiscard]] static sv::ClassVisibility class_visibility(
        frontend::SystemVerilogClassVisibility visibility) noexcept;
    [[nodiscard]] static sv::ConstraintExpressionKind constraint_kind(
        const frontend::Expression& input) noexcept;
    [[nodiscard]] sv::ConstraintExpression constraint_expression(
        const frontend::Expression& input);
    [[nodiscard]] sv::TypeReference class_property_type(
        const frontend::Type& input,
        semantic::SourceSpanId type_source);
    [[nodiscard]] static bool owner_matches(
        std::string_view owner, std::string_view selected) noexcept;
    // The classes whose properties a member of `declaration_identity` names
    // in `specialization`: that class (unless from_base) and its bases.
    [[nodiscard]] std::vector<std::string_view> visible_property_owners(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view declaration_identity, bool from_base) const;
    [[nodiscard]] bool specializes_or_derives(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view declaration_identity) const;
    [[nodiscard]] std::optional<sv::ConstraintBinding> dotted_property_binding(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view spelling,
        semantic::SourceSpanId binding_source);
    [[nodiscard]] std::optional<sv::ConstraintBinding> property_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string spelling,
        semantic::SourceSpanId binding_source);
    /// A value parameter of a module, package or compilation unit that a
    /// class constraint names (IEEE 1800-2017 18.5).
    [[nodiscard]] std::optional<sv::ConstraintBinding> unit_parameter_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view spelling,
        semantic::SourceSpanId binding_source);
    /// A variable of the unit that declares the class, which a class
    /// constraint names as a state variable (IEEE 1800-2017 18.5).
    [[nodiscard]] std::optional<sv::ConstraintBinding> state_variable_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view spelling);
    void collect_unit_parameters(const frontend::ParsedDesign& parsed);
    [[nodiscard]] std::optional<sv::ConstraintBinding> parameter_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view spelling,
        semantic::SourceSpanId binding_source);
    [[nodiscard]] std::optional<sv::ConstraintBinding> method_binding(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string spelling,
        semantic::SourceSpanId binding_source);
    void resolve_constraint_expression(
        sv::ConstraintExpression& expression,
        const frontend::SystemVerilogClassDeclaration& declaration,
        std::optional<std::string_view> enclosing_foreach = std::nullopt);
    [[nodiscard]] semantic::ScopeId ensure_compilation_unit(
        const frontend::SourceSpan& span,
        std::string_view source_library,
        std::string_view compilation_unit_identity,
        frontend::StandardRevision standard_revision,
        std::string_view compatibility_profile,
        std::string_view canonical_parent);
    [[nodiscard]] semantic::ScopeId class_parent_scope(
        const frontend::SystemVerilogClassDeclaration& input);
    [[nodiscard]] std::string class_identity(
        const frontend::SystemVerilogClassDeclaration& input,
        semantic::ScopeId parent_scope) const;
    [[nodiscard]] sv::ClassRelation class_relation(
        const frontend::SystemVerilogClassBase& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_class(
        const frontend::SystemVerilogClassDeclaration& input,
        std::optional<semantic::ScopeId> parent_scope = std::nullopt,
        std::optional<semantic::DeclarationId> generate_owner = std::nullopt,
        std::string_view alternative_discriminator = {});
    void add_classes(
        const std::vector<frontend::SystemVerilogClassDeclaration>& inputs,
        std::optional<semantic::ScopeId> parent_scope = std::nullopt,
        std::optional<semantic::DeclarationId> generate_owner = std::nullopt,
        std::string_view alternative_discriminator = {});
    void synchronize_generate_class_ownership();
    void compose_constraints();

    [[nodiscard]] sv::PackedRange packed_range(
        const frontend::PackedRangeExpression& range,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::TypeReference type_reference(
        const frontend::Type& input,
        const frontend::SourceSpan& fallback,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void apply_named_type_dimensions(sv::TypeReference& output,
        const frontend::Type& input,
        const frontend::SourceSpan& fallback,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_declaration_record(
        semantic::ScopeId scope,
        sv::DeclarationForm form,
        semantic::DeclarationKind kind,
        std::string_view declaration_name,
        const frontend::SourceSpan& frontend_span,
        semantic::OriginId parent);
    [[nodiscard]] sv::Declaration& declaration(
        semantic::DeclarationId id);
    [[nodiscard]] semantic::TypeId ensure_type(
        std::string_view type_name,
        semantic::ScopeId scope,
        semantic::TypeKind kind,
        const sv::TypeReference& base,
        semantic::SourceSpanId span,
        semantic::OriginId declaration_origin);
    [[nodiscard]] semantic::ValueId ensure_value(
        std::string_view value_name,
        semantic::ScopeId scope,
        semantic::ValueKind kind,
        const sv::TypeReference& type,
        semantic::SourceSpanId span,
        semantic::OriginId declaration_origin);
    [[nodiscard]] semantic::DeclarationId add_type_declaration(
        const frontend::TypeAliasDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_packed_members(
        const frontend::Type& input,
        sv::TypeDefinition& output,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_type_payload(
        const frontend::TypeAliasDeclaration& input,
        sv::TypeDefinition& output,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    template <typename Input>
    [[nodiscard]] semantic::DeclarationId add_object(
        const Input& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        sv::DeclarationForm form,
        semantic::DeclarationKind declaration_kind,
        semantic::ValueKind value_kind,
        sv::Direction object_direction,
        const std::optional<frontend::Expression>& initializer);
    [[nodiscard]] semantic::DeclarationId add_parameter(
        const frontend::ParameterDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_signal(
        const frontend::SignalDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_variable(
        const frontend::VariableDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_function_argument(
        const frontend::FunctionArgument& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_task_argument(
        const frontend::TaskArgument& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    template <typename Range, typename Builder>
    void add_children(
        const Range& inputs,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<semantic::DeclarationId>& children,
        Builder builder)
    {
        for (const auto& input : inputs) {
            children.push_back((this->*builder)(input, scope, parent));
        }
    }
    [[nodiscard]] semantic::DeclarationId add_function(
        const frontend::FunctionDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_task(
        const frontend::TaskDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_modport(
        const frontend::SystemVerilogModport& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::Delay instance_delay(
        const frontend::Delay& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::TimingRecord timing_record(
        const frontend::VerilogSpecifyBlock& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::size_t index);
    [[nodiscard]] sv::SourceToken source_token(
        const frontend::Token& input);
  [[nodiscard]] std::vector<sv::SourceToken> source_tokens(
      const std::vector<frontend::Token>& input);
  [[nodiscard]] sv::CovergroupFormal covergroup_formal(
      const frontend::SystemVerilogCovergroupFormal& input);
  [[nodiscard]] sv::CovergroupOptionAssignment covergroup_option(
      const frontend::SystemVerilogCovergroupOptionAssignment& input);
  [[nodiscard]] sv::CoverageBinValue coverage_bin_value(
      const frontend::SystemVerilogCoverageBinValue& input);
  [[nodiscard]] sv::CoverageTransitionSequence coverage_transition(
      const frontend::SystemVerilogCoverageTransitionSequence& input);
  [[nodiscard]] sv::CoverageBin coverage_bin(
      const frontend::SystemVerilogCoverageBin& input,
      semantic::OriginId parent);
  [[nodiscard]] sv::CoverageItem coverage_item(
      const frontend::SystemVerilogCoverageDeclaration& input,
      semantic::ScopeId scope,
      semantic::OriginId parent);
  [[nodiscard]] sv::CovergroupDeclaration covergroup_declaration(
      const frontend::SystemVerilogCovergroupDeclaration& input,
      semantic::ScopeId scope,
      std::optional<semantic::OriginId> parent);
  [[nodiscard]] semantic::ScopeId coverage_scope(
      std::string_view owner_identity) const;
  [[nodiscard]] sv::CovergroupInstance covergroup_instance(
      const frontend::SystemVerilogCovergroupInstance& input,
      const frontend::SystemVerilogCovergroupInstanceSyntax& syntax);
  [[nodiscard]] sv::DpiDeclaration dpi_declaration(
        const frontend::SystemVerilogDpiDeclaration& input,
        semantic::ScopeId scope,
        std::optional<semantic::OriginId> parent);
    [[nodiscard]] sv::ClockingSkew clocking_skew(
        const frontend::SystemVerilogClockingSkew& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::ClockingBlock clocking_block(
        const frontend::SystemVerilogClockingBlock& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::SequenceRange sequence_range(
        const frontend::SystemVerilogSequenceRange& input);
    [[nodiscard]] sv::SequenceExpression sequence_expression(
        const frontend::SystemVerilogSequenceExpression& input);
    [[nodiscard]] sv::PropertyExpression property_expression(
        const frontend::SystemVerilogPropertyExpression& input);
    [[nodiscard]] sv::ConcurrentAssertion concurrent_assertion(
        const frontend::SystemVerilogConcurrentAssertion& input,
        std::size_t index,
        semantic::OriginId parent);
    [[nodiscard]] sv::AssertionDeclaration assertion_declaration(
        const frontend::SystemVerilogAssertionDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);

    [[nodiscard]] semantic::InstanceId ensure_instance(
        const frontend::Instance& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::ProcessId add_process_skeleton(
        const frontend::Process& input,
        semantic::ScopeId parent_scope,
        semantic::OriginId parent_origin);
    [[nodiscard]] sv::Alias add_alias(
        const frontend::SystemVerilogAliasDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::size_t index);
    [[nodiscard]] sv::LetDeclaration add_let(
        const frontend::SystemVerilogLetDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] std::optional<sv::Defparam> add_defparam(
        const frontend::VerilogDefparamDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_generate_body(
        const frontend::GenerateBody& input,
        sv::GenerateRegion& output);
    [[nodiscard]] sv::GenerateRegion add_generate(
        const frontend::GenerateRegion& input,
        semantic::ScopeId parent_scope,
        semantic::OriginId parent_origin,
        std::string alternative_discriminator = {});
    template <typename Input, typename Builder>
    void queue(
        const std::vector<Input>& inputs,
        std::size_t category,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<Pending>& pending,
        Builder builder);
    void queue_declarations(
        const frontend::GenerateBody& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<Pending>& pending);
    [[nodiscard]] static bool pending_less(
        const Pending& left, const Pending& right) noexcept;
    void add_unit(
        const frontend::DesignUnit& input, semantic::UnitId unit_id);

    semantic::Model& model_;
    sv::Hir& hir_;
    mutable SystemVerilogScopeSymbols scope_symbols_ { hir_ };
    mutable SystemVerilogModelSymbols model_symbols_ { model_, hir_ };
    // The genvar of each loop-generate block scope, which hides outer
    // declarations of its name inside the block (IEEE 1800-2017 27.4).
    std::map<semantic::ScopeId, support::SymbolId> genvar_scopes_;
    std::span<const frontend::SystemVerilogClassSpecialization>
        class_specializations_;
    std::span<const frontend::SystemVerilogImport> current_imports_;
    // Block imports are supplied nearest-scope first during local typedef
    // projection; each import retains its owning scope for lexical lookup.
    std::span<const std::pair<semantic::ScopeId, sv::Import>> statement_imports_;
    std::multimap<std::string, semantic::ScopeId, std::less<>> class_scopes_;
    // The symbol of the class whose body each scope is, recorded when the
    // body opens (before its members are built).
    std::unordered_map<std::uint32_t, support::SymbolId> class_body_symbols_;
    std::unordered_map<const frontend::Expression*, semantic::ExpressionId>
        expressions_;
    std::unordered_map<std::string, semantic::TypeId>
        anonymous_aggregate_types_;
    struct UnitParameter {
        std::string unit;
        frontend::Type type;
        // The default when it is a constant; a class of the unit reads the
        // value of its instance (IEEE 1800-2023 6.22), bound at run time.
        std::optional<std::uint64_t> value;
    };
    std::multimap<std::string, UnitParameter, std::less<>> unit_parameters_;
};

// The structural identity of an anonymous aggregate type in `scope`, or
// nothing when its shape is not fixed by literals.
[[nodiscard]] std::optional<std::string> anonymous_aggregate_signature(
    const frontend::Type& type, semantic::ScopeId scope);

} // namespace fsim::app::application_detail::systemverilog_hir_detail
