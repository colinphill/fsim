// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

using boost::multiprecision::cpp_int;
using runtime::Logic4;
using runtime::PackedLogic4;
using Value = HirSystemVerilogConstant;
using ExpressionKind = semantic::sv::ExpressionKind;
using Scalar = frontend::SystemVerilogScalarConstant;
using ScalarKind = frontend::SystemVerilogScalarKind;

[[nodiscard]] bool systemverilog_constant_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::DeclarationId declaration)
{
    const auto view = specialization.find_declaration(declaration);
    if (!view || view->systemverilog == nullptr) {
        return false;
    }
    using Form = semantic::sv::DeclarationForm;
    const auto form = view->systemverilog->form;
    return form == Form::parameter
        || form == Form::local_parameter
        || form == Form::enumeration_literal;
}

[[nodiscard]] std::optional<semantic::DeclarationId>
systemverilog_constant_name_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Expression& expression)
{
    const auto selected = expression.referenced_name
        ? expression.referenced_name->selected
        : std::optional<semantic::DeclarationId> { };
    if (selected
        && systemverilog_constant_declaration(
            specialization, *selected)) {
        return selected;
    }

    // A bind actual is parsed in the directive's lexical scope before its
    // target occurrence is known. A target parameter name can consequently
    // carry an implicit-net placeholder in the immutable HIR. Re-resolve only
    // non-constant selections in the effective target specialization; real
    // lexical and package constants retain their linked declaration.
    if (const auto rebound
        = semantic::CompiledDesignResolver { specialization }
              .resolve_systemverilog_constant(
                  expression.text, specialization.scope(), false)
              .unique()) {
        return rebound;
    }
    return selected;
}

[[nodiscard]] std::optional<Scalar> parse_scalar_identity(
    std::string_view identity);

constexpr std::uint32_t maximum_constant_width
    = hir_systemverilog_maximum_constant_width;
constexpr std::uint64_t maximum_constant_work_units = 64U * 1024U * 1024U;
constexpr std::size_t maximum_constant_call_depth = 1024U;

[[nodiscard]] char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

[[nodiscard]] std::string cleaned_digits(const std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const auto character : text) {
        if (character != '_') {
            result.push_back(character);
        }
    }
    return result;
}

template <typename Integer>
[[nodiscard]] std::optional<Integer> parse_integer(
    const std::string_view text,
    const int base = 10)
{
    Integer value { };
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value, base);
    if (parsed.ec != std::errc { }
        || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool known(const PackedLogic4& value) noexcept
{
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        const auto state = runtime::to_logic4(value.get_logic9(bit));
        if (state == Logic4::x || state == Logic4::z) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool one(
    const PackedLogic4& value,
    const std::size_t bit) noexcept
{
    return runtime::to_logic4(value.get_logic9(bit)) == Logic4::one;
}

[[nodiscard]] Value make_value(
    PackedLogic4 packed,
    const bool signed_value,
    const bool unsized,
    const frontend::ValueDomain domain = frontend::ValueDomain::Logic4)
{
    Value result;
    result.width = static_cast<std::uint32_t>(packed.width());
    result.packed = std::move(packed);
    result.signed_value = signed_value;
    result.unsized = unsized;
    result.domain = domain;
    return result;
}

[[nodiscard]] Value make_known(
    const std::uint64_t input,
    const std::uint32_t width,
    const bool signed_value,
    const bool unsized,
    const frontend::ValueDomain domain = frontend::ValueDomain::Logic4)
{
    auto packed = PackedLogic4 { width, Logic4::zero };
    for (std::uint32_t bit = 0U; bit < width && bit < 64U; ++bit) {
        if (((input >> bit) & 1U) != 0U) {
            packed.set(bit, Logic4::one);
        }
    }
    return make_value(
        std::move(packed), signed_value, unsized, domain);
}

[[nodiscard]] Value make_unknown(
    const std::uint32_t width,
    const bool signed_value)
{
    return make_value(
        PackedLogic4 { width, Logic4::x }, signed_value, false);
}

[[nodiscard]] cpp_int unsigned_integer(const Value& value)
{
    cpp_int result { };
    for (auto bit = value.width; bit-- > 0U;) {
        result <<= 1U;
        if (one(value.packed, bit)) {
            result += 1;
        }
    }
    return result;
}

[[nodiscard]] cpp_int numeric_integer(const Value& value)
{
    auto result = unsigned_integer(value);
    if (value.signed_value && one(value.packed, value.width - 1U)) {
        result -= cpp_int { 1U } << value.width;
    }
    return result;
}

[[nodiscard]] PackedLogic4 packed_integer(
    cpp_int value,
    const std::uint32_t width)
{
    const auto modulus = cpp_int { 1U } << width;
    value %= modulus;
    if (value < 0) {
        value += modulus;
    }
    auto packed = PackedLogic4 { width, Logic4::zero };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        if (boost::multiprecision::bit_test(value, bit)) {
            packed.set(bit, Logic4::one);
        }
    }
    return packed;
}

[[nodiscard]] Value resized(Value value, const std::uint32_t width)
{
    if (value.width == width) {
        return value;
    }
    const auto logic9 = value.packed.is_logic9();
    auto packed = PackedLogic4 { width, Logic4::zero };
    if (logic9) {
        packed = packed.promoted_to_logic9();
    }
    if (width > value.width && value.signed_value) {
        const auto sign = value.packed.get_logic9(value.width - 1U);
        if (logic9) {
            packed.fill(sign);
        } else {
            packed.fill(runtime::to_logic4(sign));
        }
    }
    const auto copied = std::min(width, value.width);
    packed.insert_bits(value.packed.extract_bits(0U, copied), 0U);
    value.packed = std::move(packed);
    value.width = width;
    value.packed_range = semantic::sv::PackedRange {
        static_cast<std::int64_t>(width - 1U), 0, std::nullopt,
        std::nullopt, true, { }
    };
    return value;
}

void convert_to_two_state(Value& value)
{
    auto converted = PackedLogic4 { value.width, Logic4::zero };
    for (std::uint32_t bit = 0U; bit < value.width; ++bit) {
        if (runtime::to_logic4(value.packed.get_logic9(bit))
            == Logic4::one) {
            converted.set(bit, Logic4::one);
        }
    }
    value.packed = std::move(converted);
}

[[nodiscard]] frontend::ValueDomain type_domain(
    const semantic::sv::TypeReference& type)
{
    const auto spelling = type.target.spelling;
    if (spelling == "byte" || spelling == "shortint"
        || spelling == "int" || spelling == "longint"
        || spelling == "integer") {
        return frontend::ValueDomain::Integer;
    }
    return type.four_state ? frontend::ValueDomain::Logic4
                           : frontend::ValueDomain::Bit2;
}

[[nodiscard]] std::optional<std::uint64_t> resolved_type_width(
    const semantic::sv::TypeReference& type,
    const semantic::SpecializedHirUnit* const specialization,
    std::unordered_set<std::uint32_t>& visiting)
{
    if (type.packed_range) {
        auto left = type.packed_range->left;
        auto right = type.packed_range->right;
        if (specialization != nullptr) {
            if (!left && type.packed_range->left_expression) {
                left = specialization->evaluate_integral_expression(
                    *type.packed_range->left_expression);
            }
            if (!right && type.packed_range->right_expression) {
                right = specialization->evaluate_integral_expression(
                    *type.packed_range->right_expression);
            }
        }
        if (left && right) {
            const auto distance = *left >= *right
                ? static_cast<std::uint64_t>(*left)
                    - static_cast<std::uint64_t>(*right)
                : static_cast<std::uint64_t>(*right)
                    - static_cast<std::uint64_t>(*left);
            if (distance != std::numeric_limits<std::uint64_t>::max()) {
                return distance + 1U;
            }
        }
        return std::nullopt;
    }
    if (specialization != nullptr) {
        const auto effective
            = semantic::CompiledDesignResolver { *specialization }
                  .effective_systemverilog_type(
                      type, specialization->scope());
        if (effective && *effective != type) {
            return resolved_type_width(
                *effective, specialization, visiting);
        }
    }
    if (specialization != nullptr && type.target.target.valid()
        && visiting.insert(type.target.target.value()).second) {
        const auto definition = specialization->find_type(
            type.target.target);
        std::optional<std::uint64_t> result;
        if (definition && definition->systemverilog != nullptr) {
            const auto& record = *definition->systemverilog;
            result = resolved_type_width(
                record.base, specialization, visiting);
            if (!record.members.empty()) {
                std::uint64_t aggregate { };
                auto valid = true;
                for (const auto& member : record.members) {
                    const auto width = resolved_type_width(
                        member.type, specialization, visiting);
                    if (!width || *width == 0U) {
                        valid = false;
                        break;
                    }
                    if (record.form
                            == semantic::sv::TypeForm::packed_union
                        || record.form
                            == semantic::sv::TypeForm::tagged_union) {
                        aggregate = std::max(aggregate, *width);
                    } else if (*width
                        > std::numeric_limits<std::uint64_t>::max()
                            - aggregate) {
                        valid = false;
                        break;
                    } else {
                        aggregate += *width;
                    }
                }
                result = valid
                    ? std::optional { aggregate }
                    : std::nullopt;
            }
        }
        visiting.erase(type.target.target.value());
        if (result) {
            return result;
        }
    }
    if (type.executable_width && *type.executable_width != 0U) {
        return type.executable_width;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Value> parse_canonical(
    const std::string_view text)
{
    if (!text.starts_with("svconst-v3:")) {
        return std::nullopt;
    }
    const auto field = [&](const std::string_view marker)
        -> std::optional<std::string_view> {
        const auto begin = text.find(marker);
        if (begin == std::string_view::npos) {
            return std::nullopt;
        }
        const auto value_begin = begin + marker.size();
        const auto end = text.find(':', value_begin);
        return text.substr(value_begin, end == std::string_view::npos
                ? text.size() - value_begin
                : end - value_begin);
    };
    const auto width_field = field(":w=");
    const auto signed_field = field(":s=");
    const auto unsized_field = field(":u=");
    const auto domain_field = field(":d=");
    const auto unbounded_field = field(":b=");
    const auto nominal_marker = text.find(":n=");
    if (!width_field || !signed_field || !unsized_field
        || !domain_field || !unbounded_field
        || nominal_marker == std::string_view::npos) {
        return std::nullopt;
    }
    const auto width = parse_integer<std::uint32_t>(*width_field);
    const auto domain_value = parse_integer<unsigned>(*domain_field);
    const auto nominal_size_begin = nominal_marker + 3U;
    const auto nominal_size_end = text.find(':', nominal_size_begin);
    if (nominal_size_end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto nominal_size = parse_integer<std::size_t>(
        text.substr(
            nominal_size_begin,
            nominal_size_end - nominal_size_begin));
    const auto nominal_begin = nominal_size_end + 1U;
    if (!nominal_size || *nominal_size > text.size() - nominal_begin) {
        return std::nullopt;
    }
    const auto value_marker = nominal_begin + *nominal_size;
    if (text.substr(value_marker, 3U) != ":v=") {
        return std::nullopt;
    }
    const auto bits = text.substr(value_marker + 3U);
    if (!width || *width == 0U || *width > maximum_constant_width
        || bits.size() != *width || !domain_value
        || *domain_value
            > static_cast<unsigned>(frontend::ValueDomain::String)
        || (*signed_field != "0" && *signed_field != "1")
        || (*unsized_field != "0" && *unsized_field != "1")
        || (*unbounded_field != "0" && *unbounded_field != "1")) {
        return std::nullopt;
    }
    auto packed = PackedLogic4::from_msb_string(bits);
    auto result = make_value(
        std::move(packed), *signed_field == "1", *unsized_field == "1",
        static_cast<frontend::ValueDomain>(*domain_value));
    result.unbounded = *unbounded_field == "1";
    result.nominal_type = text.substr(nominal_begin, *nominal_size);
    return result;
}

[[nodiscard]] std::optional<Value> parse_literal(
    const std::string_view spelling,
    const bool integer_literal,
    std::string& error)
{
    if (const auto canonical = parse_canonical(spelling)) {
        return canonical;
    }
    const auto quote = spelling.find('\'');
    if (quote == std::string_view::npos) {
        if (!integer_literal) {
            return std::nullopt;
        }
        auto digits = cleaned_digits(spelling);
        auto negative = false;
        if (!digits.empty()
            && (digits.front() == '-' || digits.front() == '+')) {
            negative = digits.front() == '-';
            digits.erase(digits.begin());
        }
        if (digits.empty()
            || !std::ranges::all_of(digits, [](const char digit) {
                   return digit >= '0' && digit <= '9';
               })) {
            error = "SystemVerilog decimal literal contains an invalid digit";
            return std::nullopt;
        }
        if (digits.size() > maximum_constant_work_units) {
            error = "SystemVerilog decimal literal exceeds the constant-"
                    "evaluation work limit";
            return std::nullopt;
        }
        cpp_int value { };
        for (const auto digit : digits) {
            value *= 10U;
            value += static_cast<unsigned>(digit - '0');
        }
        if (negative) {
            value = -value;
        }
        const auto magnitude_value = value < 0 ? -value : value;
        const auto magnitude = magnitude_value == 0
            ? 1U
            : static_cast<unsigned>(
                boost::multiprecision::msb(magnitude_value) + 1U);
        const auto width = std::max(32U, magnitude + 1U);
        if (width > maximum_constant_width) {
            error = "SystemVerilog decimal literal exceeds the constant-width "
                    "resource limit";
            return std::nullopt;
        }
        return make_value(
            packed_integer(value, width), true, true);
    }

    auto width_text = spelling.substr(0U, quote);
    auto suffix = spelling.substr(quote + 1U);
    bool signed_value { };
    if (!suffix.empty()
        && (suffix.front() == 's' || suffix.front() == 'S')) {
        signed_value = true;
        suffix.remove_prefix(1U);
    }
    if (width_text.empty() && suffix.size() == 1U) {
        const auto digit = ascii_lower(suffix.front());
        if (digit == '0' || digit == '1'
            || digit == 'x' || digit == 'z' || digit == '?') {
            const auto state = digit == '0' ? Logic4::zero
                : digit == '1' ? Logic4::one
                : digit == 'x' ? Logic4::x
                               : Logic4::z;
            return make_value(
                PackedLogic4 { 1U, state }, false, true);
        }
    }
    if (suffix.size() < 2U) {
        error = "SystemVerilog based literal is missing a base or digits";
        return std::nullopt;
    }
    const auto base = ascii_lower(suffix.front());
    suffix.remove_prefix(1U);
    const auto digits = cleaned_digits(suffix);
    if (digits.empty()) {
        error = "SystemVerilog based literal has no digits";
        return std::nullopt;
    }
    std::optional<std::uint32_t> explicit_width;
    if (!width_text.empty()) {
        const auto cleaned_width = cleaned_digits(width_text);
        explicit_width = parse_integer<std::uint32_t>(cleaned_width);
        if (!explicit_width || *explicit_width == 0U
            || *explicit_width > maximum_constant_width) {
            error = "SystemVerilog constant width exceeds the constant-width "
                    "resource limit";
            return std::nullopt;
        }
    }
    if (base == 'd') {
        const auto unknown = std::ranges::find_if(
            digits, [](const char digit) {
                const auto folded = ascii_lower(digit);
                return folded == 'x' || folded == 'z' || folded == '?';
            });
        if (unknown != digits.end()) {
            if (digits.size() != 1U) {
                error = "a decimal SystemVerilog based literal may use only "
                        "one x, z, or ? digit";
                return std::nullopt;
            }
            const auto width = explicit_width.value_or(32U);
            return make_value(
                PackedLogic4 { width,
                    ascii_lower(*unknown) == 'x' ? Logic4::x : Logic4::z },
                signed_value, !explicit_width);
        }
        cpp_int value { };
        for (const auto digit : digits) {
            if (digit < '0' || digit > '9') {
                error = "SystemVerilog decimal literal contains an invalid "
                        "digit";
                return std::nullopt;
            }
            value *= 10U;
            value += static_cast<unsigned>(digit - '0');
        }
        const auto magnitude = value == 0
            ? 1U
            : static_cast<unsigned>(boost::multiprecision::msb(value) + 1U);
        const auto width = explicit_width.value_or(std::max(
            32U, magnitude + (signed_value ? 1U : 0U)));
        return make_value(
            packed_integer(value, width), signed_value, !explicit_width);
    }

    const auto digit_width = base == 'b' ? 1U
        : base == 'o' ? 3U
        : base == 'h' ? 4U
                      : 0U;
    const auto numeric_base = base == 'b' ? 2U
        : base == 'o' ? 8U
        : base == 'h' ? 16U
                      : 0U;
    if (digit_width == 0U) {
        error = "SystemVerilog based literal uses an unsupported base";
        return std::nullopt;
    }
    if (digits.size() > maximum_constant_width / digit_width) {
        error = "SystemVerilog based literal exceeds the constant-width "
                "resource limit";
        return std::nullopt;
    }
    const auto width = explicit_width.value_or(std::max<std::uint32_t>(
        32U, static_cast<std::uint32_t>(digits.size() * digit_width)));
    auto packed = PackedLogic4 { width, Logic4::zero };
    std::uint32_t output_bit { };
    for (auto digit = digits.rbegin();
        digit != digits.rend() && output_bit < width; ++digit) {
        const auto folded = ascii_lower(*digit);
        if (folded == 'x' || folded == 'z' || folded == '?') {
            const auto state = folded == 'x' ? Logic4::x : Logic4::z;
            for (std::uint32_t bit = 0U;
                bit < digit_width && output_bit < width;
                ++bit, ++output_bit) {
                packed.set(output_bit, state);
            }
            continue;
        }
        unsigned digit_value { };
        if (folded >= '0' && folded <= '9') {
            digit_value = static_cast<unsigned>(folded - '0');
        } else if (folded >= 'a' && folded <= 'f') {
            digit_value = 10U + static_cast<unsigned>(folded - 'a');
        } else {
            error = "SystemVerilog based literal contains an invalid digit";
            return std::nullopt;
        }
        if (digit_value >= numeric_base) {
            error = "SystemVerilog based literal digit is outside its base";
            return std::nullopt;
        }
        for (std::uint32_t bit = 0U;
            bit < digit_width && output_bit < width;
            ++bit, ++output_bit) {
            if (((digit_value >> bit) & 1U) != 0U) {
                packed.set(output_bit, Logic4::one);
            }
        }
    }
    return make_value(
        std::move(packed), signed_value, !explicit_width);
}

enum class Truth : std::uint8_t { false_value,
    true_value,
    unknown };

[[nodiscard]] Truth truth(const Value& value) noexcept
{
    bool unknown { };
    for (std::uint32_t bit = 0U; bit < value.width; ++bit) {
        const auto state = runtime::to_logic4(value.packed.get_logic9(bit));
        if (state == Logic4::one) {
            return Truth::true_value;
        }
        unknown = unknown || state == Logic4::x || state == Logic4::z;
    }
    return unknown ? Truth::unknown : Truth::false_value;
}

[[nodiscard]] Value logical_result(const Truth value)
{
    if (value == Truth::unknown) {
        return make_unknown(1U, false);
    }
    return make_known(
        value == Truth::true_value ? 1U : 0U, 1U, false, false);
}

[[nodiscard]] Value common_operand(
    Value value,
    const std::uint32_t width,
    const bool signed_value)
{
    value = resized(std::move(value), width);
    value.signed_value = signed_value;
    return value;
}

[[nodiscard]] int compare_known(const Value& left, const Value& right)
{
    const auto width = std::max(left.width, right.width);
    const auto signed_comparison
        = left.signed_value && right.signed_value;
    const auto lhs = common_operand(left, width, signed_comparison);
    const auto rhs = common_operand(right, width, signed_comparison);
    const auto lhs_value = signed_comparison
        ? numeric_integer(lhs) : unsigned_integer(lhs);
    const auto rhs_value = signed_comparison
        ? numeric_integer(rhs) : unsigned_integer(rhs);
    return lhs_value < rhs_value ? -1 : lhs_value > rhs_value ? 1 : 0;
}

[[nodiscard]] Value bitwise(
    const Value& left,
    const Value& right,
    const std::string_view operation)
{
    const auto width = std::max(left.width, right.width);
    const auto signed_value = left.signed_value && right.signed_value;
    const auto lhs = common_operand(left, width, signed_value);
    const auto rhs = common_operand(right, width, signed_value);
    auto packed = PackedLogic4 { width, Logic4::zero };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        const auto left_state
            = runtime::to_logic4(lhs.packed.get_logic9(bit));
        const auto right_state
            = runtime::to_logic4(rhs.packed.get_logic9(bit));
        const auto left_known
            = left_state == Logic4::zero || left_state == Logic4::one;
        const auto right_known
            = right_state == Logic4::zero || right_state == Logic4::one;
        const auto left_one = left_state == Logic4::one;
        const auto right_one = right_state == Logic4::one;
        auto state = Logic4::x;
        if (operation == "&") {
            state = (!left_one && left_known)
                    || (!right_one && right_known)
                ? Logic4::zero
                : left_one && right_one ? Logic4::one : Logic4::x;
        } else if (operation == "|") {
            state = left_one || right_one ? Logic4::one
                : left_known && right_known ? Logic4::zero : Logic4::x;
        } else if (left_known && right_known) {
            auto result = left_one != right_one;
            if (operation == "~^" || operation == "^~") {
                result = !result;
            }
            state = result ? Logic4::one : Logic4::zero;
        }
        packed.set(bit, state);
    }
    return make_value(
        std::move(packed), signed_value, false,
        lhs.domain == rhs.domain ? lhs.domain
                                 : frontend::ValueDomain::Logic4);
}

[[nodiscard]] Truth wildcard_equal(
    const Value& left,
    const Value& right)
{
    const auto width = std::max(left.width, right.width);
    const auto signed_value = left.signed_value && right.signed_value;
    const auto lhs = common_operand(left, width, signed_value);
    const auto rhs = common_operand(right, width, signed_value);
    auto unknown = false;
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        const auto expected
            = runtime::to_logic4(rhs.packed.get_logic9(bit));
        if (expected == Logic4::x || expected == Logic4::z) {
            continue;
        }
        const auto actual
            = runtime::to_logic4(lhs.packed.get_logic9(bit));
        if (actual == Logic4::x || actual == Logic4::z) {
            unknown = true;
        } else if (actual != expected) {
            return Truth::false_value;
        }
    }
    return unknown ? Truth::unknown : Truth::true_value;
}

[[nodiscard]] std::optional<std::uint64_t> nonnegative_count(
    const Value& value,
    std::string& error)
{
    if (!value.known()) {
        error = "constant count contains X or Z";
        return std::nullopt;
    }
    const auto integer = numeric_integer(value);
    if (integer < 0
        || integer > std::numeric_limits<std::uint64_t>::max()) {
        error = "constant count must be nonnegative and fit the bounded "
                "64-bit resource range";
        return std::nullopt;
    }
    return integer.convert_to<std::uint64_t>();
}

void append_packed(Value& destination, const Value& operand)
{
    const auto logic9
        = destination.packed.is_logic9() || operand.packed.is_logic9();
    auto packed = PackedLogic4 { destination.width, Logic4::zero };
    if (logic9) {
        packed = packed.promoted_to_logic9();
    }
    const auto retained = destination.width > operand.width
        ? destination.width - operand.width : 0U;
    for (std::uint32_t bit = 0U; bit < retained; ++bit) {
        if (logic9) {
            packed.set_logic9(
                bit + operand.width,
                destination.packed.get_logic9(bit));
        } else {
            packed.set(
                bit + operand.width, destination.packed.get(bit));
        }
    }
    const auto copied = std::min(operand.width, destination.width);
    for (std::uint32_t bit = 0U; bit < copied; ++bit) {
        if (logic9) {
            packed.set_logic9(bit, operand.packed.get_logic9(bit));
        } else {
            packed.set(bit, operand.packed.get(bit));
        }
    }
    destination.packed = std::move(packed);
}

class HirConstantEvaluator final {
public:
    explicit HirConstantEvaluator(
        const semantic::SpecializedHirUnit& specialization,
        std::string& error)
        : specialization_(specialization)
        , error_(error)
    {
    }

    [[nodiscard]] std::optional<Value> evaluate(
        const semantic::ExpressionId expression)
    {
        if (!active_expressions_.insert(expression.value()).second) {
            error_ = "recursive SystemVerilog constant expression";
            return std::nullopt;
        }
        const auto remove = [&] {
            active_expressions_.erase(expression.value());
        };
        auto result = evaluate_impl(expression);
        remove();
        return result;
    }

private:
    struct Frame {
        semantic::DeclarationId callable;
        std::map<semantic::DeclarationId, Value> values;
        std::map<std::string, Value> pattern_values;
        std::optional<Value> result;
    };

    enum class Flow : std::uint8_t { normal,
        broke,
        continued,
        returned,
        failed };

    [[nodiscard]] std::optional<std::uint32_t> expression_width(
        const semantic::ExpressionId expression,
        std::unordered_set<std::uint32_t>& visiting) const
    {
        if (!visiting.insert(expression.value()).second) {
            return std::nullopt;
        }
        const auto finish = [&](const std::optional<std::uint32_t> result) {
            visiting.erase(expression.value());
            return result;
        };
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return finish(std::nullopt);
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::boolean_literal) {
            return finish(1U);
        }
        if (record.kind == ExpressionKind::logic_literal
            || record.kind == ExpressionKind::integer_literal) {
            std::string ignored;
            const auto value = parse_literal(record.text, true, ignored);
            return finish(value
                    ? std::optional { value->width }
                    : std::nullopt);
        }
        if (record.kind == ExpressionKind::unary
            && record.operands.size() == 1U) {
            if (record.text == "!" || record.text == "&"
                || record.text == "|" || record.text == "^"
                || record.text == "~&" || record.text == "~|"
                || record.text == "~^") {
                return finish(1U);
            }
            return finish(expression_width(
                record.operands.front(), visiting));
        }
        if (record.kind == ExpressionKind::binary
            && record.operands.size() == 2U) {
            if (record.text == "==" || record.text == "!="
                || record.text == "===" || record.text == "!=="
                || record.text == "==?" || record.text == "!=?"
                || record.text == "&&" || record.text == "||") {
                return finish(1U);
            }
            const auto left = expression_width(
                record.operands.front(), visiting);
            const auto right = expression_width(
                record.operands.back(), visiting);
            return finish(left && right
                    ? std::optional { std::max(*left, *right) }
                    : std::nullopt);
        }
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            return finish(1U);
        }
        if (record.kind == ExpressionKind::slice
            && record.operands.size() == 3U) {
            const auto left = specialization_.evaluate_integral_expression(
                record.operands[1]);
            const auto right = specialization_.evaluate_integral_expression(
                record.operands[2]);
            if (!left || !right) {
                return finish(std::nullopt);
            }
            const auto distance = *left >= *right
                ? static_cast<std::uint64_t>(*left)
                    - static_cast<std::uint64_t>(*right)
                : static_cast<std::uint64_t>(*right)
                    - static_cast<std::uint64_t>(*left);
            return finish(distance < maximum_constant_width
                    ? std::optional {
                          static_cast<std::uint32_t>(distance + 1U) }
                    : std::nullopt);
        }
        if (record.kind == ExpressionKind::concatenation
            && !record.operands.empty()) {
            std::uint64_t width { };
            for (const auto operand : record.operands) {
                const auto operand_width = expression_width(
                    operand, visiting);
                if (!operand_width
                    || *operand_width > maximum_constant_width - width) {
                    return finish(std::nullopt);
                }
                width += *operand_width;
            }
            return finish(width == 0U
                    ? std::nullopt
                    : std::optional {
                          static_cast<std::uint32_t>(width) });
        }
        if (record.kind == ExpressionKind::call
            && record.text == "?:" && record.operands.size() == 3U) {
            const auto when_true = expression_width(
                record.operands[1], visiting);
            const auto when_false = expression_width(
                record.operands[2], visiting);
            return finish(when_true && when_false
                    ? std::optional {
                          std::max(*when_true, *when_false) }
                    : std::nullopt);
        }
        if (record.kind == ExpressionKind::call
            && (record.text == "$signed"
                || record.text == "$unsigned")
            && record.operands.size() == 1U) {
            return finish(expression_width(
                record.operands.front(), visiting));
        }
        if (const auto selected
            = systemverilog_constant_name_declaration(
                specialization_, record)) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            if (declaration && declaration->systemverilog != nullptr) {
                const auto& declaration_record
                    = *declaration->systemverilog;
                if (declaration_record.type) {
                    std::unordered_set<std::uint32_t> type_visiting;
                    if (const auto width = resolved_type_width(
                            *declaration_record.type, &specialization_,
                            type_visiting);
                        width && *width != 0U
                        && *width <= maximum_constant_width) {
                        return finish(static_cast<std::uint32_t>(*width));
                    }
                }
                if (declaration_record.initializer) {
                    return finish(expression_width(
                        *declaration_record.initializer, visiting));
                }
            }
        }
        return finish(std::nullopt);
    }

    [[nodiscard]] std::optional<std::uint32_t> expression_width(
        const semantic::ExpressionId expression) const
    {
        std::unordered_set<std::uint32_t> visiting;
        return expression_width(expression, visiting);
    }

    [[nodiscard]] std::optional<Value> value_for_declaration(
        const semantic::DeclarationId declaration)
    {
        for (auto frame = frames_.rbegin(); frame != frames_.rend(); ++frame) {
            if (const auto found = frame->values.find(declaration);
                found != frame->values.end()) {
                return found->second;
            }
        }
        const auto& actuals
            = specialization_.specialization().actual_identities;
        const auto actual = std::ranges::find(actuals, declaration,
            &semantic::SpecializedHirActualIdentity::declaration);
        if (actual != actuals.end()) {
            if (const auto scalar = parse_scalar_identity(actual->identity);
                scalar && scalar->kind == ScalarKind::Time) {
                return make_known(
                    scalar->bits, 64U, false, false);
            }
            if (const auto parsed = parse_canonical(actual->identity)) {
                return parsed;
            }
            std::string ignored;
            if (const auto parsed = parse_literal(
                    actual->identity, true, ignored)) {
                return parsed;
            }
        }
        if (!active_declarations_.insert(declaration.value()).second) {
            error_ = "recursive SystemVerilog constant declaration";
            return std::nullopt;
        }
        const auto remove = [&] {
            active_declarations_.erase(declaration.value());
        };
        const auto view = specialization_.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->initializer) {
            remove();
            return std::nullopt;
        }
        auto result = evaluate(*view->systemverilog->initializer);
        if (result && view->systemverilog->type
            && hir_systemverilog_explicit_integral_type(
                *view->systemverilog->type)) {
            result = convert_hir_systemverilog_constant(
                std::move(*result), *view->systemverilog->type, error_,
                &specialization_);
        }
        remove();
        return result;
    }

    [[nodiscard]] std::optional<Value> evaluate_name(
        const semantic::sv::Expression& record)
    {
        if (record.text == "$") {
            auto result = make_known(0U, 1U, false, false,
                frontend::ValueDomain::Bit2);
            result.unbounded = true;
            return result;
        }
        for (auto frame = frames_.rbegin(); frame != frames_.rend(); ++frame) {
            if (const auto found = frame->pattern_values.find(record.text);
                found != frame->pattern_values.end()) {
                return found->second;
            }
        }
        const auto& hierarchy_identities
            = specialization_.specialization().hierarchy_identities;
        const auto hierarchy = std::ranges::find(
            hierarchy_identities, record.text,
            &semantic::SpecializedHirNamedIdentity::name);
        if (hierarchy != hierarchy_identities.end()) {
            if (const auto parsed = parse_canonical(hierarchy->identity)) {
                return parsed;
            }
            std::string ignored;
            if (const auto parsed = parse_literal(
                    hierarchy->identity, true, ignored)) {
                return parsed;
            }
        }
        const auto selected = systemverilog_constant_name_declaration(
            specialization_, record);
        if (!selected) {
            error_ = "unresolved SystemVerilog constant name '"
                + record.text + "'";
            return std::nullopt;
        }
        auto result = value_for_declaration(*selected);
        if (result) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->type) {
                result->packed_range
                    = declaration->systemverilog->type->packed_range;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<Value> evaluate_unary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 1U) {
            return std::nullopt;
        }
        auto operand = evaluate(record.operands.front());
        if (!operand) {
            return std::nullopt;
        }
        if (operand->unbounded) {
            error_ = "symbolic unbounded '$' is only valid as a parameter "
                     "value or an argument to $isunbounded";
            return std::nullopt;
        }
        if (record.text == "+") {
            return operand;
        }
        if (record.text == "!") {
            const auto state = truth(*operand);
            return logical_result(state == Truth::true_value
                    ? Truth::false_value
                    : state == Truth::false_value
                    ? Truth::true_value
                    : Truth::unknown);
        }
        if (record.text == "-") {
            if (!operand->known()) {
                return make_unknown(
                    operand->width, operand->signed_value);
            }
            operand->packed = packed_integer(
                -numeric_integer(*operand), operand->width);
            return operand;
        }
        if (record.text == "~") {
            for (std::uint32_t bit = 0U; bit < operand->width; ++bit) {
                const auto state = runtime::to_logic4(
                    operand->packed.get_logic9(bit));
                operand->packed.set(bit,
                    state == Logic4::zero ? Logic4::one
                        : state == Logic4::one ? Logic4::zero
                                              : Logic4::x);
            }
            return operand;
        }
        if (record.text == "&" || record.text == "|"
            || record.text == "^" || record.text == "~&"
            || record.text == "~|" || record.text == "~^"
            || record.text == "^~") {
            auto reduced = Truth::false_value;
            if (record.text == "&" || record.text == "~&") {
                reduced = Truth::true_value;
                for (std::uint32_t bit = 0U; bit < operand->width; ++bit) {
                    const auto state = runtime::to_logic4(
                        operand->packed.get_logic9(bit));
                    if (state == Logic4::zero) {
                        reduced = Truth::false_value;
                        break;
                    }
                    if (state == Logic4::x || state == Logic4::z) {
                        reduced = Truth::unknown;
                    }
                }
            } else if (record.text == "|" || record.text == "~|") {
                reduced = truth(*operand);
            } else if (!operand->known()) {
                reduced = Truth::unknown;
            } else {
                auto odd = false;
                for (std::uint32_t bit = 0U; bit < operand->width; ++bit) {
                    odd = odd != one(operand->packed, bit);
                }
                reduced = odd ? Truth::true_value : Truth::false_value;
            }
            if (record.text == "~&" || record.text == "~|"
                || record.text == "~^" || record.text == "^~") {
                reduced = reduced == Truth::true_value
                    ? Truth::false_value
                    : reduced == Truth::false_value
                    ? Truth::true_value
                    : Truth::unknown;
            }
            return logical_result(reduced);
        }
        error_ = "unsupported SystemVerilog unary constant operator '"
            + record.text + "'";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_binary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 2U) {
            return std::nullopt;
        }
        auto left = evaluate(record.operands.front());
        if (!left) {
            return std::nullopt;
        }
        if (record.text == "&&" && truth(*left) == Truth::false_value) {
            return logical_result(Truth::false_value);
        }
        if (record.text == "||" && truth(*left) == Truth::true_value) {
            return logical_result(Truth::true_value);
        }
        auto right = evaluate(record.operands.back());
        if (!right) {
            return std::nullopt;
        }
        if (left->unbounded || right->unbounded) {
            error_ = "symbolic unbounded '$' is only valid as a parameter "
                     "value or an argument to $isunbounded";
            return std::nullopt;
        }
        if (record.text == "&&" || record.text == "||") {
            const auto lhs = truth(*left);
            const auto rhs = truth(*right);
            if (record.text == "&&") {
                return logical_result(
                    lhs == Truth::false_value || rhs == Truth::false_value
                        ? Truth::false_value
                        : lhs == Truth::true_value
                                && rhs == Truth::true_value
                        ? Truth::true_value
                        : Truth::unknown);
            }
            return logical_result(
                lhs == Truth::true_value || rhs == Truth::true_value
                    ? Truth::true_value
                    : lhs == Truth::false_value
                            && rhs == Truth::false_value
                    ? Truth::false_value
                    : Truth::unknown);
        }
        if (record.text == "&" || record.text == "|"
            || record.text == "^" || record.text == "~^"
            || record.text == "^~") {
            return bitwise(*left, *right, record.text);
        }
        if (record.text == "==?" || record.text == "!=?") {
            auto result = wildcard_equal(*left, *right);
            if (record.text == "!=?" && result != Truth::unknown) {
                result = result == Truth::true_value
                    ? Truth::false_value : Truth::true_value;
            }
            return logical_result(result);
        }
        if (record.text == "==" || record.text == "!="
            || record.text == "=" || record.text == "/="
            || record.text == "===" || record.text == "!==") {
            const auto case_equality
                = record.text == "===" || record.text == "!==";
            if (!case_equality && (!left->known() || !right->known())) {
                return logical_result(Truth::unknown);
            }
            const auto width = std::max(left->width, right->width);
            const auto signed_value
                = left->signed_value && right->signed_value;
            const auto lhs = common_operand(*left, width, signed_value);
            const auto rhs = common_operand(*right, width, signed_value);
            auto equal = lhs.packed == rhs.packed;
            if (record.text == "!=" || record.text == "!=="
                || record.text == "/=") {
                equal = !equal;
            }
            return logical_result(
                equal ? Truth::true_value : Truth::false_value);
        }
        if (record.text == "<" || record.text == "<="
            || record.text == ">" || record.text == ">=") {
            if (!left->known() || !right->known()) {
                return logical_result(Truth::unknown);
            }
            const auto compared = compare_known(*left, *right);
            const auto result = record.text == "<" ? compared < 0
                : record.text == "<=" ? compared <= 0
                : record.text == ">" ? compared > 0
                                     : compared >= 0;
            return logical_result(
                result ? Truth::true_value : Truth::false_value);
        }
        if (record.text == "<<" || record.text == "<<<"
            || record.text == ">>" || record.text == ">>>") {
            if (!right->known()) {
                return make_unknown(left->width, left->signed_value);
            }
            const auto count = right->signed_value
                ? numeric_integer(*right) : unsigned_integer(*right);
            if (count < 0) {
                error_ = "constant shift count must be nonnegative";
                return std::nullopt;
            }
            auto result = *left;
            auto packed = PackedLogic4 { result.width, Logic4::zero };
            const auto amount = count >= result.width
                ? result.width
                : count.convert_to<std::uint32_t>();
            const auto arithmetic_right
                = record.text == ">>>" && result.signed_value;
            for (std::uint32_t bit = 0U; bit < result.width; ++bit) {
                std::optional<std::uint32_t> source;
                if (record.text == "<<" || record.text == "<<<") {
                    if (bit >= amount) {
                        source = bit - static_cast<std::uint32_t>(amount);
                    }
                } else if (bit + amount < result.width) {
                    source = bit + static_cast<std::uint32_t>(amount);
                }
                if (source) {
                    packed.set(bit, runtime::to_logic4(
                        result.packed.get_logic9(*source)));
                } else if (arithmetic_right) {
                    packed.set(bit, runtime::to_logic4(
                        result.packed.get_logic9(result.width - 1U)));
                }
            }
            result.packed = std::move(packed);
            return result;
        }
        const auto arithmetic = record.text == "+" || record.text == "-"
            || record.text == "*" || record.text == "/"
            || record.text == "%" || record.text == "**";
        if (!arithmetic) {
            error_ = "unsupported SystemVerilog binary constant operator '"
                + record.text + "'";
            return std::nullopt;
        }
        const auto width = std::max(left->width, right->width);
        const auto signed_value
            = left->signed_value && right->signed_value;
        const auto lhs = common_operand(*left, width, signed_value);
        const auto rhs = common_operand(*right, width, signed_value);
        if (!lhs.known() || !rhs.known()) {
            return make_unknown(width, signed_value);
        }
        const auto left_value = signed_value
            ? numeric_integer(lhs) : unsigned_integer(lhs);
        const auto right_value = signed_value
            ? numeric_integer(rhs) : unsigned_integer(rhs);
        cpp_int result { };
        if (record.text == "+") {
            result = left_value + right_value;
        } else if (record.text == "-") {
            result = left_value - right_value;
        } else if (record.text == "*") {
            if (static_cast<std::uint64_t>(width) * width
                > maximum_constant_work_units) {
                error_ = "arbitrary-width multiplicative constant evaluation "
                         "exceeds the work limit";
                return std::nullopt;
            }
            result = left_value * right_value;
        } else if (record.text == "/" || record.text == "%") {
            if (right_value == 0) {
                error_ = "division by zero in SystemVerilog constant "
                         "expression";
                return std::nullopt;
            }
            const auto minimum = -(cpp_int { 1U } << (width - 1U));
            if (signed_value && left_value == minimum
                && right_value == -1) {
                error_ = "constant division overflows the signed destination "
                         "width";
                return std::nullopt;
            }
            if (record.text == "/") {
                result = left_value / right_value;
            } else {
                result = left_value % right_value;
            }
        } else {
            if (right_value < 0) {
                if (left_value == 0) {
                    error_ = "constant zero to a negative power is undefined";
                    return std::nullopt;
                }
                result = left_value == 1 ? 1
                    : left_value == -1
                    ? ((right_value & 1) != 0 ? -1 : 1)
                    : 0;
            } else if (right_value
                > std::numeric_limits<std::uint64_t>::max()) {
                error_ = "constant exponent exceeds the work limit";
                return std::nullopt;
            } else {
                auto exponent = right_value.convert_to<std::uint64_t>();
                auto factor = left_value;
                result = 1;
                std::uint64_t work { };
                while (exponent != 0U) {
                    if ((exponent & 1U) != 0U) {
                        work += static_cast<std::uint64_t>(width) * width;
                        if (work > maximum_constant_work_units) {
                            error_ = "arbitrary-width power constant "
                                     "evaluation exceeds the work limit";
                            return std::nullopt;
                        }
                        result *= factor;
                        result %= cpp_int { 1U } << width;
                    }
                    exponent >>= 1U;
                    if (exponent != 0U) {
                        factor *= factor;
                        factor %= cpp_int { 1U } << width;
                    }
                }
            }
        }
        return make_value(
            packed_integer(result, width), signed_value, false,
            lhs.domain == rhs.domain ? lhs.domain
                                     : frontend::ValueDomain::Logic4);
    }

    [[nodiscard]] std::optional<std::uint32_t> normalized_index(
        const Value& base,
        const std::int64_t index) const
    {
        if (base.packed_range && base.packed_range->left
            && base.packed_range->right) {
            const auto left = *base.packed_range->left;
            const auto right = *base.packed_range->right;
            if (index < std::min(left, right)
                || index > std::max(left, right)) {
                return std::nullopt;
            }
            const auto offset = left >= right
                ? index - right : right - index;
            return static_cast<std::uint32_t>(offset);
        }
        if (index < 0
            || static_cast<std::uint64_t>(index) >= base.width) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(index);
    }

    [[nodiscard]] std::optional<Value> evaluate_selection(
        const semantic::sv::Expression& record)
    {
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            const auto base = evaluate(record.operands.front());
            const auto index = base
                ? evaluate(record.operands.back()) : std::nullopt;
            if (!base || !index || !index->known()) {
                return std::nullopt;
            }
            const auto integer = index->integer_value();
            auto packed = PackedLogic4 { 1U,
                base->domain == frontend::ValueDomain::Bit2
                    ? Logic4::zero : Logic4::x };
            if (integer) {
                if (const auto offset = normalized_index(*base, *integer)) {
                    packed.set(0U, runtime::to_logic4(
                        base->packed.get_logic9(*offset)));
                }
            }
            return make_value(
                std::move(packed), false, false, base->domain);
        }
        if (record.kind != ExpressionKind::slice
            || record.operands.size() != 3U) {
            return std::nullopt;
        }
        const auto base = evaluate(record.operands[0]);
        const auto first = base
            ? evaluate(record.operands[1]) : std::nullopt;
        const auto second = first
            ? evaluate(record.operands[2]) : std::nullopt;
        if (!base || !first || !second) {
            return std::nullopt;
        }
        const auto first_index = first->integer_value();
        const auto second_index = second->integer_value();
        if (!first_index || !second_index) {
            error_ = "constant part-select bounds must be known integers";
            return std::nullopt;
        }
        std::uint64_t width { };
        if (record.text == "+:" || record.text == "-:") {
            if (*second_index <= 0) {
                error_ = "constant indexed part-select width must be positive";
                return std::nullopt;
            }
            width = static_cast<std::uint64_t>(*second_index);
        } else {
            const auto distance = *first_index >= *second_index
                ? static_cast<std::uint64_t>(*first_index)
                    - static_cast<std::uint64_t>(*second_index)
                : static_cast<std::uint64_t>(*second_index)
                    - static_cast<std::uint64_t>(*first_index);
            width = distance + 1U;
        }
        if (width == 0U || width > maximum_constant_width) {
            error_ = "constant part-select width exceeds the resource limit";
            return std::nullopt;
        }
        const auto result_width = static_cast<std::uint32_t>(width);
        auto packed = PackedLogic4 { result_width,
            base->domain == frontend::ValueDomain::Bit2
                ? Logic4::zero : Logic4::x };
        for (std::uint32_t bit = 0U; bit < result_width; ++bit) {
            std::int64_t source_index { };
            if (record.text == "+:") {
                source_index = *first_index + bit;
            } else if (record.text == "-:") {
                source_index = *first_index
                    - static_cast<std::int64_t>(result_width - bit - 1U);
            } else if (*first_index >= *second_index) {
                source_index = *second_index + bit;
            } else {
                source_index = *second_index - bit;
            }
            if (const auto offset = normalized_index(*base, source_index)) {
                packed.set(bit, runtime::to_logic4(
                    base->packed.get_logic9(*offset)));
            }
        }
        return make_value(
            std::move(packed), false, false, base->domain);
    }

    [[nodiscard]] std::optional<Value> evaluate_concatenation(
        const semantic::sv::Expression& record)
    {
        std::size_t first { };
        std::uint64_t repetitions { 1U };
        if (record.kind == ExpressionKind::replication) {
            if (record.operands.size() < 2U) {
                error_ = "replication requires a count and an operand";
                return std::nullopt;
            }
            const auto count = evaluate(record.operands.front());
            if (!count) {
                return std::nullopt;
            }
            const auto converted = nonnegative_count(*count, error_);
            if (!converted) {
                return std::nullopt;
            }
            repetitions = *converted;
            first = 1U;
        }
        if (repetitions == 0U) {
            return make_value(
                PackedLogic4 { 0U, Logic4::zero },
                false,
                false,
                frontend::ValueDomain::Bit2);
        }
        std::vector<Value> operands;
        std::uint64_t element_width { };
        bool has_zero_width_operand { };
        for (auto index = first; index < record.operands.size(); ++index) {
            const auto candidate = specialization_.find_expression(
                record.operands[index]);
            if (candidate && candidate->systemverilog != nullptr
                && candidate->systemverilog->kind
                    == ExpressionKind::replication
                && candidate->systemverilog->operands.size() >= 2U) {
                const auto count = evaluate(
                    candidate->systemverilog->operands.front());
                if (!count) {
                    return std::nullopt;
                }
                const auto converted = nonnegative_count(*count, error_);
                if (!converted) {
                    return std::nullopt;
                }
                if (*converted == 0U) {
                    has_zero_width_operand = true;
                    continue;
                }
            }
            auto operand = evaluate(record.operands[index]);
            if (!operand) {
                return std::nullopt;
            }
            if (operand->width == 0U) {
                has_zero_width_operand = true;
                continue;
            }
            if (operand->width > maximum_constant_width - element_width) {
                error_ = "concatenation element width exceeds the resource "
                         "limit";
                return std::nullopt;
            }
            element_width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (element_width == 0U && has_zero_width_operand) {
            return make_value(
                PackedLogic4 { 0U, Logic4::zero },
                false,
                false,
                frontend::ValueDomain::Bit2);
        }
        if (element_width == 0U
            || repetitions > maximum_constant_width / element_width) {
            error_ = "concatenation result exceeds the constant-width "
                     "resource limit";
            return std::nullopt;
        }
        const auto result_width = element_width * repetitions;
        if (operands.size() == 1U && element_width == 1U
            && result_width <= maximum_constant_work_units / 64U) {
            return make_value(
                PackedLogic4 {
                    static_cast<std::size_t>(result_width),
                    runtime::to_logic4(
                        operands.front().packed.get_logic9(0U)),
                },
                false,
                false,
                operands.front().domain);
        }
        if (operands.empty() || repetitions == 0U
            || repetitions
                > maximum_constant_work_units / operands.size()) {
            error_ = "concatenation or replication exceeds the constant-"
                     "evaluation work limit";
            return std::nullopt;
        }
        const auto work_multiplicity = repetitions * operands.size();
        if (result_width
            > maximum_constant_work_units / work_multiplicity) {
            error_ = "concatenation or replication exceeds the constant-"
                     "evaluation work limit";
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(result_width);
        auto result = make_value(
            PackedLogic4 { width, Logic4::zero }, false, false);
        for (std::uint64_t repetition = 0U;
            repetition < repetitions; ++repetition) {
            for (const auto& operand : operands) {
                append_packed(result, operand);
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<Value> evaluate_query(
        const semantic::sv::Expression& record)
    {
        if (record.operands.empty()) {
            return std::nullopt;
        }
        const auto operand = evaluate(record.operands.front());
        if (!operand) {
            return std::nullopt;
        }
        if (record.operands.size() == 2U) {
            const auto dimension = evaluate(record.operands.back());
            const auto selected = dimension
                ? nonnegative_count(*dimension, error_) : std::nullopt;
            if (!selected || *selected != 1U) {
                if (selected) {
                    error_ = record.text
                        + " supports only packed dimension 1";
                }
                return std::nullopt;
            }
        }
        const auto left = operand->packed_range
                && operand->packed_range->left
            ? *operand->packed_range->left
            : static_cast<std::int64_t>(operand->width - 1U);
        const auto right = operand->packed_range
                && operand->packed_range->right
            ? *operand->packed_range->right : 0;
        std::int64_t result { };
        if (record.text == "$bits" || record.text == "$size") {
            result = operand->width;
        } else if (record.text == "$left") {
            result = left;
        } else if (record.text == "$right") {
            result = right;
        } else if (record.text == "$low") {
            result = std::min(left, right);
        } else if (record.text == "$high") {
            result = std::max(left, right);
        } else if (record.text == "$increment") {
            result = left >= right ? 1 : -1;
        } else if (record.text == "$dimensions") {
            result = 1;
        } else if (record.text == "$unpacked_dimensions") {
            result = 0;
        }
        return make_known(
            static_cast<std::uint64_t>(result), 32U, true, false,
            frontend::ValueDomain::Integer);
    }

    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    declaration_type(const semantic::DeclarationId declaration_id) const
    {
        const semantic::CompiledDesignResolver resolver { specialization_ };
        const auto selected_id = resolver.actual_declaration(declaration_id)
            .value_or(declaration_id);
        const auto declaration
            = specialization_.find_declaration(selected_id);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        std::optional<semantic::sv::TypeReference> selected;
        if (declaration->systemverilog->type) {
            selected = declaration->systemverilog->type;
        } else if (declaration->systemverilog->declared_type) {
            const auto definition = specialization_.find_type(
                *declaration->systemverilog->declared_type);
            if (definition && definition->systemverilog != nullptr) {
                selected = definition->systemverilog->base;
            }
        }
        if (!selected) {
            return std::nullopt;
        }
        return resolver.effective_systemverilog_type(
                   *selected, declaration->systemverilog->scope)
            .value_or(*selected);
    }

    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    named_cast_type(const semantic::sv::Expression& record,
                    const std::string_view name) const
    {
        if (record.referenced_name
            && record.referenced_name->selected) {
            if (const auto type = declaration_type(
                    *record.referenced_name->selected)) {
                return type;
            }
        }
        const auto selected
            = semantic::CompiledDesignResolver { specialization_ }
                  .resolve_systemverilog_named_type(
                      name, record.scope)
                  .unique();
        return selected ? declaration_type(*selected) : std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_call(
        const semantic::sv::Expression& record)
    {
        if ((record.text == "$signed" || record.text == "$unsigned")
            && record.operands.size() == 1U) {
            auto result = evaluate(record.operands.front());
            if (result) {
                result->signed_value = record.text == "$signed";
            }
            return result;
        }
        if (record.text.starts_with("@sv-cast:")
            && record.operands.size() == 1U) {
            auto result = evaluate(record.operands.front());
            if (!result) {
                return std::nullopt;
            }
            const auto name = std::string_view { record.text }.substr(
                std::string_view { "@sv-cast:" }.size());
            std::optional<semantic::sv::TypeReference> type;
            if (const auto width = parse_integer<std::uint32_t>(name)) {
                type.emplace();
                type->executable_width = *width;
                type->four_state = true;
                type->target.spelling = std::string { name };
            } else if (name == "bit" || name == "logic"
                || name == "reg") {
                type.emplace();
                type->executable_width = 1U;
                type->four_state = name != "bit";
                type->target.spelling = std::string { name };
            } else if (name == "byte" || name == "shortint"
                || name == "int" || name == "longint"
                || name == "integer") {
                type.emplace();
                type->executable_width = name == "byte" ? 8U
                    : name == "shortint" ? 16U
                    : name == "longint" ? 64U : 32U;
                type->signed_value = true;
                type->four_state = name == "integer";
                type->target.spelling = std::string { name };
            } else {
                type = named_cast_type(record, name);
            }
            if (!type) {
                error_ = "cannot resolve SystemVerilog cast type '"
                    + std::string { name } + "'";
                return std::nullopt;
            }
            return convert_hir_systemverilog_constant(
                std::move(*result), *type, error_, &specialization_);
        }
        if ((record.text == "$isunknown"
                || record.text == "$isunbounded")
            && record.operands.size() == 1U) {
            const auto operand = evaluate(record.operands.front());
            if (!operand) {
                return std::nullopt;
            }
            const auto result = record.text == "$isunknown"
                ? !operand->known() : operand->unbounded;
            return make_known(
                result ? 1U : 0U, 1U, false, false,
                frontend::ValueDomain::Bit2);
        }
        if (record.text == "$clog2" && record.operands.size() == 1U) {
            const auto operand = evaluate(record.operands.front());
            if (!operand || !operand->known()) {
                if (operand) {
                    error_ = "$clog2 argument contains X or Z";
                }
                return std::nullopt;
            }
            const auto value = numeric_integer(*operand);
            if (value < 0) {
                error_ = "$clog2 requires a nonnegative integral argument";
                return std::nullopt;
            }
            std::uint32_t result { };
            if (value > 1) {
                const auto highest = boost::multiprecision::msb(value);
                result = static_cast<std::uint32_t>(highest
                    + ((value & (value - 1)) != 0 ? 1U : 0U));
            }
            return make_known(
                result, 32U, true, false,
                frontend::ValueDomain::Integer);
        }
        const auto query = record.text == "$bits"
            || record.text == "$left" || record.text == "$right"
            || record.text == "$low" || record.text == "$high"
            || record.text == "$size" || record.text == "$increment"
            || record.text == "$dimensions"
            || record.text == "$unpacked_dimensions";
        if (query) {
            return evaluate_query(record);
        }
        if (record.text == "?:" && record.operands.size() == 3U) {
            const auto condition = evaluate(record.operands.front());
            if (!condition) {
                return std::nullopt;
            }
            const auto state = truth(*condition);
            if (state != Truth::unknown) {
                auto result = evaluate(record.operands[
                    state == Truth::true_value ? 1U : 2U]);
                const auto when_true_width = expression_width(
                    record.operands[1]);
                const auto when_false_width = expression_width(
                    record.operands[2]);
                if (result && when_true_width && when_false_width) {
                    result = common_operand(
                        *result,
                        std::max(*when_true_width, *when_false_width),
                        result->signed_value);
                }
                return result;
            }
            auto when_true = evaluate(record.operands[1]);
            auto when_false = evaluate(record.operands[2]);
            if (!when_true || !when_false) {
                return std::nullopt;
            }
            const auto width = std::max(
                when_true->width, when_false->width);
            const auto signed_value
                = when_true->signed_value && when_false->signed_value;
            const auto lhs = common_operand(
                *when_true, width, signed_value);
            const auto rhs = common_operand(
                *when_false, width, signed_value);
            auto packed = PackedLogic4 { width, Logic4::x };
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                const auto left = lhs.packed.get_logic9(bit);
                const auto right = rhs.packed.get_logic9(bit);
                if (left == right) {
                    packed.set(bit, runtime::to_logic4(left));
                }
            }
            return make_value(
                std::move(packed), signed_value, false,
                lhs.domain == rhs.domain ? lhs.domain
                                         : frontend::ValueDomain::Logic4);
        }
        if (record.text == "inside" && record.operands.size() >= 2U) {
            const auto left = evaluate(record.operands.front());
            if (!left) {
                return std::nullopt;
            }
            auto accumulated = Truth::false_value;
            for (std::size_t index = 1U;
                index < record.operands.size(); ++index) {
                const auto item = specialization_.find_expression(
                    record.operands[index]);
                auto matched = Truth::false_value;
                if (item && item->systemverilog != nullptr
                    && item->systemverilog->kind == ExpressionKind::call
                    && item->systemverilog->text == "@inside-range"
                    && item->systemverilog->operands.size() == 2U) {
                    const auto low = evaluate(
                        item->systemverilog->operands.front());
                    const auto high = evaluate(
                        item->systemverilog->operands.back());
                    if (!low || !high || !low->known() || !high->known()
                        || !left->known()) {
                        matched = Truth::unknown;
                    } else if (compare_known(*low, *high) <= 0
                        && compare_known(*left, *low) >= 0
                        && compare_known(*left, *high) <= 0) {
                        matched = Truth::true_value;
                    }
                } else {
                    const auto value = evaluate(record.operands[index]);
                    if (!value) {
                        return std::nullopt;
                    }
                    matched = wildcard_equal(*left, *value);
                }
                if (matched == Truth::true_value) {
                    return logical_result(Truth::true_value);
                }
                if (matched == Truth::unknown) {
                    accumulated = Truth::unknown;
                }
            }
            return logical_result(accumulated);
        }
        if ((record.text == "@stream-left"
                || record.text == "@stream-right")
            && record.operands.size() >= 2U) {
            const auto slice = evaluate(record.operands.front());
            const auto slice_size = slice
                ? nonnegative_count(*slice, error_) : std::nullopt;
            if (!slice_size || *slice_size == 0U) {
                return std::nullopt;
            }
            semantic::sv::Expression concatenation = record;
            concatenation.kind = ExpressionKind::concatenation;
            concatenation.operands.erase(concatenation.operands.begin());
            auto result = evaluate_concatenation(concatenation);
            if (!result || record.text == "@stream-right"
                || *slice_size >= result->width) {
                return result;
            }
            auto packed = PackedLogic4 { result->width, Logic4::zero };
            for (std::uint64_t offset = 0U;
                offset < result->width;) {
                const auto chunk = std::min<std::uint64_t>(
                    *slice_size, result->width - offset);
                const auto destination = result->width - offset - chunk;
                for (std::uint64_t bit = 0U; bit < chunk; ++bit) {
                    packed.set(destination + bit,
                        runtime::to_logic4(
                            result->packed.get_logic9(offset + bit)));
                }
                offset += chunk;
            }
            result->packed = std::move(packed);
            return result;
        }
        if (record.referenced_name && record.referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *record.referenced_name->selected);
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::function) {
                return evaluate_callable(
                    *declaration->systemverilog, record);
            }
        }
        error_ = "unsupported SystemVerilog integral constant call '"
            + record.text + "'";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_pattern(
        const semantic::sv::Expression& record)
    {
        if (record.associations.size() == 1U
            && record.associations.front().choice_spelling == "default") {
            auto value = evaluate(record.associations.front().value);
            if (!value) {
                return std::nullopt;
            }
            auto packed = PackedLogic4 { 1U, Logic4::x };
            packed.set(0U, runtime::to_logic4(
                value->packed.get_logic9(0U)));
            return make_value(
                std::move(packed), true, true, value->domain);
        }
        if (std::ranges::all_of(record.associations,
                [](const auto& association) {
                    return association.choice_spelling.empty();
                })) {
            semantic::sv::Expression concatenation = record;
            concatenation.kind = ExpressionKind::concatenation;
            return evaluate_concatenation(concatenation);
        }
        if (record.associations.size() == 1U) {
            return evaluate(record.associations.front().value);
        }
        error_ = "keyed assignment pattern requires resolved aggregate layout";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_callable(
        const semantic::sv::Declaration& callable,
        const semantic::sv::Expression& call)
    {
        if (!callable.callable || frames_.size() >= maximum_constant_call_depth) {
            error_ = "constant function recursion exceeds the call-depth limit";
            return std::nullopt;
        }
        Frame frame;
        frame.callable = callable.id;
        const auto& formals = callable.callable->formals;
        for (std::size_t index = 0U; index < formals.size(); ++index) {
            const auto formal = specialization_.find_declaration(formals[index]);
            std::optional<Value> value;
            if (index < call.operands.size()) {
                value = evaluate(call.operands[index]);
            } else if (formal && formal->systemverilog != nullptr
                && formal->systemverilog->initializer) {
                value = evaluate(*formal->systemverilog->initializer);
            }
            if (!value) {
                error_ = "cannot evaluate constant function argument";
                return std::nullopt;
            }
            if (formal && formal->systemverilog != nullptr
                && formal->systemverilog->type
                && hir_systemverilog_explicit_integral_type(
                    *formal->systemverilog->type)) {
                value = convert_hir_systemverilog_constant(
                    std::move(*value), *formal->systemverilog->type, error_,
                    &specialization_);
                if (!value) {
                    return std::nullopt;
                }
            }
            frame.values.emplace(formals[index], std::move(*value));
        }
        auto result = make_known(0U, 32U, true, false);
        if (callable.callable->return_type.executable_width) {
            result = *convert_hir_systemverilog_constant(
                std::move(result), callable.callable->return_type, error_,
                &specialization_);
        }
        frame.values.emplace(callable.id, result);
        frames_.push_back(std::move(frame));
        for (const auto statement : callable.statements) {
            const auto flow = execute_statement(statement);
            if (flow == Flow::failed) {
                frames_.pop_back();
                return std::nullopt;
            }
            if (flow == Flow::returned) {
                break;
            }
        }
        auto value = frames_.back().result.value_or(
            frames_.back().values.at(callable.id));
        frames_.pop_back();
        return convert_hir_systemverilog_constant(
            std::move(value), callable.callable->return_type, error_,
            &specialization_);
    }

    [[nodiscard]] bool assign_target(
        const semantic::ExpressionId target,
        Value value)
    {
        const auto view = specialization_.find_expression(target);
        if (!view || view->systemverilog == nullptr || frames_.empty()) {
            return false;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::name
            && record.referenced_name
            && record.referenced_name->selected) {
            const auto declaration = *record.referenced_name->selected;
            const auto formal = specialization_.find_declaration(declaration);
            if (formal && formal->systemverilog != nullptr
                && formal->systemverilog->type
                && hir_systemverilog_explicit_integral_type(
                    *formal->systemverilog->type)) {
                auto converted = convert_hir_systemverilog_constant(
                    std::move(value), *formal->systemverilog->type, error_,
                    &specialization_);
                if (!converted) {
                    return false;
                }
                value = std::move(*converted);
            }
            for (auto frame = frames_.rbegin();
                frame != frames_.rend(); ++frame) {
                if (frame->values.contains(declaration)) {
                    frame->values.insert_or_assign(
                        declaration, std::move(value));
                    return true;
                }
            }
            frames_.back().values.insert_or_assign(
                declaration, std::move(value));
            return true;
        }
        if ((record.kind == ExpressionKind::index
                || record.kind == ExpressionKind::slice)
            && !record.operands.empty()) {
            const auto base_view = specialization_.find_expression(
                record.operands.front());
            if (!base_view || base_view->systemverilog == nullptr
                || base_view->systemverilog->kind != ExpressionKind::name
                || !base_view->systemverilog->referenced_name
                || !base_view->systemverilog->referenced_name->selected) {
                return false;
            }
            const auto declaration
                = *base_view->systemverilog->referenced_name->selected;
            auto base = value_for_declaration(declaration);
            if (!base) {
                return false;
            }
            if (record.kind == ExpressionKind::index
                && record.operands.size() == 2U) {
                const auto index = evaluate(record.operands.back());
                const auto integer = index ? index->integer_value()
                                           : std::nullopt;
                const auto offset = integer
                    ? normalized_index(*base, *integer) : std::nullopt;
                if (!offset) {
                    return true;
                }
                base->packed.set(*offset, runtime::to_logic4(
                    value.packed.get_logic9(0U)));
            } else if (record.operands.size() == 3U) {
                const auto first = evaluate(record.operands[1]);
                const auto second = evaluate(record.operands[2]);
                const auto first_value = first ? first->integer_value()
                                               : std::nullopt;
                const auto second_value = second ? second->integer_value()
                                                 : std::nullopt;
                if (!first_value || !second_value) {
                    return false;
                }
                const auto width = record.text == "+:"
                        || record.text == "-:"
                    ? static_cast<std::uint32_t>(*second_value)
                    : static_cast<std::uint32_t>(
                        std::max(*first_value, *second_value)
                        - std::min(*first_value, *second_value) + 1U);
                value = resized(std::move(value), width);
                for (std::uint32_t bit = 0U; bit < width; ++bit) {
                    const auto logical = record.text == "+:"
                        ? *first_value + bit
                        : record.text == "-:"
                        ? *first_value
                            - static_cast<std::int64_t>(width - bit - 1U)
                        : *first_value >= *second_value
                        ? *second_value + bit
                        : *second_value - bit;
                    if (const auto offset = normalized_index(*base, logical)) {
                        base->packed.set(*offset, runtime::to_logic4(
                            value.packed.get_logic9(bit)));
                    }
                }
            }
            for (auto frame = frames_.rbegin();
                frame != frames_.rend(); ++frame) {
                if (frame->values.contains(declaration)) {
                    frame->values.insert_or_assign(
                        declaration, std::move(*base));
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<bool> match_pattern(
        const semantic::ExpressionId expression,
        const Value& selector)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr || frames_.empty()) {
            return std::nullopt;
        }
        const auto& pattern = *view->systemverilog;
        if (pattern.kind == ExpressionKind::call) {
            if (pattern.text == "@match-wildcard") {
                return true;
            }
            if (pattern.text == "@match-guard") {
                if (pattern.operands.size() != 2U) {
                    return std::nullopt;
                }
                const auto bindings = frames_.back().pattern_values;
                const auto matched = match_pattern(
                    pattern.operands[0], selector);
                if (!matched || !*matched) {
                    frames_.back().pattern_values = bindings;
                    return matched;
                }
                const auto guard = evaluate(pattern.operands[1]);
                if (!guard || truth(*guard) == Truth::unknown) {
                    frames_.back().pattern_values = bindings;
                    return std::nullopt;
                }
                const auto accepted = truth(*guard) == Truth::true_value;
                if (!accepted) {
                    frames_.back().pattern_values = bindings;
                }
                return accepted;
            }
            constexpr auto bind_prefix
                = std::string_view { "@match-bind:" };
            if (pattern.text.starts_with(bind_prefix)) {
                const auto name = pattern.text.substr(bind_prefix.size());
                if (name.empty()) {
                    return std::nullopt;
                }
                frames_.back().pattern_values.insert_or_assign(
                    name, selector);
                return true;
            }
            return std::nullopt;
        }
        const auto candidate = evaluate(expression);
        return candidate && candidate->known() && selector.known()
            ? std::optional {
                  compare_known(*candidate, selector) == 0 }
            : std::nullopt;
    }

    [[nodiscard]] std::optional<bool> match_inside_choice(
        const semantic::ExpressionId expression,
        const Value& selector)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& choice = *view->systemverilog;
        if (choice.kind == ExpressionKind::call
            && choice.text == "@inside-range") {
            if (choice.operands.size() != 2U) {
                return std::nullopt;
            }
            const auto left = evaluate(choice.operands.front());
            const auto right = evaluate(choice.operands.back());
            if (!left || !right || !left->known() || !right->known()
                || !selector.known()) {
                return std::nullopt;
            }
            const auto& low = compare_known(*left, *right) <= 0
                ? *left : *right;
            const auto& high = compare_known(*left, *right) <= 0
                ? *right : *left;
            return compare_known(selector, low) >= 0
                && compare_known(selector, high) <= 0;
        }
        const auto candidate = evaluate(expression);
        if (!candidate) {
            return std::nullopt;
        }
        const auto matched = wildcard_equal(selector, *candidate);
        return matched == Truth::unknown
            ? std::nullopt
            : std::optional { matched == Truth::true_value };
    }

    [[nodiscard]] bool initialize_declarations(
        const semantic::sv::Statement& statement)
    {
        for (const auto declaration_id : statement.declarations) {
            const auto declaration
                = specialization_.find_declaration(declaration_id);
            if (!declaration || declaration->systemverilog == nullptr
                || !declaration->systemverilog->initializer) {
                continue;
            }
            const auto& source = *declaration->systemverilog;
            auto value = evaluate(*source.initializer);
            if (!value) {
                return false;
            }
            if (source.type
                && hir_systemverilog_explicit_integral_type(*source.type)) {
                value = convert_hir_systemverilog_constant(
                    std::move(*value), *source.type, error_,
                    &specialization_);
                if (!value) {
                    return false;
                }
            }
            frames_.back().values.insert_or_assign(
                declaration_id, std::move(*value));
        }
        return true;
    }

    [[nodiscard]] Flow execute_loop(
        const semantic::sv::Statement& record)
    {
        if (!initialize_declarations(record)) {
            return Flow::failed;
        }
        if (record.loop_repeat) {
            if (!record.loop_limit) {
                return Flow::failed;
            }
            const auto count_value = evaluate(*record.loop_limit);
            const auto count = count_value
                ? nonnegative_count(*count_value, error_)
                : std::nullopt;
            if (!count || *count > maximum_constant_work_units) {
                if (count && error_.empty()) {
                    error_ = "constant function loop exceeds the work limit";
                }
                return Flow::failed;
            }
            for (std::uint64_t iteration = 0U;
                iteration < *count; ++iteration) {
                const auto flow = execute_statements(record.statements);
                if (flow == Flow::returned || flow == Flow::failed) {
                    return flow;
                }
                if (flow == Flow::broke) {
                    break;
                }
            }
            return Flow::normal;
        }

        if (record.loop_initial) {
            const auto target = record.target
                ? record.target : record.loop_update_target;
            auto initial = evaluate(*record.loop_initial);
            if (!target || !initial
                || !assign_target(*target, std::move(*initial))) {
                return Flow::failed;
            }
        }
        for (std::uint64_t iteration = 0U;
            iteration < maximum_constant_work_units; ++iteration) {
            if (!record.loop_post_test && record.condition) {
                const auto condition = evaluate(*record.condition);
                if (!condition || truth(*condition) == Truth::unknown) {
                    error_ = "constant function loop condition is unknown";
                    return Flow::failed;
                }
                if (truth(*condition) == Truth::false_value) {
                    return Flow::normal;
                }
            }
            const auto body = execute_statements(record.statements);
            if (body == Flow::returned || body == Flow::failed) {
                return body;
            }
            if (body == Flow::broke) {
                return Flow::normal;
            }

            if (record.value) {
                const auto target = record.loop_update_target
                    ? record.loop_update_target : record.target;
                auto value = evaluate(*record.value);
                if (!target || !value
                    || !assign_target(*target, std::move(*value))) {
                    return Flow::failed;
                }
            }
            const auto update = execute_statements(record.loop_updates);
            if (update == Flow::returned || update == Flow::failed) {
                return update;
            }
            if (update == Flow::broke) {
                return Flow::normal;
            }
            if (record.loop_post_test && record.condition) {
                const auto condition = evaluate(*record.condition);
                if (!condition || truth(*condition) == Truth::unknown) {
                    error_ = "constant function loop condition is unknown";
                    return Flow::failed;
                }
                if (truth(*condition) == Truth::false_value) {
                    return Flow::normal;
                }
            }
        }
        error_ = "constant function loop exceeds the work limit";
        return Flow::failed;
    }

    [[nodiscard]] Flow execute_statement(
        const semantic::StatementId statement)
    {
        const auto view = specialization_.find_statement(statement);
        if (!view || view->systemverilog == nullptr) {
            return Flow::failed;
        }
        const auto& record = *view->systemverilog;
        using Kind = semantic::sv::StatementKind;
        if (record.kind == Kind::assignment) {
            if (!record.target || !record.value) {
                return Flow::failed;
            }
            auto value = evaluate(*record.value);
            return value && assign_target(*record.target, std::move(*value))
                ? Flow::normal : Flow::failed;
        }
        if (record.kind == Kind::return_statement) {
            if (record.value) {
                frames_.back().result = evaluate(*record.value);
                if (!frames_.back().result) {
                    return Flow::failed;
                }
            }
            return Flow::returned;
        }
        if (record.kind == Kind::conditional) {
            if (!record.condition) {
                return Flow::failed;
            }
            const auto condition = evaluate(*record.condition);
            if (!condition || truth(*condition) == Truth::unknown) {
                error_ = "constant function condition is unknown";
                return Flow::failed;
            }
            const auto& selected = truth(*condition) == Truth::true_value
                ? record.statements : record.else_statements;
            return execute_statements(selected);
        }
        if (record.kind == Kind::block) {
            if (!initialize_declarations(record)) {
                return Flow::failed;
            }
            return execute_statements(record.statements);
        }
        if (record.kind == Kind::loop) {
            return execute_loop(record);
        }
        if (record.kind == Kind::break_loop) {
            return Flow::broke;
        }
        if (record.kind == Kind::continue_loop) {
            return Flow::continued;
        }
        if (record.kind == Kind::selection) {
            if (!record.condition) {
                return Flow::failed;
            }
            const auto selector = evaluate(*record.condition);
            if (!selector) {
                return Flow::failed;
            }
            const semantic::sv::CaseAlternative* fallback { };
            for (const auto& alternative : record.case_alternatives) {
                if (alternative.is_default) {
                    fallback = &alternative;
                    continue;
                }
                for (const auto choice : alternative.choices) {
                    const auto bindings = frames_.back().pattern_values;
                    const auto matched = record.case_match
                            == semantic::sv::CaseMatchKind::matches
                        ? match_pattern(choice, *selector)
                        : record.case_match
                                == semantic::sv::CaseMatchKind::inside
                        ? match_inside_choice(choice, *selector)
                        : [&]() -> std::optional<bool> {
                              const auto candidate = evaluate(choice);
                              return candidate && candidate->known()
                                      && selector->known()
                                  ? std::optional {
                                        compare_known(
                                            *candidate, *selector)
                                        == 0 }
                                  : std::nullopt;
                          }();
                    if (!matched) {
                        frames_.back().pattern_values = bindings;
                        return Flow::failed;
                    }
                    if (*matched) {
                        const auto flow = execute_statements(
                            alternative.statements);
                        frames_.back().pattern_values = bindings;
                        return flow;
                    }
                    frames_.back().pattern_values = bindings;
                }
            }
            return fallback ? execute_statements(fallback->statements)
                            : Flow::normal;
        }
        if (record.kind == Kind::null_statement) {
            return Flow::normal;
        }
        error_ = "unsupported statement in SystemVerilog constant function";
        return Flow::failed;
    }

    [[nodiscard]] Flow execute_statements(
        const std::vector<semantic::StatementId>& statements)
    {
        for (const auto statement : statements) {
            const auto flow = execute_statement(statement);
            if (flow != Flow::normal) {
                return flow;
            }
        }
        return Flow::normal;
    }

    [[nodiscard]] std::optional<Value> evaluate_impl(
        const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::boolean_literal) {
            return make_known(
                record.text == "true" || record.text == "1" ? 1U : 0U,
                1U, false, false);
        }
        if (record.kind == ExpressionKind::integer_literal
            || record.kind == ExpressionKind::logic_literal) {
            return parse_literal(record.text,
                record.kind == ExpressionKind::integer_literal, error_);
        }
        if (record.kind == ExpressionKind::name) {
            return evaluate_name(record);
        }
        if (record.kind == ExpressionKind::unary) {
            return evaluate_unary(record);
        }
        if (record.kind == ExpressionKind::binary) {
            return evaluate_binary(record);
        }
        if (record.kind == ExpressionKind::index
            || record.kind == ExpressionKind::slice) {
            return evaluate_selection(record);
        }
        if (record.kind == ExpressionKind::concatenation
            || record.kind == ExpressionKind::replication) {
            return evaluate_concatenation(record);
        }
        if (record.kind == ExpressionKind::assignment_pattern) {
            return evaluate_pattern(record);
        }
        if (record.kind == ExpressionKind::call) {
            return evaluate_call(record);
        }
        error_ = "expression form is not a supported SystemVerilog integral "
                 "constant expression";
        return std::nullopt;
    }

    const semantic::SpecializedHirUnit& specialization_;
    std::string& error_;
    std::unordered_set<std::uint32_t> active_expressions_;
    std::unordered_set<std::uint32_t> active_declarations_;
    std::vector<Frame> frames_;
};

[[nodiscard]] bool real_scalar_kind(const ScalarKind kind) noexcept
{
    return kind == ScalarKind::ShortReal || kind == ScalarKind::Real
        || kind == ScalarKind::Realtime;
}

[[nodiscard]] ScalarKind scalar_kind(
    const semantic::sv::ScalarKind kind) noexcept
{
    return static_cast<ScalarKind>(kind);
}

[[nodiscard]] ScalarKind scalar_kind(
    const semantic::sv::TypeReference& type) noexcept
{
    const auto& spelling = type.target.spelling;
    if (spelling == "shortreal") {
        return ScalarKind::ShortReal;
    }
    if (spelling == "real") {
        return ScalarKind::Real;
    }
    if (spelling == "realtime") {
        return ScalarKind::Realtime;
    }
    if (spelling == "time") {
        return ScalarKind::Time;
    }
    if (spelling == "chandle") {
        return ScalarKind::Chandle;
    }
    return ScalarKind::None;
}

[[nodiscard]] ScalarKind promote_scalar_kind(
    const ScalarKind left, const ScalarKind right) noexcept
{
    if (left == ScalarKind::Chandle || right == ScalarKind::Chandle) {
        return ScalarKind::Chandle;
    }
    if (left == ScalarKind::Real || right == ScalarKind::Real) {
        return ScalarKind::Real;
    }
    if (left == ScalarKind::Realtime || right == ScalarKind::Realtime) {
        return ScalarKind::Realtime;
    }
    if (left == ScalarKind::ShortReal || right == ScalarKind::ShortReal) {
        return ScalarKind::ShortReal;
    }
    if (left == ScalarKind::Time || right == ScalarKind::Time) {
        return ScalarKind::Time;
    }
    return ScalarKind::None;
}

[[nodiscard]] Scalar scalar_integer(
    const std::int64_t value,
    const ScalarKind kind = ScalarKind::None) noexcept
{
    return { kind, std::bit_cast<std::uint64_t>(value) };
}

[[nodiscard]] Scalar scalar_real(
    const double value, const ScalarKind kind) noexcept
{
    return kind == ScalarKind::ShortReal
        ? Scalar { kind,
              std::bit_cast<std::uint32_t>(static_cast<float>(value)) }
        : Scalar { kind, std::bit_cast<std::uint64_t>(value) };
}

[[nodiscard]] std::optional<double> scalar_number(
    const Scalar& value) noexcept
{
    if (const auto real = value.real()) {
        return real;
    }
    if (const auto integer = value.integral()) {
        return static_cast<double>(*integer);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> physical_unit_femtoseconds(
    const std::string_view unit) noexcept
{
    if (unit == "fs") {
        return 1U;
    }
    if (unit == "ps") {
        return 1'000U;
    }
    if (unit == "ns") {
        return 1'000'000U;
    }
    if (unit == "us") {
        return 1'000'000'000U;
    }
    if (unit == "ms") {
        return 1'000'000'000'000U;
    }
    if (unit == "s") {
        return 1'000'000'000'000'000U;
    }
    return std::nullopt;
}

[[nodiscard]] std::uint64_t scalar_time_scale(
    const std::string_view spelling) noexcept
{
    if (spelling.empty()) {
        return 1U;
    }
    std::uint64_t magnitude { };
    const auto parsed = std::from_chars(
        spelling.data(), spelling.data() + spelling.size(), magnitude);
    if (parsed.ec != std::errc { } || parsed.ptr == spelling.data()) {
        return 1U;
    }
    const auto suffix = std::string_view {
        parsed.ptr,
        static_cast<std::size_t>(
            spelling.data() + spelling.size() - parsed.ptr)
    };
    const auto factor = physical_unit_femtoseconds(suffix);
    if (!factor
        || magnitude > std::numeric_limits<std::uint64_t>::max() / *factor) {
        return 1U;
    }
    return magnitude * *factor;
}

[[nodiscard]] std::optional<double> exact_decimal(
    const semantic::sv::DecimalLiteral& literal,
    std::string& error)
{
    const auto spelling = literal.digits + "e"
        + std::to_string(literal.decimal_exponent);
    double value { };
    const auto parsed = std::from_chars(
        spelling.data(), spelling.data() + spelling.size(), value,
        std::chars_format::scientific);
    if (parsed.ec != std::errc { }
        || parsed.ptr != spelling.data() + spelling.size()
        || !std::isfinite(value)) {
        error = "real/time literal is outside deterministic IEEE-754 range";
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<semantic::UnitId> scope_unit(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ScopeId scope) noexcept
{
    const auto& scopes = specialization.design().semantics.scopes();
    if (!scope.valid() || scope.value() >= scopes.size()) {
        return std::nullopt;
    }
    return scopes[scope.value()].unit;
}

[[nodiscard]] std::optional<Scalar> parse_scalar_identity(
    const std::string_view identity)
{
    constexpr auto prefix = std::string_view { "svscalar-v1:k=" };
    if (!identity.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto bits_marker = identity.find(":b=", prefix.size());
    if (bits_marker == std::string_view::npos) {
        return std::nullopt;
    }
    const auto kind_text = identity.substr(
        prefix.size(), bits_marker - prefix.size());
    const auto bits_text = identity.substr(bits_marker + 3U);
    const auto kind = parse_integer<unsigned>(kind_text);
    std::uint64_t bits { };
    const auto parsed = std::from_chars(
        bits_text.data(), bits_text.data() + bits_text.size(), bits, 16);
    if (!kind || *kind > static_cast<unsigned>(ScalarKind::Chandle)
        || bits_text.empty() || bits_text.size() > 16U
        || parsed.ec != std::errc { }
        || parsed.ptr != bits_text.data() + bits_text.size()) {
        return std::nullopt;
    }
    return Scalar { static_cast<ScalarKind>(*kind), bits };
}

[[nodiscard]] std::optional<std::int64_t> checked_scalar_arithmetic(
    const std::int64_t left,
    const std::int64_t right,
    const std::string_view operation) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (operation == "+") {
        if ((right > 0 && left > maximum - right)
            || (right < 0 && left < minimum - right)) {
            return std::nullopt;
        }
        return left + right;
    }
    if (operation == "-") {
        if ((right < 0 && left > maximum + right)
            || (right > 0 && left < minimum + right)) {
            return std::nullopt;
        }
        return left - right;
    }
    if (operation == "*") {
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
    return std::nullopt;
}

class HirScalarEvaluator final {
public:
    HirScalarEvaluator(
        const semantic::SpecializedHirUnit& specialization,
        std::string& error)
        : specialization_(specialization)
        , error_(error)
    {
    }

    [[nodiscard]] std::optional<Scalar> evaluate(
        const semantic::ExpressionId expression)
    {
        if (!active_expressions_.insert(expression.value()).second) {
            error_ = "recursive SystemVerilog scalar constant expression";
            return std::nullopt;
        }
        auto result = evaluate_impl(expression);
        active_expressions_.erase(expression.value());
        return result;
    }

    [[nodiscard]] std::optional<Scalar> evaluate_declaration(
        const semantic::DeclarationId declaration)
    {
        return value_for_declaration(declaration);
    }

private:
    struct DeclarationBoundary {
        semantic::UnitId unit;
        std::size_t position { };
    };

    [[nodiscard]] std::optional<DeclarationBoundary> declaration_boundary(
        const semantic::DeclarationId declaration) const
    {
        const auto view = specialization_.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto unit_id = scope_unit(
            specialization_, view->systemverilog->scope);
        if (!unit_id) {
            return std::nullopt;
        }
        const auto unit = specialization_.design().find_unit(*unit_id);
        if (!unit || unit->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto found = std::ranges::find(
            unit->systemverilog->declarations, declaration);
        if (found == unit->systemverilog->declarations.end()) {
            return std::nullopt;
        }
        return DeclarationBoundary { *unit_id,
            static_cast<std::size_t>(std::distance(
                unit->systemverilog->declarations.begin(), found)) };
    }

    [[nodiscard]] bool source_order_allows(
        const semantic::DeclarationId declaration) const
    {
        if (boundaries_.empty()) {
            return true;
        }
        const auto candidate = declaration_boundary(declaration);
        if (!candidate) {
            return true;
        }
        const auto& boundary = boundaries_.back();
        return candidate->unit != boundary.unit
            || candidate->position < boundary.position;
    }

    [[nodiscard]] std::optional<Scalar> actual_value(
        const semantic::DeclarationId declaration) const
    {
        const auto& actuals
            = specialization_.specialization().actual_identities;
        const auto actual = std::ranges::find(actuals, declaration,
            &semantic::SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end()) {
            return std::nullopt;
        }
        if (const auto scalar = parse_scalar_identity(actual->identity)) {
            return scalar;
        }
        if (const auto integral = parse_canonical(actual->identity)) {
            if (const auto value = integral->integer_value()) {
                return scalar_integer(*value);
            }
        }
        const auto value = parse_integer<std::int64_t>(actual->identity);
        return value ? std::optional { scalar_integer(*value) }
                     : std::nullopt;
    }

    [[nodiscard]] std::optional<Scalar> value_for_declaration(
        const semantic::DeclarationId declaration)
    {
        if (const auto actual = actual_value(declaration)) {
            return actual;
        }
        if (!source_order_allows(declaration)) {
            error_ = "scalar parameter dependency is declared later";
            return std::nullopt;
        }
        if (!active_declarations_.insert(declaration.value()).second) {
            error_ = "recursive SystemVerilog scalar constant declaration";
            return std::nullopt;
        }
        const auto view = specialization_.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->initializer) {
            active_declarations_.erase(declaration.value());
            return std::nullopt;
        }
        const auto boundary = declaration_boundary(declaration);
        if (boundary) {
            boundaries_.push_back(*boundary);
        }
        auto result = evaluate(*view->systemverilog->initializer);
        if (boundary) {
            boundaries_.pop_back();
        }
        const auto target = view->systemverilog->type
            ? scalar_kind(*view->systemverilog->type)
            : ScalarKind::None;
        if (result && target != ScalarKind::None) {
            result = frontend::convert_systemverilog_scalar_constant(
                *result, target, error_);
        }
        active_declarations_.erase(declaration.value());
        return result;
    }

    [[nodiscard]] std::optional<Scalar> evaluate_literal(
        const semantic::sv::Expression& record)
    {
        if (record.decimal_literal) {
            auto value = exact_decimal(*record.decimal_literal, error_);
            if (!value) {
                return std::nullopt;
            }
            auto kind = ScalarKind::Real;
            if (record.decimal_literal->kind
                == semantic::sv::DecimalLiteralKind::time) {
                const auto factor = physical_unit_femtoseconds(
                    record.decimal_literal->time_unit);
                const auto owner = scope_unit(specialization_, record.scope);
                const auto unit = owner
                    ? specialization_.design().find_unit(*owner)
                    : std::optional<semantic::CompiledUnitView> { };
                const auto scale = unit && unit->systemverilog != nullptr
                    ? scalar_time_scale(
                          unit->systemverilog->compilation.time_unit)
                    : 1U;
                if (!factor || scale == 0U) {
                    error_ = "time literal has an invalid unit or evaluation "
                             "context";
                    return std::nullopt;
                }
                *value *= static_cast<double>(*factor)
                    / static_cast<double>(scale);
                kind = ScalarKind::Realtime;
            }
            return scalar_real(*value, kind);
        }
        std::string integral_error;
        const auto integral = evaluate_hir_systemverilog_constant(
            specialization_, record.id, integral_error);
        if (!integral) {
            error_ = integral_error.empty()
                ? "integer literal is outside the signed 64-bit scalar range"
                : std::move(integral_error);
            return std::nullopt;
        }
        const auto value = integral->integer_value();
        if (!value) {
            error_ = "integer literal is outside the signed 64-bit scalar "
                     "range";
            return std::nullopt;
        }
        return scalar_integer(*value);
    }

    [[nodiscard]] std::optional<Scalar> evaluate_name(
        const semantic::sv::Expression& record)
    {
        const auto declaration = systemverilog_constant_name_declaration(
            specialization_, record);
        if (!declaration) {
            error_ = "unknown scalar constant '" + record.text + "'";
            return std::nullopt;
        }
        return value_for_declaration(*declaration);
    }

    [[nodiscard]] std::optional<Scalar> evaluate_unary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 1U) {
            return std::nullopt;
        }
        auto operand = evaluate(record.operands.front());
        if (!operand) {
            return std::nullopt;
        }
        if (record.text == "+") {
            return operand;
        }
        if (record.text == "!") {
            return scalar_integer(!operand->truth());
        }
        if (record.text == "-") {
            if (const auto value = operand->real()) {
                return scalar_real(-*value, operand->kind);
            }
            if (const auto value = operand->integral(); value
                && *value != std::numeric_limits<std::int64_t>::min()) {
                return scalar_integer(-*value, operand->kind);
            }
            error_ = "unary scalar negation overflows";
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Scalar> evaluate_binary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 2U) {
            return std::nullopt;
        }
        auto left = evaluate(record.operands.front());
        if (!left) {
            return std::nullopt;
        }
        if (record.text == "&&" && !left->truth()) {
            return scalar_integer(0);
        }
        if (record.text == "||" && left->truth()) {
            return scalar_integer(1);
        }
        auto right = evaluate(record.operands.back());
        if (!right) {
            return std::nullopt;
        }
        if (left->kind == ScalarKind::Chandle
            || right->kind == ScalarKind::Chandle) {
            const auto equality = record.text == "=="
                || record.text == "===";
            const auto inequality = record.text == "!="
                || record.text == "!==";
            if (left->kind != ScalarKind::Chandle
                || right->kind != ScalarKind::Chandle
                || (!equality && !inequality)) {
                error_ = "chandle constants only support equality and "
                         "inequality with chandle or null";
                return std::nullopt;
            }
            const auto equal = left->bits == right->bits;
            return scalar_integer(equality ? equal : !equal);
        }
        if (record.text == "&&" || record.text == "||") {
            return scalar_integer(right->truth());
        }
        const auto left_integer = left->integral();
        const auto right_integer = right->integral();
        const auto lhs = scalar_number(*left);
        const auto rhs = scalar_number(*right);
        if (!lhs || !rhs) {
            error_ = "scalar constant operand is not numeric";
            return std::nullopt;
        }
        const auto comparison = [&](const auto& first, const auto& second)
            -> std::optional<Scalar> {
            if (record.text == "==" || record.text == "===") {
                return scalar_integer(first == second);
            }
            if (record.text == "!=" || record.text == "!==") {
                return scalar_integer(first != second);
            }
            if (record.text == "<") {
                return scalar_integer(first < second);
            }
            if (record.text == "<=") {
                return scalar_integer(first <= second);
            }
            if (record.text == ">") {
                return scalar_integer(first > second);
            }
            if (record.text == ">=") {
                return scalar_integer(first >= second);
            }
            return std::nullopt;
        };
        if (left_integer && right_integer) {
            if (const auto result = comparison(
                    *left_integer, *right_integer)) {
                return result;
            }
        } else if (const auto result = comparison(*lhs, *rhs)) {
            return result;
        }
        const auto kind = promote_scalar_kind(left->kind, right->kind);
        if (left_integer && right_integer && !real_scalar_kind(kind)) {
            if ((record.text == "/" || record.text == "%")
                && *right_integer == 0) {
                error_ = "scalar constant division by zero";
                return std::nullopt;
            }
            if (record.text == "/") {
                if (*left_integer == std::numeric_limits<std::int64_t>::min()
                    && *right_integer == -1) {
                    error_ = "integral scalar constant operation overflows";
                    return std::nullopt;
                }
                return scalar_integer(
                    *left_integer / *right_integer, kind);
            }
            if (record.text == "%") {
                return scalar_integer(
                    *left_integer
                            == std::numeric_limits<std::int64_t>::min()
                        && *right_integer == -1
                    ? 0
                    : *left_integer % *right_integer,
                    kind);
            }
            if (const auto result = checked_scalar_arithmetic(
                    *left_integer, *right_integer, record.text)) {
                return scalar_integer(*result, kind);
            }
            error_ = "integral scalar constant operation overflows";
            return std::nullopt;
        }
        double result { };
        if (record.text == "+") {
            result = *lhs + *rhs;
        } else if (record.text == "-") {
            result = *lhs - *rhs;
        } else if (record.text == "*") {
            result = *lhs * *rhs;
        } else if (record.text == "/" && *rhs != 0.0) {
            result = *lhs / *rhs;
        } else {
            error_ = *rhs == 0.0 && record.text == "/"
                ? "scalar constant division by zero"
                : "unsupported scalar constant operator '" + record.text
                    + "'";
            return std::nullopt;
        }
        if (!std::isfinite(result)) {
            error_ = "scalar constant operation produced a nonfinite result";
            return std::nullopt;
        }
        if (real_scalar_kind(kind)) {
            return scalar_real(result, kind);
        }
        if (result
                > static_cast<double>(
                    std::numeric_limits<std::int64_t>::max())
            || result
                < static_cast<double>(
                    std::numeric_limits<std::int64_t>::min())
            || std::trunc(result) != result) {
            error_ = "integral scalar constant operation overflows";
            return std::nullopt;
        }
        return scalar_integer(static_cast<std::int64_t>(result), kind);
    }

    [[nodiscard]] std::optional<Scalar> evaluate_call(
        const semantic::sv::Expression& record)
    {
        if (record.text == "?:" && record.operands.size() == 3U) {
            const auto condition = evaluate(record.operands.front());
            if (!condition) {
                return std::nullopt;
            }
            auto selected = evaluate(
                record.operands[condition->truth() ? 1U : 2U]);
            if (!selected) {
                return std::nullopt;
            }
            const auto result_kind = scalar_kind(record.scalar_kind);
            return result_kind == ScalarKind::None
                ? selected
                : frontend::convert_systemverilog_scalar_constant(
                      *selected, result_kind, error_);
        }
        constexpr auto cast_prefix = std::string_view { "@sv-cast:" };
        if (record.operands.size() == 1U
            && record.text.starts_with(cast_prefix)) {
            const auto operand = evaluate(record.operands.front());
            if (!operand) {
                return std::nullopt;
            }
            const auto name = std::string_view { record.text }.substr(
                cast_prefix.size());
            const auto target = name == "shortreal"
                ? ScalarKind::ShortReal
                : name == "real"
                ? ScalarKind::Real
                : name == "realtime"
                ? ScalarKind::Realtime
                : name == "time"
                ? ScalarKind::Time
                : name == "chandle"
                ? ScalarKind::Chandle
                : ScalarKind::None;
            if (target != ScalarKind::None) {
                return frontend::convert_systemverilog_scalar_constant(
                    *operand, target, error_);
            }
        }
        error_ = "expression is not a supported scalar constant form";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Scalar> evaluate_impl(
        const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::integer_literal
            || record.kind == ExpressionKind::logic_literal) {
            return evaluate_literal(record);
        }
        if (record.kind == ExpressionKind::boolean_literal) {
            return scalar_integer(
                record.text == "true" || record.text == "1" ? 1 : 0);
        }
        if (record.kind == ExpressionKind::class_null
            || record.text == "@sv-null") {
            return Scalar { ScalarKind::Chandle, 0U };
        }
        if (record.kind == ExpressionKind::name) {
            return evaluate_name(record);
        }
        if (record.kind == ExpressionKind::unary) {
            return evaluate_unary(record);
        }
        if (record.kind == ExpressionKind::binary) {
            return evaluate_binary(record);
        }
        if (record.kind == ExpressionKind::call) {
            return evaluate_call(record);
        }
        error_ = "expression is not a supported scalar constant form";
        return std::nullopt;
    }

    const semantic::SpecializedHirUnit& specialization_;
    std::string& error_;
    std::unordered_set<std::uint32_t> active_expressions_;
    std::unordered_set<std::uint32_t> active_declarations_;
    std::vector<DeclarationBoundary> boundaries_;
};

[[nodiscard]] bool scalar_expression_applicable(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    std::unordered_set<std::uint32_t>& expressions,
    std::unordered_set<std::uint32_t>& declarations)
{
    if (!expressions.insert(expression.value()).second) {
        return false;
    }
    const auto view = specialization.find_expression(expression);
    if (!view || view->systemverilog == nullptr) {
        return false;
    }
    const auto& record = *view->systemverilog;
    if (record.decimal_literal
        || scalar_kind(record.scalar_kind) != ScalarKind::None
        || record.kind == ExpressionKind::class_null
        || record.text == "@sv-null") {
        return true;
    }
    if (record.kind == ExpressionKind::call
        && (record.text == "@sv-cast:shortreal"
            || record.text == "@sv-cast:real"
            || record.text == "@sv-cast:realtime"
            || record.text == "@sv-cast:time"
            || record.text == "@sv-cast:chandle")) {
        return true;
    }
    if (record.kind == ExpressionKind::name) {
        const auto declaration = systemverilog_constant_name_declaration(
            specialization, record);
        if (declaration && declarations.insert(declaration->value()).second) {
            const auto value = specialization.find_declaration(*declaration);
            if (value && value->systemverilog != nullptr) {
                const auto& candidate = *value->systemverilog;
                if ((candidate.type
                        && scalar_kind(*candidate.type) != ScalarKind::None)
                    || (candidate.initializer
                        && scalar_expression_applicable(specialization,
                            *candidate.initializer, expressions,
                            declarations))) {
                    return true;
                }
            }
        }
    }
    return std::ranges::any_of(record.operands,
        [&](const auto operand) {
            return scalar_expression_applicable(
                specialization, operand, expressions, declarations);
        });
}

} // namespace

bool HirSystemVerilogConstant::known() const noexcept
{
    return !unbounded && ::fsim::elaboration::known(packed);
}

std::optional<std::int64_t>
HirSystemVerilogConstant::integer_value() const noexcept
{
    if (!known()) {
        return std::nullopt;
    }
    const auto value = numeric_integer(*this);
    if (value < std::numeric_limits<std::int64_t>::min()
        || value > std::numeric_limits<std::int64_t>::max()) {
        return std::nullopt;
    }
    return value.convert_to<std::int64_t>();
}

std::string HirSystemVerilogConstant::display() const
{
    if (unbounded) {
        return "$";
    }
    if (known()) {
        if (const auto integer = integer_value()) {
            return std::to_string(*integer);
        }
        if (!signed_value && width <= 64U) {
            if (const auto value = packed.known_unsigned_value()) {
                return std::to_string(*value);
            }
        }
    }
    auto bits = packed.to_msb_string();
    std::ranges::transform(bits, bits.begin(), ascii_lower);
    return std::to_string(width) + (signed_value ? "'sb" : "'b")
        + bits;
}

std::string HirSystemVerilogConstant::canonical() const
{
    auto bits = packed.to_msb_string();
    std::ranges::transform(bits, bits.begin(), ascii_lower);
    return "svconst-v3:b=" + std::to_string(unbounded ? 1U : 0U)
        + ":w=" + std::to_string(width)
        + ":s=" + std::to_string(signed_value ? 1U : 0U)
        + ":u=" + std::to_string(unsized ? 1U : 0U)
        + ":d=" + std::to_string(static_cast<unsigned>(domain))
        + ":n=" + std::to_string(nominal_type.size()) + ':'
        + nominal_type + ":v=" + bits;
}

bool hir_systemverilog_explicit_integral_type(
    const semantic::sv::TypeReference& type) noexcept
{
    return type.target.spelling != "implicit"
        && type.value_form
        && *type.value_form != semantic::sv::TypeForm::unresolved;
}

std::optional<HirSystemVerilogConstant>
decode_hir_systemverilog_constant(const std::string_view identity)
{
    return parse_canonical(identity);
}

std::optional<frontend::SystemVerilogScalarConstant>
decode_hir_systemverilog_scalar_constant(const std::string_view identity)
{
    return parse_scalar_identity(identity);
}

std::optional<HirSystemVerilogConstant>
evaluate_hir_systemverilog_constant(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    std::string& error)
{
    error.clear();
    return HirConstantEvaluator { specialization, error }.evaluate(
        expression);
}

bool hir_systemverilog_scalar_expression_applicable(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression)
{
    std::unordered_set<std::uint32_t> expressions;
    std::unordered_set<std::uint32_t> declarations;
    return scalar_expression_applicable(
        specialization, expression, expressions, declarations);
}

std::optional<frontend::SystemVerilogScalarConstant>
evaluate_hir_systemverilog_scalar_constant(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    std::string& error)
{
    error.clear();
    return HirScalarEvaluator { specialization, error }.evaluate(expression);
}

std::optional<frontend::SystemVerilogScalarConstant>
evaluate_hir_systemverilog_scalar_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::DeclarationId declaration,
    std::string& error)
{
    error.clear();
    return HirScalarEvaluator { specialization, error }
        .evaluate_declaration(declaration);
}

std::optional<HirSystemVerilogConstant>
convert_hir_systemverilog_constant(
    HirSystemVerilogConstant value,
    const semantic::sv::TypeReference& type,
    std::string& error,
    const semantic::SpecializedHirUnit* const specialization)
{
    if (value.unbounded) {
        error = "symbolic unbounded '$' requires an implicit parameter type";
        return std::nullopt;
    }
    std::unordered_set<std::uint32_t> visiting;
    const auto width = resolved_type_width(
        type, specialization, visiting);
    if (!width || *width == 0U || *width > maximum_constant_width) {
        error = "resolved SystemVerilog integral type has no supported width";
        return std::nullopt;
    }
    value = resized(
        std::move(value),
        static_cast<std::uint32_t>(*width));
    value.signed_value = type.signed_value;
    value.unsized = false;
    value.domain = type_domain(type);
    value.nominal_type = type.target.target.valid()
        ? type.target.spelling
        : std::string { };
    value.packed_range = type.packed_range;
    if (!type.four_state) {
        convert_to_two_state(value);
    }
    return value;
}

} // namespace fsim::elaboration
