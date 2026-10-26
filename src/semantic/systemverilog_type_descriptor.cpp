// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_type_descriptor.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace fsim::semantic {
namespace {

    using Kind = SystemVerilogTypeDescriptor::Kind;

    [[nodiscard]] std::string lowered(std::string_view text)
    {
        std::string result { text };
        std::ranges::transform(result, result.begin(), [](const char c) {
            return static_cast<char>(
                c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        });
        return result;
    }

    struct Atom {
        std::string_view keyword;
        std::uint64_t bits;
        bool is_signed;
        bool four_state;
    };

    constexpr std::array atoms {
        Atom { "byte", 8U, true, false },
        Atom { "shortint", 16U, true, false },
        Atom { "int", 32U, true, false },
        Atom { "longint", 64U, true, false },
        Atom { "integer", 32U, true, true },
        Atom { "time", 64U, false, true },
    };

    [[nodiscard]] std::optional<std::int64_t> bound(
        const SpecializedHirUnit& unit,
        const std::optional<std::int64_t> value,
        const std::optional<ExpressionId> expression)
    {
        if (value) {
            return value;
        }
        return expression ? unit.evaluate_integral_expression(*expression)
                          : std::nullopt;
    }

    [[nodiscard]] SystemVerilogTypeDescriptor::Dimension dimension(
        const SpecializedHirUnit& unit, const sv::PackedRange& range)
    {
        return SystemVerilogTypeDescriptor::Dimension {
            bound(unit, range.left, range.left_expression),
            bound(unit, range.right, range.right_expression),
            { },
        };
    }

    [[nodiscard]] std::optional<std::uint64_t> dimension_length(
        const SystemVerilogTypeDescriptor::Dimension& dimension)
    {
        if (!dimension.left || !dimension.right) {
            return std::nullopt;
        }
        const auto distance = *dimension.left >= *dimension.right
            ? static_cast<std::uint64_t>(*dimension.left - *dimension.right)
            : static_cast<std::uint64_t>(*dimension.right - *dimension.left);
        return distance + 1U;
    }

    [[nodiscard]] SystemVerilogTypeDescriptor vector_type(
        const std::uint64_t bits, const bool is_signed, const bool four_state)
    {
        SystemVerilogTypeDescriptor result;
        result.kind = Kind::integer_vector;
        result.keyword = four_state ? "logic" : "bit";
        result.is_signed = is_signed;
        result.four_state = four_state;
        result.bits = bits;
        if (bits > 1U) {
            result.packed.push_back({
                static_cast<std::int64_t>(bits - 1U), 0, { } });
        }
        return result;
    }

    [[nodiscard]] bool real_kind(const Kind kind) noexcept
    {
        return kind == Kind::real || kind == Kind::shortreal
            || kind == Kind::realtime;
    }

    [[nodiscard]] bool integral_kind(const Kind kind) noexcept
    {
        return kind == Kind::integer_atom || kind == Kind::integer_vector
            || kind == Kind::enumeration;
    }

    [[nodiscard]] std::optional<SystemVerilogTypeDescriptor>
    describe_declaration(const SpecializedHirUnit& unit,
        const DeclarationId declaration)
    {
        const auto view = unit.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->type) {
            return std::nullopt;
        }
        return describe_systemverilog_type(
            unit, *view->systemverilog->type, view->systemverilog->scope);
    }

} // namespace

std::optional<std::uint64_t> SystemVerilogTypeDescriptor::total_bits() const
{
    if (!bits) {
        return std::nullopt;
    }
    auto total = *bits;
    for (const auto& dimension : unpacked) {
        const auto length = dimension_length(dimension);
        if (!dimension.container.empty() || !length) {
            return std::nullopt;
        }
        total *= *length;
    }
    return total;
}

std::string SystemVerilogTypeDescriptor::canonical() const
{
    std::string result;
    // An integer atom of predefined width matches the simple bit vector of
    // the same state count, signedness and [width-1:0] range (IEEE
    // 1800-2023 6.22.1 e), so both spell that vector; reg is logic (6.11.2).
    const bool atom_vector = kind == Kind::integer_atom && bits
        && packed.empty()
        && (keyword == "byte" || keyword == "shortint" || keyword == "int"
            || keyword == "longint" || keyword == "integer"
            || keyword == "time");
    switch (kind) {
    case Kind::integer_atom:
    case Kind::integer_vector:
        result = atom_vector ? (four_state ? "logic" : "bit")
            : keyword == "reg" ? std::string { "logic" }
                               : keyword;
        break;
    case Kind::real:
        result = "real";
        break;
    case Kind::shortreal:
        result = "shortreal";
        break;
    case Kind::realtime:
        // realtime is a synonym for real (IEEE 1800-2017 6.12).
        result = "real";
        break;
    case Kind::string:
        result = "string";
        break;
    case Kind::chandle:
        result = "chandle";
        break;
    case Kind::event:
        result = "event";
        break;
    case Kind::enumeration:
    case Kind::structure:
    case Kind::union_type:
    case Kind::class_handle:
    case Kind::interface:
    case Kind::other:
        result = "@" + std::to_string(static_cast<int>(kind)) + ":"
            + (nominal ? std::to_string(nominal->value()) : nominal_name);
        break;
    }
    if (kind == Kind::integer_vector || kind == Kind::integer_atom) {
        result += is_signed ? " signed" : " unsigned";
    }
    const auto append = [&](const std::vector<Dimension>& dimensions) {
        for (const auto& dimension : dimensions) {
            result += '[';
            if (!dimension.container.empty()) {
                result += dimension.container;
            } else {
                result += dimension.left ? std::to_string(*dimension.left)
                                         : std::string { "?" };
                result += ':';
                result += dimension.right ? std::to_string(*dimension.right)
                                          : std::string { "?" };
            }
            result += ']';
        }
    };
    if (atom_vector) {
        result += '[' + std::to_string(*bits - 1U) + ":0]";
    }
    append(packed);
    if (!unpacked.empty()) {
        result += " $";
        append(unpacked);
    }
    return result;
}

std::optional<sv::TypeReference> systemverilog_type_reference(
    const SystemVerilogTypeDescriptor& type)
{
    sv::TypeReference result;
    result.signed_value = type.is_signed;
    result.four_state = type.four_state;
    switch (type.kind) {
    case Kind::integer_atom:
        result.target.spelling = type.keyword;
        result.value_form = sv::TypeForm::packed_integral;
        result.executable_width = type.bits;
        // An integer atom carries its native range, as declared.
        if (type.bits) {
            sv::PackedRange native;
            native.left = static_cast<std::int64_t>(*type.bits - 1U);
            native.right = 0;
            native.descending = true;
            result.packed_range = native;
        }
        break;
    case Kind::integer_vector: {
        result.target.spelling = type.keyword;
        result.value_form = sv::TypeForm::packed_integral;
        for (const auto& dimension : type.packed) {
            if (!dimension.left || !dimension.right) {
                return std::nullopt;
            }
            sv::PackedRange range;
            range.left = dimension.left;
            range.right = dimension.right;
            range.descending = *dimension.left >= *dimension.right;
            result.packed_dimensions.push_back(range);
        }
        if (result.packed_dimensions.size() == 1U) {
            result.packed_range = result.packed_dimensions.front();
            result.packed_dimensions.clear();
        } else if (!result.packed_dimensions.empty() && type.bits) {
            sv::PackedRange flattened;
            flattened.left = static_cast<std::int64_t>(*type.bits - 1U);
            flattened.right = 0;
            flattened.descending = true;
            result.packed_range = flattened;
        }
        result.executable_width = type.bits;
        break;
    }
    case Kind::real:
    case Kind::realtime:
        result.target.spelling = "real";
        result.executable_width = 64U;
        break;
    case Kind::shortreal:
        result.target.spelling = "shortreal";
        result.executable_width = 32U;
        break;
    case Kind::string:
        result.target.spelling = "string";
        result.value_form = sv::TypeForm::string;
        break;
    case Kind::chandle:
        result.target.spelling = "chandle";
        break;
    case Kind::event:
        result.target.spelling = "event";
        break;
    default:
        if (!type.nominal) {
            return std::nullopt;
        }
        result.target.target = *type.nominal;
        result.target.spelling = type.nominal_name;
        break;
    }
    if (!type.unpacked.empty()) {
        for (const auto& dimension : type.unpacked) {
            if (!dimension.container.empty() || !dimension.left
                || !dimension.right) {
                return std::nullopt;
            }
            sv::PackedRange range;
            range.left = dimension.left;
            range.right = dimension.right;
            range.descending = *dimension.left >= *dimension.right;
            result.unpacked_dimensions.push_back(range);
        }
        result.container_form = sv::TypeForm::static_array;
    }
    return result;
}

bool systemverilog_types_match(const SystemVerilogTypeDescriptor& left,
    const SystemVerilogTypeDescriptor& right)
{
    return left.canonical() == right.canonical();
}

std::optional<SystemVerilogTypeDescriptor> describe_systemverilog_type(
    const SpecializedHirUnit& unit, const sv::TypeReference& type,
    const ScopeId scope)
{
    // `type(expression)` as a data type (6.23).
    if (type.type_of) {
        return describe_systemverilog_expression_type(unit, *type.type_of);
    }
    const CompiledDesignResolver resolver { unit };
    const auto resolved
        = resolver.underlying_systemverilog_type(type, scope).value_or(type);
    SystemVerilogTypeDescriptor result;
    result.is_signed = resolved.signed_value;
    result.four_state = resolved.four_state;
    const auto spelling = lowered(resolved.target.spelling);
    const auto form = resolved.value_form.value_or(sv::TypeForm::unresolved);
    for (const auto& range : resolved.unpacked_dimensions) {
        result.unpacked.push_back(dimension(unit, range));
    }
    // An enumeration matches only itself (6.22.1), so the typedef chain is
    // followed only as far as an enumeration declaration.
    if (!resolved.container_form && !type.packed_range
        && type.packed_dimensions.empty()) {
        auto current = resolver.effective_systemverilog_type(type, scope)
                           .value_or(type);
        std::vector<TypeId> visited;
        while (current.target.target.valid()
            && std::ranges::find(visited, current.target.target)
                == visited.end()
            && current.unpacked_dimensions.empty()) {
            visited.push_back(current.target.target);
            const auto definition = unit.find_type(current.target.target);
            if (!definition || definition->systemverilog == nullptr) {
                break;
            }
            if (definition->systemverilog->form
                == sv::TypeForm::enumeration) {
                result.kind = Kind::enumeration;
                result.nominal = current.target.target;
                result.nominal_name = definition->systemverilog->name;
                // The typedef's declared type repeats the enumeration's
                // base range, which the nominal type already carries.
                sv::TypeReference nominal;
                nominal.target = current.target;
                if (const auto width
                    = unit.evaluate_systemverilog_type_width(nominal)) {
                    result.bits = *width;
                }
                return result;
            }
            if (definition->systemverilog->form != sv::TypeForm::alias) {
                break;
            }
            const auto& base = definition->systemverilog->base;
            current = resolver.effective_systemverilog_type(base, scope)
                          .value_or(base);
        }
    }
    if (resolved.container_form) {
        // A dynamic array, queue or associative array is an unpacked
        // dimension without bounds of its element type.
        std::string container;
        switch (*resolved.container_form) {
        case sv::TypeForm::dynamic_array:
            container = "dynamic";
            break;
        case sv::TypeForm::queue:
            container = "queue";
            break;
        case sv::TypeForm::associative_array:
            container = "associative:"
                + (resolved.associative_index
                        ? lowered(resolved.associative_index->spelling)
                        : std::string { "*" });
            break;
        default:
            break;
        }
        if (!container.empty() && resolved.container_element_types.size() == 1U) {
            auto element = describe_systemverilog_type(
                unit, resolved.container_element_types.front(), scope);
            if (!element) {
                return std::nullopt;
            }
            element->unpacked.insert(element->unpacked.begin(),
                SystemVerilogTypeDescriptor::Dimension {
                    std::nullopt, std::nullopt, container });
            return element;
        }
    }
    const auto atom = std::ranges::find(
        atoms, std::string_view { spelling }, &Atom::keyword);
    // An integer atom may carry its native range ([31:0] for int).
    const auto native_range = [&] {
        if (!resolved.packed_range) {
            return true;
        }
        const auto& range = *resolved.packed_range;
        return atom != atoms.end() && !range.left_expression
            && !range.right_expression && range.descending
            && range.left == static_cast<std::int64_t>(atom->bits - 1U)
            && range.right == 0;
    };
    if (atom != atoms.end() && resolved.packed_dimensions.empty()
        && native_range()) {
        result.kind = Kind::integer_atom;
        result.keyword = std::string { atom->keyword };
        result.bits = atom->bits;
        result.four_state = atom->four_state;
        return result;
    }
    if (spelling == "real" || spelling == "realtime"
        || spelling == "shortreal") {
        result.kind = spelling == "shortreal" ? Kind::shortreal
            : spelling == "realtime"          ? Kind::realtime
                                              : Kind::real;
        result.bits = spelling == "shortreal" ? 32U : 64U;
        return result;
    }
    if (spelling == "string" || form == sv::TypeForm::string) {
        result.kind = Kind::string;
        return result;
    }
    if (spelling == "chandle") {
        result.kind = Kind::chandle;
        return result;
    }
    if (spelling == "event") {
        result.kind = Kind::event;
        return result;
    }
    if (form == sv::TypeForm::enumeration
        || form == sv::TypeForm::packed_structure
        || form == sv::TypeForm::unpacked_structure
        || form == sv::TypeForm::packed_union
        || form == sv::TypeForm::unpacked_union
        || form == sv::TypeForm::tagged_union
        || form == sv::TypeForm::class_handle) {
        result.kind = form == sv::TypeForm::enumeration ? Kind::enumeration
            : form == sv::TypeForm::class_handle         ? Kind::class_handle
            : form == sv::TypeForm::packed_structure
                || form == sv::TypeForm::unpacked_structure
            ? Kind::structure
            : Kind::union_type;
        if (resolved.target.target.valid()) {
            result.nominal = resolved.target.target;
        }
        result.nominal_name = form == sv::TypeForm::class_handle
                && !resolved.class_identity.empty()
            ? resolved.class_identity
            : spelling;
    } else if (!resolved.interface_type.empty()) {
        result.kind = Kind::interface;
        result.nominal_name = resolved.interface_type + "."
            + resolved.interface_modport;
        return result;
    } else {
        result.kind = Kind::integer_vector;
        // `reg` and `logic` are the same type (IEEE 1800-2017 6.11.2).
        result.keyword = spelling == "bit" ? "bit" : "logic";
        result.four_state = result.keyword == "logic";
    }
    if (!resolved.packed_dimensions.empty()) {
        for (const auto& range : resolved.packed_dimensions) {
            result.packed.push_back(dimension(unit, range));
        }
    } else if (resolved.packed_range) {
        result.packed.push_back(dimension(unit, *resolved.packed_range));
    }
    // `bits` is the element width; unpacked dimensions are counted by
    // total_bits().
    auto element = type;
    if (element.container_form == sv::TypeForm::static_array) {
        element.container_form.reset();
    }
    element.unpacked_dimensions.clear();
    const bool own_unpacked = !type.unpacked_dimensions.empty();
    if (const auto width = own_unpacked || resolved.unpacked_dimensions.empty()
            ? unit.evaluate_systemverilog_type_width(element)
            : std::nullopt) {
        result.bits = *width;
    } else if (result.kind == Kind::integer_vector) {
        std::uint64_t bits = 1U;
        for (const auto& packed_dimension : result.packed) {
            const auto length = dimension_length(packed_dimension);
            if (!length) {
                return result;
            }
            bits *= *length;
        }
        result.bits = bits;
    }
    return result;
}

std::optional<SystemVerilogTypeDescriptor>
describe_systemverilog_expression_type(const SpecializedHirUnit& unit,
    const ExpressionId expression)
{
    const auto view = unit.find_expression(expression);
    if (!view || view->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& record = *view->systemverilog;
    const auto operand = [&](const std::size_t index)
        -> std::optional<SystemVerilogTypeDescriptor> {
        return index < record.operands.size()
            ? describe_systemverilog_expression_type(
                  unit, record.operands[index])
            : std::nullopt;
    };
    switch (record.kind) {
    case sv::ExpressionKind::name: {
        if (!record.referenced_name || !record.referenced_name->selected) {
            return std::nullopt;
        }
        const auto declaration = unit.find_declaration(
            *record.referenced_name->selected);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        // A name of a type in `type(...)` denotes that type.
        if (declaration->systemverilog->form
                == sv::DeclarationForm::typedef_declaration
            || declaration->systemverilog->form
                == sv::DeclarationForm::type_parameter) {
            sv::TypeReference named;
            named.target.spelling = declaration->systemverilog->name;
            return describe_systemverilog_type(unit, named, record.scope);
        }
        return describe_declaration(unit, *record.referenced_name->selected);
    }
    case sv::ExpressionKind::integer_literal: {
        if (record.decimal_literal) {
            SystemVerilogTypeDescriptor result;
            result.kind = record.decimal_literal->kind
                    == sv::DecimalLiteralKind::time
                ? Kind::realtime
                : Kind::real;
            result.bits = 64U;
            return result;
        }
        const auto quote = record.text.find('\'');
        if (quote == std::string::npos || quote == 0U) {
            // An unsized literal is a signed 32-bit integer (5.7.1); an
            // unsized based one is unsigned unless `s` is given.
            SystemVerilogTypeDescriptor result;
            result.kind = Kind::integer_atom;
            result.keyword = "int";
            result.is_signed = quote == std::string::npos
                || (record.text.size() > quote + 1U
                    && (record.text[quote + 1U] == 's'
                        || record.text[quote + 1U] == 'S'));
            result.bits = 32U;
            if (quote != std::string::npos) {
                return vector_type(32U, result.is_signed, true);
            }
            return result;
        }
        std::uint64_t width { };
        for (const char digit : record.text.substr(0U, quote)) {
            if (digit >= '0' && digit <= '9') {
                width = width * 10U + static_cast<std::uint64_t>(digit - '0');
            }
        }
        const bool is_signed = record.text.size() > quote + 1U
            && (record.text[quote + 1U] == 's'
                || record.text[quote + 1U] == 'S');
        return vector_type(width == 0U ? 32U : width, is_signed, true);
    }
    case sv::ExpressionKind::logic_literal:
        return vector_type(1U, false, true);
    case sv::ExpressionKind::boolean_literal:
        return vector_type(1U, false, false);
    case sv::ExpressionKind::string_literal: {
        const auto bytes = record.decoded_string
            ? std::max<std::size_t>(record.decoded_string->size(), 1U)
            : 1U;
        return vector_type(bytes * 8U, false, false);
    }
    case sv::ExpressionKind::unary: {
        const auto value = operand(0U);
        if (!value) {
            return std::nullopt;
        }
        // Reduction and logical operators give one bit (11.4.9, 11.4.7).
        if (record.text == "!" || record.text == "&" || record.text == "|"
            || record.text == "^" || record.text == "~&"
            || record.text == "~|" || record.text == "~^"
            || record.text == "^~") {
            return vector_type(1U, false, value->four_state);
        }
        return value;
    }
    case sv::ExpressionKind::binary: {
        const auto left = operand(0U);
        const auto right = operand(1U);
        if (!left || !right) {
            return std::nullopt;
        }
        const auto& op = record.text;
        if (op == "==" || op == "!=" || op == "===" || op == "!=="
            || op == "<" || op == "<=" || op == ">" || op == ">="
            || op == "&&" || op == "||" || op == "->" || op == "<->"
            || op == "==?" || op == "!=?") {
            return vector_type(1U, false,
                left->four_state || right->four_state);
        }
        if (real_kind(left->kind) || real_kind(right->kind)) {
            SystemVerilogTypeDescriptor result;
            result.kind = left->kind == Kind::shortreal
                    && right->kind == Kind::shortreal
                ? Kind::shortreal
                : Kind::real;
            result.bits = result.kind == Kind::shortreal ? 32U : 64U;
            return result;
        }
        if (!integral_kind(left->kind) || !integral_kind(right->kind)
            || !left->bits || !right->bits) {
            return std::nullopt;
        }
        // A shift or power takes the width and sign of its left operand
        // (11.6.1).
        if (op == "<<" || op == ">>" || op == "<<<" || op == ">>>"
            || op == "**") {
            return vector_type(*left->bits, left->is_signed,
                left->four_state || right->four_state);
        }
        return vector_type(std::max(*left->bits, *right->bits),
            left->is_signed && right->is_signed,
            left->four_state || right->four_state);
    }
    case sv::ExpressionKind::concatenation: {
        std::uint64_t bits { };
        bool four_state { };
        for (std::size_t index = 0U; index < record.operands.size(); ++index) {
            const auto part = operand(index);
            if (!part || !part->bits) {
                return std::nullopt;
            }
            bits += *part->bits;
            four_state = four_state || part->four_state;
        }
        return vector_type(bits, false, four_state);
    }
    case sv::ExpressionKind::call:
        // `type(x)` denotes the type of x; a type operand denotes itself.
        if (record.text == "@sv-type") {
            if (!record.type_operand.empty()) {
                return describe_systemverilog_type(
                    unit, record.type_operand.front(), record.scope);
            }
            return operand(0U);
        }
        if (record.text == "?:") {
            const auto left = operand(1U);
            const auto right = operand(2U);
            if (!left || !right) {
                return std::nullopt;
            }
            if (real_kind(left->kind) || real_kind(right->kind)) {
                return left;
            }
            if (left->bits && right->bits) {
                return vector_type(std::max(*left->bits, *right->bits),
                    left->is_signed && right->is_signed,
                    left->four_state || right->four_state);
            }
            return std::nullopt;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

} // namespace fsim::semantic
