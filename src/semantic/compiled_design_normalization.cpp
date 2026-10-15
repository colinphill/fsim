// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_normalization.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <map>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::semantic {
namespace {

template <typename Id>
void add_unique(std::vector<Id>& values, const Id value)
{
    if (value.valid()
        && std::ranges::find(values, value) == values.end()) {
        values.push_back(value);
    }
}

void merge_dependencies(
    ResidualDependencies& output, const ResidualDependencies& input)
{
    for (const auto id : input.parameters) {
        add_unique(output.parameters, id);
    }
    for (const auto id : input.generics) {
        add_unique(output.generics, id);
    }
    for (const auto id : input.types) {
        add_unique(output.types, id);
    }
    for (const auto id : input.packages) {
        add_unique(output.packages, id);
    }
    output.hierarchy = output.hierarchy || input.hierarchy;
}

void canonicalize(ResidualDependencies& dependencies)
{
    std::ranges::sort(dependencies.parameters);
    std::ranges::sort(dependencies.generics);
    std::ranges::sort(dependencies.types);
    std::ranges::sort(dependencies.packages);
}

std::optional<UnitId> declaration_unit(
    const Model& model, const DeclarationId declaration)
{
    if (!declaration.valid()
        || declaration.value() >= model.declarations().size()) {
        return std::nullopt;
    }
    const auto scope = model.declarations().at(declaration.value()).scope;
    if (!scope.valid() || scope.value() >= model.scopes().size()) {
        return std::nullopt;
    }
    return model.scopes().at(scope.value()).unit;
}

void add_package_dependency(const Model& model,
    const DeclarationId declaration, ResidualDependencies& dependencies)
{
    const auto unit_id = declaration_unit(model, declaration);
    if (!unit_id || !unit_id->valid()
        || unit_id->value() >= model.units().size()) {
        return;
    }
    const auto kind = model.units().at(unit_id->value()).kind;
    if (kind == UnitKind::systemverilog_package
        || kind == UnitKind::vhdl_package) {
        add_unique(dependencies.packages, *unit_id);
    }
}

// Position by id of the records in one HIR vector, for the first record with
// each id (as std::ranges::find would return).
class RecordIndex {
public:
    template <typename Record>
    void build(const std::vector<Record>& records)
    {
        data_ = records.data();
        size_ = records.size();
        for (std::size_t position = 0; position < records.size(); ++position) {
            const auto& id = records[position].id;
            if (!id.valid()) {
                continue;
            }
            if (id.value() >= positions_.size()) {
                positions_.resize(std::size_t { id.value() } + 1U, absent);
            }
            if (positions_[id.value()] == absent) {
                positions_[id.value()] = static_cast<std::uint32_t>(position);
            }
        }
    }

    /// Nullopt when the index does not describe `records`.
    template <typename Record, typename Id>
    [[nodiscard]] std::optional<const Record*> find(
        const std::vector<Record>& records, const Id id) const
    {
        if (records.data() != data_ || records.size() != size_) {
            return std::nullopt;
        }
        if (!id.valid() || id.value() >= positions_.size()
            || positions_[id.value()] == absent) {
            return static_cast<const Record*>(nullptr);
        }
        const auto& record = records[positions_[id.value()]];
        if (record.id != id) {
            return std::nullopt;
        }
        return &record;
    }

private:
    static constexpr std::uint32_t absent
        = std::numeric_limits<std::uint32_t>::max();
    const void* data_ { };
    std::size_t size_ { };
    std::vector<std::uint32_t> positions_;
};

// normalize_compiled_design() changes HIR record contents but never adds,
// removes or reorders records or changes their ids, so it indexes them once;
// a linear search per lookup made normalization quadratic.
struct NormalizationIndexes {
    RecordIndex systemverilog_declarations;
    RecordIndex vhdl_declarations;
    RecordIndex systemverilog_expressions;
    RecordIndex vhdl_expressions;
    RecordIndex semantic_scopes;
    RecordIndex semantic_types;
};
thread_local const NormalizationIndexes* normalization_indexes = nullptr;

template <typename Record, typename Id>
const Record* find_record(const std::vector<Record>& records, const Id id,
    const RecordIndex* const index)
{
    if (index != nullptr) {
        if (const auto found = index->find(records, id)) {
            return *found;
        }
    }
    const auto found = std::ranges::find(records, id, &Record::id);
    return found == records.end() ? nullptr : &*found;
}

const sv::Declaration* find_declaration(
    const sv::Hir& hir, const DeclarationId id)
{
    return find_record(hir.declarations(), id,
        normalization_indexes == nullptr
            ? nullptr : &normalization_indexes->systemverilog_declarations);
}

const vhdl::Declaration* find_declaration(
    const vhdl::Hir& hir, const DeclarationId id)
{
    return find_record(hir.declarations(), id,
        normalization_indexes == nullptr
            ? nullptr : &normalization_indexes->vhdl_declarations);
}

void classify(const CompiledDesign& design, const DeclarationId id,
    ResidualDependencies& dependencies)
{
    add_package_dependency(design.semantics, id, dependencies);
    if (const auto* declaration
        = find_declaration(design.systemverilog_hir, id)) {
        switch (declaration->form) {
        case sv::DeclarationForm::parameter:
        case sv::DeclarationForm::local_parameter:
            add_unique(dependencies.parameters, id);
            return;
        case sv::DeclarationForm::type_parameter:
            if (declaration->declared_type) {
                add_unique(dependencies.types, *declaration->declared_type);
            }
            return;
        default:
            return;
        }
    }
    if (const auto* declaration = find_declaration(design.vhdl_hir, id)) {
        switch (declaration->form) {
        case vhdl::DeclarationForm::generic_constant:
        case vhdl::DeclarationForm::generic_function:
        case vhdl::DeclarationForm::generic_procedure:
        case vhdl::DeclarationForm::generic_package:
            add_unique(dependencies.generics, id);
            return;
        case vhdl::DeclarationForm::generic_type:
            add_unique(dependencies.generics, id);
            if (declaration->declared_type) {
                add_unique(dependencies.types, *declaration->declared_type);
            }
            return;
        default:
            return;
        }
    }
}

template <typename Name>
void add_name_dependencies(const CompiledDesign& design,
    const std::optional<Name>& name, ResidualDependencies& dependencies)
{
    if (!name) {
        return;
    }
    if (name->selected) {
        classify(design, *name->selected, dependencies);
    }
    for (const auto declaration : name->overloads) {
        classify(design, declaration, dependencies);
    }
    if (!name->selected && name->overloads.empty()) {
        dependencies.hierarchy = true;
    }
}

bool dependency_free(const ResidualDependencies& dependencies)
{
    return dependencies.parameters.empty()
        && dependencies.generics.empty()
        && dependencies.types.empty()
        && dependencies.packages.empty()
        && !dependencies.hierarchy;
}

bool literal(const sv::ExpressionKind kind)
{
    return kind == sv::ExpressionKind::integer_literal
        || kind == sv::ExpressionKind::boolean_literal
        || kind == sv::ExpressionKind::logic_literal
        || kind == sv::ExpressionKind::string_literal
        || kind == sv::ExpressionKind::class_null;
}

bool literal(const vhdl::ExpressionKind kind)
{
    return kind == vhdl::ExpressionKind::integer_literal
        || kind == vhdl::ExpressionKind::real_literal
        || kind == vhdl::ExpressionKind::boolean_literal
        || kind == vhdl::ExpressionKind::logic_literal
        || kind == vhdl::ExpressionKind::string_literal;
}

template <typename Expression>
const Expression* expression_for(
    const std::vector<Expression>& expressions, const ExpressionId id)
{
    const RecordIndex* index = nullptr;
    if (normalization_indexes != nullptr) {
        index = std::is_same_v<Expression, sv::Expression>
            ? &normalization_indexes->systemverilog_expressions
            : &normalization_indexes->vhdl_expressions;
    }
    return find_record(expressions, id, index);
}

enum class ConstantKind : std::uint8_t {
    integer,
    boolean,
};

struct ConstantValue {
    ConstantKind kind { ConstantKind::integer };
    std::int64_t integer { };
    bool boolean { };
};

std::string lowercase(const std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(character))));
    }
    return result;
}

std::optional<std::int64_t> plain_integer(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    bool positive = false;
    if (text.front() == '+') {
        positive = true;
        text.remove_prefix(1);
        if (text.empty()) {
            return std::nullopt;
        }
    }
    std::int64_t result { };
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), result, 10);
    if (error != std::errc { } || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return positive && result < 0 ? std::nullopt
                                  : std::optional { result };
}

std::optional<bool> plain_boolean(const std::string_view text)
{
    const auto canonical = lowercase(text);
    if (canonical == "true" || canonical == "1") {
        return true;
    }
    if (canonical == "false" || canonical == "0") {
        return false;
    }
    return std::nullopt;
}

template <typename Expression>
std::optional<ConstantValue> constant_value(const Expression& expression)
{
    using Kind = std::remove_cvref_t<decltype(expression.kind)>;
    if (!expression.nominal_type.empty()) {
        return std::nullopt;
    }
    if (expression.kind == Kind::integer_literal) {
        const auto value = plain_integer(expression.text);
        return value ? std::optional { ConstantValue {
                           ConstantKind::integer, *value, false } }
                     : std::nullopt;
    }
    if (expression.kind == Kind::boolean_literal) {
        const auto value = plain_boolean(expression.text);
        return value ? std::optional { ConstantValue {
                           ConstantKind::boolean, 0, *value } }
                     : std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::int64_t> checked_add(
    const std::int64_t left, const std::int64_t right)
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > maximum - right)
        || (right < 0 && left < minimum - right)) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checked_subtract(
    const std::int64_t left, const std::int64_t right)
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left < minimum + right)
        || (right < 0 && left > maximum + right)) {
        return std::nullopt;
    }
    return left - right;
}

std::optional<std::int64_t> checked_multiply(
    const std::int64_t left, const std::int64_t right)
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (left == 0 || right == 0) {
        return 0;
    }
    if ((left == -1 && right == minimum)
        || (right == -1 && left == minimum)) {
        return std::nullopt;
    }
    if (left > 0) {
        if ((right > 0 && left > maximum / right)
            || (right < 0 && right < minimum / left)) {
            return std::nullopt;
        }
    } else if ((right > 0 && left < minimum / right)
        || (right < 0 && left < maximum / right)) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<std::int64_t> checked_divide(
    const std::int64_t left, const std::int64_t right)
{
    if (right == 0
        || (left == std::numeric_limits<std::int64_t>::min()
            && right == -1)) {
        return std::nullopt;
    }
    return left / right;
}

std::optional<std::int64_t> checked_remainder(
    const std::int64_t left, const std::int64_t right)
{
    if (right == 0
        || (left == std::numeric_limits<std::int64_t>::min()
            && right == -1)) {
        return std::nullopt;
    }
    return left % right;
}

std::optional<std::int64_t> checked_modulus(
    const std::int64_t left, const std::int64_t right)
{
    const auto remainder = checked_remainder(left, right);
    if (!remainder) {
        return std::nullopt;
    }
    if (*remainder == 0 || (*remainder < 0) == (right < 0)) {
        return remainder;
    }
    return checked_add(*remainder, right);
}

// Operator designators that a VHDL design overloads outside the STD and
// IEEE libraries. Such an operator is not folded as predefined: `abs 10`
// may call a user `"abs"` returning an enumeration.
const std::set<std::string>& vhdl_user_operator_overloads(
    const CompiledDesign& design)
{
    thread_local const CompiledDesign* cached_design { };
    thread_local std::uint64_t cached_revision { };
    thread_local std::set<std::string> cached;
    if (cached_design == &design && cached_revision == design.vhdl_hir.revision()) {
        return cached;
    }
    cached_design = &design;
    cached_revision = design.vhdl_hir.revision();
    cached.clear();
    std::map<std::uint32_t, const vhdl::Unit*> units;
    for (const auto& unit : design.vhdl_hir.units()) {
        units.emplace(unit.id.value(), &unit);
    }
    const auto& scopes = design.semantics.scopes();
    for (const auto& declaration : design.vhdl_hir.declarations()) {
        if (!declaration.callable || !declaration.callable->function
            || !declaration.scope.valid()
            || declaration.scope.value() >= scopes.size()) {
            continue;
        }
        const auto unit = units.find(scopes[declaration.scope.value()].unit.value());
        if (unit == units.end()
            || !unit->second->standard_package_revision.empty()
            || lowercase(unit->second->library) == "ieee"
            || lowercase(unit->second->library) == "std") {
            continue;
        }
        auto name = lowercase(declaration.name);
        if (name.size() >= 2U && name.front() == '"' && name.back() == '"') {
            name = name.substr(1U, name.size() - 2U);
        }
        cached.insert(std::move(name));
    }
    return cached;
}

std::optional<ConstantValue> fold_unary(
    const std::string_view operation, const ConstantValue operand)
{
    if (operand.kind == ConstantKind::integer) {
        if (operation == "+") {
            return operand;
        }
        if (operation == "-") {
            const auto value = checked_subtract(0, operand.integer);
            return value ? std::optional { ConstantValue {
                               ConstantKind::integer, *value, false } }
                         : std::nullopt;
        }
        if (operation == "abs") {
            if (operand.integer >= 0) {
                return operand;
            }
            const auto value = checked_subtract(0, operand.integer);
            return value ? std::optional { ConstantValue {
                               ConstantKind::integer, *value, false } }
                         : std::nullopt;
        }
        return std::nullopt;
    }
    if (operation == "!" || operation == "not") {
        return ConstantValue {
            ConstantKind::boolean, 0, !operand.boolean };
    }
    return std::nullopt;
}

std::optional<ConstantValue> fold_integers(
    const std::string_view operation,
    const std::int64_t left, const std::int64_t right)
{
    std::optional<std::int64_t> value;
    if (operation == "+") {
        value = checked_add(left, right);
    } else if (operation == "-") {
        value = checked_subtract(left, right);
    } else if (operation == "*") {
        value = checked_multiply(left, right);
    } else if (operation == "/") {
        value = checked_divide(left, right);
    } else if (operation == "%" || operation == "rem") {
        value = checked_remainder(left, right);
    } else if (operation == "mod") {
        value = checked_modulus(left, right);
    } else if (operation == "<") {
        return ConstantValue { ConstantKind::boolean, 0, left < right };
    } else if (operation == "<=") {
        return ConstantValue { ConstantKind::boolean, 0, left <= right };
    } else if (operation == ">") {
        return ConstantValue { ConstantKind::boolean, 0, left > right };
    } else if (operation == ">=") {
        return ConstantValue { ConstantKind::boolean, 0, left >= right };
    } else if (operation == "==" || operation == "=") {
        return ConstantValue { ConstantKind::boolean, 0, left == right };
    } else if (operation == "!=" || operation == "/=") {
        return ConstantValue { ConstantKind::boolean, 0, left != right };
    } else {
        return std::nullopt;
    }
    return value ? std::optional { ConstantValue {
                       ConstantKind::integer, *value, false } }
                 : std::nullopt;
}

std::optional<ConstantValue> fold_booleans(
    const std::string_view operation, const bool left, const bool right)
{
    if (operation == "&&" || operation == "and") {
        return ConstantValue { ConstantKind::boolean, 0, left && right };
    }
    if (operation == "||" || operation == "or") {
        return ConstantValue { ConstantKind::boolean, 0, left || right };
    }
    if (operation == "xor") {
        return ConstantValue { ConstantKind::boolean, 0, left != right };
    }
    if (operation == "xnor") {
        return ConstantValue { ConstantKind::boolean, 0, left == right };
    }
    if (operation == "nand") {
        return ConstantValue { ConstantKind::boolean, 0, !(left && right) };
    }
    if (operation == "nor") {
        return ConstantValue { ConstantKind::boolean, 0, !(left || right) };
    }
    if (operation == "==" || operation == "=") {
        return ConstantValue { ConstantKind::boolean, 0, left == right };
    }
    if (operation == "!=" || operation == "/=") {
        return ConstantValue { ConstantKind::boolean, 0, left != right };
    }
    return std::nullopt;
}

std::optional<ConstantValue> fold_binary(
    const std::string_view operation,
    const ConstantValue left, const ConstantValue right)
{
    if (left.kind != right.kind) {
        return std::nullopt;
    }
    if (left.kind == ConstantKind::integer) {
        return fold_integers(operation, left.integer, right.integer);
    }
    return fold_booleans(operation, left.boolean, right.boolean);
}

using ConstantBindings
    = std::vector<std::pair<DeclarationId, ConstantValue>>;

const ConstantValue* bound_value(
    const ConstantBindings& bindings, const DeclarationId declaration)
{
    const auto found = std::ranges::find_if(bindings,
        [&](const auto& binding) { return binding.first == declaration; });
    return found == bindings.end() ? nullptr : &found->second;
}

const vhdl::Statement* find_statement(
    const vhdl::Hir& hir, const StatementId id)
{
    const auto found = std::ranges::find(
        hir.statements(), id, &vhdl::Statement::id);
    return found == hir.statements().end() ? nullptr : &*found;
}

bool vhdl_callable_formal(
    const CompiledDesign& design, const DeclarationId id,
    const std::vector<DeclarationId>* cached_formals = nullptr)
{
    if (cached_formals != nullptr) {
        return std::ranges::binary_search(*cached_formals, id);
    }
    return std::ranges::any_of(
        design.vhdl_hir.declarations(), [&](const auto& declaration) {
            return declaration.callable
                && std::ranges::find(
                    declaration.callable->formals, id)
                    != declaration.callable->formals.end();
        });
}

std::optional<ConstantValue> evaluate_vhdl_constant(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const ConstantBindings& bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions,
    const std::vector<DeclarationId>* cached_formals = nullptr);

std::optional<ConstantValue> evaluate_vhdl_expression(
    const CompiledDesign& design, const ExpressionId id,
    const ConstantBindings& bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions,
    const std::vector<DeclarationId>* cached_formals = nullptr)
{
    if (!active_expressions.insert(id).second) {
        return std::nullopt;
    }
    const auto* expression = expression_for(
        design.vhdl_hir.expressions(), id);
    const auto value = expression == nullptr
        ? std::nullopt
        : evaluate_vhdl_constant(design, *expression, bindings,
              active_declarations, active_expressions, cached_formals);
    active_expressions.erase(id);
    return value;
}

const vhdl::Declaration* defined_pure_function(
    const CompiledDesign& design, const DeclarationId id)
{
    const auto* declaration = find_declaration(design.vhdl_hir, id);
    if (declaration != nullptr && declaration->completion) {
        const auto* completion = find_declaration(
            design.vhdl_hir, *declaration->completion);
        if (completion != nullptr) {
            declaration = completion;
        }
    }
    return declaration != nullptr && declaration->callable
            && declaration->callable->function
            && declaration->callable->pure
            && declaration->callable->defined
        ? declaration
        : nullptr;
}

const SourceSpan* vhdl_source_span(
    const Model& model, const SourceSpanId id)
{
    const auto& spans = model.source_spans();
    if (!id.valid() || id.value() >= spans.size()) {
        return nullptr;
    }
    const auto& span = spans[id.value()];
    return span.id == id ? &span : nullptr;
}

const SourceFile* vhdl_source_file(
    const Model& model, const SourceFileId id)
{
    const auto& files = model.source_files();
    if (!id.valid() || id.value() >= files.size()) {
        return nullptr;
    }
    const auto& file = files[id.value()];
    return file.id == id ? &file : nullptr;
}

bool same_vhdl_physical_source(
    const Model& model, const SourceSpanId left_id,
    const SourceSpanId right_id)
{
    const auto* const left = vhdl_source_span(model, left_id);
    const auto* const right = vhdl_source_span(model, right_id);
    if (left == nullptr || right == nullptr
        || !left->file.valid() || !right->file.valid()) {
        return false;
    }
    if (left->file == right->file) {
        return true;
    }
    const auto* const left_file = vhdl_source_file(model, left->file);
    const auto* const right_file = vhdl_source_file(model, right->file);
    return left_file != nullptr && right_file != nullptr
        && !left_file->physical_name.empty()
        && left_file->physical_name == right_file->physical_name;
}

bool vhdl_call_targets_later_local_function(
    const CompiledDesign& design, const vhdl::Expression& use,
    const vhdl::Declaration& definition)
{
    if (use.kind != vhdl::ExpressionKind::call
        || use.scope != definition.scope
        || !same_vhdl_physical_source(
            design.semantics, definition.source, use.source)) {
        return false;
    }
    const auto* const use_span = vhdl_source_span(
        design.semantics, use.source);
    const auto* const definition_span = vhdl_source_span(
        design.semantics, definition.source);
    return use_span != nullptr && definition_span != nullptr
        && definition_span->begin.offset > use_span->begin.offset;
}

std::optional<ConstantValue> evaluate_pure_vhdl_call(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const ConstantBindings& caller_bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions,
    const std::vector<DeclarationId>* cached_formals = nullptr)
{
    if (!expression.referenced_name) {
        return std::nullopt;
    }
    std::optional<DeclarationId> selected
        = expression.referenced_name->selected;
    if (!selected && expression.referenced_name->overloads.size() == 1U) {
        selected = expression.referenced_name->overloads.front();
    }
    const auto* declaration = selected
        ? defined_pure_function(design, *selected) : nullptr;
    if (declaration == nullptr
        || vhdl_call_targets_later_local_function(
            design, expression, *declaration)
        || declaration->statements.size() != 1U
        || !active_declarations.insert(declaration->id).second) {
        return std::nullopt;
    }
    const auto finish = [&](std::optional<ConstantValue> value) {
        active_declarations.erase(declaration->id);
        return value;
    };
    const auto& formals = declaration->callable->formals;
    if (expression.operands.size() > formals.size()
        || !expression.argument_names.empty()) {
        return finish(std::nullopt);
    }
    ConstantBindings bindings;
    bindings.reserve(formals.size());
    for (std::size_t index { }; index < formals.size(); ++index) {
        std::optional<ConstantValue> value;
        if (index < expression.operands.size()) {
            value = evaluate_vhdl_expression(design,
                expression.operands[index], caller_bindings,
                active_declarations, active_expressions, cached_formals);
        } else {
            const auto* formal = find_declaration(
                design.vhdl_hir, formals[index]);
            if (formal != nullptr && formal->initializer) {
                value = evaluate_vhdl_expression(design,
                    *formal->initializer, bindings,
                    active_declarations, active_expressions, cached_formals);
            }
        }
        if (!value) {
            return finish(std::nullopt);
        }
        bindings.emplace_back(formals[index], *value);
    }
    const auto* statement = find_statement(
        design.vhdl_hir, declaration->statements.front());
    if (statement == nullptr
        || statement->kind != vhdl::StatementKind::return_statement
        || !statement->value) {
        return finish(std::nullopt);
    }
    return finish(evaluate_vhdl_expression(design, *statement->value,
        bindings, active_declarations, active_expressions, cached_formals));
}

std::optional<ConstantValue> evaluate_vhdl_constant(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const ConstantBindings& bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions,
    const std::vector<DeclarationId>* cached_formals)
{
    if (!expression.nominal_type.empty()) {
        return std::nullopt;
    }
    if (const auto value = constant_value(expression)) {
        return value;
    }
    if (expression.kind == vhdl::ExpressionKind::name
        && expression.referenced_name
        && expression.referenced_name->selected) {
        const auto declaration_id = *expression.referenced_name->selected;
        if (const auto* value = bound_value(bindings, declaration_id)) {
            return *value;
        }
        const auto* declaration = find_declaration(
            design.vhdl_hir, declaration_id);
        if (declaration == nullptr
            || declaration->form != vhdl::DeclarationForm::constant
            || vhdl_callable_formal(
                design, declaration_id, cached_formals)
            || !declaration->initializer
            || !active_declarations.insert(declaration_id).second) {
            return std::nullopt;
        }
        const auto value = evaluate_vhdl_expression(design,
            *declaration->initializer, bindings,
            active_declarations, active_expressions, cached_formals);
        active_declarations.erase(declaration_id);
        return value;
    }
    if ((expression.kind == vhdl::ExpressionKind::unary
            || expression.kind == vhdl::ExpressionKind::binary)
        && vhdl_user_operator_overloads(design).contains(
            lowercase(expression.text))) {
        return std::nullopt;
    }
    if (expression.kind == vhdl::ExpressionKind::unary
        && expression.operands.size() == 1U) {
        const auto operand = evaluate_vhdl_expression(design,
            expression.operands.front(), bindings,
            active_declarations, active_expressions, cached_formals);
        return operand ? fold_unary(lowercase(expression.text), *operand)
                       : std::nullopt;
    }
    if (expression.kind == vhdl::ExpressionKind::binary
        && expression.operands.size() == 2U) {
        const auto left = evaluate_vhdl_expression(design,
            expression.operands.front(), bindings,
            active_declarations, active_expressions, cached_formals);
        const auto right = evaluate_vhdl_expression(design,
            expression.operands.back(), bindings,
            active_declarations, active_expressions, cached_formals);
        return left && right
            ? fold_binary(lowercase(expression.text), *left, *right)
            : std::nullopt;
    }
    if (expression.kind == vhdl::ExpressionKind::call) {
        return evaluate_pure_vhdl_call(design, expression, bindings,
            active_declarations, active_expressions, cached_formals);
    }
    return std::nullopt;
}

template <typename Expression>
void write_literal(Expression& expression, const ConstantValue value)
{
    using Kind = std::remove_cvref_t<decltype(expression.kind)>;
    expression.kind = value.kind == ConstantKind::integer
        ? Kind::integer_literal : Kind::boolean_literal;
    expression.text = value.kind == ConstantKind::integer
        ? std::to_string(value.integer)
        : (value.boolean ? "true" : "false");
    if constexpr (requires { expression.signed_value = true; }) {
        expression.signed_value = value.kind == ConstantKind::integer;
    }
    expression.referenced_name.reset();
    expression.operands.clear();
    expression.argument_names.clear();
    expression.associations.clear();
    if constexpr (requires { expression.call_arguments.clear(); }) {
        expression.call_arguments.clear();
    }
    expression.decoded_string.reset();
}

struct VhdlConstantGuardCache;

template <typename Expression>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign&, const std::vector<Expression>&,
    const Expression&, VhdlConstantGuardCache* = nullptr)
{
    return std::nullopt;
}

template <>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign& design,
    const std::vector<sv::Expression>& expressions,
    const sv::Expression& expression,
    VhdlConstantGuardCache*)
{
    if (expression.kind != sv::ExpressionKind::name
        || !expression.referenced_name
        || !expression.referenced_name->selected) {
        return std::nullopt;
    }
    const auto* declaration = find_declaration(
        design.systemverilog_hir,
        *expression.referenced_name->selected);
    // An enumeration literal has its enumeration's type, not that of its
    // value expression (`P = 0` in an enum logic [2:0] is three bits), so
    // its references are not folded into untyped literals (IEEE 1800-2017
    // 6.19).
    if (declaration == nullptr
        || declaration->form != sv::DeclarationForm::local_parameter
        || !declaration->initializer) {
        return std::nullopt;
    }
    const auto* initializer = expression_for(
        expressions, *declaration->initializer);
    auto value = initializer != nullptr && initializer->folded
        && dependency_free(initializer->dependencies)
        ? constant_value(*initializer) : std::nullopt;
    // Keep a named negative constant as a reference. Replacing a parameter
    // such as `P = -1` with the literal `-1` changes its context sizing: the
    // unary minus then receives an enclosing expression's width instead of
    // the parameter's already-established value width (IEEE 1800-2017
    // 6.20.2, 11.8.1).
    if (value && value->kind == ConstantKind::integer
        && value->integer < 0) {
        return std::nullopt;
    }
    // A local parameter with a type or a range converts its value to that
    // type (IEEE 1800-2017 6.20.2). The folded literal is a 32-bit signed
    // integer, so only int and integer parameters whose value fits fold;
    // other references stay names for the lowerer to convert.
    if (value && declaration->form == sv::DeclarationForm::local_parameter
        && declaration->type
        && (declaration->type->target.spelling != "implicit"
            || declaration->type->packed_range)) {
        const auto& spelling = declaration->type->target.spelling;
        // int and integer carry their intrinsic [31:0] range.
        const bool integer_type = spelling == "int" || spelling == "integer";
        if (!integer_type || value->kind != ConstantKind::integer
            || value->integer < std::numeric_limits<std::int32_t>::min()
            || value->integer > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
    }
    return value;
}

bool simple_vhdl_guard_identifier(const std::string_view spelling)
{
    if (spelling.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(spelling.front());
    if (!((first >= 'A' && first <= 'Z')
            || (first >= 'a' && first <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(spelling, [](const char raw) {
        const auto character = static_cast<unsigned char>(raw);
        return (character >= 'A' && character <= 'Z')
            || (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || raw == '_';
    });
}

struct VhdlGenericDependencyContext {
    const CompiledDesign& design;
    CompiledDesignResolver resolver;
    // These are completed resolver lookups, not dependency-search results.
    // The design's declaration/type/scope tables are immutable during this
    // annotation pass. Keep this context alive through fallback queries while
    // each dependency search still owns fresh recursion state.
    std::map<std::pair<ScopeId, std::string>,
        std::optional<DeclarationId>> simple_names { };
    std::map<std::pair<ScopeId, std::string>, std::optional<TypeId>>
        named_types { };

    VhdlGenericDependencyContext(
        const CompiledDesign& design, const UnitId unit)
        : design { design }
        , resolver { design, unit }
    {
    }

    [[nodiscard]] bool is_lexical_declaration(
        const DeclarationId declaration_id, const ScopeId use_scope) const
    {
        const auto* declaration = find_declaration(
            design.vhdl_hir, declaration_id);
        if (declaration == nullptr) {
            return false;
        }
        const auto* declaration_scope = find_record(
            design.semantics.scopes(), declaration->scope,
            normalization_indexes == nullptr
                ? nullptr : &normalization_indexes->semantic_scopes);
        const auto* use_record = find_record(
            design.semantics.scopes(), use_scope,
            normalization_indexes == nullptr
                ? nullptr : &normalization_indexes->semantic_scopes);
        if (declaration_scope == nullptr || use_record == nullptr
            || declaration_scope->unit != use_record->unit) {
            return false;
        }
        auto active = std::optional { use_scope };
        for (std::size_t depth { };
             active && depth <= design.semantics.scopes().size(); ++depth) {
            if (*active == declaration->scope) {
                return true;
            }
            const auto* scope = find_record(
                design.semantics.scopes(), *active,
                normalization_indexes == nullptr
                    ? nullptr : &normalization_indexes->semantic_scopes);
            if (scope == nullptr) {
                break;
            }
            active = scope->parent;
        }
        return false;
    }

    [[nodiscard]] std::optional<DeclarationId> resolve_simple_name(
        const ExpressionId expression, const vhdl::Name& name,
        const ScopeId scope)
    {
        // resolve_vhdl() consumes only `vhdl_spelling(name)` and the use
        // scope when there is no selected identity or overload set. Calls,
        // extended/qualified names, and names with retained candidates stay
        // on the original expression resolver path. Only cache a unique
        // result proved to come from the same unit's lexical ancestor chain;
        // package/import results can involve recursive context resolution and
        // are deliberately recomputed. This context is keyed by selected unit,
        // so the resolver's other lookup input is fixed too.
        const auto spelling = name.canonical.empty()
            ? std::string_view { name.spelling }
            : std::string_view { name.canonical };
        if (name.selected || !name.overloads.empty()
            || !simple_vhdl_guard_identifier(spelling)) {
            return resolver.resolve_expression_name(expression).unique();
        }
        const auto key = std::pair { scope, std::string { spelling } };
        const auto cached = simple_names.find(key);
        if (cached != simple_names.end()) {
            return cached->second;
        }
        // Invoke the original expression API on a miss, preserving its
        // per-expression recursion guard and the exact name snapshot loaded
        // from the current HIR record. The cache is consulted only from the
        // outer dependency walk, after any resolver call has returned.
        const auto resolved
            = resolver.resolve_expression_name(expression).unique();
        if (resolved && is_lexical_declaration(*resolved, scope)) {
            simple_names.emplace(key, resolved);
        }
        return resolved;
    }

    [[nodiscard]] std::optional<TypeId> resolve_named_type(
        const std::string_view spelling, const ScopeId scope)
    {
        const auto key = std::pair { scope, std::string { spelling } };
        const auto cached = named_types.find(key);
        if (cached != named_types.end()) {
            return cached->second;
        }
        const auto resolved = resolver.resolve_vhdl_named_type(
            spelling, scope);
        // This is a completed lookup over catalogs fixed for this context:
        // misses and ambiguities are stable too. Dependency completeness is
        // still tracked by VhdlGenericDependencySearch and is not cached here.
        named_types.emplace(key, resolved);
        return resolved;
    }
};

struct VhdlGenericDependencySearch {
    struct Result {
        bool depends { };
        // False means a cycle, depth limit, unresolved lookup, or missing
        // record prevented a complete search. Such results preserve the old
        // bool answer but are not stored in the pass-scoped guard cache.
        bool complete { true };
    };

    const CompiledDesign& design;
    VhdlGenericDependencyContext& context;
    std::set<ExpressionId> active_expressions { };
    std::set<TypeId> active_types { };

    [[nodiscard]] Result expression(
        const ExpressionId id, const std::size_t depth = 0U)
    {
        if (depth > 64U || !active_expressions.insert(id).second) {
            return { false, false };
        }
        const auto finish = [&](const Result result) {
            active_expressions.erase(id);
            return result;
        };
        const auto stored = design.find_expression(id);
        if (!stored || stored->vhdl == nullptr) {
            return finish({ false, false });
        }
        const auto& value = *stored->vhdl;
        if (!value.dependencies.generics.empty()) {
            return finish({ true, true });
        }
        bool complete = true;
        if (value.kind == vhdl::ExpressionKind::name
            && !value.referenced_name) {
            complete = false;
        }
        if (value.referenced_name) {
            const auto selected = value.referenced_name->selected
                ? value.referenced_name->selected
                : value.kind == vhdl::ExpressionKind::name
                    ? context.resolve_simple_name(
                          id, *value.referenced_name, value.scope)
                    : context.resolver.resolve_expression_name(id).unique();
            if (!selected) {
                complete = false;
            } else {
                const auto* declaration
                    = find_declaration(design.vhdl_hir, *selected);
                if (declaration == nullptr) {
                    complete = false;
                } else {
                    using Form = vhdl::DeclarationForm;
                    switch (declaration->form) {
                    case Form::generic_constant:
                    case Form::generic_type:
                    case Form::generic_function:
                    case Form::generic_procedure:
                    case Form::generic_package:
                        return finish({ true, true });
                    case Form::constant: {
                        if (declaration->initializer) {
                            const auto initializer = expression(
                                *declaration->initializer, depth + 1U);
                            if (initializer.depends) {
                                return finish({ true,
                                    complete && initializer.complete });
                            }
                            complete = complete && initializer.complete;
                        } else {
                            complete = false;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
        for (const auto operand : value.operands) {
            const auto result = expression(operand, depth + 1U);
            if (result.depends) {
                return finish({ true, complete && result.complete });
            }
            complete = complete && result.complete;
        }
        for (const auto& association : value.associations) {
            const auto result = expression(
                association.value, depth + 1U);
            if (result.depends) {
                return finish({ true, complete && result.complete });
            }
            complete = complete && result.complete;
            for (const auto choice : association.choices) {
                const auto choice_result = expression(
                    choice, depth + 1U);
                if (choice_result.depends) {
                    return finish({ true,
                        complete && choice_result.complete });
                }
                complete = complete && choice_result.complete;
            }
        }
        return finish({ false, complete });
    }

    [[nodiscard]] Result range(
        const vhdl::RangeConstraint& value,
        const std::size_t depth = 0U)
    {
        bool complete = true;
        if (value.left_expression) {
            const auto left = expression(
                *value.left_expression, depth + 1U);
            if (left.depends) {
                return left;
            }
            complete = complete && left.complete;
        }
        if (value.right_expression) {
            const auto right = expression(
                *value.right_expression, depth + 1U);
            if (right.depends) {
                return { true, complete && right.complete };
            }
            complete = complete && right.complete;
        }
        return { false, complete };
    }

    [[nodiscard]] Result constraints(
        const vhdl::SubtypeIndication& value,
        const std::size_t depth = 0U)
    {
        if (depth > 64U) {
            return { true, false };
        }
        bool complete = true;
        for (const auto& candidate : value.constraints) {
            const auto result = range(candidate, depth + 1U);
            if (result.depends) {
                return { true, complete && result.complete };
            }
            complete = complete && result.complete;
        }
        for (const auto& element : value.element_constraints) {
            for (const auto& candidate : element.constraints) {
                const auto result = range(candidate, depth + 1U);
                if (result.depends) {
                    return { true, complete && result.complete };
                }
                complete = complete && result.complete;
            }
        }
        return { false, complete };
    }

    [[nodiscard]] Result type(
        const TypeId id, const ScopeId scope, const std::size_t depth = 0U)
    {
        if (depth > 64U || !active_types.insert(id).second) {
            return { false, false };
        }
        const auto finish = [&](const Result result) {
            active_types.erase(id);
            return result;
        };
        const auto stored = design.find_type(id);
        if (!stored || stored->vhdl == nullptr) {
            return finish({ false, false });
        }
        const auto& definition = *stored->vhdl;
        const auto* declaration = find_declaration(
            design.vhdl_hir, definition.declaration);
        const bool generic_type = declaration != nullptr
            && declaration->form == vhdl::DeclarationForm::generic_type;
        if (generic_type) {
            return finish({ true,
                definition.form != vhdl::TypeForm::unresolved });
        }
        bool complete = declaration != nullptr
            && definition.form != vhdl::TypeForm::unresolved;
        const auto base_constraints = constraints(
            definition.base, depth + 1U);
        if (base_constraints.depends) {
            return finish({ true,
                complete && base_constraints.complete });
        }
        complete = complete && base_constraints.complete;
        if (definition.scalar_range) {
            const auto scalar_range = range(
                *definition.scalar_range, depth + 1U);
            if (scalar_range.depends) {
                return finish({ true,
                    complete && scalar_range.complete });
            }
            complete = complete && scalar_range.complete;
        }
        if (definition.form != vhdl::TypeForm::subtype
            && definition.form != vhdl::TypeForm::alias) {
            if (definition.form == vhdl::TypeForm::unresolved) {
                return finish({ false, false });
            }
            return finish({ false, complete });
        }
        const auto use_scope = declaration != nullptr
            ? declaration->scope : scope;
        const auto base = definition.base.type_mark.target.valid()
            ? std::optional { definition.base.type_mark.target }
            : context.resolve_named_type(
                  definition.base.type_mark.spelling, use_scope);
        if (!base) {
            return finish({ false, false });
        }
        const auto base_result = type(*base, use_scope, depth + 1U);
        if (base_result.depends) {
            return finish({ true,
                complete && base_result.complete });
        }
        complete = complete && base_result.complete;
        return finish({ false, complete });
    }

    [[nodiscard]] Result subtype(
        const vhdl::SubtypeIndication& value,
        const ScopeId scope,
        const std::size_t depth = 0U)
    {
        if (depth > 64U) {
            return { true, false };
        }
        const auto subtype_constraints = constraints(value, depth + 1U);
        if (subtype_constraints.depends) {
            return subtype_constraints;
        }
        const auto type_id = value.type_mark.target.valid()
            ? std::optional { value.type_mark.target }
            : context.resolve_named_type(
                  value.type_mark.spelling, scope);
        if (!type_id) {
            return { false, false };
        }
        const auto type_result = type(*type_id, scope, depth + 1U);
        if (type_result.depends) {
            return { true,
                subtype_constraints.complete && type_result.complete };
        }
        return { false,
            subtype_constraints.complete && type_result.complete };
    }
};

bool vhdl_constant_subtype_has_user_defined_scalar_type(
    const CompiledDesign& design, const vhdl::Declaration& declaration,
    VhdlGenericDependencyContext& context, bool& complete)
{
    complete = true;
    if (!declaration.subtype) {
        return false;
    }

    const auto resolve_type = [&](const vhdl::SubtypeIndication& subtype,
                                  const ScopeId scope) {
        return subtype.type_mark.target.valid()
            ? std::optional { subtype.type_mark.target }
            : context.resolve_named_type(
                  subtype.type_mark.spelling, scope);
    };
    auto type_id = resolve_type(*declaration.subtype, declaration.scope);
    if (!type_id) {
        complete = false;
        return false;
    }
    std::set<TypeId> visited;
    while (type_id) {
        if (!visited.insert(*type_id).second) {
            complete = false;
            return false;
        }
        const auto type = design.find_type(*type_id);
        if (!type || type->vhdl == nullptr) {
            complete = false;
            return false;
        }
        const auto& definition = *type->vhdl;
        if (definition.form == vhdl::TypeForm::unresolved) {
            complete = false;
            return false;
        }
        const auto* type_declaration = find_declaration(
            design.vhdl_hir, definition.declaration);
        if (type_declaration == nullptr) {
            complete = false;
            return false;
        }
        const auto* semantic_type = find_record(
            design.semantics.types(), *type_id,
            normalization_indexes == nullptr
                ? nullptr : &normalization_indexes->semantic_types);
        if (semantic_type == nullptr) {
            complete = false;
            return false;
        }
        // The parser represents `type T is range ...` as a subtype-shaped
        // HIR declaration over INTEGER/REAL, while the semantic type remains
        // a declaration (a new nominal type) and the HIR retains its scalar
        // range. An ordinary `subtype T is INTEGER range ...` has
        // TypeKind::subtype and must continue through the base instead.
        const bool declared_integer_scalar_type
            = definition.form == vhdl::TypeForm::subtype
            && type_declaration->form
                == vhdl::DeclarationForm::subtype
            && semantic_type->kind == TypeKind::declaration
            && definition.scalar_range.has_value();
        if ((definition.form == vhdl::TypeForm::subtype
                || definition.form == vhdl::TypeForm::alias)
            && !declared_integer_scalar_type) {
            type_id = resolve_type(
                definition.base, type_declaration->scope);
            if (!type_id) {
                complete = false;
                return false;
            }
            continue;
        }
        const bool scalar_type
            = definition.form == vhdl::TypeForm::scalar
            && type_declaration->form == vhdl::DeclarationForm::type;
        if (!scalar_type && !declared_integer_scalar_type) {
            return false;
        }

        // A scalar declared in a user unit has a nominal identity which an
        // integer literal cannot carry. Compiler-owned standard packages are
        // marked by revision; the implicit root is identified as STD.STANDARD.
        const auto* scope = find_record(
            design.semantics.scopes(), type_declaration->scope,
            normalization_indexes == nullptr
                ? nullptr : &normalization_indexes->semantic_scopes);
        if (scope == nullptr) {
            complete = false;
            return false;
        }
        const auto owner = design.find_unit(scope->unit);
        if (!owner || owner->vhdl == nullptr) {
            complete = false;
            return false;
        }
        const auto& owner_vhdl = *owner->vhdl;
        const bool standard_root = owner_vhdl.kind == vhdl::UnitKind::package
            && owner_vhdl.primary_name.empty()
            && lowercase(owner_vhdl.library) == "std"
            && lowercase(owner_vhdl.name) == "standard";
        return owner_vhdl.standard_package_revision.empty()
            && !standard_root;
    }
    return false;
}

bool vhdl_constant_subtype_requires_name(
    const CompiledDesign& design, const vhdl::Declaration& declaration,
    VhdlGenericDependencyContext& context)
{
    if (!declaration.subtype) {
        return false;
    }
    VhdlGenericDependencySearch search { design, context };
    bool nominal_complete = true;
    // Keep the existing generic-dependent-constant guard intact. The second
    // condition preserves exact nominal provenance for user-defined scalars.
    return search.subtype(*declaration.subtype, declaration.scope).depends
        || vhdl_constant_subtype_has_user_defined_scalar_type(
            design, declaration, context, nominal_complete);
}

struct VhdlConstantGuardCache {
    using GuardKey = std::pair<UnitId, DeclarationId>;
    std::vector<DeclarationId> callable_formals { };
    std::map<GuardKey, bool> requires_name { };
    std::map<UnitId, std::unique_ptr<VhdlGenericDependencyContext>> contexts { };

    VhdlGenericDependencyContext& context_for(
        const CompiledDesign& design, const UnitId unit)
    {
        auto& context = contexts[unit];
        if (!context) {
            context = std::make_unique<VhdlGenericDependencyContext>(
                design, unit);
        }
        return *context;
    }

    VhdlConstantGuardCache(const CompiledDesign& design,
        const std::vector<vhdl::Expression>& expressions)
    {
        for (const auto& declaration : design.vhdl_hir.declarations()) {
            if (declaration.callable) {
                callable_formals.insert(callable_formals.end(),
                    declaration.callable->formals.begin(),
                    declaration.callable->formals.end());
            }
        }
        std::ranges::sort(callable_formals);
        callable_formals.erase(
            std::unique(callable_formals.begin(), callable_formals.end()),
            callable_formals.end());

        // Cache only complete top-level answers. Declaration subtype/type
        // links and selected names remain stable during this annotation pass;
        // folding may rewrite derived expression nodes, but it cannot add a
        // generic dependency or change nominal ownership. The cache is
        // discarded before the next normalization pass. Each recursive walk
        // gets its own active sets so cycle/depth answers stay query-local.
        std::set<DeclarationId> referenced_constants;
        for (const auto& expression : expressions) {
            if (expression.kind != vhdl::ExpressionKind::name
                || !expression.nominal_type.empty()
                || !expression.referenced_name
                || !expression.referenced_name->selected) {
                continue;
            }
            const auto declaration_id
                = *expression.referenced_name->selected;
            const auto* declaration
                = find_declaration(design.vhdl_hir, declaration_id);
            // The evaluator cannot fold a constant without an initializer,
            // so no subtype guard for that reference can affect the result.
            if (declaration != nullptr
                && declaration->form == vhdl::DeclarationForm::constant
                && declaration->initializer
                && declaration->subtype) {
                referenced_constants.insert(declaration_id);
            }
        }

        for (const auto declaration_id : referenced_constants) {
            const auto* declaration
                = find_declaration(design.vhdl_hir, declaration_id);
            if (declaration == nullptr || !declaration->initializer
                || !declaration->subtype) {
                continue;
            }
            const auto unit = declaration_unit(
                design.semantics, declaration_id);
            if (!unit) {
                continue;
            }
            auto& context = context_for(design, *unit);
            // Keep recursion state local to one top-level declaration query.
            // Only the completed guard answer is reused across this pass.
            VhdlGenericDependencySearch search {
                design, context };
            const auto generic_result = search.subtype(
                *declaration->subtype, declaration->scope);
            bool nominal_complete = true;
            const bool user_defined_scalar = generic_result.depends
                ? false
                : vhdl_constant_subtype_has_user_defined_scalar_type(
                      design, *declaration, context,
                      nominal_complete);
            if (generic_result.complete && nominal_complete) {
                requires_name.emplace(GuardKey { *unit, declaration_id },
                    generic_result.depends || user_defined_scalar);
            }
        }
    }

    [[nodiscard]] std::optional<bool> lookup(
        const UnitId unit, const DeclarationId declaration) const
    {
        const auto found = requires_name.find(GuardKey { unit, declaration });
        return found == requires_name.end()
            ? std::nullopt : std::optional { found->second };
    }
};

template <>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign& design,
    const std::vector<vhdl::Expression>&,
    const vhdl::Expression& expression,
    VhdlConstantGuardCache* guard_cache)
{
    if (expression.kind != vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    if (expression.referenced_name
        && expression.referenced_name->selected) {
        const auto declaration = find_declaration(design.vhdl_hir,
            *expression.referenced_name->selected);
        // Match the prepass filter: a missing initializer makes this name
        // valueless before its subtype guard can affect folding.
        if (declaration != nullptr
            && declaration->form == vhdl::DeclarationForm::constant
            && declaration->initializer
            && declaration->subtype) {
            const auto unit = declaration_unit(
                design.semantics, declaration->id);
            const auto cached = guard_cache == nullptr
                || !unit ? std::nullopt
                         : guard_cache->lookup(*unit, declaration->id);
            bool requires_name { };
            if (cached) {
                requires_name = *cached;
            } else if (unit) {
                if (guard_cache != nullptr) {
                    auto& context = guard_cache->context_for(
                        design, *unit);
                    requires_name = vhdl_constant_subtype_requires_name(
                        design, *declaration, context);
                } else {
                    VhdlGenericDependencyContext context { design, *unit };
                    requires_name = vhdl_constant_subtype_requires_name(
                        design, *declaration, context);
                }
            }
            if (requires_name) {
                return std::nullopt;
            }
        }
    }
    std::set<DeclarationId> active_declarations;
    std::set<ExpressionId> active_expressions { expression.id };
    const auto* cached_formals = guard_cache == nullptr
        ? nullptr : &guard_cache->callable_formals;
    return evaluate_vhdl_constant(design, expression, { },
        active_declarations, active_expressions, cached_formals);
}

template <typename Expression>
bool fold_expression(const CompiledDesign& design,
    const std::vector<Expression>& expressions, Expression& expression,
    VhdlConstantGuardCache* guard_cache = nullptr)
{
    using Kind = std::remove_cvref_t<decltype(expression.kind)>;
    if (!expression.nominal_type.empty()) {
        return false;
    }
    const auto operation = lowercase(expression.text);
    if constexpr (std::is_same_v<Expression, vhdl::Expression>) {
        if ((expression.kind == Kind::unary || expression.kind == Kind::binary)
            && vhdl_user_operator_overloads(design).contains(operation)) {
            return false;
        }
    }
    auto value = constant_name_value(
        design, expressions, expression, guard_cache);
    if (!value && expression.kind == Kind::unary
        && expression.operands.size() == 1) {
        const auto* operand = expression_for(
            expressions, expression.operands.front());
        if (operand != nullptr) {
            const auto input = constant_value(*operand);
            if (input) {
                value = fold_unary(operation, *input);
            }
        }
    } else if (!value && expression.kind == Kind::binary
        && expression.operands.size() == 2) {
        const auto* left = expression_for(
            expressions, expression.operands.front());
        const auto* right = expression_for(
            expressions, expression.operands.back());
        if (left != nullptr && right != nullptr) {
            // The generic folder carries only host-sized integer values.  A
            // SystemVerilog based literal also carries a self-determined
            // width and signedness which must remain visible to the compiled
            // HIR evaluator (including its bounded-work checks).
            if (left->text.find('\'') != std::string::npos
                || right->text.find('\'') != std::string::npos) {
                return false;
            }
            const auto left_value = constant_value(*left);
            const auto right_value = constant_value(*right);
            if (left_value && right_value) {
                value = fold_binary(
                    operation, *left_value, *right_value);
            }
        }
    }
    if constexpr (std::is_same_v<Expression, vhdl::Expression>) {
        if (!value && expression.kind == Kind::call) {
            std::set<DeclarationId> active_declarations;
            std::set<ExpressionId> active_expressions { expression.id };
            value = evaluate_vhdl_constant(design, expression, { },
                active_declarations, active_expressions,
                guard_cache == nullptr
                    ? nullptr : &guard_cache->callable_formals);
        }
    }
    if (!value) {
        return false;
    }
    write_literal(expression, *value);
    return true;
}

template <typename Expression>
bool add_operand_dependencies(const std::vector<Expression>& expressions,
    const Expression& expression, ResidualDependencies& dependencies,
    bool& nested_folded, bool& has_nested)
{
    const auto add = [&](const ExpressionId id) {
        const auto* input = expression_for(expressions, id);
        if (input == nullptr) {
            return false;
        }
        has_nested = true;
        merge_dependencies(dependencies, input->dependencies);
        nested_folded = nested_folded && input->folded;
        return true;
    };
    for (const auto operand : expression.operands) {
        if (!add(operand)) {
            return false;
        }
    }
    if constexpr (requires { expression.call_arguments; }) {
        for (const auto& argument : expression.call_arguments) {
            if (argument.actual && !add(*argument.actual)) {
                return false;
            }
        }
    }
    for (const auto& association : expression.associations) {
        for (const auto choice : association.choices) {
            if (!add(choice)) {
                return false;
            }
        }
        if (!add(association.value)) {
            return false;
        }
    }
    return true;
}

template <typename Expression>
bool add_initializer_dependencies(const CompiledDesign&,
    const std::vector<Expression>&, const Expression&,
    ResidualDependencies&, const VhdlConstantGuardCache*)
{
    return true;
}

template <>
bool add_initializer_dependencies(const CompiledDesign& design,
    const std::vector<sv::Expression>& expressions,
    const sv::Expression& expression,
    ResidualDependencies& dependencies,
    const VhdlConstantGuardCache*)
{
    if (expression.kind != sv::ExpressionKind::name
        || !expression.referenced_name
        || !expression.referenced_name->selected) {
        return true;
    }
    const auto* declaration = find_declaration(
        design.systemverilog_hir,
        *expression.referenced_name->selected);
    if (declaration == nullptr || !declaration->initializer) {
        return true;
    }
    const auto* initializer = expression_for(
        expressions, *declaration->initializer);
    if (initializer == nullptr) {
        return false;
    }
    merge_dependencies(dependencies, initializer->dependencies);
    return true;
}

template <>
bool add_initializer_dependencies(const CompiledDesign& design,
    const std::vector<vhdl::Expression>& expressions,
    const vhdl::Expression& expression,
    ResidualDependencies& dependencies,
    const VhdlConstantGuardCache* guard_cache)
{
    if (!expression.referenced_name
        || !expression.referenced_name->selected) {
        return true;
    }
    const auto declaration_id = *expression.referenced_name->selected;
    if (expression.kind == vhdl::ExpressionKind::name) {
        const auto* cached_formals = guard_cache == nullptr
            ? nullptr : &guard_cache->callable_formals;
        if (vhdl_callable_formal(
                design, declaration_id, cached_formals)) {
            return true;
        }
        const auto* declaration = find_declaration(
            design.vhdl_hir, declaration_id);
        if (declaration == nullptr || !declaration->initializer) {
            return true;
        }
        const auto* initializer = expression_for(
            expressions, *declaration->initializer);
        if (initializer == nullptr) {
            return false;
        }
        merge_dependencies(dependencies, initializer->dependencies);
        return true;
    }
    if (expression.kind != vhdl::ExpressionKind::call) {
        return true;
    }
    const auto* declaration = defined_pure_function(
        design, declaration_id);
    if (declaration == nullptr || declaration->statements.size() != 1U) {
        return true;
    }
    const auto* statement = find_statement(
        design.vhdl_hir, declaration->statements.front());
    if (statement == nullptr) {
        return false;
    }
    if (statement->kind != vhdl::StatementKind::return_statement
        || !statement->value) {
        return true;
    }
    const auto* value = expression_for(expressions, *statement->value);
    if (value == nullptr) {
        return false;
    }
    merge_dependencies(dependencies, value->dependencies);
    return true;
}

bool structural(const sv::ExpressionKind kind)
{
    return kind == sv::ExpressionKind::assignment_pattern;
}

bool structural(const vhdl::ExpressionKind kind)
{
    return kind == vhdl::ExpressionKind::aggregate;
}

bool static_choice(const sv::ExpressionKind kind)
{
    return kind == sv::ExpressionKind::default_choice;
}

bool static_choice(const vhdl::ExpressionKind kind)
{
    return kind == vhdl::ExpressionKind::default_choice;
}

template <typename Expression>
bool annotate_expressions(const CompiledDesign& design,
    std::vector<Expression>& expressions)
{
    using Kind = std::remove_cvref_t<
        decltype(std::declval<Expression>().kind)>;
    for (auto& expression : expressions) {
        expression.dependencies = { };
        add_name_dependencies(
            design, expression.referenced_name, expression.dependencies);
        expression.folded = literal(expression.kind)
            || static_choice(expression.kind);
    }
    std::optional<VhdlConstantGuardCache> vhdl_guard_cache;
    if constexpr (std::is_same_v<Expression, vhdl::Expression>) {
        // Build constant guard answers before fold_expression rewrites any
        // expression nodes, then discard them with this annotation pass.
        vhdl_guard_cache.emplace(design, expressions);
    }
    std::set<decltype(std::declval<Expression>().id)>
        named_path_attribute_prefixes;
    if constexpr (std::is_same_v<Expression, vhdl::Expression>) {
        for (const auto& use : expressions) {
            if (use.kind == vhdl::ExpressionKind::call
                && use.operands.size() == 1U
                && (use.text == "'simple_name"
                    || use.text == "'path_name"
                    || use.text == "'instance_name")) {
                named_path_attribute_prefixes.insert(use.operands.front());
            }
        }
    }
    for (std::size_t pass = 0; pass <= expressions.size(); ++pass) {
        bool changed = false;
        for (auto& expression : expressions) {
            ResidualDependencies dependencies;
            add_name_dependencies(
                design, expression.referenced_name, dependencies);
            if (!add_initializer_dependencies(
                    design, expressions, expression, dependencies,
                    vhdl_guard_cache ? &*vhdl_guard_cache : nullptr)) {
                return false;
            }
            bool nested_folded = true;
            bool has_nested = false;
            if (!add_operand_dependencies<Expression>(
                    expressions, expression, dependencies,
                    nested_folded, has_nested)) {
                return false;
            }
            canonicalize(dependencies);
            const bool rewrite_with_residual_name
                = expression.kind == Kind::name;
            const bool rewrite_with_residual_call
                = expression.kind == Kind::call;
            // SIMPLE_NAME, PATH_NAME, and INSTANCE_NAME apply to the named
            // entity itself. Replacing a constant prefix by its scalar value
            // discards the selected declaration needed by the attribute's
            // path producer, so retain only those exact name operands.
            const bool named_path_attribute_prefix = [&] {
                if constexpr (std::is_same_v<Expression, vhdl::Expression>) {
                    return expression.kind == vhdl::ExpressionKind::name
                        && named_path_attribute_prefixes.contains(
                            expression.id);
                }
                return false;
            }();
            const bool rewritten = !named_path_attribute_prefix
                && nested_folded
                && (dependency_free(dependencies)
                    || rewrite_with_residual_name
                    || rewrite_with_residual_call)
                && fold_expression(design, expressions, expression,
                    vhdl_guard_cache
                        ? &*vhdl_guard_cache : nullptr);
            if (rewritten) {
                dependencies = { };
            }
            const bool folded = literal(expression.kind)
                || static_choice(expression.kind)
                || (structural(expression.kind) && has_nested
                    && nested_folded && dependency_free(dependencies));
            if (dependencies != expression.dependencies
                || folded != expression.folded || rewritten) {
                expression.dependencies = std::move(dependencies);
                expression.folded = folded;
                changed = true;
            }
        }
        if (!changed) {
            return true;
        }
    }
    return false;
}

template <typename Expression, typename Range>
bool materialize_range(const std::vector<Expression>& expressions,
    Range& range)
{
    const auto materialize = [&](const std::optional<ExpressionId> id,
                                 std::optional<std::int64_t>& output) {
        if (!id) {
            return true;
        }
        const auto* expression = expression_for(expressions, *id);
        if (expression == nullptr) {
            return false;
        }
        const auto value = constant_value(*expression);
        if (value && value->kind == ConstantKind::integer
            && expression->folded
            && dependency_free(expression->dependencies)) {
            output = value->integer;
        }
        return true;
    };
    return materialize(range.left_expression, range.left)
        && materialize(range.right_expression, range.right);
}

bool materialize_systemverilog_type(
    const std::vector<sv::Expression>& expressions,
    sv::TypeReference& type)
{
    if (type.packed_range
        && !materialize_range(expressions, *type.packed_range)) {
        return false;
    }
    for (auto& range : type.unpacked_dimensions) {
        if (!materialize_range(expressions, range)) {
            return false;
        }
    }
    for (auto& actual : type.interface_parameter_actuals) {
        if (actual.type
            && !materialize_systemverilog_type(
                expressions, *actual.type)) {
            return false;
        }
    }
    return std::ranges::all_of(type.container_element_types,
        [&](auto& element) {
            return materialize_systemverilog_type(expressions, element);
        });
}

bool materialize_systemverilog_actuals(
    const std::vector<sv::Expression>& expressions,
    std::vector<sv::ActualAssociation>& actuals)
{
    for (auto& actual : actuals) {
        if (actual.type
            && !materialize_systemverilog_type(
                expressions, *actual.type)) {
            return false;
        }
    }
    return true;
}

bool materialize_systemverilog_lets(
    const std::vector<sv::Expression>& expressions,
    std::vector<sv::LetDeclaration>& lets)
{
    for (auto& declaration : lets) {
        for (auto& port : declaration.ports) {
            if (port.type
                && !materialize_systemverilog_type(
                    expressions, *port.type)) {
                return false;
            }
        }
    }
    return true;
}

bool materialize_systemverilog_generate_ranges(
    const std::vector<sv::Expression>& expressions,
    sv::GenerateRegion& generate)
{
    if (!materialize_systemverilog_lets(expressions, generate.lets)) {
        return false;
    }
    return std::ranges::all_of(generate.nested, [&](auto& nested) {
        return materialize_systemverilog_generate_ranges(
            expressions, nested);
    });
}

bool materialize_systemverilog_ranges(CompiledDesign& design)
{
    const auto& expressions = design.systemverilog_hir.expressions();
    for (auto& type : design.systemverilog_hir.mutable_types()) {
        if (!materialize_systemverilog_type(expressions, type.base)) {
            return false;
        }
        for (auto& member : type.members) {
            if (!materialize_systemverilog_type(
                    expressions, member.type)) {
                return false;
            }
        }
        if (type.container) {
            for (auto& range : type.container->static_dimensions) {
                if (!materialize_range(expressions, range)) {
                    return false;
                }
            }
        }
    }
    for (auto& declaration
         : design.systemverilog_hir.mutable_declarations()) {
        if ((declaration.type
                && !materialize_systemverilog_type(
                    expressions, *declaration.type))
            || (declaration.default_type
                && !materialize_systemverilog_type(
                    expressions, *declaration.default_type))
            || (declaration.callable
                && !materialize_systemverilog_type(
                    expressions, declaration.callable->return_type))) {
            return false;
        }
    }
    for (auto& declaration : design.systemverilog_hir.mutable_classes()) {
        for (auto& parameter : declaration.parameters) {
            if ((parameter.type
                    && !materialize_systemverilog_type(
                        expressions, *parameter.type))
                || (parameter.default_type
                    && !materialize_systemverilog_type(
                        expressions, *parameter.default_type))) {
                return false;
            }
        }
        for (auto& property : declaration.properties) {
            if (!materialize_systemverilog_type(
                    expressions, property.type)) {
                return false;
            }
        }
        const auto relation = [&](sv::ClassRelation& value) {
            return materialize_systemverilog_actuals(
                expressions, value.actuals);
        };
        if ((declaration.base && !relation(*declaration.base))
            || !std::ranges::all_of(
                declaration.extended_interfaces, relation)
            || !std::ranges::all_of(
                declaration.implemented_interfaces, relation)) {
            return false;
        }
    }
    for (auto& instance : design.systemverilog_hir.mutable_instances()) {
        if (!materialize_systemverilog_actuals(
                expressions, instance.parameters)
            || !materialize_systemverilog_actuals(
                expressions, instance.ports)) {
            return false;
        }
    }
    for (auto& declaration
         : design.systemverilog_hir.mutable_dpi_declarations()) {
        if (!declaration.resolved_profile) {
            continue;
        }
        if (declaration.resolved_profile->return_type
            && !materialize_systemverilog_type(expressions,
                *declaration.resolved_profile->return_type)) {
            return false;
        }
        for (auto& formal : declaration.resolved_profile->formals) {
            if (!materialize_systemverilog_type(
                    expressions, formal.type)) {
                return false;
            }
        }
    }
    for (auto& unit : design.systemverilog_hir.mutable_units()) {
        if (!materialize_systemverilog_lets(expressions, unit.lets)) {
            return false;
        }
        for (auto& generate : unit.generates) {
            if (!materialize_systemverilog_generate_ranges(
                    expressions, generate)) {
                return false;
            }
        }
    }
    return true;
}

bool materialize_vhdl_subtype(
    const std::vector<vhdl::Expression>& expressions,
    vhdl::SubtypeIndication& subtype)
{
    return std::ranges::all_of(subtype.constraints, [&](auto& range) {
        return materialize_range(expressions, range);
    });
}

bool materialize_vhdl_associations(
    const std::vector<vhdl::Expression>& expressions,
    std::vector<vhdl::Association>& associations)
{
    for (auto& association : associations) {
        if (association.type
            && !materialize_vhdl_subtype(
                expressions, *association.type)) {
            return false;
        }
    }
    return true;
}

bool materialize_vhdl_mode_elements(
    const std::vector<vhdl::Expression>& expressions,
    std::vector<vhdl::ModeViewElement>& elements)
{
    for (auto& element : elements) {
        if ((element.subtype
                && !materialize_vhdl_subtype(
                    expressions, *element.subtype))
            || !materialize_vhdl_mode_elements(
                expressions, element.elements)) {
            return false;
        }
    }
    return true;
}

bool materialize_vhdl_binding(
    const std::vector<vhdl::Expression>& expressions,
    vhdl::BindingIndication& binding)
{
    return materialize_vhdl_associations(
               expressions, binding.generic_map)
        && materialize_vhdl_associations(
            expressions, binding.port_map);
}

bool materialize_vhdl_block_configuration(
    const std::vector<vhdl::Expression>& expressions,
    vhdl::BlockConfiguration& configuration)
{
    for (auto& component : configuration.components) {
        if (!materialize_vhdl_binding(
                expressions, component.binding)
            || (component.block_configuration
                && !materialize_vhdl_block_configuration(
                    expressions, *component.block_configuration))) {
            return false;
        }
    }
    return std::ranges::all_of(configuration.blocks,
        [&](auto& nested) {
            return materialize_vhdl_block_configuration(
                expressions, nested);
        });
}

bool materialize_vhdl_generate_ranges(
    const std::vector<vhdl::Expression>& expressions,
    vhdl::GenerateRegion& generate)
{
    if (!materialize_vhdl_associations(
            expressions, generate.generic_map)
        || !materialize_vhdl_associations(
            expressions, generate.port_map)) {
        return false;
    }
    return std::ranges::all_of(generate.nested, [&](auto& nested) {
        return materialize_vhdl_generate_ranges(expressions, nested);
    });
}

bool materialize_vhdl_ranges(CompiledDesign& design)
{
    const auto& expressions = design.vhdl_hir.expressions();
    for (auto& type : design.vhdl_hir.mutable_types()) {
        if (!materialize_vhdl_subtype(expressions, type.base)) {
            return false;
        }
        for (auto& dimension : type.array_dimensions) {
            if (dimension.constraint
                && !materialize_range(
                    expressions, *dimension.constraint)) {
                return false;
            }
        }
        if ((type.element_subtype
                && !materialize_vhdl_subtype(
                    expressions, *type.element_subtype))
            || (type.designated_subtype
                && !materialize_vhdl_subtype(
                    expressions, *type.designated_subtype))
            || (type.scalar_range
                && !materialize_range(
                    expressions, *type.scalar_range))) {
            return false;
        }
        for (auto& element : type.record_elements) {
            if (!materialize_vhdl_subtype(
                    expressions, element.subtype)) {
                return false;
            }
        }
        for (auto& unit : type.physical_units) {
            if (!unit.scale) {
                continue;
            }
            const auto* expression = expression_for(
                expressions, *unit.scale);
            if (expression == nullptr) {
                return false;
            }
            const auto value = constant_value(*expression);
            if (value && value->kind == ConstantKind::integer
                && expression->folded
                && dependency_free(expression->dependencies)) {
                unit.scale_factor = value->integer;
            }
        }
    }
    for (auto& declaration : design.vhdl_hir.mutable_declarations()) {
        if ((declaration.subtype
                && !materialize_vhdl_subtype(
                    expressions, *declaration.subtype))
            || (declaration.default_type
                && !materialize_vhdl_subtype(
                    expressions, *declaration.default_type))
            || (declaration.callable
                && declaration.callable->return_type
                && !materialize_vhdl_subtype(expressions,
                    *declaration.callable->return_type))
            || (declaration.attribute
                && declaration.attribute->subtype
                && !materialize_vhdl_subtype(expressions,
                    *declaration.attribute->subtype))) {
            return false;
        }
        if (declaration.package
            && !materialize_vhdl_associations(
                expressions, declaration.package->generic_map)) {
            return false;
        }
        if (declaration.mode_view
            && (!materialize_vhdl_subtype(expressions,
                    declaration.mode_view->record_subtype)
                || !materialize_vhdl_mode_elements(expressions,
                    declaration.mode_view->elements))) {
            return false;
        }
        if (declaration.interface_view
            && !materialize_vhdl_mode_elements(
                expressions, declaration.interface_view->elements)) {
            return false;
        }
    }
    for (auto& instance : design.vhdl_hir.mutable_instances()) {
        if (!materialize_vhdl_associations(
                expressions, instance.generic_map)
            || !materialize_vhdl_associations(
                expressions, instance.port_map)) {
            return false;
        }
    }
    for (auto& unit : design.vhdl_hir.mutable_units()) {
        for (auto& generate : unit.generates) {
            if (!materialize_vhdl_generate_ranges(
                    expressions, generate)) {
                return false;
            }
        }
        for (auto& component : unit.component_configurations) {
            if (!materialize_vhdl_binding(
                    expressions, component.binding)) {
                return false;
            }
        }
        if (unit.configuration
            && !materialize_vhdl_block_configuration(
                expressions, *unit.configuration)) {
            return false;
        }
    }
    return true;
}

template <typename Generate, typename Expression>
bool annotate_generate(const std::vector<Expression>& expressions,
    Generate& generate)
{
    generate.dependencies = { };
    const auto add = [&](const std::optional<ExpressionId> id) {
        if (!id) {
            return true;
        }
        const auto* expression = expression_for(expressions, *id);
        if (expression == nullptr) {
            return false;
        }
        merge_dependencies(generate.dependencies, expression->dependencies);
        return true;
    };
    if (!add(generate.initial) || !add(generate.condition)
        || !add(generate.iteration)) {
        return false;
    }
    if constexpr (requires { generate.alternatives; }) {
        for (const auto& alternative : generate.alternatives) {
            for (const auto& choice : alternative.choices) {
                if (!add(choice.left) || !add(choice.right)) {
                    return false;
                }
            }
        }
    }
    if constexpr (requires { generate.defparams; }) {
        for (auto& defparam : generate.defparams) {
            defparam.dependencies = { };
            defparam.dependencies.hierarchy = true;
            const auto* value = expression_for(
                expressions, defparam.value);
            if (value == nullptr) {
                return false;
            }
            merge_dependencies(
                defparam.dependencies, value->dependencies);
            for (const auto& segment : defparam.path) {
                for (const auto index : segment.indices) {
                    const auto* expression = expression_for(
                        expressions, index);
                    if (expression == nullptr) {
                        return false;
                    }
                    merge_dependencies(defparam.dependencies,
                        expression->dependencies);
                }
            }
            canonicalize(defparam.dependencies);
            merge_dependencies(
                generate.dependencies, defparam.dependencies);
        }
    }
    for (auto& nested : generate.nested) {
        if (!annotate_generate(expressions, nested)) {
            return false;
        }
        merge_dependencies(generate.dependencies, nested.dependencies);
    }
    canonicalize(generate.dependencies);
    return true;
}

template <typename Unit, typename Expression>
bool annotate_generates(const std::vector<Expression>& expressions,
    std::vector<Unit>& units)
{
    for (auto& unit : units) {
        for (auto& generate : unit.generates) {
            if (!annotate_generate(expressions, generate)) {
                return false;
            }
        }
    }
    return true;
}

bool add_expression_dependency(
    const std::vector<sv::Expression>& expressions,
    const std::optional<ExpressionId> id,
    ResidualDependencies& dependencies)
{
    if (!id) {
        return true;
    }
    const auto* expression = expression_for(expressions, *id);
    if (expression == nullptr) {
        return false;
    }
    merge_dependencies(dependencies, expression->dependencies);
    return true;
}

bool add_type_dependencies(
    const std::vector<sv::Expression>& expressions,
    const sv::TypeReference& type,
    ResidualDependencies& dependencies)
{
    add_unique(dependencies.types, type.target.target);
    if (type.associative_index)
        add_unique(dependencies.types, type.associative_index->target);
    const auto add_range = [&](const sv::PackedRange& range) {
        return add_expression_dependency(
                   expressions, range.left_expression, dependencies)
            && add_expression_dependency(
                expressions, range.right_expression, dependencies);
    };
    if ((type.packed_range && !add_range(*type.packed_range))
        || !add_expression_dependency(
            expressions, type.queue_maximum, dependencies)) {
        return false;
    }
    for (const auto& actual : type.interface_parameter_actuals) {
        if (!add_expression_dependency(
                expressions, actual.expression, dependencies)
            || (actual.type
                && !add_type_dependencies(
                    expressions, *actual.type, dependencies))) {
            return false;
        }
    }
    if (!std::ranges::all_of(type.unpacked_dimensions, add_range)) {
        return false;
    }
    return std::ranges::all_of(type.container_element_types,
        [&](const auto& element) {
            return add_type_dependencies(
                expressions, element, dependencies);
        });
}

bool annotate_classes(CompiledDesign& design)
{
    const auto& expressions = design.systemverilog_hir.expressions();
    // The dependencies of the expressions inside each method body, gathered
    // in one pass: an expression contributes to every enclosing method
    // scope, in expression order.
    std::unordered_map<std::uint32_t, ResidualDependencies>
        method_scope_dependencies;
    for (const auto& declaration : design.systemverilog_hir.classes()) {
        for (const auto& method : declaration.methods) {
            const auto* member = find_declaration(
                design.systemverilog_hir, method.declaration);
            if (member != nullptr && member->nested_scope) {
                method_scope_dependencies.try_emplace(
                    member->nested_scope->value());
            }
        }
    }
    if (!method_scope_dependencies.empty()) {
        const auto& scopes = design.semantics.scopes();
        for (const auto& expression : expressions) {
            auto scope = expression.scope;
            while (scope.valid() && scope.value() < scopes.size()) {
                const auto found
                    = method_scope_dependencies.find(scope.value());
                if (found != method_scope_dependencies.end()) {
                    merge_dependencies(
                        found->second, expression.dependencies);
                }
                const auto parent = scopes[scope.value()].parent;
                if (!parent) {
                    break;
                }
                scope = *parent;
            }
        }
    }
    for (auto& declaration : design.systemverilog_hir.mutable_classes()) {
        declaration.dependencies = { };
        for (auto& parameter : declaration.parameters) {
            parameter.dependencies = { };
            if ((parameter.type
                    && !add_type_dependencies(expressions,
                        *parameter.type, parameter.dependencies))
                || (parameter.default_type
                    && !add_type_dependencies(expressions,
                        *parameter.default_type, parameter.dependencies))
                || !add_expression_dependency(expressions,
                    parameter.default_value, parameter.dependencies)) {
                return false;
            }
            canonicalize(parameter.dependencies);
            merge_dependencies(
                declaration.dependencies, parameter.dependencies);
        }
        const auto annotate_relation = [&](sv::ClassRelation& relation) {
            relation.dependencies = { };
            for (const auto& actual : relation.actuals) {
                if ((actual.type
                        && !add_type_dependencies(expressions,
                            *actual.type, relation.dependencies))
                    || !add_expression_dependency(expressions,
                        actual.expression, relation.dependencies)) {
                    return false;
                }
            }
            canonicalize(relation.dependencies);
            merge_dependencies(
                declaration.dependencies, relation.dependencies);
            return true;
        };
        if ((declaration.base && !annotate_relation(*declaration.base))
            || !std::ranges::all_of(
                declaration.extended_interfaces, annotate_relation)
            || !std::ranges::all_of(
                declaration.implemented_interfaces, annotate_relation)) {
            return false;
        }
        for (const auto alias : declaration.type_aliases) {
            const auto type = std::ranges::find(
                design.systemverilog_hir.types(), alias,
                &sv::TypeDefinition::declaration);
            if (type == design.systemverilog_hir.types().end()
                || !add_type_dependencies(
                    expressions, type->base, declaration.dependencies)) {
                return false;
            }
            add_unique(declaration.dependencies.types, type->id);
            for (const auto& member : type->members) {
                if (!add_type_dependencies(expressions,
                        member.type, declaration.dependencies)
                    || !add_expression_dependency(expressions,
                        member.initializer, declaration.dependencies)) {
                    return false;
                }
            }
        }
        for (auto& property : declaration.properties) {
            property.dependencies = { };
            if (!add_type_dependencies(
                    expressions, property.type, property.dependencies)
                || !add_expression_dependency(expressions,
                    property.initializer, property.dependencies)) {
                return false;
            }
            canonicalize(property.dependencies);
            merge_dependencies(
                declaration.dependencies, property.dependencies);
        }
        for (auto& method : declaration.methods) {
            method.dependencies = { };
            const auto* member = find_declaration(
                design.systemverilog_hir, method.declaration);
            if (member == nullptr)
                return false;
            if (member->type
                && !add_type_dependencies(
                    expressions, *member->type, method.dependencies)) {
                return false;
            }
            for (const auto child_id : member->children) {
                const auto* child = find_declaration(
                    design.systemverilog_hir, child_id);
                if (child == nullptr
                    || (child->type
                        && !add_type_dependencies(expressions,
                            *child->type, method.dependencies))
                    || (child->default_type
                        && !add_type_dependencies(expressions,
                            *child->default_type, method.dependencies))
                    || !add_expression_dependency(expressions,
                        child->initializer, method.dependencies)) {
                    return false;
                }
            }
            if (member->nested_scope) {
                const auto found = method_scope_dependencies.find(
                    member->nested_scope->value());
                if (found != method_scope_dependencies.end()) {
                    merge_dependencies(method.dependencies, found->second);
                }
            }
            canonicalize(method.dependencies);
            merge_dependencies(
                declaration.dependencies, method.dependencies);
        }
        canonicalize(declaration.dependencies);
    }
    return true;
}

} // namespace

bool normalize_compiled_design(CompiledDesign& design) noexcept
{
    std::optional<NormalizationIndexes> indexes;
    try {
        indexes.emplace();
        indexes->systemverilog_declarations.build(
            design.systemverilog_hir.declarations());
        indexes->vhdl_declarations.build(design.vhdl_hir.declarations());
        indexes->systemverilog_expressions.build(
            design.systemverilog_hir.expressions());
        indexes->vhdl_expressions.build(design.vhdl_hir.expressions());
        indexes->semantic_scopes.build(design.semantics.scopes());
        indexes->semantic_types.build(design.semantics.types());
    } catch (...) {
        indexes.reset();
    }
    struct ActiveIndexes {
        explicit ActiveIndexes(const NormalizationIndexes* active) noexcept
        {
            normalization_indexes = active;
        }
        ~ActiveIndexes() { normalization_indexes = nullptr; }
        ActiveIndexes(const ActiveIndexes&) = delete;
        ActiveIndexes& operator=(const ActiveIndexes&) = delete;
    } active { indexes ? &*indexes : nullptr };
    if (!design.valid()) {
        return false;
    }
    auto& systemverilog_expressions
        = design.systemverilog_hir.mutable_expressions();
    auto& vhdl_expressions = design.vhdl_hir.mutable_expressions();
    // Mutable access advances each HIR revision. Both annotation passes edit
    // the existing expression records in place, so refresh once after both
    // accesses and before resolver lookups begin.
    try {
        design.refresh_lookup_indexes();
    } catch (...) {
        return false;
    }
    if (!annotate_expressions(design, systemverilog_expressions)
        || !annotate_expressions(design, vhdl_expressions)
        || !materialize_systemverilog_ranges(design)
        || !materialize_vhdl_ranges(design)
        || !annotate_generates(
            design.systemverilog_hir.expressions(),
            design.systemverilog_hir.mutable_units())
        || !annotate_generates(
            design.vhdl_hir.expressions(),
            design.vhdl_hir.mutable_units())
        || !annotate_classes(design)) {
        return false;
    }
    for (auto& unit : design.systemverilog_hir.mutable_units()) {
        for (auto& defparam : unit.defparams) {
            defparam.dependencies.hierarchy = true;
            const auto* value = expression_for(
                design.systemverilog_hir.expressions(), defparam.value);
            if (value == nullptr) {
                return false;
            }
            merge_dependencies(defparam.dependencies, value->dependencies);
            for (const auto& segment : defparam.path) {
                for (const auto index : segment.indices) {
                    const auto* expression = expression_for(
                        design.systemverilog_hir.expressions(), index);
                    if (expression == nullptr) {
                        return false;
                    }
                    merge_dependencies(
                        defparam.dependencies, expression->dependencies);
                }
            }
            canonicalize(defparam.dependencies);
        }
    }
    if (!design.valid()) {
        return false;
    }
    try {
        design.refresh_lookup_indexes();
    } catch (...) {
        return false;
    }
    return true;
}

bool vhdl_operator_overloaded_by_design(
    const CompiledDesign& design, const std::string_view operation)
{
    return vhdl_user_operator_overloads(design).contains(lowercase(operation));
}

} // namespace fsim::semantic
