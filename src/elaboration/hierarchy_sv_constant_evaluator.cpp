// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "fsim/semantic/systemverilog_constant_array.hpp"
#include "fsim/support/index_distance.hpp"
#include "specialization_cache.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <algorithm>
#include <unordered_map>
#include <bit>
#include <functional>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
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

/// FSIM_SV_CONST_TRACE and FSIM_SV_CONST_CAST_TRACE, read once.
[[nodiscard]] bool sv_const_trace()
{
    static const bool enabled = std::getenv("FSIM_SV_CONST_TRACE") != nullptr;
    return enabled;
}

[[nodiscard]] bool sv_const_cast_trace()
{
    static const bool enabled
        = std::getenv("FSIM_SV_CONST_CAST_TRACE") != nullptr;
    return enabled;
}

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

/// Whether `scope` lies in a class or package body: one lowered for a
/// caller's specialization (a method called through a handle, a package
/// subprogram) keeps its lexical constants (IEEE 1800-2017 8.10, 26.3).
[[nodiscard]] bool class_or_package_body(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ScopeId scope)
{
    const auto& design = specialization.design();
    std::optional<semantic::ScopeId> current { scope };
    for (std::size_t depth { }; current && depth < 4096U; ++depth) {
        const auto* record = design.find_scope(*current);
        if (record == nullptr) {
            return false;
        }
        if (std::ranges::any_of(design.systemverilog_hir.classes(),
                [&](const semantic::sv::ClassDeclaration& candidate) {
                    return candidate.scope == record->id;
                })) {
            return true;
        }
        const auto unit = design.find_unit(record->unit);
        if (unit && unit->systemverilog != nullptr
            && unit->systemverilog->kind
                == semantic::sv::UnitKind::package) {
            return true;
        }
        current = record->parent;
    }
    return false;
}

[[nodiscard]] std::optional<semantic::DeclarationId>
resolve_constant_name_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Expression& expression)
{
    const auto selected = expression.referenced_name
        ? expression.referenced_name->selected
        : std::optional<semantic::DeclarationId> { };
    if (selected
        && systemverilog_constant_declaration(
            specialization, *selected)) {
        // A bind instance's actuals resolve as if the instantiation were in
        // its target (IEEE 1800-2017 23.11): a constant linked in another
        // unit yields to a same-named constant of the target.
        const auto declaration = specialization.find_declaration(*selected);
        const auto* scope = declaration && declaration->systemverilog != nullptr
            ? specialization.design().find_scope(
                  declaration->systemverilog->scope)
            : nullptr;
        if (scope != nullptr && scope->unit != specialization.unit()) {
            if (const auto rebound
                = semantic::CompiledDesignResolver { specialization }
                      .resolve_systemverilog_constant(
                          expression.text, specialization.scope(), false,
                          expression.referenced_name
                              ? expression.referenced_name->source_order
                              : 0U)
                      .unique();
                rebound && *rebound != *selected) {
                const auto target = specialization.find_declaration(
                    *rebound);
                const auto* target_scope = target
                        && target->systemverilog != nullptr
                    ? specialization.design().find_scope(
                          target->systemverilog->scope)
                    : nullptr;
                if (target_scope != nullptr
                    && target_scope->unit == specialization.unit()
                    && !class_or_package_body(
                        specialization, expression.scope)) {
                    return rebound;
                }
            }
        }
        return selected;
    }

    // A bind actual is parsed in the directive's lexical scope before its
    // target occurrence is known. A target parameter name can consequently
    // carry an implicit-net placeholder in the immutable HIR. Re-resolve only
    // non-constant selections in the effective target specialization; real
    // lexical and package constants retain their linked declaration. A class
    // or package body resolves from its own scope first, then (for names a
    // compilation-unit import provides) from the specialization's.
    const auto source_order = expression.referenced_name
        ? expression.referenced_name->source_order
        : 0U;
    std::vector<semantic::ScopeId> lookup_scopes;
    if (class_or_package_body(specialization, expression.scope)) {
        lookup_scopes.push_back(expression.scope);
    }
    lookup_scopes.push_back(specialization.scope());
    for (const auto lookup_scope : lookup_scopes) {
        if (const auto rebound
            = semantic::CompiledDesignResolver { specialization }
                  .resolve_systemverilog_constant(
                      expression.text, lookup_scope, false, source_order)
                  .unique()) {
            return rebound;
        }
    }
    // `P.m` selects a member of the structure constant P (IEEE 1800-2017
    // 7.2.1); evaluate_name slices the member.
    if (const auto dot = expression.text.find('.');
        !selected && dot != std::string::npos && dot != 0U) {
        for (const auto lookup_scope : lookup_scopes) {
            if (const auto prefix
                = semantic::CompiledDesignResolver { specialization }
                      .resolve_systemverilog_constant(
                          std::string_view { expression.text }.substr(0U, dot),
                          lookup_scope, false, source_order)
                      .unique()) {
                return prefix;
            }
        }
    }
    return selected;
}

/// The declaration a constant name selects. The result depends only on the
/// specialization and the expression, so it is kept per specialization.
[[nodiscard]] std::optional<semantic::DeclarationId>
systemverilog_constant_name_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Expression& expression)
{
    auto& known = specialization_cache(specialization).constants->name_declarations;
    if (const auto found = known.find(&expression); found != known.end()) {
        return found->second;
    }
    const auto result
        = resolve_constant_name_declaration(specialization, expression);
    known.emplace(&expression, result);
    return result;
}

[[nodiscard]] std::optional<Scalar> parse_scalar_identity(
    std::string_view identity);

constexpr std::uint32_t maximum_constant_width
    = hir_systemverilog_maximum_constant_width;
constexpr std::uint64_t maximum_constant_work_units = 64U * 1024U * 1024U;
// Each constant call frame uses several native frames; deeper recursion
// falls back to run-time evaluation instead of exhausting the stack.
constexpr std::size_t maximum_constant_call_depth = 256U;
constexpr std::size_t maximum_constant_local_array_elements = 65536U;
constexpr std::uint64_t maximum_constant_local_array_bits
    = 8U * 1024U * 1024U;

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
    if (!value.is_logic9()) {
        // Logic4 planes: an X or Z bit has its bval bit set.
        const auto bval = value.bval_words();
        const auto width = value.width();
        for (std::size_t word = 0U; word < bval.size(); ++word) {
            const auto bits = width - 64U * word;
            const auto mask = bits >= 64U ? ~std::uint64_t { 0 }
                                          : (std::uint64_t { 1 } << bits) - 1U;
            if ((bval[word] & mask) != 0U) {
                return false;
            }
        }
        return true;
    }
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
    if (width <= 64U) {
        const auto mask = width == 64U ? ~std::uint64_t { 0 }
                                       : (std::uint64_t { 1 } << width) - 1U;
        return make_value(PackedLogic4::from_aval_bval(width, input & mask, 0U),
            signed_value, unsized, domain);
    }
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
    if (!value.packed.is_logic9() && value.width == value.packed.width()) {
        // The '1' bits: aval set, bval clear.
        const auto aval = value.packed.aval_words();
        const auto bval = value.packed.bval_words();
        for (auto word = aval.size(); word-- > 0U;) {
            const auto bits = value.width - 64U * word;
            const auto mask = bits >= 64U ? ~std::uint64_t { 0 }
                                          : (std::uint64_t { 1 } << bits) - 1U;
            const auto b = word < bval.size() ? bval[word] : 0U;
            result <<= 64U;
            result += aval[word] & ~b & mask;
        }
        return result;
    }
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
    if (width <= 64U) {
        return PackedLogic4::from_aval_bval(
            width, value.convert_to<std::uint64_t>(), 0U);
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
    value.scalar.reset();
    if (value.width == width) {
        return value;
    }
    const auto logic9 = value.packed.is_logic9();
    auto packed = PackedLogic4 { width, Logic4::zero };
    if (logic9) {
        packed = packed.promoted_to_logic9();
    }
    // An unsized fill literal (`'0`, `'1`, `'x`, `'z`) sets every bit of
    // its target (IEEE 1800-2017 5.7.1).
    if (value.unsized && value.width == 1U && width > 1U) {
        if (logic9) {
            packed.fill(value.packed.get_logic9(0U));
        } else {
            packed.fill(value.packed.get(0U));
        }
        value.packed = std::move(packed);
        value.width = width;
        return value;
    }
    if (width > value.width && value.width != 0U) {
        const auto sign = value.packed.get_logic9(value.width - 1U);
        const auto sign_logic4 = runtime::to_logic4(sign);
        const bool unsized_unknown_extension
            = value.unsized
            && (sign_logic4 == Logic4::x || sign_logic4 == Logic4::z);
        if (unsized_unknown_extension || value.signed_value) {
            if (logic9) {
                packed.fill(sign);
            } else {
                packed.fill(sign_logic4);
            }
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
    const auto& spelling = type.target.spelling;
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
    std::unordered_set<std::uint32_t>& visiting,
    const std::function<std::optional<std::int64_t>(semantic::ExpressionId)>*
        const bound = nullptr)
{
    // Range bounds evaluate where the type is written: a class body's
    // ranges read the class frame's parameters (IEEE 1800-2023 8.25).
    const auto bound_value = [&](const semantic::ExpressionId expression) {
        return bound != nullptr
            ? (*bound)(expression)
            : specialization->evaluate_integral_expression(expression);
    };
    // A fixed-size unpacked array has the bits of all its elements; other
    // unpacked containers have no fixed width (IEEE 1800-2017 20.6.2).
    if (!type.unpacked_dimensions.empty()) {
        if (type.container_form
            && *type.container_form != semantic::sv::TypeForm::static_array) {
            return std::nullopt;
        }
        // A named type whose definition carries these dimensions would
        // resolve back to them; its element is reached through the
        // definition instead.
        if (type.target.target.valid()
            && !visiting.insert(type.target.target.value()).second) {
            return std::nullopt;
        }
        struct Release {
            std::unordered_set<std::uint32_t>& visiting;
            semantic::TypeId target;
            ~Release()
            {
                if (target.valid()) {
                    visiting.erase(target.value());
                }
            }
        } release { visiting, type.target.target };
        auto element = type;
        element.unpacked_dimensions.clear();
        element.container_form.reset();
        auto width = resolved_type_width(
            element, specialization, visiting, bound);
        for (const auto& dimension : type.unpacked_dimensions) {
            auto left = dimension.left;
            auto right = dimension.right;
            if (specialization != nullptr) {
                if (!left && dimension.left_expression) {
                    left = bound_value(
                        *dimension.left_expression);
                }
                if (!right && dimension.right_expression) {
                    right = bound_value(
                        *dimension.right_expression);
                }
            }
            if (!width || !left || !right) {
                return std::nullopt;
            }
            const auto count = support::index_distance(*left, *right) + 1U;
            if (count == 0U
                || *width > std::numeric_limits<std::uint64_t>::max() / count) {
                return std::nullopt;
            }
            *width *= count;
        }
        return width;
    }
    if (type.packed_range) {
        auto left = type.packed_range->left;
        auto right = type.packed_range->right;
        if (specialization != nullptr) {
            if (!left && type.packed_range->left_expression) {
                left = bound_value(
                    *type.packed_range->left_expression);
            }
            if (!right && type.packed_range->right_expression) {
                right = bound_value(
                    *type.packed_range->right_expression);
            }
        }
        if (!left || !right) {
            return std::nullopt;
        }
        const auto distance = support::index_distance(*left, *right);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        const auto extent = distance + 1U;
        // A packed array of a named packed type (`w_t [1:0]`) has elements
        // of that type's width (IEEE 1800-2017 7.4.1).
        const auto& spelling = type.target.spelling;
        static constexpr std::array<std::string_view, 17> builtin {
            "logic", "bit", "reg", "int", "integer", "byte", "shortint",
            "longint", "time", "wire", "signed", "unsigned", "struct",
            "union", "enum", "implicit", "string"
        };
        // A name not bound to its typedef here (a class property's
        // `s_t [1:0]`) resolves through the effective type below.
        const bool bound_target = type.target.target.valid();
        if (specialization == nullptr || spelling.empty()
            || type.packed_dimensions.size() > 1U
            || std::ranges::find(builtin, std::string_view { spelling })
                != builtin.end()
            || spelling.starts_with('@')
            || spelling.find(' ') != std::string::npos
            || (bound_target
                && !visiting.insert(type.target.target.value()).second)) {
            return extent;
        }
        auto element = type;
        element.packed_range.reset();
        element.packed_dimensions.clear();
        element.signed_value = false;
        const auto element_width
            = resolved_type_width(
                element, specialization, visiting, bound);
        if (bound_target) {
            visiting.erase(type.target.target.value());
        }
        const auto effective
            = semantic::CompiledDesignResolver { *specialization }
                  .effective_systemverilog_type(
                      element, specialization->scope());
        // A reference that repeats its typedef's own range denotes the
        // typedef itself.
        const auto boundary = [&](const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression) {
            return value ? value
                : expression
                ? bound_value(*expression)
                : std::nullopt;
        };
        if (effective && effective->packed_range
            && boundary(effective->packed_range->left,
                   effective->packed_range->left_expression)
                == left
            && boundary(effective->packed_range->right,
                   effective->packed_range->right_expression)
                == right) {
            return element_width.value_or(extent);
        }
        if (!element_width || *element_width <= 1U
            || *element_width
                > std::numeric_limits<std::uint64_t>::max() / extent) {
            return extent;
        }
        return extent * *element_width;
    }
    if (specialization != nullptr) {
        const auto effective
            = semantic::CompiledDesignResolver { *specialization }
                  .effective_systemverilog_type(
                      type, specialization->scope());
        if (effective && *effective != type) {
            return resolved_type_width(
                *effective, specialization, visiting, bound);
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
                record.base, specialization, visiting, bound);
            if (!record.members.empty()) {
                std::uint64_t aggregate { };
                auto valid = true;
                for (const auto& member : record.members) {
                    const auto width = resolved_type_width(
                        member.type, specialization, visiting, bound);
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
    // A leftmost x or z digit extends through the remaining bits
    // (IEEE 1800-2017 5.7.1).
    if (!digits.empty()) {
        const auto leading = ascii_lower(digits.front());
        if (leading == 'x' || leading == 'z' || leading == '?') {
            for (; output_bit < width; ++output_bit) {
                packed.set(output_bit,
                    leading == 'x' ? Logic4::x : Logic4::z);
            }
        }
    }
    return make_value(
        std::move(packed), signed_value, !explicit_width);
}

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept
{
    return width >= 64U ? ~std::uint64_t { 0 }
                        : (std::uint64_t { 1 } << width) - 1U;
}

enum class Truth : std::uint8_t { false_value,
    true_value,
    unknown };

[[nodiscard]] Truth truth(const Value& value) noexcept
{
    // A real value is true when nonzero, a chandle when not null (12.4).
    if (value.scalar) {
        if (const auto real = value.scalar->real()) {
            return *real != 0.0 ? Truth::true_value : Truth::false_value;
        }
        return value.scalar->bits != 0U ? Truth::true_value
                                         : Truth::false_value;
    }
    // Planes word by word: a one bit decides truth, else any X/Z bit
    // (bval set) leaves it unknown (11.4.7).
    if (!value.packed.is_logic9() && value.packed.width() == value.width) {
        const auto aval = value.packed.aval_words();
        const auto bval = value.packed.bval_words();
        bool unknown_bits { };
        const auto words = std::min<std::size_t>(
            aval.size(), (std::size_t { value.width } + 63U) / 64U);
        for (std::size_t word = 0U; word < words; ++word) {
            const auto low = word * 64U;
            const auto mask = low + 64U <= value.width
                ? ~std::uint64_t { 0 }
                : width_mask(static_cast<std::uint32_t>(value.width - low));
            const auto b = word < bval.size() ? bval[word] & mask : 0U;
            if ((aval[word] & ~b & mask) != 0U) {
                return Truth::true_value;
            }
            unknown_bits = unknown_bits || b != 0U;
        }
        return unknown_bits ? Truth::unknown : Truth::false_value;
    }
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

/// A Logic4 value of at most 64 bits as one word per plane (aval, bval).
struct SmallWords {
    std::uint64_t a { };
    std::uint64_t b { };
};

[[nodiscard]] std::optional<SmallWords> small_words(const Value& value)
{
    if (value.width == 0U || value.width > 64U || value.packed.is_logic9()
        || value.packed.width() != value.width) {
        return std::nullopt;
    }
    const auto aval = value.packed.aval_words();
    const auto bval = value.packed.bval_words();
    const auto mask = width_mask(value.width);
    return SmallWords { (aval.empty() ? 0U : aval[0]) & mask,
        (bval.empty() ? 0U : bval[0]) & mask };
}

/// resized()'s planes: extended by the value's own signedness, or truncated.
[[nodiscard]] SmallWords small_resized(SmallWords words,
    const std::uint32_t from, const std::uint32_t to, const bool signed_value)
{
    if (to > from && signed_value) {
        const auto fill = width_mask(to) & ~width_mask(from);
        if (((words.a >> (from - 1U)) & 1U) != 0U) {
            words.a |= fill;
        }
        if (((words.b >> (from - 1U)) & 1U) != 0U) {
            words.b |= fill;
        }
    }
    return { words.a & width_mask(to), words.b & width_mask(to) };
}

[[nodiscard]] std::int64_t small_signed(const std::uint64_t value,
    const std::uint32_t width) noexcept
{
    if (width >= 64U) {
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t { 1 } << (width - 1U);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

[[nodiscard]] Value common_operand(
    Value value,
    const std::uint32_t width,
    const bool signed_value)
{
    // An unsized fill literal (`'0`, `'1`, `'x`, `'z`) sets every bit of
    // the context width (5.7.1).
    if (value.unsized && value.width == 1U && width > 1U) {
        value.packed = runtime::PackedLogic4(
            width, value.packed.get(0));
        value.width = width;
        value.signed_value = signed_value;
        return value;
    }
    // Operands take the context's signedness before they are extended
    // (11.8.1).
    value.signed_value = signed_value;
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

/// convert_hir_systemverilog_constant with the type's resolved width.
[[nodiscard]] std::optional<HirSystemVerilogConstant> convert_constant_to_width(
    HirSystemVerilogConstant value,
    const semantic::sv::TypeReference& type,
    std::string& error,
    const std::optional<std::uint64_t> width)
{
    // `$` may be the value of a parameter of an integer type (IEEE
    // 1800-2017 6.20.2); it stays unbounded at the type's width.
    const bool unbounded = value.unbounded;
    if (unbounded && type.value_form
        && *type.value_form != semantic::sv::TypeForm::packed_integral) {
        error = "symbolic unbounded '$' requires a parameter of a simple bit vector type";
        return std::nullopt;
    }
    if (!width || *width == 0U || *width > maximum_constant_width) {
        error = "resolved SystemVerilog integral type has no supported width";
        return std::nullopt;
    }
    value.scalar.reset();
    value = resized(
        std::move(value),
        static_cast<std::uint32_t>(*width));
    value.signed_value = type.signed_value;
    value.unsized = false;
    value.domain = type_domain(type);
    value.nominal_type = type.target.target.valid()
        ? type.target.spelling
        : std::string { };
    value.unbounded = unbounded;
    value.packed_range = type.packed_range;
    if (!type.four_state) {
        convert_to_two_state(value);
    }
    return value;
}

[[nodiscard]] ScalarKind scalar_kind(
    const semantic::sv::ScalarKind kind) noexcept
{
    return static_cast<ScalarKind>(kind);
}

[[nodiscard]] ScalarKind scalar_kind(
    const semantic::sv::TypeReference& type) noexcept
{
    return frontend::systemverilog_scalar_kind(type.target.spelling);
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

struct RealMathFunction {
    std::size_t arguments { };
    double (*evaluate)(double, double) { };
};

[[nodiscard]] std::optional<RealMathFunction> real_math_function(
    const std::string_view name)
{
    using Entry = std::pair<std::string_view, RealMathFunction>;
    static const std::array functions {
        Entry { "$ln", { 1U, [](double x, double) { return std::log(x); } } },
        Entry { "$log10", { 1U, [](double x, double) { return std::log10(x); } } },
        Entry { "$exp", { 1U, [](double x, double) { return std::exp(x); } } },
        Entry { "$sqrt", { 1U, [](double x, double) { return std::sqrt(x); } } },
        Entry { "$pow", { 2U, [](double x, double y) { return std::pow(x, y); } } },
        Entry { "$floor", { 1U, [](double x, double) { return std::floor(x); } } },
        Entry { "$ceil", { 1U, [](double x, double) { return std::ceil(x); } } },
        Entry { "$sin", { 1U, [](double x, double) { return std::sin(x); } } },
        Entry { "$cos", { 1U, [](double x, double) { return std::cos(x); } } },
        Entry { "$tan", { 1U, [](double x, double) { return std::tan(x); } } },
        Entry { "$asin", { 1U, [](double x, double) { return std::asin(x); } } },
        Entry { "$acos", { 1U, [](double x, double) { return std::acos(x); } } },
        Entry { "$atan", { 1U, [](double x, double) { return std::atan(x); } } },
        Entry { "$atan2", { 2U, [](double y, double x) { return std::atan2(y, x); } } },
        Entry { "$hypot", { 2U, [](double x, double y) { return std::hypot(x, y); } } },
        Entry { "$sinh", { 1U, [](double x, double) { return std::sinh(x); } } },
        Entry { "$cosh", { 1U, [](double x, double) { return std::cosh(x); } } },
        Entry { "$tanh", { 1U, [](double x, double) { return std::tanh(x); } } },
        Entry { "$asinh", { 1U, [](double x, double) { return std::asinh(x); } } },
        Entry { "$acosh", { 1U, [](double x, double) { return std::acosh(x); } } },
        Entry { "$atanh", { 1U, [](double x, double) { return std::atanh(x); } } },
    };
    const auto found = std::ranges::find(functions, name, &Entry::first);
    return found == functions.end() ? std::nullopt
                                    : std::optional { found->second };
}

[[nodiscard]] bool real_family(const ScalarKind kind) noexcept
{
    return kind == ScalarKind::Real || kind == ScalarKind::ShortReal
        || kind == ScalarKind::Realtime;
}

/// A real-family value. Its packed form is the integer it converts to,
/// rounded away from zero at one half (IEEE 1800-2017 6.12.2).
[[nodiscard]] Value real_value(double input, const ScalarKind kind)
{
    const auto scalar = scalar_real(input, kind);
    // A shortreal keeps single precision.
    const auto value = scalar.real().value_or(input);
    Value result;
    if (std::isfinite(value)) {
        const cpp_int integer { std::round(value) };
        const auto magnitude = boost::multiprecision::abs(integer);
        const auto bits = magnitude == 0
            ? 1U
            : static_cast<std::uint32_t>(
                  boost::multiprecision::msb(magnitude) + 1U);
        const auto width = std::max<std::uint32_t>(64U, bits + 1U);
        result = make_value(packed_integer(integer, width), true, false,
            frontend::ValueDomain::Bit2);
    } else {
        result = make_known(0U, 64U, true, false, frontend::ValueDomain::Bit2);
    }
    result.scalar = scalar;
    return result;
}

/// One element identity of an unpacked array parameter actual: a decimal
/// integer, a canonical packed constant, or a real-family scalar (6.20.2).
[[nodiscard]] std::optional<Value> array_element_value(
    const std::string_view text)
{
    std::int64_t integer { };
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), integer);
    if (parsed.ec == std::errc { } && parsed.ptr == text.data() + text.size()) {
        return make_known(static_cast<std::uint64_t>(integer), 64U, true,
            false, frontend::ValueDomain::Bit2);
    }
    if (auto value = parse_canonical(text)) {
        return value;
    }
    if (const auto scalar = parse_scalar_identity(text);
        scalar && real_family(scalar->kind)) {
        return real_value(scalar->real().value_or(0.0), scalar->kind);
    }
    return std::nullopt;
}

/// The real number a value converts to: x and z bits count as zero
/// (IEEE 1800-2017 6.12.2).
[[nodiscard]] std::optional<double> real_of(const Value& value)
{
    if (value.scalar) {
        if (const auto real = value.scalar->real()) {
            return real;
        }
        if (value.scalar->kind == ScalarKind::Chandle) {
            return std::nullopt;
        }
        if (const auto integral = value.scalar->integral()) {
            return static_cast<double>(*integral);
        }
        return std::nullopt;
    }
    if (value.unbounded || value.width == 0U) {
        return std::nullopt;
    }
    return numeric_integer(value).convert_to<double>();
}

/// Convert a value to a real-family or chandle kind; an integral kind keeps
/// the integral value (its scalar, if any, rounds as in 6.12.2).
[[nodiscard]] std::optional<Value> convert_value_kind(
    Value value, const ScalarKind kind, std::string& error)
{
    if (real_family(kind)) {
        const auto real = real_of(value);
        if (!real) {
            error = "constant value does not convert to a real type";
            return std::nullopt;
        }
        return real_value(*real, kind);
    }
    if (kind == ScalarKind::Chandle) {
        if (!value.scalar || value.scalar->kind != ScalarKind::Chandle) {
            error = "only null converts to a chandle constant";
            return std::nullopt;
        }
        return value;
    }
    value.scalar.reset();
    return value;
}

[[nodiscard]] Value chandle_value(const std::uint64_t bits)
{
    auto result = make_known(bits, 64U, false, false,
        frontend::ValueDomain::Bit2);
    result.scalar = Scalar { ScalarKind::Chandle, bits };
    return result;
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

    /// Hierarchy identities this evaluator has read (hierarchy_reads_).
    [[nodiscard]] std::size_t hierarchy_reads() const noexcept
    {
        return hierarchy_reads_;
    }

    [[nodiscard]] std::optional<Value> evaluate(
        const semantic::ExpressionId expression)
    {
        // The active expressions form a short stack; a linear scan is
        // cheaper than hashing.
        // A recursive function call re-evaluates an expression in a deeper
        // frame; only re-entry within one frame is a cycle.
        const auto active = std::pair { frames_.size(), expression.value() };
        if (std::ranges::find(active_expressions_, active)
            != active_expressions_.end()) {
            error_ = "recursive SystemVerilog constant expression";
            return std::nullopt;
        }
        active_expressions_.push_back(active);
        auto result = evaluate_impl(expression);
        active_expressions_.pop_back();
        return result;
    }

    /// The value of an unpacked array expression.
    struct ArrayResult {
        std::int64_t left { };
        std::int64_t right { };
        std::vector<Value> elements;
    };
    [[nodiscard]] std::optional<ArrayResult> evaluate_unpacked_array(
        const semantic::ExpressionId expression)
    {
        auto value = evaluate_array(expression);
        if (!value) {
            return std::nullopt;
        }
        return ArrayResult { value->left, value->right,
            std::move(value->elements) };
    }

    /// A constant declaration's value in its declared type.
    [[nodiscard]] std::optional<Value> evaluate_declaration(
        const semantic::DeclarationId declaration)
    {
        return value_for_declaration(declaration);
    }

    /// An expression evaluated for a declared type, so that a keyed
    /// assignment pattern takes the type's layout.
    [[nodiscard]] std::optional<Value> evaluate_for_type(
        const semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type)
    {
        auto result = evaluate_typed(expression, type);
        if (result && hir_systemverilog_explicit_integral_type(type)
            && !unpacked_aggregate(type)) {
            result = convert_record_type(std::move(*result), type);
        }
        return result;
    }

    /// The bits `path` (`.m`, `[n]`, ...) selects in the bit stream of a
    /// value of `type` (IEEE 1800-2017 6.24.3, 7.2.1, 7.4).
    [[nodiscard]] std::optional<HirSystemVerilogAggregateSlice>
    aggregate_slice(semantic::sv::TypeReference type, std::string_view path)
    {
        const auto whole = aggregate_width(type);
        if (!whole) {
            return std::nullopt;
        }
        std::uint64_t offset { };
        while (!path.empty()) {
            if (path.front() == '[') {
                const auto close = path.find(']');
                if (close == std::string_view::npos) {
                    return std::nullopt;
                }
                const auto digits = path.substr(1U, close - 1U);
                std::int64_t index { };
                const auto parsed = std::from_chars(digits.data(),
                    digits.data() + digits.size(), index);
                if (parsed.ec != std::errc {}
                    || parsed.ptr != digits.data() + digits.size()) {
                    return std::nullopt;
                }
                type = with_unpacked_dimensions(type);
                const auto bounds = unpacked_bounds(type);
                if (!bounds || bounds->empty()) {
                    return std::nullopt;
                }
                const auto [left, right] = bounds->front();
                if (index < std::min(left, right)
                    || index > std::max(left, right)) {
                    return std::nullopt;
                }
                const auto count = static_cast<std::uint64_t>(
                    std::max(left, right) - std::min(left, right)) + 1U;
                const auto position = static_cast<std::uint64_t>(
                    left <= right ? index - left : left - index);
                type.unpacked_dimensions.erase(
                    type.unpacked_dimensions.begin());
                if (type.unpacked_dimensions.empty()) {
                    type.container_form.reset();
                }
                const auto element = aggregate_width(type);
                if (!element) {
                    return std::nullopt;
                }
                offset += (count - 1U - position) * *element;
                path.remove_prefix(close + 1U);
                continue;
            }
            if (path.front() == '.') {
                path.remove_prefix(1U);
            }
            const auto end = path.find_first_of(".[");
            const auto name = path.substr(0U, end);
            bool union_form { };
            const auto* const members = aggregate_members(type, &union_form);
            if (members == nullptr) {
                return std::nullopt;
            }
            const auto member = std::ranges::find(
                *members, name, &semantic::sv::PackedMember::name);
            if (member == members->end()) {
                return std::nullopt;
            }
            const auto member_offset = aggregate_member_offset(*members,
                static_cast<std::size_t>(member - members->begin()),
                union_form);
            if (!member_offset) {
                return std::nullopt;
            }
            offset += *member_offset;
            type = member->type;
            path = end == std::string_view::npos ? std::string_view { }
                                                 : path.substr(end);
        }
        const auto width = aggregate_width(type);
        if (!width || *width == 0U || offset + *width > *whole) {
            return std::nullopt;
        }
        return HirSystemVerilogAggregateSlice { offset, *width, *whole,
            effective_aggregate_type(type).signed_value };
    }

    [[nodiscard]] bool unpacked_aggregate_type(
        const semantic::sv::TypeReference& type)
    {
        return unpacked_aggregate(type);
    }

private:
    /// convert_hir_systemverilog_constant for a type held by a declaration
    /// or callable record, whose width is kept per specialization.
    [[nodiscard]] std::optional<Value> convert_record_type(Value value,
        const semantic::sv::TypeReference& type)
    {
        auto& widths = specialization_cache(specialization_)
                           .constants->record_type_widths;
        auto found = widths.find(&type);
        if (found == widths.end()) {
            std::unordered_set<std::uint32_t> visiting;
            found = widths.emplace(&type,
                resolved_type_width(type, &specialization_, visiting)).first;
        }
        return convert_constant_to_width(std::move(value), type, error_,
            found->second);
    }

    // Hierarchy identities read so far, including through cached values
    // that read one (value_for_declaration).
    std::size_t hierarchy_reads_ { };

    struct LocalUnpackedArray {
        std::int64_t left { };
        std::int64_t right { };
        semantic::sv::TypeReference element_type;
        Value default_value;
        std::vector<Value> elements;
    };

    struct Frame {
        semantic::DeclarationId callable;
        // The class whose parameters a class constant is evaluated with
        // (IEEE 1800-2017 8.25); its local parameters are kept here.
        const semantic::sv::ClassDeclaration* class_owner { };
        std::map<semantic::DeclarationId, Value> values;
        std::map<semantic::DeclarationId, LocalUnpackedArray> arrays;
        std::size_t array_elements { };
        std::uint64_t array_bits { };
        std::map<std::string, Value> pattern_values;
        std::optional<Value> result;
    };

    enum class Flow : std::uint8_t { normal,
        broke,
        continued,
        returned,
        failed };

    [[nodiscard]] bool consume_work(const std::uint64_t amount)
    {
        if (amount > maximum_constant_work_units - work_units_used_) {
            error_ = "constant function evaluation exceeds the work limit";
            return false;
        }
        work_units_used_ += amount;
        return true;
    }

    [[nodiscard]] static bool integral_array_element_type(
        const semantic::sv::TypeReference& type) noexcept
    {
        // A packed structure or union element is one packed value too
        // (IEEE 1800-2017 7.2.1, 7.3.1).
        if (type.value_form
            && (*type.value_form
                    == semantic::sv::TypeForm::packed_integral
                || *type.value_form
                    == semantic::sv::TypeForm::enumeration
                || *type.value_form
                    == semantic::sv::TypeForm::packed_structure
                || *type.value_form
                    == semantic::sv::TypeForm::packed_union)) {
            return true;
        }
        const auto& spelling = type.target.spelling;
        return spelling == "bit" || spelling == "logic"
            || spelling == "reg" || spelling == "byte"
            || spelling == "shortint" || spelling == "int"
            || spelling == "integer" || spelling == "longint";
    }

    [[nodiscard]] static bool fixed_one_dimensional_array_type(
        const semantic::sv::TypeReference& type) noexcept
    {
        return type.unpacked_dimensions.size() == 1U
            && (!type.container_form
                || *type.container_form
                    == semantic::sv::TypeForm::static_array)
            && !type.associative_index && !type.queue_maximum
            && integral_array_element_type(type);
    }

    [[nodiscard]] LocalUnpackedArray* find_local_array(
        const semantic::DeclarationId declaration)
    {
        for (auto frame = frames_.rbegin(); frame != frames_.rend(); ++frame) {
            if (const auto found = frame->arrays.find(declaration);
                found != frame->arrays.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    /// A frame-local unpacked array assigned or initialized as a whole: an
    /// assignment pattern's items take the element type (IEEE 1800-2017
    /// 10.9.1, 10.10); another array expression copies element by element
    /// (7.6). Nothing when the declaration is not a frame-local array.
    [[nodiscard]] std::optional<bool> assign_local_array(
        const semantic::DeclarationId declaration,
        const semantic::ExpressionId value)
    {
        const auto* target = find_local_array(declaration);
        if (target == nullptr) {
            return std::nullopt;
        }
        const auto element_type = target->element_type;
        const auto count = target->elements.size();
        const auto view = specialization_.find_expression(value);
        if (!view || view->systemverilog == nullptr
            || view->systemverilog->kind
                != ExpressionKind::assignment_pattern) {
            const auto source = evaluate_array(value);
            auto* array = find_local_array(declaration);
            return source && array != nullptr && copy_array(*array, *source);
        }
        std::optional<semantic::ExpressionId> fallback;
        std::vector<semantic::ExpressionId> positional;
        for (const auto& association : view->systemverilog->associations) {
            if (association.choice_spelling == "default") {
                fallback = association.value;
            } else if (association.choice_spelling.empty()) {
                positional.push_back(association.value);
            } else {
                return false;
            }
        }
        if (positional.size() != count && !(positional.empty() && fallback)) {
            error_ = "unpacked array pattern size differs from the array";
            return false;
        }
        // Items are evaluated before the array is written; a call among
        // them may push frames.
        std::vector<Value> elements;
        elements.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto element = evaluate_typed(
                positional.empty() ? *fallback : positional[index],
                element_type);
            if (!element) {
                return false;
            }
            auto converted = convert_hir_systemverilog_constant(
                std::move(*element), element_type, error_, &specialization_);
            if (!converted) {
                return false;
            }
            elements.push_back(std::move(*converted));
        }
        auto* array = find_local_array(declaration);
        if (array == nullptr) {
            return false;
        }
        array->elements = std::move(elements);
        return true;
    }

    [[nodiscard]] static std::optional<std::size_t> local_array_offset(
        const LocalUnpackedArray& array,
        const std::int64_t index) noexcept
    {
        if (index < std::min(array.left, array.right)
            || index > std::max(array.left, array.right)) {
            return std::nullopt;
        }
        const auto distance = array.left <= array.right
            ? static_cast<std::uint64_t>(index - array.left)
            : static_cast<std::uint64_t>(array.left - index);
        if (distance >= array.elements.size()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(distance);
    }

    /// A fixed one-dimensional unpacked array type, through typedefs. `type`
    /// is a HIR record, so the result is kept per specialization.
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    unpacked_array_type(const semantic::sv::TypeReference& type,
        const semantic::ScopeId scope) const
    {
        if (fixed_one_dimensional_array_type(type)) {
            return type;
        }
        auto& kept = specialization_cache(specialization_)
                         .constants->unpacked_array_types;
        if (const auto found = kept.find(&type);
            found != kept.end() && found->second.first == scope) {
            return found->second.second;
        }
        auto resolved = semantic::CompiledDesignResolver {
            specialization_
        }.underlying_systemverilog_type(type, scope);
        if (resolved && !fixed_one_dimensional_array_type(*resolved)) {
            resolved.reset();
        }
        kept.insert_or_assign(&type, std::pair { scope, resolved });
        return resolved;
    }

    [[nodiscard]] bool initialize_local_array(
        const semantic::DeclarationId declaration_id,
        const semantic::sv::Declaration& declaration)
    {
        if (frames_.empty()
            || declaration.form
                != semantic::sv::DeclarationForm::variable
            || !declaration.type) {
            return true;
        }
        const auto array_type
            = unpacked_array_type(*declaration.type, declaration.scope);
        if (!array_type || frames_.back().arrays.contains(declaration_id)) {
            return true;
        }
        return create_local_array(declaration_id, *array_type);
    }

    /// A frame-local array of a fixed one-dimensional unpacked array type,
    /// its elements at the element type's default.
    [[nodiscard]] bool create_local_array(
        const semantic::DeclarationId declaration_id,
        const semantic::sv::TypeReference& declared_type)
    {
        const auto& dimension = declared_type.unpacked_dimensions.front();
        const auto resolve_bound = [&](
            const std::optional<std::int64_t> value,
            const std::optional<semantic::ExpressionId> expression)
            -> std::optional<std::int64_t> {
            if (value) {
                return value;
            }
            if (!expression) {
                return std::nullopt;
            }
            const auto evaluated = evaluate(*expression);
            return evaluated ? evaluated->integer_value() : std::nullopt;
        };
        const auto left = resolve_bound(
            dimension.left, dimension.left_expression);
        const auto right = resolve_bound(
            dimension.right, dimension.right_expression);
        if (!left || !right) {
            error_ = "constant function array bounds must be known integers";
            return false;
        }
        const auto distance = support::index_distance(*left, *right);
        if (distance >= maximum_constant_local_array_elements) {
            error_ = "constant function local array exceeds the element limit";
            return false;
        }
        const auto element_count = static_cast<std::size_t>(distance + 1U);
        std::size_t active_array_elements { };
        for (const auto& frame : frames_) {
            if (frame.array_elements
                > maximum_constant_local_array_elements
                    - active_array_elements) {
                error_ = "constant function local arrays exceed the element limit";
                return false;
            }
            active_array_elements += frame.array_elements;
        }
        if (element_count
            > maximum_constant_local_array_elements
                - active_array_elements) {
            error_ = "constant function local arrays exceed the element limit";
            return false;
        }
        auto element_type = declared_type;
        element_type.container_form.reset();
        element_type.unpacked_dimensions.clear();
        element_type.container_element_types.clear();
        element_type.associative_index.reset();
        element_type.queue_maximum.reset();
        std::unordered_set<std::uint32_t> visiting;
        const auto width = resolved_type_width(
            element_type, &specialization_, visiting);
        if (!width || *width == 0U || *width > maximum_constant_width) {
            error_ = "constant function array element width is unresolved";
            return false;
        }
        const auto storage_value_count
            = static_cast<std::uint64_t>(element_count) + 1U;
        const auto array_bits = storage_value_count * *width;
        std::uint64_t active_array_bits { };
        for (const auto& frame : frames_) {
            if (frame.array_bits
                > maximum_constant_local_array_bits - active_array_bits) {
                error_ = "constant function local arrays exceed the storage limit";
                return false;
            }
            active_array_bits += frame.array_bits;
        }
        if (array_bits
            > maximum_constant_local_array_bits - active_array_bits) {
            error_ = "constant function local arrays exceed the storage limit";
            return false;
        }
        if (!consume_work(element_count)) {
            return false;
        }
        const auto element_domain = type_domain(element_type);
        // Two-state elements, including the integer atoms other than
        // `integer`, start at zero (IEEE 1800-2017 6.8, Table 6-7).
        const auto two_state = element_domain == frontend::ValueDomain::Bit2
            || (element_domain == frontend::ValueDomain::Integer
                && element_type.target.spelling != "integer");
        auto default_value = make_value(
            PackedLogic4 { static_cast<std::uint32_t>(*width),
                two_state ? Logic4::zero : Logic4::x },
            element_type.signed_value, false, element_domain);
        default_value.packed_range = element_type.packed_range;

        LocalUnpackedArray array;
        array.left = *left;
        array.right = *right;
        array.element_type = std::move(element_type);
        array.default_value = default_value;
        array.elements.resize(element_count, array.default_value);
        auto& frame = frames_.back();
        frame.array_elements += element_count;
        frame.array_bits += array_bits;
        frame.arrays.emplace(declaration_id, std::move(array));
        return true;
    }

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
            const auto value = literal_value(record, true, ignored);
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
            // Shift and exponentiation results have the left operand's
            // expression width; the right operand never widens the result
            // (IEEE 1800-2017 11.6.1).
            if (record.text == "**" || record.text == "<<"
                || record.text == "<<<" || record.text == ">>"
                || record.text == ">>>") {
                return finish(expression_width(
                    record.operands.front(), visiting));
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
            // An element of a fixed-size unpacked array has the bits of its
            // element type; a packed bit select is one bit (20.6.2).
            const auto base = specialization_.find_expression(
                record.operands.front());
            const auto selected = base && base->systemverilog != nullptr
                    && base->systemverilog->kind == ExpressionKind::name
                    && base->systemverilog->referenced_name
                ? base->systemverilog->referenced_name->selected
                : std::nullopt;
            static const std::optional<semantic::sv::TypeReference> none;
            const auto& type = selected ? declaration_type(*selected) : none;
            if (type && !type->unpacked_dimensions.empty()
                && (!type->container_form
                    || *type->container_form
                        == semantic::sv::TypeForm::static_array)) {
                auto element = *type;
                element.unpacked_dimensions.erase(
                    element.unpacked_dimensions.begin());
                if (element.unpacked_dimensions.empty()) {
                    element.container_form.reset();
                }
                std::unordered_set<std::uint32_t> types;
                const auto width = resolved_type_width(
                    element, &specialization_, types);
                return finish(width && *width != 0U
                            && *width <= maximum_constant_width
                        ? std::optional {
                              static_cast<std::uint32_t>(*width) }
                        : std::nullopt);
            }
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
            const auto distance = support::index_distance(*left, *right);
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
        // A typed pattern of an unpacked aggregate has its type's bit
        // stream (IEEE 1800-2017 10.9, 20.6.2).
        if (record.kind == ExpressionKind::assignment_pattern) {
            auto& layout = const_cast<HirConstantEvaluator&>(*this);
            if (const auto type = layout.typed_pattern_aggregate(record)) {
                if (const auto width = layout.aggregate_width(*type);
                    width && *width != 0U
                    && *width <= maximum_constant_width) {
                    return finish(static_cast<std::uint32_t>(*width));
                }
            }
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
        // A class property select has its declared type's width (20.6.2).
        if (record.kind == ExpressionKind::class_property
            && !record.class_member_identity.empty()) {
            const auto found = semantic::CompiledDesignResolver {
                specialization_ }
                .resolve_systemverilog_class_property(
                    record.class_member_identity);
            if (const auto* property = found.unique()) {
                std::unordered_set<std::uint32_t> type_visiting;
                if (const auto width = resolved_type_width(
                        property->type, &specialization_, type_visiting);
                    width && *width != 0U
                    && *width <= maximum_constant_width) {
                    return finish(static_cast<std::uint32_t>(*width));
                }
            }
        }
        if (const auto selected
            = systemverilog_constant_name_declaration(
                specialization_, record)) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            if (declaration && declaration->systemverilog != nullptr) {
                const auto& declaration_record
                    = *declaration->systemverilog;
                // A parameter without a type or range has the width of its
                // final value (IEEE 1800-2017 6.20.2).
                const bool implicit_parameter
                    = (declaration_record.form
                              == semantic::sv::DeclarationForm::parameter
                          || declaration_record.form
                              == semantic::sv::DeclarationForm::local_parameter)
                    && declaration_record.type
                    && declaration_record.type->target.spelling == "implicit"
                    && !declaration_record.type->packed_range;
                if (implicit_parameter) {
                    const auto& actuals
                        = specialization_.specialization().actual_identities;
                    const auto actual = std::ranges::find(actuals, *selected,
                        &semantic::SpecializedHirActualIdentity::declaration);
                    if (actual != actuals.end()) {
                        if (const auto parsed
                            = parse_canonical(actual->identity)) {
                            return finish(parsed->width);
                        }
                    } else if (declaration_record.initializer) {
                        return finish(expression_width(
                            *declaration_record.initializer, visiting));
                    }
                }
                // An unpacked structure or array has the bits of its whole
                // stream, unpacked array members included (6.24.3, 20.6.2).
                // The layout evaluates dimension bounds, so it is not const.
                auto& layout = const_cast<HirConstantEvaluator&>(*this);
                if (declaration_record.type
                    && layout.record_unpacked_aggregate(
                        *declaration_record.type)) {
                    if (const auto width
                        = layout.aggregate_width(*declaration_record.type);
                        width && *width != 0U
                        && *width <= maximum_constant_width) {
                        return finish(static_cast<std::uint32_t>(*width));
                    }
                }
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

    /// The self-determined width of an expression (IEEE 1800-2017 11.6.1),
    /// kept for the specialization: it reads declared types, not frames.
    [[nodiscard]] std::optional<std::uint32_t> expression_width(
        const semantic::ExpressionId expression) const
    {
        auto& widths = specialization_cache(specialization_)
                           .constants->expression_widths;
        if (const auto found = widths.find(expression.value());
            found != widths.end()) {
            return found->second;
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto width = expression_width(expression, visiting);
        widths.emplace(expression.value(), width);
        return width;
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
        // A class local parameter is evaluated in the innermost class
        // frame and kept there, never in the specialization caches; a
        // static method's body reads its class's frame.
        const auto class_frame = std::ranges::find_if(frames_.rbegin(),
            frames_.rend(), [](const Frame& frame) {
                return frame.class_owner != nullptr;
            });
        if (class_frame != frames_.rend()) {
            const auto& properties = class_frame->class_owner->properties;
            const auto property = std::ranges::find(properties, declaration,
                &semantic::sv::ClassProperty::declaration);
            if (property != properties.end()) {
                if (!property->parameter || !property->initializer) {
                    error_ = "class property '" + property->name
                        + "' is not a constant";
                    return std::nullopt;
                }
                const auto depth = frames_.size();
                const auto owner_index = static_cast<std::size_t>(
                    std::distance(class_frame, frames_.rend()) - 1);
                // An aggregate or packed property takes its declared
                // layout, as a declaration does (IEEE 1800-2023 10.9).
                auto value = scalar_kind(property->type) == ScalarKind::None
                    ? evaluate_typed(*property->initializer, property->type)
                    : evaluate(*property->initializer);
                if (value && frames_.size() == depth) {
                    frames_[owner_index].values.insert_or_assign(
                        declaration, *value);
                }
                return value;
            }
        }
        // Outside every frame, a declaration's value depends only on the
        // specialization, and a unit-scope declaration whose evaluation reads
        // no hierarchy identity depends only on the unit and its actuals; the
        // latter are shared with derived occurrence specializations
        // (derive_occurrence_specialization). A failure can depend on the
        // remaining work budget, so only values are kept.
        auto& constants = *specialization_cache(specialization_).constants;
        if (const auto found = constants.declarations.find(
                declaration.value());
            found != constants.declarations.end()) {
            ++hierarchy_reads_;
            return found->second;
        }
        auto& unit_values = *constants.unit_declarations;
        if (const auto found = unit_values.find(declaration.value());
            found != unit_values.end()) {
            return found->second;
        }
        const auto reads = hierarchy_reads_;
        const auto keep = [&](std::optional<Value> value) {
            if (value) {
                const auto view = specialization_.find_declaration(declaration);
                const bool unit_scope = view && view->systemverilog != nullptr
                    && view->systemverilog->scope == specialization_.scope();
                if (unit_scope && hierarchy_reads_ == reads) {
                    unit_values.emplace(declaration.value(), *value);
                } else {
                    ++hierarchy_reads_;
                    constants.declarations.emplace(declaration.value(), *value);
                }
            }
            return value;
        };
        const auto& actuals
            = specialization_.specialization().actual_identities;
        const auto actual = std::ranges::find(actuals, declaration,
            &semantic::SpecializedHirActualIdentity::declaration);
        if (actual != actuals.end()) {
            if (const auto scalar = parse_scalar_identity(actual->identity);
                scalar && scalar->kind == ScalarKind::Time) {
                return keep(make_known(
                    scalar->bits, 64U, false, false));
            }
            if (const auto scalar = parse_scalar_identity(actual->identity);
                scalar && real_family(scalar->kind)) {
                auto value = real_value(
                    scalar->real().value_or(0.0), scalar->kind);
                // A real actual of an integral formal rounds (6.12.2).
                const auto view = specialization_.find_declaration(
                    declaration);
                const auto formal_kind = view && view->systemverilog != nullptr
                        && view->systemverilog->type
                    ? scalar_kind(*view->systemverilog->type)
                    : ScalarKind::None;
                if (real_family(formal_kind)) {
                    auto converted = convert_value_kind(
                        std::move(value), formal_kind, error_);
                    return converted ? keep(std::move(converted))
                                     : std::nullopt;
                }
                if (view && view->systemverilog != nullptr
                    && view->systemverilog->type
                    && hir_systemverilog_explicit_integral_type(
                        *view->systemverilog->type)) {
                    value.scalar.reset();
                    auto converted = convert_record_type(
                        std::move(value), *view->systemverilog->type);
                    return converted ? keep(std::move(converted))
                                     : std::nullopt;
                }
                return keep(std::move(value));
            }
            if (const auto scalar = parse_scalar_identity(actual->identity);
                scalar && scalar->kind == ScalarKind::Chandle) {
                return keep(chandle_value(scalar->bits));
            }
            if (auto parsed = parse_canonical(actual->identity)) {
                return keep(std::move(parsed));
            }
            std::string ignored;
            if (auto parsed = parse_literal(
                    actual->identity, true, ignored)) {
                return keep(std::move(parsed));
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
        // A real-family or chandle declaration converts its initializer
        // to its own type (IEEE 1800-2017 6.12.2).
        const auto declared_kind = view->systemverilog->type
            ? scalar_kind(*view->systemverilog->type)
            : ScalarKind::None;
        auto result = view->systemverilog->type
                && !real_family(declared_kind)
                && declared_kind != ScalarKind::Chandle
            ? evaluate_typed(*view->systemverilog->initializer,
                  *view->systemverilog->type)
            : evaluate(*view->systemverilog->initializer);
        if (result && real_family(declared_kind)) {
            result = convert_value_kind(
                std::move(*result), declared_kind, error_);
        } else if (result && declared_kind == ScalarKind::Chandle) {
            result = convert_value_kind(
                std::move(*result), declared_kind, error_);
        } else if (result && view->systemverilog->type
            && hir_systemverilog_explicit_integral_type(
                *view->systemverilog->type)) {
            result->scalar.reset();
            result = convert_record_type(
                std::move(*result), *view->systemverilog->type);
        }
        remove();
        return keep(std::move(result));
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
        if (const auto member = frame_member(record)) {
            const auto& root = *member->root;
            runtime::PackedLogic4 selected {
                static_cast<std::size_t>(member->width), Logic4::x };
            for (std::uint64_t bit = 0U; bit < member->width; ++bit) {
                if (member->offset + bit < root.packed.width()) {
                    selected.set(bit, root.packed.get(member->offset + bit));
                }
            }
            Value result;
            result.packed = std::move(selected);
            result.width = static_cast<std::uint32_t>(member->width);
            result.domain = root.domain;
            return result;
        }
        // A function's formals and locals hide same-named parameters of the
        // enclosing scope (IEEE 1800-2017 13.4.3).
        if (!frames_.empty() && record.referenced_name) {
            if (const auto selected = record.referenced_name->selected) {
                for (auto frame = frames_.rbegin(); frame != frames_.rend();
                    ++frame) {
                    if (const auto found = frame->values.find(*selected);
                        found != frame->values.end()) {
                        return found->second;
                    }
                }
            }
        }
        const auto& hierarchy_identities
            = specialization_.specialization().hierarchy_identities;
        const auto hierarchy = std::ranges::find(
            hierarchy_identities, record.text,
            &semantic::SpecializedHirNamedIdentity::name);
        if (hierarchy != hierarchy_identities.end()) {
            ++hierarchy_reads_;
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
        // Outside a constant function, a variable's initializer is only its
        // starting value, not a constant (IEEE 1800-2017 11.2.1).
        if (frames_.empty()) {
            const auto view = specialization_.find_declaration(*selected);
            using Form = semantic::sv::DeclarationForm;
            if (view && view->systemverilog != nullptr
                && (view->systemverilog->form == Form::variable
                    || view->systemverilog->form == Form::net
                    || view->systemverilog->form == Form::port)
                && !view->systemverilog->array_local_parameter
                && !view->systemverilog->array_parameter) {
                error_ = "SystemVerilog name '" + record.text
                    + "' is not a constant";
                return std::nullopt;
            }
            // A parameter is declared before it is referenced in its scope
            // (IEEE 1800-2017 6.20.2, 23.10): a same-scope parameter written
            // after the reference is a forward reference. Only a use written
            // inside that scope can be one: a class parameter's actual
            // substituted into the class body keeps the body's source.
            const auto& semantics = specialization_.design().semantics;
            const auto& spans = semantics.source_spans();
            const auto& scopes = semantics.scopes();
            const auto valid_span = [&](const semantic::SourceSpanId id) {
                return id.valid() && id.value() < spans.size();
            };
            if (view && view->systemverilog != nullptr
                && (view->systemverilog->form == Form::parameter
                    || view->systemverilog->form == Form::local_parameter)
                && view->systemverilog->scope == record.scope
                && record.scope.valid() && record.scope.value() < scopes.size()
                && valid_span(record.source)
                && valid_span(view->systemverilog->source)
                && valid_span(scopes[record.scope.value()].source)) {
                const auto& use = spans[record.source.value()];
                const auto& declaration
                    = spans[view->systemverilog->source.value()];
                const auto& scope
                    = spans[scopes[record.scope.value()].source.value()];
                if (use.file == declaration.file && use.file == scope.file
                    && !use.expansion && !declaration.expansion
                    && use.begin.offset >= scope.begin.offset
                    && declaration.begin.offset > use.begin.offset) {
                    error_ = "parameter '" + record.text
                        + "' is referenced before its declaration";
                    return std::nullopt;
                }
            }
        }
        auto result = value_for_declaration(*selected);
        // A declaration reference uses the value parameter's resolved or
        // inferred type. Do not carry an unbased-unsized literal's contextual
        // fill behavior across that declaration boundary (5.7.1, 6.20.2).
        if (result) {
            result->unsized = false;
        }
        // A member of a structure parameter (`P.m`, IEEE 1800-2017 7.2.1).
        if (const auto dot = record.text.find('.');
            result && dot != std::string::npos) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            // A package-qualified prefix (`pkg::P.m`) names P.
            auto prefix = std::string_view { record.text }.substr(0U, dot);
            if (const auto scope = prefix.rfind("::");
                scope != std::string_view::npos) {
                prefix.remove_prefix(scope + 2U);
            }
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->name == prefix) {
                const auto slice = declaration->systemverilog->type
                    ? member_slice(*declaration->systemverilog->type,
                          std::string_view { record.text }.substr(dot + 1U))
                    : std::nullopt;
                if (!slice) {
                    error_ = "cannot select member '" + record.text + "'";
                    return std::nullopt;
                }
                runtime::PackedLogic4 member {
                    static_cast<std::size_t>(slice->second), Logic4::x };
                for (std::uint64_t bit = 0U; bit < slice->second; ++bit) {
                    if (slice->first + bit < result->packed.width()) {
                        member.set(bit,
                            result->packed.get(slice->first + bit));
                    }
                }
                Value selected_member;
                selected_member.packed = std::move(member);
                selected_member.width
                    = static_cast<std::uint32_t>(slice->second);
                selected_member.domain = result->domain;
                return selected_member;
            }
        }
        if (result) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->type) {
                result->packed_range
                    = declaration->systemverilog->type->packed_range;
                // Bounds written as constant expressions (`[base+15:base]`)
                // still number the parameter's bits (IEEE 1800-2017 6.20.2).
                if (auto& range = result->packed_range) {
                    if (!range->left && range->left_expression) {
                        range->left = specialization_
                            .evaluate_integral_expression(
                                *range->left_expression);
                    }
                    if (!range->right && range->right_expression) {
                        range->right = specialization_
                            .evaluate_integral_expression(
                                *range->right_expression);
                    }
                }
            }
        }
        return result;
    }

    /// The value a plain name bound in a constant-function frame denotes,
    /// as evaluate_name finds it, read in place; null for any other name.
    [[nodiscard]] const Value* frame_local(
        const semantic::ExpressionId expression) const
    {
        if (frames_.empty()) {
            return nullptr;
        }
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return nullptr;
        }
        const auto& record = *view->systemverilog;
        if (record.kind != ExpressionKind::name || record.decimal_literal
            || !record.referenced_name
            || !record.referenced_name->selected || record.text == "$"
            || record.text.starts_with('@')
            || record.text.find('.') != std::string::npos) {
            return nullptr;
        }
        for (auto frame = frames_.rbegin(); frame != frames_.rend();
             ++frame) {
            if (frame->pattern_values.contains(record.text)) {
                return nullptr;
            }
        }
        for (auto frame = frames_.rbegin(); frame != frames_.rend();
             ++frame) {
            if (const auto found
                = frame->values.find(*record.referenced_name->selected);
                found != frame->values.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<Value> evaluate_unary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 1U) {
            return std::nullopt;
        }
        if ((record.text == "-" || record.text == "+")
            && real_valued(record.operands.front())) {
            auto operand = evaluate(record.operands.front());
            const auto real = operand ? real_of(*operand) : std::nullopt;
            if (!real) {
                return std::nullopt;
            }
            const auto kind = real_family(value_kind(*operand))
                ? value_kind(*operand)
                : ScalarKind::Real;
            return real_value(record.text == "-" ? -*real : *real, kind);
        }
        // `-`, `~` and `+` take the context width of their expression
        // before applying (IEEE 1800-2017 11.6.1).
        const auto context = (record.text == "-" || record.text == "~"
                                 || record.text == "+")
            ? take_context(record.id)
            : std::nullopt;
        if (context) {
            contexts_[record.operands.front().value()] = *context;
        }
        auto operand = evaluate(record.operands.front());
        contexts_.erase(record.operands.front().value());
        if (!operand) {
            return std::nullopt;
        }
        if (context && !operand->unbounded && operand->width < *context) {
            *operand = resized(std::move(*operand), *context);
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

    /// evaluate_binary's result for Logic4 operands of at most 64 bits, by
    /// word arithmetic; nullopt leaves the operator to the general path
    /// (which also reports errors).
    [[nodiscard]] std::optional<std::optional<Value>> small_binary(
        const std::string_view op, const Value& left, const Value& right)
    {
        const auto l = small_words(left);
        const auto r = small_words(right);
        if (!l || !r) {
            return std::nullopt;
        }
        if (op == "<<" || op == "<<<" || op == ">>" || op == ">>>") {
            if (r->b != 0U) {
                return std::nullopt;
            }
            const auto count = right.signed_value
                ? small_signed(r->a, right.width)
                : static_cast<std::int64_t>(r->a);
            if (count < 0 || (!right.signed_value && r->a > 0x7fffffffU)) {
                return std::nullopt;
            }
            const auto width = left.width;
            const auto amount = static_cast<std::uint64_t>(count) >= width
                ? width : static_cast<std::uint32_t>(count);
            const auto mask = width_mask(width);
            SmallWords out;
            if (op == "<<" || op == "<<<") {
                out.a = amount >= 64U ? 0U : (l->a << amount) & mask;
                out.b = amount >= 64U ? 0U : (l->b << amount) & mask;
            } else {
                out.a = amount >= 64U ? 0U : l->a >> amount;
                out.b = amount >= 64U ? 0U : l->b >> amount;
                if (op == ">>>" && left.signed_value && amount != 0U) {
                    const auto fill = mask & ~width_mask(width - amount);
                    if (((l->a >> (width - 1U)) & 1U) != 0U) {
                        out.a |= fill;
                    }
                    if (((l->b >> (width - 1U)) & 1U) != 0U) {
                        out.b |= fill;
                    }
                }
            }
            auto result = left;
            result.packed = PackedLogic4::from_aval_bval(width, out.a, out.b);
            return std::optional<Value> { std::move(result) };
        }
        const auto width = std::max(left.width, right.width);
        // Keep an unequal-width unbased fill on the common-operand path,
        // which expands `'0`, `'1`, `'x`, or `'z` across the wider operand.
        // This fast path's plain sign/zero extension would change that value.
        if ((left.unsized && left.width == 1U && left.width < width)
            || (right.unsized && right.width == 1U
                && right.width < width)) {
            return std::nullopt;
        }
        // Operands extend as the expression's type: unsigned when either
        // is unsigned (IEEE 1800-2017 11.8.2).
        const auto signed_value = left.signed_value && right.signed_value;
        const auto lhs = small_resized(*l, left.width, width, signed_value);
        const auto rhs = small_resized(*r, right.width, width, signed_value);
        const auto known = lhs.b == 0U && rhs.b == 0U;
        const auto domain = left.domain == right.domain
            ? left.domain : frontend::ValueDomain::Logic4;
        const auto mask = width_mask(width);
        const auto word = [&](const std::uint64_t a) {
            return std::optional<Value> { make_value(
                PackedLogic4::from_aval_bval(width, a & mask, 0U),
                signed_value, false, domain) };
        };
        if (op == "==" || op == "!=" || op == "=" || op == "/=") {
            if (l->b != 0U || r->b != 0U) {
                return std::optional<Value> { logical_result(Truth::unknown) };
            }
            const auto equal = (lhs.a == rhs.a) == (op == "==" || op == "=");
            return std::optional<Value> { logical_result(
                equal ? Truth::true_value : Truth::false_value) };
        }
        if (op == "===" || op == "!==") {
            const auto equal = (lhs.a == rhs.a && lhs.b == rhs.b) == (op == "===");
            return std::optional<Value> { logical_result(
                equal ? Truth::true_value : Truth::false_value) };
        }
        if (!known) {
            return std::nullopt;
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            const auto compared = signed_value
                ? (small_signed(lhs.a, width) < small_signed(rhs.a, width) ? -1
                      : small_signed(lhs.a, width) > small_signed(rhs.a, width) ? 1 : 0)
                : (lhs.a < rhs.a ? -1 : lhs.a > rhs.a ? 1 : 0);
            const auto result = op == "<" ? compared < 0
                : op == "<=" ? compared <= 0
                : op == ">" ? compared > 0
                            : compared >= 0;
            return std::optional<Value> { logical_result(
                result ? Truth::true_value : Truth::false_value) };
        }
        if (op == "&") {
            return word(lhs.a & rhs.a);
        }
        if (op == "|") {
            return word(lhs.a | rhs.a);
        }
        if (op == "^") {
            return word(lhs.a ^ rhs.a);
        }
        if (op == "~^" || op == "^~") {
            return word(~(lhs.a ^ rhs.a));
        }
        if (op == "+") {
            return word(lhs.a + rhs.a);
        }
        if (op == "-") {
            return word(lhs.a - rhs.a);
        }
        if (op == "*") {
            return word(lhs.a * rhs.a);
        }
        if (op == "/" || op == "%") {
            if (rhs.a == 0U) {
                return std::nullopt;
            }
            if (!signed_value) {
                return word(op == "/" ? lhs.a / rhs.a : lhs.a % rhs.a);
            }
            const auto dividend = small_signed(lhs.a, width);
            const auto divisor = small_signed(rhs.a, width);
            const auto minimum = width >= 64U
                ? std::numeric_limits<std::int64_t>::min()
                : -(std::int64_t { 1 } << (width - 1U));
            if (dividend == minimum && divisor == -1) {
                return std::nullopt;
            }
            return word(static_cast<std::uint64_t>(
                op == "/" ? dividend / divisor : dividend % divisor));
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool contextual_binary_operator(
        const std::string_view op) noexcept
    {
        return op == "+" || op == "-" || op == "*" || op == "/"
            || op == "%" || op == "&" || op == "|" || op == "^"
            || op == "~^" || op == "^~" || op == "==" || op == "!="
            || op == "===" || op == "!==" || op == "<" || op == "<="
            || op == ">" || op == ">=";
    }

    [[nodiscard]] bool context_determined_operator(
        const semantic::ExpressionId id) const
    {
        const auto view = specialization_.find_expression(id);
        if (!view || view->systemverilog == nullptr) {
            return false;
        }
        const auto& operand = *view->systemverilog;
        if (operand.kind == ExpressionKind::unary) {
            return operand.text == "-" || operand.text == "~"
                || operand.text == "+";
        }
        // A negated literal is folded into the literal (`-1`).
        if (operand.kind == ExpressionKind::integer_literal) {
            return operand.text.starts_with('-');
        }
        return operand.kind == ExpressionKind::binary
            && (operand.text == "+" || operand.text == "-"
                || operand.text == "*" || operand.text == "/"
                || operand.text == "%" || operand.text == "&"
                || operand.text == "|" || operand.text == "^"
                || operand.text == "~^" || operand.text == "^~");
    }

    /// The operator classes and operand facts of a binary expression,
    /// kept for the specialization: they read declared types, not frames.
    [[nodiscard]] const SpecializationConstants::BinaryPlan& binary_plan(
        const semantic::sv::Expression& record)
    {
        auto& plans = specialization_cache(specialization_)
                          .constants->binary_plans;
        if (const auto found = plans.find(record.id.value());
            found != plans.end()) {
            return found->second;
        }
        SpecializationConstants::BinaryPlan plan;
        const auto& op = record.text;
        // Only these operators take real operands (IEEE 1800-2017 11.3.1,
        // Table 11-1); `%`, case and wildcard equality, bitwise and shift
        // operators are integral only.
        plan.real_operator = op == "+" || op == "-" || op == "*"
            || op == "/" || op == "**" || op == "==" || op == "!="
            || op == "<" || op == "<=" || op == ">" || op == ">=";
        plan.logical_operator
            = op == "&&" || op == "||" || op == "->" || op == "<->";
        plan.real_operand = real_valued(record.operands.front())
            || real_valued(record.operands.back());
        plan.chandle_operand
            = value_kind_hint(record.operands.front()) == ScalarKind::Chandle
            || value_kind_hint(record.operands.back()) == ScalarKind::Chandle;
        plan.arithmetic_context = op == "+" || op == "-" || op == "*"
            || op == "/" || op == "%" || op == "&" || op == "|" || op == "^"
            || op == "~^" || op == "^~";
        plan.relational_context = op == "==" || op == "!=" || op == "==="
            || op == "!==" || op == "<" || op == "<=" || op == ">"
            || op == ">=";
        plan.left_context = op == "**" || op == "<<" || op == ">>"
            || op == "<<<" || op == ">>>";
        plan.contextual = contextual_binary_operator(op);
        plan.left_context_determined
            = context_determined_operator(record.operands.front());
        plan.right_context_determined
            = context_determined_operator(record.operands.back());
        const bool integral = !(plan.real_operator
                                  && (plan.real_operand
                                      || plan.chandle_operand))
            && !(!plan.real_operator && !plan.logical_operator
                && plan.real_operand);
        if (integral
            && (plan.arithmetic_context || plan.relational_context
                || plan.left_context)) {
            plan.left_width = expression_width(record.operands.front());
            if (!plan.left_context && plan.left_width) {
                plan.right_width = expression_width(record.operands.back());
            }
        }
        return plans.insert_or_assign(record.id.value(), std::move(plan))
            .first->second;
    }

    [[nodiscard]] std::optional<Value> evaluate_binary(
        const semantic::sv::Expression& record)
    {
        if (record.operands.size() != 2U) {
            return std::nullopt;
        }
        const auto& plan = binary_plan(record);
        if (!plan.real_operator && !plan.logical_operator
            && plan.real_operand) {
            error_ = "operator '" + record.text
                + "' is not legal for real operands";
            return std::nullopt;
        }
        if (plan.real_operator
            && (plan.real_operand || plan.chandle_operand)) {
            return evaluate_real_binary(record);
        }
        // Operands of arithmetic and bitwise operators take the width of
        // the whole expression, those of a relational operator the wider
        // of the two, and the left operand of a power or shift its context
        // (IEEE 1800-2017 11.6.1, 11.8.2); they are extended before the
        // operator applies, so no carry is lost.
        const bool left_context = plan.left_context;
        const auto outer = plan.arithmetic_context || left_context
            ? take_context(record.id)
            : std::nullopt;
        std::optional<std::uint32_t> target;
        if (plan.arithmetic_context || plan.relational_context
            || left_context) {
            const auto left_width = plan.left_width;
            const auto right_width = left_context
                ? std::optional<std::uint32_t> { 0U }
                : plan.right_width;
            if (left_width && right_width) {
                auto width = std::max(*left_width, *right_width);
                if (outer) {
                    width = std::max(width, *outer);
                }
                if (width != 0U && width <= maximum_constant_width) {
                    target = width;
                }
            }
        }
        if (target) {
            contexts_[record.operands.front().value()] = *target;
            if (!left_context) {
                contexts_[record.operands.back().value()] = *target;
            }
        }
        auto left = evaluate(record.operands.front());
        contexts_.erase(record.operands.front().value());
        if (!left) {
            contexts_.erase(record.operands.back().value());
            return std::nullopt;
        }
        if (record.text == "&&" && truth(*left) == Truth::false_value) {
            return logical_result(Truth::false_value);
        }
        if (record.text == "||" && truth(*left) == Truth::true_value) {
            return logical_result(Truth::true_value);
        }
        auto right = evaluate(record.operands.back());
        contexts_.erase(record.operands.back().value());
        if (!right) {
            return std::nullopt;
        }
        if (left->unbounded || right->unbounded) {
            error_ = "symbolic unbounded '$' is only valid as a parameter "
                     "value or an argument to $isunbounded";
            return std::nullopt;
        }
        if (target) {
            if (left_context) {
                if (left->width < *target) {
                    *left = resized(std::move(*left), *target);
                }
            } else {
                // An unsigned operand makes both unsigned (11.8.1), so a
                // signed operand is zero-extended, except a negated literal
                // (`-1`), whose negation applies after the extension.
                const bool both_signed
                    = left->signed_value && right->signed_value;
                const auto extend = [&](Value& value,
                                        const bool context_determined) {
                    const bool negated = value.signed_value
                        && context_determined;
                    if (!both_signed && !negated) {
                        value.signed_value = false;
                    }
                    // Leave one-bit unsized fill literals at their
                    // self-determined width until common_operand() applies
                    // their context-specific all-bit fill semantics.
                    const bool unsized_fill
                        = value.unsized && value.width == 1U;
                    if (value.width < *target && !unsized_fill) {
                        value = resized(std::move(value), *target);
                    }
                    if (!both_signed) {
                        value.signed_value = false;
                    }
                };
                extend(*left, plan.left_context_determined);
                extend(*right, plan.right_context_determined);
            }
        }
        // The context of an arithmetic, bitwise or relational operator
        // reaches into an operand that is itself such an operator (`-1` in
        // `-1 + 64'd0`) before its operator applies (IEEE 1800-2017 11.6.1,
        // 11.8.2). Evaluated self-determined, a signed one of these is
        // sign-extended to the common width first; a primary operand
        // extends by the expression's signedness.
        if (plan.contextual
            && !(left->signed_value && right->signed_value)) {
            const auto width = std::max(left->width, right->width);
            const auto widen = [&](Value& value,
                                   const bool context_determined) {
                if (!value.signed_value || value.width >= width
                    || !context_determined) {
                    return;
                }
                value = resized(std::move(value), width);
            };
            widen(*left, plan.left_context_determined);
            widen(*right, plan.right_context_determined);
        }
        if (auto fast = small_binary(record.text, *left, *right)) {
            return std::move(*fast);
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
            // The self-determined shift operand is unsigned, even when its
            // declaration or literal is signed (IEEE 1800-2017 11.4.10).
            const auto count = unsigned_integer(*right);
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
            // Division or modulus by zero gives x (IEEE 1800-2017 11.4.2).
            if (right_value == 0) {
                return make_unknown(width, signed_value);
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
                // Zero to a negative power is x (IEEE 1800-2017 11.4.3).
                if (left_value == 0) {
                    return make_unknown(width, signed_value);
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

    // An element of an unpacked array parameter (`P[1]` for `parameter int
    // P[3:1] = '{10, 20, 30}`, IEEE 1800-2017 6.20.1, 7.4): the positional
    // pattern lists the elements from the left bound.
    /// `P.m[i]`: an element of an unpacked array member of the structure
    /// constant P (IEEE 1800-2017 7.2.1, 7.4). Null for any other index.
    [[nodiscard]] std::optional<Value> evaluate_member_array_element(
        const semantic::sv::Expression& record)
    {
        const auto base_view = specialization_.find_expression(
            record.operands.front());
        if (!base_view || base_view->systemverilog == nullptr
            || base_view->systemverilog->kind != ExpressionKind::name) {
            return std::nullopt;
        }
        const auto& text = base_view->systemverilog->text;
        const auto dot = text.find('.');
        const auto resolved = dot == std::string::npos
            ? std::nullopt
            : systemverilog_constant_name_declaration(
                  specialization_, *base_view->systemverilog);
        if (!resolved) {
            return std::nullopt;
        }
        const auto selected = *resolved;
        const auto declaration = specialization_.find_declaration(selected);
        // A package-qualified prefix (`pkg::P.m`) names P.
        auto prefix = std::string_view { text }.substr(0U, dot);
        if (const auto scope = prefix.rfind("::");
            scope != std::string_view::npos) {
            prefix.remove_prefix(scope + 2U);
        }
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type
            || declaration->systemverilog->name != prefix
            || !unpacked_aggregate(*declaration->systemverilog->type)) {
            return std::nullopt;
        }
        const auto index = evaluate(record.operands.back());
        const auto integer = index ? index->integer_value() : std::nullopt;
        if (!integer) {
            return std::nullopt;
        }
        const auto slice = aggregate_slice(*declaration->systemverilog->type,
            text.substr(dot) + "[" + std::to_string(*integer) + "]");
        if (!slice) {
            return std::nullopt;
        }
        const auto whole = value_for_declaration(selected);
        if (!whole) {
            return std::nullopt;
        }
        runtime::PackedLogic4 element {
            static_cast<std::size_t>(slice->width), Logic4::x };
        for (std::uint64_t bit = 0U; bit < slice->width; ++bit) {
            if (slice->offset + bit < whole->packed.width()) {
                element.set(bit, whole->packed.get(slice->offset + bit));
            }
        }
        Value result;
        result.packed = std::move(element);
        result.width = static_cast<std::uint32_t>(slice->width);
        result.domain = whole->domain;
        return result;
    }

    [[nodiscard]] std::optional<Value> evaluate_array_parameter_element(
        const semantic::sv::Expression& record)
    {
        const auto base_view = specialization_.find_expression(
            record.operands.front());
        if (!base_view || base_view->systemverilog == nullptr
            || base_view->systemverilog->kind != ExpressionKind::name
            || !base_view->systemverilog->referenced_name
            || !base_view->systemverilog->referenced_name->selected) {
            return std::nullopt;
        }
        const auto declaration = specialization_.find_declaration(
            *base_view->systemverilog->referenced_name->selected);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type
            || (declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::parameter
                && declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::local_parameter
                && !declaration->systemverilog->array_parameter
                && !declaration->systemverilog->array_local_parameter)
            || !fixed_one_dimensional_array_type(
                *declaration->systemverilog->type)) {
            return std::nullopt;
        }
        const auto& declared_type = *declaration->systemverilog->type;
        const auto& dimension = declared_type.unpacked_dimensions.front();
        const auto bound = [&](const std::optional<std::int64_t> value,
                               const std::optional<semantic::ExpressionId>
                                   expression) -> std::optional<std::int64_t> {
            if (value) {
                return value;
            }
            const auto evaluated = expression ? evaluate(*expression)
                                              : std::nullopt;
            return evaluated ? evaluated->integer_value() : std::nullopt;
        };
        const auto left = bound(dimension.left, dimension.left_expression);
        const auto right = bound(dimension.right, dimension.right_expression);
        auto element_type = declared_type;
        element_type.container_form.reset();
        element_type.unpacked_dimensions.clear();
        element_type.container_element_types.clear();
        std::unordered_set<std::uint32_t> visiting;
        const auto width = resolved_type_width(
            element_type, &specialization_, visiting);
        if (!left || !right || !width || *width == 0U) {
            return std::nullopt;
        }
        // The elements come from the declaration's positional assignment
        // pattern.
        const auto pattern = declaration->systemverilog->initializer
            ? specialization_.find_expression(
                  *declaration->systemverilog->initializer)
            : std::nullopt;
        const auto count = static_cast<std::uint64_t>(
            *left >= *right ? *left - *right : *right - *left) + 1U;
        // An instance's value for the parameter replaces its default.
        const auto actual = declaration->systemverilog->array_parameter
            ? specialization_.systemverilog_array_elements(
                  *base_view->systemverilog->referenced_name->selected)
            : std::nullopt;
        if (actual && actual->size() != count) {
            return std::nullopt;
        }
        if (!actual && (!pattern || pattern->systemverilog == nullptr
            || pattern->systemverilog->kind
                != ExpressionKind::assignment_pattern
            || pattern->systemverilog->associations.size() != count
            || std::ranges::any_of(pattern->systemverilog->associations,
                [](const auto& association) {
                    return !association.choice_spelling.empty();
                }))) {
            return std::nullopt;
        }
        const auto index = evaluate(record.operands.back());
        if (!index) {
            return std::nullopt;
        }
        const auto integer = index->integer_value();
        if (!integer || *integer < std::min(*left, *right)
            || *integer > std::max(*left, *right)) {
            // An out-of-range element reads the element type's default.
            return convert_hir_systemverilog_constant(
                make_value(PackedLogic4 { static_cast<std::uint32_t>(*width),
                               type_domain(element_type)
                                       == frontend::ValueDomain::Bit2
                                   ? Logic4::zero : Logic4::x },
                    false, false, type_domain(element_type)),
                element_type, error_, &specialization_);
        }
        const auto ordinal = static_cast<std::size_t>(
            *integer >= *left ? *integer - *left : *left - *integer);
        if (actual) {
            auto element = array_element_value((*actual)[ordinal]);
            if (!element) {
                error_ = "array parameter element is not a constant value";
                return std::nullopt;
            }
            return convert_hir_systemverilog_constant(
                std::move(*element), element_type, error_, &specialization_);
        }
        auto element = evaluate(
            pattern->systemverilog->associations[ordinal].value);
        if (!element) {
            return std::nullopt;
        }
        return convert_hir_systemverilog_constant(
            std::move(*element), element_type, error_, &specialization_);
    }

    /// The packed dimensions of a name or of an element of one, outermost
    /// first, when they are constant.
    [[nodiscard]] std::optional<std::vector<std::pair<std::int64_t,
        std::int64_t>>>
    packed_shape(const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            auto shape = packed_shape(record.operands.front());
            if (!shape || shape->size() < 2U) {
                return std::nullopt;
            }
            shape->erase(shape->begin());
            return shape;
        }
        if (record.kind != ExpressionKind::name || !record.referenced_name
            || !record.referenced_name->selected) {
            return std::nullopt;
        }
        const auto& declared
            = declaration_type(*record.referenced_name->selected);
        const auto* type = declared ? &*declared : nullptr;
        // A member of a structure (`s.m`) has its member's dimensions
        // (IEEE 1800-2017 7.2.1).
        semantic::sv::TypeReference member_type;
        if (const auto dot = record.text.find('.');
            type && dot != std::string::npos) {
            if (!packed_member_slice(*type,
                    std::string_view { record.text }.substr(dot + 1U),
                    &member_type)) {
                return std::nullopt;
            }
            type = &member_type;
        }
        if (!type || type->packed_dimensions.size() < 2U
            || !type->unpacked_dimensions.empty() || type->container_form) {
            return std::nullopt;
        }
        std::vector<std::pair<std::int64_t, std::int64_t>> shape;
        for (const auto& range : type->packed_dimensions) {
            const auto bound = [&](const std::optional<std::int64_t> value,
                                   const std::optional<semantic::ExpressionId>
                                       bound_expression)
                -> std::optional<std::int64_t> {
                if (bound_expression) {
                    const auto evaluated = evaluate(*bound_expression);
                    return evaluated ? evaluated->integer_value()
                                     : std::nullopt;
                }
                return value;
            };
            const auto left = bound(range.left, range.left_expression);
            const auto right = bound(range.right, range.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            shape.emplace_back(*left, *right);
        }
        return shape;
    }

    [[nodiscard]] std::optional<Value> evaluate_selection(
        const semantic::sv::Expression& record)
    {
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            const auto base_view = specialization_.find_expression(
                record.operands.front());
            if (base_view && base_view->systemverilog != nullptr
                && base_view->systemverilog->kind == ExpressionKind::name
                && base_view->systemverilog->referenced_name
                && base_view->systemverilog->referenced_name->selected) {
                const auto declaration
                    = *base_view->systemverilog->referenced_name->selected;
                if (find_local_array(declaration) != nullptr) {
                    const auto index = evaluate(record.operands.back());
                    if (!index || !consume_work(1U)) {
                        return std::nullopt;
                    }
                    const auto* array = find_local_array(declaration);
                    if (array == nullptr) {
                        return std::nullopt;
                    }
                    const auto integer = index->integer_value();
                    if (!integer) {
                        return array->default_value;
                    }
                    const auto offset = local_array_offset(*array, *integer);
                    return offset ? std::optional { array->elements[*offset] }
                                  : std::optional { array->default_value };
                }
            }
            // An element of a packed structure array constant (7.4.1).
            if (auto element = typed_constant(record.id)) {
                if (sv_const_trace()) {
                    std::fprintf(stderr,
                        "FSIM_SV_CONST structure array element = %s\n",
                        element->first.canonical().c_str());
                }
                return std::move(element->first);
            }
            if (sv_const_trace()) {
                std::fprintf(stderr,
                    "FSIM_SV_CONST no structure array element (%s)\n",
                    error_.c_str());
            }
            error_.clear();
            if (auto element = evaluate_array_parameter_element(record)) {
                return element;
            }
            if (!error_.empty()) {
                return std::nullopt;
            }
            if (auto element = evaluate_member_array_element(record)) {
                return element;
            }
            // An index of a multidimensional packed array selects an element
            // of its outermost dimension (IEEE 1800-2017 7.4.5).
            if (const auto shape = packed_shape(record.operands.front());
                shape && shape->size() > 1U) {
                const auto base = evaluate(record.operands.front());
                const auto index = base
                    ? evaluate(record.operands.back()) : std::nullopt;
                if (!base || !index) {
                    return std::nullopt;
                }
                std::uint64_t element_width = 1U;
                for (std::size_t level = 1U; level < shape->size(); ++level) {
                    const auto& [left, right] = (*shape)[level];
                    element_width *= static_cast<std::uint64_t>(
                        std::max(left, right) - std::min(left, right) + 1);
                }
                if (element_width > maximum_constant_width) {
                    return std::nullopt;
                }
                const auto width = static_cast<std::uint32_t>(element_width);
                const auto integer = index->known()
                    ? index->integer_value() : std::nullopt;
                const auto [left, right] = shape->front();
                if (!integer || *integer < std::min(left, right)
                    || *integer > std::max(left, right)) {
                    return make_value(
                        PackedLogic4 { width,
                            base->domain == frontend::ValueDomain::Bit2
                                ? Logic4::zero : Logic4::x },
                        false, false, base->domain);
                }
                const auto element = static_cast<std::uint64_t>(
                    left >= right ? *integer - right : right - *integer);
                auto packed = PackedLogic4 { width, Logic4::zero };
                for (std::uint32_t bit = 0U; bit < width; ++bit) {
                    const auto from = element * width + bit;
                    if (from < base->packed.width()) {
                        packed.set(bit, runtime::to_logic4(
                            base->packed.get_logic9(from)));
                    }
                }
                return make_value(
                    std::move(packed), false, false, base->domain);
            }
            // A frame local is read in place rather than copied; operand
            // evaluation order is unspecified (IEEE 1800-2017 11.3.5).
            std::optional<Value> base_value;
            std::optional<Value> index;
            const Value* base = nullptr;
            if (frame_local(record.operands.front()) != nullptr) {
                index = evaluate(record.operands.back());
                base = frame_local(record.operands.front());
            } else {
                base_value = evaluate(record.operands.front());
                base = base_value ? &*base_value : nullptr;
                index = base ? evaluate(record.operands.back())
                             : std::nullopt;
            }
            if (base == nullptr || !index || !index->known()) {
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
            const auto distance
                = support::index_distance(*first_index, *second_index);
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
        // An ascending base (`[0:15]`) takes `p[b +: w]` as `p[b : b+w-1]`
        // and `p[b -: w]` as `p[b-w+1 : b]`, both with the MSB on the left
        // (IEEE 1800-2017 11.5.1).
        const bool ascending = base->packed_range && base->packed_range->left
            && base->packed_range->right
            && *base->packed_range->left < *base->packed_range->right;
        for (std::uint32_t bit = 0U; bit < result_width; ++bit) {
            std::int64_t source_index { };
            if (record.text == "+:") {
                source_index = ascending
                    ? *first_index
                        + static_cast<std::int64_t>(result_width - bit - 1U)
                    : *first_index + bit;
            } else if (record.text == "-:") {
                source_index = ascending
                    ? *first_index - bit
                    : *first_index
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

    struct ArrayQueryDimensions {
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        std::size_t unpacked { };
    };

    /// The dimensions of a named fixed-size unpacked array, unpacked ones
    /// first and then those of its packed element (IEEE 1800-2017 20.7);
    /// nothing for any other operand.
    [[nodiscard]] std::optional<ArrayQueryDimensions> array_query_dimensions(
        const semantic::ExpressionId operand)
    {
        const auto view = specialization_.find_expression(operand);
        if (!view || view->systemverilog == nullptr
            || view->systemverilog->kind != ExpressionKind::name) {
            return std::nullopt;
        }
        auto declaration = systemverilog_constant_name_declaration(
            specialization_, *view->systemverilog);
        if (!declaration) {
            // An unpacked array parameter is held as a constant variable
            // (6.20.1), which a package import may make visible.
            declaration = semantic::CompiledDesignResolver { specialization_ }
                .resolve_systemverilog_expression(operand,
                    [](const semantic::CompiledDeclarationView& candidate) {
                        return candidate.systemverilog != nullptr
                            && (candidate.systemverilog->array_parameter
                                || candidate.systemverilog
                                       ->array_local_parameter);
                    })
                .unique();
        }
        // A query of a fixed-size net or variable is a constant function
        // of its declared dimensions (IEEE 1800-2017 20.7).
        using Form = semantic::sv::DeclarationForm;
        if (!declaration) {
            declaration = semantic::CompiledDesignResolver { specialization_ }
                .resolve_systemverilog_expression(operand,
                    [](const semantic::CompiledDeclarationView& candidate) {
                        return candidate.systemverilog != nullptr
                            && (candidate.systemverilog->form == Form::net
                                || candidate.systemverilog->form
                                    == Form::variable
                                || candidate.systemverilog->form
                                    == Form::port);
                    })
                .unique();
        }
        // Only an array parameter or object itself, possibly
        // package-qualified, is queried here; a member path (`a[i].m.f`)
        // selects within it.
        const auto record = declaration
            ? specialization_.find_declaration(*declaration) : std::nullopt;
        const std::string_view text { view->systemverilog->text };
        // A subprogram's variable is read from its frame instead: only a
        // unit-level variable is queried by its declaration.
        const auto unit_level = [&](const semantic::ScopeId scope) {
            const auto& design = specialization_.design();
            const auto* found = design.find_scope(scope);
            const auto unit = found != nullptr
                ? design.find_unit(found->unit) : std::nullopt;
            return unit && unit->identity->scope == scope;
        };
        const bool object = record && record->systemverilog != nullptr
            && (record->systemverilog->form == Form::net
                || record->systemverilog->form == Form::port
                || (record->systemverilog->form == Form::variable
                    && unit_level(record->systemverilog->scope)));
        if (!record || record->systemverilog == nullptr
            || !(record->systemverilog->array_parameter
                || record->systemverilog->array_local_parameter
                || record->systemverilog->form == Form::parameter
                || record->systemverilog->form == Form::local_parameter
                || object)
            || !(text == record->systemverilog->name
                || (text.ends_with(record->systemverilog->name)
                    && text.substr(0U,
                           text.size() - record->systemverilog->name.size())
                        .ends_with("::")))) {
            return std::nullopt;
        }
        const auto& type = declaration_type(*declaration);
        if (!type || (type->unpacked_dimensions.empty() && !object)
            || (type->container_form
                && *type->container_form
                    != semantic::sv::TypeForm::static_array)) {
            return std::nullopt;
        }
        const auto bound = [&](const std::optional<std::int64_t> value,
                               const std::optional<semantic::ExpressionId>
                                   expression) -> std::optional<std::int64_t> {
            if (value || !expression) {
                return value;
            }
            const auto evaluated = evaluate(*expression);
            return evaluated ? evaluated->integer_value() : std::nullopt;
        };
        ArrayQueryDimensions result;
        const auto append = [&](const semantic::sv::PackedRange& range) {
            const auto left = bound(range.left, range.left_expression);
            const auto right = bound(range.right, range.right_expression);
            if (!left || !right) {
                return false;
            }
            result.ranges.emplace_back(*left, *right);
            return true;
        };
        for (const auto& range : type->unpacked_dimensions) {
            if (!append(range)) {
                return std::nullopt;
            }
        }
        result.unpacked = result.ranges.size();
        auto element = *type;
        element.unpacked_dimensions.clear();
        element.container_form.reset();
        if (!element.packed_dimensions.empty()) {
            for (const auto& range : element.packed_dimensions) {
                if (!append(range)) {
                    return std::nullopt;
                }
            }
        } else if (element.packed_range) {
            if (!append(*element.packed_range)) {
                return std::nullopt;
            }
        } else {
            // An integral element without declared ranges (`int`, a packed
            // structure) is one dimension of its width (7.4.1).
            std::unordered_set<std::uint32_t> visiting;
            const auto width
                = resolved_type_width(element, &specialization_, visiting);
            const auto form = element.value_form;
            if (width && *width != 0U
                && (form == semantic::sv::TypeForm::packed_integral
                    || form == semantic::sv::TypeForm::packed_structure
                    || form == semantic::sv::TypeForm::packed_union
                    || form == semantic::sv::TypeForm::enumeration)) {
                result.ranges.emplace_back(
                    static_cast<std::int64_t>(*width) - 1, 0);
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<Value> evaluate_array_query(
        const semantic::sv::Expression& record,
        const ArrayQueryDimensions& dimensions)
    {
        const auto known = [](const std::int64_t value) {
            return make_known(static_cast<std::uint64_t>(value), 32U, true,
                false, frontend::ValueDomain::Integer);
        };
        if (record.text == "$dimensions") {
            return known(static_cast<std::int64_t>(dimensions.ranges.size()));
        }
        if (record.text == "$unpacked_dimensions") {
            return known(static_cast<std::int64_t>(dimensions.unpacked));
        }
        std::uint64_t selected { 1U };
        if (record.operands.size() == 2U) {
            const auto dimension = evaluate(record.operands.back());
            const auto count = dimension
                ? nonnegative_count(*dimension, error_) : std::nullopt;
            if (!count) {
                return std::nullopt;
            }
            selected = *count;
        }
        if (selected == 0U || selected > dimensions.ranges.size()) {
            error_ = record.text + " dimension "
                + std::to_string(selected) + " is out of range";
            return std::nullopt;
        }
        const auto [left, right] = dimensions.ranges[selected - 1U];
        if (record.text == "$size") {
            return known(static_cast<std::int64_t>(
                support::index_distance(left, right) + 1U));
        }
        if (record.text == "$left") {
            return known(left);
        }
        if (record.text == "$right") {
            return known(right);
        }
        if (record.text == "$low") {
            return known(std::min(left, right));
        }
        if (record.text == "$high") {
            return known(std::max(left, right));
        }
        if (record.text == "$increment") {
            return known(left >= right ? 1 : -1);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_query(
        const semantic::sv::Expression& record)
    {
        if (record.operands.empty()) {
            return std::nullopt;
        }
        // `$bits(T)` of a type parameter takes its actual type (20.6.2).
        if (record.text == "$bits" && record.operands.size() == 1U) {
            const auto view = specialization_.find_expression(
                record.operands.front());
            const auto declaration_id = view && view->systemverilog != nullptr
                    && view->systemverilog->kind == ExpressionKind::name
                ? systemverilog_constant_name_declaration(
                      specialization_, *view->systemverilog)
                : std::nullopt;
            const auto declaration = declaration_id
                ? specialization_.find_declaration(*declaration_id)
                : std::nullopt;
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::type_parameter) {
                const auto& actuals
                    = specialization_.specialization().actual_identities;
                const auto actual = std::ranges::find(actuals,
                    *declaration_id,
                    &semantic::SpecializedHirActualIdentity::declaration);
                const auto type = actual != actuals.end()
                        && actual->systemverilog_type
                    ? actual->systemverilog_type
                    : declaration->systemverilog->type;
                if (type) {
                    std::unordered_set<std::uint32_t> visiting;
                    if (const auto width = resolved_type_width(
                            *type, &specialization_, visiting);
                        width && *width != 0U) {
                        return make_known(*width, 32U, true, false,
                            frontend::ValueDomain::Integer);
                    }
                }
            }
            // A typedef of a class body: its ranges read the class
            // frame's parameters (IEEE 1800-2023 8.25, 20.6.2).
            const auto typedef_declaration = !frames_.empty() && view
                    && view->systemverilog != nullptr
                    && view->systemverilog->kind == ExpressionKind::name
                    && view->systemverilog->referenced_name
                    && view->systemverilog->referenced_name->selected
                ? specialization_.find_declaration(
                      *view->systemverilog->referenced_name->selected)
                : std::nullopt;
            if (sv_const_trace()) {
                std::fprintf(stderr,
                    "FSIM_SV_CONST $bits operand kind=%d frames=%zu "
                    "selected=%d typedef=%d\n",
                    view && view->systemverilog != nullptr
                        ? static_cast<int>(view->systemverilog->kind) : -1,
                    frames_.size(),
                    view && view->systemverilog != nullptr
                        && view->systemverilog->referenced_name
                        && view->systemverilog->referenced_name->selected
                        ? 1 : 0,
                    typedef_declaration
                        && typedef_declaration->systemverilog != nullptr
                        ? static_cast<int>(
                              typedef_declaration->systemverilog->form)
                        : -1);
            }
            if (typedef_declaration
                && typedef_declaration->systemverilog != nullptr
                && typedef_declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::typedef_declaration
                && typedef_declaration->systemverilog->type) {
                const std::function<std::optional<std::int64_t>(
                    semantic::ExpressionId)>
                    bound = [this](const semantic::ExpressionId id)
                    -> std::optional<std::int64_t> {
                    const auto value = evaluate(id);
                    return value ? value->integer_value() : std::nullopt;
                };
                std::unordered_set<std::uint32_t> visiting;
                if (const auto width = resolved_type_width(
                        *typedef_declaration->systemverilog->type,
                        &specialization_, visiting, &bound);
                    width && *width != 0U) {
                    return make_known(*width, 32U, true, false,
                        frontend::ValueDomain::Integer);
                }
            }
        }
        // $bits is determined by the expression's type, not by the width
        // of the value produced while evaluating it. This matters for enum
        // literal declarations whose initializer has a different width from
        // the literal's enum subtype (IEEE 1800-2017 20.6.2).
        if (record.text == "$bits" && record.operands.size() == 1U) {
            if (const auto width = expression_width(record.operands.front());
                width && *width != 0U) {
                return make_known(*width, 32U, true, false,
                    frontend::ValueDomain::Integer);
            }
        }
        if (record.text != "$bits") {
            if (const auto dimensions
                = array_query_dimensions(record.operands.front())) {
                return evaluate_array_query(record, *dimensions);
            }
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

    /// The effective type of a declaration in this specialization, kept per
    /// specialization: the resolver reads no constant-function frame.
    [[nodiscard]] const std::optional<semantic::sv::TypeReference>&
    declaration_type(const semantic::DeclarationId declaration_id) const
    {
        auto& types = specialization_cache(specialization_)
                          .constants->declaration_types;
        if (const auto found = types.find(declaration_id.value());
            found != types.end()) {
            return found->second;
        }
        auto type = resolve_declaration_type(declaration_id);
        // Resolution can resolve other declarations; the entry stays put.
        return types.try_emplace(declaration_id.value(), std::move(type))
            .first->second;
    }

    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    resolve_declaration_type(const semantic::DeclarationId declaration_id) const
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
        if (declaration->systemverilog->form
                == semantic::sv::DeclarationForm::type_parameter
            && declaration->systemverilog->declared_type) {
            // A type parameter denotes its actual, or else its default
            // (6.20.3); the resolver selects it in this specialization.
            semantic::sv::TypeReference named;
            named.target.target = *declaration->systemverilog->declared_type;
            named.target.spelling = declaration->systemverilog->name;
            return resolver.underlying_systemverilog_type(
                       named, declaration->systemverilog->scope)
                .value_or(named);
        }
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
        const auto cast_value
            = semantic::sv::systemverilog_cast_value_operand(record);
        if (cast_value) {
            // An assignment pattern cast to an unpacked structure or array
            // type (`foo_t'{4, '{1, 2}}`) takes the type's layout: each item
            // converts to its member or element type, as the pattern of a
            // declaration initializer does (IEEE 1800-2017 6.24.1, 10.9.1,
            // 10.9.2). Evaluated on its own, the pattern would concatenate
            // the items at their self-determined widths.
            if (!semantic::sv::systemverilog_cast_size_operand(record)) {
                const auto operand = specialization_.find_expression(
                    *cast_value);
                const auto name = std::string_view { record.text }.substr(
                    std::string_view { "@sv-cast:" }.size());
                if (operand && operand->systemverilog != nullptr
                    && operand->systemverilog->kind
                        == ExpressionKind::assignment_pattern
                    && !name.empty()
                    && frontend::systemverilog_scalar_kind(name)
                        == ScalarKind::None
                    && !parse_integer<std::uint32_t>(name)) {
                    const auto pattern_type = named_cast_type(record, name);
                    const bool aggregate = pattern_type
                        && unpacked_aggregate(*pattern_type);
                    // FSIM_SV_CONST_CAST_TRACE names a typed pattern that
                    // does not take its aggregate type's layout.
                    const bool trace
                        = sv_const_cast_trace();
                    if (aggregate) {
                        if (auto image = evaluate_for_type(
                                *cast_value, *pattern_type)) {
                            return image;
                        }
                    }
                    if (trace) {
                        std::cerr << "FSIM_SV_CONST_CAST " << name
                                  << " type=" << pattern_type.has_value()
                                  << " aggregate=" << aggregate
                                  << " error=" << error_ << '\n';
                    }
                    error_.clear();
                }
            }
            auto result = evaluate(*cast_value);
            if (!result) {
                return std::nullopt;
            }
            std::optional<semantic::sv::TypeReference> type;
            const auto size_operand
                = semantic::sv::systemverilog_cast_size_operand(record);
            std::string_view cast_name;
            if (size_operand) {
                const auto integral_domain = [](
                    const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Logic4
                        || domain == frontend::ValueDomain::Boolean
                        || domain == frontend::ValueDomain::Integer;
                };
                if (!integral_domain(result->domain)) {
                    error_ = "SystemVerilog size cast operand must be "
                             "integral";
                    return std::nullopt;
                }
                if (!hir_systemverilog_constant_expression(
                        specialization_, *size_operand)) {
                    error_ = "SystemVerilog size cast width is not a "
                             "constant expression";
                    return std::nullopt;
                }
                const auto size_value = evaluate(*size_operand);
                const bool integral_width = size_value
                    && integral_domain(size_value->domain);
                const auto width = integral_width
                    ? nonnegative_count(*size_value, error_)
                    : std::nullopt;
                if (!width || *width == 0U
                    || *width > maximum_constant_width) {
                    error_ = "SystemVerilog size cast width must be a "
                             "positive constant integral value";
                    return std::nullopt;
                }
                type.emplace();
                type->executable_width = *width;
                type->four_state
                    = result->domain != frontend::ValueDomain::Bit2;
                type->signed_value = result->signed_value;
                type->target.spelling
                    = result->domain == frontend::ValueDomain::Integer
                    ? "int"
                    : std::to_string(*width);
                if (*width > 1U && result->unsized
                    && result->width == 1U) {
                    result->packed = PackedLogic4 {
                        static_cast<std::uint32_t>(*width),
                        result->packed.get(0U) };
                    result->width = static_cast<std::uint32_t>(*width);
                }
            } else {
                cast_name = std::string_view { record.text }.substr(
                    std::string_view { "@sv-cast:" }.size());
                // A cast to a real type converts the value; one to an
                // integral type rounds a real (IEEE 1800-2017 6.24.1).
                if (const auto kind = frontend::systemverilog_scalar_kind(
                        cast_name);
                    real_family(kind) || kind == ScalarKind::Chandle) {
                    return convert_value_kind(
                        std::move(*result), kind, error_);
                }
                if (cast_name == "time") {
                    type.emplace();
                    type->executable_width = 64U;
                    type->four_state = true;
                    type->target.spelling = "time";
                } else if (const auto width
                    = parse_integer<std::uint32_t>(cast_name)) {
                    // A size cast keeps the expression's signedness (6.24.1).
                    type.emplace();
                    type->executable_width = *width;
                    type->four_state = true;
                    type->signed_value = result->signed_value;
                    type->target.spelling = std::string { cast_name };
                } else if (cast_name == "bit" || cast_name == "logic"
                    || cast_name == "reg") {
                    type.emplace();
                    type->executable_width = 1U;
                    type->four_state = cast_name != "bit";
                    type->target.spelling = std::string { cast_name };
                } else if (cast_name == "byte" || cast_name == "shortint"
                    || cast_name == "int" || cast_name == "longint"
                    || cast_name == "integer") {
                    type.emplace();
                    type->executable_width = cast_name == "byte" ? 8U
                        : cast_name == "shortint" ? 16U
                        : cast_name == "longint" ? 64U : 32U;
                    type->signed_value = true;
                    type->four_state = cast_name == "integer";
                    type->target.spelling = std::string { cast_name };
                } else if (cast_name == "@type"
                    && !record.type_operand.empty()) {
                    // `type(...)'(x)` (6.23).
                    type = semantic::CompiledDesignResolver {
                        specialization_
                    }.underlying_systemverilog_type(
                         record.type_operand.front(), record.scope);
                } else {
                    type = named_cast_type(record, cast_name);
                }
                // `W'(v)` with W a constant parameter is a size cast
                // (IEEE 1800-2023 6.24.1).
                if (!type) {
                    const auto size_constant
                        = [](const semantic::CompiledDeclarationView& view) {
                              return view.systemverilog != nullptr
                                  && semantic::sv::
                                      is_size_constant_declaration(
                                          *view.systemverilog);
                          };
                    const auto selected
                        = semantic::CompiledDesignResolver { specialization_ }
                              .resolve_systemverilog(cast_name, record.scope,
                                  size_constant, false)
                              .unique();
                    const auto size = selected
                        ? value_for_declaration(*selected) : std::nullopt;
                    const auto width = size ? size->integer_value()
                                            : std::nullopt;
                    if (width && *width > 0
                        && static_cast<std::uint64_t>(*width)
                            <= maximum_constant_width) {
                        type.emplace();
                        type->executable_width
                            = static_cast<std::uint64_t>(*width);
                        type->four_state = true;
                        type->signed_value = result->signed_value;
                        type->target.spelling = std::to_string(*width);
                    }
                }
            }
            if (!type) {
                error_ = cast_name.empty()
                    ? "SystemVerilog size cast width must be a positive "
                      "constant integral value"
                    : "cannot resolve SystemVerilog cast type '"
                          + std::string { cast_name } + "'";
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
            // IEEE 1800-2017 20.8.1 requires the argument to be treated as
            // an unsigned value, irrespective of the operand's signedness.
            const auto value = unsigned_integer(*operand);
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
        if (record.text == "?:" && record.operands.size() == 3U
            && (real_valued(record.operands[1])
                || real_valued(record.operands[2]))) {
            // A real operand makes the result real (IEEE 1800-2017
            // 11.4.11); an ambiguous condition with differing reals gives 0.
            const auto condition = evaluate(record.operands.front());
            if (!condition) {
                return std::nullopt;
            }
            const auto state = truth(*condition);
            auto when_true = state != Truth::false_value
                ? evaluate(record.operands[1])
                : std::optional<Value> { };
            auto when_false = state != Truth::true_value
                ? evaluate(record.operands[2])
                : std::optional<Value> { };
            if ((state != Truth::false_value && !when_true)
                || (state != Truth::true_value && !when_false)) {
                return std::nullopt;
            }
            const auto kind = promote_scalar_kind(
                when_true ? value_kind(*when_true) : ScalarKind::None,
                when_false ? value_kind(*when_false) : ScalarKind::None);
            const auto result_kind = real_family(kind) ? kind
                                                       : ScalarKind::Real;
            if (state == Truth::unknown) {
                const auto lhs = real_of(*when_true);
                const auto rhs = real_of(*when_false);
                if (!lhs || !rhs) {
                    error_ = "real conditional operand is not numeric";
                    return std::nullopt;
                }
                return real_value(*lhs == *rhs ? *lhs : 0.0, result_kind);
            }
            return convert_value_kind(
                std::move(state == Truth::true_value ? *when_true
                                                     : *when_false),
                result_kind, error_);
        }
        // The real math functions are constant system functions (IEEE
        // 1800-2017 11.2.1, 20.8.2); their results follow IEEE 754.
        if (const auto function = real_math_function(record.text);
            function && record.operands.size() == function->arguments) {
            std::array<double, 2U> arguments { };
            for (std::size_t index { }; index < function->arguments;
                ++index) {
                const auto operand = evaluate(record.operands[index]);
                const auto real = operand ? real_of(*operand)
                                          : std::optional<double> { };
                if (!real) {
                    if (operand) {
                        error_ = "a math function argument is not numeric";
                    }
                    return std::nullopt;
                }
                arguments[index] = *real;
            }
            return real_value(function->evaluate(arguments[0], arguments[1]),
                ScalarKind::Real);
        }
        if (record.text == "?:" && record.operands.size() == 3U) {
            const auto condition = evaluate(record.operands.front());
            if (!condition) {
                return std::nullopt;
            }
            const auto state = truth(*condition);
            if (state != Truth::unknown) {
                const auto when_true_width = expression_width(
                    record.operands[1]);
                const auto when_false_width = expression_width(
                    record.operands[2]);
                // The selected operand takes the expression's context
                // width (IEEE 1800-2017 11.6.1).
                const auto selected = record.operands[
                    state == Truth::true_value ? 1U : 2U];
                const auto outer = take_context(record.id);
                if (when_true_width && when_false_width) {
                    auto width = std::max(*when_true_width, *when_false_width);
                    if (outer) {
                        width = std::max(width, *outer);
                    }
                    contexts_[selected.value()] = width;
                }
                auto result = evaluate(selected);
                contexts_.erase(selected.value());
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
        // A static method of a class specialization (`C#(7)::f()`) runs
        // with the selection's parameters (IEEE 1800-2017 8.25.1, 13.4.3).
        if (constexpr std::string_view prefix { "@sv-static-method:" };
            std::string_view { record.text }.starts_with(prefix)) {
            const auto identity
                = std::string_view { record.text }.substr(prefix.size());
            for (const auto& candidate :
                specialization_.design().systemverilog_hir.classes()) {
                if (candidate.forward_declaration) continue;
                const auto method = std::ranges::find(candidate.methods,
                    identity, &semantic::sv::ClassMethod::canonical_identity);
                if (method == candidate.methods.end()) continue;
                const auto declaration
                    = specialization_.find_declaration(method->declaration);
                if (!method->static_method || !declaration
                    || declaration->systemverilog == nullptr
                    || declaration->systemverilog->form
                        != semantic::sv::DeclarationForm::function) {
                    break;
                }
                const auto* selection = record.class_owner.empty()
                    ? nullptr : &record.class_owner.front();
                if (!push_class_frame(candidate, selection)) {
                    return std::nullopt;
                }
                struct Pop {
                    std::vector<Frame>& frames;
                    ~Pop() { frames.pop_back(); }
                } pop { frames_ };
                return evaluate_callable(*declaration->systemverilog, record);
            }
            error_ = "unsupported SystemVerilog static method constant call '"
                + std::string { identity } + "'";
            return std::nullopt;
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
        // Inside a function body its name also denotes the result
        // variable; a call names the function itself (IEEE 1800-2017
        // 13.4.1), so a recursive call resolves to the function.
        if (!record.text.starts_with("$") && !record.text.starts_with("@")) {
            const semantic::CompiledDeclarationPredicate function
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.systemverilog != nullptr
                          && candidate.systemverilog->form
                              == semantic::sv::DeclarationForm::function
                          && candidate.systemverilog->callable
                          && candidate.systemverilog->callable->function;
                  };
            const auto resolved = semantic::CompiledDesignResolver {
                specialization_ }
                                      .resolve_systemverilog(record.text,
                                          record.scope, function, false);
            const auto selected = resolved.unique();
            const auto declaration = selected
                ? specialization_.find_declaration(*selected)
                : std::nullopt;
            if (declaration && declaration->systemverilog != nullptr) {
                return evaluate_callable(*declaration->systemverilog, record);
            }
            const auto visible_name
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_systemverilog(
                          record.text, record.scope, { }, false);
            const bool class_member_call
                = !record.class_identity.empty()
                || !record.class_member_identity.empty();
            if (!selected && !class_member_call
                && resolved.status
                    == semantic::CompiledResolutionStatus::not_found
                && visible_name.status
                    == semantic::CompiledResolutionStatus::not_found) {
                error_ = "unresolved SystemVerilog function call '"
                    + record.text + "'";
                return std::nullopt;
            }
        }
        // A member of a packed structure array element constant
        // (`P[1].m`, IEEE 1800-2023 7.2.1, 7.4.1).
        if (record.text.starts_with("@sv-select:")
            && record.operands.size() == 1U) {
            const auto base = specialization_.find_expression(
                record.operands.front());
            if (base && base->systemverilog != nullptr
                && base->systemverilog->kind == ExpressionKind::index) {
                if (auto member = typed_constant(record.id)) {
                    return std::move(member->first);
                }
                error_.clear();
            }
        }
        // A member of a packed structure class constant (`C::p.a.b`, IEEE
        // 1800-2023 7.2.1, 8.25.1).
        if (record.text.starts_with("@sv-select:")
            && record.operands.size() == 1U) {
            std::string path = record.text.substr(11U);
            auto base = specialization_.find_expression(
                record.operands.front());
            for (std::size_t depth { }; depth < 64U && base
                 && base->systemverilog != nullptr
                 && base->systemverilog->kind == ExpressionKind::call
                 && base->systemverilog->text.starts_with("@sv-select:")
                 && base->systemverilog->operands.size() == 1U;
                 ++depth) {
                path = base->systemverilog->text.substr(11U) + "." + path;
                base = specialization_.find_expression(
                    base->systemverilog->operands.front());
            }
            if (base && base->systemverilog != nullptr
                && (base->systemverilog->kind
                        == ExpressionKind::class_static_property
                    || (base->systemverilog->kind == ExpressionKind::call
                        && base->systemverilog->text.starts_with(
                            "@sv-static-property:")))) {
                semantic::sv::TypeReference type;
                const auto value
                    = evaluate_class_parameter(*base->systemverilog, &type);
                const auto slice = value ? member_slice(type, path)
                                         : std::nullopt;
                if (!slice) {
                    if (value) {
                        error_ = "cannot select member '" + path
                            + "' of a class constant";
                    }
                    return std::nullopt;
                }
                runtime::PackedLogic4 member {
                    static_cast<std::size_t>(slice->second), Logic4::x };
                for (std::uint64_t bit = 0U; bit < slice->second; ++bit) {
                    if (slice->first + bit < value->packed.width()) {
                        member.set(bit, value->packed.get(slice->first + bit));
                    }
                }
                Value selected_member;
                selected_member.packed = std::move(member);
                selected_member.width
                    = static_cast<std::uint32_t>(slice->second);
                selected_member.domain = value->domain;
                return selected_member;
            }
        }
        // A parameter of an element of an interface port array
        // (`p[0].FOO`) reads the identity that element publishes (IEEE
        // 1800-2017 25.5, 25.10).
        if (record.text.starts_with("@sv-select:")
            && record.operands.size() == 1U) {
            std::function<std::optional<std::string>(semantic::ExpressionId)>
                path = [&](const semantic::ExpressionId id)
                -> std::optional<std::string> {
                const auto view = specialization_.find_expression(id);
                if (!view || view->systemverilog == nullptr) {
                    return std::nullopt;
                }
                const auto& node = *view->systemverilog;
                if (node.kind == ExpressionKind::name) {
                    return node.text;
                }
                if (node.kind == ExpressionKind::index
                    && node.operands.size() == 2U) {
                    const auto base = path(node.operands.front());
                    const auto index = base
                        ? evaluate(node.operands.back())
                        : std::nullopt;
                    const auto integer = index ? index->integer_value()
                                               : std::nullopt;
                    return integer ? std::optional { *base + "["
                                         + std::to_string(*integer) + "]" }
                                   : std::nullopt;
                }
                if (node.kind == ExpressionKind::call
                    && node.text.starts_with("@sv-select:")
                    && node.operands.size() == 1U) {
                    const auto base = path(node.operands.front());
                    return base ? std::optional { *base + "."
                                      + node.text.substr(11U) }
                                : std::nullopt;
                }
                return std::nullopt;
            };
            const auto& identities
                = specialization_.specialization().hierarchy_identities;
            if (auto name = path(record.operands.front())) {
                *name += "." + record.text.substr(11U);
                const auto hierarchy = std::ranges::find(identities, *name,
                    &semantic::SpecializedHirNamedIdentity::name);
                if (hierarchy != identities.end()) {
                    ++hierarchy_reads_;
                    if (const auto parsed
                        = parse_canonical(hierarchy->identity)) {
                        return parsed;
                    }
                }
            }
        }
        error_ = "unsupported SystemVerilog integral constant call '"
            + record.text + "'";
        return std::nullopt;
    }

    /// The unpacked structure or array type an explicitly typed assignment
    /// pattern (`foo_t'{...}`) names (IEEE 1800-2017 10.9.1, 10.9.2).
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    typed_pattern_aggregate(const semantic::sv::Expression& record)
    {
        if (record.kind != ExpressionKind::assignment_pattern
            || record.nominal_type.empty()) {
            return std::nullopt;
        }
        std::optional<semantic::sv::TypeReference> type;
        if (record.nominal_type_target
            && record.nominal_type_target->valid()) {
            type.emplace();
            type->target.target = *record.nominal_type_target;
            type->target.spelling = record.nominal_type;
        } else if (const auto selected
            = semantic::CompiledDesignResolver { specialization_ }
                  .resolve_systemverilog_named_type(
                      record.nominal_type, record.scope)
                  .unique()) {
            type = declaration_type(*selected);
        }
        if (!type || !unpacked_aggregate(*type)) {
            return std::nullopt;
        }
        return type;
    }

    [[nodiscard]] std::optional<Value> evaluate_pattern(
        const semantic::sv::Expression& record)
    {
        if (const auto type = typed_pattern_aggregate(record)) {
            return evaluate_unpacked_pattern(record, *type);
        }
        if (!record.nominal_type.empty()
            && sv_const_cast_trace()) {
            std::cerr << "FSIM_SV_CONST_CAST pattern " << record.nominal_type
                      << " target=" << record.nominal_type_target.has_value()
                      << '\n';
        }
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
        const semantic::sv::Expression& call,
        LocalUnpackedArray* const array_result = nullptr)
    {
        if (!callable.callable || frames_.size() >= maximum_constant_call_depth) {
            error_ = "constant function recursion exceeds the call-depth limit";
            return std::nullopt;
        }
        // A real result or argument is outside integral evaluation.
        const auto real_spelling = [](const std::string_view spelling) {
            return spelling == "real" || spelling == "realtime"
                || spelling == "shortreal";
        };
        if (real_spelling(callable.callable->return_type.target.spelling)
            || std::ranges::any_of(callable.callable->formals,
                [&](const semantic::DeclarationId formal) {
                    const auto view = specialization_.find_declaration(formal);
                    return view && view->systemverilog != nullptr
                        && view->systemverilog->type
                        && real_spelling(
                            view->systemverilog->type->target.spelling);
                })) {
            error_ = "a real constant function is outside integral "
                     "constant evaluation";
            return std::nullopt;
        }
        Frame frame;
        frame.callable = callable.id;
        const auto& formals = callable.callable->formals;
        // Bind actuals to formals by position, then by name (13.5.4).
        std::vector<std::optional<semantic::ExpressionId>> actuals(
            formals.size());
        std::size_t positional { };
        for (std::size_t operand = 0U; operand < call.operands.size();
            ++operand) {
            const auto name = operand < call.argument_names.size()
                ? std::string_view { call.argument_names[operand] }
                : std::string_view { };
            std::size_t target = positional;
            if (!name.empty()) {
                target = formals.size();
                for (std::size_t index = 0U; index < formals.size();
                    ++index) {
                    const auto formal
                        = specialization_.find_declaration(formals[index]);
                    if (formal && formal->systemverilog != nullptr
                        && formal->systemverilog->name == name) {
                        target = index;
                        break;
                    }
                }
            } else {
                ++positional;
            }
            if (target >= formals.size() || actuals[target]) {
                error_ = "constant function actual does not match a formal";
                return std::nullopt;
            }
            actuals[target] = call.operands[operand];
        }
        // An unpacked array formal copies its actual's elements (13.5.1).
        std::vector<std::pair<semantic::DeclarationId, LocalUnpackedArray>>
            array_formals;
        std::map<std::uint32_t, semantic::sv::TypeReference>
            dynamic_array_types;
        for (std::size_t index = 0U; index < formals.size(); ++index) {
            const auto formal = specialization_.find_declaration(formals[index]);
            auto array_type = formal && formal->systemverilog != nullptr
                    && formal->systemverilog->type && actuals[index]
                ? unpacked_array_type(*formal->systemverilog->type,
                      formal->systemverilog->scope)
                : std::nullopt;
            // A dynamic array or queue formal holds a copy of its actual,
            // indexed from 0 at the actual's left element (7.6, 7.10).
            const bool dynamic_formal = !array_type && formal
                && formal->systemverilog != nullptr
                && formal->systemverilog->type && actuals[index]
                && (formal->systemverilog->type->container_form
                        == semantic::sv::TypeForm::dynamic_array
                    || formal->systemverilog->type->container_form
                        == semantic::sv::TypeForm::queue);
            if (array_type || dynamic_formal) {
                auto actual = evaluate_array(*actuals[index]);
                if (actual && dynamic_formal) {
                    auto fixed = *formal->systemverilog->type;
                    fixed.container_form = semantic::sv::TypeForm::static_array;
                    fixed.container_element_types.clear();
                    fixed.queue_maximum.reset();
                    semantic::sv::PackedRange bounds;
                    bounds.left = 0;
                    bounds.right = static_cast<std::int64_t>(
                        actual->elements.size()) - 1;
                    fixed.unpacked_dimensions = { bounds };
                    array_type = fixed;
                    dynamic_array_types.emplace(
                        formals[index].value(), std::move(fixed));
                }
                if (!actual) {
                    if (error_.empty()) {
                        error_ = "cannot evaluate constant function array "
                                 "argument";
                    }
                    return std::nullopt;
                }
                array_formals.emplace_back(
                    formals[index], std::move(*actual));
                continue;
            }
            std::optional<Value> value;
            if (actuals[index]) {
                value = evaluate(*actuals[index]);
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
                value = convert_record_type(
                    std::move(*value), *formal->systemverilog->type);
                if (!value) {
                    return std::nullopt;
                }
            }
            frame.values.emplace(formals[index], std::move(*value));
        }
        // A constant function reads only its arguments, parameters and other
        // constant items (IEEE 1800-2023 13.4.3), so a call with the same
        // converted arguments that read no hierarchy identity has the same
        // result in every occurrence of this specialization. Its recorded
        // work is charged again, so the work limit is reached as before.
        // Array arguments and results, recursion and class contexts are
        // evaluated each time.
        std::string memo_key;
        if (array_formals.empty() && array_result == nullptr
            && std::ranges::none_of(frames_, [&](const Frame& active) {
                   return active.callable == callable.id
                       || active.class_owner != nullptr;
               })) {
            memo_key = call_memo_key(callable.id, formals, frame.values);
            const auto& calls = *specialization_cache(specialization_)
                                     .constants->unit_calls;
            if (const auto found = calls.find(memo_key); found != calls.end()) {
                if (!consume_work(found->second.work)) {
                    return std::nullopt;
                }
                return found->second.value;
            }
        }
        const auto reads_before = hierarchy_reads_;
        const auto work_before = work_units_used_;
        const auto error_before = error_;
        auto result = make_known(0U, 32U, true, false);
        if (callable.callable->return_type.executable_width) {
            result = *convert_record_type(
                std::move(result), callable.callable->return_type);
        }
        // A 4-state result variable starts unknown (IEEE 1800-2017 6.8
        // Table 6-7), so a body that never assigns it yields x.
        if (callable.callable->return_type.four_state
            && (result.domain == frontend::ValueDomain::Logic4
                || result.domain == frontend::ValueDomain::Integer)) {
            result.packed = PackedLogic4 { result.width, Logic4::x };
        }
        frame.values.emplace(callable.id, result);
        frames_.push_back(std::move(frame));
        for (auto& [formal, actual] : array_formals) {
            const auto view = specialization_.find_declaration(formal);
            const auto dynamic = dynamic_array_types.find(formal.value());
            const auto formal_type = dynamic != dynamic_array_types.end()
                ? std::optional { dynamic->second }
                : unpacked_array_type(*view->systemverilog->type,
                      view->systemverilog->scope);
            if (!formal_type || actual.elements.empty()
                || !create_local_array(formal, *formal_type)
                || !copy_array(frames_.back().arrays.at(formal), actual)) {
                frames_.pop_back();
                return std::nullopt;
            }
        }
        // An unpacked array result is the function's own local array.
        const auto result_array_type = unpacked_array_type(
            callable.callable->return_type, callable.scope);
        if (result_array_type
            && (array_result == nullptr
                || !create_local_array(callable.id, *result_array_type))) {
            if (array_result == nullptr) {
                error_ = "an unpacked array function result is not a scalar "
                         "constant";
            }
            frames_.pop_back();
            return std::nullopt;
        }
        if (callable.nested_scope) {
            for (const auto child_id : callable.children) {
                const auto child
                    = specialization_.find_declaration(child_id);
                if (!child || child->systemverilog == nullptr
                    || child->systemverilog->scope != *callable.nested_scope
                    || child->systemverilog->initializer) {
                    continue;
                }
                if (!initialize_local_array(
                        child_id, *child->systemverilog)) {
                    frames_.pop_back();
                    return std::nullopt;
                }
                // An uninitialized packed local holds its type's default,
                // so a member write (`s.m = v`) updates it in place
                // (IEEE 1800-2017 6.8, 13.4.4).
                const auto& local = *child->systemverilog;
                if (local.form == semantic::sv::DeclarationForm::variable
                    && local.type && !local.type->container_form
                    && local.type->unpacked_dimensions.empty()
                    && !frames_.back().arrays.contains(child_id)
                    && !frames_.back().values.contains(child_id)) {
                    std::unordered_set<std::uint32_t> visiting;
                    if (const auto width = resolved_type_width(
                            *local.type, &specialization_, visiting);
                        width && *width != 0U
                        && *width <= maximum_constant_width) {
                        auto initial = local.type->four_state
                            ? make_unknown(
                                  static_cast<std::uint32_t>(*width),
                                  local.type->signed_value)
                            : make_known(0U,
                                  static_cast<std::uint32_t>(*width),
                                  local.type->signed_value, false);
                        // The declared range indexes its selects (`[2:1]`,
                        // IEEE 1800-2017 7.4.6).
                        auto typed = convert_record_type(
                            initial, *local.type);
                        if (typed && local.type->four_state) {
                            typed->packed = initial.packed;
                        }
                        frames_.back().values.emplace(child_id,
                            typed ? std::move(*typed) : std::move(initial));
                    }
                }
            }
        }
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
        if (result_array_type) {
            *array_result = std::move(frames_.back().arrays.at(callable.id));
            frames_.pop_back();
            return make_known(0U, 1U, false, false);
        }
        auto value = frames_.back().result.value_or(
            frames_.back().values.at(callable.id));
        frames_.pop_back();
        auto converted = convert_record_type(
            std::move(value), callable.callable->return_type);
        if (converted && !memo_key.empty() && hierarchy_reads_ == reads_before
            && error_ == error_before) {
            specialization_cache(specialization_).constants->unit_calls->emplace(
                std::move(memo_key),
                SpecializationConstants::CallResult {
                    *converted, work_units_used_ - work_before });
        }
        return converted;
    }

    /// The callable and its converted argument values, in formal order.
    [[nodiscard]] static std::string call_memo_key(
        const semantic::DeclarationId callable,
        const std::vector<semantic::DeclarationId>& formals,
        const std::map<semantic::DeclarationId, Value>& values)
    {
        std::string key = std::to_string(callable.value());
        for (const auto formal : formals) {
            const auto found = values.find(formal);
            key += '|';
            if (found == values.end()) {
                continue;
            }
            key += found->second.canonical();
            // The packed range is not part of the canonical identity; every
            // field of it can reach the body (selects, their signedness).
            if (const auto& range = found->second.packed_range) {
                const auto bound = [&](const auto& value) {
                    key += value ? ':' + std::to_string(*value) : std::string { ":-" };
                };
                const auto expression = [&](const auto& value) {
                    key += value ? ':' + std::to_string(value->value())
                                 : std::string { ":-" };
                };
                key += ":r";
                bound(range->left);
                bound(range->right);
                expression(range->left_expression);
                expression(range->right_expression);
                key += ':' + std::to_string(range->source.value()) + ':'
                    + (range->descending ? 'd' : 'a')
                    + (range->element_signed ? 's' : 'u');
            }
        }
        return key;
    }

    /// Copy `source`'s elements, left to right, into `target`, converting
    /// each to the target element type (7.6).
    [[nodiscard]] bool copy_array(
        LocalUnpackedArray& target, const LocalUnpackedArray& source)
    {
        if (target.elements.size() != source.elements.size()) {
            error_ = "unpacked array constant sizes differ";
            return false;
        }
        for (std::size_t index = 0; index < source.elements.size(); ++index) {
            auto converted = convert_hir_systemverilog_constant(
                source.elements[index], target.element_type, error_,
                &specialization_);
            if (!converted) {
                return false;
            }
            target.elements[index] = std::move(*converted);
        }
        return true;
    }

    /// The value of an unpacked array expression (IEEE 1800-2017 7.4, 10.10,
    /// 13.4): a frame array, an array parameter, an array-valued constant
    /// function call, or a slice of one of these.
    [[nodiscard]] std::optional<LocalUnpackedArray> evaluate_array(
        const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::name && record.referenced_name
            && record.referenced_name->selected) {
            const auto declaration_id = *record.referenced_name->selected;
            if (const auto* local = find_local_array(declaration_id)) {
                return *local;
            }
            return array_parameter_value(declaration_id);
        }
        if (record.kind == ExpressionKind::call
            && !record.text.starts_with("$")
            && !record.text.starts_with("@")) {
            std::optional<semantic::DeclarationId> selected;
            if (record.referenced_name && record.referenced_name->selected) {
                selected = *record.referenced_name->selected;
            } else {
                const semantic::CompiledDeclarationPredicate function
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.systemverilog != nullptr
                              && candidate.systemverilog->callable
                              && candidate.systemverilog->callable->function;
                      };
                selected = semantic::CompiledDesignResolver { specialization_ }
                               .resolve_systemverilog(record.text,
                                   record.scope, function, false)
                               .unique();
            }
            const auto callable = selected
                ? specialization_.find_declaration(*selected)
                : std::nullopt;
            if (!callable || callable->systemverilog == nullptr
                || !callable->systemverilog->callable) {
                return std::nullopt;
            }
            LocalUnpackedArray result;
            if (!evaluate_callable(*callable->systemverilog, record, &result)) {
                return std::nullopt;
            }
            return result;
        }
        if (record.kind == ExpressionKind::slice
            && record.operands.size() == 3U) {
            auto base = evaluate_array(record.operands[0]);
            const auto first = base ? evaluate(record.operands[1])
                                    : std::nullopt;
            const auto second = first ? evaluate(record.operands[2])
                                      : std::nullopt;
            const auto a = first ? first->integer_value() : std::nullopt;
            const auto b = second ? second->integer_value() : std::nullopt;
            if (!a || !b) {
                return std::nullopt;
            }
            // A part-select keeps the base's direction (7.4.6).
            const bool ascending = base->left <= base->right;
            std::int64_t left = *a;
            std::int64_t right = *b;
            if (record.text == "+:" || record.text == "-:") {
                if (*b <= 0) {
                    return std::nullopt;
                }
                const auto low = record.text == "+:" ? *a : *a - *b + 1;
                const auto high = low + *b - 1;
                left = ascending ? low : high;
                right = ascending ? high : low;
            }
            LocalUnpackedArray result;
            result.left = left;
            result.right = right;
            result.element_type = base->element_type;
            result.default_value = base->default_value;
            const auto step = left <= right ? 1 : -1;
            for (auto index = left;; index += step) {
                const auto offset = local_array_offset(*base, index);
                if (!offset) {
                    return std::nullopt;
                }
                result.elements.push_back(base->elements[*offset]);
                if (index == right) {
                    break;
                }
            }
            return result;
        }
        return std::nullopt;
    }

    /// The value of an unpacked array parameter: its instance actual, or its
    /// default from a positional pattern or an array expression (6.20.2).
    [[nodiscard]] std::optional<LocalUnpackedArray> array_parameter_value(
        const semantic::DeclarationId declaration_id)
    {
        const auto declaration
            = specialization_.find_declaration(declaration_id);
        // An unpacked array parameter or local parameter may be kept as a
        // constant container variable (6.20.2).
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type
            || (declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::parameter
                && declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::local_parameter
                && !declaration->systemverilog->array_parameter
                && !declaration->systemverilog->array_local_parameter)) {
            return std::nullopt;
        }
        const auto array_type = unpacked_array_type(
            *declaration->systemverilog->type,
            declaration->systemverilog->scope);
        if (!array_type) {
            return std::nullopt;
        }
        // The array is built in a scratch frame so that the frame-array
        // limits apply.
        Frame scratch;
        frames_.push_back(std::move(scratch));
        const bool created = create_local_array(declaration_id, *array_type);
        auto result = created
            ? std::optional { std::move(
                  frames_.back().arrays.at(declaration_id)) }
            : std::nullopt;
        frames_.pop_back();
        if (!result) {
            return std::nullopt;
        }
        if (const auto actual = declaration->systemverilog->array_parameter
                ? specialization_.systemverilog_array_elements(declaration_id)
                : std::nullopt) {
            if (actual->size() != result->elements.size()) {
                return std::nullopt;
            }
            for (std::size_t index = 0; index < actual->size(); ++index) {
                auto value = array_element_value((*actual)[index]);
                if (!value) {
                    return std::nullopt;
                }
                auto element = convert_hir_systemverilog_constant(
                    std::move(*value), result->element_type, error_,
                    &specialization_);
                if (!element) {
                    return std::nullopt;
                }
                result->elements[index] = std::move(*element);
            }
            return result;
        }
        const auto initializer = declaration->systemverilog->initializer;
        const auto pattern = initializer
            ? specialization_.find_expression(*initializer)
            : std::nullopt;
        if (!pattern || pattern->systemverilog == nullptr) {
            return std::nullopt;
        }
        if (pattern->systemverilog->kind == ExpressionKind::assignment_pattern) {
            const auto& associations = pattern->systemverilog->associations;
            std::optional<semantic::ExpressionId> fallback;
            std::vector<semantic::ExpressionId> positional;
            for (const auto& association : associations) {
                if (association.choice_spelling == "default") {
                    fallback = association.value;
                } else if (association.choice_spelling.empty()) {
                    positional.push_back(association.value);
                } else {
                    return std::nullopt;
                }
            }
            if (positional.size() != result->elements.size()
                && !(positional.empty() && fallback)) {
                return std::nullopt;
            }
            for (std::size_t index = 0; index < result->elements.size();
                 ++index) {
                auto element = evaluate(
                    positional.empty() ? *fallback : positional[index]);
                if (!element) {
                    return std::nullopt;
                }
                auto converted = convert_hir_systemverilog_constant(
                    std::move(*element), result->element_type, error_,
                    &specialization_);
                if (!converted) {
                    return std::nullopt;
                }
                result->elements[index] = std::move(*converted);
            }
            return result;
        }
        const auto source = evaluate_array(*initializer);
        if (!source || !copy_array(*result, *source)) {
            return std::nullopt;
        }
        return result;
    }

    /// A type of this specialization or, for a typedef from another unit
    /// such as a package, of the design.
    [[nodiscard]] std::optional<semantic::CompiledTypeView>
    find_design_type(const semantic::TypeId type) const
    {
        if (auto found = specialization_.find_type(type)) {
            return found;
        }
        return specialization_.design().find_type(type);
    }

    /// The bit offset of `members[index]` within its packed aggregate:
    /// a structure's later members occupy its lower bits (IEEE 1800-2017
    /// 7.2.1). A member width the parser could not fold (`logic [N-1:0]`
    /// from a package parameter) resolves here.
    [[nodiscard]] std::uint64_t packed_member_offset(
        const std::vector<semantic::sv::PackedMember>& members,
        const std::size_t index, const bool union_form) const
    {
        if (union_form) {
            return 0U;
        }
        std::uint64_t offset { };
        for (auto later = index + 1U; later < members.size(); ++later) {
            std::unordered_set<std::uint32_t> visiting;
            const auto width = resolved_type_width(
                members[later].type, &specialization_, visiting);
            if (!width) {
                return members[index].lsb_offset;
            }
            offset += *width;
        }
        return offset;
    }

    /// The members of a packed structure or union type, or null.
    [[nodiscard]] const std::vector<semantic::sv::PackedMember>*
    packed_members(semantic::sv::TypeReference type,
        bool* union_form = nullptr) const
    {
        if (const auto effective
            = semantic::CompiledDesignResolver { specialization_ }
                  .effective_systemverilog_type(
                      type, specialization_.scope())) {
            type = *effective;
        }
        auto definition = type.target.target.valid()
            ? find_design_type(type.target.target)
            : std::nullopt;
        // A typedef of a package type (`typedef pkg::S t;`) names that type
        // in the typedef's own scope.
        for (std::size_t depth { }; depth < 8U && definition
             && definition->systemverilog != nullptr
             && definition->systemverilog->form
                 == semantic::sv::TypeForm::alias;
             ++depth) {
            const auto alias = specialization_.find_declaration(
                definition->systemverilog->declaration);
            auto base = definition->systemverilog->base;
            if (const auto effective
                = semantic::CompiledDesignResolver { specialization_ }
                      .effective_systemverilog_type(base,
                          alias && alias->systemverilog != nullptr
                              ? alias->systemverilog->scope
                              : specialization_.scope())) {
                base = *effective;
            }
            definition = base.target.target.valid()
                ? find_design_type(base.target.target)
                : std::nullopt;
        }
        if (!definition || definition->systemverilog == nullptr
            || (definition->systemverilog->form
                    != semantic::sv::TypeForm::packed_structure
                && definition->systemverilog->form
                    != semantic::sv::TypeForm::packed_union)) {
            return nullptr;
        }
        if (union_form != nullptr) {
            *union_form = definition->systemverilog->form
                == semantic::sv::TypeForm::packed_union;
        }
        return &definition->systemverilog->members;
    }

    /// A type through its typedefs and type parameters (IEEE 1800-2017
    /// 6.18, 6.20.3).
    [[nodiscard]] semantic::sv::TypeReference effective_aggregate_type(
        semantic::sv::TypeReference type) const
    {
        const semantic::CompiledDesignResolver resolver { specialization_ };
        if (const auto effective = resolver.effective_systemverilog_type(
                type, specialization_.scope())) {
            type = *effective;
        }
        if (const auto underlying = resolver.underlying_systemverilog_type(
                type, specialization_.scope())) {
            type = *underlying;
        }
        return type;
    }

    /// The members of a structure or packed union whose value is a bit
    /// stream (IEEE 1800-2017 6.24.3, 7.2, 7.3), or null.
    [[nodiscard]] const std::vector<semantic::sv::PackedMember>*
    aggregate_members(const semantic::sv::TypeReference& input,
        bool* const union_form = nullptr) const
    {
        if (const auto* const packed = packed_members(input, union_form)) {
            return packed;
        }
        const auto type = effective_aggregate_type(input);
        const auto definition = type.target.target.valid()
            ? find_design_type(type.target.target)
            : std::nullopt;
        if (!definition || definition->systemverilog == nullptr) {
            return nullptr;
        }
        const auto form = definition->systemverilog->form;
        if (form != semantic::sv::TypeForm::packed_structure
            && form != semantic::sv::TypeForm::packed_union
            && form != semantic::sv::TypeForm::unpacked_structure) {
            return nullptr;
        }
        if (union_form != nullptr) {
            *union_form = form == semantic::sv::TypeForm::packed_union;
        }
        return &definition->systemverilog->members;
    }

    [[nodiscard]] std::optional<std::int64_t> dimension_bound(
        const std::optional<std::int64_t> value,
        const std::optional<semantic::ExpressionId> expression)
    {
        if (value) {
            return value;
        }
        if (!expression) {
            return std::nullopt;
        }
        if (const auto known
            = specialization_.evaluate_integral_expression(*expression)) {
            return known;
        }
        const auto evaluated = evaluate(*expression);
        return evaluated ? evaluated->integer_value() : std::nullopt;
    }

    /// The fixed unpacked dimensions of a type, outermost first.
    [[nodiscard]] std::optional<std::vector<std::pair<std::int64_t,
        std::int64_t>>>
    unpacked_bounds(const semantic::sv::TypeReference& type)
    {
        if (type.container_form
            && *type.container_form != semantic::sv::TypeForm::static_array) {
            return std::nullopt;
        }
        std::vector<std::pair<std::int64_t, std::int64_t>> bounds;
        for (const auto& dimension : type.unpacked_dimensions) {
            const auto left
                = dimension_bound(dimension.left, dimension.left_expression);
            const auto right
                = dimension_bound(dimension.right, dimension.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            bounds.emplace_back(*left, *right);
        }
        return bounds;
    }

    /// A type with unpacked dimensions, through a typedef of an array.
    [[nodiscard]] semantic::sv::TypeReference with_unpacked_dimensions(
        semantic::sv::TypeReference type) const
    {
        if (type.unpacked_dimensions.empty()) {
            const auto effective = effective_aggregate_type(type);
            if (!effective.unpacked_dimensions.empty()) {
                return effective;
            }
        }
        return type;
    }

    /// The bits of a fixed-size type's bit stream: a structure's members
    /// and an array's elements in order (IEEE 1800-2017 6.24.3).
    [[nodiscard]] std::optional<std::uint64_t> aggregate_width(
        const semantic::sv::TypeReference& input, const unsigned depth = 0U)
    {
        if (depth > 32U) {
            return std::nullopt;
        }
        const auto type = with_unpacked_dimensions(input);
        if (!type.unpacked_dimensions.empty()) {
            const auto bounds = unpacked_bounds(type);
            if (!bounds) {
                return std::nullopt;
            }
            auto element = type;
            element.unpacked_dimensions.clear();
            element.container_form.reset();
            auto width = aggregate_width(element, depth + 1U);
            for (const auto& [left, right] : *bounds) {
                const auto count = static_cast<std::uint64_t>(
                    std::max(left, right) - std::min(left, right)) + 1U;
                if (!width || *width == 0U
                    || *width > maximum_constant_width / count) {
                    return std::nullopt;
                }
                *width *= count;
            }
            return width;
        }
        bool union_form { };
        if (const auto* const members = aggregate_members(type, &union_form)) {
            std::uint64_t total { };
            for (const auto& member : *members) {
                const auto width = aggregate_width(member.type, depth + 1U);
                if (!width || *width == 0U
                    || *width > maximum_constant_width - total) {
                    return std::nullopt;
                }
                total = union_form ? std::max(total, *width) : total + *width;
            }
            return total;
        }
        std::unordered_set<std::uint32_t> visiting;
        return resolved_type_width(type, &specialization_, visiting);
    }

    /// The offset of `members[index]` in its aggregate's bit stream: later
    /// members occupy lower bits (IEEE 1800-2017 7.2.1, 6.24.3).
    [[nodiscard]] std::optional<std::uint64_t> aggregate_member_offset(
        const std::vector<semantic::sv::PackedMember>& members,
        const std::size_t index, const bool union_form)
    {
        if (union_form) {
            return 0U;
        }
        std::uint64_t offset { };
        for (auto later = index + 1U; later < members.size(); ++later) {
            const auto width = aggregate_width(members[later].type);
            if (!width) {
                return std::nullopt;
            }
            offset += *width;
        }
        return offset;
    }

    /// True for a structure or fixed-size array type that is not packed.
    [[nodiscard]] bool unpacked_aggregate(
        const semantic::sv::TypeReference& input)
    {
        const auto type = with_unpacked_dimensions(input);
        if (!type.unpacked_dimensions.empty()) {
            return unpacked_bounds(type).has_value();
        }
        if (packed_members(type) != nullptr) {
            return false;
        }
        return aggregate_members(type) != nullptr;
    }

    /// unpacked_aggregate for a type held by a declaration record, kept per
    /// specialization when the type has no unpacked dimension bounds.
    [[nodiscard]] bool record_unpacked_aggregate(
        const semantic::sv::TypeReference& type)
    {
        auto& kinds = specialization_cache(specialization_)
                          .constants->record_unpacked_aggregates;
        if (const auto found = kinds.find(&type); found != kinds.end()) {
            return found->second;
        }
        const bool result = unpacked_aggregate(type);
        if (with_unpacked_dimensions(type).unpacked_dimensions.empty()) {
            kinds.emplace(&type, result);
        }
        return result;
    }

    /// The value of an assignment pattern for an unpacked structure or
    /// fixed-size array, as its bit stream: positional items in order, or
    /// member keys and `default:` (IEEE 1800-2017 10.9.1, 10.9.2).
    [[nodiscard]] std::optional<Value> evaluate_unpacked_pattern(
        const semantic::sv::Expression& pattern,
        const semantic::sv::TypeReference& input)
    {
        const auto type = with_unpacked_dimensions(input);
        const auto width = aggregate_width(type);
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        PackedLogic4 packed { static_cast<std::size_t>(*width), Logic4::x };
        const auto place = [&](const semantic::ExpressionId item,
                               const semantic::sv::TypeReference& item_type,
                               const std::uint64_t offset) -> bool {
            const auto item_width = aggregate_width(item_type);
            auto value = item_width ? evaluate_typed(item, item_type)
                                    : std::nullopt;
            if (!value) {
                return false;
            }
            const auto fitted = resized(std::move(*value),
                static_cast<std::uint32_t>(*item_width));
            for (std::uint64_t bit { }; bit < *item_width; ++bit) {
                if (offset + bit < packed.width()) {
                    packed.set(offset + bit,
                        runtime::to_logic4(fitted.packed.get_logic9(bit)));
                }
            }
            return true;
        };
        const auto& associations = pattern.associations;
        if (!type.unpacked_dimensions.empty()) {
            const auto bounds = unpacked_bounds(type);
            if (!bounds || bounds->empty()) {
                return std::nullopt;
            }
            const auto [left, right] = bounds->front();
            const auto count = static_cast<std::uint64_t>(
                std::max(left, right) - std::min(left, right)) + 1U;
            auto element = type;
            element.unpacked_dimensions.erase(
                element.unpacked_dimensions.begin());
            if (element.unpacked_dimensions.empty()) {
                element.container_form.reset();
            }
            const auto element_width = aggregate_width(element);
            if (!element_width) {
                return std::nullopt;
            }
            if (associations.size() == 1U
                && associations.front().choice_spelling == "default") {
                for (std::uint64_t index { }; index < count; ++index) {
                    if (!place(associations.front().value, element,
                            (count - 1U - index) * *element_width)) {
                        return std::nullopt;
                    }
                }
            } else if (associations.size() == count
                && std::ranges::all_of(associations,
                    [](const auto& association) {
                        return association.choice_spelling.empty();
                    })) {
                for (std::uint64_t index { }; index < count; ++index) {
                    if (!place(associations[index].value, element,
                            (count - 1U - index) * *element_width)) {
                        return std::nullopt;
                    }
                }
            } else {
                return std::nullopt;
            }
            return make_value(std::move(packed), false, false);
        }
        bool union_form { };
        const auto* const members = aggregate_members(type, &union_form);
        if (members == nullptr || union_form) {
            return std::nullopt;
        }
        const bool positional = std::ranges::all_of(associations,
            [](const auto& association) {
                return association.choice_spelling.empty();
            });
        if (positional && associations.size() != members->size()) {
            return std::nullopt;
        }
        std::optional<semantic::ExpressionId> default_value;
        std::unordered_set<std::string> assigned;
        const auto place_member = [&](const std::size_t index,
                                      const semantic::ExpressionId item) {
            const auto offset
                = aggregate_member_offset(*members, index, false);
            return offset
                && place(item, (*members)[index].type, *offset);
        };
        for (std::size_t index { }; index < associations.size(); ++index) {
            const auto& association = associations[index];
            if (positional) {
                if (!place_member(index, association.value)) {
                    return std::nullopt;
                }
                continue;
            }
            if (association.choice_spelling == "default") {
                default_value = association.value;
                continue;
            }
            std::string_view member_name = association.choice_spelling;
            if (member_name == "@key" && association.choices.size() == 1U) {
                if (const auto key
                    = pattern_member_key(association.choices.front())) {
                    member_name = *key;
                }
            }
            const auto member = std::ranges::find(*members, member_name,
                &semantic::sv::PackedMember::name);
            if (member == members->end()
                || !place_member(
                    static_cast<std::size_t>(member - members->begin()),
                    association.value)) {
                return std::nullopt;
            }
            assigned.insert(member->name);
        }
        // Without `default:`, a member the pattern does not name keeps its
        // declared initial value (IEEE 1800-2017 7.2.2).
        for (std::size_t index { }; index < members->size(); ++index) {
            const auto& member = (*members)[index];
            const auto item = default_value ? default_value
                                            : member.initializer;
            if (!assigned.contains(member.name) && item
                && !place_member(index, *item)) {
                return std::nullopt;
            }
        }
        return make_value(std::move(packed), false, false);
    }

    /// The member a keyed pattern choice names: its name, or in a class
    /// body the class property the same spelling resolved to (10.9.2).
    [[nodiscard]] std::optional<std::string_view> pattern_member_key(
        const semantic::ExpressionId choice_id) const
    {
        const auto choice = specialization_.find_expression(choice_id);
        if (!choice || choice->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& choice_record = *choice->systemverilog;
        if (choice_record.kind == ExpressionKind::name) {
            return std::string_view { choice_record.text };
        }
        if (choice_record.kind != ExpressionKind::class_static_property
            && choice_record.kind != ExpressionKind::class_property
            && !(choice_record.kind == ExpressionKind::call
                && choice_record.text.starts_with("@sv-static-property:"))) {
            return std::nullopt;
        }
        const std::string_view identity
            = !choice_record.class_member_identity.empty()
            ? std::string_view { choice_record.class_member_identity }
            : std::string_view { choice_record.text };
        const auto cut = identity.rfind("::");
        return cut == std::string_view::npos ? identity
                                             : identity.substr(cut + 2U);
    }

    /// A packed array whose elements are packed structures (`s_t [1:0]`,
    /// IEEE 1800-2023 7.4.1): its element type, range and element width.
    struct PackedStructureArray {
        semantic::sv::TypeReference element;
        std::int64_t left { };
        std::int64_t right { };
        std::uint64_t element_width { };

        [[nodiscard]] std::uint64_t count() const
        {
            return support::index_distance(left, right) + 1U;
        }
    };
    [[nodiscard]] std::optional<PackedStructureArray> packed_structure_array(
        const semantic::sv::TypeReference& type)
    {
        // An inline structure's single range is its own width; with two
        // dimensions (`s_t [1:0]` inlined as its structure) the outer one
        // is the array's and the inner one the element's.
        const bool nested = type.packed_dimensions.size() == 2U;
        if (!type.packed_range || type.packed_dimensions.size() > 2U
            || type.target.spelling.empty()
            || (!nested
                && (type.target.spelling.starts_with("struct")
                    || type.target.spelling.starts_with("union")))) {
            return std::nullopt;
        }
        const auto outer = nested ? type.packed_dimensions.front()
                                  : *type.packed_range;
        auto element = type;
        element.packed_range.reset();
        if (nested) {
            element.packed_range = type.packed_dimensions.back();
        }
        element.packed_dimensions.clear();
        element.signed_value = false;
        const auto range_bound = [&](const std::optional<std::int64_t> value,
                                     const std::optional<
                                         semantic::ExpressionId> bound) {
            if (value || !bound) return value;
            const auto folded = evaluate(*bound);
            return folded ? folded->integer_value() : std::nullopt;
        };
        const auto left = range_bound(outer.left, outer.left_expression);
        const auto right = range_bound(outer.right, outer.right_expression);
        if (!left || !right || packed_members(element) == nullptr) {
            return std::nullopt;
        }
        // A reference repeating its typedef's own range denotes the
        // structure itself, not an array of it.
        if (!nested) {
            const auto effective
                = semantic::CompiledDesignResolver { specialization_ }
                      .effective_systemverilog_type(
                          element, specialization_.scope());
            if (effective && effective->packed_range
                && range_bound(effective->packed_range->left,
                       effective->packed_range->left_expression)
                    == left
                && range_bound(effective->packed_range->right,
                       effective->packed_range->right_expression)
                    == right) {
                return std::nullopt;
            }
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto element_width
            = resolved_type_width(element, &specialization_, visiting);
        const auto count = support::index_distance(*left, *right) + 1U;
        if (!element_width || *element_width == 0U || count == 0U
            || *element_width > (1U << 20U) / count) {
            return std::nullopt;
        }
        return PackedStructureArray {
            std::move(element), *left, *right, *element_width };
    }

    /// Whether the type of the named base `name` can be a packed array of
    /// packed structures: its shape and element type, without its range
    /// bounds, which alone may read a constant-function frame. Kept per
    /// specialization by name record.
    [[nodiscard]] bool structure_array_name(
        const semantic::sv::Expression& name,
        const semantic::sv::TypeReference& type) const
    {
        auto& names = specialization_cache(specialization_)
                          .constants->structure_array_names;
        if (const auto found = names.find(&name); found != names.end()) {
            return found->second;
        }
        const bool nested = type.packed_dimensions.size() == 2U;
        bool result = type.packed_range && type.packed_dimensions.size() <= 2U
            && !type.target.spelling.empty()
            && (nested
                || (!type.target.spelling.starts_with("struct")
                    && !type.target.spelling.starts_with("union")));
        if (result) {
            auto element = type;
            element.packed_range.reset();
            if (nested) {
                element.packed_range = type.packed_dimensions.back();
            }
            element.packed_dimensions.clear();
            element.signed_value = false;
            result = packed_members(element) != nullptr;
        }
        names.emplace(&name, result);
        return result;
    }

    /// An integer or logic literal's value, parsed once per specialization
    /// (IEEE 1800-2017 5.7).
    [[nodiscard]] std::optional<Value> literal_value(
        const semantic::sv::Expression& record, const bool integer_literal,
        std::string& error) const
    {
        auto& literals = specialization_cache(specialization_)
                             .constants->literals[integer_literal ? 1U : 0U];
        auto found = literals.find(&record);
        if (found == literals.end()) {
            std::string parse_error;
            auto value = parse_literal(record.text, integer_literal,
                parse_error);
            found = literals.emplace(&record,
                std::pair { std::move(value), std::move(parse_error) }).first;
        }
        if (!found->second.first) {
            error = found->second.second;
        }
        return found->second.first;
    }

    /// The value and type of a constant reached through packed structure
    /// members and packed structure array elements (`P[1].m`, `C::p.m`,
    /// IEEE 1800-2023 7.2.1, 7.4.1, 8.25.1). Null for anything else.
    [[nodiscard]] std::optional<std::pair<Value, semantic::sv::TypeReference>>
    typed_constant(const semantic::ExpressionId id)
    {
        const auto view = specialization_.find_expression(id);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        const auto slice = [](const Value& whole, const std::uint64_t offset,
                               const std::uint64_t width) {
            runtime::PackedLogic4 bits {
                static_cast<std::size_t>(width), Logic4::x };
            for (std::uint64_t bit = 0U; bit < width; ++bit) {
                if (offset + bit < whole.packed.width()) {
                    bits.set(bit, whole.packed.get(offset + bit));
                }
            }
            Value result;
            result.packed = std::move(bits);
            result.width = static_cast<std::uint32_t>(width);
            result.domain = whole.domain;
            return result;
        };
        if (record.kind == ExpressionKind::class_static_property
            || (record.kind == ExpressionKind::call
                && record.text.starts_with("@sv-static-property:"))) {
            semantic::sv::TypeReference type;
            auto value = evaluate_class_parameter(record, &type);
            if (!value) return std::nullopt;
            return std::pair { std::move(*value), std::move(type) };
        }
        if (record.kind == ExpressionKind::name && record.referenced_name
            && record.referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *record.referenced_name->selected);
            if (!declaration || declaration->systemverilog == nullptr
                || !declaration->systemverilog->type) {
                return std::nullopt;
            }
            auto type = *declaration->systemverilog->type;
            if (const auto dot = record.text.find('.');
                dot != std::string::npos
                && !packed_member_slice(type,
                    std::string_view { record.text }.substr(dot + 1U),
                    &type)) {
                return std::nullopt;
            }
            auto value = evaluate(id);
            if (sv_const_trace()) {
                std::fprintf(stderr, "FSIM_SV_CONST typed name '%s' = %s\n",
                    record.text.c_str(),
                    value ? value->canonical().c_str() : "none");
            }
            if (!value) return std::nullopt;
            return std::pair { std::move(*value), std::move(type) };
        }
        if (record.kind == ExpressionKind::call
            && record.text.starts_with("@sv-select:")
            && record.operands.size() == 1U) {
            auto base = typed_constant(record.operands.front());
            semantic::sv::TypeReference type;
            const auto member = base
                ? packed_member_slice(base->second,
                      std::string_view { record.text }.substr(11U), &type)
                : std::nullopt;
            if (!member) return std::nullopt;
            return std::pair { slice(base->first, member->first,
                                   member->second),
                std::move(type) };
        }
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            // A named base is checked by its type before its value is read.
            if (const auto named = specialization_.find_expression(
                    record.operands.front());
                named && named->systemverilog != nullptr
                && named->systemverilog->kind == ExpressionKind::name) {
                const auto& name = *named->systemverilog;
                const auto declaration = name.referenced_name
                        && name.referenced_name->selected
                    ? specialization_.find_declaration(
                          *name.referenced_name->selected)
                    : std::nullopt;
                if (!declaration || declaration->systemverilog == nullptr
                    || !declaration->systemverilog->type) {
                    return std::nullopt;
                }
                const auto* declared = &*declaration->systemverilog->type;
                semantic::sv::TypeReference member_type;
                if (const auto dot = name.text.find('.');
                    dot != std::string::npos) {
                    if (!packed_member_slice(*declared,
                            std::string_view { name.text }.substr(dot + 1U),
                            &member_type)) {
                        return std::nullopt;
                    }
                    declared = &member_type;
                }
                const auto& type = *declared;
                if (sv_const_trace()) {
                    std::fprintf(stderr,
                        "FSIM_SV_CONST element of '%s': type '%s' range=%d "
                        "dimensions=%zu array=%d\n",
                        name.text.c_str(), type.target.spelling.c_str(),
                        type.packed_range ? 1 : 0,
                        type.packed_dimensions.size(),
                        packed_structure_array(type) ? 1 : 0);
                }
                if (!structure_array_name(name, type)
                    || !packed_structure_array(type)) {
                    return std::nullopt;
                }
            }
            auto base = typed_constant(record.operands.front());
            const auto array = base ? packed_structure_array(base->second)
                                    : std::nullopt;
            const auto index = array ? evaluate(record.operands.back())
                                     : std::nullopt;
            const auto integer = index ? index->integer_value()
                                       : std::nullopt;
            if (!integer
                || *integer < std::min(array->left, array->right)
                || *integer > std::max(array->left, array->right)) {
                return std::nullopt;
            }
            const auto position = static_cast<std::uint64_t>(
                array->left >= array->right ? *integer - array->right
                                            : array->right - *integer);
            return std::pair { slice(base->first,
                                   position * array->element_width,
                                   array->element_width),
                array->element };
        }
        return std::nullopt;
    }

    /// An expression evaluated for a packed structure type: an assignment
    /// pattern keyed by member names and `default:` places each value in
    /// its member (IEEE 1800-2017 10.9.2); anything else evaluates as usual.
    [[nodiscard]] std::optional<Value> evaluate_typed(
        const semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type)
    {
        const auto view = specialization_.find_expression(expression);
        // Only an assignment pattern takes its form from the type (10.9).
        if (!view || view->systemverilog == nullptr
            || view->systemverilog->kind
                != ExpressionKind::assignment_pattern) {
            return evaluate(expression);
        }
        if (unpacked_aggregate(type)) {
            if (auto image
                = evaluate_unpacked_pattern(*view->systemverilog, type)) {
                return image;
            }
            error_.clear();
        }
        bool union_form { };
        const auto* const members = packed_members(type, &union_form);
        // An index-keyed or `default:` pattern for a packed array of
        // structures gives each element its own structure pattern (IEEE
        // 1800-2023 10.9.1).
        if (view && view->systemverilog != nullptr
            && view->systemverilog->kind == ExpressionKind::assignment_pattern
            && !view->systemverilog->associations.empty()
            && std::ranges::none_of(view->systemverilog->associations,
                [](const auto& association) {
                    return association.choice_spelling.empty();
                })) {
            if (const auto array = packed_structure_array(type)) {
                std::optional<semantic::ExpressionId> fallback;
                std::map<std::int64_t, semantic::ExpressionId> keyed;
                for (const auto& association :
                    view->systemverilog->associations) {
                    if (association.choice_spelling == "default") {
                        fallback = association.value;
                        continue;
                    }
                    const auto key = association.choices.size() == 1U
                        ? evaluate(association.choices.front())
                        : std::nullopt;
                    const auto index = key ? key->integer_value()
                                           : std::nullopt;
                    if (!index) {
                        error_ = "packed array pattern key is not an index";
                        return std::nullopt;
                    }
                    keyed.insert_or_assign(*index, association.value);
                }
                const auto count = array->count();
                PackedLogic4 packed { static_cast<std::size_t>(
                                          count * array->element_width),
                    Logic4::x };
                const auto step = array->left >= array->right ? -1 : 1;
                for (std::uint64_t position { }; position < count;
                     ++position) {
                    const auto index = array->left
                        + step * static_cast<std::int64_t>(position);
                    const auto found = keyed.find(index);
                    const auto item = found != keyed.end()
                        ? std::optional { found->second } : fallback;
                    if (!item) {
                        error_ = "packed array pattern leaves an element "
                                 "unassigned";
                        return std::nullopt;
                    }
                    auto value = evaluate_typed(*item, array->element);
                    if (!value) return std::nullopt;
                    const auto fitted = resized(std::move(*value),
                        static_cast<std::uint32_t>(array->element_width));
                    const auto base
                        = (count - 1U - position) * array->element_width;
                    for (std::uint64_t bit { }; bit < array->element_width;
                         ++bit) {
                        packed.set(base + bit, runtime::to_logic4(
                            fitted.packed.get_logic9(bit)));
                    }
                }
                return make_value(std::move(packed), false, false);
            }
        }
        // A positional pattern for a packed array: each item converts to
        // the element width, the first item the leftmost (10.9.1).
        if (view && view->systemverilog != nullptr && members == nullptr
            && view->systemverilog->kind == ExpressionKind::assignment_pattern
            && !view->systemverilog->associations.empty()
            && std::ranges::all_of(view->systemverilog->associations,
                [](const auto& association) {
                    return association.choice_spelling.empty();
                })) {
            std::unordered_set<std::uint32_t> visiting;
            const auto width = resolved_type_width(
                type, &specialization_, visiting);
            const auto count = view->systemverilog->associations.size();
            if (width && *width != 0U && *width % count == 0U
                && *width <= (1U << 20U)) {
                const auto element_width = *width / count;
                PackedLogic4 packed { static_cast<std::size_t>(*width),
                    Logic4::zero };
                for (std::size_t index { }; index < count; ++index) {
                    const auto item_id
                        = view->systemverilog->associations[index].value;
                    auto item = evaluate(item_id);
                    if (!item) {
                        // A real item rounds to an integer (6.12.2).
                        std::string scalar_error;
                        const auto scalar = evaluate_hir_systemverilog_scalar_constant(
                            specialization_, item_id, scalar_error);
                        const auto real = scalar ? scalar->real() : std::nullopt;
                        if (!real || !std::isfinite(*real)) {
                            return std::nullopt;
                        }
                        error_.clear();
                        const auto rounded = static_cast<std::int64_t>(
                            std::llround(*real));
                        item = make_value(PackedLogic4::from_aval_bval(64U,
                                              static_cast<std::uint64_t>(rounded), 0U),
                            true, false, frontend::ValueDomain::Bit2);
                    }
                    const auto resized_item = resized(
                        std::move(*item),
                        static_cast<std::uint32_t>(element_width));
                    const auto base = (count - 1U - index) * element_width;
                    for (std::size_t bit { }; bit < element_width; ++bit) {
                        packed.set(base + bit, runtime::to_logic4(
                            resized_item.packed.get_logic9(bit)));
                    }
                }
                return make_value(std::move(packed), type.signed_value, false,
                    type.four_state ? frontend::ValueDomain::Logic4
                                    : frontend::ValueDomain::Bit2);
            }
        }
        // A positional pattern for a packed structure gives its members in
        // declaration order, each converted to the member's type (10.9.1).
        const bool positional = view && view->systemverilog != nullptr
            && std::ranges::all_of(view->systemverilog->associations,
                [](const auto& association) {
                    return association.choice_spelling.empty();
                });
        if (!view || view->systemverilog == nullptr || members == nullptr
            || view->systemverilog->kind != ExpressionKind::assignment_pattern
            || (positional
                && view->systemverilog->associations.size()
                    != members->size())) {
            return evaluate(expression);
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto width = resolved_type_width(
            type, &specialization_, visiting);
        if (!width || *width == 0U || *width > (1U << 20U)) {
            error_ = "keyed assignment pattern requires resolved aggregate layout";
            return std::nullopt;
        }
        PackedLogic4 packed { static_cast<std::size_t>(*width), Logic4::x };
        std::optional<semantic::ExpressionId> default_value;
        std::unordered_set<std::string> assigned;
        std::vector<std::pair<std::string_view, semantic::ExpressionId>>
            type_keys;
        const auto place = [&](const semantic::sv::PackedMember& member,
                               const semantic::ExpressionId value_id) -> bool {
            std::unordered_set<std::uint32_t> member_visiting;
            const auto member_width = resolved_type_width(
                member.type, &specialization_, member_visiting);
            auto value = evaluate_typed(value_id, member.type);
            if (!value || !member_width) {
                if (sv_const_trace()) {
                    std::fprintf(stderr,
                        "FSIM_SV_CONST pattern member '%s' failed: %s\n",
                        member.name.c_str(), error_.c_str());
                }
                return false;
            }
            const auto fitted = resized(std::move(*value),
                static_cast<std::uint32_t>(*member_width));
            const auto lsb = packed_member_offset(*members,
                static_cast<std::size_t>(&member - members->data()),
                union_form);
            for (std::uint64_t bit = 0U; bit < *member_width; ++bit) {
                if (lsb + bit < packed.width()) {
                    packed.set(lsb + bit,
                        runtime::to_logic4(fitted.packed.get_logic9(bit)));
                }
            }
            return true;
        };
        for (std::size_t index { };
            const auto& association : view->systemverilog->associations) {
            if (positional) {
                if (!place((*members)[index++], association.value)) {
                    return std::nullopt;
                }
                continue;
            }
            if (association.choice_spelling == "default") {
                default_value = association.value;
                continue;
            }
            std::string_view member_name = association.choice_spelling;
            if (member_name == "@key" && association.choices.size() == 1U) {
                if (const auto key
                    = pattern_member_key(association.choices.front())) {
                    member_name = *key;
                }
            }
            const auto member = std::ranges::find(*members, member_name,
                &semantic::sv::PackedMember::name);
            // A type key (`int: v`) sets every member of that type that no
            // member key names (IEEE 1800-2017 10.9.2).
            if (member == members->end() && member_name == "@key"
                && association.choices.size() == 1U) {
                if (const auto key = specialization_.find_expression(
                        association.choices.front());
                    key && key->systemverilog != nullptr) {
                    member_name = key->systemverilog->text;
                }
            }
            if (member == members->end()
                && std::ranges::any_of(*members,
                    [&](const semantic::sv::PackedMember& candidate) {
                        return candidate.type.target.spelling == member_name;
                    })) {
                type_keys.emplace_back(member_name, association.value);
                continue;
            }
            if (member == members->end() || !place(*member, association.value)) {
                error_ = "keyed assignment pattern names an unknown member '"
                    + std::string { member_name } + "'";
                return std::nullopt;
            }
            assigned.insert(member->name);
        }
        for (const auto& [spelling, item] : type_keys) {
            for (const auto& member : *members) {
                if (member.type.target.spelling == spelling
                    && !assigned.contains(member.name)) {
                    if (!place(member, item)) {
                        return std::nullopt;
                    }
                    assigned.insert(member.name);
                }
            }
        }
        if (default_value) {
            for (const auto& member : *members) {
                if (!assigned.contains(member.name)
                    && !place(member, *default_value)) {
                    return std::nullopt;
                }
            }
        }
        return make_value(std::move(packed), false, false);
    }

    /// The bit offset and width of `path` (`a.b`) within a packed
    /// structure or union type.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>>
    packed_member_slice(
        semantic::sv::TypeReference type, std::string_view path,
        semantic::sv::TypeReference* const member_type = nullptr) const
    {
        std::uint64_t offset { };
        while (!path.empty()) {
            const auto separator = path.find('.');
            const auto segment = path.substr(0U, separator);
            if (const auto effective
                = semantic::CompiledDesignResolver { specialization_ }
                      .effective_systemverilog_type(
                          type, specialization_.scope())) {
                type = *effective;
            }
            auto definition = type.target.target.valid()
                ? find_design_type(type.target.target)
                : std::nullopt;
            // A typedef of a package type (`typedef pkg::S t;`) names that
            // type in the typedef's own scope.
            for (std::size_t depth { }; depth < 8U && definition
                 && definition->systemverilog != nullptr
                 && definition->systemverilog->form
                     == semantic::sv::TypeForm::alias;
                 ++depth) {
                const auto alias = specialization_.find_declaration(
                    definition->systemverilog->declaration);
                auto base = definition->systemverilog->base;
                if (const auto effective
                    = semantic::CompiledDesignResolver { specialization_ }
                          .effective_systemverilog_type(base,
                              alias && alias->systemverilog != nullptr
                                  ? alias->systemverilog->scope
                                  : specialization_.scope())) {
                    base = *effective;
                }
                definition = base.target.target.valid()
                    ? find_design_type(base.target.target)
                    : std::nullopt;
            }
            if (!definition || definition->systemverilog == nullptr
                || (definition->systemverilog->form
                        != semantic::sv::TypeForm::packed_structure
                    && definition->systemverilog->form
                        != semantic::sv::TypeForm::packed_union)) {
                return std::nullopt;
            }
            const auto& members = definition->systemverilog->members;
            const auto member = std::ranges::find(
                members, segment, &semantic::sv::PackedMember::name);
            if (member == members.end()) {
                return std::nullopt;
            }
            offset += packed_member_offset(members,
                static_cast<std::size_t>(member - members.begin()),
                definition->systemverilog->form
                    == semantic::sv::TypeForm::packed_union);
            type = member->type;
            path = separator == std::string_view::npos
                ? std::string_view { }
                : path.substr(separator + 1U);
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto width = resolved_type_width(
            type, &specialization_, visiting);
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        if (member_type != nullptr) {
            *member_type = std::move(type);
        }
        return std::pair { offset, *width };
    }

    /// The bit slice a member path selects in a packed aggregate, or in the
    /// bit stream of an unpacked structure (IEEE 1800-2017 7.2.1, 6.24.3).
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>>
    member_slice(const semantic::sv::TypeReference& type,
        const std::string_view path)
    {
        if (auto slice = packed_member_slice(type, path)) {
            return slice;
        }
        if (!unpacked_aggregate(type)) {
            return std::nullopt;
        }
        const auto aggregate = aggregate_slice(type, "." + std::string { path });
        if (!aggregate) {
            return std::nullopt;
        }
        return std::pair { aggregate->offset, aggregate->width };
    }

    /// The frame-local packed aggregate a dotted name `s.m` selects into,
    /// with the member's bit slice.
    struct FrameMember {
        Value* root { };
        std::uint64_t root_width { };
        std::uint64_t offset { };
        std::uint64_t width { };
    };
    [[nodiscard]] std::optional<FrameMember> frame_member(
        const semantic::sv::Expression& record)
    {
        if (record.kind != ExpressionKind::name || frames_.empty()) {
            return std::nullopt;
        }
        const auto separator = record.text.find('.');
        if (separator == std::string::npos || separator == 0U) {
            return std::nullopt;
        }
        const auto root_name
            = std::string_view { record.text }.substr(0U, separator);
        const auto root = semantic::CompiledDesignResolver { specialization_ }
                              .resolve_systemverilog(root_name, record.scope,
                                  { }, false)
                              .unique();
        if (!root) {
            return std::nullopt;
        }
        const auto declaration = specialization_.find_declaration(*root);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type) {
            return std::nullopt;
        }
        for (auto frame = frames_.rbegin(); frame != frames_.rend();
            ++frame) {
            const auto found = frame->values.find(*root);
            if (found == frame->values.end()) {
                continue;
            }
            const auto& type = *declaration->systemverilog->type;
            const auto slice = member_slice(type,
                std::string_view { record.text }.substr(separator + 1U));
            std::unordered_set<std::uint32_t> visiting;
            auto root_width = resolved_type_width(
                type, &specialization_, visiting);
            if (!root_width && unpacked_aggregate(type)) {
                root_width = aggregate_width(type);
            }
            if (!slice || !root_width
                || slice->first + slice->second > *root_width) {
                return std::nullopt;
            }
            return FrameMember { &found->second, *root_width,
                slice->first, slice->second };
        }
        return std::nullopt;
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
        // A member of a packed structure local: `s.m = v` (IEEE 1800-2017
        // 7.2.1, 13.4.4).
        if (const auto member = frame_member(record)) {
            auto& root = *member->root;
            if (root.packed.width() != member->root_width) {
                // Widen the whole value first; an unsized fill literal
                // (`'0`, `'1`) fills every bit.
                const auto fill = root.unsized && root.packed.width() == 1U
                    ? root.packed.get(0U)
                    : Logic4::zero;
                runtime::PackedLogic4 widened {
                    static_cast<std::size_t>(member->root_width), fill };
                for (std::size_t bit = 0U; bit < root.packed.width()
                    && bit < widened.width() && !(root.unsized
                        && root.packed.width() == 1U); ++bit) {
                    widened.set(bit, root.packed.get(bit));
                }
                root.packed = std::move(widened);
                root.width = static_cast<std::uint32_t>(member->root_width);
                root.unsized = false;
            }
            for (std::uint64_t bit = 0U; bit < member->width; ++bit) {
                const auto state = bit < value.packed.width()
                    ? value.packed.get(bit)
                    : value.signed_value && value.packed.width() != 0U
                    ? value.packed.get(value.packed.width() - 1U)
                    : Logic4::zero;
                root.packed.set(member->offset + bit, state);
            }
            return true;
        }
        if (record.kind == ExpressionKind::index
            && record.operands.size() == 2U) {
            const auto base_view = specialization_.find_expression(
                record.operands.front());
            if (base_view && base_view->systemverilog != nullptr
                && base_view->systemverilog->kind == ExpressionKind::name
                && base_view->systemverilog->referenced_name
                && base_view->systemverilog->referenced_name->selected) {
                const auto declaration
                    = *base_view->systemverilog->referenced_name->selected;
                if (find_local_array(declaration) != nullptr) {
                    const auto index = evaluate(record.operands.back());
                    if (!index || !consume_work(1U)) {
                        return false;
                    }
                    auto* array = find_local_array(declaration);
                    if (array == nullptr) {
                        return false;
                    }
                    const auto integer = index->integer_value();
                    if (!integer) {
                        return true;
                    }
                    const auto offset = local_array_offset(*array, *integer);
                    if (!offset) {
                        return true;
                    }
                    auto converted = convert_hir_systemverilog_constant(
                        std::move(value), array->element_type, error_,
                        &specialization_);
                    if (!converted) {
                        return false;
                    }
                    converted->packed_range
                        = array->element_type.packed_range;
                    array->elements[*offset] = std::move(*converted);
                    return true;
                }
            }
        }
        if (record.kind == ExpressionKind::name
            && record.referenced_name
            && record.referenced_name->selected) {
            const auto declaration = *record.referenced_name->selected;
            const auto formal = specialization_.find_declaration(declaration);
            if (formal && formal->systemverilog != nullptr
                && formal->systemverilog->type
                && hir_systemverilog_explicit_integral_type(
                    *formal->systemverilog->type)) {
                auto converted = convert_record_type(
                    std::move(value), *formal->systemverilog->type);
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
            const auto shape = record.kind == ExpressionKind::index
                    && record.operands.size() == 2U
                ? packed_shape(record.operands.front())
                : std::nullopt;
            if (shape && shape->size() > 1U) {
                // An index of a multidimensional packed array writes one
                // element of its outermost dimension (IEEE 1800-2017 7.4.5).
                std::uint64_t element_width = 1U;
                std::uint64_t total_width = 1U;
                for (std::size_t level = 0U; level < shape->size(); ++level) {
                    const auto& [left, right] = (*shape)[level];
                    const auto extent = static_cast<std::uint64_t>(
                        std::max(left, right) - std::min(left, right) + 1);
                    if (level != 0U) {
                        element_width *= extent;
                    }
                    total_width *= extent;
                }
                if (total_width > maximum_constant_width) {
                    return false;
                }
                const auto index = evaluate(record.operands.back());
                const auto integer = index && index->known()
                    ? index->integer_value() : std::nullopt;
                const auto [left, right] = shape->front();
                if (!integer || *integer < std::min(left, right)
                    || *integer > std::max(left, right)) {
                    return true;
                }
                if (base->packed.width() != total_width) {
                    PackedLogic4 widened {
                        static_cast<std::size_t>(total_width),
                        base->domain == frontend::ValueDomain::Bit2
                            ? Logic4::zero : Logic4::x };
                    for (std::size_t bit = 0U; bit < base->packed.width()
                        && bit < widened.width(); ++bit) {
                        widened.set(bit, runtime::to_logic4(
                            base->packed.get_logic9(bit)));
                    }
                    base->packed = std::move(widened);
                    base->width = static_cast<std::uint32_t>(total_width);
                    base->unsized = false;
                }
                const auto element = static_cast<std::uint64_t>(
                    left >= right ? *integer - right : right - *integer);
                for (std::uint64_t bit = 0U; bit < element_width; ++bit) {
                    const auto state = bit < value.packed.width()
                        ? runtime::to_logic4(value.packed.get_logic9(bit))
                        : Logic4::zero;
                    base->packed.set(element * element_width + bit, state);
                }
            } else if (record.kind == ExpressionKind::index
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
            if (!declaration || declaration->systemverilog == nullptr) {
                continue;
            }
            const auto& source = *declaration->systemverilog;
            if (!source.initializer) {
                if (!initialize_local_array(declaration_id, source)) {
                    return false;
                }
                if (frames_.back().arrays.contains(declaration_id)
                    || source.form
                        != semantic::sv::DeclarationForm::variable
                    || !source.type || source.type->container_form
                    || !source.type->unpacked_dimensions.empty()
                    || !source.type->value_form) {
                    continue;
                }
                using TypeForm = semantic::sv::TypeForm;
                const auto form = *source.type->value_form;
                if (form != TypeForm::packed_integral
                    && form != TypeForm::enumeration
                    && form != TypeForm::packed_structure
                    && form != TypeForm::packed_union
                    && form != TypeForm::tagged_union) {
                    continue;
                }
                std::unordered_set<std::uint32_t> visiting;
                const auto width = resolved_type_width(
                    *source.type, &specialization_, visiting);
                if (!width || *width == 0U
                    || *width > maximum_constant_width) {
                    error_ = "constant function local type width is unresolved";
                    return false;
                }
                auto initial_value = make_value(
                    PackedLogic4 {
                        static_cast<std::size_t>(*width),
                        source.type->four_state ? Logic4::x : Logic4::zero },
                    source.type->signed_value, false,
                    type_domain(*source.type));
                auto converted = convert_record_type(
                    std::move(initial_value), *source.type);
                if (!converted) {
                    return false;
                }
                frames_.back().values.insert_or_assign(
                    declaration_id, std::move(*converted));
                continue;
            }
            if (!initialize_local_array(declaration_id, source)) {
                return false;
            }
            if (const auto assigned
                = assign_local_array(declaration_id, *source.initializer)) {
                if (!*assigned) {
                    return false;
                }
                continue;
            }
            auto value = source.type
                ? evaluate_typed(*source.initializer, *source.type)
                : evaluate(*source.initializer);
            if (!value) {
                return false;
            }
            if (source.type
                && hir_systemverilog_explicit_integral_type(*source.type)) {
                value = convert_record_type(
                    std::move(*value), *source.type);
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
            const auto target = specialization_.find_expression(*record.target);
            const auto* named = target && target->systemverilog != nullptr
                    && target->systemverilog->kind == ExpressionKind::name
                    && target->systemverilog->referenced_name
                    && target->systemverilog->referenced_name->selected
                ? &*target->systemverilog->referenced_name->selected
                : nullptr;
            if (named != nullptr) {
                if (const auto assigned
                    = assign_local_array(*named, *record.value)) {
                    return *assigned ? Flow::normal : Flow::failed;
                }
            }
            // A pattern takes the shape of the variable or local array
            // element it is assigned to (IEEE 1800-2017 10.9); any other
            // value is self-determined.
            const auto value_view
                = specialization_.find_expression(*record.value);
            const bool pattern = value_view
                && value_view->systemverilog != nullptr
                && value_view->systemverilog->kind
                    == ExpressionKind::assignment_pattern;
            const auto target_type = [&]()
                -> std::optional<semantic::sv::TypeReference> {
                if (!pattern) {
                    return std::nullopt;
                }
                if (named != nullptr) {
                    const auto declared
                        = specialization_.find_declaration(*named);
                    return declared && declared->systemverilog != nullptr
                            && declared->systemverilog->type
                        ? std::optional { *declared->systemverilog->type }
                        : std::nullopt;
                }
                if (!target || target->systemverilog == nullptr
                    || target->systemverilog->kind != ExpressionKind::index
                    || target->systemverilog->operands.size() != 2U) {
                    return std::nullopt;
                }
                const auto base = specialization_.find_expression(
                    target->systemverilog->operands.front());
                const auto* array = base && base->systemverilog != nullptr
                        && base->systemverilog->kind == ExpressionKind::name
                        && base->systemverilog->referenced_name
                        && base->systemverilog->referenced_name->selected
                    ? find_local_array(
                          *base->systemverilog->referenced_name->selected)
                    : nullptr;
                return array != nullptr
                    ? std::optional { array->element_type } : std::nullopt;
            }();
            auto value = target_type
                ? evaluate_typed(*record.value, *target_type)
                : evaluate(*record.value);
            return value && assign_target(*record.target, std::move(*value))
                ? Flow::normal : Flow::failed;
        }
        if (record.kind == Kind::return_statement) {
            // An unpacked array function returns into its result array.
            if (record.value
                && frames_.back().arrays.contains(frames_.back().callable)) {
                const auto value = evaluate_array(*record.value);
                return value
                        && copy_array(frames_.back().arrays.at(
                                          frames_.back().callable),
                            *value)
                    ? Flow::returned
                    : Flow::failed;
            }
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
            // The case expression and items compare signed only when all of
            // them are signed (IEEE 1800-2017 12.5, 11.8.1).
            bool all_signed = selector->signed_value;
            if (record.case_match == semantic::sv::CaseMatchKind::exact) {
                for (const auto& alternative : record.case_alternatives) {
                    for (const auto choice : alternative.choices) {
                        const auto candidate = evaluate(choice);
                        all_signed = all_signed && candidate
                            && candidate->signed_value;
                    }
                }
            }
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
                              auto candidate = evaluate(choice);
                              if (!candidate || !candidate->known()
                                  || !selector->known()) {
                                  return std::nullopt;
                              }
                              auto left = *candidate;
                              auto right = *selector;
                              left.signed_value = all_signed;
                              right.signed_value = all_signed;
                              return compare_known(left, right) == 0;
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

    // ---- Real-family values (IEEE 1800-2017 6.12, 11.3.1) ----

    /// Whether an expression has a real-family type: a real literal, a real
    /// declaration, a real cast or math function, or an arithmetic or
    /// conditional operator with a real operand (11.3.1, 11.4.11).
    /// Kept for the specialization: it reads declared types, not frames.
    [[nodiscard]] bool real_valued(const semantic::ExpressionId expression)
    {
        auto& known = specialization_cache(specialization_)
                          .constants->real_valued;
        if (const auto found = known.find(expression.value());
            found != known.end()) {
            return found->second;
        }
        // Not real while its own operands are examined.
        known.emplace(expression.value(), false);
        const auto result = real_valued_impl(expression);
        known[expression.value()] = result;
        return result;
    }

    [[nodiscard]] bool real_valued_impl(
        const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return false;
        }
        const auto& record = *view->systemverilog;
        if (record.decimal_literal
            || real_family(scalar_kind(record.scalar_kind))) {
            return true;
        }
        const auto operand = [&](const std::size_t index) {
            return index < record.operands.size()
                && real_valued(record.operands[index]);
        };
        switch (record.kind) {
        case ExpressionKind::name: {
            const auto declaration = systemverilog_constant_name_declaration(
                specialization_, record);
            if (!declaration) {
                return false;
            }
            const auto& actuals
                = specialization_.specialization().actual_identities;
            const auto actual = std::ranges::find(actuals, *declaration,
                &semantic::SpecializedHirActualIdentity::declaration);
            const auto declared = specialization_.find_declaration(
                *declaration);
            if (!declared || declared->systemverilog == nullptr) {
                return false;
            }
            const auto& candidate = *declared->systemverilog;
            if (candidate.type
                && candidate.type->target.spelling != "implicit") {
                return real_family(scalar_kind(*candidate.type));
            }
            if (actual != actuals.end()) {
                const auto scalar = parse_scalar_identity(actual->identity);
                return scalar && real_family(scalar->kind);
            }
            return candidate.initializer
                && real_valued(*candidate.initializer);
        }
        case ExpressionKind::unary:
            return (record.text == "-" || record.text == "+") && operand(0U);
        case ExpressionKind::binary:
            return (record.text == "+" || record.text == "-"
                       || record.text == "*" || record.text == "/"
                       || record.text == "**")
                && (operand(0U) || operand(1U));
        case ExpressionKind::call:
            if (record.text == "?:") {
                return operand(1U) || operand(2U);
            }
            return record.text == "@sv-cast:real"
                || record.text == "@sv-cast:shortreal"
                || record.text == "@sv-cast:realtime"
                || real_math_function(record.text).has_value();
        default:
            return false;
        }
    }

    /// A real or time literal; a time literal is rounded to the time
    /// precision and expressed in the time unit (IEEE 1800-2017 5.8).
    [[nodiscard]] std::optional<Value> evaluate_decimal(
        const semantic::sv::Expression& record)
    {
        auto value = exact_decimal(*record.decimal_literal, error_);
        if (!value) {
            return std::nullopt;
        }
        if (record.decimal_literal->kind
            != semantic::sv::DecimalLiteralKind::time) {
            return real_value(*value, ScalarKind::Real);
        }
        const auto factor = physical_unit_femtoseconds(
            record.decimal_literal->time_unit);
        const auto owner = scope_unit(specialization_, record.scope);
        const auto unit = owner
            ? specialization_.design().find_unit(*owner)
            : std::optional<semantic::CompiledUnitView> { };
        const auto scale = unit && unit->systemverilog != nullptr
            ? scalar_time_scale(unit->systemverilog->compilation.time_unit)
            : 1U;
        if (!factor || scale == 0U) {
            error_ = "time literal has an invalid unit or evaluation context";
            return std::nullopt;
        }
        const auto precision = unit && unit->systemverilog != nullptr
            ? scalar_time_scale(
                  unit->systemverilog->compilation.time_precision)
            : 0U;
        auto femtoseconds = *value * static_cast<double>(*factor);
        if (precision != 0U) {
            femtoseconds = std::round(
                femtoseconds / static_cast<double>(precision))
                * static_cast<double>(precision);
        }
        return real_value(
            femtoseconds / static_cast<double>(scale), ScalarKind::Realtime);
    }

    [[nodiscard]] static ScalarKind value_kind(const Value& value) noexcept
    {
        return value.scalar ? value.scalar->kind : ScalarKind::None;
    }

    /// The chandle kind of a null literal or a chandle expression.
    [[nodiscard]] ScalarKind value_kind_hint(
        const semantic::ExpressionId expression) const
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return ScalarKind::None;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == ExpressionKind::class_null
            || record.text == "@sv-null") {
            return ScalarKind::Chandle;
        }
        return scalar_kind(record.scalar_kind) == ScalarKind::Chandle
            ? ScalarKind::Chandle
            : ScalarKind::None;
    }

    /// Arithmetic and comparison with a real operand: the other operand
    /// converts to real (IEEE 1800-2017 11.3.1); comparisons give a 1-bit
    /// result.
    [[nodiscard]] std::optional<Value> evaluate_real_binary(
        const semantic::sv::Expression& record)
    {
        auto left = evaluate(record.operands.front());
        if (!left) {
            return std::nullopt;
        }
        auto right = evaluate(record.operands.back());
        if (!right) {
            return std::nullopt;
        }
        const auto& op = record.text;
        const bool equality = op == "==" || op == "===";
        const bool inequality = op == "!=" || op == "!==";
        if (value_kind(*left) == ScalarKind::Chandle
            || value_kind(*right) == ScalarKind::Chandle) {
            if (value_kind(*left) != value_kind(*right)
                || (!equality && !inequality)) {
                error_ = "chandle constants only support equality and "
                         "inequality with chandle or null";
                return std::nullopt;
            }
            const auto equal = left->scalar->bits == right->scalar->bits;
            return logical_result(equal == equality ? Truth::true_value
                                                    : Truth::false_value);
        }
        const auto lhs = real_of(*left);
        const auto rhs = real_of(*right);
        if (!lhs || !rhs) {
            error_ = "real constant operand is not numeric";
            return std::nullopt;
        }
        std::optional<bool> compared;
        if (equality) {
            compared = *lhs == *rhs;
        } else if (inequality) {
            compared = *lhs != *rhs;
        } else if (op == "<") {
            compared = *lhs < *rhs;
        } else if (op == "<=") {
            compared = *lhs <= *rhs;
        } else if (op == ">") {
            compared = *lhs > *rhs;
        } else if (op == ">=") {
            compared = *lhs >= *rhs;
        }
        if (compared) {
            return logical_result(
                *compared ? Truth::true_value : Truth::false_value);
        }
        auto kind = promote_scalar_kind(value_kind(*left), value_kind(*right));
        if (!real_family(kind)) {
            kind = ScalarKind::Real;
        }
        double result { };
        if (op == "+") {
            result = *lhs + *rhs;
        } else if (op == "-") {
            result = *lhs - *rhs;
        } else if (op == "*") {
            result = *lhs * *rhs;
        } else if (op == "/") {
            result = *lhs / *rhs;
        } else if (op == "**") {
            result = std::pow(*lhs, *rhs);
        } else {
            error_ = "operator '" + op + "' is not legal for real operands";
            return std::nullopt;
        }
        // A real division by zero gives an IEEE 754 infinity or NaN.
        if (!std::isfinite(result) && op != "/" && op != "**") {
            error_ = "real constant operation produced a nonfinite result";
            return std::nullopt;
        }
        return real_value(result, kind);
    }

    // Pushes a frame holding a parameterized class's parameters in the
    // selected specialization (IEEE 1800-2017 8.25.1): the selection's
    // actuals, else the parameters' defaults. The actuals are written where
    // the selection is, so they are evaluated before the class frame. The
    // caller pops the frame when the result is true.
    [[nodiscard]] bool push_class_frame(
        const semantic::sv::ClassDeclaration& owner,
        const semantic::sv::TypeReference* const selection)
    {
        if (selection != nullptr && !selection->class_identity.empty()
            && selection->class_identity
                != semantic::sv::class_declaration_identity(owner)) {
            error_ = "class constant inherited through a parameterized base";
            return false;
        }
        std::vector<std::optional<Value>> bound(owner.parameters.size());
        // A fixed unpacked array value parameter is kept as a frame array
        // (10.9.1); its actual's items are evaluated here and take the
        // element type once the class frame binds the element type's
        // parameters.
        const auto array_type = [&](const semantic::sv::ClassParameter&
                                        parameter)
            -> std::optional<semantic::sv::TypeReference> {
            const auto declaration
                = specialization_.find_declaration(parameter.declaration);
            return parameter.type && declaration
                    && declaration->systemverilog != nullptr
                ? unpacked_array_type(
                      *parameter.type, declaration->systemverilog->scope)
                : std::nullopt;
        };
        std::vector<std::optional<std::vector<Value>>> bound_items(
            owner.parameters.size());
        if (selection != nullptr) {
            std::size_t position { };
            for (const auto& actual : selection->interface_parameter_actuals) {
                auto index = position++;
                if (actual.formal) {
                    index = static_cast<std::size_t>(std::distance(
                        owner.parameters.begin(),
                        std::ranges::find(owner.parameters, *actual.formal,
                            &semantic::sv::ClassParameter::name)));
                }
                if (index >= bound.size()) {
                    error_ = "class specialization actual has no formal";
                    return false;
                }
                const auto& parameter = owner.parameters[index];
                if (parameter.type_parameter
                    || actual.kind != semantic::sv::ActualKind::expression
                    || !actual.expression) {
                    continue;
                }
                if (array_type(parameter)) {
                    const auto view
                        = specialization_.find_expression(*actual.expression);
                    if (!view || view->systemverilog == nullptr
                        || view->systemverilog->kind
                            != ExpressionKind::assignment_pattern) {
                        error_ = "class array parameter actual is not a "
                                 "positional pattern";
                        return false;
                    }
                    std::vector<Value> items;
                    for (const auto& association :
                        view->systemverilog->associations) {
                        if (!association.positional()) {
                            error_ = "class array parameter actual is not a "
                                     "positional pattern";
                            return false;
                        }
                        auto item = evaluate(association.value);
                        if (!item) return false;
                        items.push_back(std::move(*item));
                    }
                    bound_items[index] = std::move(items);
                    continue;
                }
                bound[index] = parameter.type
                    ? evaluate_for_type(*actual.expression, *parameter.type)
                    : evaluate(*actual.expression);
                if (!bound[index]) return false;
            }
        }
        frames_.push_back(Frame { });
        frames_.back().class_owner = &owner;
        for (std::size_t index { }; index < bound.size(); ++index) {
            const auto& parameter = owner.parameters[index];
            if (parameter.type_parameter || array_type(parameter)) continue;
            auto value = std::move(bound[index]);
            if (!value && parameter.default_value) {
                value = parameter.type
                    ? evaluate_for_type(*parameter.default_value,
                          *parameter.type)
                    : evaluate(*parameter.default_value);
            }
            if (!value) {
                if (error_.empty()) {
                    error_ = "class parameter '" + parameter.name
                        + "' has no value";
                }
                frames_.pop_back();
                return false;
            }
            frames_.back().values.insert_or_assign(
                parameter.declaration, std::move(*value));
        }
        for (std::size_t index { }; index < bound.size(); ++index) {
            const auto& parameter = owner.parameters[index];
            const auto type = parameter.type_parameter
                ? std::nullopt
                : array_type(parameter);
            if (!type) continue;
            bool assigned { };
            if (create_local_array(parameter.declaration, *type)) {
                if (auto& items = bound_items[index]) {
                    auto& array = frames_.back().arrays.at(
                        parameter.declaration);
                    assigned = items->size() == array.elements.size();
                    if (!assigned) {
                        error_ = "unpacked array pattern size differs from "
                                 "the array";
                    }
                    for (std::size_t item { };
                        assigned && item < items->size(); ++item) {
                        auto element = convert_hir_systemverilog_constant(
                            std::move((*items)[item]), array.element_type,
                            error_, &specialization_);
                        assigned = element.has_value();
                        if (assigned) {
                            array.elements[item] = std::move(*element);
                        }
                    }
                } else if (parameter.default_value) {
                    assigned = assign_local_array(parameter.declaration,
                                   *parameter.default_value)
                                   .value_or(false);
                }
            }
            if (!assigned) {
                if (error_.empty()) {
                    error_ = "class parameter '" + parameter.name
                        + "' has no value";
                }
                frames_.pop_back();
                return false;
            }
        }
        return true;
    }

    // A class's local parameter or enumeration literal (`C::E_A`) has its
    // initializer's value (IEEE 1800-2017 8.25.1, 6.19); a parameter of a
    // parameterized class has a value only in a specialization.
    [[nodiscard]] std::optional<Value> evaluate_class_parameter(
        const semantic::sv::Expression& record,
        semantic::sv::TypeReference* const type = nullptr)
    {
        constexpr std::string_view prefix { "@sv-static-property:" };
        const auto identity = !record.class_member_identity.empty()
            ? std::string_view { record.class_member_identity }
            : std::string_view { record.text }.starts_with(prefix)
            ? std::string_view { record.text }.substr(prefix.size())
            : std::string_view { };
        const auto cut = identity.rfind("::");
        const auto member = cut == std::string_view::npos
            ? identity : identity.substr(cut + 2U);
        const semantic::sv::ClassDeclaration* owner { };
        const semantic::sv::ClassProperty* property { };
        const semantic::sv::ClassParameter* formal { };
        for (const auto& candidate :
            specialization_.design().systemverilog_hir.classes()) {
            if (candidate.forward_declaration) continue;
            if (const auto found = std::ranges::find(candidate.properties,
                    identity,
                    &semantic::sv::ClassProperty::canonical_identity);
                found != candidate.properties.end()) {
                owner = &candidate;
                property = &*found;
                break;
            }
            if (cut != std::string_view::npos
                && identity.substr(0U, cut) == candidate.canonical_identity) {
                if (const auto found = std::ranges::find(
                        candidate.parameters, member,
                        &semantic::sv::ClassParameter::name);
                    found != candidate.parameters.end()
                    && !found->type_parameter) {
                    owner = &candidate;
                    formal = &*found;
                    break;
                }
            }
        }
        if (sv_const_trace()) {
            std::fprintf(stderr, "FSIM_SV_CONST class constant '%s' %s\n",
                std::string { identity }.c_str(),
                owner == nullptr ? "has no owner" : "found");
        }
        if (owner == nullptr
            || (property != nullptr
                && (!property->parameter || !property->initializer))) {
            error_ = "class member '" + std::string { identity }
                + "' is not a class constant";
            return std::nullopt;
        }
        if (type != nullptr) {
            if (property != nullptr) {
                *type = property->type;
            } else if (formal->type) {
                *type = *formal->type;
            }
        }
        if (owner->parameters.empty() && property != nullptr) {
            return evaluate(*property->initializer);
        }
        // A value parameter retained as a constant property takes the
        // specialization's actual, not its default (IEEE 1800-2017 8.25).
        if (formal == nullptr) {
            if (const auto found = std::ranges::find(owner->parameters,
                    member, &semantic::sv::ClassParameter::name);
                found != owner->parameters.end() && !found->type_parameter) {
                formal = &*found;
            }
        }
        const auto* selection = record.class_owner.empty()
            ? nullptr : &record.class_owner.front();
        if (!push_class_frame(*owner, selection)) {
            return std::nullopt;
        }
        struct Pop {
            std::vector<Frame>& frames;
            ~Pop() { frames.pop_back(); }
        } pop { frames_ };
        if (formal != nullptr) {
            return frames_.back().values.at(formal->declaration);
        }
        auto value = value_for_declaration(property->declaration);
        if (sv_const_trace()) {
            std::fprintf(stderr,
                "FSIM_SV_CONST class constant '%s' = %s (%s)\n",
                std::string { identity }.c_str(),
                value ? value->canonical().c_str() : "none",
                error_.c_str());
        }
        return value;
    }

    [[nodiscard]] std::optional<Value> evaluate_impl(
        const semantic::ExpressionId expression)
    {
        const auto view = specialization_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.decimal_literal) {
            return evaluate_decimal(record);
        }
        if (record.kind == ExpressionKind::class_null
            || record.text == "@sv-null") {
            return chandle_value(0U);
        }
        if (record.kind == ExpressionKind::boolean_literal) {
            return make_known(
                record.text == "true" || record.text == "1" ? 1U : 0U,
                1U, false, false);
        }
        if (record.kind == ExpressionKind::integer_literal
            || record.kind == ExpressionKind::logic_literal) {
            return literal_value(record,
                record.kind == ExpressionKind::integer_literal, error_);
        }
        if (record.kind == ExpressionKind::string_literal
            && record.decoded_string) {
            // A string literal in an integral context is an unsigned packed
            // value of eight bits per character, the last character in the
            // low byte (IEEE 1800-2017 5.9, 11.10); "" is one NUL byte.
            const auto& text = *record.decoded_string;
            const auto characters = std::max<std::size_t>(text.size(), 1U);
            if (characters > hir_systemverilog_maximum_constant_width / 8U) {
                error_ = "string literal exceeds the constant width limit";
                return std::nullopt;
            }
            auto packed = PackedLogic4 { characters * 8U, Logic4::zero };
            for (std::size_t at = 0; at < text.size(); ++at) {
                const auto character = static_cast<unsigned char>(
                    text[text.size() - 1U - at]);
                for (std::size_t bit = 0; bit < 8U; ++bit) {
                    if (((character >> bit) & 1U) != 0U) {
                        packed.set(at * 8U + bit, Logic4::one);
                    }
                }
            }
            return make_value(std::move(packed), false, false);
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
        if (record.kind == ExpressionKind::class_static_property) {
            return evaluate_class_parameter(record);
        }
        error_ = "expression form is not a supported SystemVerilog integral "
                 "constant expression";
        return std::nullopt;
    }

    // The context width an enclosing operator gives an operand.
    [[nodiscard]] std::optional<std::uint32_t> take_context(
        const semantic::ExpressionId id) const
    {
        const auto* found = contexts_.find(id.value());
        return found == nullptr ? std::nullopt : std::optional { *found };
    }

    /// Context widths of the operands being evaluated, by expression: a
    /// few entries at a time, one per enclosing operator level.
    class OperandContexts {
    public:
        std::uint32_t& operator[](const std::uint32_t expression)
        {
            for (auto& entry : entries_) {
                if (entry.first == expression) {
                    return entry.second;
                }
            }
            return entries_.emplace_back(expression, 0U).second;
        }
        void erase(const std::uint32_t expression)
        {
            for (auto& entry : entries_) {
                if (entry.first == expression) {
                    entry = entries_.back();
                    entries_.pop_back();
                    return;
                }
            }
        }
        [[nodiscard]] const std::uint32_t* find(
            const std::uint32_t expression) const
        {
            for (const auto& entry : entries_) {
                if (entry.first == expression) {
                    return &entry.second;
                }
            }
            return nullptr;
        }

    private:
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries_;
    };

    const semantic::SpecializedHirUnit& specialization_;
    std::string& error_;
    std::vector<std::pair<std::size_t, std::uint32_t>> active_expressions_;
    std::unordered_set<std::uint32_t> active_declarations_;
    OperandContexts contexts_;
    std::vector<Frame> frames_;
    std::uint64_t work_units_used_ { };
};

/// The scalar view of a constant: a real-family or chandle value as it is,
/// an integral value as a signed 64-bit integer (x and z bits as 0).
[[nodiscard]] std::optional<Scalar> scalar_of(const Value& value,
    const ScalarKind kind, std::string& error)
{
    if (value.scalar) {
        return value.scalar;
    }
    if (value.unbounded) {
        error = "symbolic unbounded '$' is not a scalar constant";
        return std::nullopt;
    }
    const auto integer = numeric_integer(value);
    if (integer < std::numeric_limits<std::int64_t>::min()
        || integer > std::numeric_limits<std::int64_t>::max()) {
        error = "integral constant is outside the signed 64-bit scalar range";
        return std::nullopt;
    }
    return scalar_integer(integer.convert_to<std::int64_t>(),
        kind == ScalarKind::Time ? ScalarKind::Time : ScalarKind::None);
}

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
    if (semantic::sv::is_expression_size_cast(record)) {
        // The packed constant evaluator validates the operand and applies
        // the expression-sized cast's target width.
        return false;
    }
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
            || record.text == "@sv-cast:chandle"
            || real_math_function(record.text))) {
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

bool hir_systemverilog_constant_expression(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression)
{
    std::unordered_set<std::uint32_t> active;
    const auto is_constant = [&](const auto& self,
                                 const semantic::ExpressionId id,
                                 const std::size_t depth) -> bool {
        if (depth
                > specialization.design().systemverilog_hir.expressions().size()
            || !active.insert(id.value()).second) {
            return false;
        }
        const auto view = specialization.find_expression(id);
        bool result { };
        if (view && view->systemverilog != nullptr) {
            const auto& source = *view->systemverilog;
            const auto operands_are_constant = [&] {
                return std::ranges::all_of(source.operands,
                    [&](const semantic::ExpressionId operand) {
                        return self(self, operand, depth + 1U);
                    });
            };
            const auto call_arguments_are_constant = [&] {
                return std::ranges::all_of(source.call_arguments,
                    [&](const semantic::sv::CallAssociation& argument) {
                        return argument.actual
                            && self(self, *argument.actual, depth + 1U);
                    });
            };
            switch (source.kind) {
            case ExpressionKind::integer_literal:
                result = !source.decimal_literal
                    || source.decimal_literal->kind
                        != semantic::sv::DecimalLiteralKind::real;
                break;
            case ExpressionKind::boolean_literal:
            case ExpressionKind::logic_literal:
                result = true;
                break;
            case ExpressionKind::name:
                // A genvar of an enclosing loop generate is a constant of
                // the occurrence being elaborated (IEEE 1800-2017 27.4).
                result = (source.referenced_name
                             && source.referenced_name->selected
                             && systemverilog_constant_declaration(
                                 specialization,
                                 *source.referenced_name->selected))
                    || std::ranges::any_of(
                        specialization.specialization().hierarchy_identities,
                        [&](const semantic::SpecializedHirNamedIdentity&
                                identity) {
                            return identity.name == source.text;
                        });
                break;
            case ExpressionKind::unary:
            case ExpressionKind::binary:
            case ExpressionKind::index:
            case ExpressionKind::slice:
            case ExpressionKind::concatenation:
            case ExpressionKind::replication:
                result = !source.operands.empty()
                    && operands_are_constant();
                break;
            // A class's parameter or local parameter (8.25.1).
            case ExpressionKind::class_static_property: {
                constexpr std::string_view prefix { "@sv-static-property:" };
                const auto identity = !source.class_member_identity.empty()
                    ? std::string_view { source.class_member_identity }
                    : std::string_view { source.text }.starts_with(prefix)
                    ? std::string_view { source.text }.substr(prefix.size())
                    : std::string_view { };
                const auto cut = identity.rfind("::");
                for (const auto& owner :
                    specialization.design().systemverilog_hir.classes()) {
                    if (owner.forward_declaration) continue;
                    if (std::ranges::any_of(owner.properties,
                            [&](const semantic::sv::ClassProperty& property) {
                                return property.parameter
                                    && property.canonical_identity
                                        == identity;
                            })
                        || (cut != std::string_view::npos
                            && identity.substr(0U, cut)
                                == owner.canonical_identity
                            && std::ranges::any_of(owner.parameters,
                                [&](const semantic::sv::ClassParameter&
                                        parameter) {
                                    return !parameter.type_parameter
                                        && parameter.name
                                            == identity.substr(cut + 2U);
                                }))) {
                        result = true;
                        break;
                    }
                }
                break;
            }
            case ExpressionKind::call: {
                const auto call_arguments_are_valid
                    = operands_are_constant()
                    && call_arguments_are_constant();
                const bool sized_type_cast
                    = source.text.starts_with("@sv-cast:")
                    && source.operands.size() == 1U;
                const bool expression_size_cast
                    = semantic::sv::is_expression_size_cast(source);
                const bool conditional
                    = source.text == "?:"
                    && source.operands.size() == 3U;
                const bool constant_query
                    = source.text == "$bits"
                    || source.text == "$left"
                    || source.text == "$right"
                    || source.text == "$low"
                    || source.text == "$high"
                    || source.text == "$size"
                    || source.text == "$increment"
                    || source.text == "$dimensions"
                    || source.text == "$unpacked_dimensions";
                const bool constant_function
                    = source.referenced_name
                    && source.referenced_name->selected
                    && [&] {
                           const auto declaration
                               = specialization.find_declaration(
                                   *source.referenced_name->selected);
                           return declaration
                               && declaration->systemverilog != nullptr
                               && declaration->systemverilog->callable
                               && declaration->systemverilog->callable
                                      ->function;
                       }();
                result = constant_query
                    || ((sized_type_cast || expression_size_cast
                            || conditional || source.text == "$clog2"
                            || source.text == "$isunknown"
                            || source.text == "$isunbounded"
                            || source.text == "inside"
                            || constant_function)
                        && call_arguments_are_valid);
                break;
            }
            default:
                break;
            }
        }
        active.erase(id.value());
        return result;
    };
    return is_constant(is_constant, expression, 0U);
}

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
    if (scalar) {
        return scalar->display();
    }
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
    if (scalar) {
        return scalar->canonical();
    }
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
    auto value = evaluate_hir_systemverilog_value(
        specialization, expression, error);
    if (value && value->scalar) {
        error = "a real-family or chandle value is not an integral constant";
        return std::nullopt;
    }
    return value;
}

std::optional<HirSystemVerilogConstant>
evaluate_hir_systemverilog_value(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    std::string& error)
{
    error.clear();
    // Evaluation depends only on the specialization and the expression;
    // instances sharing a specialization evaluate each expression once. A
    // unit-scope expression lexically reaches no generate-local name, so a
    // value of one that read no hierarchy identity holds for every
    // occurrence derived from the specialization.
    auto& constants = *specialization_cache(specialization).constants;
    auto& results = constants.expressions;
    if (const auto found = results.find(expression.value());
        found != results.end()) {
        error = found->second.second;
        return found->second.first;
    }
    auto& unit_values = *constants.unit_expressions;
    if (const auto found = unit_values.find(expression.value());
        found != unit_values.end()) {
        return found->second;
    }
    HirConstantEvaluator evaluator { specialization, error };
    auto value = evaluator.evaluate(expression);
    const auto view = specialization.find_expression(expression);
    if (value && evaluator.hierarchy_reads() == 0U && view
        && view->systemverilog != nullptr
        && view->systemverilog->scope == specialization.scope()) {
        unit_values.emplace(expression.value(), *value);
    } else {
        results.emplace(expression.value(), std::pair { value, error });
    }
    return value;
}

std::optional<HirSystemVerilogArrayConstant>
evaluate_hir_systemverilog_array_constant(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    std::string& error)
{
    error.clear();
    auto value = HirConstantEvaluator { specialization, error }
                     .evaluate_unpacked_array(expression);
    if (!value) {
        return std::nullopt;
    }
    return HirSystemVerilogArrayConstant {
        value->left, value->right, std::move(value->elements)
    };
}

std::optional<std::uint64_t> hir_systemverilog_type_bits(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& type)
{
    std::unordered_set<std::uint32_t> visiting;
    const auto width = resolved_type_width(type, &specialization, visiting);
    return width && *width != 0U ? width : std::nullopt;
}

std::optional<HirSystemVerilogConstant>
evaluate_hir_systemverilog_typed_constant(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    const semantic::sv::TypeReference& type,
    std::string& error)
{
    error.clear();
    auto value = HirConstantEvaluator { specialization, error }
                     .evaluate_for_type(expression, type);
    if (value && value->scalar) {
        error = "a real-family or chandle value is not an integral constant";
        return std::nullopt;
    }
    return value;
}

std::optional<HirSystemVerilogAggregateSlice>
hir_systemverilog_aggregate_slice(
    const semantic::SpecializedHirUnit& owner,
    const semantic::sv::TypeReference& type,
    const std::string_view path)
{
    std::string error;
    return HirConstantEvaluator { owner, error }.aggregate_slice(type, path);
}

bool hir_systemverilog_unpacked_aggregate_type(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& type)
{
    std::string error;
    return HirConstantEvaluator { specialization, error }
        .unpacked_aggregate_type(type);
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
    const auto value = evaluate_hir_systemverilog_value(
        specialization, expression, error);
    return value ? scalar_of(*value, ScalarKind::None, error) : std::nullopt;
}

std::optional<HirSystemVerilogConstant>
evaluate_hir_systemverilog_declaration_value(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::DeclarationId declaration,
    std::string& error)
{
    error.clear();
    return HirConstantEvaluator { specialization, error }
        .evaluate_declaration(declaration);
}

std::optional<HirSystemVerilogConstant>
convert_hir_systemverilog_value_kind(HirSystemVerilogConstant value,
    const frontend::SystemVerilogScalarKind kind, std::string& error)
{
    return convert_value_kind(std::move(value), kind, error);
}

std::optional<frontend::SystemVerilogScalarConstant>
evaluate_hir_systemverilog_scalar_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::DeclarationId declaration,
    std::string& error)
{
    const auto value = evaluate_hir_systemverilog_declaration_value(
        specialization, declaration, error);
    if (!value) {
        return std::nullopt;
    }
    const auto view = specialization.find_declaration(declaration);
    const auto kind = view && view->systemverilog != nullptr
            && view->systemverilog->type
        ? scalar_kind(*view->systemverilog->type)
        : ScalarKind::None;
    return scalar_of(*value, kind, error);
}

std::optional<HirSystemVerilogConstant>
convert_hir_systemverilog_constant(
    HirSystemVerilogConstant value,
    const semantic::sv::TypeReference& type,
    std::string& error,
    const semantic::SpecializedHirUnit* const specialization)
{
    std::unordered_set<std::uint32_t> visiting;
    return convert_constant_to_width(std::move(value), type, error,
        resolved_type_width(type, specialization, visiting));
}


namespace {

// Generate selection in a specialization reaches this evaluator where its
// integral fast path stops (IEEE 1800-2017 27.5).
[[maybe_unused]] const bool shared_constant_integral_registered = [] {
    semantic::set_systemverilog_constant_integral(
        [](const semantic::SpecializedHirUnit& unit,
            const semantic::ExpressionId expression)
            -> std::optional<std::int64_t> {
            std::string error;
            const auto value = evaluate_hir_systemverilog_constant(
                unit, expression, error);
            return value ? value->integer_value() : std::nullopt;
        });
    return true;
}();

// An element of an unpacked array parameter actual whose elements are not
// all 64-bit integers: a string, a real-family value, or a pattern in the
// formal's element type (IEEE 1800-2017 6.20.2, 10.9).
[[maybe_unused]] const bool shared_constant_element_registered = [] {
    semantic::set_systemverilog_constant_element(
        [](const semantic::SpecializedHirUnit& unit,
            const semantic::ExpressionId expression,
            const semantic::sv::TypeReference& element_type)
            -> std::optional<std::string> {
            if (element_type.value_form == semantic::sv::TypeForm::string
                || element_type.target.spelling == "string") {
                const auto text = unit.evaluate_string_expression(expression);
                return text ? std::optional {
                                  semantic::systemverilog_string_identity(
                                      *text) }
                            : std::nullopt;
            }
            std::string error;
            const auto view = unit.find_expression(expression);
            const auto value = view && view->systemverilog != nullptr
                    && view->systemverilog->kind
                        == ExpressionKind::assignment_pattern
                ? evaluate_hir_systemverilog_typed_constant(
                      unit, expression, element_type, error)
                : evaluate_hir_systemverilog_value(unit, expression, error);
            return value ? std::optional { value->canonical() }
                         : std::nullopt;
        });
    return true;
}();

// A constant function call as an unpacked array parameter actual
// (IEEE 1800-2017 13.4.3, 23.10).
[[maybe_unused]] const bool shared_constant_array_registered = [] {
    semantic::set_systemverilog_constant_array(
        [](const semantic::SpecializedHirUnit& unit,
            const semantic::ExpressionId expression)
            -> std::optional<std::vector<std::int64_t>> {
            std::string error;
            const auto array = evaluate_hir_systemverilog_array_constant(
                unit, expression, error);
            if (!array) {
                return std::nullopt;
            }
            std::vector<std::int64_t> values;
            values.reserve(array->elements.size());
            for (const auto& element : array->elements) {
                const auto value = element.integer_value();
                if (!value) {
                    return std::nullopt;
                }
                values.push_back(*value);
            }
            return values;
        });
    return true;
}();

} // namespace

} // namespace fsim::elaboration
