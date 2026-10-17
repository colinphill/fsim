// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// VHDL identifier spelling rules (IEEE 1076-2019 15.4). A basic identifier is
// case-insensitive; an extended identifier is written \...\ with a doubled
// backslash for each literal one, and is compared exactly. A "spelling" is
// source text (possibly \extended\); a "name" is a decoded identifier with a
// separate extended flag.
namespace fsim::support {

[[nodiscard]] constexpr char ascii_lower(const char character) noexcept
{
    return character >= 'A' && character <= 'Z'
        ? static_cast<char>(character - 'A' + 'a')
        : character;
}

[[nodiscard]] inline std::string ascii_lower(const std::string_view value)
{
    std::string result { value };
    for (auto& character : result) {
        character = ascii_lower(character);
    }
    return result;
}

[[nodiscard]] constexpr bool equal_ignoring_ascii_case(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index { }; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool is_extended_vhdl_identifier(
    const std::string_view spelling) noexcept
{
    return spelling.size() >= 2U && spelling.front() == '\\'
        && spelling.back() == '\\';
}

/// The identifier a spelling denotes: an extended identifier's body with
/// doubled backslashes collapsed, or a basic identifier folded to lower case.
[[nodiscard]] inline std::string decode_vhdl_identifier(
    std::string_view spelling)
{
    if (!is_extended_vhdl_identifier(spelling)) {
        return ascii_lower(spelling);
    }
    spelling.remove_prefix(1U);
    spelling.remove_suffix(1U);
    std::string result;
    result.reserve(spelling.size());
    for (std::size_t index { }; index < spelling.size(); ++index) {
        result.push_back(spelling[index]);
        if (spelling[index] == '\\' && index + 1U < spelling.size()
            && spelling[index + 1U] == '\\') {
            ++index;
        }
    }
    return result;
}

/// The spelling of a decoded name: extended names are quoted with each
/// backslash doubled; basic names are returned unchanged.
[[nodiscard]] inline std::string encode_vhdl_identifier(
    const std::string_view name, const bool extended)
{
    if (!extended) {
        return std::string { name };
    }
    std::string result { "\\" };
    result.reserve(name.size() + 2U);
    for (const auto character : name) {
        result.push_back(character);
        if (character == '\\') {
            result.push_back('\\');
        }
    }
    result.push_back('\\');
    return result;
}

/// Position of the first `wanted` at or after `from` that is not inside an
/// extended identifier, or npos.
[[nodiscard]] constexpr std::size_t find_outside_vhdl_extended_identifier(
    const std::string_view text, const char wanted,
    const std::size_t from = 0U) noexcept
{
    bool extended { };
    for (std::size_t index { }; index < text.size(); ++index) {
        if (text[index] == '\\') {
            if (extended && index + 1U < text.size()
                && text[index + 1U] == '\\') {
                ++index;
            } else {
                extended = !extended;
            }
        } else if (!extended && index >= from && text[index] == wanted) {
            return index;
        }
    }
    return std::string_view::npos;
}

/// Position of the last `wanted` that is not inside an extended identifier,
/// or npos.
[[nodiscard]] constexpr std::size_t rfind_outside_vhdl_extended_identifier(
    const std::string_view text, const char wanted) noexcept
{
    std::size_t found { std::string_view::npos };
    bool extended { };
    for (std::size_t index { }; index < text.size(); ++index) {
        if (text[index] == '\\') {
            if (extended && index + 1U < text.size()
                && text[index + 1U] == '\\') {
                ++index;
            } else {
                extended = !extended;
            }
        } else if (!extended && text[index] == wanted) {
            found = index;
        }
    }
    return found;
}

/// Calls `part(std::string_view)` for each dot-separated part of a selected
/// name, splitting only outside extended identifiers. Returns false for an
/// empty part or an unterminated extended identifier.
template <typename Part>
constexpr bool visit_vhdl_selected_name(
    const std::string_view spelling, Part&& part)
{
    std::size_t begin { };
    bool extended { };
    for (std::size_t index { }; index < spelling.size(); ++index) {
        if (spelling[index] == '\\') {
            if (extended && index + 1U < spelling.size()
                && spelling[index + 1U] == '\\') {
                ++index;
            } else {
                extended = !extended;
            }
        } else if (spelling[index] == '.' && !extended) {
            if (index == begin) {
                return false;
            }
            part(spelling.substr(begin, index - begin));
            begin = index + 1U;
        }
    }
    if (extended || begin == spelling.size()) {
        return false;
    }
    part(spelling.substr(begin));
    return true;
}

/// The parts of a selected name (see visit_vhdl_selected_name); empty when
/// the name is malformed.
[[nodiscard]] inline std::vector<std::string_view> split_vhdl_selected_name(
    const std::string_view spelling)
{
    std::vector<std::string_view> result;
    if (!visit_vhdl_selected_name(spelling,
            [&](const std::string_view part) { result.push_back(part); })) {
        result.clear();
    }
    return result;
}

/// The parts of a `[library.]unit[(architecture)]` target spelling, each
/// still spelled (not decoded).
struct VhdlTargetSpelling {
    std::string_view library;
    std::string_view unit;
    std::string_view architecture;
    bool has_library { };
    bool has_architecture { };
};

/// Splits a target at the separators outside extended identifiers. Fails
/// when an architecture is not closed by the final ')' or the name does not
/// have one or two parts.
[[nodiscard]] inline bool split_vhdl_target(
    std::string_view text, VhdlTargetSpelling& result)
{
    result = { };
    const auto open = rfind_outside_vhdl_extended_identifier(text, '(');
    if (open != std::string_view::npos) {
        if (text.back() != ')') {
            return false;
        }
        result.has_architecture = true;
        result.architecture
            = text.substr(open + 1U, text.size() - open - 2U);
        text = text.substr(0U, open);
    }
    const auto parts = split_vhdl_selected_name(text);
    if (parts.empty() || parts.size() > 2U) {
        return false;
    }
    if (parts.size() == 2U) {
        result.has_library = true;
        result.library = parts.front();
    }
    result.unit = parts.back();
    return true;
}

/// Whether two spellings denote the same identifier.
[[nodiscard]] constexpr bool same_vhdl_identifier(
    const std::string_view left, const std::string_view right) noexcept
{
    if (is_extended_vhdl_identifier(left)
        || is_extended_vhdl_identifier(right)) {
        return left == right;
    }
    return equal_ignoring_ascii_case(left, right);
}

/// Whether two decoded names, each with its extended flag, are the same.
[[nodiscard]] constexpr bool same_vhdl_identifier(
    const std::string_view left, const bool left_extended,
    const std::string_view right, const bool right_extended) noexcept
{
    return left_extended == right_extended
        && (left_extended ? left == right
                          : equal_ignoring_ascii_case(left, right));
}

/// Whether a decoded name (with its extended flag) is the one `spelling`
/// denotes.
[[nodiscard]] inline bool vhdl_name_matches_spelling(
    const std::string_view name, const bool extended,
    const std::string_view spelling)
{
    if (extended != is_extended_vhdl_identifier(spelling)) {
        return false;
    }
    return extended ? name == decode_vhdl_identifier(spelling)
                    : equal_ignoring_ascii_case(name, spelling);
}

/// The canonical form of a (possibly selected) spelling: basic identifier
/// text is folded to lower case, extended identifiers (\...\) are kept
/// exactly, and a character literal ('x') is returned unchanged.
[[nodiscard]] inline std::string canonical_vhdl_spelling(
    const std::string_view spelling)
{
    std::string value { spelling };
    if (!value.empty() && value.front() == '\'') {
        return value;
    }
    bool in_extended_identifier { };
    for (std::size_t index { }; index < value.size(); ++index) {
        if (value[index] == '\\') {
            if (in_extended_identifier && index + 1U < value.size()
                && value[index + 1U] == '\\') {
                ++index;
                continue;
            }
            in_extended_identifier = !in_extended_identifier;
        } else if (!in_extended_identifier) {
            value[index] = ascii_lower(value[index]);
        }
    }
    return value;
}

} // namespace fsim::support
