// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_structure_image.hpp"

#include "fsim/support/index_distance.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <system_error>
#include <string_view>
#include <utility>

namespace fsim::semantic::sv {
namespace {

    // The width of a predefined integer type (IEEE 1800-2017 6.11).
    [[nodiscard]] std::optional<std::size_t> integer_type_width(
        std::string_view spelling)
    {
        static constexpr std::array<std::pair<std::string_view, std::size_t>, 9>
            widths { {
                { "bit", 1U },
                { "logic", 1U },
                { "reg", 1U },
                { "byte", 8U },
                { "shortint", 16U },
                { "int", 32U },
                { "integer", 32U },
                { "longint", 64U },
                { "time", 64U },
            } };
        for (const auto& [name, width] : widths) {
            if (name == spelling) {
                return width;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const TypeDefinition* named_definition(
        const Hir& hir, const TypeReference& reference)
    {
        if (reference.target.target.valid()) {
            return hir.find_type(reference.target.target);
        }
        auto spelling = std::string_view { reference.target.spelling };
        if (const auto cut = spelling.rfind("::");
            cut != std::string_view::npos) {
            spelling.remove_prefix(cut + 2U);
        }
        if (spelling.empty()) {
            return nullptr;
        }
        const TypeDefinition* found = nullptr;
        for (const auto& candidate : hir.types()) {
            if (candidate.name != spelling) {
                continue;
            }
            if (found != nullptr) {
                return nullptr;
            }
            found = &candidate;
        }
        return found;
    }

    // The bits of a packed value of `type` outside any structure.
    [[nodiscard]] std::optional<std::size_t> packed_width(
        const TypeReference& type)
    {
        if (type.executable_width) {
            return static_cast<std::size_t>(*type.executable_width);
        }
        if (type.packed_range && type.packed_range->left
            && type.packed_range->right) {
            return static_cast<std::size_t>(support::index_distance(
                       *type.packed_range->left, *type.packed_range->right))
                + 1U;
        }
        return integer_type_width(type.target.spelling);
    }

} // namespace

std::optional<std::uint64_t> systemverilog_based_literal_bits(
    std::string_view text)
{
    const auto tick = text.find('\'');
    if (tick == std::string_view::npos || tick + 2U > text.size()) {
        return std::nullopt;
    }
    auto position = tick + 1U;
    if (text[position] == 's' || text[position] == 'S') {
        ++position;
    }
    if (position >= text.size()) {
        return std::nullopt;
    }
    unsigned radix { };
    switch (text[position]) {
    case 'b': case 'B': radix = 2U; break;
    case 'o': case 'O': radix = 8U; break;
    case 'd': case 'D': radix = 10U; break;
    case 'h': case 'H': radix = 16U; break;
    default: return std::nullopt;
    }
    ++position;
    std::uint64_t value { };
    bool digits { };
    for (; position < text.size(); ++position) {
        const auto character = text[position];
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
        if (digit >= radix
            || value > (std::numeric_limits<std::uint64_t>::max() - digit)
                    / radix) {
            return std::nullopt;
        }
        value = value * radix + digit;
        digits = true;
    }
    return digits ? std::optional { value } : std::nullopt;
}

std::vector<std::uint64_t> systemverilog_enumeration_values(
    const Hir& hir, const TypeDefinition& definition)
{
    std::vector<std::uint64_t> result;
    if (definition.form != TypeForm::enumeration) {
        return result;
    }
    std::uint64_t next { };
    for (const auto& literal : definition.enumeration_literals) {
        std::optional<std::uint64_t> value = next;
        if (literal.value) {
            const auto* expression
                = find_by_id(hir.expressions(), *literal.value);
            value = std::nullopt;
            if (expression != nullptr
                && expression->kind == ExpressionKind::logic_literal) {
                value = systemverilog_based_literal_bits(expression->text);
            } else if (expression != nullptr
                && expression->kind == ExpressionKind::integer_literal) {
                std::uint64_t decimal { };
                const auto& text = expression->text;
                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), decimal, 10);
                if (error == std::errc { }
                    && end == text.data() + text.size()) {
                    value = decimal;
                }
            }
        }
        if (!value) {
            return { };
        }
        result.push_back(*value);
        next = *value + 1U;
    }
    return result;
}

const TypeDefinition* systemverilog_type_definition(
    const Hir& hir, const TypeReference& type)
{
    const auto* definition = named_definition(hir, type);
    // An alias names the type it renames; an enumeration is its own type
    // even though its base names an integer type (6.19).
    for (std::size_t depth = 0; depth < 16U && definition != nullptr
         && definition->members.empty()
         && definition->form != TypeForm::enumeration
         && (definition->base.target.target.valid()
             || !definition->base.target.spelling.empty());
         ++depth) {
        const auto* next = named_definition(hir, definition->base);
        if (next == nullptr) {
            break;
        }
        definition = next;
    }
    return definition;
}

const TypeDefinition* systemverilog_structure_definition(
    const Hir& hir, const TypeReference& type)
{
    if (type.container_form) {
        return nullptr;
    }
    const auto* definition = systemverilog_type_definition(hir, type);
    return definition != nullptr && !definition->members.empty()
            && (definition->form == TypeForm::packed_structure
                || definition->form == TypeForm::unpacked_structure)
        ? definition
        : nullptr;
}

std::optional<std::size_t> systemverilog_structure_image_width(
    const Hir& hir, const TypeReference& type)
{
    if (type.container_form || !type.unpacked_dimensions.empty()
        || !type.class_identity.empty() || type.virtual_interface) {
        return std::nullopt;
    }
    if (const auto* definition = systemverilog_structure_definition(hir, type)) {
        std::size_t total { };
        for (const auto& member : definition->members) {
            const auto width
                = systemverilog_structure_image_width(hir, member.type);
            if (!width) {
                return std::nullopt;
            }
            total += *width;
        }
        return total;
    }
    if (const auto width = packed_width(type)) {
        return width;
    }
    const auto* definition = systemverilog_type_definition(hir, type);
    if (definition == nullptr || !definition->members.empty()) {
        return std::nullopt;
    }
    if (definition->form == TypeForm::enumeration) {
        // An enumeration's values have its base type's width; the default
        // base is int (6.19).
        return definition->base.target.spelling.empty()
                && !definition->base.executable_width
                && !definition->base.packed_range
            ? std::optional<std::size_t> { 32U }
            : packed_width(definition->base);
    }
    if (definition->form == TypeForm::packed_integral
        || definition->form == TypeForm::alias) {
        return packed_width(definition->base);
    }
    return std::nullopt;
}

bool systemverilog_boxed_aggregate(const Hir& hir, const TypeReference& type)
{
    const auto* definition = systemverilog_structure_definition(hir, type);
    return definition != nullptr
        && definition->form == TypeForm::unpacked_structure
        && !systemverilog_structure_image_width(hir, type);
}

} // namespace fsim::semantic::sv
