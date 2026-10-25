// SPDX-License-Identifier: Apache-2.0
#include "application_systemverilog_hir_internal.hpp"

#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <sstream>

namespace fsim::app::application_detail {
namespace systemverilog_hir_detail {

namespace sv = semantic::sv;

namespace {

// The width of a value in the packed image a class property holds an
// unpacked structure as: its members concatenated, first most significant.
[[nodiscard]] std::optional<std::uint64_t> structure_image_width(
    const frontend::Type& type)
{
    if (type.packed_aggregate != frontend::PackedAggregateKind::UnpackedStruct
        || type.systemverilog_container) {
        return type.width();
    }
    std::uint64_t total { };
    for (const auto& member : type.packed_members) {
        const auto width = member.nested_types.empty()
            ? member.width()
            : structure_image_width(member.nested_types.front());
        if (!width) return std::nullopt;
        total += *width;
    }
    return total;
}

// An integral cast to `type`'s width and signedness, as the constraint
// lowering spells it; nothing for a type that is not a bounded integral.
[[nodiscard]] std::optional<std::string> integral_cast_text(
    const frontend::Type& type)
{
    const auto width = type.width();
    if (!width || *width == 0U || *width > 4096U
        || type.domain == frontend::ValueDomain::String
        || type.systemverilog_container
        || type.systemverilog_scalar != frontend::SystemVerilogScalarKind::None
        || !type.systemverilog_class_declaration.empty()) {
        return std::nullopt;
    }
    return std::string { "@sv-cast:" }
        + (type.is_signed ? "signed:" : "unsigned:") + std::to_string(*width);
}

// The value a function returns when its body is one `return e;` or
// `F = e;` and it declares no variables of its own.
[[nodiscard]] const frontend::Expression* single_returned_value(
    const frontend::SystemVerilogClassMethod& method)
{
    if (method.kind != frontend::SystemVerilogClassMethodKind::Function
        || !method.variables.empty() || !method.type_aliases.empty()
        || method.is_virtual || method.is_pure || !method.defined) {
        return nullptr;
    }
    if (method.statements.size() != 1U) return nullptr;
    const auto* body = &method.statements.front();
    while (body->kind == frontend::StatementKind::Block
        && body->statements.size() == 1U) {
        body = &body->statements.front();
    }
    const auto& statement = *body;
    if (statement.kind == frontend::StatementKind::Return) {
        return statement.value.kind == frontend::ExpressionKind::Invalid
            ? nullptr : &statement.value;
    }
    if (statement.kind == frontend::StatementKind::Assignment
        && statement.assignment_kind == frontend::AssignmentKind::Blocking
        && statement.target.kind == frontend::ExpressionKind::Identifier
        && statement.target.text == method.name) {
        return &statement.value;
    }
    return nullptr;
}

[[nodiscard]] bool names_identifier(
    const frontend::Expression& expression, const std::string_view name)
{
    if (expression.kind == frontend::ExpressionKind::Identifier
        && expression.text == name) {
        return true;
    }
    return std::ranges::any_of(expression.operands,
        [&](const frontend::Expression& operand) {
            return names_identifier(operand, name);
        });
}

void substitute_formals(frontend::Expression& expression,
    const std::map<std::string, frontend::Expression, std::less<>>& actuals)
{
    if (expression.kind == frontend::ExpressionKind::Identifier) {
        if (const auto found = actuals.find(expression.text);
            found != actuals.end()) {
            expression = found->second;
            return;
        }
    }
    for (auto& operand : expression.operands) {
        substitute_formals(operand, actuals);
    }
}

// A call in a constraint of a function whose body is one returned
// expression is that expression of its actual arguments, cast to the
// function's type (IEEE 1800-2017 18.5.12): the function sees its
// arguments' solved values, so its result is the expression of them.
void inline_constraint_functions(frontend::Expression& expression,
    const std::vector<frontend::SystemVerilogClassMethod>& methods,
    const std::size_t depth = 0U)
{
    for (auto& operand : expression.operands) {
        inline_constraint_functions(operand, methods, depth);
    }
    if (expression.kind != frontend::ExpressionKind::Call || depth > 8U) {
        return;
    }
    const auto method = std::ranges::find(
        methods, expression.text, &frontend::SystemVerilogClassMethod::name);
    if (method == methods.end()) return;
    const auto* value = single_returned_value(*method);
    const auto result_cast = integral_cast_text(method->return_type);
    if (value == nullptr || !result_cast
        || method->arguments.size() < expression.operands.size()
        || names_identifier(*value, method->name)) {
        return;
    }
    std::map<std::string, frontend::Expression, std::less<>> actuals;
    for (std::size_t index = 0; index < method->arguments.size(); ++index) {
        const auto& formal = method->arguments[index];
        const auto formal_cast = integral_cast_text(formal.type);
        if (formal.direction != frontend::PortDirection::Input
            || formal.reference || !formal_cast) {
            return;
        }
        const auto* actual = index < expression.operands.size()
                && expression.operands[index].kind
                    != frontend::ExpressionKind::Invalid
            ? &expression.operands[index]
            : formal.default_value ? &*formal.default_value : nullptr;
        if (actual == nullptr) return;
        frontend::Expression cast;
        cast.kind = frontend::ExpressionKind::Call;
        cast.text = *formal_cast;
        cast.operands = { *actual };
        cast.span = actual->span;
        actuals.emplace(formal.name, std::move(cast));
    }
    auto body = *value;
    substitute_formals(body, actuals);
    inline_constraint_functions(body, methods, depth + 1U);
    frontend::Expression result;
    result.kind = frontend::ExpressionKind::Call;
    result.text = *result_cast;
    result.span = expression.span;
    result.operands = { std::move(body) };
    expression = std::move(result);
}

[[nodiscard]] bool is_structure(const frontend::Type& type)
{
    return !type.packed_members.empty()
        && (type.packed_aggregate == frontend::PackedAggregateKind::Struct
            || type.packed_aggregate
                == frontend::PackedAggregateKind::UnpackedStruct);
}

sv::ClassLifetime class_lifetime(
    const frontend::SystemVerilogClassLifetime lifetime) noexcept
{
    switch (lifetime) {
    case frontend::SystemVerilogClassLifetime::Static:
        return sv::ClassLifetime::static_lifetime;
    case frontend::SystemVerilogClassLifetime::Automatic:
        return sv::ClassLifetime::automatic;
    case frontend::SystemVerilogClassLifetime::Inherited:
        return sv::ClassLifetime::inherited;
    }
    return sv::ClassLifetime::inherited;
}

sv::ClassMethodKind class_method_kind(
    const frontend::SystemVerilogClassMethodKind kind) noexcept
{
    switch (kind) {
    case frontend::SystemVerilogClassMethodKind::Constructor:
        return sv::ClassMethodKind::constructor;
    case frontend::SystemVerilogClassMethodKind::Task:
        return sv::ClassMethodKind::task;
    case frontend::SystemVerilogClassMethodKind::Function:
        return sv::ClassMethodKind::function;
    }
    return sv::ClassMethodKind::function;
}

std::string type_signature(const frontend::Type& type)
{
    if (!type.systemverilog_class_declaration.empty()) {
        return type.systemverilog_class_declaration;
    }
    if (!type.named_type.empty()) {
        return type.named_type;
    }
    if (!type.spelling.empty()) {
        return type.spelling;
    }
    if (const auto width = type.width()) {
        return std::to_string(*width);
    }
    return "?";
}

std::string method_profile_identity(
    const frontend::SystemVerilogClassMethod& method)
{
    std::ostringstream output;
    output << static_cast<unsigned>(method.kind) << ':' << method.name << '(';
    for (const auto& argument : method.arguments) {
        output << static_cast<unsigned>(argument.direction) << ':'
               << type_signature(argument.type) << ';';
    }
    output << ")->" << type_signature(method.return_type);
    return output.str();
}

frontend::TaskArgument task_argument(
    const frontend::FunctionArgument& input)
{
    return { input.name, input.type, input.direction, input.span,
        input.reference, input.default_value, input.const_reference,
        input.static_reference };
}

// The value of a class constant initializer made of decimal or based
// integer literals joined by `+` (class enum literals, IEEE 1800-2017 6.19).
std::optional<std::uint64_t> class_constant_value(
    const frontend::Expression& expression)
{
    if (expression.kind == frontend::ExpressionKind::Binary
        && expression.text == "+" && expression.operands.size() == 2U) {
        const auto left = class_constant_value(expression.operands[0]);
        const auto right = class_constant_value(expression.operands[1]);
        if (!left || !right) {
            return std::nullopt;
        }
        return *left + *right;
    }
    // A sized literal (`2'd1`) may be a logic literal; x and z digits
    // still fail below.
    if (expression.kind != frontend::ExpressionKind::IntegerLiteral
        && expression.kind != frontend::ExpressionKind::LogicLiteral) {
        return std::nullopt;
    }
    std::string digits;
    unsigned base = 10U;
    const auto quote = expression.text.find('\'');
    auto text = std::string_view { expression.text };
    if (quote != std::string_view::npos) {
        text.remove_prefix(quote + 1U);
        if (!text.empty() && (text.front() == 's' || text.front() == 'S')) {
            text.remove_prefix(1U);
        }
        if (text.empty()) {
            return std::nullopt;
        }
        switch (text.front()) {
        case 'd': case 'D': base = 10U; break;
        case 'h': case 'H': base = 16U; break;
        case 'b': case 'B': base = 2U; break;
        case 'o': case 'O': base = 8U; break;
        default: return std::nullopt;
        }
        text.remove_prefix(1U);
    }
    std::uint64_t value { };
    bool any { };
    for (const auto character : text) {
        if (character == '_') {
            continue;
        }
        unsigned digit { };
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a') + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A') + 10U;
        } else {
            return std::nullopt;
        }
        if (digit >= base
            || value > (std::numeric_limits<std::uint64_t>::max() - digit)
                / base) {
            return std::nullopt;
        }
        value = value * base + digit;
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return value;
}


// The values of an enumeration type's literals in declaration order; an
// implicit value follows the previous one (IEEE 1800-2017 6.19). Empty
// when the type is not an enumeration or a value is not a literal sum.
std::vector<std::uint64_t> enumeration_values(const frontend::Type& type)
{
    std::vector<std::uint64_t> result;
    std::uint64_t next { };
    std::map<std::string, std::uint64_t, std::less<>> earlier;
    const auto evaluate = [&](const auto& self,
                              const frontend::Expression& expression)
        -> std::optional<std::uint64_t> {
        if (expression.kind == frontend::ExpressionKind::Identifier) {
            const auto found = earlier.find(expression.text);
            return found == earlier.end()
                ? std::nullopt
                : std::optional { found->second };
        }
        if (expression.kind == frontend::ExpressionKind::Binary
            && expression.text == "+" && expression.operands.size() == 2U) {
            const auto left = self(self, expression.operands[0]);
            const auto right = self(self, expression.operands[1]);
            return left && right
                ? std::optional { *left + *right }
                : std::nullopt;
        }
        return class_constant_value(expression);
    };
    for (std::size_t index = 0; index < type.enumeration_literals.size();
         ++index) {
        std::optional<std::uint64_t> value = next;
        if (index < type.systemverilog_enumeration_values.size()) {
            value = evaluate(evaluate,
                type.systemverilog_enumeration_values[index]);
        }
        if (!value) {
            return { };
        }
        earlier.insert_or_assign(type.enumeration_literals[index], *value);
        result.push_back(*value);
        next = *value + 1U;
    }
    return result;
}

} // namespace

frontend::FunctionDeclaration class_function(
    const frontend::SystemVerilogClassMethod& input)
{
    frontend::FunctionDeclaration output;
    output.name = input.name;
    output.return_type = input.return_type;
    output.arguments = input.arguments;
    output.type_aliases = input.type_aliases;
    output.variables = input.variables;
    output.statements = input.statements;
    output.automatic = true;
    output.lifetime_explicit = true;
    output.pure = input.is_pure;
    output.defined = input.defined;
    output.span = input.span;
    return output;
}

frontend::TaskDeclaration class_task(
    const frontend::SystemVerilogClassMethod& input)
{
    frontend::TaskDeclaration output;
    output.name = input.name;
    output.type_aliases = input.type_aliases;
    output.variables = input.variables;
    output.statements = input.statements;
    output.automatic = true;
    output.lifetime_explicit = true;
    output.span = input.span;
    for (const auto& argument : input.arguments) {
        output.arguments.push_back(task_argument(argument));
    }
    return output;
}

sv::ClassVisibility SystemVerilogHirBuilder::class_visibility(
    const frontend::SystemVerilogClassVisibility visibility) noexcept
{
    switch (visibility) {
    case frontend::SystemVerilogClassVisibility::Public:
        return sv::ClassVisibility::public_access;
    case frontend::SystemVerilogClassVisibility::Protected:
        return sv::ClassVisibility::protected_access;
    case frontend::SystemVerilogClassVisibility::Local:
        return sv::ClassVisibility::local_access;
    }
    return sv::ClassVisibility::public_access;
}

sv::ConstraintExpressionKind SystemVerilogHirBuilder::constraint_kind(
    const frontend::Expression& input) noexcept
{
    using FrontendKind = frontend::ExpressionKind;
    using HirKind = sv::ConstraintExpressionKind;
    switch (input.kind) {
    case FrontendKind::Invalid:
        return HirKind::invalid;
    case FrontendKind::Identifier:
        return HirKind::name;
    case FrontendKind::IntegerLiteral:
        return HirKind::integer_literal;
    case FrontendKind::BooleanLiteral:
        return HirKind::boolean_literal;
    case FrontendKind::LogicLiteral:
        return HirKind::logic_literal;
    case FrontendKind::StringLiteral:
        return HirKind::string_literal;
    case FrontendKind::Unary:
    case FrontendKind::Update:
        return HirKind::unary;
    case FrontendKind::Binary:
        return HirKind::binary;
    case FrontendKind::Index:
        return HirKind::index;
    case FrontendKind::Slice:
        return HirKind::slice;
    case FrontendKind::Aggregate:
    case FrontendKind::DefaultChoice:
        return HirKind::assignment_pattern;
    case FrontendKind::Concatenation:
        return HirKind::concatenation;
    case FrontendKind::Replication:
        return HirKind::replication;
    case FrontendKind::Conditional:
        return HirKind::conditional;
    case FrontendKind::Call:
        if (input.text == "?:")
            return HirKind::conditional;
        if (input.text == "inside")
            return HirKind::inside_set;
        if (input.text == "@inside-range")
            return HirKind::inside_range;
        if (input.text == "dist")
            return HirKind::distribution;
        if (input.text == "@dist-:=" || input.text == "@dist-:/")
            return HirKind::distribution_item;
        if (input.text == "soft")
            return HirKind::soft;
        if (input.text == "@constraint-block")
            return HirKind::constraint_block;
        if (input.text == "@constraint-implies")
            return HirKind::implication;
        if (input.text == "@constraint-if")
            return HirKind::conditional_constraint;
        if (input.text == "@constraint-foreach")
            return HirKind::foreach_constraint;
        if (input.text == "@solve-before")
            return HirKind::solve_before;
        if (input.text == "@solve-list")
            return HirKind::solve_list;
        if (input.text == "@constraint-unique")
            return HirKind::unique_constraint;
        return HirKind::call;
    }
    return HirKind::invalid;
}

sv::ConstraintExpression SystemVerilogHirBuilder::constraint_expression(
    const frontend::Expression& input)
{
    sv::ConstraintExpression output;
    output.kind = constraint_kind(input);
    output.text = input.text;
    output.source = source(input.span);
    for (const auto& operand : input.operands) {
        output.operands.push_back(constraint_expression(operand));
    }
    return output;
}

sv::TypeReference SystemVerilogHirBuilder::class_property_type(
    const frontend::Type& input,
    const semantic::SourceSpanId type_source)
{
    sv::TypeReference output;
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    output.target = { { }, type_source, spelling };
    output.signed_value = input.is_signed;
    output.executable_width = input.width();
    output.four_state = input.domain == frontend::ValueDomain::Logic4
        || input.domain == frontend::ValueDomain::Integer;
    if (!input.systemverilog_class_declaration.empty()) {
        output.value_form = sv::TypeForm::class_handle;
        output.class_identity = input.systemverilog_class_declaration;
    } else if (input.domain == frontend::ValueDomain::String) {
        output.value_form = sv::TypeForm::string;
    } else if (input.systemverilog_container) {
        switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
            output.container_form = sv::TypeForm::dynamic_array;
            break;
        case frontend::SystemVerilogContainerKind::Queue:
            output.container_form = sv::TypeForm::queue;
            break;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
            output.container_form = sv::TypeForm::associative_array;
            if (input.systemverilog_container->associative_index_type) {
                const auto index_type = class_property_type(
                    *input.systemverilog_container->associative_index_type,
                    type_source);
                output.associative_index = index_type.target;
                output.associative_index_metadata
                    = sv::make_associative_index_metadata(index_type);
            }
            break;
        case frontend::SystemVerilogContainerKind::StaticArray:
            output.container_form = sv::TypeForm::static_array;
            break;
        }
    } else if (!input.enumeration_literals.empty()) {
        output.value_form = sv::TypeForm::enumeration;
    } else {
        output.value_form = sv::TypeForm::packed_integral;
    }
    if (input.packed_range) {
        output.packed_range = packed_range(*input.packed_range, type_source);
    }
    return output;
}

bool SystemVerilogHirBuilder::owner_matches(
    const std::string_view owner,
    const std::string_view selected) noexcept
{
    return owner == selected
        || (owner.size() > selected.size() + 2U
            && owner.ends_with(selected)
            && owner[owner.size() - selected.size() - 1U] == ':');
}

bool SystemVerilogHirBuilder::specializes_or_derives(
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view declaration_identity) const
{
    const auto* current = &specialization;
    while (current != nullptr) {
        if (current->declaration_identity == declaration_identity)
            return true;
        if (current->base_specialization_identity.empty())
            return false;
        const auto base = std::ranges::find(
            class_specializations_, current->base_specialization_identity,
            &frontend::SystemVerilogClassSpecialization::specialization_identity);
        current = base == class_specializations_.end() ? nullptr : &*base;
    }
    return false;
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::property_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string spelling,
    const semantic::SourceSpanId binding_source)
{
    enum class Selection {
        ordinary,
        this_object,
        super_object,
        qualified,
    };
    auto selection = Selection::ordinary;
    std::string owner;
    if (spelling.starts_with("this.")) {
        spelling.erase(0, 5U);
        selection = Selection::this_object;
    } else if (spelling.starts_with("super.")) {
        spelling.erase(0, 6U);
        selection = Selection::super_object;
    } else if (const auto separator = spelling.rfind("::");
        separator != std::string::npos) {
        owner = spelling.substr(0, separator);
        spelling.erase(0, separator + 2U);
        selection = Selection::qualified;
    }
    const auto found = std::ranges::find_if(
        specialization.properties.rbegin(),
        specialization.properties.rend(),
        [&](const frontend::SystemVerilogClassPropertyLayout& property) {
            if (property.name != spelling)
                return false;
            if (selection == Selection::super_object) {
                return property.owner_identity
                    != declaration.canonical_identity;
            }
            if (selection == Selection::qualified)
                return owner_matches(property.owner_identity, owner);
            return true;
        });
    if (found == specialization.properties.rend())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::property;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = found->owner_identity + "::" + found->name;
    binding.type = class_property_type(found->type, binding_source);
    // An unpacked structure is held as its packed image.
    if (!binding.type.executable_width
        && found->type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedStruct) {
        binding.type.executable_width = structure_image_width(found->type);
    }
    // The elements of an array of an enumeration are enumerated values.
    if (found->type.systemverilog_container
        && (!found->type.enumeration_literals.empty()
            || (found->type.systemverilog_container->element_types.size() == 1U
                && !found->type.systemverilog_container->element_types.front()
                        .enumeration_literals.empty()))) {
        binding.type.value_form = sv::TypeForm::enumeration;
    }
    // A property of this class keeps its declared type, as its runtime
    // descriptor does, so an enumeration keeps its nominal type.
    if (found->owner_identity == declaration.canonical_identity) {
        const auto declared = std::ranges::find_if(declaration.properties,
            [&](const frontend::SystemVerilogClassProperty& property) {
                return property.declaration.name == found->name;
            });
        if (declared != declaration.properties.end()
            && !declared->declaration.type.enumeration_literals.empty()) {
            const auto declared_type = class_property_type(
                declared->declaration.type, binding_source);
            binding.type.target = declared_type.target;
            binding.type.value_form = declared_type.value_form;
        }
    }
    // A class constant, such as a class enum literal, is a constant of
    // the constraint (IEEE 1800-2017 18.5.1).
    if (found->is_static && found->is_const && found->initializer) {
        if (const auto value = class_constant_value(*found->initializer)) {
            binding.constant_value = std::to_string(*value);
        }
    }
    return binding;
}

// `h.m` through a class-handle property names property m of the object h
// refers to; a constraint may name the members of a random handle's object
// (IEEE 1800-2017 18.5.9). Its identity joins each property's canonical
// identity with '.', the key the randomizer gives that object's variable.
std::optional<sv::ConstraintBinding>
SystemVerilogHirBuilder::dotted_property_binding(
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string_view spelling,
    const semantic::SourceSpanId binding_source)
{
    if (spelling.starts_with("this.")) {
        spelling.remove_prefix(5U);
    }
    if (spelling.find('.') == std::string_view::npos
        || spelling.find("::") != std::string_view::npos) {
        return std::nullopt;
    }
    const auto* current = &specialization;
    std::string identity;
    while (true) {
        const auto dot = spelling.find('.');
        const auto segment = spelling.substr(0U, dot);
        const auto found = std::ranges::find_if(
            current->properties.rbegin(), current->properties.rend(),
            [&](const frontend::SystemVerilogClassPropertyLayout& property) {
                return property.name == segment;
            });
        if (found == current->properties.rend() || found->is_static) {
            return std::nullopt;
        }
        if (!identity.empty()) {
            identity += '.';
        }
        identity += found->owner_identity + "::" + found->name;
        if (dot == std::string_view::npos) {
            sv::ConstraintBinding binding;
            binding.kind = sv::ConstraintReferenceKind::property;
            binding.specialization_identity
                = specialization.specialization_identity;
            binding.canonical_identity = std::move(identity);
            binding.type = class_property_type(found->type, binding_source);
            return binding;
        }
        const auto& type = found->type;
        // `s.m.n` through a structure property names its member variable
        // `s.m.n`, which the randomizer gives each member (IEEE 1800-2017
        // 7.2, 18.4).
        if (!type.systemverilog_container && is_structure(type)) {
            const frontend::Type* aggregate = &type;
            auto rest = spelling.substr(dot + 1U);
            while (true) {
                const auto next = rest.find('.');
                const auto member_name = rest.substr(0U, next);
                const auto member = std::ranges::find(
                    aggregate->packed_members, member_name,
                    &frontend::PackedMember::name);
                if (member == aggregate->packed_members.end()) {
                    return std::nullopt;
                }
                identity += '.';
                identity += member_name;
                frontend::Type member_type;
                if (member->nested_types.empty()) {
                    member_type.domain = member->domain;
                    member_type.spelling = member->spelling;
                    member_type.packed_range = member->packed_range;
                    member_type.is_signed = member->is_signed;
                } else {
                    member_type = member->nested_types.front();
                }
                if (next == std::string_view::npos) {
                    sv::ConstraintBinding binding;
                    binding.kind = sv::ConstraintReferenceKind::property;
                    binding.specialization_identity
                        = specialization.specialization_identity;
                    binding.canonical_identity = std::move(identity);
                    binding.type
                        = class_property_type(member_type, binding_source);
                    if (!binding.type.executable_width) {
                        binding.type.executable_width
                            = structure_image_width(member_type);
                    }
                    return binding;
                }
                if (member->nested_types.empty()
                    || member_type.systemverilog_container
                    || !is_structure(member->nested_types.front())) {
                    return std::nullopt;
                }
                aggregate = &member->nested_types.front();
                rest.remove_prefix(next + 1U);
            }
        }
        if (type.systemverilog_class_declaration.empty()
            || type.systemverilog_container) {
            return std::nullopt;
        }
        const frontend::SystemVerilogClassSpecialization* next = nullptr;
        std::size_t matches { };
        for (const auto& candidate : class_specializations_) {
            if (candidate.specialization_identity
                == type.systemverilog_class_name) {
                next = &candidate;
                matches = 1U;
                break;
            }
            if (candidate.declaration_identity
                == type.systemverilog_class_declaration) {
                next = &candidate;
                ++matches;
            }
        }
        if (next == nullptr || matches != 1U) {
            return std::nullopt;
        }
        current = next;
        spelling.remove_prefix(dot + 1U);
    }
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::parameter_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view spelling,
    const semantic::SourceSpanId binding_source)
{
    const auto value = std::ranges::find(
        specialization.parameter_values,
        spelling,
        &std::pair<std::string, std::string>::first);
    if (value == specialization.parameter_values.end())
        return std::nullopt;
    const auto formal = std::ranges::find(
        declaration.parameters,
        spelling,
        &frontend::ParameterDeclaration::name);
    if (formal == declaration.parameters.end())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::parameter;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = declaration.canonical_identity
        + "::" + std::string { spelling };
    binding.type = class_property_type(formal->type, binding_source);
    binding.constant_value = value->second;
    return binding;
}

void SystemVerilogHirBuilder::collect_unit_parameters(
    const frontend::ParsedDesign& parsed)
{
    unit_parameters_.clear();
    for (const auto& unit : parsed.units) {
        if (unit.language == frontend::Language::Vhdl2008) {
            continue;
        }
        for (const auto& parameter : unit.parameters) {
            if (parameter.kind == frontend::ParameterKind::Type
                || parameter.type.domain == frontend::ValueDomain::String) {
                continue;
            }
            if (const auto value = class_constant_value(
                    parameter.default_value)) {
                unit_parameters_.emplace(parameter.name,
                    UnitParameter { unit.name, parameter.type, *value });
            }
        }
    }
}

std::optional<sv::ConstraintBinding>
SystemVerilogHirBuilder::unit_parameter_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view spelling,
    const semantic::SourceSpanId binding_source)
{
    // The class's own unit first; otherwise a name only one unit declares.
    const auto [first, last] = unit_parameters_.equal_range(spelling);
    const UnitParameter* selected = nullptr;
    std::size_t candidates { };
    for (auto entry = first; entry != last; ++entry) {
        ++candidates;
        const auto& unit = entry->second.unit;
        if (!unit.empty()
            && declaration.canonical_identity.find("::" + unit + "::")
                != std::string::npos) {
            selected = &entry->second;
            candidates = 1U;
            break;
        }
        selected = &entry->second;
    }
    if (selected == nullptr || candidates != 1U) {
        return std::nullopt;
    }
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::parameter;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = selected->unit + "::" + std::string { spelling };
    binding.type = class_property_type(selected->type, binding_source);
    if (!binding.type.executable_width) {
        binding.type.executable_width = 32U;
        binding.type.signed_value = true;
    }
    binding.constant_value = std::to_string(selected->value);
    return binding;
}

std::optional<sv::ConstraintBinding>
SystemVerilogHirBuilder::state_variable_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view spelling)
{
    // Only a simple name; a hierarchical or scoped name is not a variable
    // of the declaring unit.
    if (spelling.empty()
        || spelling.find_first_of(".:") != std::string_view::npos) {
        return std::nullopt;
    }
    const sv::Declaration* selected = nullptr;
    std::string owner;
    std::size_t candidates { };
    for (const auto& unit : hir_.units()) {
        if (declaration.canonical_identity.find("::" + unit.name + "::")
            == std::string::npos) {
            continue;
        }
        for (const auto id : unit.declarations) {
            const auto* candidate = hir_.find_declaration(id);
            if (candidate == nullptr || candidate->scope != unit.scope
                || candidate->name != spelling
                || (candidate->form != sv::DeclarationForm::variable
                    && candidate->form != sv::DeclarationForm::net)
                || !candidate->declared_value || !candidate->type) {
                continue;
            }
            selected = candidate;
            owner = unit.name;
            ++candidates;
        }
    }
    if (selected == nullptr || candidates != 1U) {
        return std::nullopt;
    }
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::state;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity
        = "$state::" + owner + "::" + std::string { spelling };
    binding.type = *selected->type;
    binding.declaration = selected->id;
    return binding;
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::method_binding(
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string spelling,
    const semantic::SourceSpanId binding_source)
{
    if (spelling.starts_with('.'))
        spelling.erase(0, 1U);
    if (const auto separator = spelling.rfind("::");
        separator != std::string::npos) {
        spelling.erase(0, separator + 2U);
    }
    const auto found = std::ranges::find(
        specialization.methods,
        spelling,
        &frontend::SystemVerilogClassMethodProfile::name);
    if (found == specialization.methods.end())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::method;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = found->canonical_identity;
    binding.type = class_property_type(found->return_type, binding_source);
    return binding;
}

void SystemVerilogHirBuilder::resolve_constraint_expression(
    sv::ConstraintExpression& expression,
    const frontend::SystemVerilogClassDeclaration& declaration,
    const std::optional<std::string_view> enclosing_foreach)
{
    // `a.size` without parentheses calls the size method of array `a`
    // (IEEE 1800-2017 13.4.5).
    if (expression.kind == sv::ConstraintExpressionKind::name
        && expression.text.ends_with(".size")
        && expression.text.size() > 5U) {
        const auto array = expression.text.substr(
            0, expression.text.size() - 5U);
        const bool container = std::ranges::any_of(class_specializations_,
            [&](const frontend::SystemVerilogClassSpecialization& candidate) {
                return specializes_or_derives(
                           candidate, declaration.canonical_identity)
                    && std::ranges::any_of(candidate.properties,
                        [&](const frontend::SystemVerilogClassPropertyLayout&
                                property) {
                            return property.name == array
                                && property.type.systemverilog_container;
                        });
            });
        if (container) {
            sv::ConstraintExpression receiver;
            receiver.kind = sv::ConstraintExpressionKind::name;
            receiver.text = array;
            receiver.source = expression.source;
            expression.kind = sv::ConstraintExpressionKind::call;
            expression.text = ".size";
            expression.operands = { std::move(receiver) };
        }
    }
    // `T'(x)` with a typedef of a packed integral type casts to its width
    // and signedness (IEEE 1800-2017 6.24.1).
    if (constexpr std::string_view cast_prefix { "@sv-cast:" };
        expression.kind == sv::ConstraintExpressionKind::call
        && expression.text.starts_with(cast_prefix)) {
        const auto target = expression.text.substr(cast_prefix.size());
        const bool builtin = target == "signed" || target == "unsigned"
            || target == "byte" || target == "shortint" || target == "int"
            || target == "longint" || target == "integer" || target == "bit"
            || target == "logic" || target == "reg" || target == "time"
            || (!target.empty()
                && std::ranges::all_of(target, [](const char c) {
                       return c >= '0' && c <= '9';
                   }));
        for (const auto& candidate : hir_.declarations()) {
            if (builtin
                || candidate.form != sv::DeclarationForm::typedef_declaration
                || candidate.name != target || !candidate.type
                || candidate.type->packed_dimensions.size() > 1U) {
                continue;
            }
            const auto& type = *candidate.type;
            const auto& spelling = type.target.spelling;
            std::optional<std::uint64_t> width;
            if (type.packed_range && type.packed_range->left
                && type.packed_range->right
                && (spelling == "bit" || spelling == "logic"
                    || spelling == "reg")) {
                const auto left = *type.packed_range->left;
                const auto right = *type.packed_range->right;
                width = static_cast<std::uint64_t>(
                    (left > right ? left - right : right - left) + 1);
            } else if (!type.packed_range) {
                width = spelling == "byte" ? 8U
                    : spelling == "shortint" ? 16U
                    : spelling == "int" || spelling == "integer" ? 32U
                    : spelling == "longint" ? 64U
                    : spelling == "bit" || spelling == "logic"
                        || spelling == "reg"
                    ? std::optional<std::uint64_t> { 1U }
                    : std::nullopt;
            }
            if (width && *width != 0U && *width <= 4096U) {
                expression.text = std::string { cast_prefix }
                    + (type.signed_value ? "signed:" : "unsigned:")
                    + std::to_string(*width);
            }
            break;
        }
    }
    // A member of a packed union property (`u.a`) is a constant
    // part-select of the property's bits (IEEE 1800-2017 7.3.1, 18.3).
    // A structure's members are variables of their own (see
    // dotted_property_binding). `s.size` names a member; the size method
    // of an array was rewritten above.
    if (expression.kind == sv::ConstraintExpressionKind::name
        && expression.text.find('.') != std::string::npos) {
        std::string_view path { expression.text };
        if (path.starts_with("this.")) path.remove_prefix(5U);
        const auto dot = path.find('.');
        const auto root = path.substr(0U, dot);
        const frontend::Type* root_type = nullptr;
        for (const auto& candidate : class_specializations_) {
            if (!specializes_or_derives(
                    candidate, declaration.canonical_identity)) {
                continue;
            }
            for (const auto& property : candidate.properties) {
                if (property.name == root) {
                    root_type = &property.type;
                    break;
                }
            }
            if (root_type != nullptr) break;
        }
        if (dot != std::string_view::npos && root_type != nullptr
            && !root_type->packed_members.empty()
            && (root_type->packed_aggregate
                    == frontend::PackedAggregateKind::Union
                || root_type->packed_aggregate
                    == frontend::PackedAggregateKind::TaggedUnion)
            && !root_type->systemverilog_container) {
            const frontend::Type* current = root_type;
            std::uint64_t offset { };
            std::optional<std::uint64_t> width;
            auto remaining = path.substr(dot + 1U);
            while (current != nullptr && !remaining.empty()) {
                const auto next = remaining.find('.');
                const auto segment = remaining.substr(0U, next);
                const auto member = std::ranges::find(
                    current->packed_members, segment,
                    &frontend::PackedMember::name);
                if (member == current->packed_members.end()) {
                    width.reset();
                    break;
                }
                if (current->packed_aggregate
                    == frontend::PackedAggregateKind::UnpackedStruct) {
                    // The members after this one fill the lower bits.
                    std::optional<std::uint64_t> below { 0U };
                    for (auto later = std::next(member);
                         below && later != current->packed_members.end();
                         ++later) {
                        const auto later_width = later->nested_types.empty()
                            ? later->width()
                            : structure_image_width(
                                  later->nested_types.front());
                        below = later_width
                            ? std::optional { *below + *later_width }
                            : std::nullopt;
                    }
                    width = member->nested_types.empty() ? member->width()
                        : structure_image_width(
                              member->nested_types.front());
                    if (!below || !width) {
                        width.reset();
                        break;
                    }
                    offset += *below;
                } else {
                    if (!member->width()) {
                        width.reset();
                        break;
                    }
                    offset += member->lsb_offset;
                    width = member->width();
                }
                remaining = next == std::string_view::npos
                    ? std::string_view { }
                    : remaining.substr(next + 1U);
                current = !remaining.empty() && !member->nested_types.empty()
                    ? &member->nested_types.front()
                    : nullptr;
                if (!remaining.empty() && current == nullptr) {
                    width.reset();
                    break;
                }
            }
            if (width && *width != 0U && remaining.empty()) {
                const auto literal = [&](const std::uint64_t value) {
                    sv::ConstraintExpression result;
                    result.kind = sv::ConstraintExpressionKind::integer_literal;
                    result.text = std::to_string(value);
                    result.source = expression.source;
                    return result;
                };
                sv::ConstraintExpression receiver;
                receiver.kind = sv::ConstraintExpressionKind::name;
                receiver.text = std::string { root };
                receiver.source = expression.source;
                expression.kind = sv::ConstraintExpressionKind::slice;
                expression.text = ":";
                expression.operands = { std::move(receiver),
                    literal(offset + *width - 1U), literal(offset) };
            }
        }
    }
    auto foreach_iterator = enclosing_foreach;
    if (expression.kind == sv::ConstraintExpressionKind::foreach_constraint
        && expression.operands.size() == 2U
        && expression.operands[0].kind
            == sv::ConstraintExpressionKind::index
        && expression.operands[0].operands.size() == 2U
        && expression.operands[0].operands[1].kind
            == sv::ConstraintExpressionKind::name) {
        foreach_iterator = expression.operands[0].operands[1].text;
    }
    for (auto& operand : expression.operands) {
        resolve_constraint_expression(operand, declaration, foreach_iterator);
    }
    for (const auto& specialization : class_specializations_) {
        if (!specializes_or_derives(
                specialization, declaration.canonical_identity)) {
            continue;
        }
        std::optional<sv::ConstraintBinding> binding;
        if (expression.kind == sv::ConstraintExpressionKind::name) {
            if (foreach_iterator && expression.text == *foreach_iterator) {
                sv::ConstraintBinding local;
                local.kind = sv::ConstraintReferenceKind::local_variable;
                local.specialization_identity
                    = specialization.specialization_identity;
                local.canonical_identity = declaration.canonical_identity
                    + "::$foreach::" + expression.text;
                local.type.target = { { }, expression.source, "int" };
                local.type.value_form = sv::TypeForm::packed_integral;
                local.type.signed_value = true;
                local.type.executable_width = 32;
                local.type.four_state = true;
                binding = std::move(local);
            } else {
                binding = property_binding(
                    declaration, specialization, expression.text,
                    expression.source);
                if (!binding) {
                    binding = dotted_property_binding(
                        specialization, expression.text, expression.source);
                }
                if (!binding) {
                    binding = parameter_binding(
                        declaration, specialization, expression.text,
                        expression.source);
                }
                if (!binding) {
                    binding = unit_parameter_binding(
                        declaration, specialization, expression.text,
                        expression.source);
                }
                // A literal of an enumerated property's type is a constant
                // (IEEE 1800-2017 18.5, 6.19).
                if (!binding) {
                    for (const auto& property : declaration.properties) {
                        const auto& type = property.declaration.type;
                        const auto literal = std::ranges::find(
                            type.enumeration_literals, expression.text);
                        if (literal == type.enumeration_literals.end()) {
                            continue;
                        }
                        const auto values = enumeration_values(type);
                        const auto index = static_cast<std::size_t>(
                            literal - type.enumeration_literals.begin());
                        if (index >= values.size()) {
                            continue;
                        }
                        sv::ConstraintBinding constant;
                        constant.kind = sv::ConstraintReferenceKind::parameter;
                        constant.specialization_identity
                            = specialization.specialization_identity;
                        constant.canonical_identity
                            = declaration.canonical_identity + "::"
                            + expression.text;
                        // The layout gives the width; the declaration the
                        // enumeration's nominal type.
                        const auto layout = std::ranges::find_if(
                            specialization.properties,
                            [&](const frontend::SystemVerilogClassPropertyLayout&
                                    candidate) {
                                return candidate.name
                                        == property.declaration.name
                                    && candidate.owner_identity
                                        == declaration.canonical_identity;
                            });
                        if (layout == specialization.properties.end()) {
                            continue;
                        }
                        constant.type = class_property_type(
                            layout->type, expression.source);
                        const auto declared_type = class_property_type(
                            type, expression.source);
                        constant.type.target = declared_type.target;
                        constant.type.value_form = declared_type.value_form;
                        constant.constant_value
                            = std::to_string(values[index]);
                        binding = std::move(constant);
                        break;
                    }
                }
                // Likewise a literal of the element type of an array, or of
                // an inherited property's type.
                // Structure members count too (`items[i].kind == WRITE`).
                if (!binding) {
                    const auto enumeration = [&](const auto& self,
                                                 const frontend::Type& type,
                                                 const std::size_t depth)
                        -> const frontend::Type* {
                        if (std::ranges::find(type.enumeration_literals,
                                expression.text)
                            != type.enumeration_literals.end()) {
                            return &type;
                        }
                        if (depth == 8U) {
                            return nullptr;
                        }
                        if (type.systemverilog_container) {
                            for (const auto& element :
                                type.systemverilog_container->element_types) {
                                if (const auto* found
                                    = self(self, element, depth + 1U)) {
                                    return found;
                                }
                            }
                        }
                        for (const auto& member : type.packed_members) {
                            for (const auto& nested : member.nested_types) {
                                if (const auto* found
                                    = self(self, nested, depth + 1U)) {
                                    return found;
                                }
                            }
                        }
                        return nullptr;
                    };
                    for (const auto& layout : specialization.properties) {
                        const frontend::Type* type
                            = enumeration(enumeration, layout.type, 0U);
                        if (type == nullptr) {
                            continue;
                        }
                        const auto literal = std::ranges::find(
                            type->enumeration_literals, expression.text);
                        const auto values = enumeration_values(*type);
                        const auto index = static_cast<std::size_t>(
                            literal - type->enumeration_literals.begin());
                        if (index >= values.size()) {
                            continue;
                        }
                        sv::ConstraintBinding constant;
                        constant.kind = sv::ConstraintReferenceKind::parameter;
                        constant.specialization_identity
                            = specialization.specialization_identity;
                        constant.canonical_identity
                            = layout.owner_identity + "::" + expression.text;
                        constant.type = class_property_type(
                            *type, expression.source);
                        constant.type.container_form.reset();
                        constant.type.value_form = sv::TypeForm::enumeration;
                        constant.constant_value
                            = std::to_string(values[index]);
                        binding = std::move(constant);
                        break;
                    }
                }
                // A variable of the enclosing unit is a state variable of
                // the solve (IEEE 1800-2017 18.5); every class-scope
                // meaning of the name takes precedence.
                if (!binding) {
                    binding = state_variable_binding(
                        declaration, specialization, expression.text);
                }
            }
        } else if (expression.kind == sv::ConstraintExpressionKind::call) {
            binding = method_binding(
                specialization, expression.text, expression.source);
        }
        if (binding)
            expression.bindings.push_back(std::move(*binding));
    }
}

semantic::ScopeId SystemVerilogHirBuilder::ensure_compilation_unit(
    const frontend::SourceSpan& span,
    const std::string_view source_library,
    const std::string_view compilation_unit_identity,
    const frontend::StandardRevision standard_revision,
    const std::string_view compatibility_profile,
    const std::string_view canonical_parent)
{
    const auto separator = canonical_parent.find("::");
    const auto library = separator == std::string_view::npos
        ? source_library
        : canonical_parent.substr(0, separator);
    const auto name = separator == std::string_view::npos
        ? canonical_parent
        : canonical_parent.substr(separator + 2U);
    const auto existing = std::ranges::find_if(
        hir_.units(), [&](const sv::Unit& unit) {
            const auto unit_library = unit.library.empty()
                ? std::string_view { "work" }
                : std::string_view { unit.library };
            return unit.kind == sv::UnitKind::compilation_unit
                && unit_library == library && unit.name == name;
        });
    if (existing != hir_.units().end())
        return existing->scope;
    if (!name.starts_with("$unit")) {
        throw std::logic_error {
            "SystemVerilog declaration has no semantic owner scope"
        };
    }
    const auto unit_source = source(span);
    const auto unit_origin = model_.add_origin(
        semantic::OriginKind::parsed,
        unit_source,
        std::nullopt,
        std::string { canonical_parent });
    const auto unit_id = model_.add_unit(
        semantic::Language::system_verilog,
        semantic::UnitKind::systemverilog_compilation_unit,
        std::string { library },
        std::string { name },
        { },
        unit_source,
        unit_origin);
    const auto& common = model_.units()[unit_id.value()];
    sv::Unit output;
    output.id = unit_id;
    output.scope = common.scope;
    output.kind = sv::UnitKind::compilation_unit;
    output.library = common.library;
    output.name = common.name;
    output.source = common.source;
    output.origin = common.origin;
    output.compilation_unit_identity = compilation_unit_identity;
    output.standard = std::string {
        frontend::revision_string(standard_revision)
    };
    output.compatibility_profile = compatibility_profile;
    hir_.mutable_units().push_back(std::move(output));
    return common.scope;
}

semantic::ScopeId SystemVerilogHirBuilder::class_parent_scope(
    const frontend::SystemVerilogClassDeclaration& input)
{
    const auto separator = input.canonical_identity.rfind("::");
    if (separator == std::string::npos) {
        throw std::logic_error {
            "SystemVerilog class has no canonical owner identity"
        };
    }
    const auto parent = std::string_view { input.canonical_identity }
                            .substr(0, separator);
    if (const auto enclosing_class = class_scopes_.find(parent);
        enclosing_class != class_scopes_.end()) {
        return enclosing_class->second;
    }
    const auto enclosing_unit = std::ranges::find_if(
        hir_.units(), [&](const sv::Unit& unit) {
            const auto library = unit.library.empty()
                ? std::string_view { "work" }
                : std::string_view { unit.library };
            return parent == std::string { library } + "::" + unit.name;
        });
    if (enclosing_unit != hir_.units().end())
        return enclosing_unit->scope;
    const auto scope = ensure_compilation_unit(
        input.span,
        input.library,
        input.compilation_unit_identity,
        input.standard_revision,
        input.verilog_compatibility_profile,
        parent);
    // A $unit class keeps the time unit of its compilation unit (IEEE
    // 1800-2023 3.14.2.3).
    for (auto& unit : hir_.mutable_units()) {
        if (unit.scope == scope
            && unit.kind == sv::UnitKind::compilation_unit
            && unit.compilation.time_unit.empty()
            && unit.compilation.time_precision.empty()) {
            unit.compilation.time_unit = input.time_unit;
            unit.compilation.time_precision = input.time_precision;
        }
    }
    return scope;
}

std::string SystemVerilogHirBuilder::class_identity(
    const frontend::SystemVerilogClassDeclaration& input,
    const semantic::ScopeId parent_scope) const
{
    // Class resolution assigns library-qualified identities to ordinary
    // unit and compilation-unit declarations. Generate-local declarations
    // do not pass through that inventory, so derive their identity from the
    // semantic owner scope rather than retaining the parser-local label.
    if (!input.canonical_identity.empty() && !input.library.empty())
        return input.canonical_identity;
    const auto enclosing_class = std::ranges::find_if(
        class_scopes_, [&](const auto& item) {
            return item.second == parent_scope;
        });
    if (enclosing_class != class_scopes_.end()) {
        return enclosing_class->first + "::" + input.name;
    }
    const auto unit_id = scope_unit(parent_scope);
    const auto& unit = model_.units().at(unit_id.value());
    std::vector<std::string_view> scopes;
    auto scope = std::optional<semantic::ScopeId> { parent_scope };
    while (scope && *scope != unit.scope) {
        const auto& current = model_.scopes().at(scope->value());
        if (!current.name.empty())
            scopes.push_back(current.name);
        scope = current.parent;
    }
    std::string identity = unit.library.empty() ? "work" : unit.library;
    identity += "::" + unit.name + "::";
    for (auto item = scopes.rbegin(); item != scopes.rend(); ++item) {
        identity += std::string { *item } + '.';
    }
    identity += input.name;
    return identity;
}

sv::ClassRelation SystemVerilogHirBuilder::class_relation(
    const frontend::SystemVerilogClassBase& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    sv::ClassRelation output;
    output.name = input.name;
    output.declaration_identity = input.declaration_identity;
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    for (const auto& input_actual : input.parameter_actuals) {
        sv::ActualAssociation actual;
        if (!input_actual.name.empty())
            actual.formal = input_actual.name;
        actual.source = source(input_actual.span);
        if (input_actual.type_actual) {
            actual.kind = sv::ActualKind::type;
            actual.type = type_reference(
                *input_actual.type_actual, input_actual.span,
                scope, output.origin);
        } else if (input_actual.value.kind
            == frontend::ExpressionKind::Invalid) {
            actual.kind = sv::ActualKind::default_value;
        } else {
            actual.expression = expression(
                input_actual.value, scope, output.origin);
        }
        output.actuals.push_back(std::move(actual));
    }
    return output;
}

void SystemVerilogHirBuilder::add_class(
    const frontend::SystemVerilogClassDeclaration& input,
    const std::optional<semantic::ScopeId> explicit_parent_scope,
    const std::optional<semantic::DeclarationId> generate_owner,
    const std::string_view alternative_discriminator)
{
    const auto parent_scope = explicit_parent_scope
        ? *explicit_parent_scope
        : class_parent_scope(input);
    sv::ClassDeclaration output;
    const auto generate_separator = input.name.rfind('.');
    output.name = generate_separator == std::string::npos
        ? input.name
        : input.name.substr(generate_separator + 1U);
    output.canonical_identity = class_identity(input, parent_scope);
    output.alternative_discriminator = alternative_discriminator;
    const auto enclosing_class = std::ranges::find(
        hir_.classes(), parent_scope, &sv::ClassDeclaration::scope);
    if (enclosing_class != hir_.classes().end()) {
        output.enclosing_identity
            = sv::class_declaration_identity(*enclosing_class);
    } else if (generate_owner) {
        // A class of a generate block is identified under the block's scope
        // path; its enclosing declaration is the unit (IEEE 1800-2017 27.3).
        const auto& unit = model_.units().at(
            scope_unit(parent_scope).value());
        output.enclosing_identity = (unit.library.empty() ? "work"
                                                          : unit.library)
            + "::" + unit.name;
    } else if (const auto separator
        = output.canonical_identity.rfind("::");
        separator != std::string::npos) {
        output.enclosing_identity = output.canonical_identity.substr(
            0, separator);
    }
    const auto owner_identity = sv::class_declaration_identity(output);
    output.generate_owner = generate_owner;
    output.lifetime = class_lifetime(input.lifetime);
    output.virtual_class = input.is_virtual;
    output.interface_class = input.is_interface;
    output.forward_declaration = input.is_forward_declaration;
    output.end_name = input.end_name;
    output.source = source(input.span);
    const auto parent_origin = model_.scopes()[parent_scope.value()].origin;
    output.origin = origin(
        output.source, parent_origin, owner_identity);
    output.scope = nested_scope(
        parent_scope, output.name, output.source, output.origin);
    class_scopes_.emplace(output.canonical_identity, output.scope);

    for (const auto& parameter : input.parameters) {
        const auto id = add_parameter(parameter, output.scope, output.origin);
        const auto& retained_declaration = declaration(id);
        sv::ClassParameter retained;
        retained.declaration = id;
        retained.name = parameter.name;
        retained.type_parameter
            = parameter.kind == frontend::ParameterKind::Type;
        retained.type = retained_declaration.type;
        retained.default_type = retained_declaration.default_type;
        retained.default_value = retained_declaration.initializer;
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.parameters.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    if (input.base) {
        if (input.is_interface) {
            output.extended_interfaces.push_back(class_relation(
                *input.base, output.scope, output.origin));
        } else {
            output.base = class_relation(
                *input.base, output.scope, output.origin);
            output.base_declaration_identity
                = input.base->declaration_identity;
        }
    }
    for (const auto& relation : input.extended_interfaces) {
        output.extended_interfaces.push_back(class_relation(
            relation, output.scope, output.origin));
    }
    for (const auto& relation : input.implemented_interfaces) {
        output.implemented_interfaces.push_back(class_relation(
            relation, output.scope, output.origin));
    }
    for (const auto& alias : input.type_aliases) {
        const auto id = add_type_declaration(
            alias, output.scope, output.origin);
        output.type_aliases.push_back(id);
        output.member_declarations.push_back(id);
    }
    for (const auto& property : input.properties) {
        const auto id = add_variable(
            property.declaration, output.scope, output.origin);
        // A class local parameter is a constant of the class scope (IEEE
        // 1800-2017 8.25), which ranges there evaluate.
        if (property.is_parameter && property.declaration.initializer) {
            declaration(id).array_local_parameter = true;
        }
        const auto& retained_declaration = declaration(id);
        sv::ClassProperty retained;
        retained.declaration = id;
        retained.name = property.declaration.name;
        retained.owner_identity = owner_identity;
        retained.canonical_identity = owner_identity + "::"
            + property.declaration.name;
        retained.type = *retained_declaration.type;
        retained.initializer = retained_declaration.initializer;
        retained.visibility = class_visibility(property.visibility);
        retained.random_kind = property.is_rand
            ? sv::ClassRandomKind::rand
            : property.is_randc
            ? sv::ClassRandomKind::randc
            : sv::ClassRandomKind::none;
        retained.static_storage = property.is_static;
        retained.constant = property.is_const;
        retained.parameter = property.is_parameter;
        // An enumeration's literal values; an implicit value follows the
        // previous one (IEEE 1800-2017 6.19).
        retained.enumeration_domain = enumeration_values(
            property.declaration.type);
        // An array of an enumeration: its elements' values, from the
        // layout's alias-expanded element type.
        if (retained.enumeration_domain.empty()
            && property.declaration.type.systemverilog_container) {
            for (const auto& specialization : class_specializations_) {
                const auto layout = std::ranges::find_if(
                    specialization.properties,
                    [&](const frontend::SystemVerilogClassPropertyLayout&
                            candidate) {
                        return candidate.name == property.declaration.name
                            && candidate.owner_identity == owner_identity;
                    });
                if (layout == specialization.properties.end()
                    || !layout->type.systemverilog_container
                    || layout->type.systemverilog_container
                           ->element_types.size() != 1U) {
                    continue;
                }
                retained.enumeration_domain = enumeration_values(
                    layout->type.systemverilog_container
                        ->element_types.front());
                if (retained.enumeration_domain.empty()) {
                    retained.enumeration_domain = enumeration_values(
                        layout->type);
                }
                break;
            }
        }
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.properties.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    for (const auto& method : input.methods) {
        const auto id = method.kind
                == frontend::SystemVerilogClassMethodKind::Task
            ? add_task(class_task(method), output.scope, output.origin)
            : add_function(class_function(method), output.scope, output.origin);
        const auto& retained_declaration = declaration(id);
        sv::ClassMethod retained;
        retained.declaration = id;
        retained.name = method.name;
        retained.canonical_identity = method.canonical_identity.empty()
                || !output.alternative_discriminator.empty()
            ? owner_identity + "::" + method.name
            : method.canonical_identity;
        retained.owner_identity = owner_identity;
        retained.profile_identity = method_profile_identity(method);
        retained.kind = class_method_kind(method.kind);
        retained.visibility = class_visibility(method.visibility);
        retained.lifetime = class_lifetime(method.lifetime);
        retained.static_method = method.is_static;
        retained.virtual_method = method.is_virtual;
        retained.pure = method.is_pure;
        retained.final_method = method.is_final;
        retained.external = method.is_extern;
        retained.out_of_block_definition = method.out_of_block_definition;
        retained.defined = method.defined;
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.methods.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    for (const auto& constraint : input.constraints) {
        sv::ClassConstraint retained;
        retained.name = constraint.name;
        retained.canonical_identity = owner_identity + "::"
            + constraint.name;
        retained.owner_identity = owner_identity;
        retained.visibility = class_visibility(constraint.visibility);
        retained.static_constraint = constraint.is_static;
        retained.pure = constraint.is_pure;
        retained.external = constraint.is_extern;
        retained.defined = constraint.defined;
        retained.source = source(constraint.span);
        retained.origin = origin(
            retained.source, output.origin, retained.canonical_identity);
        for (const auto& input_expression : constraint.expressions) {
            auto inlined = input_expression;
            inline_constraint_functions(inlined, input.methods);
            retained.expressions.push_back(constraint_expression(inlined));
        }
        for (auto& retained_expression : retained.expressions) {
            resolve_constraint_expression(retained_expression, input);
        }
        output.constraints.push_back(std::move(retained));
    }
    for (const auto& input_covergroup : input.covergroups) {
        output.covergroups.push_back(covergroup_declaration(
            input_covergroup, output.scope, output.origin));
    }
    for (const auto& nested : input.nested_classes) {
        output.nested_class_identities.push_back(
            sv::class_declaration_identity(
                nested.canonical_identity.empty()
                    ? output.canonical_identity + "::" + nested.name
                    : nested.canonical_identity,
                output.alternative_discriminator));
    }
    const auto class_scope = output.scope;
    hir_.mutable_classes().push_back(std::move(output));
    add_classes(input.nested_classes, class_scope, generate_owner,
        alternative_discriminator);
}

void SystemVerilogHirBuilder::add_classes(
    const std::vector<frontend::SystemVerilogClassDeclaration>& inputs,
    const std::optional<semantic::ScopeId> parent_scope,
    const std::optional<semantic::DeclarationId> generate_owner,
    const std::string_view alternative_discriminator)
{
    for (const auto& input : inputs)
        add_class(input, parent_scope, generate_owner,
            alternative_discriminator);
}

void SystemVerilogHirBuilder::synchronize_generate_class_ownership()
{
    const auto synchronize = [&](const auto& self,
                                 sv::GenerateRegion& region) -> void {
        for (auto& nested : region.nested) {
            self(self, nested);
        }
        region.class_declarations.clear();
        for (const auto& declaration : hir_.classes()) {
            if (declaration.generate_owner == region.declaration) {
                region.class_declarations.push_back(
                    sv::class_declaration_identity(declaration));
            }
        }
        for (auto& alternative : region.alternatives) {
            const auto nested = std::ranges::find_if(
                region.nested, [&](const sv::GenerateRegion& candidate) {
                    return candidate.scope == alternative.scope
                        && candidate.alternative_discriminator
                            == alternative.alternative_discriminator;
                });
            alternative.class_declarations = nested == region.nested.end()
                ? std::vector<std::string> { }
                : nested->class_declarations;
        }
    };
    for (auto& unit : hir_.mutable_units()) {
        for (auto& generate : unit.generates) {
            synchronize(synchronize, generate);
        }
    }
}

void SystemVerilogHirBuilder::compose_constraints()
{
    std::set<std::string> complete;
    std::set<std::string> active;
    std::function<void(sv::ClassDeclaration&)> compose;
    compose = [&](sv::ClassDeclaration& declaration) {
        const auto declaration_identity
            = sv::class_declaration_identity(declaration);
        if (complete.contains(declaration_identity))
            return;
        if (!active.insert(declaration_identity).second)
            return;
        if (!declaration.base_declaration_identity.empty()) {
            const auto same_base_identity = [&](const auto& candidate) {
                return candidate.canonical_identity
                    == declaration.base_declaration_identity;
            };
            auto base = std::ranges::find_if(
                hir_.mutable_classes(), [&](const auto& candidate) {
                    return same_base_identity(candidate)
                        && candidate.alternative_discriminator
                            == declaration.alternative_discriminator;
                });
            if (base == hir_.mutable_classes().end()) {
                base = std::ranges::find_if(
                    hir_.mutable_classes(), [&](const auto& candidate) {
                        return same_base_identity(candidate)
                            && candidate.alternative_discriminator.empty();
                    });
            }
            if (base != hir_.mutable_classes().end()) {
                compose(*base);
                declaration.composed_constraints
                    = base->composed_constraints;
            }
        }
        for (const auto& constraint : declaration.constraints) {
            sv::ComposedClassConstraint selected;
            selected.name = constraint.name;
            selected.selected_identity = constraint.canonical_identity;
            selected.mode_enabled = constraint.defined
                && !constraint.pure && !constraint.external;
            const auto inherited = std::ranges::find(
                declaration.composed_constraints,
                constraint.name,
                &sv::ComposedClassConstraint::name);
            if (inherited == declaration.composed_constraints.end()) {
                declaration.composed_constraints.push_back(
                    std::move(selected));
                continue;
            }
            const auto inherited_owner = std::ranges::find_if(
                hir_.classes(), [&](const sv::ClassDeclaration& candidate) {
                    return std::ranges::any_of(
                        candidate.constraints,
                        [&](const sv::ClassConstraint& block) {
                            return block.canonical_identity
                                == inherited->selected_identity;
                        });
                });
            const sv::ClassConstraint* inherited_block = nullptr;
            if (inherited_owner != hir_.classes().end()) {
                const auto block = std::ranges::find(
                    inherited_owner->constraints,
                    inherited->selected_identity,
                    &sv::ClassConstraint::canonical_identity);
                if (block != inherited_owner->constraints.end())
                    inherited_block = &*block;
            }
            selected.overrides = true;
            selected.overridden_identity = inherited->selected_identity;
            selected.override_legal = inherited_block == nullptr
                || inherited_block->static_constraint
                    == constraint.static_constraint;
            *inherited = std::move(selected);
        }
        active.erase(declaration_identity);
        complete.insert(declaration_identity);
    };
    for (auto& declaration : hir_.mutable_classes())
        compose(declaration);
}

} // namespace systemverilog_hir_detail
} // namespace fsim::app::application_detail
