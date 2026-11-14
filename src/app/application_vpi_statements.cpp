// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_statements.hpp"

#include "application_vpi_operator_type.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsim::app::application_detail {

namespace {

// The inline constraint block of a `randomize() with {...}` call, which the
// HIR keeps as the call's association the lowering marks (18.7); null for
// any other expression.
[[nodiscard]] const auto* inline_constraint_of(const auto& record)
{
    const auto found = std::ranges::find(record.associations,
        std::string_view { "@sv-inline-constraint" },
        &std::remove_cvref_t<
            decltype(record.associations.front())>::choice_spelling);
    return found == record.associations.end() ? nullptr : &*found;
}

namespace sv = semantic::sv;
using runtime::Logic4;
using runtime::PackedLogic4;

// The statement list an implicit block holds: a process's, a callable's,
// a statement's then or else branch, or case item `aux - body_case`.
constexpr std::uint32_t body_process = 0U;
constexpr std::uint32_t body_callable = 1U;
constexpr std::uint32_t body_then = 2U;
constexpr std::uint32_t body_else = 3U;
constexpr std::uint32_t body_case = 4U;

// `x++`, `--x` and the like used as statements are operations (IEEE
// 1800-2023 37.78, 11.4.2); `x += e` stays an assignment (37.47).
[[nodiscard]] bool step_statement(const sv::Statement& record)
{
    return record.kind == sv::StatementKind::assignment
        && (record.update_kind == sv::UpdateKind::prefix
            || record.update_kind == sv::UpdateKind::postfix);
}

// A for-loop increment written `k++` or `--k` (37.78).
[[nodiscard]] bool step_update(const sv::Statement& record)
{
    return record.update_kind == sv::UpdateKind::prefix
        || record.update_kind == sv::UpdateKind::postfix;
}

[[nodiscard]] PLI_INT32 step_operator(const sv::Statement& record)
{
    const bool increment = record.update_operator == "+";
    return record.update_kind == sv::UpdateKind::prefix
        ? (increment ? vpiPreIncOp : vpiPreDecOp)
        : (increment ? vpiPostIncOp : vpiPostDecOp);
}

// The leading word of a type's spelling: "logic [3:0]" gives "logic".
[[nodiscard]] std::string type_word(const sv::TypeReference& type)
{
    const auto& text = type.target.spelling;
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return { };
    }
    auto end = begin;
    while (end < text.size()
        && (std::isalnum(static_cast<unsigned char>(text[end])) != 0
            || text[end] == '_')) {
        ++end;
    }
    return text.substr(begin, end - begin);
}

[[nodiscard]] std::optional<std::int64_t> range_extent(
    const sv::PackedRange& range)
{
    if (!range.left || !range.right) {
        return std::nullopt;
    }
    return (*range.left >= *range.right ? *range.left - *range.right
                                        : *range.right - *range.left)
        + 1;
}

// The size of a callable's formal or return type, when it follows from the
// declaration (IEEE 1800-2023 37.41 detail 12); a void type has none.
[[nodiscard]] std::optional<std::int64_t> type_width(
    const sv::TypeReference& type)
{
    const auto word = type_word(type);
    if (word == "void") {
        return 0;
    }
    if (word == "int" || word == "integer" || word == "shortreal") {
        return 32;
    }
    if (word == "byte") {
        return 8;
    }
    if (word == "shortint") {
        return 16;
    }
    if (word == "longint" || word == "time" || word == "real"
        || word == "realtime") {
        return 64;
    }
    if (word.empty() || word == "bit" || word == "logic" || word == "reg") {
        if (type.container_form || !type.unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        std::int64_t size = 1;
        if (!type.packed_dimensions.empty()) {
            for (const auto& range : type.packed_dimensions) {
                const auto extent = range_extent(range);
                if (!extent) {
                    return std::nullopt;
                }
                size *= *extent;
            }
        } else if (type.packed_range) {
            const auto extent = range_extent(*type.packed_range);
            if (!extent) {
                return std::nullopt;
            }
            size = *extent;
        }
        return size;
    }
    if (type.executable_width) {
        return static_cast<std::int64_t>(*type.executable_width);
    }
    return std::nullopt;
}

// The frontend records a built-in type's signedness, its default (6.11.3)
// or the `signed`/`unsigned` written after it.
[[nodiscard]] bool type_signed(const sv::TypeReference& type)
{
    return type.signed_value;
}

// A built-in type with its packed dimensions (IEEE 1800-2023 37.41); none
// for a typedef, class or container type, or an unresolved bound.
[[nodiscard]] std::optional<VpiStatementModel::DeclaredType> declared_type_of(
    const sv::TypeReference& type)
{
    // A virtual interface type has the interface typespec of the interface,
    // or of its modport (25.9, 37.30).
    if (type.virtual_interface && !type.container_form
        && type.unpacked_dimensions.empty()) {
        VpiStatementModel::DeclaredType named;
        named.interface_name = type.interface_type;
        named.modport_name = type.interface_modport;
        return named;
    }
    auto word = type_word(type);
    if (word == "reg" || word.empty()) {
        word = "logic";
    }
    static const std::set<std::string, std::less<>> built_in { "logic", "bit",
        "byte", "shortint", "int", "longint", "integer", "time", "real",
        "realtime", "shortreal", "string" };
    if (!built_in.contains(word) && word != "void" && !type.container_form
        && type.unpacked_dimensions.empty() && type.packed_dimensions.empty()
        && !type.packed_range && type.class_identity.empty()
        && !type.virtual_interface && !type.type_of
        && type.target.spelling.find('#') == std::string::npos) {
        // A type named by a typedef (or enum or struct typedef): VPI
        // reaches the typedef's typespec (37.41); the elaborator resolves
        // the name where the callable is declared (26.3).
        VpiStatementModel::DeclaredType named;
        named.type_name = type.target.spelling;
        return named;
    }
    // A class handle: the class model gives its typespec (37.41, 37.31).
    if (!type.container_form && type.unpacked_dimensions.empty()
        && !type.class_identity.empty() && !type.virtual_interface) {
        VpiStatementModel::DeclaredType named;
        named.class_name = type.class_specialization.empty()
            ? type.class_identity
            : type.class_specialization;
        return named;
    }
    // A container or fixed-size unpacked array: an array typespec of its
    // element type (37.25 detail 2).
    const bool dynamic = type.container_form == sv::TypeForm::dynamic_array;
    const bool queue = type.container_form == sv::TypeForm::queue;
    const bool associative
        = type.container_form == sv::TypeForm::associative_array;
    const bool fixed = type.container_form == sv::TypeForm::static_array
        || (!type.container_form && !type.unpacked_dimensions.empty());
    if (dynamic || queue || associative || fixed) {
        VpiStatementModel::DeclaredType array;
        array.array_kind = dynamic ? 2 : queue ? 3 : associative ? 4 : 1;
        sv::TypeReference base = type;
        base.container_form.reset();
        base.unpacked_dimensions.clear();
        base.associative_index.reset();
        if (array.array_kind == 1) {
            for (const auto& range : type.unpacked_dimensions) {
                if (!range.left || !range.right) {
                    return std::nullopt;
                }
                array.dimensions.emplace_back(*range.left, *range.right);
            }
        } else if (!type.container_element_types.empty()) {
            base = type.container_element_types.front();
        }
        if (associative && type.associative_index) {
            array.index_word = type.associative_index->spelling;
        }
        const auto element = declared_type_of(base);
        if (!element) {
            return std::nullopt;
        }
        array.element.push_back(*element);
        return array;
    }
    if (!built_in.contains(word) || type.container_form
        || !type.unpacked_dimensions.empty()) {
        return std::nullopt;
    }
    VpiStatementModel::DeclaredType result;
    result.word = word;
    result.is_signed = type_signed(type);
    const auto add = [&](const sv::PackedRange& range) {
        if (!range.left || !range.right) {
            return false;
        }
        result.ranges.emplace_back(*range.left, *range.right);
        return true;
    };
    if (!type.packed_dimensions.empty()) {
        for (const auto& range : type.packed_dimensions) {
            if (!add(range)) {
                return std::nullopt;
            }
        }
    } else if (type.packed_range && !add(*type.packed_range)) {
        return std::nullopt;
    }
    // An integer atom (`int`, `byte`, ...) carries its implicit packed range
    // in the HIR; its typespec has no dimensions (37.25).
    if (!result.ranges.empty() && word != "logic" && word != "bit") {
        result.ranges.clear();
    }
    return result;
}

// The type a DPI import spells in its declaration, for an import that has no
// resolved profile: a built-in word with `[left:right]` packed ranges, none
// for a named type, an unpacked formal or a non-literal bound (37.41).
[[nodiscard]] std::optional<VpiStatementModel::DeclaredType>
declared_type_of_tokens(const std::vector<sv::SourceToken>& tokens,
    const std::vector<sv::SourceToken>& dimension_tokens)
{
    if (!dimension_tokens.empty()) {
        return std::nullopt;
    }
    VpiStatementModel::DeclaredType result;
    std::optional<bool> sign;
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        const auto& text = tokens[index].text;
        if (text == "signed" || text == "unsigned") {
            sign = text == "signed";
        } else if (text == "const" || text == "var") {
            continue;
        } else if (text == "[") {
            if (index + 4U >= tokens.size() || tokens[index + 2U].text != ":"
                || tokens[index + 4U].text != "]") {
                return std::nullopt;
            }
            try {
                result.ranges.emplace_back(std::stoll(tokens[index + 1U].text),
                    std::stoll(tokens[index + 3U].text));
            } catch (const std::exception&) {
                return std::nullopt;
            }
            index += 4U;
        } else if (result.word.empty()) {
            result.word = text;
        } else {
            return std::nullopt;
        }
    }
    if (result.word.empty() || result.word == "reg") {
        result.word = "logic";
    }
    static const std::set<std::string, std::less<>> built_in { "logic", "bit",
        "byte", "shortint", "int", "longint", "integer", "time", "real",
        "realtime", "shortreal", "string" };
    if (!built_in.contains(result.word)
        || (!result.ranges.empty() && result.word != "logic"
            && result.word != "bit")) {
        return std::nullopt;
    }
    const auto& word = result.word;
    result.is_signed = sign.value_or(word == "int" || word == "integer"
        || word == "byte" || word == "shortint" || word == "longint");
    return result;
}

// The vpiFuncType of a function returning `type` (IEEE 1800-2023 37.41).
[[nodiscard]] PLI_INT32 function_type(const sv::TypeReference& type)
{
    const auto word = type_word(type);
    if (word == "int" || word == "integer") {
        return vpiIntFunc;
    }
    if (word == "real" || word == "realtime" || word == "shortreal") {
        return vpiRealFunc;
    }
    if (word == "time") {
        return vpiTimeFunc;
    }
    if (word.empty() || word == "byte" || word == "shortint"
        || word == "longint" || word == "bit" || word == "logic"
        || word == "reg") {
        return type_signed(type) ? vpiSizedSignedFunc : vpiSizedFunc;
    }
    return vpiOtherFunc;
}

// The vpiType of the variable that holds a value of `type` (IEEE 1800-2023
// 37.17).
[[nodiscard]] PLI_INT32 variable_type(const sv::TypeReference& type)
{
    if (type.virtual_interface && !type.container_form
        && type.unpacked_dimensions.empty()) {
        return vpiVirtualInterfaceVar;
    }
    const auto word = type_word(type);
    if (word == "int") {
        return vpiIntVar;
    }
    if (word == "integer") {
        return vpiIntegerVar;
    }
    if (word == "byte") {
        return vpiByteVar;
    }
    if (word == "shortint") {
        return vpiShortIntVar;
    }
    if (word == "longint") {
        return vpiLongIntVar;
    }
    if (word == "time") {
        return vpiTimeVar;
    }
    if (word == "real" || word == "realtime") {
        return vpiRealVar;
    }
    if (word == "shortreal") {
        return vpiShortRealVar;
    }
    if (type.value_form == sv::TypeForm::string || word == "string") {
        return vpiStringVar;
    }
    return word == "bit" ? vpiBitVar : vpiReg;
}

// The vpiType of a local variable of a property or sequence declaration, from
// the first word of its declared type (IEEE 1800-2023 16.8, 37.51 detail 2).
[[nodiscard]] PLI_INT32 assertion_variable_type(
    const std::vector<sv::SourceToken>& tokens)
{
    const std::string_view word = tokens.empty()
        ? std::string_view { }
        : std::string_view { tokens.front().text };
    if (word == "int") {
        return vpiIntVar;
    }
    if (word == "integer") {
        return vpiIntegerVar;
    }
    if (word == "byte") {
        return vpiByteVar;
    }
    if (word == "shortint") {
        return vpiShortIntVar;
    }
    if (word == "longint") {
        return vpiLongIntVar;
    }
    if (word == "time") {
        return vpiTimeVar;
    }
    if (word == "real" || word == "realtime") {
        return vpiRealVar;
    }
    if (word == "shortreal") {
        return vpiShortRealVar;
    }
    if (word == "string") {
        return vpiStringVar;
    }
    return word == "bit" ? vpiBitVar : vpiReg;
}

[[nodiscard]] Logic4 digit_logic(const char digit) noexcept
{
    switch (digit) {
    case 'x':
    case 'X':
        return Logic4::x;
    case 'z':
    case 'Z':
    case '?':
        return Logic4::z;
    default:
        return digit == '1' ? Logic4::one : Logic4::zero;
    }
}

// An integer literal's value (IEEE 1800-2023 5.7.1): sized and unsized,
// based and decimal, with x and z digits; an unsized one is 32 bits.
[[nodiscard]] std::optional<VpiStatementValue> integer_value(
    std::string_view text)
{
    std::string digits;
    for (const auto character : text) {
        if (character != '_' && character != ' ') {
            digits.push_back(character);
        }
    }
    VpiStatementValue result;
    const auto quote = digits.find('\'');
    if (quote == std::string::npos) {
        // A folded negation of a constant keeps its sign in the text.
        const bool negative = digits.starts_with('-');
        const auto* const begin = digits.data() + (negative ? 1 : 0);
        std::uint64_t value { };
        const auto parsed
            = std::from_chars(begin, digits.data() + digits.size(), value);
        if (parsed.ec != std::errc { } || parsed.ptr != digits.data() + digits.size()) {
            return std::nullopt;
        }
        result.is_signed = true;
        const auto width
            = value > (negative ? 0x8000'0000U : 0xFFFF'FFFFU) ? 64U : 32U;
        if (negative) {
            value = ~value + 1U;
        }
        result.bits = PackedLogic4(width, Logic4::zero);
        for (std::size_t bit = 0U; bit < width; ++bit) {
            result.bits.set(bit, ((value >> bit) & 1U) != 0U ? Logic4::one
                                                             : Logic4::zero);
        }
        return result;
    }
    std::size_t width = 32U;
    bool sized = false;
    if (quote != 0U) {
        std::uint64_t size { };
        const auto parsed = std::from_chars(digits.data(),
            digits.data() + quote, size);
        if (parsed.ec != std::errc { } || size == 0U || size > 65536U) {
            return std::nullopt;
        }
        width = static_cast<std::size_t>(size);
        sized = true;
    }
    auto rest = std::string_view { digits }.substr(quote + 1U);
    if (!rest.empty() && (rest.front() == 's' || rest.front() == 'S')) {
        result.is_signed = true;
        rest.remove_prefix(1U);
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    const auto base = static_cast<char>(
        std::tolower(static_cast<unsigned char>(rest.front())));
    // An unbased unsized literal fills its context (5.7.1); alone it is
    // one bit.
    if (base == '0' || base == '1' || base == 'x' || base == 'z') {
        result.bits = PackedLogic4(1U, digit_logic(base));
        return result;
    }
    rest.remove_prefix(1U);
    std::vector<Logic4> bits; // least significant first
    if (base == 'd') {
        if (rest.size() == 1U && digit_logic(rest.front()) != Logic4::zero
            && rest.front() != '0' && rest.front() != '1'
            && !std::isdigit(static_cast<unsigned char>(rest.front()))) {
            bits.assign(width, digit_logic(rest.front()));
        } else {
            std::uint64_t value { };
            const auto parsed = std::from_chars(
                rest.data(), rest.data() + rest.size(), value);
            if (parsed.ec != std::errc { }) {
                return std::nullopt;
            }
            for (std::size_t bit = 0U; bit < 64U; ++bit) {
                bits.push_back(((value >> bit) & 1U) != 0U ? Logic4::one
                                                           : Logic4::zero);
            }
        }
    } else {
        const auto per_digit = base == 'b' ? 1U
            : base == 'o'                 ? 3U
            : base == 'h'                 ? 4U
                                          : 0U;
        if (per_digit == 0U) {
            return std::nullopt;
        }
        for (auto digit = rest.rbegin(); digit != rest.rend(); ++digit) {
            const auto logic = digit_logic(*digit);
            if (logic == Logic4::x || logic == Logic4::z) {
                bits.insert(bits.end(), per_digit, logic);
                continue;
            }
            unsigned value { };
            const auto lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(*digit)));
            if (lower >= '0' && lower <= '9') {
                value = static_cast<unsigned>(lower - '0');
            } else if (lower >= 'a' && lower <= 'f') {
                value = static_cast<unsigned>(lower - 'a' + 10);
            } else {
                return std::nullopt;
            }
            for (unsigned bit = 0U; bit < per_digit; ++bit) {
                bits.push_back(((value >> bit) & 1U) != 0U ? Logic4::one
                                                           : Logic4::zero);
            }
        }
    }
    if (!sized && bits.size() > width) {
        width = bits.size();
    }
    // A leftmost x or z digit extends; other values zero-extend.
    const auto fill = !bits.empty()
            && (bits.back() == Logic4::x || bits.back() == Logic4::z)
        ? bits.back()
        : Logic4::zero;
    result.bits = PackedLogic4(width, fill);
    for (std::size_t bit = 0U; bit < width && bit < bits.size(); ++bit) {
        result.bits.set(bit, bits[bit]);
    }
    return result;
}

[[nodiscard]] std::string_view base_name(std::string_view name)
{
    const auto bracket = name.find('[');
    return bracket == std::string_view::npos ? name : name.substr(0U, bracket);
}

// The delay of a delay list at `index`: the first, then each additional
// one in source order (IEEE 1800-2023 37.47).
const sv::DelayValue* delay_value(
    const sv::Delay& delay, const std::uint32_t index) noexcept
{
    if (index == 0U) {
        return &delay.primary;
    }
    return index <= delay.additional.size()
        ? &delay.additional[index - 1U].primary
        : nullptr;
}

// The size, signedness and variable type of a DPI import's formal or result,
// read from the tokens of its type (IEEE 1800-2023 35.5.5, 37.41): the small
// types of the DPI, with a packed range multiplying a `bit` or `logic`
// element. Types that have no fixed size here (string, chandle, a named
// type) have none.
struct DpiTokenShape {
    std::optional<std::int64_t> size;
    bool is_signed { };
    PLI_INT32 variable { vpiReg };
};

[[nodiscard]] DpiTokenShape dpi_token_shape(
    const std::vector<sv::SourceToken>& tokens)
{
    DpiTokenShape shape;
    std::optional<bool> sign;
    std::string_view word;
    std::int64_t elements { 1 };
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        const auto& text = tokens[index].text;
        if (text == "signed" || text == "unsigned") {
            sign = text == "signed";
        } else if (text == "const" || text == "var") {
            continue;
        } else if (text == "[") {
            // `[ left : right ]` with integer literals.
            if (index + 4U < tokens.size() && tokens[index + 2U].text == ":"
                && tokens[index + 4U].text == "]") {
                try {
                    const auto left = std::stoll(tokens[index + 1U].text);
                    const auto right = std::stoll(tokens[index + 3U].text);
                    elements = (left >= right ? left - right : right - left) + 1;
                } catch (const std::exception&) {
                    elements = 0;
                }
                index += 4U;
            } else {
                elements = 0;
            }
        } else if (word.empty()) {
            word = text;
        }
    }
    const auto fixed = [&](const std::int64_t size, const bool signed_value,
                           const PLI_INT32 variable) {
        shape.size = size;
        shape.is_signed = sign.value_or(signed_value);
        shape.variable = variable;
    };
    if (word == "int") {
        fixed(32, true, vpiIntVar);
    } else if (word == "integer") {
        fixed(32, true, vpiIntegerVar);
    } else if (word == "byte") {
        fixed(8, true, vpiByteVar);
    } else if (word == "shortint") {
        fixed(16, true, vpiShortIntVar);
    } else if (word == "longint") {
        fixed(64, true, vpiLongIntVar);
    } else if (word == "time") {
        fixed(64, false, vpiTimeVar);
    } else if (word == "bit" || word == "logic" || word == "reg") {
        if (elements > 0) {
            fixed(elements, false, word == "bit" ? vpiBitVar : vpiReg);
        }
    } else if (word == "real" || word == "realtime") {
        fixed(64, true, vpiRealVar);
    } else if (word == "shortreal") {
        fixed(32, true, vpiShortRealVar);
    } else if (word == "string") {
        shape.variable = vpiStringVar;
    } else if (word == "chandle") {
        shape.variable = vpiChandleVar;
    }
    return shape;
}

// The precedence of an operator (IEEE 1800-2023 11.3.2, Table 11-2): a
// larger number binds tighter; primaries are 100.
[[nodiscard]] int operator_precedence(
    const std::string_view text, const std::size_t operands)
{
    if (operands == 1U) {
        return 90;
    }
    if (text == "?:") {
        return 20;
    }
    if (text == "->" || text == "<->") {
        return 10;
    }
    static constexpr std::pair<std::string_view, int> binary[] = {
        { "**", 80 }, { "*", 70 }, { "/", 70 }, { "%", 70 }, { "+", 60 },
        { "-", 60 }, { "<<", 55 }, { ">>", 55 }, { "<<<", 55 },
        { ">>>", 55 }, { "<", 50 }, { "<=", 50 }, { ">", 50 }, { ">=", 50 },
        { "inside", 50 }, { "dist", 50 }, { "==", 45 }, { "!=", 45 },
        { "===", 45 }, { "!==", 45 }, { "==?", 45 }, { "!=?", 45 },
        { "&", 40 }, { "^", 38 }, { "~^", 38 }, { "^~", 38 }, { "|", 36 },
        { "&&", 32 }, { "||", 30 },
    };
    for (const auto& [spelling, level] : binary) {
        if (text == spelling) {
            return level;
        }
    }
    return 0;
}


// Restores the let frame a relation or iteration set (IEEE 1800-2023
// 37.57).
struct FrameRestore {
    std::uint32_t& slot;
    std::uint32_t saved;
    ~FrameRestore() { slot = saved; }
};

} // namespace

PLI_INT32 vpi_hir_variable_type(const sv::TypeReference& type)
{
    return variable_type(type);
}

VpiDpiResult vpi_dpi_result(const sv::DpiDeclaration& declaration)
{
    VpiDpiResult result;
    if (declaration.callable_kind != sv::DpiCallableKind::function) {
        return result;
    }
    // An import's result is one of the small types of IEEE 1800-2023
    // 35.5.5, spelled by its leading keyword; only an export resolves to a
    // native callable's declared type.
    if (!declaration.resolved_profile
        || !declaration.resolved_profile->return_type) {
        std::optional<bool> sign;
        std::string_view word;
        for (const auto& token : declaration.return_type_tokens) {
            if (token.text == "signed" || token.text == "unsigned") {
                sign = token.text == "signed";
            } else if (word.empty()) {
                word = token.text;
            }
        }
        const auto sized = [&](const std::int64_t size, const bool signed_value) {
            result.size = size;
            result.is_signed = sign.value_or(signed_value);
            result.func_type = word == "int" ? vpiIntFunc
                : result.is_signed            ? vpiSizedSignedFunc
                                              : vpiSizedFunc;
        };
        if (word == "int") {
            sized(32, true);
        } else if (word == "byte") {
            sized(8, true);
        } else if (word == "shortint") {
            sized(16, true);
        } else if (word == "longint") {
            sized(64, true);
        } else if (word == "bit" || word == "logic") {
            sized(1, false);
        } else if (word == "real" || word == "shortreal") {
            result.func_type = vpiRealFunc;
            result.size = word == "real" ? 64 : 32;
            result.is_signed = true;
        }
        return result;
    }
    const auto& type = *declaration.resolved_profile->return_type;
    result.func_type = function_type(type);
    result.size = type_width(type).value_or(0);
    result.is_signed = type_signed(type);
    return result;
}

VpiStatementModel::VpiStatementModel(const semantic::sv::Hir& hir,
    const semantic::Model& semantics, Hooks hooks)
    : hir_ { hir }
    , semantics_ { semantics }
    , hooks_ { std::move(hooks) }
{
    for (const auto& record : hir_.statements()) {
        statements_.emplace(record.id.value(), &record);
    }
    for (const auto& record : hir_.processes()) {
        processes_.emplace(record.id.value(), &record);
    }
    // A net declaration assignment is its own continuous assignment whose
    // delay is the declaration's (IEEE 1800-2023 10.3.3, 37.46); simulation
    // applies that delay to the net, so only this model sees it as the
    // assignment's.
    std::map<std::pair<std::uint32_t, std::string>, const sv::Delay*> delays;
    for (const auto& declaration : hir_.declarations()) {
        if (declaration.form == sv::DeclarationForm::net && declaration.delay
            && declaration.scope.valid()) {
            delays.emplace(std::pair { declaration.scope.value(),
                               declaration.name },
                &*declaration.delay);
        }
    }
    if (delays.empty()) {
        return;
    }
    for (const auto& record : hir_.statements()) {
        if (record.kind != sv::StatementKind::assignment
            || record.assignment_kind != sv::AssignmentKind::continuous
            || !record.gate_primitive.empty() || record.delay
            || !record.target || !declared_net_assignment(record)) {
            continue;
        }
        const auto* const target = expression(record.target->value());
        if (target == nullptr || !target->scope.valid()) {
            continue;
        }
        const auto found = delays.find(
            std::pair { target->scope.value(), target->text });
        if (found == delays.end()) {
            continue;
        }
        auto& copy = declaration_assignments_.emplace_back(record);
        copy.delay = *found->second;
        statements_[record.id.value()] = &copy;
    }
}

const sv::Statement* VpiStatementModel::statement(const std::uint32_t id) const
{
    const auto found = statements_.find(id);
    return found == statements_.end() ? nullptr : found->second;
}

const sv::Process* VpiStatementModel::process(const std::uint32_t id) const
{
    const auto found = processes_.find(id);
    return found == processes_.end() ? nullptr : found->second;
}

const sv::ModulePathDeclaration* VpiStatementModel::module_path_declaration(
    const std::uint32_t unit_id, std::size_t ordinal) const
{
    const auto unit = std::ranges::find(hir_.units(),
        semantic::UnitId::from_index(unit_id), &sv::Unit::id);
    if (unit == hir_.units().end()) {
        return nullptr;
    }
    for (const auto& block : unit->timing) {
        if (ordinal < block.module_paths.size()) {
            return &block.module_paths[ordinal];
        }
        ordinal -= block.module_paths.size();
    }
    return nullptr;
}

const sv::TimingCheckDeclaration* VpiStatementModel::timing_check_declaration(
    const std::uint32_t unit_id, std::size_t ordinal) const
{
    const auto unit = std::ranges::find(hir_.units(),
        semantic::UnitId::from_index(unit_id), &sv::Unit::id);
    if (unit == hir_.units().end()) {
        return nullptr;
    }
    for (const auto& block : unit->timing) {
        if (ordinal < block.timing_checks.size()) {
            return &block.timing_checks[ordinal];
        }
        ordinal -= block.timing_checks.size();
    }
    return nullptr;
}

const sv::Expression* VpiStatementModel::expression(
    const std::uint32_t id) const
{
    return hir_.find_expression(semantic::ExpressionId::from_index(id));
}

fsim_vpi_handle_v1 VpiStatementModel::node(const Kind kind,
    const std::uint32_t id, const std::uint32_t aux,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent,
    const bool of_process)
{
    const auto key = std::tuple { kind, id, aux, scope, of_process };
    const auto [found, created] = index_.try_emplace(key, nodes_.size());
    if (created) {
        nodes_.push_back(Node { kind, id, aux, scope, parent, of_process });
        serial_of_.emplace(hooks_.handle(found->second), found->second);
    }
    return hooks_.handle(found->second);
}

fsim_vpi_handle_v1 VpiStatementModel::connection_list(
    std::vector<fsim_vpi_handle_v1> operands, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 array)
{
    auto [found, created] = connection_list_of_array_.try_emplace(
        array, static_cast<std::uint32_t>(connection_lists_.size()));
    if (created) {
        connection_lists_.push_back(std::move(operands));
    }
    return node(Kind::connection_list, found->second, 0U, scope, array);
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_event_handle(
    const std::uint32_t list, const std::uint32_t position,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent)
{
    const auto& events = *assertion_events_.at(list);
    const auto& item = events.at(position);
    if (item.edge != sv::EdgeKind::any) {
        return node(Kind::assertion_event, list, position, scope, parent);
    }
    if (item.expression) {
        return expression_handle(*item.expression, scope, parent);
    }
    return hooks_.resolve(scope, item.signal);
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_clocking_event(
    const std::vector<sv::Sensitivity>& events, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    if (events.empty()) {
        return 0U;
    }
    const auto [found, created] = assertion_event_index_.try_emplace(
        &events, static_cast<std::uint32_t>(assertion_events_.size()));
    if (created) {
        assertion_events_.push_back(&events);
    }
    if (events.size() == 1U) {
        return assertion_event_handle(found->second, 0U, scope, parent);
    }
    return node(Kind::assertion_event_list, found->second, 0U, scope, parent);
}

fsim_vpi_handle_v1 VpiStatementModel::operation(const PLI_INT32 op,
    std::vector<fsim_vpi_handle_v1> operands, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent, const std::uint32_t discriminator)
{
    const auto [found, created] = operation_index_.try_emplace(
        std::tuple { parent, op, discriminator },
        static_cast<std::uint32_t>(connection_lists_.size()));
    if (created) {
        connection_lists_.push_back(std::move(operands));
    }
    return node(Kind::synthetic_operation, found->second,
        static_cast<std::uint32_t>(op), scope, parent);
}

fsim_vpi_handle_v1 VpiStatementModel::specify_delays(
    const std::vector<sv::Delay>& delays, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    if (delays.empty()) {
        return 0U;
    }
    const auto [found, created] = specify_delay_index_.try_emplace(
        &delays, static_cast<std::uint32_t>(specify_delays_.size()));
    if (created) {
        specify_delays_.push_back(&delays);
    }
    if (delays.size() == 1U) {
        return specify_delay_handle(found->second, 0U, scope, parent);
    }
    return node(Kind::specify_delay_list, found->second, 0U, scope, parent);
}

// A delay is its value, or a vpiMinTypMaxOp operation over the three of a
// minimum:typical:maximum triple (IEEE 1800-2023 37.39, 37.80).
fsim_vpi_handle_v1 VpiStatementModel::specify_delay_handle(
    const std::uint32_t set, const std::uint32_t position,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent)
{
    const auto& delay = specify_delays_.at(set)->at(position);
    if (delay.minimum || delay.typical || delay.maximum) {
        return node(Kind::specify_mtm_delay, set, position, scope, parent);
    }
    return specify_value_handle(set, position, 0U, scope, parent);
}

fsim_vpi_handle_v1 VpiStatementModel::specify_value_handle(
    const std::uint32_t set, const std::uint32_t position,
    const std::uint32_t which, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    const auto& delay = specify_delays_.at(set)->at(position);
    const sv::DelayValue* chosen = &delay.primary;
    if (which == 1U) {
        chosen = delay.minimum ? &*delay.minimum : nullptr;
    } else if (which == 2U) {
        chosen = delay.typical ? &*delay.typical : nullptr;
    } else if (which == 3U) {
        chosen = delay.maximum ? &*delay.maximum : nullptr;
    }
    if (!chosen) {
        return 0U;
    }
    if (chosen->expression) {
        return expression_handle(*chosen->expression, scope, parent);
    }
    return node(Kind::specify_delay_value, set, position * 4U + which, scope,
        parent);
}

const sv::DelayValue* VpiStatementModel::specify_value(const Node& entry) const
{
    if (entry.kind != Kind::specify_delay_value
        || entry.id >= specify_delays_.size()) {
        return nullptr;
    }
    const auto& delays = *specify_delays_[entry.id];
    const auto position = entry.aux / 4U;
    if (position >= delays.size()) {
        return nullptr;
    }
    const auto& delay = delays[position];
    switch (entry.aux % 4U) {
    case 1U:
        return delay.minimum ? &*delay.minimum : nullptr;
    case 2U:
        return delay.typical ? &*delay.typical : nullptr;
    case 3U:
        return delay.maximum ? &*delay.maximum : nullptr;
    default:
        return &delay.primary;
    }
}

// A named block is its scope object (IEEE 1800-2023 37.43); other
// statements are nodes.
fsim_vpi_handle_v1 VpiStatementModel::statement_handle(
    const semantic::StatementId id, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    const auto* const record = statement(id.value());
    if (record == nullptr) {
        return 0U;
    }
    if ((record->kind == sv::StatementKind::block
            || record->kind == sv::StatementKind::fork)
        && !record->label.empty()) {
        if (const auto named = hooks_.child_scope(scope, record->label)) {
            return named;
        }
    }
    return node(Kind::statement, id.value(), 0U, scope, parent);
}

// A name is the object it refers to (IEEE 1800-2023 37.78); one that
// refers to nothing published is a reference node.
fsim_vpi_handle_v1 VpiStatementModel::expression_handle(
    const semantic::ExpressionId id, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    const auto* const record = expression(id.value());
    if (record == nullptr) {
        return 0U;
    }
    // A formal of the let whose body this is (IEEE 1800-2023 11.12): in a
    // let expression the argument bound to it, seen where the let
    // expression is; in the declaration a ref obj of the formal (37.57).
    if (frame_ != 0U && record->kind == sv::ExpressionKind::name) {
        const auto frame = let_frames_[frame_];
        const auto& ports = frame.declaration->ports;
        const auto port = std::ranges::find(
            ports, record->text, &sv::LetPort::name);
        if (port != ports.end()) {
            if (!frame.use) {
                return node(Kind::reference, id.value(), frame_, scope, parent);
            }
            const auto argument = let_argument(*frame.declaration, *frame.use,
                static_cast<std::size_t>(port - ports.begin()));
            if (!argument) {
                return 0U;
            }
            const auto saved = std::exchange(frame_, frame.outer);
            const auto handle = expression_handle(*argument, frame.scope, parent);
            frame_ = saved;
            return handle;
        }
    }
    // A call or name that expands a let declaration is a let expression
    // (11.12, 37.57).
    if ((record->kind == sv::ExpressionKind::call
            || record->kind == sv::ExpressionKind::name)
        && hooks_.let_body) {
        if (const auto body = hooks_.let_body(scope, id);
            body && let_of_body(*body) != nullptr) {
            return node(Kind::let_expr, id.value(), frame_, scope, parent);
        }
    }
    if (record->kind == sv::ExpressionKind::name) {
        const auto& name = record->referenced_name
            ? record->referenced_name->spelling
            : record->text;
        // A name the link step resolved to a declaration of a package
        // (`pkg::pv`, IEEE 1800-2023 26.3) is the object of that declaration
        // (37.10).
        if (record->referenced_name && record->referenced_name->selected
            && hooks_.declared) {
            if (const auto object = hooks_.declared(
                    *record->referenced_name->selected)) {
                return hooks_.reference ? hooks_.reference(object) : object;
            }
        }
        // A name of a variable a for statement declares is that variable,
        // whose scope is the loop the name is in (12.7.1, 37.12 detail 2).
        if (record->referenced_name && record->referenced_name->selected) {
            const auto selected = *record->referenced_name->selected;
            for (auto above = parent; above != 0U;) {
                const auto found = serial_of_.find(above);
                if (found == serial_of_.end()) {
                    break;
                }
                const auto owner = nodes_[found->second];
                if (owner.kind == Kind::statement) {
                    const auto* const loop = statement(owner.id);
                    if (loop != nullptr && loop->loop_variable_declared
                        && statement_type(*loop) == vpiFor
                        && std::ranges::find(loop->declarations, selected)
                            != loop->declarations.end()) {
                        return node(Kind::local_variable, selected.value(),
                            owner.id, owner.scope, above);
                    }
                }
                above = owner.parent;
            }
        }
        // A formal or local of the class method the name is in (8.6): in a
        // frame of a call of the method, that call's automatic variable
        // (37.43), else the declared variable (37.29 example 2).
        if (record->referenced_name && record->referenced_name->selected
            && hooks_.method) {
            const auto selected = *record->referenced_name->selected;
            const auto method = hooks_.method(scope);
            const auto* const callable
                = method ? hir_.find_declaration(*method) : nullptr;
            if (callable != nullptr && callable->callable
                && (std::ranges::find(callable->callable->formals, selected)
                        != callable->callable->formals.end()
                    || std::ranges::find(callable->children, selected)
                        != callable->children.end())) {
                if (hooks_.frame_local) {
                    if (const auto live = hooks_.frame_local(scope, selected);
                        live != 0U) {
                        return live;
                    }
                }
                return node(Kind::local_variable, selected.value(),
                    method->value(), scope, scope);
            }
        }
        if (const auto object = hooks_.resolve(scope, name)) {
            return hooks_.reference ? hooks_.reference(object) : object;
        }
        return node(Kind::reference, id.value(), frame_, scope, parent);
    }
    // A property named in a method of its class through `this`, written or
    // implicit (8.11): the class's property, or in a frame of a call of the
    // method the property of the object the call runs on (37.29 example 2).
    if (record->kind == sv::ExpressionKind::class_property && hooks_.member
        && record->operands.size() == 1U) {
        const auto* const receiver = expression(record->operands.front().value());
        if (receiver != nullptr && receiver->kind == sv::ExpressionKind::name
            && (receiver->referenced_name
                    ? receiver->referenced_name->spelling
                    : receiver->text)
                == "this") {
            if (const auto member
                = hooks_.member(scope, record->class_member_identity);
                member != 0U) {
                return member;
            }
        }
    }
    return node(Kind::expression, id.value(), frame_, scope, parent);
}

fsim_vpi_handle_v1 VpiStatementModel::call_callee(
    const std::uint32_t expression, const fsim_vpi_handle_v1 scope) const
{
    return hooks_.callee ? hooks_.callee(scope, false, expression)
                         : fsim_vpi_handle_v1 { };
}

// vpiDecompile of an expression (IEEE 1800-2023 37.59 detail 2): a
// functionally equivalent expression, each operand and operator separated by
// one space, parentheses only where precedence needs them; none for an
// expression the frontend generated with no source form.
std::optional<std::string> VpiStatementModel::decompile(
    const semantic::ExpressionId id, int* precedence) const
{
    const auto* const record = expression(id.value());
    if (record == nullptr) {
        return std::nullopt;
    }
    const auto set = [&](const int level) {
        if (precedence != nullptr) {
            *precedence = level;
        }
    };
    set(100);
    // An operand in parentheses when it binds less tightly than its
    // operator, or as tightly on the side the operator does not associate
    // to (all binary operators associate left but ?: and ->, 11.3.2).
    const auto operand = [&](const semantic::ExpressionId child,
                             const int level, const bool parenthesize_equal)
        -> std::optional<std::string> {
        int inner = 100;
        auto text = decompile(child, &inner);
        if (!text) {
            return std::nullopt;
        }
        if (inner < level || (parenthesize_equal && inner == level)) {
            return "(" + *text + ")";
        }
        return text;
    };
    const auto list = [&](const std::size_t first)
        -> std::optional<std::string> {
        std::string text;
        for (std::size_t index = first; index < record->operands.size();
             ++index) {
            auto item = decompile(record->operands[index], nullptr);
            if (!item) {
                return std::nullopt;
            }
            if (index > first) {
                text += ",";
            }
            if (index < record->argument_names.size()
                && !record->argument_names[index].empty()) {
                text += "." + record->argument_names[index] + "(" + *item + ")";
            } else {
                text += *item;
            }
        }
        return text;
    };
    using enum sv::ExpressionKind;
    switch (record->kind) {
    case name:
        return record->referenced_name ? record->referenced_name->spelling
                                       : record->text;
    case integer_literal:
    case boolean_literal:
    case logic_literal:
        return record->text;
    case string_literal:
        if (record->text.size() >= 2U && record->text.front() == '"'
            && record->text.back() == '"') {
            return record->text;
        }
        return "\"" + record->text + "\"";
    case class_null:
        return std::string { "null" };
    case unary: {
        if (record->operands.size() != 1U) {
            return std::nullopt;
        }
        set(90);
        auto inner = operand(record->operands[0], 90, false);
        return inner ? std::optional { record->text + " " + *inner }
                     : std::nullopt;
    }
    case binary: {
        if (record->operands.size() != 2U) {
            return std::nullopt;
        }
        const auto level = operator_precedence(record->text, 2U);
        if (level == 0) {
            return std::nullopt;
        }
        set(level);
        const bool right_associative = record->text == "->";
        auto left = operand(record->operands[0], level, right_associative);
        auto right = operand(record->operands[1], level, !right_associative);
        if (!left || !right) {
            return std::nullopt;
        }
        return *left + " " + record->text + " " + *right;
    }
    case call: {
        if (record->text == "?:" && record->operands.size() == 3U) {
            set(20);
            auto condition = operand(record->operands[0], 20, true);
            auto first = operand(record->operands[1], 20, true);
            auto second = operand(record->operands[2], 20, false);
            if (!condition || !first || !second) {
                return std::nullopt;
            }
            return *condition + " ? " + *first + " : " + *second;
        }
        if (sv::is_expression_size_cast(*record)) {
            auto size = operand(record->operands[0], 100, false);
            auto value = decompile(record->operands[1], nullptr);
            if (!size || !value) {
                return std::nullopt;
            }
            return *size + "'(" + *value + ")";
        }
        if (record->text.starts_with("@sv-cast:")
            && record->operands.size() == 1U) {
            const auto type = std::string_view { record->text }.substr(
                std::string_view { "@sv-cast:" }.size());
            auto value = decompile(record->operands[0], nullptr);
            if (!value || type.starts_with('@')) {
                return std::nullopt;
            }
            return std::string { type } + "'(" + *value + ")";
        }
        if (record->text.empty() || record->text.starts_with('@')) {
            return std::nullopt;
        }
        auto arguments = list(0U);
        if (!arguments) {
            return std::nullopt;
        }
        // A call with no arguments keeps its parentheses only for a user
        // function (13.4.1); `$time` is written without them.
        if (record->operands.empty() && record->text.starts_with('$')) {
            return record->text;
        }
        return record->text + "(" + *arguments + ")";
    }
    case index: {
        if (record->operands.size() < 2U) {
            return std::nullopt;
        }
        auto base = operand(record->operands[0], 100, false);
        if (!base) {
            return std::nullopt;
        }
        std::string text = *base;
        for (std::size_t position = 1U; position < record->operands.size();
             ++position) {
            auto item = decompile(record->operands[position], nullptr);
            if (!item) {
                return std::nullopt;
            }
            text += "[" + *item + "]";
        }
        return text;
    }
    case slice: {
        if (record->operands.size() != 3U) {
            return std::nullopt;
        }
        auto base = operand(record->operands[0], 100, false);
        auto left = decompile(record->operands[1], nullptr);
        auto right = decompile(record->operands[2], nullptr);
        if (!base || !left || !right) {
            return std::nullopt;
        }
        const auto separator = record->text == "+:" || record->text == "-:"
            ? record->text
            : std::string { ":" };
        return *base + "[" + *left + separator + *right + "]";
    }
    case concatenation: {
        auto items = list(0U);
        return items ? std::optional { "{" + *items + "}" } : std::nullopt;
    }
    case replication: {
        if (record->operands.empty()) {
            return std::nullopt;
        }
        auto count = decompile(record->operands[0], nullptr);
        auto items = list(1U);
        if (!count || !items) {
            return std::nullopt;
        }
        return "{" + *count + "{" + *items + "}}";
    }
    default:
        return std::nullopt;
    }
}

std::uint32_t VpiStatementModel::let_entry(const sv::LetDeclaration& declaration)
{
    const auto [found, created] = let_entry_of_.try_emplace(
        &declaration, static_cast<std::uint32_t>(let_entries_.size()));
    if (created) {
        let_entries_.push_back(&declaration);
    }
    return found->second;
}

const sv::LetDeclaration* VpiStatementModel::let_of_body(
    const semantic::ExpressionId body)
{
    if (!let_bodies_indexed_) {
        let_bodies_indexed_ = true;
        const auto add = [&](const std::vector<sv::LetDeclaration>& lets) {
            for (const auto& let : lets) {
                let_by_body_.insert_or_assign(let.expression.value(), &let);
            }
        };
        const auto visit = [&](const auto& self,
                               const sv::GenerateRegion& region) -> void {
            add(region.lets);
            for (const auto& nested : region.nested) {
                self(self, nested);
            }
        };
        for (const auto& unit : hir_.units()) {
            add(unit.lets);
            for (const auto& region : unit.generates) {
                visit(visit, region);
            }
        }
    }
    const auto found = let_by_body_.find(body.value());
    return found == let_by_body_.end() ? nullptr : found->second;
}

const sv::LetDeclaration* VpiStatementModel::let_of_use(const Node& entry)
{
    if (entry.kind != Kind::let_expr || !hooks_.let_body) {
        return nullptr;
    }
    const auto body = hooks_.let_body(
        entry.scope, semantic::ExpressionId::from_index(entry.id));
    return body ? let_of_body(*body) : nullptr;
}

std::uint32_t VpiStatementModel::let_frame(
    const sv::LetDeclaration& declaration,
    const std::optional<semantic::ExpressionId> use, const std::uint32_t outer,
    const fsim_vpi_handle_v1 scope)
{
    const auto key = std::tuple { &declaration,
        use ? use->value() : ~std::uint32_t { }, outer, scope };
    const auto [found, created] = let_frame_of_.try_emplace(
        key, static_cast<std::uint32_t>(let_frames_.size()));
    if (created) {
        let_frames_.push_back(LetFrame { &declaration, use, outer, scope });
    }
    return found->second;
}

// The arguments bind to the formals by position, then by name (11.12); a
// formal no argument names takes its default.
std::optional<semantic::ExpressionId> VpiStatementModel::let_argument(
    const sv::LetDeclaration& declaration, const semantic::ExpressionId use,
    const std::size_t port) const
{
    const auto* const call = expression(use.value());
    if (port >= declaration.ports.size() || call == nullptr) {
        return std::nullopt;
    }
    if (call->kind == sv::ExpressionKind::call) {
        for (std::size_t index = 0U; index < call->operands.size(); ++index) {
            const bool named = index < call->argument_names.size()
                && !call->argument_names[index].empty();
            if (named ? call->argument_names[index]
                        == declaration.ports[port].name
                      : index == port) {
                return call->operands[index];
            }
        }
    }
    return declaration.ports[port].default_value;
}

fsim_vpi_handle_v1 VpiStatementModel::cover_sequence_spec(const sv::Unit& unit,
    const sv::ConcurrentAssertion& assertion, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    if (assertion.form != sv::ConcurrentAssertionForm::sequence
        || !assertion.inline_tree) {
        return 0U;
    }
    const auto [found, created]
        = cover_sequence_of_.try_emplace(&assertion, cover_sequences_.size());
    if (created) {
        // A property with the sequence as its expression (16.14.3): the
        // name is no source name, so no scope lists it.
        sv::AssertionDeclaration declaration;
        declaration.kind = sv::AssertionDeclarationKind::property;
        declaration.name = "$inline_property$cover";
        if (!assertion.inline_clock_events.empty()) {
            declaration.clock = sv::AssertionClock { };
            declaration.clock->events = assertion.inline_clock_events;
        }
        if (assertion.inline_disable_condition) {
            declaration.disable = sv::AssertionDisable { };
            declaration.disable->condition = assertion.inline_disable_condition;
        }
        declaration.tree = assertion.inline_tree;
        declaration.source = assertion.source;
        cover_sequences_.push_back(std::move(declaration));
    }
    const auto& declaration = cover_sequences_[found->second];
    return node(Kind::assertion_spec,
        assertion_entry(declaration, &*declaration.tree, unit), 0U, scope,
        parent);
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::let_declarations(
    const VpiHirScope& scope)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto container = locate(scope);
    if (!container || container->lets == nullptr) {
        return found;
    }
    for (const auto& let : *container->lets) {
        found.push_back(node(
            Kind::let_decl, let_entry(let), 0U, scope.handle, scope.handle));
    }
    return found;
}

fsim_vpi_handle_v1 VpiStatementModel::let_value_expression(
    const std::uint64_t serial)
{
    const auto entry = nodes_.at(serial);
    const auto* const declaration = let_of_use(entry);
    if (declaration == nullptr) {
        return 0U;
    }
    const auto saved = std::exchange(frame_,
        let_frame(*declaration, semantic::ExpressionId::from_index(entry.id),
            entry.aux, entry.scope));
    const auto handle = expression_handle(
        declaration->expression, entry.scope, hooks_.handle(serial));
    frame_ = saved;
    return handle;
}

const sv::Statement* VpiStatementModel::find_block(
    const std::vector<semantic::StatementId>& statements,
    const std::string& label) const
{
    for (const auto id : statements) {
        const auto* const record = statement(id.value());
        if (record == nullptr) {
            continue;
        }
        if ((record->kind == sv::StatementKind::block
                || record->kind == sv::StatementKind::fork)
            && record->label == label) {
            return record;
        }
        // Statements nest without opening a named scope, except a named
        // block of their own.
        if ((record->kind == sv::StatementKind::block
                || record->kind == sv::StatementKind::fork)
            && !record->label.empty()) {
            continue;
        }
        if (const auto* found = find_block(record->statements, label)) {
            return found;
        }
        if (const auto* found = find_block(record->else_statements, label)) {
            return found;
        }
        for (const auto& alternative : record->case_alternatives) {
            if (const auto* found = find_block(alternative.statements, label)) {
                return found;
            }
        }
    }
    return nullptr;
}

std::optional<VpiStatementModel::Container> VpiStatementModel::locate(
    const VpiHirScope& scope)
{
    const auto unit = std::ranges::find(
        hir_.units(), semantic::UnitId::from_index(scope.unit), &sv::Unit::id);
    if (unit == hir_.units().end()) {
        return std::nullopt;
    }
    Container container;
    container.processes = unit->processes;
    container.concurrent = unit->concurrent_statements;
    container.declarations = unit->declarations;
    container.generates = &unit->generates;
    container.assertion_declarations = &unit->assertion_declarations;
    container.aliases = &unit->aliases;
    container.lets = &unit->lets;
    for (std::size_t at = 0U; at < scope.steps.size(); ++at) {
        const auto& [step, name] = scope.steps[at];
        switch (step) {
        case VpiHirScope::Step::generate: {
            if (container.generates == nullptr) {
                return std::nullopt;
            }
            const auto wanted = base_name(name);
            const auto selected = at < scope.regions.size()
                ? scope.regions[at]
                : semantic::DeclarationId { };
            const sv::GenerateRegion* region = nullptr;
            // The first region of that name, taken when the elaborator's
            // selection is unknown or none of them is it.
            const sv::GenerateRegion* first = nullptr;
            const auto take = [&](const sv::GenerateRegion& candidate) {
                if (selected.valid() && candidate.declaration != selected) {
                    if (first == nullptr) {
                        first = &candidate;
                    }
                    return;
                }
                region = &candidate;
            };
            // A generate block's region: a branch of a construct (its else
            // branch or case item) is a nested region of its own, named by
            // its label (27.5); a region without a label adds no scope. An
            // else branch that shares its if branch's name and scope has no
            // label of its own: the selected branch of the two is the scope.
            const std::function<void(const sv::GenerateRegion&)> visit
                = [&](const sv::GenerateRegion& candidate) {
                      if (region != nullptr) {
                          return;
                      }
                      if (candidate.label == wanted) {
                          take(candidate);
                          for (const auto& branch : candidate.nested) {
                              if (region == nullptr && branch.label.empty()
                                  && branch.scope == candidate.scope
                                  && branch.alternative_discriminator
                                      != candidate.alternative_discriminator) {
                                  take(branch);
                              }
                          }
                          if (region != nullptr) {
                              return;
                          }
                      }
                      for (const auto& nested : candidate.nested) {
                          if (candidate.label.empty()
                              || nested.alternative_discriminator
                                  != candidate.alternative_discriminator) {
                              visit(nested);
                          }
                      }
                  };
            const auto search = [&](const std::vector<sv::GenerateRegion>& regions) {
                for (const auto& candidate : regions) {
                    visit(candidate);
                }
            };
            search(*container.generates);
            if (region == nullptr) {
                region = first;
            }
            if (region == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.processes = region->processes;
            container.concurrent = region->concurrent_statements;
            container.declarations = region->declarations;
            container.generates = &region->nested;
            container.assertion_declarations = &region->assertion_declarations;
            container.aliases = &region->aliases;
            container.lets = &region->lets;
            break;
        }
        case VpiHirScope::Step::callable: {
            const sv::Declaration* callable = nullptr;
            for (const auto id : container.declarations) {
                const auto* const declaration = hir_.find_declaration(id);
                if (declaration != nullptr && declaration->name == name
                    && (declaration->form == sv::DeclarationForm::task
                        || declaration->form
                            == sv::DeclarationForm::function)) {
                    callable = declaration;
                    break;
                }
            }
            if (callable == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.statements = callable->statements;
            container.callable = callable->id;
            break;
        }
        case VpiHirScope::Step::block: {
            const sv::Statement* block = find_block(container.statements, name);
            for (const auto id : container.processes) {
                if (block != nullptr) {
                    break;
                }
                if (const auto* const record = process(id.value())) {
                    block = find_block(record->statements, name);
                }
            }
            if (block == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.statements = block->statements;
            container.block = block;
            break;
        }
        }
    }
    return container;
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiStatementModel::iterate_scope(
    const VpiHirScope& scope, const PLI_INT32 type)
{
    if (type != vpiProcess && type != vpiContAssign && type != vpiStmt) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container) {
        return std::nullopt;
    }
    std::vector<fsim_vpi_handle_v1> found;
    if (type == vpiProcess) {
        for (const auto id : container->processes) {
            const auto* const record = process(id.value());
            // Declaration initializers run as frontend-made processes
            // named with a '$'; they are not processes of the design.
            if (record != nullptr && !record->concurrent_assertion
                && !record->name.starts_with('$')) {
                found.push_back(node(Kind::process, id.value(), 0U,
                    scope.handle, scope.handle));
            }
        }
    } else if (type == vpiContAssign) {
        for (const auto id : container->concurrent) {
            const auto* const record = statement(id.value());
            // A gate is lowered to continuous assignments but is a
            // primitive, not a continuous assignment (IEEE 1800-2023 37.35).
            if (record != nullptr
                && record->kind == sv::StatementKind::assignment
                && record->assignment_kind
                    == sv::AssignmentKind::continuous
                && record->gate_primitive.empty()) {
                found.push_back(node(Kind::cont_assign, id.value(), 0U,
                    scope.handle, scope.handle));
            }
        }
    } else {
        for (const auto id : container->statements) {
            if (const auto handle
                = statement_handle(id, scope.handle, scope.handle)) {
                found.push_back(handle);
            }
        }
    }
    return found;
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::alias_statements(
    const VpiHirScope& scope)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto container = locate(scope);
    if (!container || container->aliases == nullptr) {
        return found;
    }
    for (const auto& alias : *container->aliases) {
        for (std::size_t terminal = 0U; terminal + 1U < alias.terminals.size();
             ++terminal) {
            alias_entries_.push_back(
                { &alias, static_cast<std::uint32_t>(terminal) });
            found.push_back(node(Kind::alias_stmt,
                static_cast<std::uint32_t>(alias_entries_.size() - 1U), 0U,
                scope.handle, scope.handle));
        }
    }
    return found;
}

std::uint32_t VpiStatementModel::assertion_entry(
    const sv::AssertionDeclaration& declaration, const sv::AssertionTree* tree,
    const sv::Unit& unit)
{
    const auto [found, created] = assertion_entry_index_.try_emplace(
        std::tuple<const void*, const void*> { &declaration, tree },
        static_cast<std::uint32_t>(assertion_entries_.size()));
    if (created) {
        assertion_entries_.push_back({ &declaration, tree, &unit });
    }
    return found->second;
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_decl_handle(
    const sv::AssertionDeclaration& declaration, const sv::Unit& unit,
    const fsim_vpi_handle_v1 scope)
{
    return node(Kind::assertion_decl,
        assertion_entry(declaration,
            declaration.tree ? &*declaration.tree : nullptr, unit),
        0U, scope, scope);
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::assertion_declarations(
    const VpiHirScope& scope, const PLI_INT32 type)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto container = locate(scope);
    const auto unit = std::ranges::find(
        hir_.units(), semantic::UnitId::from_index(scope.unit), &sv::Unit::id);
    if (!container || container->assertion_declarations == nullptr
        || unit == hir_.units().end()) {
        return found;
    }
    for (const auto& declaration : *container->assertion_declarations) {
        // The declarations the frontend synthesizes for inline properties,
        // and those a checker instance specializes into the scope (17.3),
        // are not declarations written in it.
        if (declaration.kind == sv::AssertionDeclarationKind::checker
            || declaration.name.starts_with('$')
            || !declaration.checker_instance.empty()) {
            continue;
        }
        const bool property
            = declaration.kind == sv::AssertionDeclarationKind::property;
        if (type == (property ? vpiPropertyDecl : vpiSequenceDecl)) {
            found.push_back(
                assertion_decl_handle(declaration, *unit, scope.handle));
        }
    }
    return found;
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_declaration_named(
    const VpiHirScope& scope, const std::string& name)
{
    const auto container = locate(scope);
    const auto unit = std::ranges::find(
        hir_.units(), semantic::UnitId::from_index(scope.unit), &sv::Unit::id);
    if (!container || container->assertion_declarations == nullptr
        || unit == hir_.units().end()) {
        return 0U;
    }
    for (const auto& declaration : *container->assertion_declarations) {
        if (declaration.kind != sv::AssertionDeclarationKind::checker
            && declaration.checker_instance.empty()
            && declaration.name == name && !name.starts_with('$')) {
            return assertion_decl_handle(declaration, *unit, scope.handle);
        }
    }
    return 0U;
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_property(const sv::Unit& unit,
    const sv::AssertionDeclaration& declaration,
    const sv::AssertionTree* instance, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    if (instance != nullptr && instance->root.node >= 0
        && static_cast<std::size_t>(instance->root.node)
            < instance->nodes.size()) {
        const auto root = static_cast<std::uint32_t>(instance->root.node);
        const auto* const named
            = sv::find_assertion_declaration(unit, instance->nodes[root].instance);
        return node(Kind::assertion_op,
            assertion_entry(named != nullptr ? *named : declaration, instance,
                unit),
            root, scope, parent);
    }
    if (instance == nullptr && declaration.name.starts_with("$inline_property$")) {
        return node(Kind::assertion_spec,
            assertion_entry(declaration,
                declaration.tree ? &*declaration.tree : nullptr, unit),
            0U, scope, parent);
    }
    return 0U;
}

fsim_vpi_handle_v1 VpiStatementModel::assertion_operand(
    const std::uint32_t entry, const sv::AssertionTreeOperand& operand,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent)
{
    const auto& table = assertion_entries_.at(entry);
    fsim_vpi_handle_v1 handle { };
    if (operand.node >= 0 && table.tree != nullptr
        && static_cast<std::size_t>(operand.node) < table.tree->nodes.size()) {
        handle = node(Kind::assertion_op, entry,
            static_cast<std::uint32_t>(operand.node), scope, parent);
    } else if (operand.expression) {
        // A plain name with match items, `(a, v = d)`, or a strength,
        // `strong(a)`, is an occurrence of its own: a ref obj named like it
        // whose vpiActual is the object (37.15, 37.52, 37.54), so each
        // occurrence keeps its own items and strength.
        const auto* const record = expression(operand.expression->value());
        if (operand.strong) {
            operand_strength_.insert_or_assign(
                operand.expression->value(), *operand.strong);
        }
        handle = (!operand.match_items.empty() || operand.strong)
                && record != nullptr
                && record->kind == sv::ExpressionKind::name
            ? node(Kind::reference, operand.expression->value(), 0U, scope,
                  parent)
            : expression_handle(*operand.expression, scope, parent);
    }
    if (handle != 0U && !operand.match_items.empty()) {
        match_items_.insert_or_assign(
            handle, MatchItems { operand.match_items, scope });
    }
    return handle;
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiStatementModel::match_items(
    const fsim_vpi_handle_v1 handle)
{
    const auto found = match_items_.find(handle);
    if (found == match_items_.end()) {
        return std::nullopt;
    }
    // Copied: creating statement nodes may add match items of other
    // operands.
    const auto entry = found->second;
    std::vector<fsim_vpi_handle_v1> items;
    for (const auto item : entry.items) {
        if (const auto statement = statement_handle(item, entry.scope, handle)) {
            items.push_back(statement);
        }
    }
    return items;
}

PLI_INT32 VpiStatementModel::assertion_op_type(const sv::AssertionOperator op)
{
    using Operator = sv::AssertionOperator;
    switch (op) {
    case Operator::not_operator:
        return vpiNotOp;
    case Operator::overlap_imply:
        return vpiOverlapImplyOp;
    case Operator::nonoverlap_imply:
        return vpiNonOverlapImplyOp;
    case Operator::overlap_followed_by:
        return vpiOverlapFollowedByOp;
    case Operator::nonoverlap_followed_by:
        return vpiNonOverlapFollowedByOp;
    case Operator::if_operator:
        return vpiIfOp;
    case Operator::if_else:
        return vpiIfElseOp;
    case Operator::iff:
        return vpiIffOp;
    case Operator::implies:
        return vpiImpliesOp;
    case Operator::until:
        return vpiUntilOp;
    case Operator::until_with:
        return vpiUntilWithOp;
    case Operator::nexttime:
        return vpiNexttimeOp;
    case Operator::always:
        return vpiAlwaysOp;
    case Operator::eventually:
        return vpiEventuallyOp;
    case Operator::accept_on:
        return vpiAcceptOnOp;
    case Operator::reject_on:
        return vpiRejectOnOp;
    case Operator::sync_accept_on:
        return vpiSyncAcceptOnOp;
    case Operator::sync_reject_on:
        return vpiSyncRejectOnOp;
    case Operator::comp_and:
        return vpiCompAndOp;
    case Operator::comp_or:
        return vpiCompOrOp;
    case Operator::cycle_delay:
        return vpiCycleDelayOp;
    case Operator::unary_cycle_delay:
        return vpiUnaryCycleDelayOp;
    case Operator::repeat:
        return vpiRepeatOp;
    case Operator::consecutive_repeat:
        return vpiConsecutiveRepeatOp;
    case Operator::goto_repeat:
        return vpiGotoRepeatOp;
    case Operator::intersect:
        return vpiIntersectOp;
    case Operator::first_match:
        return vpiFirstMatchOp;
    case Operator::throughout:
        return vpiThroughoutOp;
    case Operator::within:
        return vpiWithinOp;
    }
    return vpiNotOp;
}

bool VpiStatementModel::declares(
    const VpiHirScope& scope, const std::string& name)
{
    if (scope.steps.empty()
        || scope.steps.back().first == VpiHirScope::Step::generate) {
        return false;
    }
    const auto container = locate(scope);
    if (!container) {
        return false;
    }
    const auto named = [&](const std::vector<semantic::DeclarationId>& ids) {
        return std::ranges::any_of(ids, [&](const semantic::DeclarationId id) {
            const auto* const declaration = hir_.find_declaration(id);
            return declaration != nullptr && declaration->name == name;
        });
    };
    if (container->callable) {
        const auto* const callable
            = hir_.find_declaration(*container->callable);
        return callable != nullptr
            && ((callable->callable && named(callable->callable->formals))
                || named(callable->children));
    }
    return container->block != nullptr
        && named(container->block->declarations);
}

std::optional<bool> VpiStatementModel::local_automatic(
    const VpiHirScope& scope, const std::string& name)
{
    if (scope.steps.empty()
        || scope.steps.back().first == VpiHirScope::Step::generate) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container || !container->callable) {
        return std::nullopt;
    }
    const auto* const callable = hir_.find_declaration(*container->callable);
    if (callable == nullptr || !callable->callable) {
        return std::nullopt;
    }
    using Lifetime = semantic::sv::Lifetime;
    const bool automatic_callable
        = callable->callable->lifetime == Lifetime::automatic
        || callable->lifetime == Lifetime::automatic;
    const auto find = [&](const std::vector<semantic::DeclarationId>& ids)
        -> const semantic::sv::Declaration* {
        for (const auto id : ids) {
            if (const auto* const declaration = hir_.find_declaration(id);
                declaration != nullptr && declaration->name == name) {
                return declaration;
            }
        }
        return nullptr;
    };
    // The formals of a static task are static; its locals follow their own
    // keyword.
    if (find(callable->callable->formals) != nullptr) {
        return automatic_callable;
    }
    if (const auto* const local = find(callable->children)) {
        if (local->lifetime == Lifetime::automatic) {
            return true;
        }
        if (local->lifetime == Lifetime::static_lifetime) {
            return false;
        }
        return automatic_callable;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> VpiStatementModel::referenced_declaration(
    const std::uint64_t serial) const
{
    if (!owns(serial) || nodes_[serial].kind != Kind::reference) {
        return std::nullopt;
    }
    const auto* const record = expression(nodes_[serial].id);
    if (record == nullptr || !record->referenced_name
        || !record->referenced_name->selected) {
        return std::nullopt;
    }
    return record->referenced_name->selected->value();
}

std::optional<VpiStatementModel::BlockSource> VpiStatementModel::block_source(
    const VpiHirScope& scope)
{
    if (scope.steps.empty()
        || scope.steps.back().first != VpiHirScope::Step::block) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container || container->block == nullptr
        || !container->block->source.valid()
        || container->block->source.value()
            >= semantics_.source_spans().size()) {
        return std::nullopt;
    }
    const auto& location
        = semantics_.source_spans().at(container->block->source.value());
    BlockSource found;
    found.line = location.begin.line;
    if (!location.logical_name.empty()) {
        found.file = location.logical_name;
    } else if (location.file.valid()
        && location.file.value() < semantics_.source_files().size()) {
        found.file
            = semantics_.source_files().at(location.file.value()).physical_name;
    }
    return found;
}

std::optional<VpiStatementModel::ScopeSpan> VpiStatementModel::scope_span(
    const VpiHirScope& scope)
{
    if (scope.steps.empty()) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container) {
        return std::nullopt;
    }
    semantic::SourceSpanId source;
    if (scope.steps.back().first == VpiHirScope::Step::block
        && container->block != nullptr) {
        source = container->block->source;
    } else if (scope.steps.back().first == VpiHirScope::Step::callable
        && container->callable) {
        if (const auto* const declaration
            = hir_.find_declaration(*container->callable)) {
            source = declaration->source;
        }
    }
    if (!source.valid() || source.value() >= semantics_.source_spans().size()) {
        return std::nullopt;
    }
    const auto& location = semantics_.source_spans().at(source.value());
    ScopeSpan found;
    found.logical_name = location.logical_name;
    if (location.file.valid()
        && location.file.value() < semantics_.source_files().size()) {
        found.physical_name
            = semantics_.source_files().at(location.file.value()).physical_name;
    }
    found.begin_line = location.begin.line;
    found.begin_column = location.begin.column;
    found.end_line = location.end.line;
    found.end_column = location.end.column;
    return found;
}

std::optional<semantic::StatementId> VpiStatementModel::foreach_body(
    const sv::Statement& record) const
{
    const auto& spans = semantics_.source_spans();
    if (!record.foreach_body_source
        || record.foreach_body_source->value() >= spans.size()) {
        return std::nullopt;
    }
    const auto wanted
        = spans.at(record.foreach_body_source->value()).begin.offset;
    const auto search = [&](const auto& self, const sv::Statement& within,
                            const std::size_t depth)
        -> std::optional<semantic::StatementId> {
        const auto visit = [&](const std::vector<semantic::StatementId>& ids)
            -> std::optional<semantic::StatementId> {
            for (const auto id : ids) {
                const auto* const child = statement(id.value());
                if (child == nullptr) {
                    continue;
                }
                if (child->source.valid()
                    && child->source.value() < spans.size()
                    && spans.at(child->source.value()).begin.offset == wanted) {
                    return id;
                }
                if (const auto found = self(self, *child, depth + 1U)) {
                    return found;
                }
            }
            return std::nullopt;
        };
        if (const auto found = visit(within.statements)) {
            return found;
        }
        if (const auto found = visit(within.else_statements)) {
            return found;
        }
        for (const auto& alternative : within.case_alternatives) {
            if (const auto found = visit(alternative.statements)) {
                return found;
            }
        }
        return std::nullopt;
    };
    return search(search, record, 0U);
}

const sv::Declaration* VpiStatementModel::callable_declaration(
    const VpiHirScope& scope)
{
    if (scope.steps.empty()
        || scope.steps.back().first != VpiHirScope::Step::callable) {
        return nullptr;
    }
    const auto container = locate(scope);
    if (!container || !container->callable) {
        return nullptr;
    }
    return hir_.find_declaration(*container->callable);
}

std::optional<fsim_vpi_handle_v1> VpiStatementModel::scope_relation(
    const VpiHirScope& scope, const PLI_INT32 type)
{
    // The variable that holds a function's value (IEEE 1800-2023 37.41
    // details 1-3); a task and a void function have none.
    if (type == vpiReturn) {
        const auto* const callable = callable_declaration(scope);
        if (callable == nullptr) {
            return std::nullopt;
        }
        if (callable->form != sv::DeclarationForm::function
            || !callable->callable
            || type_word(callable->callable->return_type) == "void") {
            return fsim_vpi_handle_v1 { };
        }
        return node(Kind::return_variable, callable->id.value(),
            callable->id.value(), scope.handle, scope.handle);
    }
    if (type != vpiStmt || scope.steps.empty()
        || scope.steps.back().first != VpiHirScope::Step::callable) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container || !container->callable) {
        return fsim_vpi_handle_v1 { };
    }
    return sequence(container->statements, container->callable->value(),
        body_callable, scope.handle, scope.handle);
}

std::optional<std::vector<fsim_vpi_handle_v1>>
VpiStatementModel::scope_iterate_tf(const VpiHirScope& scope, const PLI_INT32 type)
{
    if (type != vpiIODecl) {
        return std::nullopt;
    }
    const auto* const callable = callable_declaration(scope);
    if (callable == nullptr) {
        return std::nullopt;
    }
    std::vector<fsim_vpi_handle_v1> found;
    if (callable->callable) {
        for (const auto formal : callable->callable->formals) {
            found.push_back(node(Kind::io_decl, formal.value(),
                callable->id.value(), scope.handle, scope.handle));
        }
    }
    return found;
}

// Whether a continuous assignment is a net declaration assignment: the
// `wire w = e;` form, whose span starts at the net's name, where an `assign`
// starts at its keyword (IEEE 1800-2023 10.3.1, 37.47).
bool VpiStatementModel::declared_net_assignment(
    const sv::Statement& record) const
{
    const auto* const target
        = record.target ? expression(record.target->value()) : nullptr;
    if (target == nullptr || !target->source.valid()
        || !record.source.valid()
        || target->source.value() >= semantics_.source_spans().size()
        || record.source.value() >= semantics_.source_spans().size()) {
        return false;
    }
    const auto& statement_begin
        = semantics_.source_spans().at(record.source.value()).begin;
    const auto& target_begin
        = semantics_.source_spans().at(target->source.value()).begin;
    return statement_begin.line == target_begin.line
        && statement_begin.column == target_begin.column;
}

const sv::DpiDeclaration* VpiStatementModel::dpi_import(const Node& entry) const
{
    // A compilation-unit import is in the HIR's own list (unit id all ones).
    if (entry.id == std::numeric_limits<std::uint32_t>::max()) {
        const auto index = entry.aux >> 16U;
        return index < hir_.dpi_declarations().size()
            ? &hir_.dpi_declarations()[index]
            : nullptr;
    }
    for (const auto& unit : hir_.units()) {
        if (unit.id.value() != entry.id) {
            continue;
        }
        const auto index = entry.aux >> 16U;
        return index < unit.dpi_declarations.size()
            ? &unit.dpi_declarations[index]
            : nullptr;
    }
    return nullptr;
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::dpi_formals(
    const std::uint32_t unit, const std::uint32_t index,
    const fsim_vpi_handle_v1 scope)
{
    std::vector<fsim_vpi_handle_v1> found;
    const Node probe { Kind::dpi_io_decl, unit, index << 16U, 0U, 0U, false };
    const auto* const dpi = dpi_import(probe);
    if (dpi == nullptr) {
        return found;
    }
    for (std::uint32_t formal = 0U; formal < dpi->formals.size(); ++formal) {
        found.push_back(node(Kind::dpi_io_decl, unit, (index << 16U) | formal,
            scope, scope));
    }
    return found;
}

fsim_vpi_handle_v1 VpiStatementModel::dpi_result(const std::uint32_t unit,
    const std::uint32_t index, const fsim_vpi_handle_v1 scope)
{
    const Node probe { Kind::dpi_return_variable, unit, index << 16U, 0U, 0U,
        false };
    const auto* const dpi = dpi_import(probe);
    // A task and a void function have no result variable (37.41 details
    // 1-3).
    if (dpi == nullptr || dpi->callable_kind != sv::DpiCallableKind::function
        || (dpi->return_type_tokens.size() == 1U
            && dpi->return_type_tokens.front().text == "void")) {
        return fsim_vpi_handle_v1 { };
    }
    return node(Kind::dpi_return_variable, unit, (index << 16U) | 0xffffU,
        scope, scope);
}

std::optional<VpiStatementModel::DeclaredType>
VpiStatementModel::scope_return_type(const VpiHirScope& scope)
{
    const auto* const callable = callable_declaration(scope);
    if (callable == nullptr || callable->form != sv::DeclarationForm::function
        || !callable->callable) {
        return std::nullopt;
    }
    return declared_type_of(callable->callable->return_type);
}

std::optional<VpiStatementModel::DeclaredType> VpiStatementModel::declared_type(
    const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    // The formals and result of a DPI import are typed by the profile the
    // import resolved to (IEEE 1800-2023 35.5.4, 37.41).
    if (entry.kind == Kind::dpi_io_decl
        || entry.kind == Kind::dpi_return_variable) {
        const auto* const dpi = dpi_import(entry);
        if (dpi == nullptr) {
            return std::nullopt;
        }
        const auto formal = entry.aux & 0xffffU;
        // An import is typed by its own declaration; only an export resolves
        // to a native callable's profile.
        if (!dpi->resolved_profile) {
            if (entry.kind == Kind::dpi_return_variable) {
                return declared_type_of_tokens(dpi->return_type_tokens, { });
            }
            return formal < dpi->formals.size()
                ? declared_type_of_tokens(dpi->formals[formal].type_tokens,
                      dpi->formals[formal].dimension_tokens)
                : std::nullopt;
        }
        const auto& profile = *dpi->resolved_profile;
        if (entry.kind == Kind::dpi_return_variable) {
            return profile.return_type ? declared_type_of(*profile.return_type)
                                       : std::nullopt;
        }
        return formal < profile.formals.size()
            ? declared_type_of(profile.formals[formal].type)
            : std::nullopt;
    }
    // A typed formal or a local variable of a property or sequence
    // declaration (IEEE 1800-2023 37.51, 37.53 vpiTypespec): a built-in type
    // from its declared type, a named type through the elaborator's typespec
    // keyed by the declaration and the name. An untyped, sequence or
    // property formal has none.
    // A typed let formal (37.57, 37.53 vpiTypespec), keyed by the let and
    // the formal like a callable's.
    if (entry.kind == Kind::let_formal) {
        if (entry.id >= let_entries_.size()
            || entry.aux >= let_entries_[entry.id]->ports.size()
            || !let_entries_[entry.id]->ports[entry.aux].type) {
            return std::nullopt;
        }
        const auto& let = *let_entries_[entry.id];
        auto result = declared_type_of(*let.ports[entry.aux].type);
        if (result) {
            result->scope = entry.scope;
            result->callable = let.name;
            result->formal = let.ports[entry.aux].name;
        }
        return result;
    }
    if (entry.kind == Kind::assertion_formal
        || entry.kind == Kind::assertion_variable) {
        const auto& table = assertion_entries_.at(entry.id);
        const auto& declaration = *table.declaration;
        const std::vector<sv::SourceToken>* tokens { };
        std::vector<sv::SourceToken> dimensions;
        std::string name;
        if (entry.kind == Kind::assertion_formal) {
            if (entry.aux >= declaration.formals.size()
                || declaration.formals[entry.aux].kind
                    != sv::AssertionFormalKind::value) {
                return std::nullopt;
            }
            tokens = &declaration.formals[entry.aux].type_tokens;
            name = declaration.formals[entry.aux].name;
        } else {
            if (entry.aux >= declaration.local_variables.size()) {
                return std::nullopt;
            }
            const auto& local = declaration.local_variables[entry.aux];
            tokens = &local.type_tokens;
            name = local.name;
            // Unpacked dimensions of the declarator.
            for (const auto& token : local.declarator_tokens) {
                if (token.text == "=") {
                    break;
                }
                if (token.text == "[" || !dimensions.empty()) {
                    dimensions.push_back(token);
                }
            }
        }
        auto result = declared_type_of_tokens(*tokens, { });
        const bool named = !tokens->empty()
            && (tokens->size() == 1U
                || (tokens->size() == 3U && (*tokens)[1].text == "::"));
        if (!result && named) {
            result = DeclaredType { };
            for (const auto& token : *tokens) {
                result->type_name += token.text;
            }
        }
        // A fixed-size unpacked array of the type (37.25 detail 2): `[n]`
        // is `[0:n-1]` (7.4.2); a bound that is not a literal leaves it
        // without a typespec.
        if (result && !dimensions.empty()) {
            DeclaredType array;
            array.array_kind = 1;
            const auto number = [](const std::string& text)
                -> std::optional<std::int64_t> {
                if (text.empty()
                    || !std::ranges::all_of(text, [](const char digit) {
                           return digit >= '0' && digit <= '9';
                       })) {
                    return std::nullopt;
                }
                return std::stoll(text);
            };
            for (std::size_t index = 0U; index < dimensions.size();) {
                if (index + 2U < dimensions.size()
                    && dimensions[index].text == "["
                    && dimensions[index + 2U].text == "]") {
                    const auto size = number(dimensions[index + 1U].text);
                    if (!size || *size == 0) {
                        return std::nullopt;
                    }
                    array.dimensions.emplace_back(0, *size - 1);
                    index += 3U;
                } else if (index + 4U < dimensions.size()
                    && dimensions[index].text == "["
                    && dimensions[index + 2U].text == ":"
                    && dimensions[index + 4U].text == "]") {
                    const auto left = number(dimensions[index + 1U].text);
                    const auto right = number(dimensions[index + 3U].text);
                    if (!left || !right) {
                        return std::nullopt;
                    }
                    array.dimensions.emplace_back(*left, *right);
                    index += 5U;
                } else {
                    return std::nullopt;
                }
            }
            array.element.push_back(*result);
            result = std::move(array);
        }
        if (result) {
            result->scope = entry.scope;
            result->callable = declaration.name;
            result->formal = name;
        }
        return result;
    }
    // A for loop's own variable (12.7.1): a built-in type from its declared
    // type, a named type through the typedef visible where the loop is.
    if (entry.kind == Kind::local_variable) {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        if (declaration == nullptr || !declaration->type) {
            return std::nullopt;
        }
        auto result = declared_type_of(*declaration->type);
        if (result) {
            result->scope = entry.scope;
        }
        return result;
    }
    if (entry.kind != Kind::io_decl && entry.kind != Kind::return_variable
        && entry.kind != Kind::ref_formal) {
        return std::nullopt;
    }
    const auto* const declaration = hir_.find_declaration(
        semantic::DeclarationId::from_index(entry.id));
    if (declaration == nullptr) {
        return std::nullopt;
    }
    auto result = entry.kind != Kind::return_variable
        ? (declaration->type ? declared_type_of(*declaration->type)
                             : std::nullopt)
        : (declaration->callable
                ? declared_type_of(declaration->callable->return_type)
                : std::nullopt);
    if (result) {
        result->scope = entry.scope;
        if (entry.kind != Kind::return_variable) {
            result->formal = declaration->name;
            if (const auto* const callable = hir_.find_declaration(
                    semantic::DeclarationId::from_index(entry.aux))) {
                result->callable = callable->name;
            }
        } else {
            result->callable = declaration->name;
        }
    }
    return result;
}

fsim_vpi_handle_v1 VpiStatementModel::declaration_initializer(
    const VpiHirScope& scope, const std::string& name)
{
    const auto container = locate(scope);
    if (!container) {
        return 0U;
    }
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration != nullptr && declaration->name == name
            && declaration->initializer) {
            return expression_handle(
                *declaration->initializer, scope.handle, scope.handle);
        }
    }
    return 0U;
}

std::optional<VpiStatementModel::RefFormal> VpiStatementModel::ref_formal(
    const std::uint64_t serial) const
{
    const auto entry = nodes_.at(serial);
    if (entry.kind != Kind::ref_formal) {
        return std::nullopt;
    }
    const auto* const callable = hir_.find_declaration(
        semantic::DeclarationId::from_index(entry.aux));
    if (callable == nullptr || !callable->callable) {
        return std::nullopt;
    }
    const auto& formals = callable->callable->formals;
    const auto found = std::ranges::find_if(formals, [&](const auto formal) {
        return formal.value() == entry.id;
    });
    if (found == formals.end()) {
        return std::nullopt;
    }
    return RefFormal { callable->name,
        static_cast<std::size_t>(found - formals.begin()), entry.scope };
}

fsim_vpi_handle_v1 VpiStatementModel::call_actual_at(const std::string_view file,
    const std::uint32_t line, const std::uint32_t column,
    const std::size_t position, const fsim_vpi_handle_v1 scope)
{
    const auto begins_here = [&](const semantic::SourceSpanId source) {
        if (!source.valid() || source.value() >= semantics_.source_spans().size()) {
            return false;
        }
        const auto& location = semantics_.source_spans().at(source.value());
        if (location.begin.line != line || location.begin.column != column) {
            return false;
        }
        const auto* const physical = location.file.valid()
                && location.file.value() < semantics_.source_files().size()
            ? &semantics_.source_files().at(location.file.value()).physical_name
            : nullptr;
        // A point names the file by its last component.
        const auto base = [](const std::string_view path) {
            const auto slash = path.find_last_of("/\\");
            return slash == std::string_view::npos ? path
                                                   : path.substr(slash + 1U);
        };
        return file.empty() || base(location.logical_name) == base(file)
            || (physical != nullptr && base(*physical) == base(file));
    };
    for (const auto& record : hir_.statements()) {
        if (record.kind != sv::StatementKind::task_call
            || !begins_here(record.source)) {
            continue;
        }
        if (position < record.task_arguments.size()
            && record.task_arguments[position].actual) {
            return expression_handle(
                *record.task_arguments[position].actual, scope, scope);
        }
    }
    for (const auto& record : hir_.expressions()) {
        if (record.kind != sv::ExpressionKind::call
            || !begins_here(record.source)) {
            continue;
        }
        if (position < record.operands.size()) {
            return expression_handle(record.operands[position], scope, scope);
        }
    }
    return 0U;
}

bool VpiStatementModel::extern_prototype(const VpiHirScope& scope)
{
    const auto* const callable = callable_declaration(scope);
    return callable != nullptr && callable->callable
        && callable->callable->extern_prototype;
}

std::optional<std::int64_t> VpiStatementModel::scope_property(
    const VpiHirScope& scope, const PLI_INT32 property)
{
    const auto* const callable = callable_declaration(scope);
    if (callable == nullptr) {
        return std::nullopt;
    }
    const auto function = callable->form == sv::DeclarationForm::function
        && callable->callable.has_value();
    switch (property) {
    // Only class members are methods, virtual or restricted; a task or
    // function of an instance is public (37.41 detail 4).
    case vpiMethod:
    case vpiVirtual:
        return 0;
    case vpiVisibility:
        return vpiPublicVis;
    // An `extern` prototype is vpiExternAcc, an `extern forkjoin` task
    // vpiForkJoinAcc (37.8 detail 2); a body is vpiForkJoinAcc.
    case vpiAccessType:
        return callable->callable && callable->callable->extern_prototype
                && !callable->callable->extern_forkjoin
            ? vpiExternAcc
            : vpiForkJoinAcc;
    case vpiSize:
        return function ? type_width(callable->callable->return_type)
                        : std::nullopt;
    case vpiSigned:
        return function && type_signed(callable->callable->return_type) ? 1 : 0;
    case vpiFuncType:
        return function
            ? std::optional<std::int64_t> { function_type(
                callable->callable->return_type) }
            : std::nullopt;
    default:
        return std::nullopt;
    }
}

bool VpiStatementModel::names_object(const sv::Expression& record,
    const fsim_vpi_handle_v1 within, const fsim_vpi_handle_v1 object) const
{
    // A member path also names the object its prefix names (IEEE 1800-2023
    // 37.21 detail 1).
    std::string path = record.referenced_name ? record.referenced_name->spelling
                                              : record.text;
    if (!name_filter_.empty()) {
        // Some component of the name is the object's own name.
        bool present { };
        for (std::size_t begin = 0U; !present && begin <= path.size();) {
            auto end = path.find('.', begin);
            if (end == std::string::npos) {
                end = path.size();
            }
            // A select in a hierarchical name (`g[1].x`) belongs to its
            // component.
            const auto component = std::string_view { path }.substr(
                begin, end - begin);
            present = component == name_filter_
                || (component.size() > name_filter_.size()
                    && component.starts_with(name_filter_)
                    && component[name_filter_.size()] == '[');
            begin = end + 1U;
        }
        if (!present) {
            return false;
        }
    }
    // Each step drops the last component of the path, so the walk ends.
    for (;;) {
        if (hooks_.resolve(within, path) == object) {
            return true;
        }
        const auto dot = path.rfind('.');
        if (dot == std::string::npos) {
            break;
        }
        path.resize(dot);
    }
    return false;
}

bool VpiStatementModel::mentions(const std::optional<semantic::ExpressionId> id,
    const fsim_vpi_handle_v1 within, const fsim_vpi_handle_v1 object) const
{
    if (!id) {
        return false;
    }
    std::vector<semantic::ExpressionId> pending { *id };
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto* const record = expression(current.value());
        if (record == nullptr) {
            continue;
        }
        if (record->kind == sv::ExpressionKind::name) {
            if (names_object(*record, within, object)) {
                return true;
            }
            continue;
        }
        pending.insert(pending.end(), record->operands.begin(),
            record->operands.end());
    }
    return false;
}

bool VpiStatementModel::targets_object(
    const std::optional<semantic::ExpressionId> id,
    const fsim_vpi_handle_v1 within, const fsim_vpi_handle_v1 object) const
{
    auto current = id;
    while (current) {
        const auto* const record = expression(current->value());
        if (record == nullptr) {
            return false;
        }
        if (record->kind == sv::ExpressionKind::name) {
            return names_object(*record, within, object);
        }
        if (record->kind == sv::ExpressionKind::concatenation) {
            return std::ranges::any_of(record->operands,
                [&](const semantic::ExpressionId operand) {
                    return targets_object(operand, within, object);
                });
        }
        if ((record->kind != sv::ExpressionKind::index
                && record->kind != sv::ExpressionKind::slice)
            || record->operands.empty()) {
            return false;
        }
        current = record->operands.front();
    }
    return false;
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::connections(
    const VpiHirScope& scope, const fsim_vpi_handle_v1 object,
    const Connection connection, std::vector<ConnectionOwner>* const owners,
    const std::string* const leaf)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto container = locate(scope);
    if (!container || object == 0U) {
        return found;
    }
    struct FilterScope {
        std::string& filter;
        ~FilterScope() { filter.clear(); }
    } filter_scope { name_filter_ };
    name_filter_ = leaf != nullptr ? *leaf : std::string { };
    // Who executes the statement being added, for `owners`.
    ConnectionOwner current_owner { ConnectionOwner::Kind::callable, 0U, 0U,
        false };
    const auto add = [&](const fsim_vpi_handle_v1 handle) {
        if (handle != 0U && std::ranges::find(found, handle) == found.end()) {
            found.push_back(handle);
            if (owners != nullptr) {
                owners->push_back(current_owner);
            }
        }
    };
    // Whether an assignment target names one object (a select of it counts)
    // rather than a concatenation of several.
    const auto sole_target = [&](std::optional<semantic::ExpressionId> id) {
        while (id) {
            const auto* const record = expression(id->value());
            if (record == nullptr
                || record->kind == sv::ExpressionKind::concatenation) {
                return false;
            }
            if (record->kind == sv::ExpressionKind::name) {
                return true;
            }
            if ((record->kind != sv::ExpressionKind::index
                    && record->kind != sv::ExpressionKind::slice)
                || record->operands.empty()) {
                return false;
            }
            id = record->operands.front();
        }
        return false;
    };
    // Whether an assignment's target reads the object through the indexes
    // of its selects (`mem[i] = ...` reads i).
    const auto select_reads = [&](std::optional<semantic::ExpressionId> id,
                                  const fsim_vpi_handle_v1 within) {
        while (id) {
            const auto* const record = expression(id->value());
            if (record == nullptr
                || (record->kind != sv::ExpressionKind::index
                    && record->kind != sv::ExpressionKind::slice)
                || record->operands.empty()) {
                return false;
            }
            for (std::size_t index = 1U; index < record->operands.size();
                 ++index) {
                if (mentions(record->operands[index], within, object)) {
                    return true;
                }
            }
            id = record->operands.front();
        }
        return false;
    };
    // Whether a node that writes `target` and reads `value` is wanted.
    const auto wanted = [&](const bool writes, const bool reads) {
        switch (connection) {
        case Connection::driver:
            return writes;
        case Connection::load:
            return reads;
        case Connection::use:
            return writes || reads;
        }
        return false;
    };
    // A statement's own expressions, for vpiUse.
    const auto statement_uses = [&](const sv::Statement& record,
                                    const fsim_vpi_handle_v1 within) {
        const auto uses = [&](const std::optional<semantic::ExpressionId> id) {
            return mentions(id, within, object);
        };
        if (uses(record.target) || uses(record.value) || uses(record.condition)
            || uses(record.loop_initial) || uses(record.loop_limit)
            || uses(record.file_handle)) {
            return true;
        }
        if (std::ranges::any_of(record.task_arguments,
                [&](const sv::TaskAssociation& argument) {
                    return uses(argument.actual);
                })
            || std::ranges::any_of(record.output_values,
                [&](const sv::OutputValue& output) {
                    return uses(output.value);
                })
            || std::ranges::any_of(record.case_alternatives,
                [&](const sv::CaseAlternative& alternative) {
                    return std::ranges::any_of(alternative.choices,
                        [&](const semantic::ExpressionId choice) {
                            return uses(choice);
                        });
                })) {
            return true;
        }
        return std::ranges::any_of(record.sensitivities,
            [&](const sv::Sensitivity& item) {
                return item.expression ? uses(item.expression)
                                       : hooks_.resolve(within, item.signal)
                        == object;
            });
    };
    const std::function<void(const std::vector<semantic::StatementId>&,
        fsim_vpi_handle_v1, fsim_vpi_handle_v1)>
        walk = [&](const std::vector<semantic::StatementId>& statements,
                   const fsim_vpi_handle_v1 within,
                   const fsim_vpi_handle_v1 parent) {
            for (const auto id : statements) {
                const auto* const record = statement(id.value());
                if (record == nullptr) {
                    continue;
                }
                using Statement = sv::StatementKind;
                const auto self = statement_handle(id, within, parent);
                // A blocking or nonblocking assignment uses the objects it
                // names but drives or loads none of them: the drivers and
                // loads are the forces and procedural continuous assigns
                // (IEEE 1800-2023 37.16 details 14 and 15, 37.21).
                const auto assigns = record->kind == Statement::force
                    || record->kind == Statement::procedural_assign
                    || (record->kind == Statement::assignment
                        && connection == Connection::use);
                if (assigns) {
                    const auto writes
                        = targets_object(record->target, within, object);
                    // An update (`x += 1`, `x++`) reads its target.
                    const auto updates = record->update_kind
                            != sv::UpdateKind::none
                        || !record->update_operator.empty();
                    const auto reads = mentions(record->value, within, object)
                        || (updates && writes)
                        || select_reads(record->target, within);
                    if (wanted(writes, reads)) {
                        current_owner.single_target
                            = sole_target(record->target);
                        add(self);
                    }
                } else if (connection == Connection::use
                    && statement_uses(*record, within)) {
                    add(self);
                }
                if (connection == Connection::use
                    && statement_type(*record) == vpiFor) {
                    if (record->loop_initial) {
                        const auto target = record->loop_update_target
                            ? record->loop_update_target
                            : record->target;
                        const auto writes = target
                            ? targets_object(target, within, object)
                            : !record->loop_variable_declared
                                && hooks_.resolve(within, record->loop_variable)
                                    == object;
                        if (wanted(writes,
                                mentions(record->loop_initial, within, object))) {
                            current_owner.single_target = target
                                ? sole_target(target) : true;
                            add(node(Kind::loop_init, id.value(), 0U, within,
                                self));
                        }
                    }
                    if (record->loop_updates.empty() && record->target
                        && record->value
                        && wanted(targets_object(record->target, within, object),
                            mentions(record->value, within, object))) {
                        current_owner.single_target
                            = sole_target(record->target);
                        add(node(Kind::loop_step, id.value(), 0U, within, self));
                    }
                }
                auto inner = within;
                if ((record->kind == Statement::block
                        || record->kind == Statement::fork)
                    && !record->label.empty()) {
                    if (const auto named
                        = hooks_.child_scope(within, record->label)) {
                        inner = named;
                    }
                }
                walk(record->statements, inner, self);
                walk(record->else_statements, inner, self);
                walk(record->loop_updates, inner, self);
                for (const auto& alternative : record->case_alternatives) {
                    walk(alternative.statements, inner, self);
                }
            }
        };
    for (std::size_t position = 0U; position < container->concurrent.size();
         ++position) {
        const auto id = container->concurrent[position];
        const auto* const record = statement(id.value());
        if (record == nullptr || record->kind != sv::StatementKind::assignment
            || record->assignment_kind != sv::AssignmentKind::continuous
            || !record->gate_primitive.empty()) {
            continue;
        }
        const auto writes = targets_object(record->target, scope.handle, object);
        const auto reads = mentions(record->value, scope.handle, object)
            || select_reads(record->target, scope.handle);
        if (wanted(writes, reads)) {
            current_owner = { ConnectionOwner::Kind::continuous, 0U,
                static_cast<std::uint32_t>(position),
                sole_target(record->target) };
            add(node(Kind::cont_assign, id.value(), 0U, scope.handle,
                scope.handle));
        }
    }
    current_owner.kind = ConnectionOwner::Kind::callable;
    walk(container->statements, scope.handle, scope.handle);
    for (const auto id : container->processes) {
        const auto* const record = process(id.value());
        if (record == nullptr || record->name.starts_with('$')) {
            continue;
        }
        current_owner.kind = ConnectionOwner::Kind::process;
        current_owner.process = id.value();
        walk(record->statements, scope.handle,
            node(Kind::process, id.value(), 0U, scope.handle, scope.handle));
    }
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration == nullptr
            || (declaration->form != sv::DeclarationForm::task
                && declaration->form != sv::DeclarationForm::function)) {
            continue;
        }
        const auto callable = hooks_.child_scope(scope.handle, declaration->name);
        if (callable != 0U) {
            current_owner.kind = ConnectionOwner::Kind::callable;
            walk(declaration->statements, callable, callable);
        }
    }
    return found;
}

std::vector<VpiStatementModel::ForceSite> VpiStatementModel::force_sites(
    const VpiHirScope& scope, const bool release,
    const std::function<bool(fsim_vpi_handle_v1)>& targets)
{
    std::vector<ForceSite> found;
    const auto container = locate(scope);
    if (!container) {
        return found;
    }
    // The object an assignment target names: a select's selected object.
    const auto target_object = [&](std::optional<semantic::ExpressionId> id,
                                   const fsim_vpi_handle_v1 within) {
        while (id) {
            const auto* const record = expression(id->value());
            if (record == nullptr) {
                return fsim_vpi_handle_v1 { };
            }
            if (record->kind == sv::ExpressionKind::name) {
                return hooks_.resolve(within,
                    record->referenced_name ? record->referenced_name->spelling
                                            : record->text);
            }
            if ((record->kind != sv::ExpressionKind::index
                    && record->kind != sv::ExpressionKind::slice)
                || record->operands.empty()) {
                return fsim_vpi_handle_v1 { };
            }
            id = record->operands.front();
        }
        return fsim_vpi_handle_v1 { };
    };
    std::uint32_t owner { };
    const std::function<void(const std::vector<semantic::StatementId>&,
        fsim_vpi_handle_v1, fsim_vpi_handle_v1)>
        walk = [&](const std::vector<semantic::StatementId>& statements,
                   const fsim_vpi_handle_v1 within,
                   const fsim_vpi_handle_v1 parent) {
            for (const auto id : statements) {
                const auto* const record = statement(id.value());
                if (record == nullptr) {
                    continue;
                }
                using Statement = sv::StatementKind;
                const auto reason = record->kind == Statement::force ? cbForce
                    : record->kind == Statement::procedural_assign ? cbAssign
                    : record->kind == Statement::release           ? cbRelease
                    : record->kind == Statement::deassign          ? cbDeassign
                                                                   : 0;
                const auto releases = reason == cbRelease || reason == cbDeassign;
                if (reason != 0 && releases == release
                    && targets(target_object(record->target, within))) {
                    found.push_back(ForceSite {
                        statement_handle(id, within, parent), reason, owner });
                    continue;
                }
                auto inner = within;
                if ((record->kind == Statement::block
                        || record->kind == Statement::fork)
                    && !record->label.empty()) {
                    if (const auto named
                        = hooks_.child_scope(within, record->label)) {
                        inner = named;
                    }
                }
                const auto self = statement_handle(id, within, parent);
                walk(record->statements, inner, self);
                walk(record->else_statements, inner, self);
                for (const auto& alternative : record->case_alternatives) {
                    walk(alternative.statements, inner, self);
                }
            }
        };
    for (const auto id : container->processes) {
        const auto* const record = process(id.value());
        if (record == nullptr || record->name.starts_with('$')) {
            continue;
        }
        owner = id.value();
        walk(record->statements, scope.handle,
            node(Kind::process, id.value(), 0U, scope.handle, scope.handle));
    }
    owner = 0U;
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration == nullptr
            || (declaration->form != sv::DeclarationForm::task
                && declaration->form != sv::DeclarationForm::function)) {
            continue;
        }
        const auto callable = hooks_.child_scope(scope.handle, declaration->name);
        if (callable != 0U) {
            walk(declaration->statements, callable, callable);
        }
    }
    return found;
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::immediate_assertions(
    const VpiHirScope& scope)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto container = locate(scope);
    if (!container) {
        return found;
    }
    const std::function<void(const std::vector<semantic::StatementId>&,
        fsim_vpi_handle_v1, fsim_vpi_handle_v1)>
        walk = [&](const std::vector<semantic::StatementId>& statements,
                   const fsim_vpi_handle_v1 within,
                   const fsim_vpi_handle_v1 parent) {
            for (const auto id : statements) {
                const auto* const record = statement(id.value());
                if (record == nullptr) {
                    continue;
                }
                using Statement = sv::StatementKind;
                const auto self = statement_handle(id, within, parent);
                const auto kind = statement_type(*record);
                if (kind == vpiImmediateAssert || kind == vpiImmediateAssume
                    || kind == vpiImmediateCover) {
                    found.push_back(self);
                }
                auto inner = within;
                if ((record->kind == Statement::block
                        || record->kind == Statement::fork)
                    && !record->label.empty()) {
                    if (const auto named
                        = hooks_.child_scope(within, record->label)) {
                        inner = named;
                    }
                }
                walk(record->statements, inner, self);
                walk(record->else_statements, inner, self);
                for (const auto& alternative : record->case_alternatives) {
                    walk(alternative.statements, inner, self);
                }
            }
        };
    for (const auto id : container->processes) {
        const auto* const record = process(id.value());
        // The processes that evaluate concurrent assertions are not the
        // design's statements.
        if (record == nullptr || record->concurrent_assertion) {
            continue;
        }
        walk(record->statements, scope.handle,
            node(Kind::process, id.value(), 0U, scope.handle, scope.handle));
    }
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration == nullptr
            || (declaration->form != sv::DeclarationForm::task
                && declaration->form != sv::DeclarationForm::function)) {
            continue;
        }
        const auto callable = hooks_.child_scope(scope.handle, declaration->name);
        if (callable != 0U) {
            walk(declaration->statements, callable, callable);
        }
    }
    return found;
}

VpiStatementModel::StatementPoint VpiStatementModel::find_statement(
    const std::vector<semantic::StatementId>& statements,
    const fsim_vpi_handle_v1 within, const fsim_vpi_handle_v1 parent,
    const std::string_view file, const std::uint32_t line,
    const std::uint32_t column)
{
    bool pass = false;
    const auto begins_here = [&](const sv::Statement& record) {
        if (!record.source.valid()
            || record.source.value() >= semantics_.source_spans().size()) {
            return false;
        }
        const auto& location = semantics_.source_spans().at(
            record.source.value());
        const auto at = [&](const auto& position) {
            return position.line == line && position.column == column;
        };
        pass = record.kind == sv::StatementKind::loop && !at(location.begin)
            && at(location.end);
        if (!at(location.begin) && !pass) {
            return false;
        }
        const auto* const physical = location.file.valid()
                && location.file.value() < semantics_.source_files().size()
            ? &semantics_.source_files().at(location.file.value()).physical_name
            : nullptr;
        return file.empty() || location.logical_name == file
            || (physical != nullptr && *physical == file);
    };
    fsim_vpi_handle_v1 found { };
    const std::function<bool(const std::vector<semantic::StatementId>&,
        fsim_vpi_handle_v1, fsim_vpi_handle_v1)>
        walk = [&](const std::vector<semantic::StatementId>& items,
                   const fsim_vpi_handle_v1 scope,
                   const fsim_vpi_handle_v1 owner) {
            for (const auto id : items) {
                const auto* const record = statement(id.value());
                if (record == nullptr) {
                    continue;
                }
                using Statement = sv::StatementKind;
                const auto block = record->kind == Statement::block
                    || record->kind == Statement::fork;
                const auto self = statement_handle(id, scope, owner);
                if (!block && begins_here(*record)) {
                    found = self;
                    return true;
                }
                auto inner = scope;
                if (block && !record->label.empty()) {
                    if (const auto named
                        = hooks_.child_scope(scope, record->label)) {
                        inner = named;
                    }
                }
                if (walk(record->statements, inner, self)
                    || walk(record->else_statements, inner, self)) {
                    return true;
                }
                for (const auto& alternative : record->case_alternatives) {
                    if (walk(alternative.statements, inner, self)) {
                        return true;
                    }
                }
            }
            return false;
        };
    return walk(statements, within, parent) ? StatementPoint { found, pass }
                                            : StatementPoint { };
}

fsim_vpi_handle_v1 VpiStatementModel::method_body(
    const semantic::DeclarationId method, const fsim_vpi_handle_v1 scope)
{
    const auto* const record = hir_.find_declaration(method);
    return record == nullptr
        ? fsim_vpi_handle_v1 { }
        : sequence(record->statements, method.value(), body_callable, scope,
              scope);
}

std::vector<fsim_vpi_handle_v1> VpiStatementModel::method_formals(
    const semantic::DeclarationId method, const fsim_vpi_handle_v1 scope)
{
    std::vector<fsim_vpi_handle_v1> found;
    const auto* const record = hir_.find_declaration(method);
    if (record == nullptr || !record->callable) {
        return found;
    }
    for (const auto formal : record->callable->formals) {
        found.push_back(
            node(Kind::io_decl, formal.value(), method.value(), scope, scope));
    }
    return found;
}

fsim_vpi_handle_v1 VpiStatementModel::method_return(
    const semantic::DeclarationId method, const fsim_vpi_handle_v1 scope)
{
    const auto* const record = hir_.find_declaration(method);
    if (record == nullptr || record->form != sv::DeclarationForm::function
        || !record->callable) {
        return fsim_vpi_handle_v1 { };
    }
    const auto word = type_word(record->callable->return_type);
    if (word.empty() || word == "void") {
        return fsim_vpi_handle_v1 { };
    }
    return node(
        Kind::return_variable, method.value(), method.value(), scope, scope);
}

std::optional<VpiStatementModel::DeclaredType>
VpiStatementModel::declaration_type(
    const semantic::DeclarationId declaration) const
{
    const auto* const record = hir_.find_declaration(declaration);
    return record != nullptr && record->type ? declared_type_of(*record->type)
                                             : std::nullopt;
}

VpiStatementModel::StatementPoint VpiStatementModel::method_statement_at(
    const semantic::DeclarationId method, const fsim_vpi_handle_v1 scope,
    const std::string_view file, const std::uint32_t line,
    const std::uint32_t column)
{
    const auto* const record = hir_.find_declaration(method);
    if (record == nullptr || record->statements.empty()) {
        return { };
    }
    // A body of several statements is the method's begin-end, the parent
    // of each (37.41 vpiStmt).
    const auto body = method_body(method, scope);
    return find_statement(record->statements, scope,
        record->statements.size() == 1U ? scope : body, file, line, column);
}

VpiStatementModel::StatementPoint VpiStatementModel::statement_at(
    const VpiHirScope& scope, const std::uint32_t process_id,
    const std::string_view file, const std::uint32_t line,
    const std::uint32_t column)
{
    const auto container = locate(scope);
    if (!container) {
        return { };
    }
    // The event control of `always @(...)` runs at each activation, which
    // is the process's entry point at the process's own span (IEEE
    // 1800-2023 37.44, Table 38-6).
    const auto process_begins_here = [&](const sv::Process& record) {
        if (!record.source.valid()
            || record.source.value() >= semantics_.source_spans().size()) {
            return false;
        }
        const auto& location = semantics_.source_spans().at(
            record.source.value());
        if (location.begin.line != line || location.begin.column != column) {
            return false;
        }
        const auto* const physical = location.file.valid()
                && location.file.value() < semantics_.source_files().size()
            ? &semantics_.source_files().at(location.file.value()).physical_name
            : nullptr;
        return file.empty() || location.logical_name == file
            || (physical != nullptr && *physical == file);
    };
    for (const auto id : container->processes) {
        const auto* const record = process(id.value());
        if (record == nullptr || id.value() != process_id) {
            continue;
        }
        if (!record->sensitivities.empty()
            && (record->kind == sv::ProcessKind::always
                || record->kind == sv::ProcessKind::always_ff
                || record->kind == sv::ProcessKind::always_latch)
            && process_begins_here(*record)) {
            return { node(Kind::process_control, id.value(), 0U, scope.handle,
                         scope.handle, true),
                false };
        }
        if (const auto point = find_statement(record->statements,
                scope.handle,
                node(Kind::process, id.value(), 0U, scope.handle,
                    scope.handle),
                file, line, column);
            point.statement != 0U) {
            return point;
        }
    }
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration == nullptr
            || (declaration->form != sv::DeclarationForm::task
                && declaration->form != sv::DeclarationForm::function)) {
            continue;
        }
        const auto callable = hooks_.child_scope(scope.handle, declaration->name);
        if (callable == 0U) {
            continue;
        }
        if (const auto point = find_statement(declaration->statements,
                callable, callable, file, line, column);
            point.statement != 0U) {
            return point;
        }
    }
    return { };
}

fsim_vpi_handle_v1 VpiStatementModel::sequence(
    const std::vector<semantic::StatementId>& statements,
    const std::uint32_t id, const std::uint32_t owner,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent,
    const bool of_process)
{
    if (statements.empty()) {
        return 0U;
    }
    if (statements.size() == 1U) {
        return statement_handle(statements.front(), scope, parent);
    }
    return node(Kind::body, id, owner, scope, parent, of_process);
}

const std::vector<semantic::StatementId>* VpiStatementModel::body_statements(
    const Node& entry) const
{
    if (entry.aux == body_process) {
        const auto* const record = process(entry.id);
        return record == nullptr ? nullptr : &record->statements;
    }
    if (entry.aux == body_callable) {
        const auto* const record = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        return record == nullptr ? nullptr : &record->statements;
    }
    const auto* const record = statement(entry.id);
    if (record == nullptr) {
        return nullptr;
    }
    if (entry.aux == body_then) {
        return &record->statements;
    }
    if (entry.aux == body_else) {
        return &record->else_statements;
    }
    const auto alternative = entry.aux - body_case;
    return alternative < record->case_alternatives.size()
        ? &record->case_alternatives[alternative].statements
        : nullptr;
}

// The format string a display task was given, rebuilt from its parsed
// form (IEEE 1800-2023 21.2.1); none when it had only values.
std::optional<std::string> VpiStatementModel::format_text(
    const sv::Statement& record) const
{
    if (!system_task_name(record) || record.kind == sv::StatementKind::task_call) {
        return std::nullopt;
    }
    // The literal as written, when the frontend kept the source form (not
    // for generated text such as `__FILE__`, rebuilt below).
    if (record.output_source.size() > 1U) {
        return record.output_source[1];
    }
    if (!record.output_source.empty()
        && record.output_generated_text == sv::GeneratedTextKind::none) {
        return std::nullopt;
    }
    std::string text;
    const auto conversion = [&](const sv::OutputFormat format,
                                const bool left, const bool zero,
                                const bool suppress, const std::uint32_t width,
                                const std::uint32_t precision) {
        text += '%';
        if (left) {
            text += '-';
        }
        if (zero) {
            text += '0';
        }
        if (suppress) {
            text += '0';
        } else if (width != 0U) {
            text += std::to_string(width);
        }
        if (precision != std::numeric_limits<std::uint32_t>::max()) {
            text += '.' + std::to_string(precision);
        }
        static constexpr std::string_view letters = "bhodcsefgmtuzpvl";
        const auto index = static_cast<std::size_t>(format);
        text += index < letters.size() ? letters[index] : 'd';
    };
    bool formatted = !record.output_text.empty()
        || !record.output_trailing_text.empty();
    // One conversion of one value is kept on the statement itself.
    if (record.output_values.empty() && record.output_format) {
        text += record.output_prefix;
        conversion(*record.output_format, record.output_left_justify,
            record.output_zero_pad, record.output_suppress_leading_zero,
            record.output_minimum_width, record.output_precision);
        text += record.output_suffix;
        formatted = true;
    }
    for (const auto& output : record.output_values) {
        text += output.prefix;
        formatted = formatted || !output.prefix.empty()
            || output.suppress_leading_zero || output.minimum_width != 0U
            || output.left_justify || output.zero_pad
            || output.format != sv::OutputFormat::decimal;
        conversion(output.format, output.left_justify, output.zero_pad,
            output.suppress_leading_zero, output.minimum_width,
            output.precision);
    }
    text += record.output_text;
    text += record.output_trailing_text;
    if (!formatted) {
        return std::nullopt;
    }
    return text;
}

PLI_INT32 VpiStatementModel::statement_type(const sv::Statement& record) const
{
    using enum sv::StatementKind;
    switch (record.kind) {
    case assignment:
        return step_statement(record) ? vpiOperation : vpiAssignment;
    case force:
        return vpiForce;
    case release:
        return vpiRelease;
    case conditional:
        // An immediate cover is lowered to a conditional (IEEE 1800-2023
        // 16.3, 37.55).
        if (record.immediate_directive
            == sv::Statement::ImmediateDirective::cover) {
            return vpiImmediateCover;
        }
        return record.else_statements.empty() ? vpiIf : vpiIfElse;
    case selection:
        return vpiCase;
    case loop:
        // A foreach over a string keeps its loop (12.7.3, 37.51).
        if (record.foreach_collection) {
            return vpiForeachStmt;
        }
        if (record.loop_repeat) {
            return vpiRepeat;
        }
        if (record.loop_post_test) {
            return vpiDoWhile;
        }
        if (!record.loop_variable.empty() || record.loop_initial) {
            return vpiFor;
        }
        if (const auto* const condition = record.condition
                ? expression(record.condition->value())
                : nullptr;
            condition == nullptr
            || (condition->kind == sv::ExpressionKind::integer_literal
                && condition->text == "1")) {
            return vpiForever;
        }
        return vpiWhile;
    case break_loop:
        return vpiBreak;
    case continue_loop:
        return vpiContinue;
    case return_statement:
        return vpiReturn;
    case task_call:
        return record.task.spelling.starts_with('$') ? vpiSysTaskCall
                                                     : vpiTaskCall;
    case assertion:
        return record.immediate_directive
                == sv::Statement::ImmediateDirective::assumption
            ? vpiImmediateAssume
            : vpiImmediateAssert;
    case delay_control:
        return vpiDelayControl;
    case event_control:
        return vpiEventControl;
    case wait_statement:
        return vpiWait;
    case event_trigger:
        return vpiEventStmt;
    case fork:
        return record.label.empty() ? vpiFork : vpiNamedFork;
    case block:
        if (record.foreach_collection) {
            return vpiForeachStmt;
        }
        return record.label.empty() ? vpiBegin : vpiNamedBegin;
    case wait_fork:
        return vpiWaitFork;
    case disable_fork:
        return vpiDisableFork;
    case disable:
        return vpiDisable;
    case display:
    case file_close:
    case file_flush:
    case file_display:
    case memory_transfer:
    case monitor_control:
    case pause:
    case finish:
    case exit_program:
    case report:
        return vpiSysTaskCall;
    case container_method:
        return vpiMethodTaskCall;
    case procedural_assign:
        return vpiAssignStmt;
    case deassign:
        return vpiDeassign;
    case wait_order:
        return vpiOrderedWait;
    case null_statement:
        return vpiNullStmt;
    }
    return vpiNullStmt;
}

// The system task a folded statement calls (IEEE 1800-2023 21.2, 21.3,
// 20.2, 20.6); the frontend keeps its form, not its name.
std::optional<std::string> VpiStatementModel::system_task_name(
    const sv::Statement& record) const
{
    using enum sv::StatementKind;
    const auto display_name = [&](const std::string_view prefix) {
        const std::string_view verb = record.output_monitor ? "monitor"
            : record.output_postponed                      ? "strobe"
            : record.output_newline                        ? "display"
                                                           : "write";
        return std::string { "$" } + std::string { prefix }
            + std::string { verb };
    };
    switch (record.kind) {
    case task_call:
        return record.task.spelling;
    case display:
    case file_display:
        if (!record.output_source.empty()) {
            return record.output_source.front();
        }
        return display_name(record.kind == display ? "" : "f");
    case file_close:
        return std::string { "$fclose" };
    case file_flush:
        return std::string { "$fflush" };
    case memory_transfer:
        return std::string { record.memory_write ? "$writemem" : "$readmem" }
            + (record.memory_hex ? "h" : "b");
    case monitor_control:
        return std::string { record.monitor_enabled ? "$monitoron"
                                                    : "$monitoroff" };
    case pause:
        return std::string { "$stop" };
    case finish:
        return std::string { "$finish" };
    case exit_program:
        return std::string { "$exit" };
    // A severity system task (20.10).
    case report:
        switch (record.assertion_severity) {
        case sv::AssertionSeverity::note:
            return std::string { "$info" };
        case sv::AssertionSeverity::warning:
            return std::string { "$warning" };
        case sv::AssertionSeverity::error:
            return std::string { "$error" };
        case sv::AssertionSeverity::failure:
            return std::string { "$fatal" };
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

// A streaming concatenation `{>> n {a, b}}` or `{<< n {a, b}}` (IEEE
// 1800-2023 11.4.14): the frontend spells it as a call whose first operand is
// the slice size (1 when none is written) and the others the streamed items.
static bool streaming_operator(const sv::Expression& record)
{
    return record.kind == sv::ExpressionKind::call
        && (record.text == "@stream-left" || record.text == "@stream-right");
}

PLI_INT32 VpiStatementModel::expression_type(
    const sv::Expression& record) const
{
    using enum sv::ExpressionKind;
    switch (record.kind) {
    case integer_literal:
    case boolean_literal:
    case logic_literal:
    case string_literal:
    case class_null:
        return vpiConstant;
    case call:
        // The frontend spells a conditional operator as a "?:" call (11.4.11).
        if (sv::systemverilog_cast_value_operand(record)
            || (record.text == "?:" && record.operands.size() == 3U)
            || streaming_operator(record)) {
            return vpiOperation;
        }
        return record.text.starts_with('$') ? vpiSysFuncCall : vpiFuncCall;
    case class_method_call:
    case class_static_method_call:
        return vpiMethodFuncCall;
    case index:
        return vpiBitSelect;
    case slice:
        return record.text == "+:" || record.text == "-:"
            ? vpiIndexedPartSelect
            : vpiPartSelect;
    default:
        return vpiOperation;
    }
}

std::optional<VpiStatementModel::VarSelect> VpiStatementModel::var_select(
    const Node& entry, const sv::Expression& record) const
{
    if (record.kind != sv::ExpressionKind::index
        || !hooks_.unpacked_dimensions) {
        return std::nullopt;
    }
    std::vector<semantic::ExpressionId> indices;
    const sv::Expression* current = &record;
    while (current->kind == sv::ExpressionKind::index
        && current->operands.size() >= 2U) {
        indices.push_back(current->operands[1U]);
        current = expression(current->operands.front().value());
        if (current == nullptr || indices.size() > 64U) {
            return std::nullopt;
        }
    }
    if (current->kind != sv::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto array = hooks_.resolve(entry.scope,
        current->referenced_name ? current->referenced_name->spelling
                                 : current->text);
    // The first dimensions of an unpacked array select its elements; any
    // index beyond them is a bit select (IEEE 1800-2023 37.19).
    if (array == 0U || hooks_.unpacked_dimensions(array) < indices.size()) {
        return std::nullopt;
    }
    std::ranges::reverse(indices);
    return VarSelect { array, std::move(indices) };
}

PLI_INT32 VpiStatementModel::select_aware_type(
    const Node& entry, const sv::Expression& record) const
{
    const auto type = expression_type(record);
    return type == vpiBitSelect && var_select(entry, record)
        ? vpiVarSelect
        : type;
}

bool VpiStatementModel::is_statement(const std::uint64_t serial) const
{
    const auto kind = nodes_.at(serial).kind;
    return kind == Kind::statement || kind == Kind::body
        || kind == Kind::process_control || kind == Kind::loop_init
        || kind == Kind::loop_step;
}

PLI_INT32 VpiStatementModel::type(const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    switch (entry.kind) {
    case Kind::process: {
        const auto* const record = process(entry.id);
        if (record == nullptr) {
            return vpiAlways;
        }
        return record->kind == sv::ProcessKind::initial ? vpiInitial
            : record->kind == sv::ProcessKind::final   ? vpiFinal
                                                       : vpiAlways;
    }
    case Kind::process_control:
        return vpiEventControl;
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        return record == nullptr ? vpiNullStmt : statement_type(*record);
    }
    case Kind::assignment_control: {
        const auto* const record = statement(entry.id);
        return record != nullptr
                && record->assignment_control == sv::AssignmentControl::delay
            ? vpiDelayControl
            : vpiEventControl;
    }
    case Kind::case_item:
        return vpiCaseItem;
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        return record == nullptr ? vpiOperation
                                 : select_aware_type(entry, *record);
    }
    case Kind::sensitivity_list:
    case Kind::sensitivity:
    case Kind::assertion_event_list:
    case Kind::assertion_event:
    case Kind::delay_list:
    case Kind::connection_list:
        return vpiOperation;
    case Kind::delay:
    case Kind::specify_delay_value:
        return vpiConstant;
    case Kind::specify_delay_list:
    case Kind::specify_mtm_delay:
    case Kind::synthetic_operation:
        return vpiOperation;
    case Kind::assertion_decl:
        return assertion_entries_.at(entry.id).declaration->kind
                == sv::AssertionDeclarationKind::property
            ? vpiPropertyDecl
            : vpiSequenceDecl;
    case Kind::assertion_formal:
        return assertion_entries_.at(entry.id).declaration->kind
                == sv::AssertionDeclarationKind::property
            ? vpiPropFormalDecl
            : vpiSeqFormalDecl;
    case Kind::assertion_spec:
        return vpiPropertySpec;
    case Kind::let_decl:
        return vpiLetDecl;
    case Kind::let_formal:
        return vpiSeqFormalDecl;
    case Kind::let_expr:
        return vpiLetExpr;
    case Kind::assertion_variable: {
        const auto& variables
            = assertion_entries_.at(entry.id).declaration->local_variables;
        return entry.aux < variables.size()
            ? assertion_variable_type(variables[entry.aux].type_tokens)
            : vpiReg;
    }
    case Kind::assertion_op: {
        const auto& table = assertion_entries_.at(entry.id);
        if (table.tree == nullptr || entry.aux >= table.tree->nodes.size()) {
            return vpiOperation;
        }
        const auto& item = table.tree->nodes[entry.aux];
        using NodeKind = sv::AssertionTreeNode::Kind;
        switch (item.kind) {
        case NodeKind::instance:
            return item.sequence ? vpiSequenceInst : vpiPropertyInst;
        case NodeKind::clocked_property:
            return vpiClockedProp;
        case NodeKind::clocked_sequence:
            return vpiClockedSeq;
        case NodeKind::multiclock_sequence:
            return vpiMulticlockSequenceExpr;
        case NodeKind::case_property:
            return vpiCaseProperty;
        case NodeKind::case_item:
            return vpiCasePropertyItem;
        case NodeKind::operation:
            break;
        }
        return vpiOperation;
    }
    case Kind::cont_assign:
        return vpiContAssign;
    case Kind::alias_stmt:
        return vpiAliasStmt;
    case Kind::cont_assign_bit:
        return vpiContAssignBit;
    case Kind::reference:
        return vpiRefObj;
    case Kind::body:
        return vpiBegin;
    case Kind::null_operation:
        return vpiOperation;
    case Kind::loop_init:
        return vpiAssignment;
    case Kind::loop_step: {
        const auto* const record = statement(entry.id);
        return record != nullptr && step_update(*record) ? vpiOperation
                                                         : vpiAssignment;
    }
    case Kind::format:
        return vpiConstant;
    case Kind::inline_constraint:
        return vpiConstraint;
    case Kind::ref_formal:
        return vpiRefObj;
    case Kind::io_decl:
    case Kind::dpi_io_decl:
        return vpiIODecl;
    case Kind::dpi_return_variable: {
        const auto* const dpi = dpi_import(entry);
        return dpi != nullptr ? dpi_token_shape(dpi->return_type_tokens).variable
                              : vpiReg;
    }
    case Kind::return_variable: {
        const auto* const declaration
            = hir_.find_declaration(semantic::DeclarationId::from_index(entry.id));
        return declaration != nullptr && declaration->callable
            ? variable_type(declaration->callable->return_type)
            : vpiReg;
    }
    case Kind::local_variable: {
        const auto* const declaration
            = hir_.find_declaration(semantic::DeclarationId::from_index(entry.id));
        return declaration != nullptr && declaration->type
            ? variable_type(*declaration->type)
            : vpiReg;
    }
    }
    return vpiOperation;
}

std::optional<semantic::SourceSpanId> VpiStatementModel::assignment_source(
    const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return std::nullopt;
    }
    return source(nodes_[serial]);
}

fsim_vpi_handle_v1 VpiStatementModel::assignment_whole(
    const std::uint64_t serial, const fsim_vpi_handle_v1 handle) const
{
    if (owns(serial) && nodes_[serial].kind == Kind::cont_assign_bit
        && nodes_[serial].parent != 0U) {
        return nodes_[serial].parent;
    }
    return handle;
}

bool VpiStatementModel::assignment_constant(const std::uint64_t serial) const
{
    if (!owns(serial)) {
        return false;
    }
    const auto& entry = nodes_[serial];
    if (entry.kind != Kind::cont_assign && entry.kind != Kind::cont_assign_bit) {
        return false;
    }
    const auto* const record = statement(entry.id);
    if (record == nullptr || !record->value) {
        return false;
    }
    const auto* const value = expression(record->value->value());
    using Form = semantic::sv::ExpressionKind;
    return value != nullptr
        && (value->kind == Form::integer_literal
            || value->kind == Form::logic_literal
            || value->kind == Form::boolean_literal);
}

std::optional<semantic::SourceSpanId> VpiStatementModel::source(
    const Node& entry) const
{
    switch (entry.kind) {
    case Kind::process:
    case Kind::process_control:
        if (const auto* const record = process(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::inline_constraint:
        if (const auto* const record = expression(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::body: {
        const auto* const list = body_statements(entry);
        if (list == nullptr || list->empty()) {
            return std::nullopt;
        }
        if (const auto* const record = statement(list->front().value())) {
            return record->source;
        }
        return std::nullopt;
    }
    case Kind::alias_stmt:
        if (entry.id < alias_entries_.size()) {
            return alias_entries_[entry.id].alias->source;
        }
        return std::nullopt;
    case Kind::statement:
    case Kind::assignment_control:
    case Kind::cont_assign:
    case Kind::cont_assign_bit:
    case Kind::delay:
    case Kind::delay_list:
    case Kind::loop_init:
    case Kind::loop_step:
    case Kind::null_operation:
    case Kind::format:
        if (const auto* const record = statement(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::connection_list:
    case Kind::specify_delay_list:
    case Kind::specify_mtm_delay:
    case Kind::specify_delay_value:
    case Kind::synthetic_operation:
        return std::nullopt;
    case Kind::assertion_decl:
    case Kind::assertion_spec:
        return assertion_entries_.at(entry.id).declaration->source;
    case Kind::assertion_formal: {
        const auto& formals
            = assertion_entries_.at(entry.id).declaration->formals;
        if (entry.aux >= formals.size()) {
            return std::nullopt;
        }
        return formals[entry.aux].source;
    }
    case Kind::assertion_variable: {
        const auto& variables
            = assertion_entries_.at(entry.id).declaration->local_variables;
        if (entry.aux >= variables.size()) {
            return std::nullopt;
        }
        return variables[entry.aux].source;
    }
    case Kind::assertion_op: {
        const auto& table = assertion_entries_.at(entry.id);
        if (table.tree == nullptr || entry.aux >= table.tree->nodes.size()) {
            return std::nullopt;
        }
        return table.tree->nodes[entry.aux].source;
    }
    case Kind::assertion_event_list:
    case Kind::assertion_event: {
        const auto& list = *assertion_events_.at(entry.id);
        if (list.empty()) {
            return std::nullopt;
        }
        return list[entry.kind == Kind::assertion_event
                && entry.aux < list.size()
            ? entry.aux
            : 0U]
            .source;
    }
    case Kind::case_item:
        if (const auto* const record = statement(entry.id);
            record != nullptr && entry.aux < record->case_alternatives.size()) {
            return record->case_alternatives[entry.aux].source;
        }
        return std::nullopt;
    case Kind::let_decl:
        if (entry.id < let_entries_.size()) {
            return let_entries_[entry.id]->source;
        }
        return std::nullopt;
    case Kind::let_formal:
        if (entry.id < let_entries_.size()
            && entry.aux < let_entries_[entry.id]->ports.size()) {
            return let_entries_[entry.id]->ports[entry.aux].source;
        }
        return std::nullopt;
    case Kind::let_expr:
    case Kind::expression:
    case Kind::reference:
        if (const auto* const record = expression(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::io_decl:
    case Kind::ref_formal:
    case Kind::return_variable:
    case Kind::local_variable:
        if (const auto* const declaration = hir_.find_declaration(
                semantic::DeclarationId::from_index(entry.id))) {
            return declaration->source;
        }
        return std::nullopt;
    case Kind::dpi_io_decl:
    case Kind::dpi_return_variable: {
        const auto* const dpi = dpi_import(entry);
        if (dpi == nullptr) {
            return std::nullopt;
        }
        const auto formal = entry.aux & 0xffffU;
        return entry.kind == Kind::dpi_io_decl && formal < dpi->formals.size()
            ? dpi->formals[formal].source
            : dpi->source;
    }
    case Kind::sensitivity_list:
    case Kind::sensitivity: {
        const auto* const list = entry.of_process
            ? (process(entry.id) != nullptr ? &process(entry.id)->sensitivities
                                            : nullptr)
            : (statement(entry.id) != nullptr
                    ? &statement(entry.id)->sensitivities
                    : nullptr);
        if (list == nullptr || list->empty()) {
            return std::nullopt;
        }
        return (*list)[entry.kind == Kind::sensitivity
                && entry.aux < list->size()
            ? entry.aux
            : 0U]
            .source;
    }
    }
    return std::nullopt;
}

std::optional<std::int64_t> VpiStatementModel::property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = nodes_.at(serial);
    if (property == vpiLineNo || property == vpiColumn) {
        const auto span = source(entry);
        if (!span || !span->valid()
            || span->value() >= semantics_.source_spans().size()) {
            return std::nullopt;
        }
        const auto& location = semantics_.source_spans().at(span->value());
        return property == vpiLineNo
            ? static_cast<std::int64_t>(location.begin.line)
            : static_cast<std::int64_t>(location.begin.column);
    }
    // The strength of a sequence expr that is a boolean leaf (37.52
    // detail 3).
    if (property == vpiOpStrong
        && (entry.kind == Kind::reference || entry.kind == Kind::expression)) {
        if (const auto found = operand_strength_.find(entry.id);
            found != operand_strength_.end()) {
            return found->second ? 1 : 0;
        }
    }
    switch (entry.kind) {
    // An inline constraint applies whenever its call runs, and is not
    // virtual (37.34).
    case Kind::inline_constraint:
        if (property == vpiIsConstraintEnabled) {
            return 1;
        }
        if (property == vpiVirtual) {
            return 0;
        }
        return std::nullopt;
    case Kind::process:
        if (property == vpiAlwaysType) {
            const auto* const record = process(entry.id);
            if (record == nullptr || record->kind == sv::ProcessKind::initial
                || record->kind == sv::ProcessKind::final) {
                return std::nullopt;
            }
            return record->kind == sv::ProcessKind::always_comb ? vpiAlwaysComb
                : record->kind == sv::ProcessKind::always_ff    ? vpiAlwaysFF
                : record->kind == sv::ProcessKind::always_latch
                ? vpiAlwaysLatch
                : vpiAlways;
        }
        return std::nullopt;
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        switch (property) {
        case vpiBlocking:
            if (record->kind != sv::StatementKind::assignment
                || step_statement(*record)) {
                return std::nullopt;
            }
            return record->assignment_kind == sv::AssignmentKind::nonblocking
                ? 0
                : 1;
        case vpiOpType:
            if (record->kind != sv::StatementKind::assignment) {
                return std::nullopt;
            }
            if (step_statement(*record)) {
                return step_operator(*record);
            }
            if (!record->update_operator.empty()) {
                auto spelling = std::string_view { record->update_operator };
                if (spelling.ends_with('=')) {
                    spelling.remove_suffix(1U);
                }
                if (const auto type = operator_type(spelling, 2U)) {
                    return *type;
                }
            }
            return vpiAssignmentOp;
        case vpiCaseType:
            if (record->kind != sv::StatementKind::selection) {
                return std::nullopt;
            }
            return record->case_match == sv::CaseMatchKind::wildcard_z
                ? vpiCaseZ
                : record->case_match == sv::CaseMatchKind::wildcard_xz
                ? vpiCaseX
                : vpiCaseExact;
        // Whether an immediate assertion is deferred, and whether it is
        // final (IEEE 1800-2023 37.55).
        case vpiIsDeferred:
        case vpiIsFinal: {
            const auto type = statement_type(*record);
            if (type != vpiImmediateAssert && type != vpiImmediateAssume
                && type != vpiImmediateCover) {
                return std::nullopt;
            }
            // A final deferred assertion is deferred too (16.4).
            return (property == vpiIsDeferred
                        ? record->immediate_deferred || record->immediate_final
                        : record->immediate_final)
                ? 1
                : 0;
        }
        case vpiQualifier:
            if (record->kind != sv::StatementKind::selection
                && record->kind != sv::StatementKind::conditional) {
                return std::nullopt;
            }
            return record->case_qualifier == sv::CaseQualifier::priority
                ? vpiPriorityQualifier
                : record->case_qualifier == sv::CaseQualifier::none
                ? vpiNoQualifier
                : vpiUniqueQualifier;
        // A for loop that declares its variable has local variables (37.74).
        case vpiLocalVarDecls:
            if (statement_type(*record) != vpiFor) {
                return std::nullopt;
            }
            return record->loop_variable_declared ? 1 : 0;
        case vpiJoinType:
            if (record->kind != sv::StatementKind::fork) {
                return std::nullopt;
            }
            return record->fork_join == sv::ForkJoinKind::any ? vpiJoinAny
                : record->fork_join == sv::ForkJoinKind::none ? vpiJoinNone
                                                              : vpiJoin;
        default:
            return std::nullopt;
        }
    }
    case Kind::cont_assign: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        // IEEE 1800-2023 37.46 vpiStrength0/1: the 38.34 strength
        // constants.
        static constexpr PLI_INT32 strengths[] = { vpiHighZ, vpiSmallCharge,
            vpiMediumCharge, vpiWeakDrive, vpiLargeCharge, vpiPullDrive,
            vpiStrongDrive, vpiSupplyDrive };
        if (property == vpiStrength0 || property == vpiStrength1) {
            const auto& level = property == vpiStrength0 ? record->drive_zero
                                                         : record->drive_one;
            return level && *level < std::size(strengths)
                ? strengths[*level]
                : vpiStrongDrive;
        }
        if (property == vpiNetDeclAssign) {
            return declared_net_assignment(*record) ? 1 : 0;
        }
        return std::nullopt;
    }
    // A bit of a continuous assignment is scalar and sits `aux` bits above
    // the LSB (37.47 details 1, 3).
    case Kind::cont_assign_bit:
        if (property == vpiSize) {
            return 1;
        }
        if (property == vpiOffset) {
            return entry.aux;
        }
        return std::nullopt;
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto type = select_aware_type(entry, *record);
        if (property == vpiOpType && type == vpiOperation) {
            using enum sv::ExpressionKind;
            switch (record->kind) {
            case concatenation:
                return vpiConcatOp;
            case replication:
                return vpiMultiConcatOp;
            case assignment_pattern:
                return vpiAssignmentPatternOp;
            case update:
                return record->text == "pre++" ? vpiPreIncOp
                    : record->text == "post++"     ? vpiPostIncOp
                    : record->text == "pre--"      ? vpiPreDecOp
                                                   : vpiPostDecOp;
            case call:
                if (record->text == "@stream-right") {
                    return vpiStreamLROp;
                }
                if (record->text == "@stream-left") {
                    return vpiStreamRLOp;
                }
                return record->text == "?:" ? vpiConditionOp : vpiCastOp;
            default:
                if (const auto op
                    = operator_type(record->text, record->operands.size())) {
                    return *op;
                }
                return vpiNullOp;
            }
        }
        if (property == vpiConstType && type == vpiConstant) {
            if (record->kind == sv::ExpressionKind::string_literal) {
                return vpiStringConst;
            }
            if (record->kind == sv::ExpressionKind::class_null) {
                return vpiNullConst;
            }
            if (record->decimal_literal) {
                return record->decimal_literal->kind
                        == sv::DecimalLiteralKind::time
                    ? vpiTimeConst
                    : vpiRealConst;
            }
            const auto quote = record->text.find('\'');
            if (quote == std::string::npos) {
                return vpiIntConst;
            }
            auto rest = std::string_view { record->text }.substr(quote + 1U);
            if (!rest.empty() && (rest.front() == 's' || rest.front() == 'S')) {
                rest.remove_prefix(1U);
            }
            const auto base = rest.empty() ? 'd'
                                           : static_cast<char>(std::tolower(
                                               static_cast<unsigned char>(
                                                   rest.front())));
            return base == 'b' ? vpiBinaryConst
                : base == 'o'  ? vpiOctConst
                : base == 'h'  ? vpiHexConst
                : base == 'd'  ? vpiDecConst
                               : vpiBinaryConst;
        }
        if (property == vpiSize && type == vpiConstant) {
            if (const auto constant = value(serial);
                constant && constant->kind == VpiStatementValue::Kind::bits) {
                return static_cast<std::int64_t>(constant->bits.width());
            }
            if (record->kind == sv::ExpressionKind::string_literal) {
                return static_cast<std::int64_t>(
                    8U * record->decoded_string.value_or(record->text).size());
            }
            return std::nullopt;
        }
        if (property == vpiIndexedPartSelectType
            && type == vpiIndexedPartSelect) {
            return record->text == "+:" ? vpiPosIndexed : vpiNegIndexed;
        }
        if (property == vpiConstantSelect && type == vpiVarSelect) {
            // 37.19 detail 1: every index of the select is constant.
            const auto select = var_select(entry, *record);
            if (!select) {
                return std::nullopt;
            }
            for (const auto index : select->indices) {
                const auto* const operand = expression(index.value());
                if (operand == nullptr
                    || operand->kind != sv::ExpressionKind::integer_literal) {
                    return 0;
                }
            }
            return 1;
        }
        if (property == vpiConstantSelect
            && (type == vpiBitSelect || type == vpiPartSelect
                || type == vpiIndexedPartSelect)) {
            for (std::size_t index = 1U; index < record->operands.size();
                 ++index) {
                const auto* const operand
                    = expression(record->operands[index].value());
                if (operand == nullptr
                    || operand->kind != sv::ExpressionKind::integer_literal) {
                    return 0;
                }
            }
            return 1;
        }
        return std::nullopt;
    }
    case Kind::ref_formal:
    case Kind::io_decl: {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        if (declaration == nullptr) {
            return std::nullopt;
        }
        // A ref obj has no direction (37.15).
        if (property == vpiDirection && entry.kind == Kind::ref_formal) {
            return std::nullopt;
        }
        if (property == vpiDirection) {
            switch (declaration->direction) {
            case sv::Direction::output:
                return vpiOutput;
            case sv::Direction::inout:
                return vpiInout;
            case sv::Direction::ref:
                return vpiRef;
            default:
                // A formal without a direction is an input (13.3, 13.4).
                return vpiInput;
            }
        }
        if (!declaration->type) {
            return std::nullopt;
        }
        // A formal of a fixed-size unpacked array type has the number of
        // the array's elements as its size and is not signed (37.17 details
        // 4 and 9, 37.41).
        if (declaration->type->container_form == sv::TypeForm::static_array
            || (!declaration->type->container_form
                && !declaration->type->unpacked_dimensions.empty())) {
            if (property == vpiSigned) {
                return 0;
            }
            if (property != vpiSize) {
                return std::nullopt;
            }
            if (declaration->type->unpacked_dimensions.empty()) {
                return std::nullopt;
            }
            std::int64_t count = 1;
            for (const auto& range : declaration->type->unpacked_dimensions) {
                const auto extent = range_extent(range);
                if (!extent) {
                    return std::nullopt;
                }
                count *= *extent;
            }
            return count;
        }
        if (property == vpiSize) {
            return type_width(*declaration->type);
        }
        if (property == vpiSigned) {
            return type_signed(*declaration->type) ? 1 : 0;
        }
        return std::nullopt;
    }
    case Kind::dpi_io_decl: {
        const auto* const dpi = dpi_import(entry);
        const auto formal = entry.aux & 0xffffU;
        if (dpi == nullptr || formal >= dpi->formals.size()) {
            return std::nullopt;
        }
        const auto& declared = dpi->formals[formal];
        if (property == vpiDirection) {
            switch (declared.direction) {
            case sv::Direction::output:
                return vpiOutput;
            case sv::Direction::inout:
                return vpiInout;
            case sv::Direction::ref:
                return vpiRef;
            default:
                // A formal without a direction is an input (35.5.4).
                return vpiInput;
            }
        }
        // An unpacked formal has the size of the whole array, which is not
        // known from the tokens.
        if (!declared.dimension_tokens.empty()) {
            return std::nullopt;
        }
        const auto shape = dpi_token_shape(declared.type_tokens);
        // A formal of a named type (a typedef, struct or enum) has the width
        // its resolved type carries (37.41 detail 12); a string or chandle
        // has no size that follows from the declaration.
        const auto* const resolved = dpi->resolved_profile
                && formal < dpi->resolved_profile->formals.size()
            ? &dpi->resolved_profile->formals[formal].type
            : nullptr;
        if (property == vpiSize) {
            if (shape.size) {
                return shape.size;
            }
            return resolved != nullptr ? type_width(*resolved) : std::nullopt;
        }
        if (property == vpiSigned) {
            if (shape.size) {
                return shape.is_signed ? 1 : 0;
            }
            return resolved != nullptr && type_width(*resolved)
                ? std::optional<std::int64_t> { type_signed(*resolved) ? 1 : 0 }
                : std::nullopt;
        }
        return std::nullopt;
    }
    case Kind::dpi_return_variable: {
        const auto* const dpi = dpi_import(entry);
        if (dpi == nullptr) {
            return std::nullopt;
        }
        const auto shape = dpi_token_shape(dpi->return_type_tokens);
        const auto* const resolved = dpi->resolved_profile
                && dpi->resolved_profile->return_type
            ? &*dpi->resolved_profile->return_type
            : nullptr;
        if (property == vpiSize) {
            if (shape.size) {
                return shape.size;
            }
            return resolved != nullptr ? type_width(*resolved) : std::nullopt;
        }
        if (property == vpiSigned) {
            if (shape.size) {
                return shape.is_signed ? 1 : 0;
            }
            return resolved != nullptr && type_width(*resolved)
                ? std::optional<std::int64_t> { type_signed(*resolved) ? 1 : 0 }
                : std::nullopt;
        }
        return std::nullopt;
    }
    case Kind::return_variable: {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        if (declaration == nullptr || !declaration->callable) {
            return std::nullopt;
        }
        const auto& type = declaration->callable->return_type;
        if (property == vpiSize) {
            return type_width(type);
        }
        if (property == vpiSigned) {
            return type_signed(type) ? 1 : 0;
        }
        return std::nullopt;
    }
    // A for loop's own variable is automatic (IEEE 1800-2023 12.7.1), as
    // are the formals and locals of a class method (8.6).
    case Kind::local_variable: {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        if (declaration == nullptr || !declaration->type) {
            return std::nullopt;
        }
        if (property == vpiIsModPort && declaration->type->virtual_interface) {
            return declaration->type->interface_modport.empty() ? 0 : 1;
        }
        if (property == vpiSize) {
            return type_width(*declaration->type);
        }
        if (property == vpiSigned) {
            return type_signed(*declaration->type) ? 1 : 0;
        }
        if (property == vpiAutomatic) {
            return 1;
        }
        return std::nullopt;
    }
    case Kind::sensitivity_list:
        if (property == vpiOpType) {
            return vpiEventOrOp;
        }
        return std::nullopt;
    case Kind::delay_list:
    case Kind::connection_list:
    case Kind::specify_delay_list:
        if (property == vpiOpType) {
            return vpiListOp;
        }
        return std::nullopt;
    case Kind::specify_mtm_delay:
        if (property == vpiOpType) {
            return vpiMinTypMaxOp;
        }
        return std::nullopt;
    case Kind::synthetic_operation:
        if (property == vpiOpType) {
            return static_cast<std::int64_t>(entry.aux);
        }
        return std::nullopt;
    case Kind::specify_delay_value:
        if (property == vpiConstType) {
            return vpiDecConst;
        }
        if (property == vpiSize) {
            return 64;
        }
        return std::nullopt;
    case Kind::assertion_op: {
        const auto& table = assertion_entries_.at(entry.id);
        if (table.tree == nullptr || entry.aux >= table.tree->nodes.size()) {
            return std::nullopt;
        }
        const auto& item = table.tree->nodes[entry.aux];
        if (item.kind != sv::AssertionTreeNode::Kind::operation) {
            return std::nullopt;
        }
        // vpiOpStrong tells the strong version of a temporal operator
        // (37.52 detail 3).
        if (property == vpiOpType) {
            return assertion_op_type(item.op);
        }
        if (property == vpiOpStrong) {
            return item.strong ? 1 : 0;
        }
        return std::nullopt;
    }
    case Kind::let_formal:
        // A let formal has no direction (11.12, 37.53 seq formal decl).
        return property == vpiDirection
            ? std::optional<std::int64_t> { vpiNoDirection }
            : std::nullopt;
    case Kind::assertion_formal: {
        const auto& table = assertion_entries_.at(entry.id);
        const auto& formals = table.declaration->formals;
        if (property != vpiDirection || entry.aux >= formals.size()) {
            return std::nullopt;
        }
        // vpiNoDirection unless the formal is a local variable argument
        // (37.51 detail 5, 37.53 detail 4).
        const auto& formal = formals[entry.aux];
        if (!formal.local) {
            return vpiNoDirection;
        }
        return formal.direction == semantic::sv::Direction::output ? vpiOutput
            : formal.direction == semantic::sv::Direction::inout   ? vpiInout
                                                              : vpiInput;
    }
    case Kind::assertion_event_list:
        if (property == vpiOpType) {
            return vpiEventOrOp;
        }
        return std::nullopt;
    case Kind::assertion_event: {
        if (property != vpiOpType) {
            return std::nullopt;
        }
        const auto& list = *assertion_events_.at(entry.id);
        return entry.aux < list.size()
                && list[entry.aux].edge == sv::EdgeKind::negative
            ? vpiNegedgeOp
            : vpiPosedgeOp;
    }
    case Kind::sensitivity: {
        if (property != vpiOpType) {
            return std::nullopt;
        }
        const auto* const list = entry.of_process
            ? &process(entry.id)->sensitivities
            : &statement(entry.id)->sensitivities;
        const auto edge = entry.aux < list->size() ? (*list)[entry.aux].edge
                                                   : sv::EdgeKind::any;
        return edge == sv::EdgeKind::positive ? vpiPosedgeOp : vpiNegedgeOp;
    }
    case Kind::null_operation:
        return property == vpiOpType ? std::optional<std::int64_t> { vpiNullOp }
                                     : std::nullopt;
    case Kind::loop_init:
    case Kind::loop_step:
        if (entry.kind == Kind::loop_step) {
            if (const auto* const record = statement(entry.id);
                record != nullptr && step_update(*record)) {
                return property == vpiOpType
                    ? std::optional<std::int64_t> { step_operator(*record) }
                    : std::nullopt;
            }
        }
        if (property == vpiBlocking) {
            return 1;
        }
        if (property == vpiOpType) {
            return vpiAssignmentOp;
        }
        return std::nullopt;
    case Kind::format:
        if (property == vpiConstType) {
            return vpiStringConst;
        }
        if (property == vpiSize) {
            const auto* const record = statement(entry.id);
            const auto text = record ? format_text(*record) : std::nullopt;
            return text ? static_cast<std::int64_t>(8U * text->size())
                        : std::int64_t { };
        }
        return std::nullopt;
    case Kind::delay:
        if (property == vpiConstType) {
            return vpiDecConst;
        }
        if (property == vpiSize) {
            return 64;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<std::string> VpiStatementModel::string_property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = nodes_.at(serial);
    if (property == vpiFile) {
        const auto span = source(entry);
        if (!span || !span->valid()
            || span->value() >= semantics_.source_spans().size()) {
            return std::nullopt;
        }
        const auto& location = semantics_.source_spans().at(span->value());
        if (!location.logical_name.empty()) {
            return location.logical_name;
        }
        if (location.file.valid()
            && location.file.value() < semantics_.source_files().size()) {
            return semantics_.source_files().at(location.file.value())
                .physical_name;
        }
        return std::nullopt;
    }
    // Every expression has its source form (37.59 detail 2).
    if (property == vpiDecompile
        && (entry.kind == Kind::expression || entry.kind == Kind::reference
            || entry.kind == Kind::let_expr)) {
        return decompile(semantic::ExpressionId::from_index(entry.id), nullptr);
    }
    switch (entry.kind) {
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        if (property == vpiName) {
            if (const auto name = system_task_name(*record)) {
                return name;
            }
            // A labelled immediate assertion is named by its label (IEEE
            // 1800-2023 16.3, 37.49 vpiName).
            if (record->immediate_directive
                    != sv::Statement::ImmediateDirective::none
                && !record->immediate_label.empty()) {
                return record->immediate_label;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto type = select_aware_type(entry, *record);
        if (property == vpiName
            && (type == vpiSysFuncCall || type == vpiFuncCall
                || type == vpiMethodFuncCall)) {
            // A call of another instance's function is named as written,
            // its prefix and the function (`a.twice`, IEEE 1800-2023 23.8).
            if (record->kind == sv::ExpressionKind::call
                && record->text.starts_with('.') && !record->operands.empty()
                && call_callee(entry.id, entry.scope) != 0U) {
                const auto* const prefix
                    = expression(record->operands.front().value());
                if (prefix != nullptr
                    && prefix->kind == sv::ExpressionKind::name) {
                    return prefix->text + record->text;
                }
            }
            return record->text;
        }
        return std::nullopt;
    }
    case Kind::let_decl:
    case Kind::let_formal: {
        if (entry.id >= let_entries_.size()) {
            return std::nullopt;
        }
        const auto& let = *let_entries_[entry.id];
        const bool formal = entry.kind == Kind::let_formal;
        if (formal && entry.aux >= let.ports.size()) {
            return std::nullopt;
        }
        const auto& name = formal ? let.ports[entry.aux].name : let.name;
        if (property == vpiName) {
            return name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "." + let.name
                + (formal ? "." + name : std::string { });
        }
        return std::nullopt;
    }
    case Kind::assertion_decl:
    case Kind::assertion_formal: {
        const auto& declaration = *assertion_entries_.at(entry.id).declaration;
        const auto name = entry.kind == Kind::assertion_formal
            ? (entry.aux < declaration.formals.size()
                    ? declaration.formals[entry.aux].name
                    : std::string { })
            : declaration.name;
        // A declaration a checker instance specialized into the scope has
        // its name as written in the checker, under the instance's path
        // (17.3, 37.53).
        const auto& instance = declaration.checker_instance;
        const bool expanded = !instance.empty();
        const auto& declared
            = expanded ? declaration.checker_name : declaration.name;
        if (property == vpiName) {
            return entry.kind == Kind::assertion_decl ? declared : name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "."
                + (expanded ? instance + "." : std::string { }) + declared
                + (entry.kind == Kind::assertion_formal ? "." + name
                                                         : std::string { });
        }
        return std::nullopt;
    }
    case Kind::assertion_variable: {
        const auto& declaration = *assertion_entries_.at(entry.id).declaration;
        if (entry.aux >= declaration.local_variables.size()) {
            return std::nullopt;
        }
        const auto& name = declaration.local_variables[entry.aux].name;
        if (property == vpiName) {
            return name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "." + declaration.name + "."
                + name;
        }
        return std::nullopt;
    }
    case Kind::assertion_op: {
        const auto& table = assertion_entries_.at(entry.id);
        if (property != vpiName || table.tree == nullptr
            || entry.aux >= table.tree->nodes.size()
            || table.tree->nodes[entry.aux].kind
                != sv::AssertionTreeNode::Kind::instance) {
            return std::nullopt;
        }
        return table.tree->nodes[entry.aux].instance;
    }
    case Kind::reference: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto& name = record->referenced_name
            ? record->referenced_name->spelling
            : record->text;
        if (property == vpiName) {
            return name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "." + name;
        }
        return std::nullopt;
    }
    // A formal is named by its declaration, the return variable by its
    // function (37.41 detail 1).
    case Kind::io_decl:
    case Kind::ref_formal:
    case Kind::return_variable:
    case Kind::local_variable: {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(
                entry.kind != Kind::return_variable ? entry.id : entry.aux));
        if (declaration == nullptr) {
            return std::nullopt;
        }
        if (property == vpiName) {
            return declaration->name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "." + declaration->name;
        }
        return std::nullopt;
    }
    // A DPI import's formal is named by its declaration, the result
    // variable by the import (37.41 detail 1).
    case Kind::dpi_io_decl:
    case Kind::dpi_return_variable: {
        const auto* const dpi = dpi_import(entry);
        if (dpi == nullptr || (property != vpiName && property != vpiFullName)) {
            return std::nullopt;
        }
        const auto formal = entry.aux & 0xffffU;
        std::string name = dpi->systemverilog_name;
        if (entry.kind == Kind::dpi_io_decl) {
            if (formal >= dpi->formals.size()) {
                return std::nullopt;
            }
            name = dpi->formals[formal].name;
        }
        return property == vpiName
            ? name
            : hooks_.full_name(entry.scope) + "." + name;
    }
    case Kind::format:
        if (property == vpiDecompile) {
            if (const auto* const record = statement(entry.id)) {
                if (const auto text = format_text(*record)) {
                    return "\"" + *text + "\"";
                }
            }
        }
        return std::nullopt;
    case Kind::specify_delay_value:
        if (property == vpiDecompile) {
            if (const auto* const value = specify_value(entry)) {
                return std::to_string(value->magnitude)
                    + (value->divisor > 1U
                            ? "/" + std::to_string(value->divisor)
                            : std::string { })
                    + value->unit;
            }
        }
        return std::nullopt;
    case Kind::delay:
        if (property == vpiDecompile) {
            if (const auto* const record = statement(entry.id);
                record != nullptr && record->delay
                && delay_value(*record->delay, entry.aux) != nullptr) {
                const auto& primary = *delay_value(*record->delay, entry.aux);
                return std::to_string(primary.magnitude)
                    + (primary.divisor > 1U
                            ? "/" + std::to_string(primary.divisor)
                            : std::string { })
                    + primary.unit;
            }
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<fsim_vpi_handle_v1> VpiStatementModel::relation(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = nodes_.at(serial);
    const auto self = hooks_.handle(serial);
    const FrameRestore restore { frame_,
        std::exchange(frame_, entry.kind == Kind::expression
                || entry.kind == Kind::reference || entry.kind == Kind::let_expr
            ? entry.aux
            : 0U) };
    // A formal used in a let declaration's body is a ref obj of the formal
    // decl (IEEE 1800-2023 37.57).
    if (entry.kind == Kind::reference && type == vpiActual && entry.aux != 0U
        && entry.aux < let_frames_.size() && !let_frames_[entry.aux].use) {
        const auto frame = let_frames_[entry.aux];
        const auto* const record = expression(entry.id);
        const auto& ports = frame.declaration->ports;
        const auto port = record == nullptr ? ports.end()
                                            : std::ranges::find(ports,
                                                  record->text,
                                                  &sv::LetPort::name);
        return port == ports.end()
            ? fsim_vpi_handle_v1 { }
            : node(Kind::let_formal, let_entry(*frame.declaration),
                  static_cast<std::uint32_t>(port - ports.begin()),
                  entry.scope, entry.scope);
    }
    // A let declaration has its body, a formal its default and its
    // declaration, a let expression the declaration it names (37.57).
    if (entry.kind == Kind::let_decl || entry.kind == Kind::let_formal
        || entry.kind == Kind::let_expr) {
        if (type == vpiParent) {
            return entry.kind == Kind::let_formal
                ? node(Kind::let_decl, entry.id, 0U, entry.scope, entry.scope)
                : entry.parent;
        }
        if (entry.kind == Kind::let_expr) {
            const auto* const declaration = let_of_use(entry);
            return type == vpiLetDecl && declaration != nullptr
                ? node(Kind::let_decl, let_entry(*declaration), 0U,
                      entry.scope, entry.scope)
                : fsim_vpi_handle_v1 { };
        }
        if (type != vpiExpr || entry.id >= let_entries_.size()) {
            return fsim_vpi_handle_v1 { };
        }
        const auto& let = *let_entries_[entry.id];
        if (entry.kind == Kind::let_formal) {
            return entry.aux < let.ports.size()
                    && let.ports[entry.aux].default_value
                ? expression_handle(
                      *let.ports[entry.aux].default_value, entry.scope, self)
                : fsim_vpi_handle_v1 { };
        }
        frame_ = let_frame(let, std::nullopt, 0U, entry.scope);
        return expression_handle(let.expression, entry.scope, self);
    }
    // The io decl of a `ref` formal has its ref obj as the expression (IEEE
    // 1800-2023 37.14 detail 2, 37.15 detail 2); a ref obj of a formal has no
    // parent, which only a sub-element of a ref obj has (37.15 detail 4).
    if (entry.kind == Kind::io_decl && type == vpiExpr) {
        const auto* const declaration = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        return declaration != nullptr
                && declaration->direction == sv::Direction::ref
            ? node(Kind::ref_formal, entry.id, entry.aux, entry.scope, self)
            : fsim_vpi_handle_v1 { };
    }
    if (entry.kind == Kind::ref_formal && type == vpiParent) {
        return fsim_vpi_handle_v1 { };
    }
    // The object a reference names, when a scope holds it (37.15): a
    // sequence operand that is a plain name with match items is a ref obj
    // of its own (assertion_operand).
    if (entry.kind == Kind::reference && type == vpiActual) {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        // A local variable or local formal argument of the property or
        // sequence declaration whose expression the name is in (16.10,
        // 37.51, 37.53): reached through the operands and match items up to
        // the declaration's tree.
        for (auto parent = entry.parent; parent != 0U;) {
            const auto found = serial_of_.find(parent);
            if (found == serial_of_.end()) {
                break;
            }
            const auto& above = nodes_[found->second];
            if (above.kind == Kind::assertion_op
                || above.kind == Kind::assertion_spec
                || above.kind == Kind::assertion_decl) {
                const auto& declaration
                    = *assertion_entries_.at(above.id).declaration;
                const auto& locals = declaration.local_variables;
                const auto local = std::ranges::find(
                    locals, record->text, &sv::AssertionLocalVariable::name);
                if (local != locals.end()) {
                    return node(Kind::assertion_variable, above.id,
                        static_cast<std::uint32_t>(local - locals.begin()),
                        above.scope, above.scope);
                }
                const auto& formals = declaration.formals;
                const auto formal = std::ranges::find_if(
                    formals, [&](const sv::AssertionFormal& item) {
                        return item.local && item.name == record->text;
                    });
                if (formal != formals.end()) {
                    return node(Kind::assertion_formal, above.id,
                        static_cast<std::uint32_t>(formal - formals.begin()),
                        above.scope, above.scope);
                }
                break;
            }
            if (above.kind != Kind::expression && above.kind != Kind::reference
                && above.kind != Kind::statement) {
                break;
            }
            parent = above.parent;
        }
        if (record->referenced_name && record->referenced_name->selected
            && hooks_.declared) {
            if (const auto object
                = hooks_.declared(*record->referenced_name->selected)) {
                return object;
            }
        }
        return hooks_.resolve(entry.scope,
            record->referenced_name ? record->referenced_name->spelling
                                    : record->text);
    }
    // Property and sequence declarations, their formals and specs, and the
    // operations and instances of their expressions (IEEE 1800-2023
    // 37.50-37.54).
    if (entry.kind == Kind::assertion_decl || entry.kind == Kind::assertion_formal
        || entry.kind == Kind::assertion_variable
        || entry.kind == Kind::assertion_spec || entry.kind == Kind::assertion_op) {
        const auto& table = assertion_entries_.at(entry.id);
        const auto& declaration = *table.declaration;
        const auto disable = [&]() -> fsim_vpi_handle_v1 {
            return declaration.disable && declaration.disable->condition
                ? expression_handle(
                      *declaration.disable->condition, entry.scope, self)
                : fsim_vpi_handle_v1 { };
        };
        const auto root = [&]() -> fsim_vpi_handle_v1 {
            return table.tree != nullptr
                ? assertion_operand(
                      entry.id, table.tree->root, entry.scope, self)
                : fsim_vpi_handle_v1 { };
        };
        if (type == vpiParent) {
            switch (entry.kind) {
            case Kind::assertion_formal:
            case Kind::assertion_variable:
            case Kind::assertion_spec:
                return assertion_decl_handle(
                    declaration, *table.unit, entry.scope);
            case Kind::assertion_op:
                return entry.parent;
            default:
                return entry.scope;
            }
        }
        switch (entry.kind) {
        case Kind::assertion_decl:
            if (type == vpiPropertySpec
                && declaration.kind == sv::AssertionDeclarationKind::property) {
                return node(Kind::assertion_spec, entry.id, 0U, entry.scope, self);
            }
            if (type == vpiDisableCondition) {
                return disable();
            }
            if (type == vpiExpr
                && declaration.kind == sv::AssertionDeclarationKind::sequence) {
                return root();
            }
            return fsim_vpi_handle_v1 { };
        case Kind::assertion_formal:
            // The initialization expression; no typespec is kept for a
            // formal (37.51 detail 3, 37.53 detail 2).
            if (type == vpiExpr && table.tree != nullptr
                && entry.aux < table.tree->formal_defaults.size()
                && table.tree->formal_defaults[entry.aux]) {
                return expression_handle(
                    *table.tree->formal_defaults[entry.aux], entry.scope, self);
            }
            return fsim_vpi_handle_v1 { };
        case Kind::assertion_variable:
            return fsim_vpi_handle_v1 { };
        case Kind::assertion_spec:
            if (type == vpiClockingEvent && declaration.clock) {
                return assertion_clocking_event(
                    declaration.clock->events, entry.scope, self);
            }
            if (type == vpiDisableCondition) {
                return disable();
            }
            if (type == vpiPropertyExpr) {
                return root();
            }
            return fsim_vpi_handle_v1 { };
        default: {
            using NodeKind = sv::AssertionTreeNode::Kind;
            if (table.tree != nullptr && entry.aux < table.tree->nodes.size()) {
                const auto& item = table.tree->nodes[entry.aux];
                const auto operand = [&](const std::size_t position)
                    -> fsim_vpi_handle_v1 {
                    return position < item.operands.size()
                        ? assertion_operand(entry.id, item.operands[position],
                              entry.scope, self)
                        : fsim_vpi_handle_v1 { };
                };
                switch (item.kind) {
                // A clocking event and its property or sequence (37.52,
                // 37.56).
                case NodeKind::clocked_property:
                case NodeKind::clocked_sequence:
                    if (type == vpiClockingEvent && !item.clock_events.empty()) {
                        return assertion_clocking_event(
                            item.clock_events, entry.scope, self);
                    }
                    if (type
                        == (item.kind == NodeKind::clocked_property
                                ? vpiPropertyExpr
                                : vpiExpr)) {
                        return operand(0U);
                    }
                    return fsim_vpi_handle_v1 { };
                // The case expression, and the property of an item (37.52).
                case NodeKind::case_property:
                    return type == vpiCondition ? operand(0U)
                                                : fsim_vpi_handle_v1 { };
                case NodeKind::case_item:
                    return type == vpiPropertyExpr
                        ? operand(item.operands.size() - 1U)
                        : fsim_vpi_handle_v1 { };
                case NodeKind::multiclock_sequence:
                    return fsim_vpi_handle_v1 { };
                case NodeKind::operation:
                case NodeKind::instance:
                    break;
                }
            }
            // An instance: the declaration it instantiates.
            if ((type == vpiPropertyDecl || type == vpiSequenceDecl)
                && table.tree != nullptr && entry.aux < table.tree->nodes.size()
                && table.tree->nodes[entry.aux].kind
                    == sv::AssertionTreeNode::Kind::instance) {
                if (const auto* const named = sv::find_assertion_declaration(
                        *table.unit, table.tree->nodes[entry.aux].instance)) {
                    return assertion_decl_handle(
                        *named, *table.unit, entry.scope);
                }
            }
            return fsim_vpi_handle_v1 { };
        }
        }
    }
    // A for loop's own variable has the loop as its scope (37.12 detail 2),
    // a class method's variable the method, and neither has another
    // relation of the statement model.
    if (entry.kind == Kind::local_variable) {
        return type == vpiScope ? entry.parent : fsim_vpi_handle_v1 { };
    }
    if (type == vpiScope) {
        return entry.scope;
    }
    if (type == vpiModule) {
        return std::nullopt;
    }
    if (type == vpiParent) {
        // A select's parent is the object or expression selected (IEEE
        // 1800-2023 37.19, 37.58, 37.59 details 10); a name that refers to
        // no object published has the scope or the object its prefix names
        // (37.15 detail 4); anything else the construct that holds it.
        if (entry.kind == Kind::expression) {
            if (const auto* const record = expression(entry.id)) {
                const auto kind = select_aware_type(entry, *record);
                if (kind == vpiVarSelect) {
                    if (const auto select = var_select(entry, *record)) {
                        return select->array;
                    }
                } else if ((kind == vpiBitSelect || kind == vpiPartSelect
                               || kind == vpiIndexedPartSelect)
                    && !record->operands.empty()) {
                    return expression_handle(
                        record->operands.front(), entry.scope, self);
                }
            }
        } else if (entry.kind == Kind::reference) {
            if (const auto* const record = expression(entry.id)) {
                const auto& name = record->referenced_name
                    ? record->referenced_name->spelling
                    : record->text;
                const auto dot = name.rfind('.');
                if (dot != std::string::npos) {
                    if (const auto prefix
                        = hooks_.resolve(entry.scope, name.substr(0U, dot))) {
                        return prefix;
                    }
                }
            }
            return entry.scope;
        }
        return entry.parent;
    }
    const auto expression_at = [&](const std::optional<semantic::ExpressionId>
                                       id) -> fsim_vpi_handle_v1 {
        return id ? expression_handle(*id, entry.scope, self) : 0U;
    };
    switch (entry.kind) {
    case Kind::process: {
        const auto* const record = process(entry.id);
        if (record == nullptr || type != vpiStmt) {
            return fsim_vpi_handle_v1 { };
        }
        // `always @(...) stmt`: the event control is the process's
        // statement (IEEE 1800-2023 37.44).
        if (!record->sensitivities.empty()
            && (record->kind == sv::ProcessKind::always
                || record->kind == sv::ProcessKind::always_ff
                || record->kind == sv::ProcessKind::always_latch)) {
            return node(Kind::process_control, entry.id, 0U, entry.scope, self,
                true);
        }
        return sequence(record->statements, entry.id, body_process,
            entry.scope, self, true);
    }
    case Kind::process_control: {
        const auto* const record = process(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiStmt) {
            return sequence(record->statements, entry.id, body_process,
                entry.scope, self, true);
        }
        if (type == vpiCondition) {
            return record->sensitivities.size() == 1U
                ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self, true)
                : node(Kind::sensitivity_list, entry.id, 0U, entry.scope, self,
                    true);
        }
        return fsim_vpi_handle_v1 { };
    }
    // The aliased terminal and the terminal it is aliased to (37.76).
    case Kind::alias_stmt: {
        if ((type != vpiLhs && type != vpiRhs) || entry.id >= alias_entries_.size()) {
            return fsim_vpi_handle_v1 { };
        }
        const auto& alias = alias_entries_[entry.id];
        return expression_handle(type == vpiLhs
                ? alias.alias->terminals[alias.terminal]
                : alias.alias->terminals.back(),
            entry.scope, self);
    }
    case Kind::statement:
    case Kind::cont_assign: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        using enum sv::StatementKind;
        // Only assignments have sides (IEEE 1800-2023 37.45-37.47); a
        // loop keeps its step there.
        const auto assigns = (record->kind == assignment
                                 && !step_statement(*record))
            || record->kind == force || record->kind == procedural_assign;
        switch (type) {
        case vpiLhs:
            return assigns || record->kind == release || record->kind == deassign
                ? expression_at(record->target)
                : 0U;
        case vpiRhs:
            return assigns ? expression_at(record->value) : 0U;
        case vpiForInitStmt:
            return statement_type(*record) == vpiFor && record->loop_initial
                ? node(Kind::loop_init, entry.id, 0U, entry.scope, self)
                : 0U;
        case vpiForIncStmt:
            if (statement_type(*record) != vpiFor) {
                return fsim_vpi_handle_v1 { };
            }
            if (!record->loop_updates.empty()) {
                return statement_handle(
                    record->loop_updates.front(), entry.scope, self);
            }
            return record->target && record->value
                ? node(Kind::loop_step, entry.id, 0U, entry.scope, self)
                : 0U;
        case vpiCondition:
            if (record->kind == event_control) {
                if (record->sensitivities.empty()) {
                    return fsim_vpi_handle_v1 { };
                }
                return record->sensitivities.size() == 1U
                    ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self)
                    : node(Kind::sensitivity_list, entry.id, 0U, entry.scope,
                        self);
            }
            if (record->kind == return_statement) {
                return expression_at(record->value);
            }
            // A repeat loop's condition is its count (IEEE 1800-2023
            // 37.75).
            if (record->kind == loop && record->loop_repeat) {
                return expression_at(record->loop_limit);
            }
            return expression_at(record->condition);
        case vpiExpr:
            if (record->kind == assertion
                || record->immediate_directive
                    == sv::Statement::ImmediateDirective::cover) {
                return expression_at(record->condition);
            }
            if (record->kind == disable) {
                return hooks_.resolve(entry.scope, record->task.spelling);
            }
            return fsim_vpi_handle_v1 { };
        case vpiVariables:
            return record->foreach_collection
                ? expression_at(record->foreach_collection)
                : fsim_vpi_handle_v1 { };
        case vpiStmt:
            // A foreach statement's one statement is its body (37.51).
            if (record->foreach_collection) {
                const auto body = foreach_body(*record);
                return body ? statement_handle(*body, entry.scope, self)
                            : fsim_vpi_handle_v1 { };
            }
            return sequence(
                record->statements, entry.id, body_then, entry.scope, self);
        case vpiElseStmt:
            return sequence(record->else_statements, entry.id, body_else,
                entry.scope, self);
        case vpiDelay:
            if (record->kind == delay_control || entry.kind == Kind::cont_assign) {
                if (!record->delay) {
                    return fsim_vpi_handle_v1 { };
                }
                // More than one delay is an operation of vpiOpType vpiListOp
                // (37.80).
                if (!record->delay->additional.empty()) {
                    return node(
                        Kind::delay_list, entry.id, 0U, entry.scope, self);
                }
                if (record->delay->primary.expression) {
                    return expression_at(record->delay->primary.expression);
                }
                return node(Kind::delay, entry.id, 0U, entry.scope, self);
            }
            return fsim_vpi_handle_v1 { };
        case vpiDelayControl:
        case vpiEventControl:
            if (record->kind == assignment
                && record->assignment_control
                    == (type == vpiDelayControl ? sv::AssignmentControl::delay
                                                : sv::AssignmentControl::event)) {
                return node(Kind::assignment_control, entry.id, 0U, entry.scope,
                    self);
            }
            return fsim_vpi_handle_v1 { };
        case vpiNamedEvent:
            return record->kind == event_trigger ? expression_at(record->target)
                                                 : 0U;
        case vpiTask:
            if (record->kind == task_call && hooks_.callee) {
                return hooks_.callee(entry.scope, true, entry.id);
            }
            return fsim_vpi_handle_v1 { };
        default:
            return fsim_vpi_handle_v1 { };
        }
    }
    case Kind::assignment_control: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiDelay && record->delay) {
            if (record->delay->primary.expression) {
                return expression_at(record->delay->primary.expression);
            }
            return node(Kind::delay, entry.id, 0U, entry.scope, self);
        }
        if (type == vpiCondition && !record->sensitivities.empty()) {
            return record->sensitivities.size() == 1U
                ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self)
                : node(Kind::sensitivity_list, entry.id, 0U, entry.scope, self);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::case_item: {
        const auto* const record = statement(entry.id);
        if (record == nullptr || entry.aux >= record->case_alternatives.size()
            || type != vpiStmt) {
            return fsim_vpi_handle_v1 { };
        }
        return sequence(record->case_alternatives[entry.aux].statements,
            entry.id, body_case + entry.aux, entry.scope, self);
    }
    case Kind::loop_init:
    case Kind::loop_step: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (entry.kind == Kind::loop_step && step_update(*record)) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiLhs) {
            const auto target = entry.kind == Kind::loop_init
                ? record->loop_update_target.has_value()
                    ? record->loop_update_target
                    : record->target
                : record->target;
            return expression_at(target);
        }
        if (type == vpiRhs) {
            return expression_at(entry.kind == Kind::loop_init
                    ? record->loop_initial
                    : record->value);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        const auto kind = select_aware_type(entry, *record);
        const auto operand = [&](const std::size_t index) {
            return index < record->operands.size()
                ? expression_handle(record->operands[index], entry.scope, self)
                : 0U;
        };
        // The first index of a var select; the others by iteration.
        if (kind == vpiVarSelect) {
            const auto select = var_select(entry, *record);
            return type == vpiIndex && select
                ? expression_handle(select->indices.front(), entry.scope, self)
                : 0U;
        }
        // A method call is applied to its prefix: the first operand of an
        // instance call (IEEE 1800-2023 37.42 detail 2).
        if (kind == vpiMethodFuncCall && type == vpiPrefix) {
            return record->kind == sv::ExpressionKind::class_method_call
                ? operand(0U)
                : 0U;
        }
        if (kind == vpiBitSelect || kind == vpiPartSelect
            || kind == vpiIndexedPartSelect) {
            if (kind == vpiBitSelect && type == vpiIndex) {
                return operand(1U);
            }
            if (kind == vpiPartSelect
                && (type == vpiLeftRange || type == vpiRightRange)) {
                return operand(type == vpiLeftRange ? 1U : 2U);
            }
            if (kind == vpiIndexedPartSelect
                && (type == vpiBaseExpr || type == vpiWidthExpr)) {
                return operand(type == vpiBaseExpr ? 1U : 2U);
            }
            return fsim_vpi_handle_v1 { };
        }
        // The inline constraint of a randomize call (18.7, 37.42 detail 1).
        if ((kind == vpiFuncCall || kind == vpiMethodFuncCall)
            && type == vpiWith && inline_constraint_of(*record) != nullptr) {
            return node(Kind::inline_constraint, entry.id, 0U, entry.scope, self);
        }
        // The with expression of an array locator method (IEEE 1800-2023
        // 7.12.1, 37.42 detail 1): the last operand after the array, which
        // the parser keeps with the iterator argument, if any, before it.
        if (kind == vpiFuncCall && type == vpiWith
            && record->kind == sv::ExpressionKind::call
            && record->operands.size() >= 2U) {
            static const std::set<std::string, std::less<>> locators {
                ".find", ".find_index", ".find_first", ".find_first_index",
                ".find_last", ".find_last_index", ".min", ".max", ".unique",
                ".unique_index" };
            if (locators.contains(record->text)) {
                return operand(record->operands.size() - 1U);
            }
            return fsim_vpi_handle_v1 { };
        }
        if ((kind == vpiFuncCall || kind == vpiMethodFuncCall)
            && type == vpiFunction) {
            return call_callee(entry.id, entry.scope);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::sensitivity: {
        const auto* const list = entry.of_process
            ? (process(entry.id) != nullptr ? &process(entry.id)->sensitivities
                                            : nullptr)
            : (statement(entry.id) != nullptr
                    ? &statement(entry.id)->sensitivities
                    : nullptr);
        if (list == nullptr || entry.aux >= list->size()) {
            return fsim_vpi_handle_v1 { };
        }
        return fsim_vpi_handle_v1 { };
    }
    default:
        return fsim_vpi_handle_v1 { };
    }
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiStatementModel::iterate(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = nodes_.at(serial);
    const auto self = hooks_.handle(serial);
    std::vector<fsim_vpi_handle_v1> found;
    const FrameRestore restore { frame_,
        std::exchange(frame_, entry.kind == Kind::expression
                || entry.kind == Kind::reference || entry.kind == Kind::let_expr
            ? entry.aux
            : 0U) };
    // The formals of a let declaration; the arguments of a let expression
    // in the order of the formals, a default where none is given (IEEE
    // 1800-2023 37.57 detail 1).
    if (entry.kind == Kind::let_decl && type == vpiSeqFormalDecl) {
        if (entry.id < let_entries_.size()) {
            for (std::uint32_t port = 0U;
                 port < let_entries_[entry.id]->ports.size(); ++port) {
                found.push_back(
                    node(Kind::let_formal, entry.id, port, entry.scope, self));
            }
        }
        return found;
    }
    if (entry.kind == Kind::let_expr && type == vpiArgument) {
        if (const auto* const declaration = let_of_use(entry)) {
            for (std::size_t port = 0U; port < declaration->ports.size();
                 ++port) {
                if (const auto argument = let_argument(*declaration,
                        semantic::ExpressionId::from_index(entry.id), port)) {
                    if (const auto handle
                        = expression_handle(*argument, entry.scope, self)) {
                        found.push_back(handle);
                    }
                }
            }
        }
        return found;
    }
    if (entry.kind == Kind::let_decl || entry.kind == Kind::let_formal
        || entry.kind == Kind::let_expr) {
        return std::nullopt;
    }
    const auto sensitivities = [&]() -> const std::vector<sv::Sensitivity>* {
        if (entry.of_process) {
            const auto* const record = process(entry.id);
            return record == nullptr ? nullptr : &record->sensitivities;
        }
        const auto* const record = statement(entry.id);
        return record == nullptr ? nullptr : &record->sensitivities;
    };
    // A sensitivity: its expression, or for an edge the vpiPosedgeOp or
    // vpiNegedgeOp operation over it.
    const auto sensitivity_handle = [&](const std::size_t index) {
        const auto* const list = sensitivities();
        if (list == nullptr || index >= list->size()) {
            return fsim_vpi_handle_v1 { };
        }
        const auto& item = (*list)[index];
        if (item.edge != sv::EdgeKind::any) {
            return node(Kind::sensitivity, entry.id,
                static_cast<std::uint32_t>(index), entry.scope, self,
                entry.of_process);
        }
        if (item.expression) {
            return expression_handle(*item.expression, entry.scope, self);
        }
        return hooks_.resolve(entry.scope, item.signal);
    };
    switch (entry.kind) {
    // The constraint items of an inline constraint: its expressions, in
    // order (37.34).
    case Kind::inline_constraint: {
        const auto* const record = expression(entry.id);
        const auto* const block
            = record != nullptr ? inline_constraint_of(*record) : nullptr;
        if (block != nullptr && type == vpiConstraintItem) {
            for (const auto choice : block->choices) {
                found.push_back(expression_handle(choice, entry.scope, self));
            }
        }
        return found;
    }
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return found;
        }
        using enum sv::StatementKind;
        if ((type == vpiForInitStmt || type == vpiForIncStmt)
            && statement_type(*record) == vpiFor) {
            if (const auto handle = relation(serial, type).value_or(0U)) {
                found.push_back(handle);
            }
            if (type == vpiForIncStmt) {
                for (std::size_t index = 1U;
                     index < record->loop_updates.size(); ++index) {
                    if (const auto handle = statement_handle(
                            record->loop_updates[index], entry.scope, self)) {
                        found.push_back(handle);
                    }
                }
            }
        } else if (type == vpiVariables && statement_type(*record) == vpiFor
            && record->loop_variable_declared) {
            // The variables a for statement declares, of which it is the
            // scope (12.7.1, 37.12 detail 2).
            for (const auto id : record->declarations) {
                const auto* const declaration = hir_.find_declaration(id);
                if (declaration != nullptr
                    && declaration->form == sv::DeclarationForm::variable) {
                    found.push_back(node(Kind::local_variable, id.value(),
                        entry.id, entry.scope, self));
                }
            }
        } else if (type == vpiLoopVars && record->foreach_collection) {
            for (std::size_t index = 0U; index < record->foreach_variables.size();
                 ++index) {
                const auto& variable = record->foreach_variables[index];
                found.push_back(variable
                        ? expression_handle(*variable, entry.scope, self)
                        : node(Kind::null_operation, entry.id,
                            static_cast<std::uint32_t>(index), entry.scope,
                            self));
            }
        } else if (type == vpiStmt && record->foreach_collection) {
            if (const auto body = foreach_body(*record)) {
                if (const auto handle
                    = statement_handle(*body, entry.scope, self)) {
                    found.push_back(handle);
                }
            }
        } else if (type == vpiStmt
            && (record->kind == block || record->kind == fork)) {
            for (const auto id : record->statements) {
                if (const auto handle = statement_handle(id, entry.scope, self)) {
                    found.push_back(handle);
                }
            }
        } else if (type == vpiCaseItem && record->kind == selection) {
            for (std::size_t index = 0U;
                 index < record->case_alternatives.size(); ++index) {
                found.push_back(node(Kind::case_item, entry.id,
                    static_cast<std::uint32_t>(index), entry.scope, self));
            }
        } else if (type == vpiOperand && step_statement(*record)) {
            if (record->target) {
                found.push_back(
                    expression_handle(*record->target, entry.scope, self));
            }
        } else if (type == vpiArgument) {
            if (record->kind == task_call) {
                for (const auto& argument : record->task_arguments) {
                    if (argument.actual) {
                        found.push_back(expression_handle(
                            *argument.actual, entry.scope, self));
                    }
                }
            } else if (system_task_name(*record)) {
                if (record->file_handle) {
                    found.push_back(expression_handle(
                        *record->file_handle, entry.scope, self));
                }
                if (!record->output_arguments.empty()) {
                    // The arguments as written, in order: the literals the
                    // frontend folded into text are kept (37.62).
                    // An empty argument is a vpiNullOp operation (37.42
                    // detail 8).
                    for (std::size_t index = 0U;
                         index < record->output_arguments.size(); ++index) {
                        const auto argument = record->output_arguments[index];
                        found.push_back(argument
                                ? expression_handle(
                                      *argument, entry.scope, self)
                                : node(Kind::null_operation, entry.id,
                                      static_cast<std::uint32_t>(index),
                                      entry.scope, self));
                    }
                } else {
                    if (format_text(*record)) {
                        found.push_back(node(
                            Kind::format, entry.id, 0U, entry.scope, self));
                    }
                    for (const auto& output : record->output_values) {
                        if (output.value) {
                            found.push_back(expression_handle(
                                *output.value, entry.scope, self));
                        }
                    }
                    if (record->output_values.empty() && record->value) {
                        found.push_back(expression_handle(
                            *record->value, entry.scope, self));
                    }
                }
                if (record->target) {
                    found.push_back(expression_handle(
                        *record->target, entry.scope, self));
                }
            }
        }
        return found;
    }
    case Kind::body:
        if (type == vpiStmt) {
            if (const auto* const list = body_statements(entry)) {
                for (const auto id : *list) {
                    if (const auto handle
                        = statement_handle(id, entry.scope, self)) {
                        found.push_back(handle);
                    }
                }
            }
        }
        return found;
    case Kind::loop_step: {
        const auto* const record = statement(entry.id);
        if (record != nullptr && type == vpiOperand && step_update(*record)
            && record->target) {
            found.push_back(
                expression_handle(*record->target, entry.scope, self));
        }
        return found;
    }
    // The scalar bits of the assignment's left-hand side, from the LSB up
    // (IEEE 1800-2023 37.47).
    case Kind::cont_assign: {
        const auto* const record = statement(entry.id);
        if (type != vpiBit || record == nullptr || !record->target
            || !hooks_.bit_width) {
            return found;
        }
        const auto target
            = expression_handle(*record->target, entry.scope, self);
        const auto width = target != 0U ? hooks_.bit_width(target)
                                        : std::size_t { 0U };
        for (std::size_t offset = 0U; offset < width; ++offset) {
            found.push_back(node(Kind::cont_assign_bit, entry.id,
                static_cast<std::uint32_t>(offset), entry.scope, self));
        }
        return found;
    }
    case Kind::case_item: {
        const auto* const record = statement(entry.id);
        if (record != nullptr && entry.aux < record->case_alternatives.size()
            && type == vpiExpr) {
            for (const auto choice :
                record->case_alternatives[entry.aux].choices) {
                found.push_back(expression_handle(choice, entry.scope, self));
            }
        }
        return found;
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return found;
        }
        const auto kind = select_aware_type(entry, *record);
        // A var select's indexes, outermost dimension first (IEEE 1800-2023
        // 37.19).
        if (type == vpiIndex && kind == vpiVarSelect) {
            if (const auto select = var_select(entry, *record)) {
                for (const auto index : select->indices) {
                    found.push_back(expression_handle(index, entry.scope, self));
                }
            }
            return found;
        }
        if (type == vpiIndex && kind == vpiBitSelect
            && record->operands.size() > 1U) {
            found.push_back(
                expression_handle(record->operands[1U], entry.scope, self));
            return found;
        }
        if ((type == vpiOperand && kind == vpiOperation)
            || (type == vpiArgument
                && (kind == vpiSysFuncCall || kind == vpiFuncCall
                    || kind == vpiMethodFuncCall))) {
            auto operands = std::span { record->operands };
            // An instance call's first operand is its receiver, the prefix
            // (37.42 detail 2), not an argument.
            if ((record->kind == sv::ExpressionKind::class_method_call
                    || (type == vpiArgument
                        && record->kind == sv::ExpressionKind::call
                        && record->text.starts_with('.')
                        && call_callee(entry.id, entry.scope) != 0U))
                && !operands.empty()) {
                operands = operands.subspan(1U);
            }
            // A cast's operand is its value (6.24).
            if (const auto value = sv::systemverilog_cast_value_operand(*record);
                value && kind == vpiOperation) {
                found.push_back(expression_handle(*value, entry.scope, self));
                return found;
            }
            for (const auto operand : operands) {
                found.push_back(expression_handle(operand, entry.scope, self));
            }
        }
        return found;
    }
    case Kind::delay_list:
        // The delays in source order: the first, then each additional one
        // (37.47, 37.80).
        if (type == vpiOperand) {
            const auto* const record = statement(entry.id);
            if (record != nullptr && record->delay) {
                for (std::uint32_t index = 0U;
                     index <= record->delay->additional.size(); ++index) {
                    const auto* const value
                        = delay_value(*record->delay, index);
                    if (value == nullptr) {
                        continue;
                    }
                    found.push_back(value->expression
                            ? expression_handle(
                                *value->expression, entry.scope, self)
                            : node(Kind::delay, entry.id, index, entry.scope,
                                self));
                }
            }
        }
        return found;
    case Kind::specify_delay_list:
        // The delays in source order (37.39, 37.80).
        if (type == vpiOperand && entry.id < specify_delays_.size()) {
            for (std::uint32_t index = 0U;
                 index < specify_delays_[entry.id]->size(); ++index) {
                found.push_back(specify_delay_handle(
                    entry.id, index, entry.scope, self));
            }
        }
        return found;
    case Kind::specify_mtm_delay:
        if (type == vpiOperand && entry.id < specify_delays_.size()) {
            for (std::uint32_t which = 1U; which <= 3U; ++which) {
                if (const auto handle = specify_value_handle(
                        entry.id, entry.aux, which, entry.scope, self)) {
                    found.push_back(handle);
                }
            }
        }
        return found;
    case Kind::synthetic_operation:
    case Kind::connection_list:
        if (type == vpiOperand && entry.id < connection_lists_.size()) {
            found = connection_lists_[entry.id];
        }
        return found;
    case Kind::sensitivity_list:
        if (type == vpiOperand) {
            const auto* const list = sensitivities();
            for (std::size_t index = 0U; list != nullptr && index < list->size();
                 ++index) {
                if (const auto handle = sensitivity_handle(index)) {
                    found.push_back(handle);
                }
            }
        }
        return found;
    case Kind::assertion_decl: {
        const auto& table = assertion_entries_.at(entry.id);
        const bool property
            = table.declaration->kind == sv::AssertionDeclarationKind::property;
        // The local variables the declaration declares (37.51, 37.53).
        if (type == vpiVariables) {
            for (std::size_t index = 0U;
                 index < table.declaration->local_variables.size(); ++index) {
                found.push_back(node(Kind::assertion_variable, entry.id,
                    static_cast<std::uint32_t>(index), entry.scope, self));
            }
        }
        if (type == (property ? vpiPropFormalDecl : vpiSeqFormalDecl)) {
            for (std::size_t index = 0U;
                 index < table.declaration->formals.size(); ++index) {
                found.push_back(node(Kind::assertion_formal, entry.id,
                    static_cast<std::uint32_t>(index), entry.scope, self));
            }
        }
        return found;
    }
    case Kind::assertion_op: {
        const auto& table = assertion_entries_.at(entry.id);
        if (table.tree == nullptr || entry.aux >= table.tree->nodes.size()) {
            return found;
        }
        const auto& item = table.tree->nodes[entry.aux];
        using NodeKind = sv::AssertionTreeNode::Kind;
        // The operands a relation lists, from `first` up to but not
        // including `last`.
        const auto list = [&](const std::size_t first, const std::size_t last) {
            for (auto position = first;
                 position < last && position < item.operands.size();
                 ++position) {
                if (const auto handle = assertion_operand(entry.id,
                        item.operands[position], entry.scope, self)) {
                    found.push_back(handle);
                }
            }
        };
        switch (item.kind) {
        // The clocked sequences of a multiclock sequence expression (37.56).
        case NodeKind::multiclock_sequence:
            if (type == vpiClockedSeq) {
                list(0U, item.operands.size());
            }
            break;
        // The items of a case property; the expressions of an item, none for
        // the default item (37.52 details 4 and 5).
        case NodeKind::case_property:
            if (type == vpiCasePropertyItem) {
                list(1U, item.operands.size());
            }
            break;
        case NodeKind::case_item:
            if (type == vpiExpr && !item.default_item) {
                list(0U, item.operands.size() - 1U);
            }
            break;
        case NodeKind::clocked_property:
        case NodeKind::clocked_sequence:
            break;
        case NodeKind::instance:
            if (type == vpiArgument) {
                list(0U, item.operands.size());
            }
            break;
        case NodeKind::operation:
            if (type == vpiOperand) {
                list(0U, item.operands.size());
            }
            break;
        }
        return found;
    }
    case Kind::assertion_event_list:
        if (type == vpiOperand) {
            const auto count = assertion_events_.at(entry.id)->size();
            for (std::size_t index = 0U; index < count; ++index) {
                if (const auto handle = assertion_event_handle(entry.id,
                        static_cast<std::uint32_t>(index), entry.scope,
                        self)) {
                    found.push_back(handle);
                }
            }
        }
        return found;
    case Kind::assertion_event:
        if (type == vpiOperand) {
            const auto& list = *assertion_events_.at(entry.id);
            if (entry.aux < list.size()) {
                const auto& item = list[entry.aux];
                const auto operand = item.expression
                    ? expression_handle(*item.expression, entry.scope, self)
                    : hooks_.resolve(entry.scope, item.signal);
                if (operand != 0U) {
                    found.push_back(operand);
                }
            }
        }
        return found;
    case Kind::sensitivity:
        if (type == vpiOperand) {
            const auto* const list = sensitivities();
            if (list != nullptr && entry.aux < list->size()) {
                const auto& item = (*list)[entry.aux];
                const auto operand = item.expression
                    ? expression_handle(*item.expression, entry.scope, self)
                    : hooks_.resolve(entry.scope, item.signal);
                if (operand != 0U) {
                    found.push_back(operand);
                }
            }
        }
        return found;
    default:
        return found;
    }
}

std::optional<VpiStatementModel::ContinuousDelay>
VpiStatementModel::continuous_delay(const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    if (entry.kind != Kind::cont_assign
        && entry.kind != Kind::cont_assign_bit) {
        return std::nullopt;
    }
    const auto* const record = statement(entry.id);
    if (record == nullptr) {
        return std::nullopt;
    }
    ContinuousDelay result;
    result.scope = entry.scope;
    if (!record->delay) {
        return result;
    }
    const auto add = [&](const semantic::sv::Delay& delay) {
        if (delay.primary.expression) {
            return false;
        }
        result.values.push_back(delay.primary.magnitude);
        return true;
    };
    if (!add(*record->delay)) {
        return std::nullopt;
    }
    for (const auto& additional : record->delay->additional) {
        if (!add(additional)) {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<semantic::ExpressionId> VpiStatementModel::cast_expression(
    const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    if (entry.kind != Kind::expression) {
        return std::nullopt;
    }
    const auto* const record = expression(entry.id);
    if (record == nullptr || !sv::systemverilog_cast_value_operand(*record)) {
        return std::nullopt;
    }
    return record->id;
}

std::optional<VpiStatementModel::CastTarget> VpiStatementModel::cast_target(
    const std::uint64_t serial)
{
    const auto entry = nodes_.at(serial);
    if (entry.kind != Kind::expression) {
        return std::nullopt;
    }
    const auto* const record = expression(entry.id);
    if (record == nullptr || !sv::systemverilog_cast_value_operand(*record)) {
        return std::nullopt;
    }
    CastTarget target;
    if (const auto size = sv::systemverilog_cast_size_operand(*record)) {
        target.size
            = expression_handle(*size, entry.scope, hooks_.handle(serial));
        return target;
    }
    constexpr std::string_view prefix { "@sv-cast:" };
    target.type = record->text.substr(prefix.size());
    return target;
}

std::optional<VpiStatementValue> VpiStatementModel::value(
    const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    if (entry.kind == Kind::delay) {
        const auto* const record = statement(entry.id);
        if (record == nullptr || !record->delay
            || delay_value(*record->delay, entry.aux) == nullptr) {
            return std::nullopt;
        }
        VpiStatementValue result;
        const auto& primary = *delay_value(*record->delay, entry.aux);
        if (primary.divisor > 1U) {
            result.kind = VpiStatementValue::Kind::real;
            result.real = static_cast<double>(primary.magnitude)
                / static_cast<double>(primary.divisor);
            return result;
        }
        result.bits = PackedLogic4(64U, Logic4::zero);
        for (std::size_t bit = 0U; bit < 64U; ++bit) {
            result.bits.set(bit, ((primary.magnitude >> bit) & 1U) != 0U
                    ? Logic4::one
                    : Logic4::zero);
        }
        return result;
    }
    if (entry.kind == Kind::specify_delay_value) {
        const auto* const value = specify_value(entry);
        if (value == nullptr) {
            return std::nullopt;
        }
        VpiStatementValue result;
        if (value->divisor > 1U) {
            result.kind = VpiStatementValue::Kind::real;
            result.real = static_cast<double>(value->magnitude)
                / static_cast<double>(value->divisor);
            return result;
        }
        result.bits = PackedLogic4(64U, Logic4::zero);
        for (std::size_t bit = 0U; bit < 64U; ++bit) {
            result.bits.set(bit, ((value->magnitude >> bit) & 1U) != 0U
                    ? Logic4::one
                    : Logic4::zero);
        }
        return result;
    }
    if (entry.kind == Kind::format) {
        const auto* const record = statement(entry.id);
        auto text = record ? format_text(*record) : std::nullopt;
        if (!text) {
            return std::nullopt;
        }
        VpiStatementValue result;
        result.kind = VpiStatementValue::Kind::string;
        result.text = std::move(*text);
        return result;
    }
    if (entry.kind != Kind::expression) {
        return std::nullopt;
    }
    const auto* const record = expression(entry.id);
    if (record == nullptr) {
        return std::nullopt;
    }
    using enum sv::ExpressionKind;
    switch (record->kind) {
    case string_literal: {
        VpiStatementValue result;
        result.kind = VpiStatementValue::Kind::string;
        result.text = record->decoded_string.value_or(record->text);
        return result;
    }
    case integer_literal:
    case boolean_literal:
    case logic_literal: {
        if (record->decimal_literal) {
            const auto& decimal = *record->decimal_literal;
            VpiStatementValue result;
            result.kind = VpiStatementValue::Kind::real;
            double digits { };
            const auto parsed = std::from_chars(decimal.digits.data(),
                decimal.digits.data() + decimal.digits.size(), digits);
            if (parsed.ec != std::errc { }) {
                return std::nullopt;
            }
            result.real = digits
                * std::pow(10.0, static_cast<double>(decimal.decimal_exponent));
            return result;
        }
        if (record->text == "true" || record->text == "false") {
            VpiStatementValue result;
            result.bits = PackedLogic4(1U,
                record->text == "true" ? Logic4::one : Logic4::zero);
            return result;
        }
        return integer_value(record->text);
    }
    default:
        return std::nullopt;
    }
}

} // namespace fsim::app::application_detail
