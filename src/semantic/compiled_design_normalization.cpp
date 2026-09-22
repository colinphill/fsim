// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_normalization.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
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

const sv::Declaration* find_declaration(
    const sv::Hir& hir, const DeclarationId id)
{
    const auto found = std::ranges::find(
        hir.declarations(), id, &sv::Declaration::id);
    return found == hir.declarations().end() ? nullptr : &*found;
}

const vhdl::Declaration* find_declaration(
    const vhdl::Hir& hir, const DeclarationId id)
{
    const auto found = std::ranges::find(
        hir.declarations(), id, &vhdl::Declaration::id);
    return found == hir.declarations().end() ? nullptr : &*found;
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
    const auto found = std::ranges::find(
        expressions, id, &Expression::id);
    return found == expressions.end() ? nullptr : &*found;
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
    const CompiledDesign& design, const DeclarationId id)
{
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
    std::set<ExpressionId>& active_expressions);

std::optional<ConstantValue> evaluate_vhdl_expression(
    const CompiledDesign& design, const ExpressionId id,
    const ConstantBindings& bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions)
{
    if (!active_expressions.insert(id).second) {
        return std::nullopt;
    }
    const auto* expression = expression_for(
        design.vhdl_hir.expressions(), id);
    const auto value = expression == nullptr
        ? std::nullopt
        : evaluate_vhdl_constant(design, *expression, bindings,
              active_declarations, active_expressions);
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

std::optional<ConstantValue> evaluate_pure_vhdl_call(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const ConstantBindings& caller_bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions)
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
    if (declaration == nullptr || declaration->statements.size() != 1U
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
                active_declarations, active_expressions);
        } else {
            const auto* formal = find_declaration(
                design.vhdl_hir, formals[index]);
            if (formal != nullptr && formal->initializer) {
                value = evaluate_vhdl_expression(design,
                    *formal->initializer, bindings,
                    active_declarations, active_expressions);
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
        bindings, active_declarations, active_expressions));
}

std::optional<ConstantValue> evaluate_vhdl_constant(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const ConstantBindings& bindings,
    std::set<DeclarationId>& active_declarations,
    std::set<ExpressionId>& active_expressions)
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
            || vhdl_callable_formal(design, declaration_id)
            || !declaration->initializer
            || !active_declarations.insert(declaration_id).second) {
            return std::nullopt;
        }
        const auto value = evaluate_vhdl_expression(design,
            *declaration->initializer, bindings,
            active_declarations, active_expressions);
        active_declarations.erase(declaration_id);
        return value;
    }
    if (expression.kind == vhdl::ExpressionKind::unary
        && expression.operands.size() == 1U) {
        const auto operand = evaluate_vhdl_expression(design,
            expression.operands.front(), bindings,
            active_declarations, active_expressions);
        return operand ? fold_unary(lowercase(expression.text), *operand)
                       : std::nullopt;
    }
    if (expression.kind == vhdl::ExpressionKind::binary
        && expression.operands.size() == 2U) {
        const auto left = evaluate_vhdl_expression(design,
            expression.operands.front(), bindings,
            active_declarations, active_expressions);
        const auto right = evaluate_vhdl_expression(design,
            expression.operands.back(), bindings,
            active_declarations, active_expressions);
        return left && right
            ? fold_binary(lowercase(expression.text), *left, *right)
            : std::nullopt;
    }
    if (expression.kind == vhdl::ExpressionKind::call) {
        return evaluate_pure_vhdl_call(design, expression, bindings,
            active_declarations, active_expressions);
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

template <typename Expression>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign&, const std::vector<Expression>&, const Expression&)
{
    return std::nullopt;
}

template <>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign& design,
    const std::vector<sv::Expression>& expressions,
    const sv::Expression& expression)
{
    if (expression.kind != sv::ExpressionKind::name
        || !expression.referenced_name
        || !expression.referenced_name->selected) {
        return std::nullopt;
    }
    const auto* declaration = find_declaration(
        design.systemverilog_hir,
        *expression.referenced_name->selected);
    if (declaration == nullptr
        || (declaration->form != sv::DeclarationForm::local_parameter
            && declaration->form
                != sv::DeclarationForm::enumeration_literal)
        || !declaration->initializer) {
        return std::nullopt;
    }
    const auto* initializer = expression_for(
        expressions, *declaration->initializer);
    return initializer != nullptr && initializer->folded
        && dependency_free(initializer->dependencies)
        ? constant_value(*initializer) : std::nullopt;
}

template <>
std::optional<ConstantValue> constant_name_value(
    const CompiledDesign& design,
    const std::vector<vhdl::Expression>&,
    const vhdl::Expression& expression)
{
    if (expression.kind != vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    std::set<DeclarationId> active_declarations;
    std::set<ExpressionId> active_expressions { expression.id };
    return evaluate_vhdl_constant(design, expression, { },
        active_declarations, active_expressions);
}

template <typename Expression>
bool fold_expression(const CompiledDesign& design,
    const std::vector<Expression>& expressions, Expression& expression)
{
    using Kind = std::remove_cvref_t<decltype(expression.kind)>;
    if (!expression.nominal_type.empty()) {
        return false;
    }
    const auto operation = lowercase(expression.text);
    auto value = constant_name_value(design, expressions, expression);
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
                active_declarations, active_expressions);
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
    ResidualDependencies&)
{
    return true;
}

template <>
bool add_initializer_dependencies(const CompiledDesign& design,
    const std::vector<sv::Expression>& expressions,
    const sv::Expression& expression,
    ResidualDependencies& dependencies)
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
    ResidualDependencies& dependencies)
{
    if (!expression.referenced_name
        || !expression.referenced_name->selected) {
        return true;
    }
    const auto declaration_id = *expression.referenced_name->selected;
    if (expression.kind == vhdl::ExpressionKind::name) {
        if (vhdl_callable_formal(design, declaration_id)) {
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
    for (std::size_t pass = 0; pass <= expressions.size(); ++pass) {
        bool changed = false;
        for (auto& expression : expressions) {
            ResidualDependencies dependencies;
            add_name_dependencies(
                design, expression.referenced_name, dependencies);
            if (!add_initializer_dependencies(
                    design, expressions, expression, dependencies)) {
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
            const bool rewritten = nested_folded
                && (dependency_free(dependencies)
                    || rewrite_with_residual_name
                    || rewrite_with_residual_call)
                && fold_expression(design, expressions, expression);
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
                expressions, component.binding)) {
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

bool scope_within(
    const Model& model, ScopeId scope, const ScopeId owner)
{
    while (scope.valid() && scope.value() < model.scopes().size()) {
        if (scope == owner) {
            return true;
        }
        const auto parent = model.scopes()[scope.value()].parent;
        if (!parent) {
            return false;
        }
        scope = *parent;
    }
    return false;
}

bool annotate_classes(CompiledDesign& design)
{
    const auto& expressions = design.systemverilog_hir.expressions();
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
                for (const auto& expression : expressions) {
                    if (scope_within(design.semantics,
                            expression.scope, *member->nested_scope)) {
                        merge_dependencies(
                            method.dependencies, expression.dependencies);
                    }
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
    if (!design.valid()
        || !annotate_expressions(
            design, design.systemverilog_hir.mutable_expressions())
        || !annotate_expressions(
            design, design.vhdl_hir.mutable_expressions())
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

} // namespace fsim::semantic
