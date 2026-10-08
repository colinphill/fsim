// SPDX-License-Identifier: Apache-2.0
#include "elaboration_vhdl_configuration_hir.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace fsim::elaboration::vhdl_configuration_detail {
namespace {

template <typename Value>
void append_identity_value(
    std::ostringstream& output,
    const std::string_view label,
    const Value& value)
{
    std::ostringstream encoded;
    encoded << value;
    output << ';' << label.size() << ':' << label
           << '=' << encoded.str().size() << ':' << encoded.str();
}

std::string semantic_scope_identity(
    const semantic::CompiledDesign& compiled,
    semantic::ScopeId scope)
{
    std::vector<std::string_view> names;
    const auto& scopes = compiled.semantics.scopes();
    while (scope.valid() && scope.value() < scopes.size()) {
        const auto& record = scopes[scope.value()];
        names.push_back(record.name);
        if (!record.parent) {
            break;
        }
        scope = *record.parent;
    }
    std::ostringstream output;
    for (auto iterator = names.rbegin(); iterator != names.rend(); ++iterator) {
        output << '/' << configuration_canonical_name(*iterator);
    }
    return output.str();
}

std::string declaration_identity(
    const semantic::CompiledDesign& compiled,
    const semantic::DeclarationId id)
{
    if (!id.valid() || id.value() >= compiled.semantics.declarations().size()) {
        return "invalid";
    }
    const auto& declaration = compiled.semantics.declarations()[id.value()];
    if (!declaration.scope.valid()
        || declaration.scope.value() >= compiled.semantics.scopes().size()) {
        return "invalid";
    }
    const auto& scope = compiled.semantics.scopes()[declaration.scope.value()];
    if (!scope.unit.valid()
        || scope.unit.value() >= compiled.semantics.units().size()) {
        return "invalid";
    }
    const auto& unit = compiled.semantics.units()[scope.unit.value()];
    std::ostringstream output;
    output << static_cast<int>(unit.language) << ':'
           << configuration_canonical_name(
                  unit.library.empty() ? "work" : unit.library)
           << ':' << configuration_canonical_name(unit.name)
           << ':' << static_cast<int>(declaration.kind)
           << ':' << semantic_scope_identity(compiled, declaration.scope)
           << ':' << configuration_canonical_name(declaration.name);
    if (declaration.source.valid()
        && declaration.source.value()
            < compiled.semantics.source_spans().size()) {
        const auto& source = compiled.semantics.source_spans()[
            declaration.source.value()];
        output << ':' << source.logical_name << ':' << source.begin.offset;
    }
    return output.str();
}

std::string type_identity(
    const semantic::CompiledDesign& compiled,
    const semantic::TypeId id)
{
    if (!id.valid() || id.value() >= compiled.semantics.types().size()) {
        return "invalid";
    }
    const auto& type = compiled.semantics.types()[id.value()];
    std::ostringstream output;
    output << static_cast<int>(type.kind) << ':'
           << semantic_scope_identity(compiled, type.scope) << ':'
           << configuration_canonical_name(type.name);
    if (type.source.valid()
        && type.source.value() < compiled.semantics.source_spans().size()) {
        const auto& source = compiled.semantics.source_spans()[
            type.source.value()];
        output << ':' << source.logical_name << ':' << source.begin.offset;
    }
    return output.str();
}

std::string unit_identity(
    const semantic::CompiledDesign& compiled,
    const semantic::UnitId id)
{
    if (!id.valid() || id.value() >= compiled.semantics.units().size()) {
        return "invalid";
    }
    const auto& unit = compiled.semantics.units()[id.value()];
    return std::to_string(static_cast<int>(unit.language)) + ':'
        + configuration_canonical_name(
            unit.library.empty() ? "work" : unit.library)
        + ':' + configuration_canonical_name(unit.name)
        + ':' + configuration_canonical_name(unit.secondary_name);
}

const semantic::vhdl::Expression* expression_for(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id)
{
    const auto expression = compiled.find_expression(id);
    return expression ? expression->vhdl : nullptr;
}

std::string expression_identity(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id,
    std::vector<semantic::ExpressionId>& active)
{
    const auto* expression = expression_for(compiled, id);
    if (expression == nullptr
        || std::ranges::find(active, id) != active.end()) {
        return "invalid";
    }
    active.push_back(id);
    std::ostringstream output;
    output << "kind=" << static_cast<int>(expression->kind);
    append_identity_value(output, "text", expression->text);
    append_identity_value(output, "nominal", expression->nominal_type);
    append_identity_value(output, "folded", expression->folded);
    append_identity_value(
        output, "decoded",
        expression->decoded_string.value_or(std::string {}));
    if (expression->referenced_name) {
        const auto& name = *expression->referenced_name;
        append_identity_value(
            output, "reference", configuration_canonical_name(name.spelling));
        if (name.selected) {
            append_identity_value(
                output, "selected",
                declaration_identity(compiled, *name.selected));
        }
        std::vector<std::string> overloads;
        overloads.reserve(name.overloads.size());
        for (const auto overload : name.overloads) {
            overloads.push_back(declaration_identity(compiled, overload));
        }
        std::ranges::sort(overloads);
        for (const auto& overload : overloads) {
            append_identity_value(output, "overload", overload);
        }
    }
    for (const auto& name : expression->argument_names) {
        append_identity_value(
            output, "argument", configuration_canonical_name(name));
    }
    for (const auto operand : expression->operands) {
        append_identity_value(
            output, "operand", expression_identity(compiled, operand, active));
    }
    for (const auto& association : expression->associations) {
        append_identity_value(
            output, "choice-spelling", association.choice_spelling);
        for (const auto choice : association.choices) {
            append_identity_value(
                output, "choice",
                expression_identity(compiled, choice, active));
        }
        append_identity_value(
            output, "association-value",
            expression_identity(compiled, association.value, active));
    }
    for (const auto& inferred : expression->inferred_type_identities) {
        append_identity_value(output, "inferred", inferred);
    }
    append_identity_value(
        output, "inference-unique",
        expression->unspecified_type_inference_unique);
    for (const auto dependency : expression->dependencies.parameters) {
        append_identity_value(
            output, "parameter", declaration_identity(compiled, dependency));
    }
    for (const auto dependency : expression->dependencies.generics) {
        append_identity_value(
            output, "generic", declaration_identity(compiled, dependency));
    }
    for (const auto dependency : expression->dependencies.types) {
        append_identity_value(
            output, "type", type_identity(compiled, dependency));
    }
    for (const auto dependency : expression->dependencies.packages) {
        append_identity_value(
            output, "package", unit_identity(compiled, dependency));
    }
    append_identity_value(
        output, "hierarchy", expression->dependencies.hierarchy);
    active.pop_back();
    return output.str();
}

std::optional<std::uint64_t> unsigned_integer(
    const std::string_view spelling,
    const unsigned base)
{
    if (spelling.empty()) {
        return std::nullopt;
    }
    std::uint64_t value { };
    for (const auto character : spelling) {
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
    }
    return value;
}

std::optional<std::int64_t> integer_literal(std::string spelling)
{
    std::erase(spelling, '_');
    if (spelling.empty()) {
        return std::nullopt;
    }
    const auto separator = spelling.find('#');
    unsigned base = 10U;
    std::string_view digits = spelling;
    std::string_view exponent;
    if (separator != std::string::npos) {
        const auto closing = spelling.find('#', separator + 1U);
        if (closing == std::string::npos
            || spelling.find('#', closing + 1U) != std::string::npos) {
            return std::nullopt;
        }
        const auto parsed_base = unsigned_integer(
            std::string_view { spelling }.substr(0, separator), 10U);
        if (!parsed_base || *parsed_base < 2U || *parsed_base > 16U) {
            return std::nullopt;
        }
        base = static_cast<unsigned>(*parsed_base);
        digits = std::string_view { spelling }.substr(
            separator + 1U, closing - separator - 1U);
        exponent = std::string_view { spelling }.substr(closing + 1U);
    } else {
        const auto marker = spelling.find_first_of("eE");
        if (marker != std::string::npos) {
            digits = std::string_view { spelling }.substr(0, marker);
            exponent = std::string_view { spelling }.substr(marker);
        }
    }
    auto value = unsigned_integer(digits, base);
    if (!value) {
        return std::nullopt;
    }
    if (!exponent.empty()) {
        if (exponent.front() == 'e' || exponent.front() == 'E') {
            exponent.remove_prefix(1U);
        }
        if (!exponent.empty() && exponent.front() == '+') {
            exponent.remove_prefix(1U);
        }
        if (exponent.empty() || exponent.front() == '-') {
            return std::nullopt;
        }
        const auto power = unsigned_integer(exponent, 10U);
        if (!power) {
            return std::nullopt;
        }
        for (std::uint64_t index = 0; index < *power; ++index) {
            if (*value > std::numeric_limits<std::uint64_t>::max() / base) {
                return std::nullopt;
            }
            *value *= base;
        }
    }
    if (*value > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
}

bool indexed_scope_matches(
    const std::string_view occurrence,
    const std::string_view scope)
{
    if (configuration_name_equal(occurrence, scope)) {
        return true;
    }
    const auto opening = occurrence.rfind('[');
    return opening != std::string_view::npos && occurrence.back() == ']'
        && configuration_name_equal(occurrence.substr(0, opening), scope);
}

} // namespace

std::string configuration_canonical_name(const std::string_view input)
{
    if (input.size() >= 2U
        && ((input.front() == '\\' && input.back() == '\\')
            || (input.front() == '\'' && input.back() == '\''))) {
        return std::string { input };
    }
    std::string result;
    result.reserve(input.size());
    for (const auto character : input) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(character))));
    }
    return result;
}

bool configuration_name_equal(
    const std::string_view left,
    const std::string_view right)
{
    const auto delimited = [](const std::string_view value) {
        return value.size() >= 2U
            && ((value.front() == '\\' && value.back() == '\\')
                || (value.front() == '\'' && value.back() == '\''));
    };
    if (delimited(left) || delimited(right)) {
        return left == right;
    }
    const auto lower_ascii = [](const unsigned char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character - 'A' + 'a')
            : character;
    };
    return left.size() == right.size()
        && std::ranges::equal(left, right,
            [&](const unsigned char lhs, const unsigned char rhs) {
                return lower_ascii(lhs) == lower_ascii(rhs);
            });
}

std::vector<std::string> configuration_name_parts(const std::string_view name)
{
    std::vector<std::string> result;
    std::size_t begin { };
    bool extended { };
    for (std::size_t index = 0; index < name.size(); ++index) {
        if (name[index] == '\\') {
            extended = !extended;
        } else if (name[index] == '.' && !extended) {
            result.emplace_back(name.substr(begin, index - begin));
            begin = index + 1U;
        }
    }
    result.emplace_back(name.substr(begin));
    return result;
}

std::string configuration_expression_identity(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id)
{
    std::vector<semantic::ExpressionId> active;
    return expression_identity(compiled, id, active);
}

namespace {

std::optional<std::int64_t> static_integer(
    const semantic::CompiledDesign& compiled,
    semantic::ExpressionId id,
    std::size_t depth);

const semantic::vhdl::Declaration* named_declaration(
    const semantic::CompiledDesign& compiled,
    const semantic::vhdl::Expression& expression)
{
    if (!expression.referenced_name) {
        return nullptr;
    }
    const auto& name = *expression.referenced_name;
    const auto id = name.selected ? name.selected
        : name.overloads.size() == 1U
        ? std::optional { name.overloads.front() }
        : std::nullopt;
    if (id) {
        const auto declaration = compiled.find_declaration(*id);
        return declaration ? declaration->vhdl : nullptr;
    }
    // A configuration's names are not resolved against the configured
    // architecture or its packages; accept a design-wide constant, type or
    // subtype only when the name is unique.
    const semantic::vhdl::Declaration* found = nullptr;
    for (const auto& declaration : compiled.vhdl_hir.declarations()) {
        using Form = semantic::vhdl::DeclarationForm;
        if ((declaration.form != Form::constant
                && declaration.form != Form::type
                && declaration.form != Form::subtype)
            || !configuration_name_equal(declaration.name, expression.text)) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &declaration;
    }
    return found;
}

// A bound of a scalar subtype's range: 'LEFT, 'RIGHT, 'LOW or 'HIGH.
std::optional<std::int64_t> static_range_bound(
    const semantic::CompiledDesign& compiled,
    const semantic::vhdl::SubtypeIndication& subtype,
    const std::string_view attribute,
    const std::size_t depth)
{
    const semantic::vhdl::RangeConstraint* range = nullptr;
    for (const auto& constraint : subtype.constraints) {
        if (constraint.kind == semantic::vhdl::RangeKind::integer) {
            range = &constraint;
            break;
        }
    }
    if (range == nullptr && subtype.type_mark.target.valid()) {
        const auto type = compiled.find_type(subtype.type_mark.target);
        if (type && type->vhdl != nullptr) {
            if (type->vhdl->scalar_range) {
                range = &*type->vhdl->scalar_range;
            } else if (type->vhdl->form
                    == semantic::vhdl::TypeForm::subtype
                && depth < 16U) {
                return static_range_bound(
                    compiled, type->vhdl->base, attribute, depth + 1U);
            }
        }
    }
    if (range == nullptr) {
        return std::nullopt;
    }
    const auto bound = [&](const std::optional<std::int64_t>& value,
                           const std::optional<semantic::ExpressionId>&
                               expression) -> std::optional<std::int64_t> {
        if (value) {
            return value;
        }
        return expression ? static_integer(compiled, *expression, depth + 1U)
                          : std::nullopt;
    };
    const auto left = bound(range->left, range->left_expression);
    const auto right = bound(range->right, range->right_expression);
    if (!left || !right) {
        return std::nullopt;
    }
    if (attribute == "'left") {
        return left;
    }
    if (attribute == "'right") {
        return right;
    }
    if (attribute == "'low") {
        return std::min(*left, *right);
    }
    if (attribute == "'high") {
        return std::max(*left, *right);
    }
    return std::nullopt;
}

std::optional<std::int64_t> static_integer(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id,
    const std::size_t depth)
{
    const auto* expression = expression_for(compiled, id);
    if (expression == nullptr || depth > 32U) {
        return std::nullopt;
    }
    using Kind = semantic::vhdl::ExpressionKind;
    if (expression->kind == Kind::integer_literal) {
        return integer_literal(expression->text);
    }
    if (expression->kind == Kind::name) {
        // A constant whose initializer is itself static (IEEE 1076-2008
        // 9.4.2).
        const auto* declaration = named_declaration(compiled, *expression);
        if (declaration == nullptr
            || declaration->form != semantic::vhdl::DeclarationForm::constant
            || !declaration->initializer) {
            return std::nullopt;
        }
        return static_integer(compiled, *declaration->initializer, depth + 1U);
    }
    if (expression->kind == Kind::call && expression->operands.size() == 1U
        && (expression->text == "'left" || expression->text == "'right"
            || expression->text == "'low" || expression->text == "'high")) {
        const auto* prefix = expression_for(
            compiled, expression->operands.front());
        const auto* declaration = prefix != nullptr
            ? named_declaration(compiled, *prefix)
            : nullptr;
        if (declaration == nullptr) {
            return std::nullopt;
        }
        if (declaration->subtype) {
            return static_range_bound(
                compiled, *declaration->subtype, expression->text, depth);
        }
        if (declaration->declared_type) {
            const auto type = compiled.find_type(*declaration->declared_type);
            if (type && type->vhdl != nullptr) {
                return static_range_bound(
                    compiled, type->vhdl->base, expression->text, depth);
            }
        }
        return std::nullopt;
    }
    if (expression->kind == Kind::binary
        && expression->operands.size() == 2U
        && (expression->text == "+" || expression->text == "-")) {
        const auto left = static_integer(
            compiled, expression->operands[0], depth + 1U);
        const auto right = static_integer(
            compiled, expression->operands[1], depth + 1U);
        std::int64_t result { };
        if (!left || !right
            || (expression->text == "+"
                    ? __builtin_add_overflow(*left, *right, &result)
                    : __builtin_sub_overflow(*left, *right, &result))) {
            return std::nullopt;
        }
        return result;
    }
    if (expression->kind == Kind::unary
        && expression->operands.size() == 1U) {
        const auto operand = static_integer(
            compiled, expression->operands.front(), depth + 1U);
        if (!operand) {
            return std::nullopt;
        }
        if (expression->text == "+") {
            return operand;
        }
        if (expression->text == "-"
            && *operand != std::numeric_limits<std::int64_t>::min()) {
            return -*operand;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::int64_t> configuration_static_integer(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id)
{
    return static_integer(compiled, id, 0U);
}

std::optional<std::vector<std::string>> configuration_block_scopes(
    const semantic::CompiledDesign& compiled,
    const semantic::vhdl::BlockConfiguration& block)
{
    if (!block.generate_index) {
        return std::vector { block.block.spelling };
    }
    const auto* index = expression_for(compiled, *block.generate_index);
    if (index != nullptr
        && index->kind == semantic::vhdl::ExpressionKind::call
        && (index->text == "@vhdl-case-range-to"
            || index->text == "@vhdl-case-range-downto")
        && index->operands.size() == 2U) {
        // A static discrete range selects every generate parameter value in
        // it (IEEE 1076-2008 3.4.2); a null range selects none.
        const auto left = configuration_static_integer(
            compiled, index->operands[0]);
        const auto right = configuration_static_integer(
            compiled, index->operands[1]);
        if (!left || !right) {
            return std::nullopt;
        }
        const bool descending = index->text == "@vhdl-case-range-downto";
        const auto low = descending ? *right : *left;
        const auto high = descending ? *left : *right;
        std::vector<std::string> scopes;
        constexpr std::int64_t maximum_scopes = 65536;
        if (low <= high && high - low >= maximum_scopes) {
            return std::nullopt;
        }
        for (auto value = low; value <= high; ++value) {
            scopes.push_back(
                block.block.spelling + "[" + std::to_string(value) + "]");
        }
        return scopes;
    }
    const auto value = configuration_static_integer(
        compiled, *block.generate_index);
    if (!value) {
        return std::nullopt;
    }
    return std::vector {
        block.block.spelling + "[" + std::to_string(*value) + "]"
    };
}

std::optional<std::vector<std::string>> configuration_occurrence_parts(
    const semantic::CompiledDesign& compiled,
    const semantic::vhdl::Unit& unit,
    const semantic::vhdl::Instance& instance,
    const std::string_view occurrence_path)
{
    const auto& scopes = compiled.semantics.scopes();
    if (!instance.scope.valid() || instance.scope.value() >= scopes.size()) {
        return std::nullopt;
    }
    std::vector<std::string_view> scope_names;
    auto scope = instance.scope;
    while (scope != unit.scope) {
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return std::nullopt;
        }
        const auto& record = scopes[scope.value()];
        if (record.unit != unit.id || !record.parent) {
            return std::nullopt;
        }
        scope_names.push_back(record.name);
        scope = *record.parent;
    }
    std::ranges::reverse(scope_names);

    const auto path = configuration_name_parts(occurrence_path);
    if (path.empty()) {
        return std::nullopt;
    }

    auto path_index = path.size();
    const auto consume_occurrence_suffix
        = [&](const std::string_view semantic_name,
              const bool allow_index) -> std::optional<std::string> {
        std::string candidate;
        for (auto begin = path_index; begin != 0U;) {
            --begin;
            candidate = path[begin]
                + (candidate.empty() ? std::string { }
                                     : "." + candidate);
            const bool matches = allow_index
                ? indexed_scope_matches(candidate, semantic_name)
                : configuration_name_equal(candidate, semantic_name);
            if (matches) {
                path_index = begin;
                return candidate;
            }
        }
        return std::nullopt;
    };
    const auto instance_occurrence = consume_occurrence_suffix(
        instance.name, false);
    if (!instance_occurrence) {
        return std::nullopt;
    }

    // Selection-generates retain both the selection wrapper and the selected
    // alternative in semantic scope identity, while hierarchy occurrence
    // names contain only the selected alternative. Match the occurrence
    // suffix against semantic scopes and skip structural wrappers that do not
    // materialize as hierarchy nodes. This also keeps enclosing parent-unit
    // paths out of the configuration lookup for nested configured units.
    std::vector<std::string> result;
    result.reserve(std::min(scope_names.size(), path_index) + 1U);
    auto scope_index = scope_names.size();
    while (scope_index != 0U && path_index != 0U) {
        const auto scope_name = scope_names[scope_index - 1U];
        if (const auto occurrence = consume_occurrence_suffix(
                scope_name, true)) {
            result.push_back(*occurrence);
        }
        --scope_index;
    }
    std::ranges::reverse(result);
    result.push_back(*instance_occurrence);
    return result;
}

} // namespace fsim::elaboration::vhdl_configuration_detail
