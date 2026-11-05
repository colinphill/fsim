// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/dpi_c_type.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

namespace {

using semantic::sv::SourceToken;

bool checked_add(const std::int64_t a, const std::int64_t b, std::int64_t& r)
{
    return !__builtin_add_overflow(a, b, &r);
}

bool checked_sub(const std::int64_t a, const std::int64_t b, std::int64_t& r)
{
    return !__builtin_sub_overflow(a, b, &r);
}

bool checked_mul(const std::int64_t a, const std::int64_t b, std::int64_t& r)
{
    return !__builtin_mul_overflow(a, b, &r);
}

// A constant integral expression over tokens (IEEE 1800-2023 35.5.6: a
// packed or unpacked range of a DPI type is a constant expression).
class DpiConstantParser {
public:
    DpiConstantParser(std::span<const SourceToken> tokens,
        const DpiTypeContext& context)
        : tokens_ { tokens }, context_ { context }
    {
    }

    [[nodiscard]] std::optional<std::int64_t> parse()
    {
        const auto value = binary(0);
        if (!value || position_ != tokens_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    [[nodiscard]] std::string_view peek(const std::size_t ahead = 0U) const
    {
        return position_ + ahead < tokens_.size()
            ? std::string_view { tokens_[position_ + ahead].text }
            : std::string_view { };
    }

    // The precedence of a binary operator, 0 for none; `**` binds tightest.
    [[nodiscard]] static int precedence(const std::string_view op)
    {
        if (op == "**") return 8;
        if (op == "*" || op == "/" || op == "%") return 7;
        if (op == "+" || op == "-") return 6;
        if (op == "<<" || op == ">>") return 5;
        if (op == "&") return 4;
        if (op == "^") return 3;
        if (op == "|") return 2;
        return 0;
    }

    [[nodiscard]] std::optional<std::int64_t> binary(const int minimum)
    {
        auto left = unary();
        while (left) {
            const auto op = peek();
            const auto level = precedence(op);
            if (level == 0 || level < minimum) {
                break;
            }
            const std::string operation { op };
            ++position_;
            // `**` is right associative.
            const auto right = binary(operation == "**" ? level : level + 1);
            if (!right) {
                return std::nullopt;
            }
            left = apply(operation, *left, *right);
        }
        return left;
    }

    [[nodiscard]] static std::optional<std::int64_t> apply(
        const std::string& op, const std::int64_t a, const std::int64_t b)
    {
        std::int64_t r { };
        if (op == "+") {
            return checked_add(a, b, r)
                ? std::optional<std::int64_t> { r } : std::nullopt;
        }
        if (op == "-") {
            return checked_sub(a, b, r)
                ? std::optional<std::int64_t> { r } : std::nullopt;
        }
        if (op == "*") {
            return checked_mul(a, b, r)
                ? std::optional<std::int64_t> { r } : std::nullopt;
        }
        if (op == "/" || op == "%") {
            if (b == 0 || (a == std::numeric_limits<std::int64_t>::min()
                    && b == -1)) {
                return std::nullopt;
            }
            return op == "/" ? a / b : a % b;
        }
        if (op == "<<" || op == ">>") {
            if (b < 0 || b > 62) {
                return std::nullopt;
            }
            if (op == ">>") {
                return a >> b;
            }
            return checked_mul(a, std::int64_t { 1 } << b, r)
                ? std::optional<std::int64_t> { r }
                : std::nullopt;
        }
        if (op == "&") return a & b;
        if (op == "|") return a | b;
        if (op == "^") return a ^ b;
        // `**`: a non-negative exponent of an integer.
        if (b < 0 || b > 62) {
            return std::nullopt;
        }
        std::int64_t power = 1;
        for (std::int64_t step = 0; step < b; ++step) {
            if (!checked_mul(power, a, power)) {
                return std::nullopt;
            }
        }
        return power;
    }

    [[nodiscard]] std::optional<std::int64_t> unary()
    {
        const auto op = peek();
        if (op == "-" || op == "+" || op == "~") {
            ++position_;
            const auto value = unary();
            if (!value) {
                return std::nullopt;
            }
            if (op == "~") {
                return ~*value;
            }
            if (op == "+") {
                return value;
            }
            if (*value == std::numeric_limits<std::int64_t>::min()) {
                return std::nullopt;
            }
            return -*value;
        }
        return primary();
    }

    // `[size]'[s]<base><digits>` or a decimal literal; underscores are
    // separators.
    [[nodiscard]] static std::optional<std::int64_t> literal(
        const std::string_view text)
    {
        const auto quote = text.find('\'');
        auto digits = quote == std::string_view::npos
            ? text
            : text.substr(quote + 1U);
        int base = 10;
        if (quote != std::string_view::npos) {
            if (!digits.empty() && (digits.front() == 's'
                    || digits.front() == 'S')) {
                digits.remove_prefix(1U);
            }
            if (digits.empty()) {
                return std::nullopt;
            }
            switch (digits.front()) {
            case 'b': case 'B': base = 2; break;
            case 'o': case 'O': base = 8; break;
            case 'd': case 'D': base = 10; break;
            case 'h': case 'H': base = 16; break;
            default: return std::nullopt;
            }
            digits.remove_prefix(1U);
        }
        if (digits.empty()) {
            return std::nullopt;
        }
        std::uint64_t value { };
        for (const auto character : digits) {
            if (character == '_') {
                continue;
            }
            int digit = -1;
            if (character >= '0' && character <= '9') {
                digit = character - '0';
            } else if (character >= 'a' && character <= 'f') {
                digit = character - 'a' + 10;
            } else if (character >= 'A' && character <= 'F') {
                digit = character - 'A' + 10;
            }
            constexpr auto limit = static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max());
            if (digit < 0 || digit >= base
                || value > (limit - static_cast<std::uint64_t>(digit))
                        / static_cast<std::uint64_t>(base)) {
                return std::nullopt;
            }
            value = value * static_cast<std::uint64_t>(base)
                + static_cast<std::uint64_t>(digit);
        }
        return static_cast<std::int64_t>(value);
    }

    [[nodiscard]] std::optional<std::int64_t> primary()
    {
        const auto text = peek();
        if (text.empty()) {
            return std::nullopt;
        }
        if (text == "(") {
            ++position_;
            const auto value = binary(0);
            if (!value || peek() != ")") {
                return std::nullopt;
            }
            ++position_;
            return value;
        }
        if (text == "$clog2") {
            ++position_;
            if (peek() != "(") {
                return std::nullopt;
            }
            ++position_;
            const auto value = binary(0);
            if (!value || peek() != ")") {
                return std::nullopt;
            }
            ++position_;
            std::int64_t bits { };
            while (bits < 62 && (std::int64_t { 1 } << bits) < *value) {
                ++bits;
            }
            return bits;
        }
        const auto first = static_cast<unsigned char>(text.front());
        if (std::isdigit(first) != 0 || text.front() == '\'') {
            // `8 'hFF` may arrive as a size and a based token.
            if (text.find('\'') == std::string_view::npos
                && peek(1U).starts_with('\'')) {
                const auto based = literal(peek(1U));
                position_ += 2U;
                return based;
            }
            ++position_;
            return literal(text);
        }
        if (std::isalpha(first) != 0 || text.front() == '_') {
            // A possibly package-scoped name: the constant is that of the
            // last component, resolved in the declaration's scope.
            std::string_view name = text;
            ++position_;
            while (peek() == "::" && position_ + 1U < tokens_.size()) {
                name = peek(1U);
                position_ += 2U;
            }
            return context_.constant ? context_.constant(name)
                                     : std::nullopt;
        }
        return std::nullopt;
    }

    std::span<const SourceToken> tokens_;
    const DpiTypeContext& context_;
    std::size_t position_ { };
};

// The index just past the `]` that closes the `[` at `open`; the range's
// own tokens lie between.
std::optional<std::size_t> dpi_close_bracket(
    const std::span<const SourceToken> tokens, const std::size_t open)
{
    std::size_t depth { };
    for (std::size_t index = open; index < tokens.size(); ++index) {
        const auto& text = tokens[index].text;
        if (text == "[" || text == "(") {
            ++depth;
        } else if (text == "]" || text == ")") {
            if (--depth == 0U) {
                return text == "]" ? std::optional { index }
                                   : std::nullopt;
            }
        }
    }
    return std::nullopt;
}

// The position of the `:` of a range at nesting depth zero.
std::optional<std::size_t> dpi_range_colon(
    const std::span<const SourceToken> inner)
{
    std::size_t depth { };
    for (std::size_t index = 0U; index < inner.size(); ++index) {
        const auto& text = inner[index].text;
        if (text == "[" || text == "(") {
            ++depth;
        } else if (text == "]" || text == ")") {
            if (depth == 0U) {
                return std::nullopt;
            }
            --depth;
        } else if (text == ":" && depth == 0U) {
            return index;
        }
    }
    return std::nullopt;
}

const DpiTypeContext& no_dpi_context()
{
    static const DpiTypeContext context;
    return context;
}

}  // namespace

std::optional<std::int64_t> dpi_constant_expression(
    const std::span<const SourceToken> tokens, const DpiTypeContext& context)
{
    return DpiConstantParser { tokens, context }.parse();
}

std::optional<std::vector<DpiDimension>> dpi_unpacked_dimensions(
    const std::span<const SourceToken> tokens, const DpiTypeContext& context)
{
    std::vector<DpiDimension> result;
    std::size_t index { };
    while (index < tokens.size()) {
        if (tokens[index].text != "[") {
            return std::nullopt;
        }
        const auto close = dpi_close_bracket(tokens, index);
        if (!close) {
            return std::nullopt;
        }
        const auto inner = tokens.subspan(index + 1U, *close - index - 1U);
        DpiDimension dimension;
        if (inner.empty()) {
            dimension.open = true;
        } else if (const auto colon = dpi_range_colon(inner)) {
            const auto left = dpi_constant_expression(
                inner.first(*colon), context);
            const auto right = dpi_constant_expression(
                inner.subspan(*colon + 1U), context);
            if (!left || !right) {
                return std::nullopt;
            }
            dimension.left = *left;
            dimension.right = *right;
        } else {
            // `[N]` is [0:N-1] (IEEE 1800-2023 7.4.2).
            const auto size = dpi_constant_expression(inner, context);
            if (!size || *size < 1) {
                return std::nullopt;
            }
            dimension.right = *size - 1;
        }
        result.push_back(dimension);
        index = *close + 1U;
    }
    return result;
}

namespace {

std::optional<DpiCType> dpi_builtin_c_type(
    const std::span<const SourceToken> tokens, const DpiTypeContext& context)
{
    DpiCType result;
    std::string_view base;
    bool explicit_signed { };
    bool explicit_unsigned { };
    std::size_t packed_width = 1U;
    bool open_packed { };
    bool sized_packed { };
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        const auto text = std::string_view { tokens[index].text };
        if (text == "signed") {
            explicit_signed = true;
            continue;
        }
        if (text == "unsigned") {
            explicit_unsigned = true;
            continue;
        }
        if (text == "[") {
            const auto close = dpi_close_bracket(tokens, index);
            if (!close) {
                return std::nullopt;
            }
            const auto inner = tokens.subspan(index + 1U, *close - index - 1U);
            if (inner.empty()) {
                // An open packed dimension is the only packed dimension
                // (IEEE 1800-2023 35.5.6.1).
                if (open_packed || sized_packed) {
                    return std::nullopt;
                }
                open_packed = true;
                index = *close;
                continue;
            }
            const auto colon = dpi_range_colon(inner);
            if (!colon || open_packed) {
                return std::nullopt;
            }
            const auto left = dpi_constant_expression(
                inner.first(*colon), context);
            const auto right = dpi_constant_expression(
                inner.subspan(*colon + 1U), context);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto extent = *left >= *right
                ? static_cast<std::uint64_t>(*left) - static_cast<std::uint64_t>(*right) + 1U
                : static_cast<std::uint64_t>(*right) - static_cast<std::uint64_t>(*left) + 1U;
            if (extent == 0U
                || extent > std::numeric_limits<std::size_t>::max()
                || packed_width
                    > std::numeric_limits<std::size_t>::max()
                        / static_cast<std::size_t>(extent)) {
                return std::nullopt;
            }
            packed_width *= static_cast<std::size_t>(extent);
            sized_packed = true;
            index = *close;
            continue;
        }
        if (text == "bit" || text == "logic" || text == "reg"
            || text == "byte" || text == "shortint" || text == "int"
            || text == "integer" || text == "longint" || text == "time"
            || text == "chandle" || text == "shortreal"
            || text == "real" || text == "realtime" || text == "string") {
            if (!base.empty()) {
                return std::nullopt;
            }
            base = text;
            continue;
        }
        return std::nullopt;
    }
    if (base.empty() || (explicit_signed && explicit_unsigned)) {
        return std::nullopt;
    }
    result.string = base == "string";
    if (result.string) {
        result.code = 'z';
        return packed_width == 1U && !open_packed && !sized_packed
                && !explicit_signed && !explicit_unsigned
            ? std::optional<DpiCType> { result }
            : std::nullopt;
    }
    const bool ranged = packed_width != 1U
        || std::ranges::any_of(tokens, [](const auto& token) {
               return token.text == "[";
           });
    if (ranged && base != "bit" && base != "logic" && base != "reg") {
        return std::nullopt;
    }
    result.code = base == "byte" ? 'c'
        : base == "shortint"     ? 's'
        : base == "int"          ? 'i'
        : base == "longint"      ? 'l'
        : base == "real" || base == "realtime" ? 'd'
        : base == "shortreal"    ? 'f'
        : base == "chandle"      ? 'p'
        : base == "integer" || base == "time" ? 'V'
        : base == "bit"          ? (ranged ? 'B' : 'b')
                                 : (ranged ? 'V' : 'g');
    if (explicit_unsigned
        && (result.code == 'c' || result.code == 's' || result.code == 'i'
            || result.code == 'l')) {
        result.code = static_cast<char>(result.code - 'a' + 'A');
    }
    result.scalar = result.code == 'd' ? frontend::SystemVerilogScalarKind::Real
        : result.code == 'f' ? frontend::SystemVerilogScalarKind::ShortReal
        : result.code == 'p' ? frontend::SystemVerilogScalarKind::Chandle
                             : frontend::SystemVerilogScalarKind::None;
    const auto base_width = base == "byte" ? 8U
        : base == "shortint" ? 16U
        : base == "int" || base == "integer" || base == "shortreal"
            ? 32U
        : base == "longint" || base == "time" || base == "chandle"
                || base == "real" || base == "realtime"
            ? 64U
            : 1U;
    if (packed_width > std::numeric_limits<std::size_t>::max()
            / base_width) {
        return std::nullopt;
    }
    result.width = packed_width * base_width;
    if (open_packed) {
        result.open_packed = true;
        result.width = 0U;
    }
    result.domain = base == "logic" || base == "reg"
            || base == "integer" || base == "time"
        ? frontend::ValueDomain::Logic4
        : base == "byte" || base == "shortint" || base == "int"
                || base == "longint"
            ? frontend::ValueDomain::Integer
            : frontend::ValueDomain::Bit2;
    const auto implicitly_signed = base == "byte" || base == "shortint"
        || base == "int" || base == "integer" || base == "longint";
    result.signed_value = explicit_signed
        || (!explicit_unsigned && implicitly_signed);
    return result;
}

}  // namespace

std::optional<DpiCType> dpi_c_type(
    const std::span<const semantic::sv::SourceToken> tokens)
{
    return dpi_builtin_c_type(tokens, no_dpi_context());
}

namespace {

constexpr std::size_t dpi_type_depth_limit = 64U;

bool dpi_builtin_keyword(const std::string_view text)
{
    return text == "bit" || text == "logic" || text == "reg"
        || text == "byte" || text == "shortint" || text == "int"
        || text == "integer" || text == "longint" || text == "time"
        || text == "chandle" || text == "shortreal" || text == "real"
        || text == "realtime" || text == "string";
}

semantic::sv::SourceToken dpi_token(std::string text)
{
    semantic::sv::SourceToken token;
    token.text = std::move(text);
    return token;
}

std::optional<std::pair<std::int64_t, std::int64_t>> dpi_range_bounds(
    const semantic::sv::PackedRange& range, const DpiTypeContext& context)
{
    const auto bound = [&](const std::optional<std::int64_t> value,
                           const std::optional<semantic::ExpressionId>
                               expression) -> std::optional<std::int64_t> {
        if (value) {
            return value;
        }
        return expression && context.evaluate
            ? context.evaluate(*expression)
            : std::nullopt;
    };
    const auto left = bound(range.left, range.left_expression);
    const auto right = bound(range.right, range.right_expression);
    if (!left || !right) {
        return std::nullopt;
    }
    return std::pair { *left, *right };
}

std::optional<std::uint64_t> dpi_range_extent(
    const semantic::sv::PackedRange& range, const DpiTypeContext& context)
{
    const auto bounds = dpi_range_bounds(range, context);
    if (!bounds) {
        return std::nullopt;
    }
    const auto [left, right] = *bounds;
    return left >= right
        ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right) + 1U
        : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left) + 1U;
}

// The extent of a reference's packed dimensions: 1 when it has none.
std::optional<std::uint64_t> dpi_packed_extent(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context)
{
    std::uint64_t extent = 1U;
    if (type.packed_dimensions.size() > 1U) {
        for (const auto& dimension : type.packed_dimensions) {
            const auto count = dpi_range_extent(dimension, context);
            if (!count || *count == 0U
                || extent > (std::uint64_t { 1 } << 32U) / *count) {
                return std::nullopt;
            }
            extent *= *count;
        }
        return extent;
    }
    if (type.packed_range) {
        return dpi_range_extent(*type.packed_range, context);
    }
    return extent;
}

std::optional<DpiCType> dpi_reference_c_type_impl(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    std::size_t depth);

std::optional<DpiCType> dpi_reference_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    std::size_t depth);

// An unpacked structure maps to a C structure (IEEE 1800-2023 H.7.8); its
// members are small values, packed vectors, structures, or arrays of those.
// A member that is a string, dynamic or a class handle has no mapping.
std::optional<DpiCType> dpi_struct_c_type(
    const semantic::sv::TypeDefinition& definition,
    const std::string_view spelling, const DpiTypeContext& context,
    const std::size_t depth)
{
    if (definition.members.empty()) {
        return std::nullopt;
    }
    constexpr std::uint64_t bit_limit = std::uint64_t { 1 } << 24U;
    DpiCType aggregate;
    aggregate.code = 'R';
    aggregate.domain = frontend::ValueDomain::Bit2;
    std::uint64_t bits { };
    for (const auto& member : definition.members) {
        const auto& declared = member.type;
        if ((declared.container_form
                && *declared.container_form
                    != semantic::sv::TypeForm::static_array)
            || (declared.container_form
                && declared.unpacked_dimensions.empty())
            || declared.virtual_interface
            || !declared.class_identity.empty()) {
            return std::nullopt;
        }
        DpiCMember entry;
        entry.name = member.name;
        std::uint64_t count = 1U;
        for (const auto& range : declared.unpacked_dimensions) {
            const auto bounds = dpi_range_bounds(range, context);
            const auto extent = dpi_range_extent(range, context);
            if (!bounds || !extent || *extent == 0U
                || count > bit_limit / *extent) {
                return std::nullopt;
            }
            count *= *extent;
            entry.dimensions.push_back(*bounds);
        }
        auto element = declared;
        element.unpacked_dimensions.clear();
        element.container_form.reset();
        auto type = dpi_reference_c_type(element, context, depth + 1U);
        if (!type || type->string || type->open_packed || type->width == 0U
            || type->width > bit_limit
            || bits + count * type->width > bit_limit) {
            return std::nullopt;
        }
        if (type->domain == frontend::ValueDomain::Logic4) {
            aggregate.domain = frontend::ValueDomain::Logic4;
        }
        bits += count * type->width;
        entry.type = std::move(*type);
        aggregate.members.push_back(std::move(entry));
    }
    aggregate.width = static_cast<std::size_t>(bits);
    if (context.struct_tag) {
        aggregate.tag = context.struct_tag(definition.id, spelling);
    }
    return aggregate;
}

// FSIM_DPI_TYPE_TRACE=1 names each reference the mapping rejects.
std::optional<DpiCType> dpi_reference_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    const std::size_t depth)
{
    auto result = dpi_reference_c_type_impl(type, context, depth);
    static const bool trace = std::getenv("FSIM_DPI_TYPE_TRACE") != nullptr;
    if (trace) {
        std::cerr << "[fsim-dpi-type] depth=" << depth << " spelling='"
                  << type.target.spelling << "' target="
                  << (type.target.target.valid()
                             ? std::to_string(type.target.target.value())
                             : std::string { "-" })
                  << " packed_range="
                  << (type.packed_range
                             ? "[" + (type.packed_range->left
                                          ? std::to_string(*type.packed_range->left)
                                          : std::string { "e" })
                                 + ":"
                                 + (type.packed_range->right
                                         ? std::to_string(*type.packed_range->right)
                                         : std::string { "e" })
                                 + "]"
                             : std::string { "-" })
                  << " dims=" << type.packed_dimensions.size()
                  << " -> " << (result ? std::string(1, result->code) + ":"
                                       + std::to_string(result->width)
                                       : std::string { "none" })
                  << '\n';
    }
    return result;
}

std::optional<DpiCType> dpi_reference_c_type_impl(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    const std::size_t depth)
{
    if (depth > dpi_type_depth_limit || type.container_form
        || !type.unpacked_dimensions.empty() || type.virtual_interface
        || !type.class_identity.empty()) {
        return std::nullopt;
    }
    std::vector<semantic::sv::SourceToken> tokens;
    for (std::size_t start = 0U; start < type.target.spelling.size();) {
        const auto end = type.target.spelling.find(' ', start);
        const auto word = type.target.spelling.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) {
            tokens.push_back(dpi_token(word));
        }
        start = end == std::string::npos ? type.target.spelling.size()
                                         : end + 1U;
    }
    if (!tokens.empty() && dpi_builtin_keyword(tokens.front().text)) {
        // A built-in type keeps its keyword, its signedness and its
        // packed dimensions, flattened to one range; the dimensions come
        // from the reference, not from the spelling.
        std::erase_if(tokens, [](const auto& token) {
            return !dpi_builtin_keyword(token.text) && token.text != "signed"
                && token.text != "unsigned";
        });
        const auto base = tokens.front().text;
        const bool integral = base != "chandle" && base != "shortreal"
            && base != "real" && base != "realtime" && base != "string";
        const bool explicit_sign = std::ranges::any_of(tokens,
            [](const auto& token) {
                return token.text == "signed" || token.text == "unsigned";
            });
        if (integral && !explicit_sign) {
            tokens.push_back(dpi_token(
                type.signed_value ? "signed" : "unsigned"));
        }
        // An integer atom's own width is no packed dimension.
        const bool vector_base
            = base == "bit" || base == "logic" || base == "reg";
        const auto extent = vector_base ? dpi_packed_extent(type, context)
                                        : std::optional<std::uint64_t> { 1U };
        if (!extent || *extent == 0U) {
            return std::nullopt;
        }
        if (vector_base
            && (type.packed_range || type.packed_dimensions.size() > 1U)) {
            tokens.push_back(dpi_token("["));
            tokens.push_back(dpi_token(std::to_string(*extent - 1U)));
            tokens.push_back(dpi_token(":"));
            tokens.push_back(dpi_token("0"));
            tokens.push_back(dpi_token("]"));
        }
        return dpi_c_type(tokens);
    }
    if (!type.target.target.valid() || !context.definition) {
        return std::nullopt;
    }
    const auto* const definition = context.definition(type.target.target);
    if (definition == nullptr) {
        return std::nullopt;
    }
    using Form = semantic::sv::TypeForm;
    std::optional<DpiCType> element;
    if (definition->form == Form::packed_structure
        || definition->form == Form::packed_union) {
        // A packed structure or union is a vector of its members' bits;
        // any 4-state member makes it a logic vector (H.7.6).
        if (definition->members.empty()) {
            return std::nullopt;
        }
        DpiCType aggregate;
        bool four_state { };
        for (const auto& member : definition->members) {
            const auto member_type
                = dpi_reference_c_type(member.type, context, depth + 1U);
            if (!member_type || member_type->string
                || member_type->scalar
                    != frontend::SystemVerilogScalarKind::None) {
                return std::nullopt;
            }
            four_state = four_state
                || member_type->domain == frontend::ValueDomain::Logic4;
            aggregate.width = definition->form == Form::packed_structure
                ? aggregate.width + member_type->width
                : std::max(aggregate.width, member_type->width);
        }
        aggregate.code = four_state ? 'V' : 'B';
        aggregate.domain = four_state ? frontend::ValueDomain::Logic4
                                      : frontend::ValueDomain::Bit2;
        element = aggregate;
    } else if (definition->form == Form::string) {
        DpiCType text;
        text.code = 'z';
        text.string = true;
        element = text;
    } else if (definition->form == Form::unpacked_structure) {
        element = dpi_struct_c_type(
            *definition, type.target.spelling, context, depth);
    } else if (definition->form != Form::unpacked_union
        && definition->form != Form::tagged_union
        && definition->form != Form::class_handle
        && definition->form != Form::dynamic_array
        && definition->form != Form::queue
        && definition->form != Form::associative_array
        && definition->form != Form::static_array
        && definition->base != type) {
        // A typedef, enumeration or type parameter maps like its base.
        element = dpi_reference_c_type(definition->base, context, depth + 1U);
    }
    // An unpacked structure has no packed dimensions (IEEE 1800-2023 7.2):
    // a range on its reference is the members' summed width, which leaves
    // out their unpacked dimensions, and does not size the C structure.
    if (!element || element->code == 'R'
        || (!type.packed_range && type.packed_dimensions.size() < 2U)) {
        return element;
    }
    // A named packed type's reference spells its flattened width: its own
    // `[W-1:0]`, or for a packed array of it the outer dimensions followed
    // by that range, so the extent is the whole vector's width.
    const auto extent = dpi_packed_extent(type, context);
    if (!extent || *extent == 0U || element->string
        || element->scalar != frontend::SystemVerilogScalarKind::None
        || element->width == 0U || *extent % element->width != 0U) {
        return std::nullopt;
    }
    if (*extent == element->width) {
        return element;
    }
    element->width = static_cast<std::size_t>(*extent);
    const bool four_state = element->domain == frontend::ValueDomain::Logic4;
    element->code = four_state ? 'V' : 'B';
    element->domain = four_state ? frontend::ValueDomain::Logic4
                                 : frontend::ValueDomain::Bit2;
    element->signed_value = false;
    return element;
}

}  // namespace

std::optional<DpiCType> dpi_c_type(
    const std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context)
{
    if (auto result = dpi_builtin_c_type(tokens, context)) {
        return result;
    }
    // A typedef name, possibly package-qualified (`pkg::name_t`).
    if (tokens.empty() || !context.named_type) {
        return std::nullopt;
    }
    for (std::size_t index = 1U; index < tokens.size(); index += 2U) {
        if (tokens[index].text != "::" || index + 1U >= tokens.size()) {
            return std::nullopt;
        }
    }
    const auto type = context.named_type(tokens.back().text);
    if (!type) {
        return std::nullopt;
    }
    auto result = dpi_reference_c_type(*type, context, 0U);
    // The typedef the declaration names gives its structure the C name.
    if (result && result->code == 'R' && context.struct_tag
        && type->target.target.valid()) {
        result->tag
            = context.struct_tag(type->target.target, tokens.back().text);
    }
    return result;
}

std::optional<DpiCType> dpi_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context)
{
    return dpi_reference_c_type(type, context, 0U);
}

std::optional<DpiTypeIssue> dpi_type_issue(
    const std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context)
{
    if (tokens.empty()) {
        return std::nullopt;
    }
    for (std::size_t index = 1U; index < tokens.size(); index += 2U) {
        if (tokens[index].text != "::" || index + 1U >= tokens.size()) {
            return std::nullopt;
        }
    }
    const auto& name = tokens.back().text;
    if (context.class_named && context.class_named(name)) {
        return DpiTypeIssue::class_handle;
    }
    if (!context.named_type) {
        return std::nullopt;
    }
    using Form = semantic::sv::TypeForm;
    auto type = context.named_type(name);
    // A typedef may name another typedef; follow the chain to the form.
    for (std::size_t depth = 0U; type && depth < dpi_type_depth_limit;
         ++depth) {
        if (type->container_form) {
            return *type->container_form == Form::static_array
                ? std::nullopt
                : std::optional { DpiTypeIssue::dynamic_array };
        }
        if (!type->class_identity.empty()) {
            return DpiTypeIssue::class_handle;
        }
        if (!context.definition || !type->target.target.valid()) {
            return std::nullopt;
        }
        const auto* const definition = context.definition(type->target.target);
        if (definition == nullptr) {
            return std::nullopt;
        }
        switch (definition->form) {
        case Form::class_handle:
            return DpiTypeIssue::class_handle;
        case Form::unpacked_union:
        case Form::tagged_union:
            return DpiTypeIssue::unpacked_union;
        case Form::dynamic_array:
        case Form::queue:
        case Form::associative_array:
            return DpiTypeIssue::dynamic_array;
        default:
            break;
        }
        if (definition->base == *type) {
            return std::nullopt;
        }
        type = definition->base;
    }
    return std::nullopt;
}

std::string dpi_c_type_signature(const DpiCType& type)
{
    if (type.code == 'R') {
        std::string text = "R(";
        for (std::size_t index = 0U; index < type.members.size(); ++index) {
            const auto& member = type.members[index];
            text += (index == 0U ? "" : ";") + dpi_c_type_signature(member.type);
            for (const auto& [left, right] : member.dimensions) {
                text += '/' + std::to_string(left) + '/'
                    + std::to_string(right);
            }
        }
        return text + ')';
    }
    std::string text(1U, type.code);
    if (type.code == 'B' || type.code == 'V') {
        text += std::to_string(type.width);
    }
    return text;
}

std::uint64_t dpi_c_type_stream_bits(const DpiCType& type)
{
    if (type.code != 'R') {
        return type.width;
    }
    std::uint64_t bits { };
    for (const auto& member : type.members) {
        std::uint64_t count = 1U;
        for (const auto& [left, right] : member.dimensions) {
            count *= left >= right
                ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right) + 1U
                : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left) + 1U;
        }
        bits += count * dpi_c_type_stream_bits(member.type);
    }
    return bits;
}

DpiTypeContext dpi_hir_type_context(
    const semantic::sv::Hir& hir, const semantic::ScopeId scope)
{
    DpiTypeContext context;
    context.named_type = [&hir, scope](const std::string_view name)
        -> std::optional<semantic::sv::TypeReference> {
        const semantic::sv::Declaration* found { };
        std::size_t matches { };
        for (const auto& declaration : hir.declarations()) {
            if (declaration.form
                    != semantic::sv::DeclarationForm::typedef_declaration
                || declaration.name != name || !declaration.type) {
                continue;
            }
            if (declaration.scope == scope) {
                return declaration.type;
            }
            found = &declaration;
            ++matches;
        }
        return matches == 1U ? found->type : std::nullopt;
    };
    context.definition = [&hir](const semantic::TypeId type) {
        return hir.find_type(type);
    };
    context.class_named = [&hir](const std::string_view name) {
        return std::ranges::any_of(hir.classes(),
            [name](const semantic::sv::ClassDeclaration& declaration) {
                return declaration.name == name;
            });
    };
    // A structure's C name is its typedef name qualified by the unit that
    // declares it (a package or module), so equal names of different
    // scopes stay distinct; a clash that remains takes the type's id.
    struct Tags {
        // By typedef declaration, or by type for a structure no typedef
        // names.
        std::map<std::pair<bool, std::uint64_t>, std::string> by_owner;
        std::set<std::string, std::less<>> used;
    };
    const auto tags = std::make_shared<Tags>();
    context.struct_tag = [&hir, tags](const semantic::TypeId type,
                             const std::string_view spelling) {
        // A structure body is anonymous and shared by equal layouts: the
        // typedef the reference names gives its name, else the first
        // typedef declaring it.
        using Form = semantic::sv::DeclarationForm;
        std::string_view name = spelling;
        if (const auto qualifier = name.rfind("::");
            qualifier != std::string_view::npos) {
            name.remove_prefix(qualifier + 2U);
        }
        while (!name.empty() && name.front() == ' ') {
            name.remove_prefix(1U);
        }
        name = name.substr(0U, name.find_first_of(" ["));
        const auto* const definition = hir.find_type(type);
        const auto* declaration = definition != nullptr
            ? hir.find_declaration(definition->declaration)
            : nullptr;
        if (declaration == nullptr
            || declaration->form != Form::typedef_declaration
            || (!name.empty() && declaration->name != name)) {
            const semantic::sv::Declaration* first { };
            const semantic::sv::Declaration* named { };
            for (const auto& candidate : hir.declarations()) {
                if (candidate.form != Form::typedef_declaration
                    || (candidate.declared_type != type
                        && (!candidate.type
                            || candidate.type->target.target != type))) {
                    continue;
                }
                if (first == nullptr) {
                    first = &candidate;
                }
                if (candidate.name == name) {
                    named = &candidate;
                    break;
                }
            }
            if (named != nullptr || first != nullptr) {
                declaration = named != nullptr ? named : first;
            }
        }
        const bool by_typedef = declaration != nullptr
            && declaration->form == Form::typedef_declaration;
        const std::pair key { by_typedef,
            by_typedef ? static_cast<std::uint64_t>(declaration->id.value())
                       : static_cast<std::uint64_t>(type.value()) };
        if (const auto found = tags->by_owner.find(key);
            found != tags->by_owner.end()) {
            return found->second;
        }
        std::string base = "fsim_dpi_";
        if (declaration != nullptr) {
            for (const auto& unit : hir.units()) {
                if (unit.scope == declaration->scope) {
                    base += unit.name + '_';
                    break;
                }
            }
        }
        base += by_typedef
            ? declaration->name
            : definition != nullptr && !definition->name.empty()
            ? definition->name
            : std::string { "struct" };
        for (auto& character : base) {
            if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
                character = '_';
            }
        }
        if (!tags->used.insert(base).second) {
            base += '_' + std::to_string(key.second);
            tags->used.insert(base);
        }
        tags->by_owner.emplace(key, base);
        return base;
    };
    return context;
}

namespace {

std::string dpi_c_scalar(const char code)
{
    switch (code) {
    case 'c':
        return "char";
    case 'C':
        return "unsigned char";
    case 's':
        return "short";
    case 'S':
        return "unsigned short";
    case 'i':
        return "int";
    case 'I':
        return "unsigned int";
    case 'l':
        return "long long";
    case 'L':
        return "unsigned long long";
    case 'd':
        return "double";
    case 'f':
        return "float";
    case 'p':
        return "void*";
    case 'z':
        return "const char*";
    case 'b':
        return "svBit";
    case 'g':
        return "svLogic";
    default:
        return { };
    }
}

// The C type of one value or array element: a vector is its canonical
// type, an unpacked structure its generated typedef (H.7.8).
std::string dpi_c_element(const DpiCType& type)
{
    if (type.code == 'R') {
        return type.tag;
    }
    if (type.code == 'B') {
        return "svBitVecVal";
    }
    if (type.code == 'V') {
        return "svLogicVecVal";
    }
    return dpi_c_scalar(type.code);
}

// A formal's C parameter type (IEEE 1800-2023 H.7.4, H.8.7 to H.8.10,
// H.12): inputs pass small values by value and vectors, structures and
// sized arrays by const pointer; outputs and inouts pass by pointer; open
// arrays pass a handle.
std::optional<std::string> dpi_c_parameter(
    const std::optional<DpiCType>& type,
    const semantic::sv::Direction direction, const bool open_array,
    const bool sized_array, const std::string& name)
{
    if (!type) {
        return std::nullopt;
    }
    const bool input = direction == semantic::sv::Direction::input;
    if (!input && direction != semantic::sv::Direction::output
        && direction != semantic::sv::Direction::inout) {
        return std::nullopt;
    }
    if (open_array) {
        return std::string { input ? "const svOpenArrayHandle "
                                   : "svOpenArrayHandle " }
            + name;
    }
    const auto element = dpi_c_element(*type);
    if (element.empty()) {
        return std::nullopt;
    }
    if (sized_array || type->code == 'B' || type->code == 'V'
        || type->code == 'R') {
        return (input ? "const " : "") + element + "* " + name;
    }
    return element + (input ? " " : "* ") + name;
}

// A function result is a small value, never a packed vector (IEEE
// 1800-2023 35.5.5).
std::optional<std::string> dpi_c_result(const std::optional<DpiCType>& type)
{
    if (!type || type->code == 'B' || type->code == 'V'
        || type->code == 'R' || type->open_packed) {
        return std::nullopt;
    }
    auto result = dpi_c_scalar(type->code);
    return result.empty() ? std::nullopt : std::optional { result };
}

// The C result type and parameter declarations of an import or export.
struct DpiCSignature {
    std::string result;
    std::vector<std::string> parameters;
    std::vector<std::string> names;
    // The unpacked structures the formals use, for their typedefs.
    std::vector<DpiCType> aggregates;
};

std::optional<DpiCSignature> dpi_c_signature(
    const semantic::sv::DpiDeclaration& declaration,
    const DpiTypeContext& context)
{
    std::vector<std::string> names;
    std::vector<std::string> parameters;
    std::vector<DpiCType> aggregates;
    std::string result;
    const bool task
        = declaration.callable_kind == semantic::sv::DpiCallableKind::task;
    if (declaration.direction
        == semantic::sv::DpiDirection::export_declaration) {
        // An export's types are those of the callable it names.
        if (!declaration.resolved_profile) {
            return std::nullopt;
        }
        const auto& profile = *declaration.resolved_profile;
        if (task) {
            result = "int";
        } else if (!profile.return_type
            || profile.return_type->target.spelling == "void") {
            result = "void";
        } else {
            const auto type = dpi_c_result(
                dpi_c_type(*profile.return_type, context));
            if (!type) {
                return std::nullopt;
            }
            result = *type;
        }
        for (const auto& formal : profile.formals) {
            // An export has no open arrays; a sized unpacked formal passes
            // its elements by pointer and a dynamic one is erroneous (IEEE
            // 1800-2023 35.5.6).
            auto element = formal.type;
            const bool sized_array = !element.unpacked_dimensions.empty();
            if (element.container_form && !sized_array) {
                return std::nullopt;
            }
            element.unpacked_dimensions.clear();
            element.container_form.reset();
            const auto formal_type = dpi_c_type(element, context);
            const auto parameter = dpi_c_parameter(formal_type,
                formal.direction, false, sized_array, formal.name);
            if (!parameter) {
                return std::nullopt;
            }
            if (formal_type->code == 'R') {
                aggregates.push_back(*formal_type);
            }
            parameters.push_back(*parameter);
            names.push_back(formal.name);
        }
    } else {
        if (task) {
            // A task returns its disable status (IEEE 1800-2023 35.9).
            result = "int";
        } else if (declaration.return_type_tokens.size() == 1U
            && declaration.return_type_tokens.front().text == "void") {
            result = "void";
        } else {
            const auto type = dpi_c_result(
                dpi_c_type(declaration.return_type_tokens, context));
            if (!type) {
                return std::nullopt;
            }
            result = *type;
        }
        for (const auto& formal : declaration.formals) {
            // `[]` dimensions and an open packed range make an open array
            // (H.12); sized dimensions pass the elements by pointer.
            const auto dimensions = dpi_unpacked_dimensions(
                formal.dimension_tokens, context);
            const auto type = dpi_c_type(formal.type_tokens, context);
            if (!dimensions || !type) {
                return std::nullopt;
            }
            const bool open_array = type->open_packed
                || std::ranges::any_of(*dimensions,
                    [](const DpiDimension& dimension) {
                        return dimension.open;
                    });
            const auto parameter = dpi_c_parameter(type, formal.direction,
                open_array, !dimensions->empty(), formal.name);
            if (!parameter) {
                return std::nullopt;
            }
            if (type->code == 'R') {
                aggregates.push_back(*type);
            }
            parameters.push_back(*parameter);
            names.push_back(formal.name);
        }
    }
    return DpiCSignature { result, parameters, names, aggregates };
}

// The C member declaration of one structure member: a scalar by its C
// type, a packed vector as an array of 32-bit chunks, a nested structure by
// its typedef, each followed by the member's unpacked dimensions (H.7.8).
std::string dpi_c_member(const DpiCMember& member)
{
    std::string text = "    " + dpi_c_element(member.type) + ' ' + member.name;
    if (member.type.code == 'B' || member.type.code == 'V') {
        text += '[' + std::to_string((member.type.width + 31U) / 32U) + ']';
    }
    for (const auto& [left, right] : member.dimensions) {
        text += '[' + std::to_string((left >= right ? left - right
                                                    : right - left) + 1)
            + ']';
    }
    return text + ";\n";
}

// The typedef of an unpacked structure after those of its nested
// structures, once per tag (IEEE 1800-2023 H.7.8).
void dpi_c_struct_typedef(const DpiCType& type,
    std::set<std::string, std::less<>>& declared, std::string& out)
{
    for (const auto& member : type.members) {
        if (member.type.code == 'R') {
            dpi_c_struct_typedef(member.type, declared, out);
        }
    }
    if (!declared.insert(type.tag).second) {
        return;
    }
    out += "typedef struct " + type.tag + " {\n";
    for (const auto& member : type.members) {
        out += dpi_c_member(member);
    }
    out += "} " + type.tag + ";\n";
}

std::string dpi_c_declarator(
    const DpiCSignature& signature, const std::string& linkage)
{
    auto result = signature.result + ' ' + linkage + '(';
    for (std::size_t index = 0U; index < signature.parameters.size();
         ++index) {
        result += (index == 0U ? "" : ", ") + signature.parameters[index];
    }
    if (signature.parameters.empty()) {
        result += "void";
    }
    return result + ')';
}

// A weak definition of an export that the C code links against: it hands
// the address of each argument's C value (an argument passed by pointer
// is that address) to the simulator, which runs the export. An exported
// task returns 1 when it returns due to a disable (IEEE 1800-2023 35.9 a).
std::string dpi_export_stub(const DpiCSignature& signature,
    const std::string& linkage, const bool task)
{
    std::string arguments;
    for (std::size_t index = 0U; index < signature.parameters.size();
         ++index) {
        const auto& parameter = signature.parameters[index];
        const auto& name = signature.names[index];
        const auto type = std::string_view { parameter }.substr(
            0U, parameter.size() - name.size());
        const bool by_value = type.find('*') == std::string_view::npos
            || type.starts_with("const char* ")
            || type.starts_with("void* ") || type.starts_with("const void* ");
        arguments += std::string { index == 0U ? "" : ", " }
            + "(void*)" + (by_value ? "&" : "") + name;
    }
    const bool value = signature.result != "void";
    return "__attribute__((weak)) " + dpi_c_declarator(signature, linkage)
        + "\n{\n    void* fsim_arguments[] = { "
        + (arguments.empty() ? std::string { "0" } : arguments) + " };\n"
        + (value ? "    " + signature.result + " fsim_result = 0;\n" : "")
        + (task ? "    const int fsim_status = " : "    (void)")
        + "fsim_svdpi_export_call_v3(\"" + linkage + "\", fsim_arguments, "
        + (value ? "&fsim_result" : "0") + ");\n"
        + (task ? "    return fsim_status != 0 ? 1 : fsim_result;\n"
                : value ? "    return fsim_result;\n" : "")
        + "}\n";
}

}  // namespace

std::string systemverilog_dpi_header(const semantic::sv::Hir& hir)
{
    // Declarations in a design unit belong to it; those in a compilation
    // unit's scope to the design (IEEE 1800-2023 35.5.4).
    std::vector<const semantic::sv::DpiDeclaration*> declarations;
    for (const auto& declaration : hir.dpi_declarations()) {
        declarations.push_back(&declaration);
    }
    for (const auto& unit : hir.units()) {
        for (const auto& declaration : unit.dpi_declarations) {
            declarations.push_back(&declaration);
        }
    }
    std::string result
        = "/* DPI-C prototypes of the design's imports and exports (IEEE "
          "1800-2023 35, Annex H). */\n"
          "#ifndef FSIM_DPI_HEADER_H\n#define FSIM_DPI_HEADER_H\n"
          "#include \"svdpi.h\"\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
          "extern int fsim_svdpi_export_call_v3(const char* name, "
          "void** arguments, void* result);\n";
    // The structure typedefs precede the prototypes that use them.
    std::string types;
    std::string body;
    std::set<std::string, std::less<>> typedefs;
    std::string stubs;
    std::set<std::string, std::less<>> declared;
    for (const auto* const entry : declarations) {
        const auto& declaration = *entry;
        // The hidden imports of a unit's export service are not C code.
        if (!declaration.validated || declaration.linkage_name.empty()
            || declaration.linkage_name.starts_with("__fsim_dpi_export_")
            || !declared.insert(declaration.linkage_name).second) {
            continue;
        }
        const bool import
            = declaration.direction == semantic::sv::DpiDirection::import;
        const auto signature = dpi_c_signature(declaration,
            dpi_hir_type_context(hir, declaration.owner_scope));
        const auto prototype = signature
            ? std::optional { dpi_c_declarator(
                  *signature, declaration.linkage_name) }
            : std::nullopt;
        if (signature) {
            for (const auto& aggregate : signature->aggregates) {
                dpi_c_struct_typedef(aggregate, typedefs, types);
            }
        }
        if (!import && signature) {
            stubs += dpi_export_stub(*signature, declaration.linkage_name,
                declaration.callable_kind
                    == semantic::sv::DpiCallableKind::task);
        }
        if (!prototype) {
            body += "/* " + std::string { import ? "import" : "export" }
                + ' ' + declaration.linkage_name
                + ": no C prototype for its types */\n";
            continue;
        }
        body += "/* " + std::string { import ? "Imported" : "Exported" }
            + " " + (declaration.callable_kind
                    == semantic::sv::DpiCallableKind::task
                ? "task" : "function")
            + " " + declaration.systemverilog_name + " */\nextern "
            + *prototype + ";\n";
    }
    if (!stubs.empty()) {
        body += "#ifndef FSIM_DPI_NO_EXPORT_STUBS\n" + stubs + "#endif\n";
    }
    return result + types + body
        + "#ifdef __cplusplus\n}\n#endif\n#endif\n";
}

}  // namespace fsim::elaboration
