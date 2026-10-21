// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/dpi_c_type.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

namespace {

std::optional<std::uint64_t> dpi_decimal_token(
    const std::string_view text)
{
    std::uint64_t value { };
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return result.ec == std::errc { }
            && result.ptr == text.data() + text.size()
        ? std::optional<std::uint64_t> { value }
        : std::nullopt;
}

}  // namespace

std::optional<DpiCType> dpi_c_type(
    const std::span<const semantic::sv::SourceToken> tokens)
{
    DpiCType result;
    std::string_view base;
    bool explicit_signed { };
    bool explicit_unsigned { };
    std::size_t packed_width = 1U;
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
            if (index + 4U >= tokens.size()
                || tokens[index + 2U].text != ":"
                || tokens[index + 4U].text != "]") {
                return std::nullopt;
            }
            const auto left = dpi_decimal_token(tokens[index + 1U].text);
            const auto right = dpi_decimal_token(tokens[index + 3U].text);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto extent = *left >= *right
                ? *left - *right + 1U
                : *right - *left + 1U;
            if (extent == 0U
                || extent > std::numeric_limits<std::size_t>::max()
                || packed_width
                    > std::numeric_limits<std::size_t>::max()
                        / static_cast<std::size_t>(extent)) {
                return std::nullopt;
            }
            packed_width *= static_cast<std::size_t>(extent);
            index += 4U;
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
        return packed_width == 1U && !explicit_signed
                && !explicit_unsigned
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

// A formal's C parameter type (IEEE 1800-2023 H.7.4, H.8.7 to H.8.10,
// H.12): inputs pass small values by value and vectors by const pointer;
// outputs and inouts pass by pointer; open arrays pass a handle.
std::optional<std::string> dpi_c_parameter(
    const semantic::sv::DpiFormal& formal)
{
    const auto type = dpi_c_type(formal.type_tokens);
    if (!type) {
        return std::nullopt;
    }
    const bool input = formal.direction == semantic::sv::Direction::input;
    if (!input && formal.direction != semantic::sv::Direction::output
        && formal.direction != semantic::sv::Direction::inout) {
        return std::nullopt;
    }
    if (!formal.dimension_tokens.empty()) {
        for (std::size_t token = 0U; token < formal.dimension_tokens.size();
             token += 2U) {
            if (token + 1U >= formal.dimension_tokens.size()
                || formal.dimension_tokens[token].text != "["
                || formal.dimension_tokens[token + 1U].text != "]") {
                return std::nullopt;
            }
        }
        return std::string { input ? "const svOpenArrayHandle "
                                   : "svOpenArrayHandle " }
            + formal.name;
    }
    if (type->code == 'B' || type->code == 'V') {
        const std::string vector
            = type->code == 'B' ? "svBitVecVal* " : "svLogicVecVal* ";
        return (input ? "const " : "") + vector + formal.name;
    }
    auto scalar = dpi_c_scalar(type->code);
    if (scalar.empty()) {
        return std::nullopt;
    }
    if (input && type->code == 'p') {
        scalar = "const void*";
    }
    return scalar + (input ? " " : "* ") + formal.name;
}

std::optional<std::string> dpi_c_prototype(
    const semantic::sv::DpiDeclaration& declaration)
{
    std::string result;
    if (declaration.callable_kind == semantic::sv::DpiCallableKind::task) {
        // A task returns its disable status (IEEE 1800-2023 35.9).
        result = "int";
    } else if (declaration.return_type_tokens.size() == 1U
        && declaration.return_type_tokens.front().text == "void") {
        result = "void";
    } else {
        const auto type = dpi_c_type(declaration.return_type_tokens);
        if (!type || type->code == 'B' || type->code == 'V') {
            return std::nullopt;
        }
        result = dpi_c_scalar(type->code);
        if (result.empty()) {
            return std::nullopt;
        }
    }
    result += ' ' + declaration.linkage_name + '(';
    for (std::size_t index = 0U; index < declaration.formals.size();
         ++index) {
        const auto parameter = dpi_c_parameter(declaration.formals[index]);
        if (!parameter) {
            return std::nullopt;
        }
        result += (index == 0U ? "" : ", ") + *parameter;
    }
    if (declaration.formals.empty()) {
        result += "void";
    }
    return result + ')';
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
          "#include \"svdpi.h\"\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n";
    std::set<std::string, std::less<>> declared;
    for (const auto* const entry : declarations) {
        const auto& declaration = *entry;
        if (!declaration.validated || declaration.linkage_name.empty()
            || !declared.insert(declaration.linkage_name).second) {
            continue;
        }
        const bool import
            = declaration.direction == semantic::sv::DpiDirection::import;
        const auto prototype = dpi_c_prototype(declaration);
        if (!prototype) {
            result += "/* " + std::string { import ? "import" : "export" }
                + ' ' + declaration.linkage_name
                + ": no C prototype for its types */\n";
            continue;
        }
        result += "/* " + std::string { import ? "Imported" : "Exported" }
            + " " + (declaration.callable_kind
                    == semantic::sv::DpiCallableKind::task
                ? "task" : "function")
            + " " + declaration.systemverilog_name + " */\nextern "
            + *prototype + ";\n";
    }
    return result + "#ifdef __cplusplus\n}\n#endif\n#endif\n";
}

}  // namespace fsim::elaboration
