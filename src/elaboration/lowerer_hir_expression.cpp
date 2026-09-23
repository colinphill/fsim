// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "lowerer_internal.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/version.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>

namespace fsim::elaboration {

std::optional<double> systemverilog_real_literal(
    const std::string_view spelling) noexcept
{
    if (spelling.find_first_of(".eE") == std::string_view::npos) {
        return std::nullopt;
    }
    std::string normalized;
    normalized.reserve(spelling.size());
    std::ranges::copy_if(
        spelling, std::back_inserter(normalized), [](const char value) {
            return value != '_';
        });
    double value { };
    const auto [end, error] = std::from_chars(
        normalized.data(),
        normalized.data() + normalized.size(),
        value,
        std::chars_format::general);
    if (error != std::errc { }
        || end != normalized.data() + normalized.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<runtime::simir::RandomDistributionKind>
systemverilog_random_distribution_kind(
    const std::string_view spelling) noexcept
{
    using Kind = runtime::simir::RandomDistributionKind;
    if (spelling == "$dist_uniform") {
        return Kind::uniform;
    }
    if (spelling == "$dist_normal") {
        return Kind::normal;
    }
    if (spelling == "$dist_exponential") {
        return Kind::exponential;
    }
    if (spelling == "$dist_poisson") {
        return Kind::poisson;
    }
    if (spelling == "$dist_chi_square") {
        return Kind::chi_square;
    }
    if (spelling == "$dist_t") {
        return Kind::student_t;
    }
    if (spelling == "$dist_erlang") {
        return Kind::erlang;
    }
    return std::nullopt;
}

bool systemverilog_sampled_value_call(
    const std::string_view spelling) noexcept
{
    return spelling == "$sampled" || spelling == "$rose"
        || spelling == "$fell" || spelling == "$stable"
        || spelling == "$changed" || spelling == "$past"
        || spelling == "$past_gclk" || spelling == "$rose_gclk"
        || spelling == "$fell_gclk" || spelling == "$stable_gclk"
        || spelling == "$changed_gclk" || spelling == "$future_gclk"
        || spelling == "$rising_gclk" || spelling == "$falling_gclk"
        || spelling == "$steady_gclk" || spelling == "$changing_gclk";
}

bool systemverilog_sampled_value_preserves_signal(
    const std::string_view spelling) noexcept
{
    return spelling == "$sampled" || spelling == "$past"
        || spelling == "$past_gclk" || spelling == "$future_gclk";
}

std::optional<std::string_view> vhdl_logic_string_function_name(
    const std::string_view spelling) noexcept
{
    const auto separator = spelling.find_last_of('.');
    const auto name = spelling.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
    if (name == "to_string" || name == "to_bstring"
        || name == "to_binary_string" || name == "to_ostring"
        || name == "to_octal_string" || name == "to_hstring"
        || name == "to_hex_string") {
        return name;
    }
    return std::nullopt;
}

RegisterId Lowerer::widen_enumeration_ordinal(const RegisterId source)
{
    const auto source_width = register_width(source);
    const auto destination_width
        = language_ == frontend::Language::Vhdl2008
        ? static_cast<std::size_t>(
              frontend::vhdl_predefined_integer_storage_width(
                  vhdl_standard_))
        : std::size_t { 32U };
    if (source_width == 1U
        && register_domain(source) == frontend::ValueDomain::Logic9) {
        auto ordinal = allocate_register(
            destination_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            ordinal, unsigned_value(0U, destination_width) });
        for (std::uint8_t state = 1U;
            state <= static_cast<std::uint8_t>(
                runtime::Logic9::dont_care);
            ++state) {
            const auto literal = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(LoadConstant {
                literal,
                PackedLogic4::from_logic9_msb_string(std::string(
                    1U,
                    runtime::to_char(
                        static_cast<runtime::Logic9>(state)))),
            });
            const auto matches = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                matches,
                source,
                literal,
            });
            const auto candidate = allocate_register(
                destination_width, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                candidate,
                unsigned_value(state, destination_width),
            });
            const auto selected = allocate_register(
                destination_width, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(ConditionalSelect {
                selected,
                matches,
                candidate,
                ordinal,
            });
            ordinal = selected;
        }
        return ordinal;
    }
    if (source_width > destination_width) {
        return source;
    }
    const auto destination = allocate_register(
        destination_width, frontend::ValueDomain::Integer);
    if (source_width == destination_width) {
        process_.operations.emplace_back(CopyRegister {
            destination,
            source,
        });
        return destination;
    }
    const auto padding = allocate_register(
        destination_width - source_width,
        frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant {
        padding,
        PackedLogic4(
            destination_width - source_width, Logic4::zero),
    });
    process_.operations.emplace_back(Concatenate {
        destination,
        { padding, source },
        static_cast<std::uint32_t>(destination_width),
    });
    return destination;
}

namespace {

    std::optional<std::int64_t> hir_systemverilog_range_bound(
        const semantic::SpecializedHirUnit& specialization,
        const std::optional<std::int64_t> value,
        const std::optional<semantic::ExpressionId> expression)
    {
        if (value) {
            return value;
        }
        return expression
            ? specialization.evaluate_integral_expression(*expression)
            : std::nullopt;
    }

    std::string hir_systemverilog_type_name(
        const semantic::SpecializedHirUnit& specialization,
        semantic::sv::TypeReference type,
        const std::size_t depth = 0U)
    {
        if (depth > 32U) {
            return { };
        }
        if (type.virtual_interface && !type.interface_type.empty()) {
            auto result = std::string { "virtual " } + type.interface_type;
            if (!type.interface_modport.empty()) {
                result += "." + type.interface_modport;
            }
            return result;
        }
        if (!type.class_identity.empty()) {
            auto result = type.target.spelling.empty()
                ? type.class_identity
                : type.target.spelling;
            if (const auto separator = result.rfind("::");
                separator != std::string::npos) {
                result.erase(0U, separator + 2U);
            }
            return result;
        }

        if (type.container_form) {
            auto element_type = type;
            if (!type.container_element_types.empty()) {
                element_type = type.container_element_types.front();
            } else {
                element_type.container_form.reset();
                element_type.queue_maximum.reset();
                element_type.associative_index.reset();
                element_type.unpacked_dimensions.clear();
                element_type.container_element_types.clear();
            }
            auto result = hir_systemverilog_type_name(
                specialization, std::move(element_type), depth + 1U);
            if (result.empty()) {
                return { };
            }
            switch (*type.container_form) {
            case semantic::sv::TypeForm::dynamic_array:
                result += "[]";
                break;
            case semantic::sv::TypeForm::queue:
                result += "[$";
                if (type.queue_maximum) {
                    const auto maximum
                        = specialization.evaluate_integral_expression(
                            *type.queue_maximum);
                    if (!maximum) {
                        return { };
                    }
                    result += ":" + std::to_string(*maximum);
                }
                result += "]";
                break;
            case semantic::sv::TypeForm::associative_array:
                result += "[";
                result += type.associative_index
                        && !type.associative_index->spelling.empty()
                    ? type.associative_index->spelling
                    : "*";
                result += "]";
                break;
            case semantic::sv::TypeForm::static_array:
                for (const auto& range : type.unpacked_dimensions) {
                    const auto left = hir_systemverilog_range_bound(
                        specialization,
                        range.left,
                        range.left_expression);
                    const auto right = hir_systemverilog_range_bound(
                        specialization,
                        range.right,
                        range.right_expression);
                    if (!left || !right) {
                        return { };
                    }
                    result += "[" + std::to_string(*left) + ":"
                        + std::to_string(*right) + "]";
                }
                break;
            default:
                return { };
            }
            return result;
        }

        if (type.target.target.valid()) {
            const auto definition = specialization.find_type(
                type.target.target);
            if (definition && definition->systemverilog != nullptr
                && (definition->systemverilog->base.target.target
                        != type.target.target
                    || definition->systemverilog->base.target.spelling
                        != type.target.spelling)) {
                return hir_systemverilog_type_name(
                    specialization,
                    definition->systemverilog->base,
                    depth + 1U);
            }
        }
        if (type.value_form == semantic::sv::TypeForm::string) {
            return "string";
        }
        auto result = type.target.spelling;
        if (result.empty()) {
            switch (type.value_form.value_or(
                semantic::sv::TypeForm::unresolved)) {
            case semantic::sv::TypeForm::packed_integral:
                result = "logic";
                break;
            case semantic::sv::TypeForm::class_handle:
                result = type.class_identity;
                break;
            default:
                break;
            }
        }
        if (result.empty()) {
            return { };
        }
        const bool atomic = result == "byte" || result == "shortint"
            || result == "int" || result == "longint"
            || result == "integer" || result == "time"
            || result == "shortreal" || result == "real"
            || result == "realtime" || result == "chandle"
            || result == "process" || result == "string";
        if (type.signed_value
            && (result == "bit" || result == "logic"
                || result == "reg")) {
            result += " signed";
        }
        if (type.packed_range && !atomic) {
            const auto left = hir_systemverilog_range_bound(
                specialization,
                type.packed_range->left,
                type.packed_range->left_expression);
            const auto right = hir_systemverilog_range_bound(
                specialization,
                type.packed_range->right,
                type.packed_range->right_expression);
            if (!left || !right) {
                return { };
            }
            result += "[" + std::to_string(*left) + ":"
                + std::to_string(*right) + "]";
        }
        return result;
    }

    [[nodiscard]] PackedLogic4 resize_hir_constant(
        const HirSystemVerilogConstant& value,
        const std::size_t width)
    {
        if (value.packed.width() == width) {
            return value.packed;
        }
        const auto logic9 = value.packed.is_logic9();
        auto result = PackedLogic4(width, Logic4::zero);
        if (logic9) {
            result = result.promoted_to_logic9();
        }
        if (width > value.packed.width() && value.signed_value
            && value.packed.width() != 0U) {
            const auto sign = value.packed.get_logic9(
                value.packed.width() - 1U);
            if (logic9) {
                result.fill(sign);
            } else {
                result.fill(runtime::to_logic4(sign));
            }
        }
        const auto copied = std::min(width, value.packed.width());
        if (copied != 0U) {
            result.insert_bits(
                value.packed.extract_bits(0U, copied), 0U);
        }
        return result;
    }

    struct LoweredLiteral {
        PackedLogic4 value;
        frontend::ValueDomain domain { frontend::ValueDomain::Bit2 };
    };

    struct HirExpressionLeaf {
        std::string_view text;
        semantic::SourceSpanId source;
        bool integer { };
        bool boolean { };
        bool logic { };
        bool unary { };
        bool binary { };
        bool index { };
        bool slice { };
        bool concatenation { };
        bool replication { };
        bool conditional { };
        bool call { };
        std::span<const semantic::ExpressionId> operands;
    };

    bool hir_reduction_operator(
        const std::string_view operation) noexcept
    {
        return operation == "&" || operation == "|"
            || operation == "^" || operation == "~&"
            || operation == "~|" || operation == "~^"
            || operation == "^~" || operation == "and"
            || operation == "or" || operation == "nand"
            || operation == "nor" || operation == "xor"
            || operation == "xnor";
    }

    HirExpressionLeaf expression_leaf(
        const semantic::CompiledExpressionView expression)
    {
        HirExpressionLeaf result;
        if (expression.systemverilog != nullptr) {
            const auto& source = *expression.systemverilog;
            result.text = source.text;
            result.source = source.source;
            result.integer = source.kind
                == semantic::sv::ExpressionKind::integer_literal;
            result.boolean = source.kind
                == semantic::sv::ExpressionKind::boolean_literal;
            result.logic = source.kind
                == semantic::sv::ExpressionKind::logic_literal;
            result.unary = source.kind == semantic::sv::ExpressionKind::unary;
            result.binary = source.kind == semantic::sv::ExpressionKind::binary;
            result.index = source.kind == semantic::sv::ExpressionKind::index;
            result.slice = source.kind == semantic::sv::ExpressionKind::slice;
            result.concatenation = source.kind
                == semantic::sv::ExpressionKind::concatenation;
            result.replication = source.kind
                == semantic::sv::ExpressionKind::replication;
            result.conditional = source.kind == semantic::sv::ExpressionKind::call
                && source.text == "?:";
            result.call = source.kind == semantic::sv::ExpressionKind::call
                && !result.conditional;
            result.operands = source.operands;
            return result;
        }
        const auto& source = *expression.vhdl;
        result.text = source.text;
        result.source = source.source;
        result.integer = source.kind
            == semantic::vhdl::ExpressionKind::integer_literal;
        result.boolean = source.kind
            == semantic::vhdl::ExpressionKind::boolean_literal;
        result.logic = source.kind
            == semantic::vhdl::ExpressionKind::logic_literal;
        result.unary = source.kind == semantic::vhdl::ExpressionKind::unary;
        result.binary = source.kind == semantic::vhdl::ExpressionKind::binary;
        result.index = source.kind == semantic::vhdl::ExpressionKind::index;
        result.slice = source.kind == semantic::vhdl::ExpressionKind::slice;
        result.concatenation = source.kind
            == semantic::vhdl::ExpressionKind::concatenation;
        result.replication = source.kind
            == semantic::vhdl::ExpressionKind::replication;
        result.conditional = source.kind
                == semantic::vhdl::ExpressionKind::conditional
            || (source.kind == semantic::vhdl::ExpressionKind::call
                && source.text == "?:");
        result.call = source.kind == semantic::vhdl::ExpressionKind::call
            && !result.conditional;
        result.operands = source.operands;
        return result;
    }

    std::optional<std::string_view>
    vhdl_signal_attribute_diagnostic_code(
        const std::string_view spelling) noexcept
    {
        if (spelling == "'driving_value") {
            return "FSIM-ELAB-VHATTR-003";
        }
        if (spelling == "'stable") {
            return "FSIM-ELAB-VHATTR-004";
        }
        if (spelling == "'quiet") {
            return "FSIM-ELAB-VHATTR-005";
        }
        if (spelling == "'transaction") {
            return "FSIM-ELAB-VHATTR-006";
        }
        if (spelling == "'delayed") {
            return "FSIM-ELAB-VHATTR-007";
        }
        return std::nullopt;
    }

    std::optional<std::string> expand_based_digits(
        const std::string_view digits,
        const char base)
    {
        const auto bits_per_digit = base == 'b' ? 1U
            : base == 'o'                       ? 3U
            : base == 'h'                       ? 4U
                                                : 0U;
        if (bits_per_digit == 0U) {
            return std::nullopt;
        }
        std::string expanded;
        expanded.reserve(digits.size() * bits_per_digit);
        for (const char raw : digits) {
            if (raw == '_') {
                continue;
            }
            const auto digit = static_cast<char>(
                std::tolower(static_cast<unsigned char>(raw)));
            if (digit == 'x' || digit == 'z' || digit == '?') {
                expanded.append(
                    bits_per_digit, digit == '?' ? 'z' : digit);
                continue;
            }
            const auto value = digit >= '0' && digit <= '9'
                ? static_cast<unsigned>(digit - '0')
                : digit >= 'a' && digit <= 'f'
                ? static_cast<unsigned>(digit - 'a' + 10)
                : 16U;
            const auto radix = base == 'b' ? 2U : base == 'o' ? 8U
                                                              : 16U;
            if (value >= radix) {
                return std::nullopt;
            }
            for (auto bit = bits_per_digit; bit != 0U; --bit) {
                expanded.push_back(
                    ((value >> (bit - 1U)) & 1U) != 0U ? '1' : '0');
            }
        }
        return expanded.empty() ? std::nullopt
                                : std::optional { std::move(expanded) };
    }

    std::optional<std::string> expand_decimal_digits(
        const std::string_view digits,
        const std::size_t width)
    {
        std::string decimal;
        decimal.reserve(digits.size());
        for (const char raw : digits) {
            if (raw != '_') {
                decimal.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(raw))));
            }
        }
        if (decimal.empty()) {
            return std::nullopt;
        }
        if (decimal == "x" || decimal == "z" || decimal == "?") {
            return std::string(
                width, decimal == "?" ? 'z' : decimal.front());
        }
        if (!std::ranges::all_of(decimal, [](const char digit) {
                return digit >= '0' && digit <= '9';
            })) {
            return std::nullopt;
        }
        const auto first_nonzero = decimal.find_first_not_of('0');
        decimal = first_nonzero == std::string::npos
            ? "0"
            : decimal.substr(first_nonzero);
        std::string expanded(width, '0');
        for (std::size_t bit = 0; bit < width && decimal != "0"; ++bit) {
            unsigned carry = 0U;
            for (char& digit : decimal) {
                const auto value = carry * 10U
                    + static_cast<unsigned>(digit - '0');
                digit = static_cast<char>('0' + value / 2U);
                carry = value % 2U;
            }
            expanded[width - 1U - bit] = carry != 0U ? '1' : '0';
            const auto next_nonzero = decimal.find_first_not_of('0');
            decimal = next_nonzero == std::string::npos
                ? "0"
                : decimal.substr(next_nonzero);
        }
        return expanded;
    }

    std::optional<LoweredLiteral> based_literal(
        const std::string_view digits,
        const char base,
        const std::size_t width)
    {
        auto expanded = base == 'd'
            ? expand_decimal_digits(digits, width)
            : expand_based_digits(digits, base);
        if (!expanded) {
            return std::nullopt;
        }
        const auto fill = expanded->front() == 'x' ? 'x'
            : expanded->front() == 'z'             ? 'z'
                                                   : '0';
        if (expanded->size() < width) {
            expanded->insert(expanded->begin(), width - expanded->size(), fill);
        } else if (expanded->size() > width) {
            expanded->erase(0, expanded->size() - width);
        }
        try {
            auto value = PackedLogic4::from_msb_string(*expanded);
            const auto four_state = std::ranges::any_of(
                *expanded,
                [](const char bit) { return bit != '0' && bit != '1'; });
            return LoweredLiteral {
                std::move(value),
                four_state ? frontend::ValueDomain::Logic4
                           : frontend::ValueDomain::Bit2,
            };
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

    std::optional<LoweredLiteral> hir_literal(
        const HirExpressionLeaf& expression,
        const std::size_t expected_width,
        const frontend::Language language)
    {
        const auto width = std::max<std::size_t>(expected_width, 1U);
        auto text = expression.text;
        if (expression.boolean) {
            auto normalized = std::string { text };
            std::ranges::transform(normalized, normalized.begin(), [](char value) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(value)));
            });
            if (normalized == "true" || normalized == "false") {
                return LoweredLiteral {
                    PackedLogic4(
                        1U,
                        normalized == "true" ? Logic4::one : Logic4::zero),
                    frontend::ValueDomain::Boolean,
                };
            }
        }
        if (expression.logic && text.size() == 3U
            && text.front() == '\'' && text.back() == '\'') {
            if (language == frontend::Language::Vhdl2008) {
                const auto parsed = runtime::parse_logic9(text[1]);
                if (!parsed) {
                    return std::nullopt;
                }
                return LoweredLiteral {
                    PackedLogic4::from_logic9_msb_string(
                        text.substr(1U, 1U)),
                    frontend::ValueDomain::Logic9,
                };
            }
            const auto parsed = runtime::parse_logic4(text[1]);
            if (!parsed) {
                return std::nullopt;
            }
            return LoweredLiteral {
                PackedLogic4(1U, *parsed),
                *parsed == Logic4::zero || *parsed == Logic4::one
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4,
            };
        }
        const auto quote = text.find('\'');
        if (quote != std::string_view::npos) {
            auto literal_width = width;
            const auto width_text = text.substr(0U, quote);
            if (!width_text.empty()) {
                const auto parsed = unsigned_decimal(width_text);
                if (!parsed || *parsed == 0U
                    || *parsed > std::numeric_limits<std::size_t>::max()) {
                    return std::nullopt;
                }
                literal_width = static_cast<std::size_t>(*parsed);
            }
            auto digits = text.substr(quote + 1U);
            if (!digits.empty()
                && (digits.front() == 's' || digits.front() == 'S')) {
                digits.remove_prefix(1U);
            }
            if (digits.empty()) {
                return std::nullopt;
            }
            if (language == frontend::Language::SystemVerilog2017
                && width_text.empty() && digits.size() == 1U) {
                const auto fill = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(digits.front())));
                const auto value = fill == '0'   ? Logic4::zero
                    : fill == '1'                ? Logic4::one
                    : fill == 'x'                ? Logic4::x
                    : fill == 'z' || fill == '?' ? Logic4::z
                                                 : Logic4::zero;
                if (fill != '0' && fill != '1' && fill != 'x'
                    && fill != 'z' && fill != '?') {
                    return std::nullopt;
                }
                return LoweredLiteral {
                    PackedLogic4(literal_width, value),
                    fill == '0' || fill == '1'
                        ? frontend::ValueDomain::Bit2
                        : frontend::ValueDomain::Logic4,
                };
            }
            const auto base = static_cast<char>(
                std::tolower(static_cast<unsigned char>(digits.front())));
            digits.remove_prefix(1U);
            return based_literal(digits, base, literal_width);
        }
        bool negative = false;
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
            negative = text.front() == '-';
            text.remove_prefix(1U);
        }
        const auto value = unsigned_decimal(text);
        if (value && negative) {
            const auto maximum = static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max());
            if (*value > maximum + 1U) {
                return std::nullopt;
            }
            const auto signed_value = *value == maximum + 1U
                ? std::numeric_limits<std::int64_t>::min()
                : -static_cast<std::int64_t>(*value);
            return LoweredLiteral {
                integer_value(signed_value, width),
                language == frontend::Language::Vhdl2008
                        && expression.integer
                    ? frontend::ValueDomain::Integer
                    : frontend::ValueDomain::Bit2,
            };
        }
        return value
            ? std::optional { LoweredLiteral {
                  unsigned_value(*value, width),
                  language == frontend::Language::Vhdl2008
                          && expression.integer
                      ? frontend::ValueDomain::Integer
                      : frontend::ValueDomain::Bit2,
              } }
            : std::nullopt;
    }

    bool comparison_operator(const std::string_view operation)
    {
        return operation == "=" || operation == "=="
            || operation == "!=" || operation == "/="
            || operation == "===" || operation == "!=="
            || operation == "==?" || operation == "!=?"
            || operation == "?=" || operation == "?/="
            || operation == "<" || operation == "<="
            || operation == ">" || operation == ">=";
    }

    bool shift_operator(const std::string_view operation)
    {
        return operation == "<<" || operation == ">>"
            || operation == "<<<" || operation == ">>>"
            || operation == "sll" || operation == "srl"
            || operation == "sla" || operation == "sra"
            || operation == "rol" || operation == "ror";
    }

    ShiftOperator lowered_shift_operator(
        const std::string_view operation,
        const bool signed_value)
    {
        if (operation == ">>" || operation == "srl"
            || (operation == ">>>" && !signed_value)) {
            return ShiftOperator::logical_right;
        }
        if (operation == ">>>" || operation == "sra") {
            return ShiftOperator::arithmetic_right;
        }
        if (operation == "sla") {
            return ShiftOperator::arithmetic_left;
        }
        if (operation == "rol") {
            return ShiftOperator::rotate_left;
        }
        if (operation == "ror") {
            return ShiftOperator::rotate_right;
        }
        return ShiftOperator::logical_left;
    }

    ExpressionValueDomain profile_domain(const frontend::ValueDomain domain)
    {
        switch (domain) {
        case frontend::ValueDomain::Bit2:
            return ExpressionValueDomain::two_state;
        case frontend::ValueDomain::Logic9:
            return ExpressionValueDomain::nine_state;
        case frontend::ValueDomain::Integer:
            return ExpressionValueDomain::integer;
        case frontend::ValueDomain::Boolean:
            return ExpressionValueDomain::boolean;
        default:
            return ExpressionValueDomain::four_state;
        }
    }

    std::optional<std::size_t> vhdl_subtype_width(
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        auto width = subtype.executable_width;
        if ((!width || *width == 0U)
            && subtype.domain == semantic::vhdl::ValueDomain::integer
            && subtype.integer_storage_width != 0U) {
            width = subtype.integer_storage_width;
        }
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(*width);
    }

    frontend::ValueDomain vhdl_subtype_domain(
        const semantic::vhdl::ValueDomain domain)
    {
        switch (domain) {
        case semantic::vhdl::ValueDomain::bit2:
            return frontend::ValueDomain::Bit2;
        case semantic::vhdl::ValueDomain::logic4:
            return frontend::ValueDomain::Logic4;
        case semantic::vhdl::ValueDomain::logic9:
            return frontend::ValueDomain::Logic9;
        case semantic::vhdl::ValueDomain::boolean:
            return frontend::ValueDomain::Boolean;
        case semantic::vhdl::ValueDomain::integer:
            return frontend::ValueDomain::Integer;
        case semantic::vhdl::ValueDomain::string:
            return frontend::ValueDomain::String;
        case semantic::vhdl::ValueDomain::unknown:
            break;
        }
        return frontend::ValueDomain::Unknown;
    }

    bool same_vhdl_identifier(
        const std::string_view left,
        const std::string_view right)
    {
        if (left.size() != right.size()) {
            return false;
        }
        return std::ranges::equal(
            left, right, [](const char lhs, const char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs))
                    == std::tolower(static_cast<unsigned char>(rhs));
            });
    }

    std::string normalized_vhdl_choice(const std::string_view choice)
    {
        std::string result;
        result.reserve(choice.size());
        for (const char value : choice) {
            if (std::isspace(static_cast<unsigned char>(value)) == 0) {
                result.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(value))));
            }
        }
        return result;
    }

} // namespace

std::optional<DynamicIndex> Lowerer::lower_hir_dynamic_index(
    const semantic::ExpressionId source_id,
    const semantic::ExpressionId index_id,
    const std::size_t source_width,
    const std::uint32_t base_offset)
{
    const auto range = hir_expression_range(source_id, hir_process_scope_);
    const auto index_width = hir_expression_width(
        index_id, hir_process_scope_);
    if (!range || !index_width
        || !hir_dynamic_index_supported(
            source_id, index_id, hir_process_scope_)
        || index_distance(range->left, range->right) + 1U
            != source_width) {
        return std::nullopt;
    }
    const auto lowered = lower_hir_expression(index_id, *index_width);
    if (!lowered) {
        return std::nullopt;
    }
    const auto index_expression = specialized_hir_unit_->find_expression(
        index_id);
    if (!index_expression) {
        return std::nullopt;
    }
    if (index_expression->systemverilog != nullptr) {
        const auto normalized = register_width(*lowered) == 32U
            ? *lowered
            : resize_register(
                  *lowered, 32U, hir_expression_signed(index_id));
        return DynamicIndex {
            normalized,
            range->left,
            range->right,
            base_offset,
            false,
        };
    }

    const auto width = register_width(*lowered);
    const auto direct = width == 32U
        && range->left >= std::numeric_limits<std::int32_t>::min()
        && range->left <= std::numeric_limits<std::int32_t>::max()
        && range->right >= std::numeric_limits<std::int32_t>::min()
        && range->right <= std::numeric_limits<std::int32_t>::max();
    if (direct) {
        return DynamicIndex {
            *lowered,
            range->left,
            range->right,
            base_offset,
            true,
        };
    }
    const auto lower = std::min(range->left, range->right);
    const auto upper = std::max(range->left, range->right);
    process_.operations.emplace_back(IntegerCheck {
        *lowered, lower, upper });
    const auto right = allocate_register(
        width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        right, integer_value(range->right, width) });
    const auto ordinal = allocate_register(
        width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::subtract,
        ordinal,
        range->descending ? *lowered : right,
        range->descending ? right : *lowered,
    });
    const auto maximum_offset = index_distance(
        range->left, range->right);
    process_.operations.emplace_back(IntegerCheck {
        ordinal, 0, static_cast<std::int64_t>(maximum_offset) });
    const auto normalized = resize_register(ordinal, 32U, true);
    return DynamicIndex {
        normalized,
        static_cast<std::int64_t>(maximum_offset),
        0,
        base_offset,
        true,
    };
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_dynamic_element_offset(
    const semantic::ExpressionId source_id,
    const semantic::ExpressionId index_id,
    const std::size_t element_width,
    const std::uint32_t member_offset)
{
    const auto range = hir_expression_range(source_id, hir_process_scope_);
    const auto index_width = hir_expression_width(
        index_id, hir_process_scope_);
    const auto index_domain = hir_expression_domain(
        index_id, hir_process_scope_);
    if (!range || !index_width || *index_width == 0U
        || !index_domain
        || *index_domain != frontend::ValueDomain::Integer
        || (*index_width != 32U && *index_width != 64U)
        || element_width == 0U
        || element_width > std::numeric_limits<std::int64_t>::max()) {
        return std::nullopt;
    }
    auto index = lower_hir_expression(index_id, *index_width);
    if (!index) {
        return std::nullopt;
    }
    const auto arithmetic_width = std::max<std::size_t>(
        register_width(*index), 32U);
    if (register_width(*index) != arithmetic_width) {
        index = resize_register(*index, arithmetic_width, true);
    }
    process_.operations.emplace_back(IntegerCheck {
        *index,
        std::min(range->left, range->right),
        std::max(range->left, range->right),
    });
    const auto right = allocate_register(
        arithmetic_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        right, integer_value(range->right, arithmetic_width) });
    const auto ordinal = allocate_register(
        arithmetic_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::subtract,
        ordinal,
        range->descending ? *index : right,
        range->descending ? right : *index,
    });
    const auto stride = allocate_register(
        arithmetic_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        stride,
        integer_value(
            static_cast<std::int64_t>(element_width), arithmetic_width),
    });
    const auto scaled = allocate_register(
        arithmetic_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::multiply,
        scaled,
        ordinal,
        stride,
    });
    auto offset = scaled;
    if (member_offset != 0U) {
        const auto member = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            member,
            integer_value(
                static_cast<std::int64_t>(member_offset),
                arithmetic_width),
        });
        offset = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            offset,
            scaled,
            member,
        });
    }
    process_.operations.emplace_back(IntegerCheck {
        offset,
        0,
        std::numeric_limits<std::int32_t>::max(),
    });
    return register_width(offset) == 32U
        ? std::optional { offset }
        : std::optional { resize_register(offset, 32U, true) };
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_array_offset(
    const HirVhdlArraySelection& selection)
{
    if (selection.dynamic_indices.empty()
        || selection.offset
            > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    auto arithmetic_width = std::size_t { 32U };
    for (const auto& dimension : selection.dynamic_indices) {
        const auto width = hir_expression_width(
            dimension.expression, hir_process_scope_);
        if (!width || (*width != 32U && *width != 64U)) {
            return std::nullopt;
        }
        arithmetic_width = std::max(arithmetic_width, *width);
    }
    std::optional<RegisterId> offset;
    if (selection.offset != 0U) {
        offset = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            *offset,
            integer_value(
                static_cast<std::int64_t>(selection.offset),
                arithmetic_width),
        });
    }
    for (const auto& dimension : selection.dynamic_indices) {
        const auto index_width = hir_expression_width(
            dimension.expression, hir_process_scope_);
        auto index = lower_hir_expression(
            dimension.expression, index_width.value());
        if (!index) {
            return std::nullopt;
        }
        if (register_width(*index) != arithmetic_width) {
            index = resize_register(*index, arithmetic_width, true);
        }
        process_.operations.emplace_back(IntegerCheck {
            *index,
            std::min(dimension.left, dimension.right),
            std::max(dimension.left, dimension.right),
        });
        const auto right = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            right, integer_value(dimension.right, arithmetic_width) });
        const auto ordinal = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::subtract,
            ordinal,
            dimension.descending ? *index : right,
            dimension.descending ? right : *index,
        });
        const auto stride = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            stride,
            integer_value(
                static_cast<std::int64_t>(dimension.stride),
                arithmetic_width),
        });
        const auto scaled = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            scaled,
            ordinal,
            stride,
        });
        if (!offset) {
            offset = scaled;
            continue;
        }
        const auto combined = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            combined,
            *offset,
            scaled,
        });
        offset = combined;
    }
    if (!offset) {
        return std::nullopt;
    }
    process_.operations.emplace_back(IntegerCheck {
        *offset, 0, std::numeric_limits<std::int32_t>::max() });
    return register_width(*offset) == 32U
        ? offset
        : std::optional { resize_register(*offset, 32U, true) };
}

std::optional<DynamicPartIndex> Lowerer::lower_hir_vhdl_dynamic_slice(
    const semantic::ExpressionId expression_id,
    const std::size_t source_width,
    const std::size_t selected_width,
    const std::uint32_t base_offset)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::slice
        || expression->vhdl->operands.size() != 3U
        || (expression->vhdl->text != "to"
            && expression->vhdl->text != "downto")) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto range = hir_expression_range(
        source.operands.front(), hir_process_scope_);
    const auto descending = source.text == "downto";
    const auto range_width = range
        ? index_distance(range->left, range->right) + 1U
        : 0U;
    if (!range || range_width == 0U
        || range->descending != descending
        || range_width != source_width
        || range_width - 1U
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max())
        || selected_width == 0U
        || selected_width - 1U
            > static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max())) {
        report(
            "FSIM-ELAB-VHSLICE-001",
            "a dynamic VHDL slice requires a nonempty fixed shape and "
            "the direction of its concrete source range",
            hir_source_span(source.source));
        return std::nullopt;
    }
    const auto left_width = hir_expression_width(
        source.operands[1], hir_process_scope_);
    const auto right_width = hir_expression_width(
        source.operands[2], hir_process_scope_);
    const auto left_domain = hir_expression_domain(
        source.operands[1], hir_process_scope_);
    const auto right_domain = hir_expression_domain(
        source.operands[2], hir_process_scope_);
    if (!left_width || !right_width
        || (*left_width != 32U && *left_width != 64U)
        || (*right_width != 32U && *right_width != 64U)
        || left_domain != frontend::ValueDomain::Integer
        || right_domain != frontend::ValueDomain::Integer) {
        report(
            "FSIM-ELAB-VHSLICE-002",
            "dynamic VHDL slice bounds require integer-family expressions",
            hir_source_span(source.source));
        return std::nullopt;
    }
    auto left = lower_hir_expression(source.operands[1], *left_width);
    auto right = lower_hir_expression(source.operands[2], *right_width);
    if (!left || !right) {
        report(
            "FSIM-ELAB-VHSLICE-002",
            "dynamic VHDL slice bounds must lower to signed integer values",
            hir_source_span(source.source));
        return std::nullopt;
    }
    const auto arithmetic_width = std::max<std::size_t>(
        { register_width(*left), register_width(*right), 32U });
    if (register_width(*left) != arithmetic_width) {
        left = resize_register(*left, arithmetic_width, true);
    }
    if (register_width(*right) != arithmetic_width) {
        right = resize_register(*right, arithmetic_width, true);
    }
    const auto lower = std::min(range->left, range->right);
    const auto upper = std::max(range->left, range->right);
    process_.operations.emplace_back(IntegerCheck {
        *left, lower, upper });
    process_.operations.emplace_back(IntegerCheck {
        *right, lower, upper });
    const auto right_bound = allocate_register(
        arithmetic_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        right_bound,
        integer_value(range->right, arithmetic_width),
    });
    const auto normalize = [&](const RegisterId value) {
        const auto ordinal = allocate_register(
            arithmetic_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::subtract,
            ordinal,
            range->descending ? value : right_bound,
            range->descending ? right_bound : value,
        });
        process_.operations.emplace_back(IntegerCheck {
            ordinal,
            0,
            static_cast<std::int64_t>(range_width - 1U),
        });
        return resize_register(ordinal, 32U, true);
    };
    const auto normalized_left = normalize(*left);
    const auto normalized_right = normalize(*right);
    const auto distance = allocate_register(
        32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::subtract,
        distance,
        normalized_left,
        normalized_right,
    });
    const auto required_distance = static_cast<std::int32_t>(
        selected_width - 1U);
    process_.operations.emplace_back(IntegerCheck {
        distance, required_distance, required_distance });
    return DynamicPartIndex {
        normalized_right,
        static_cast<std::int64_t>(range_width - 1U),
        0,
        base_offset,
        static_cast<std::uint32_t>(selected_width),
        true,
        true,
    };
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_attribute(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    HirVhdlAttributeFailure failure { HirVhdlAttributeFailure::none };
    const auto attribute = hir_vhdl_attribute_profile(
        expression_id, hir_process_scope_, &failure);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    const auto report_failure = [&](const std::string_view code,
                                    const std::string_view message) {
        report(std::string { code }, std::string { message }, span);
    };
    if (!attribute) {
        switch (failure) {
        case HirVhdlAttributeFailure::array_prefix:
            report_failure(
                "FSIM-ELAB-VHARRAYATTR-001",
                "VHDL array attribute prefix does not have a concrete "
                "bounded array range");
            break;
        case HirVhdlAttributeFailure::array_dimension:
            report_failure(
                "FSIM-ELAB-VHARRAYATTR-002",
                "VHDL array attribute dimension is nonstatic or "
                "outside the array rank");
            break;
        case HirVhdlAttributeFailure::array_result:
            report_failure(
                "FSIM-ELAB-VHARRAYATTR-004",
                "VHDL array attribute result is outside the portable "
                "signed 32-bit range");
            break;
        case HirVhdlAttributeFailure::scalar_profile:
            report_failure(
                "FSIM-ELAB-VHSCALARATTR-001",
                "VHDL scalar attribute has an invalid prefix, arity, "
                "argument type, or range");
            break;
        case HirVhdlAttributeFailure::scalar_value:
            report_failure(
                "FSIM-ELAB-VHSCALARATTR-002",
                "VHDL scalar attribute argument is outside its subtype "
                "range or has no adjacent value");
            break;
        case HirVhdlAttributeFailure::enumeration_profile:
            if ((source.text == "'left"
                    || source.text == "'right"
                    || source.text == "'low"
                    || source.text == "'high")
                && source.operands.size() != 1U) {
                report_failure(
                    "FSIM-ELAB-VHENUMATTR-001",
                    source.text
                        + " on enumeration type requires 0 arguments");
            } else if (source.text == "'val") {
                report_failure(
                    "FSIM-ELAB-VHENUMATTR-001",
                    "VHDL enumeration 'val requires an integer-family "
                    "argument");
            } else {
                report_failure(
                    "FSIM-ELAB-VHENUMATTR-001",
                    "VHDL enumeration scalar attribute requires a value "
                    "of enumeration type with a matching nominal type");
            }
            break;
        case HirVhdlAttributeFailure::enumeration_value:
            report_failure(
                "FSIM-ELAB-VHENUMATTR-002",
                "VHDL enumeration scalar attribute argument is outside "
                "the type declaration range or has no adjacent value");
            break;
        case HirVhdlAttributeFailure::none:
            break;
        }
        return std::nullopt;
    }
    if (!attribute->array_prefix
        && vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
        && (source.text == "'length" || source.text == "'range"
            || source.text == "'reverse_range")) {
        report_failure(
            "FSIM-ELAB-VHATTR-011",
            "scalar VHDL length and range attributes require VHDL-2019");
        return std::nullopt;
    }
    if (attribute->discrete_range) {
        report_failure(
            attribute->array_prefix
                ? "FSIM-ELAB-VHARRAYATTR-003"
                : "FSIM-ELAB-VHSCALARATTR-003",
            "VHDL range attribute cannot be used as a scalar expression");
        return std::nullopt;
    }
    if (attribute->width == 0U) {
        return std::nullopt;
    }
    const auto result_width = expected_width != 0U
        ? expected_width
        : attribute->width;
    if (attribute->constant) {
        const auto destination = allocate_register(
            result_width, attribute->domain);
        process_.operations.emplace_back(LoadConstant {
            destination,
            integer_value(*attribute->constant, result_width),
        });
        return destination;
    }
    if (!attribute->value || attribute->value_width == 0U
        || !attribute->lower_bound || !attribute->upper_bound) {
        return std::nullopt;
    }
    const auto value = lower_hir_expression(
        *attribute->value, attribute->value_width);
    if (!value || register_width(*value) != attribute->value_width) {
        return std::nullopt;
    }
    const auto value_domain = hir_expression_domain(
        *attribute->value, hir_process_scope_);
    auto ordinal_source = *value;
    if (*attribute->lower_bound == 0
        && *attribute->upper_bound == 1
        && register_domain(ordinal_source)
            == frontend::ValueDomain::Logic9) {
        ordinal_source = convert_to_two_state(ordinal_source);
    }
    const auto ordinal_source_width = register_width(ordinal_source);
    const auto integer_storage = value_domain
            == std::optional { frontend::ValueDomain::Integer }
        && (ordinal_source_width == 32U || ordinal_source_width == 64U);
    auto ordinal = integer_storage
        ? ordinal_source
        : widen_enumeration_ordinal(ordinal_source);
    if (register_domain(ordinal) == frontend::ValueDomain::Logic9
        || register_width(ordinal) > 64U) {
        return std::nullopt;
    }
    if (value_domain == std::optional { frontend::ValueDomain::Integer }
        && attribute->domain == frontend::ValueDomain::Integer
        && attribute->kind != HirVhdlAttributeKind::position
        && register_width(ordinal) != attribute->width) {
        ordinal = resize_register(ordinal, attribute->width, true);
    }
    if (attribute->kind == HirVhdlAttributeKind::position) {
        process_.operations.emplace_back(IntegerCheck {
            ordinal, *attribute->lower_bound, *attribute->upper_bound });
        if (register_width(ordinal) != result_width) {
            ordinal = resize_register(ordinal, result_width, true);
        }
        return ordinal;
    }
    if (attribute->kind == HirVhdlAttributeKind::value) {
        process_.operations.emplace_back(IntegerCheck {
            ordinal, *attribute->lower_bound, *attribute->upper_bound });
        if (attribute->domain == frontend::ValueDomain::Integer) {
            return register_width(ordinal) == result_width
                ? std::optional { ordinal }
                : std::optional {
                      resize_register(ordinal, result_width, true)
                  };
        }
        const auto narrowed = allocate_register(
            attribute->width, attribute->domain);
        process_.operations.emplace_back(Extract {
            narrowed,
            ordinal,
            0U,
            static_cast<std::uint32_t>(attribute->width),
        });
        return result_width != attribute->width
            ? std::optional {
                  resize_register(narrowed, result_width, false)
              }
            : std::optional { narrowed };
    }
    const auto successor
        = attribute->kind == HirVhdlAttributeKind::successor;
    const auto predecessor
        = attribute->kind == HirVhdlAttributeKind::predecessor;
    if ((!successor && !predecessor)
        || *attribute->lower_bound == *attribute->upper_bound) {
        return std::nullopt;
    }
    process_.operations.emplace_back(IntegerCheck {
        ordinal,
        successor ? *attribute->lower_bound
                  : *attribute->lower_bound + 1,
        successor ? *attribute->upper_bound - 1
                  : *attribute->upper_bound,
    });
    const auto ordinal_width = register_width(ordinal);
    const auto one = allocate_register(
        ordinal_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        one, integer_value(1, ordinal_width) });
    const auto adjusted = allocate_register(
        ordinal_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        successor ? IntegerBinaryOperator::add
                  : IntegerBinaryOperator::subtract,
        adjusted,
        ordinal,
        one,
    });
    const auto narrowed = allocate_register(
        attribute->width, attribute->domain);
    process_.operations.emplace_back(Extract {
        narrowed,
        adjusted,
        0U,
        static_cast<std::uint32_t>(attribute->width),
    });
    return result_width != attribute->width
        ? std::optional {
              resize_register(narrowed, result_width, false)
          }
        : std::optional { narrowed };
}

std::optional<SignalId> Lowerer::vhdl_implicit_signal_attribute(
    const SignalId source,
    const std::string_view attribute,
    const runtime::SimulationTick duration,
    const frontend::SourceSpan span)
{
    if (source >= design_.signals_.size()
        || source >= design_.signal_info_.size()) {
        report(
            "FSIM-ELAB-VHATTR-004",
            "implicit VHDL signal attribute references an invalid signal",
            span);
        return std::nullopt;
    }

    const auto source_info = design_.signal_info_[source];
    const auto name = source_info.name + "'" + std::string { attribute }
        + "(" + std::to_string(duration) + ")";
    if (const auto existing = design_.signal_by_name_.find(name);
        existing != design_.signal_by_name_.end()) {
        implicit_signal_dependencies_.push_back(existing->second);
        return existing->second;
    }
    if (design_.signals_.size()
        > std::numeric_limits<SignalId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many signals for an implicit VHDL "
            "attribute",
            span);
        return std::nullopt;
    }
    const auto process_index = design_.processes_.size()
        + 1U + generated_processes_.size();
    if (process_index > std::numeric_limits<ProcessId>::max()) {
        report(
            "FSIM-ELAB-VHATTR-008",
            "the design has too many processes for an implicit VHDL "
            "attribute",
            span);
        return std::nullopt;
    }

    const auto derived = static_cast<SignalId>(design_.signals_.size());
    const auto boolean = attribute != "delayed";
    SignalInfo info;
    info.id = derived;
    info.name = name;
    info.width = boolean ? 1U : source_info.width;
    info.type_name = boolean ? "boolean" : source_info.type_name;
    info.source_domain = boolean
        ? frontend::ValueDomain::Boolean
        : source_info.source_domain;
    info.systemverilog_scalar = boolean
        ? frontend::SystemVerilogScalarKind::None
        : source_info.systemverilog_scalar;
    info.is_signed = !boolean && source_info.is_signed;
    if (!boolean) {
        info.packed_range = source_info.packed_range;
        info.vhdl_array = source_info.vhdl_array;
        info.vhdl_access = source_info.vhdl_access;
        info.vhdl_physical = source_info.vhdl_physical;
        info.packed_members = source_info.packed_members;
        info.integer_range = source_info.integer_range;
        info.nominal_type = source_info.nominal_type;
        info.enumeration_literals = source_info.enumeration_literals;
        info.enumeration_range = source_info.enumeration_range;
    }
    info.declaration_span = span;
    design_.signal_info_.push_back(std::move(info));

    auto signal = boolean
        ? Signal {
              name,
              PackedLogic4(
                  1U,
                  attribute == "transaction"
                      ? Logic4::zero
                      : Logic4::one),
              ResolutionKind::none,
              ValueKind::logic4,
          }
        : design_.signals_[source];
    signal.name = name;
    signal.resolution = ResolutionKind::none;
    design_.signals_.push_back(std::move(signal));
    design_.signal_by_name_.emplace(name, derived);

    Process driver;
    driver.id = static_cast<ProcessId>(process_index);
    driver.name = name + ".$implicit_driver";
    driver.initialize = false;
    driver.static_sensitivity.push_back({
        source,
        attribute == "transaction" || attribute == "quiet"
            ? EdgeKind::transaction
            : EdgeKind::any,
    });
    driver.driver_regions.push_back(Process::DriverRegion {
        derived,
        0U,
        static_cast<std::uint32_t>(
            boolean ? 1U : source_info.width),
        true,
    });

    if (attribute == "transaction") {
        driver.register_count = 2U;
        driver.register_value_kinds.assign(2U, ValueKind::logic4);
        driver.operations.emplace_back(ReadSignal { 0U, derived });
        driver.operations.emplace_back(UnaryNot { 1U, 0U });
        driver.operations.emplace_back(WriteUpdate { derived, 1U });
    } else if (attribute == "delayed") {
        driver.register_count = 1U;
        driver.register_value_kinds.push_back(
            design_.signals_[source].value_kind);
        driver.operations.emplace_back(ReadSignal { 0U, source });
        driver.operations.emplace_back(WriteProjected {
            derived,
            0U,
            duration,
            0U,
            ProjectedDelayMode::transport,
        });
    } else {
        driver.register_count = 2U;
        driver.register_value_kinds.assign(2U, ValueKind::logic4);
        driver.operations.emplace_back(LoadConstant {
            0U, PackedLogic4(1U, Logic4::zero) });
        driver.operations.emplace_back(LoadConstant {
            1U, PackedLogic4(1U, Logic4::one) });
        driver.operations.emplace_back(WriteProjectedWaveform {
            derived,
            { { 0U, 0U }, { 1U, duration } },
            0U,
            ProjectedDelayMode::transport,
        });
    }
    driver.operations.emplace_back(WaitSensitivity { });
    driver.operations.emplace_back(Jump { 0U });
    generated_processes_.push_back(std::move(driver));
    implicit_signal_dependencies_.push_back(derived);
    return derived;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_signal_attribute(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto attribute = hir_vhdl_signal_attribute_profile(
        expression_id, hir_process_scope_);
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto span = hir_source_span(expression->vhdl->source);
    const auto diagnostic_code = vhdl_signal_attribute_diagnostic_code(
        expression->vhdl->text);
    if (!attribute) {
        if (diagnostic_code) {
            report(
                std::string { *diagnostic_code },
                "VHDL signal attribute '" + expression->vhdl->text
                    + "' has an invalid signal prefix or argument",
                span);
        }
        return std::nullopt;
    }
    emit_debug_point(DebugPointKind::call, span);
    if (attribute->kind
            == HirVhdlSignalAttributeKind::driving_value
        && !expression->vhdl->operands.empty()) {
        const auto declaration = hir_referenced_declaration(
            expression->vhdl->operands.front());
        const auto statement_drives = [&](const auto& self,
                                          const semantic::StatementId id)
            -> bool {
            const auto statement = specialized_hir_unit_->find_statement(id);
            if (!statement || statement->vhdl == nullptr) {
                return false;
            }
            const auto& source = *statement->vhdl;
            if (source.target && declaration
                && hir_target_declaration(*source.target)
                    == declaration) {
                return true;
            }
            if (std::ranges::any_of(
                    source.statements,
                    [&](const semantic::StatementId child) {
                        return self(self, child);
                    })
                || std::ranges::any_of(
                    source.else_statements,
                    [&](const semantic::StatementId child) {
                        return self(self, child);
                    })) {
                return true;
            }
            return std::ranges::any_of(
                source.alternatives,
                [&](const semantic::vhdl::CaseAlternative& alternative) {
                    return std::ranges::any_of(
                        alternative.statements,
                        [&](const semantic::StatementId child) {
                            return self(self, child);
                        });
                });
        };
        const auto process_drives = declaration
            && std::ranges::any_of(
                specialized_hir_unit_->design().vhdl_hir.processes(),
                [&](const semantic::vhdl::Process& process) {
                    return process.scope == hir_process_scope_
                        && std::ranges::any_of(
                            process.statements,
                            [&](const semantic::StatementId statement) {
                                return statement_drives(
                                    statement_drives, statement);
                            });
                });
        if (!process_drives) {
            report(
                "FSIM-ELAB-VHATTR-003",
                "'driving_value requires a signal driven by the current "
                "process",
                span);
            return std::nullopt;
        }
    }
    const auto result_width = expected_width != 0U
        ? expected_width
        : attribute->width;
    const auto resize = [&](const RegisterId value) {
        return register_width(value) == result_width
            ? value
            : resize_register(value, result_width, false);
    };
    const auto read_derived = [&](const std::string_view name)
        -> std::optional<RegisterId> {
        const auto derived = vhdl_implicit_signal_attribute(
            attribute->signal, name, attribute->duration, span);
        if (!derived) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            attribute->width, attribute->domain);
        process_.operations.emplace_back(ReadSignal {
            destination, *derived });
        return resize(destination);
    };

    switch (attribute->kind) {
    case HirVhdlSignalAttributeKind::event: {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(SignalEvent {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::last_value: {
        implicit_signal_dependencies_.push_back(attribute->signal);
        const auto destination = allocate_register(
            attribute->width, attribute->domain);
        process_.operations.emplace_back(SignalLastValue {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::last_event: {
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SignalLastEvent {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::last_active: {
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SignalLastActive {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::driving: {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(SignalDriving {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::driving_value: {
        const auto destination = allocate_register(
            attribute->width, attribute->domain);
        process_.operations.emplace_back(SignalDrivingValue {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::stable:
    case HirVhdlSignalAttributeKind::quiet: {
        if (attribute->duration != 0U) {
            return read_derived(
                attribute->kind == HirVhdlSignalAttributeKind::stable
                    ? "stable"
                    : "quiet");
        }
        const auto changed = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        if (attribute->kind == HirVhdlSignalAttributeKind::stable) {
            process_.operations.emplace_back(SignalEvent {
                changed, attribute->signal });
        } else {
            process_.operations.emplace_back(SignalActive {
                changed, attribute->signal });
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(UnaryNot {
            destination, changed });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::active: {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(SignalActive {
            destination, attribute->signal });
        return resize(destination);
    }
    case HirVhdlSignalAttributeKind::transaction:
        return read_derived("transaction");
    case HirVhdlSignalAttributeKind::delayed:
        return read_derived("delayed");
    }
    return std::nullopt;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_vital_mux2(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || !is_hir_vhdl_vital_mux2(expression_id)
        || (expected_width != 0U && expected_width != 1U)
        || expression->vhdl->operands.size() != 3U) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (!source.argument_names.empty()
        && (source.argument_names.size() != source.operands.size()
            || !std::ranges::all_of(
                source.argument_names,
                [](const std::string& name) {
                    return name.empty();
                }))) {
        return std::nullopt;
    }

    const auto logic9 = [&](const std::string_view value) {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_logic9_msb_string(value),
        });
        return destination;
    };
    const auto promote = [&](const semantic::ExpressionId operand)
        -> std::optional<RegisterId> {
        auto value = lower_hir_expression(operand, 1U);
        if (!value || register_width(*value) != 1U) {
            return std::nullopt;
        }
        if (register_domain(*value) == frontend::ValueDomain::Logic9) {
            return value;
        }
        if (register_domain(*value) != frontend::ValueDomain::Bit2
            && register_domain(*value) != frontend::ValueDomain::Logic4) {
            return std::nullopt;
        }
        const auto promoted = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister {
            promoted, *value });
        return promoted;
    };
    const auto matches = [&](const RegisterId value,
                             const std::string_view state) {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            destination,
            value,
            logic9(state),
        });
        return destination;
    };
    const auto either = [&](const RegisterId lhs, const RegisterId rhs) {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_or,
            destination,
            lhs,
            rhs,
        });
        return destination;
    };
    const auto select = [&](const RegisterId condition,
                            const RegisterId when_true,
                            const RegisterId when_false) {
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(ConditionalSelect {
            destination,
            condition,
            when_true,
            when_false,
        });
        return destination;
    };
    const auto normalize = [&](const RegisterId value) {
        const auto zero = either(
            matches(value, "0"), matches(value, "L"));
        const auto one = either(
            matches(value, "1"), matches(value, "H"));
        const auto unknown = select(
            matches(value, "U"), logic9("U"), logic9("X"));
        return select(
            zero,
            logic9("0"),
            select(one, logic9("1"), unknown));
    };

    const auto data1 = promote(source.operands[0]);
    const auto data0 = promote(source.operands[1]);
    const auto selector = promote(source.operands[2]);
    if (!data1 || !data0 || !selector) {
        return std::nullopt;
    }
    const auto normalized_data1 = normalize(*data1);
    const auto normalized_data0 = normalize(*data0);
    const auto select_zero = either(
        matches(*selector, "0"), matches(*selector, "L"));
    const auto select_one = either(
        matches(*selector, "1"), matches(*selector, "H"));
    const auto equal = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary {
        BinaryOperator::case_equal,
        equal,
        normalized_data0,
        normalized_data1,
    });
    const auto merged = select(
        equal, normalized_data0, logic9("X"));
    return select(
        select_zero,
        normalized_data0,
        select(select_one, normalized_data1, merged));
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_aggregate(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width,
    const semantic::vhdl::SubtypeIndication* contextual_subtype)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::aggregate
        || expression->vhdl->associations.empty()
        || expected_width == 0U
        || expected_width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto& aggregate = *expression->vhdl;
    const auto aggregate_span = hir_source_span(aggregate.source);
    const auto definition_for = [&](
                                    const semantic::vhdl::SubtypeIndication& subtype)
        -> const semantic::vhdl::TypeDefinition* {
        auto type_id = subtype.type_mark.target;
        if (!type_id.valid() && !subtype.type_mark.spelling.empty()) {
            semantic::vhdl::Name name;
            name.spelling = subtype.type_mark.spelling;
            name.canonical = name.spelling;
            const semantic::CompiledDeclarationPredicate type_declaration
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.vhdl != nullptr
                          && candidate.vhdl->declared_type.has_value();
                  };
            const auto declaration
                = semantic::CompiledDesignResolver {
                      *specialized_hir_unit_ }
                      .resolve_vhdl(
                          name, aggregate.scope, type_declaration)
                      .unique();
            const auto view = declaration
                ? specialized_hir_unit_->find_declaration(*declaration)
                : std::nullopt;
            if (view && view->vhdl != nullptr
                && view->vhdl->declared_type) {
                type_id = *view->vhdl->declared_type;
            }
        }
        if (!type_id.valid() && !subtype.type_mark.spelling.empty()) {
            const auto named = std::ranges::find_if(
                specialized_hir_unit_->design().vhdl_hir.types(),
                [&](const semantic::vhdl::TypeDefinition& candidate) {
                    return same_vhdl_identifier(
                        candidate.name, subtype.type_mark.spelling);
                });
            if (named
                != specialized_hir_unit_->design().vhdl_hir.types().end()) {
                type_id = named->id;
            }
        }
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid() && visited.insert(type_id.value()).second) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return nullptr;
            }
            const auto* definition = type->vhdl;
            if (definition->form != semantic::vhdl::TypeForm::subtype
                && definition->form != semantic::vhdl::TypeForm::alias) {
                return definition;
            }
            type_id = definition->base.type_mark.target;
        }
        return nullptr;
    };
    const auto association_names = [&](
                                       const semantic::vhdl::AggregateAssociation& association) {
        std::vector<std::string> result;
        for (const auto choice_id : association.choices) {
            const auto choice = specialized_hir_unit_->find_expression(
                choice_id);
            if (choice && choice->vhdl != nullptr
                && choice->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name) {
                result.push_back(choice->vhdl->text);
            }
        }
        if (!result.empty() || !association.choices.empty()
            || association.choice_spelling.empty()) {
            return result;
        }
        auto spelling = std::string_view { association.choice_spelling };
        while (!spelling.empty()) {
            const auto separator = spelling.find('|');
            auto name = spelling.substr(0U, separator);
            while (!name.empty()
                && std::isspace(
                       static_cast<unsigned char>(name.front()))
                    != 0) {
                name.remove_prefix(1U);
            }
            while (!name.empty()
                && std::isspace(
                       static_cast<unsigned char>(name.back()))
                    != 0) {
                name.remove_suffix(1U);
            }
            if (!name.empty()) {
                result.emplace_back(name);
            }
            if (separator == std::string_view::npos) {
                break;
            }
            spelling.remove_prefix(separator + 1U);
        }
        return result;
    };
    auto effective = contextual_subtype != nullptr
        ? hir_effective_vhdl_subtype(*contextual_subtype)
        : std::optional<semantic::vhdl::SubtypeIndication> { };
    if (contextual_subtype != nullptr && active_hir_callable_) {
        const auto& frame
            = hir_callable_frames_[*active_hir_callable_];
        const auto callable = specialized_hir_unit_->find_declaration(
            frame.declaration);
        const auto result_identifier = callable
                && callable->vhdl != nullptr
                && callable->vhdl->callable
                && callable->vhdl->callable->return_identifier
            ? specialized_hir_unit_->find_declaration(
                  *callable->vhdl->callable->return_identifier)
            : std::optional<semantic::CompiledDeclarationView> { };
        auto contextual_name = std::string_view {
            contextual_subtype->type_mark.spelling
        };
        if (const auto separator = contextual_name.find_last_of(".:");
            separator != std::string_view::npos) {
            contextual_name.remove_prefix(separator + 1U);
        }
        const auto contextual_result = result_identifier
            && result_identifier->vhdl != nullptr
            && contextual_name == result_identifier->vhdl->name;
        if (contextual_result && callable->vhdl->callable->return_type) {
            effective = hir_effective_vhdl_subtype(
                *callable->vhdl->callable->return_type);
            if (effective) {
                effective->executable_width = expected_width;
            }
        }
    }
    if (!effective && contextual_subtype != nullptr) {
        // A linked formal can retain a precise nominal subtype even when its
        // package-selected base is intentionally left residual. Keep that
        // nominal identity as aggregate context; definition_for() below can
        // follow its relocated type ID or classify its predefined base.
        effective = *contextual_subtype;
        if (!effective->executable_width
            || *effective->executable_width == 0U) {
            effective->executable_width = expected_width;
        }
    }
    if (!effective && !aggregate.nominal_type.empty()) {
        semantic::vhdl::SubtypeIndication nominal;
        nominal.type_mark.spelling = aggregate.nominal_type;
        semantic::vhdl::Name name;
        name.spelling = aggregate.nominal_type;
        name.canonical = name.spelling;
        const semantic::CompiledDeclarationPredicate type_declaration
            = [](const semantic::CompiledDeclarationView& candidate) {
                  return candidate.vhdl != nullptr
                      && candidate.vhdl->declared_type.has_value();
              };
        const auto declaration
            = semantic::CompiledDesignResolver {
                  *specialized_hir_unit_ }
                  .resolve_vhdl(
                      name, aggregate.scope, type_declaration)
                  .unique();
        const auto view = declaration
            ? specialized_hir_unit_->find_declaration(*declaration)
            : std::nullopt;
        if (view && view->vhdl != nullptr
            && view->vhdl->declared_type) {
            nominal.type_mark.target = *view->vhdl->declared_type;
        }
        effective = hir_effective_vhdl_subtype(nominal);
    }
    const auto adopt_actual_context
        = [&](const semantic::DeclarationId formal) {
        if (effective) {
            return;
        }
        const auto declaration = specialized_hir_unit_->find_declaration(
            formal);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            effective = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            if (!effective) {
                effective = *declaration->vhdl->subtype;
                effective->executable_width = expected_width;
            }
        }
    };
    if (!effective) {
        for (auto frame = hir_generic_binding_frames_.rbegin();
            frame != hir_generic_binding_frames_.rend() && !effective;
            ++frame) {
            for (const auto& binding : *frame) {
                if (binding.expression
                    && *binding.expression == expression_id) {
                    adopt_actual_context(binding.formal);
                    break;
                }
            }
        }
    }
    if (!effective) {
        for (const auto& actual :
            specialized_hir_unit_->specialization().actual_identities) {
            if (actual.actual_expression
                && *actual.actual_expression == expression_id) {
                adopt_actual_context(actual.declaration);
                if (effective) {
                    break;
                }
            }
        }
    }
    if (effective && !effective->type_mark.target.valid()
        && !effective->type_mark.spelling.empty()) {
        semantic::vhdl::Name name;
        name.spelling = effective->type_mark.spelling;
        name.canonical = name.spelling;
        const semantic::CompiledDeclarationPredicate type_declaration
            = [](const semantic::CompiledDeclarationView& candidate) {
                  return candidate.vhdl != nullptr
                      && candidate.vhdl->declared_type.has_value();
              };
        const auto declaration = semantic::CompiledDesignResolver {
            *specialized_hir_unit_ }
                                     .resolve_vhdl(
                                         name, aggregate.scope,
                                         type_declaration)
                                     .unique();
        const auto view = declaration
            ? specialized_hir_unit_->find_declaration(*declaration)
            : std::nullopt;
        if (view && view->vhdl != nullptr
            && view->vhdl->declared_type) {
            effective->type_mark.target = *view->vhdl->declared_type;
        }
    }
    if (!effective) {
        std::optional<semantic::vhdl::SubtypeIndication> inferred;
        for (const auto& type : specialized_hir_unit_->vhdl_types()) {
            if (type.form != semantic::vhdl::TypeForm::record
                && type.form != semantic::vhdl::TypeForm::array) {
                continue;
            }
            auto candidate = type.base;
            candidate.type_mark.target = type.id;
            if (!candidate.executable_width) {
                candidate.executable_width = expected_width;
            }
            if (vhdl_subtype_width(candidate)
                != std::optional { expected_width }) {
                continue;
            }
            if (type.form == semantic::vhdl::TypeForm::record
                && !std::ranges::all_of(
                    aggregate.associations,
                    [&](const auto& association) {
                        const auto spelling = normalized_vhdl_choice(
                            association.choice_spelling);
                        if (spelling.empty() || spelling == "others") {
                            return true;
                        }
                        const auto names = association_names(association);
                        return !names.empty()
                            && std::ranges::all_of(
                                names, [&](const std::string& name) {
                                    return std::ranges::any_of(
                                        type.record_elements,
                                        [&](const auto& element) {
                                            return same_vhdl_identifier(
                                                element.name, name);
                                        });
                                });
                    })) {
                continue;
            }
            if (inferred) {
                inferred.reset();
                break;
            }
            inferred = std::move(candidate);
        }
        effective = std::move(inferred);
    }
    auto* definition = effective ? definition_for(*effective) : nullptr;
    if (definition == nullptr && contextual_subtype != nullptr) {
        // Effective subtype resolution intentionally folds named subtypes to
        // their executable base. Aggregate association rules still belong to
        // the formal's nominal composite declaration, so retain that context
        // when the folded subtype no longer names the array or record.
        definition = definition_for(*contextual_subtype);
        if (effective && definition != nullptr) {
            effective->type_mark.target = definition->id;
        }
    }
    if (effective && definition != nullptr
        && (definition->form == semantic::vhdl::TypeForm::array
            || definition->form == semantic::vhdl::TypeForm::record)
        && vhdl_subtype_width(*effective)
            != std::optional { expected_width }) {
        // Effective-subtype folding can expose the scalar element width of a
        // nominal composite formal.  The aggregate's caller has already
        // supplied the bounded composite width, so keep that width with the
        // restored nominal array/record definition.
        effective->executable_width = expected_width;
    }
    std::optional<semantic::vhdl::SubtypeIndication>
        predefined_array_element;
    if (effective && definition == nullptr) {
        auto predefined_context = contextual_subtype != nullptr
            ? *contextual_subtype
            : *effective;
        auto spelling = std::string_view {
            predefined_context.type_mark.spelling
        };
        auto type_id = predefined_context.type_mark.target;
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid() && visited.insert(type_id.value()).second) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr
                || (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias)) {
                break;
            }
            predefined_context = type->vhdl->base;
            spelling = predefined_context.type_mark.spelling;
            type_id = predefined_context.type_mark.target;
        }
        const auto predefined_array
            = same_vhdl_identifier(spelling, "bit_vector")
            || same_vhdl_identifier(spelling, "std_logic_vector")
            || same_vhdl_identifier(spelling, "std_ulogic_vector")
            || same_vhdl_identifier(spelling, "signed")
            || same_vhdl_identifier(spelling, "unsigned");
        if (predefined_array) {
            effective = std::move(predefined_context);
            // The scalar width carried by a predefined array's base subtype
            // describes one element.  The aggregate caller has already
            // established the bounded composite width, so it is authoritative
            // for this contextual aggregate.
            effective->executable_width = expected_width;
            if (effective->domain
                == semantic::vhdl::ValueDomain::unknown) {
                effective->domain = same_vhdl_identifier(
                                        spelling, "bit_vector")
                    ? semantic::vhdl::ValueDomain::bit2
                    : semantic::vhdl::ValueDomain::logic9;
            }
            predefined_array_element.emplace();
            predefined_array_element->domain = effective->domain;
            predefined_array_element->executable_width = 1U;
            predefined_array_element->type_mark.spelling
                = effective->domain == semantic::vhdl::ValueDomain::bit2
                ? "bit"
                : "std_logic";
        }
    }
    if (!effective
        || (definition == nullptr && !predefined_array_element)
        || vhdl_subtype_width(*effective)
            != std::optional { expected_width }) {
        report(
            "FSIM-ELAB-VHAGG-001",
            "a VHDL aggregate requires a bounded contextual composite type",
            aggregate_span);
        return std::nullopt;
    }
    const auto root_type = [&](
                               const semantic::vhdl::SubtypeIndication& subtype) {
        const auto* type = definition_for(subtype);
        return type != nullptr ? std::optional { type->id }
                               : std::optional<semantic::TypeId> { };
    };
    const auto expression_subtype = [&](const semantic::ExpressionId value)
        -> std::optional<semantic::vhdl::SubtypeIndication> {
        if (const auto direct = hir_vhdl_expression_subtype(value)) {
            return direct;
        }
        const auto source = specialized_hir_unit_->find_expression(value);
        if (!source || source->vhdl == nullptr
            || source->vhdl->kind != semantic::vhdl::ExpressionKind::call
            || !source->vhdl->referenced_name
            || !source->vhdl->referenced_name->selected) {
            return std::nullopt;
        }
        const auto declaration = specialized_hir_unit_->find_declaration(
            *source->vhdl->referenced_name->selected);
        return declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
    };
    const auto lower_value = [&](
                                 const semantic::ExpressionId value,
                                 const semantic::vhdl::SubtypeIndication& subtype,
                                 const std::string_view diagnostic_prefix,
                                 const frontend::SourceSpan& span)
        -> std::optional<RegisterId> {
        const auto expected = hir_effective_vhdl_subtype(subtype);
        const auto width = expected ? vhdl_subtype_width(*expected)
                                    : std::nullopt;
        if (!expected || !width) {
            report(
                std::string { diagnostic_prefix } + "-002",
                "VHDL aggregate element has no executable flattened layout",
                span);
            return std::nullopt;
        }
        const auto source = specialized_hir_unit_->find_expression(value);
        const auto context_dependent_literal = source
            && source->vhdl != nullptr
            && (source->vhdl->kind
                    == semantic::vhdl::ExpressionKind::logic_literal
                || source->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal);
        const auto actual = expression_subtype(value);
        const auto nominally_incompatible = !context_dependent_literal
            && actual
            && actual->domain != semantic::vhdl::ValueDomain::unknown
            && expected->domain != semantic::vhdl::ValueDomain::unknown
            && actual->domain != expected->domain;
        if (const auto expected_root = root_type(*expected);
            !context_dependent_literal && expected_root && actual) {
            const auto actual_root = root_type(*actual);
            if (actual_root && *expected_root != *actual_root) {
                report(
                    std::string { diagnostic_prefix } + "-009",
                    "VHDL aggregate element requires its exact contextual subtype",
                    span);
                return std::nullopt;
            }
        }
        if (nominally_incompatible) {
            report(
                std::string { diagnostic_prefix } + "-009",
                "VHDL aggregate element requires its exact contextual subtype",
                span);
            return std::nullopt;
        }
        auto lowered = source && source->vhdl != nullptr
                && source->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
            ? lower_hir_vhdl_aggregate(value, *width, &*expected)
            : lower_hir_expression(value, *width);
        if (!lowered || register_width(*lowered) != *width) {
            report(
                std::string { diagnostic_prefix } + "-006",
                "VHDL aggregate element width does not match its context",
                span);
            return std::nullopt;
        }
        const auto domain = vhdl_subtype_domain(expected->domain);
        const auto context_compatible_bit_literal = source
            && source->vhdl != nullptr
            && source->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && source->vhdl->text.size() == 3U
            && (source->vhdl->text[1] == '0'
                || source->vhdl->text[1] == '1');
        if (is_two_state_domain(domain)
            && !is_two_state_domain(register_domain(*lowered))
            && !context_compatible_bit_literal) {
            report(
                std::string { diagnostic_prefix } + "-007",
                "two-state VHDL aggregate element requires an explicit conversion",
                span);
            return std::nullopt;
        }
        if (domain == frontend::ValueDomain::Unknown) {
            return std::nullopt;
        }
        if (register_domain(*lowered) != domain) {
            const auto converted = allocate_register(*width, domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *lowered });
            lowered = converted;
        }
        const auto* expected_type = definition_for(*expected);
        std::optional<std::pair<std::int64_t, std::int64_t>>
            ordinal_range;
        for (const auto& constraint : expected->constraints) {
            if (constraint.null) {
                continue;
            }
            const auto left = constraint.left
                ? constraint.left
                : constraint.left_expression
                ? hir_constant_integer(*constraint.left_expression)
                : std::nullopt;
            const auto right = constraint.right
                ? constraint.right
                : constraint.right_expression
                ? hir_constant_integer(*constraint.right_expression)
                : std::nullopt;
            if (left && right) {
                ordinal_range.emplace(*left, *right);
                break;
            }
        }
        const auto enumeration_ordinal
            = expected_type != nullptr
            && expected_type->form
                == semantic::vhdl::TypeForm::enumeration
            && vhdl_subtype_domain(expected->domain)
                == frontend::ValueDomain::Bit2;
        const auto constrained_ordinal
            = expected->domain == semantic::vhdl::ValueDomain::integer
            || enumeration_ordinal;
        if (constrained_ordinal && ordinal_range) {
            const auto checked
                = expected->domain == semantic::vhdl::ValueDomain::integer
                ? *lowered
                : widen_enumeration_ordinal(*lowered);
            process_.operations.emplace_back(IntegerCheck {
                checked,
                std::min(ordinal_range->first, ordinal_range->second),
                std::max(ordinal_range->first, ordinal_range->second),
            });
        } else if (enumeration_ordinal
            && !expected_type->enumeration_literals.empty()) {
            process_.operations.emplace_back(IntegerCheck {
                widen_enumeration_ordinal(*lowered),
                0,
                static_cast<std::int64_t>(
                    expected_type->enumeration_literals.size() - 1U),
            });
        }
        return lowered;
    };
    auto aggregate_domain = vhdl_subtype_domain(effective->domain);
    if (aggregate_domain == frontend::ValueDomain::Unknown
        && definition != nullptr
        && (definition->form == semantic::vhdl::TypeForm::array
            || definition->form == semantic::vhdl::TypeForm::record)) {
        // Heterogeneous and unresolved composites use the same packed Logic9
        // storage domain selected by hir_runtime_binding for local objects.
        aggregate_domain = frontend::ValueDomain::Logic9;
    }
    if (aggregate_domain == frontend::ValueDomain::Unknown) {
        return std::nullopt;
    }

    if (definition != nullptr
        && definition->form == semantic::vhdl::TypeForm::record) {
        const auto& elements = definition->record_elements;
        std::vector<std::optional<RegisterId>> values(elements.size());
        std::optional<std::size_t> others;
        std::size_t positional { };
        bool valid = !elements.empty();
        const auto assign = [&](const std::size_t index,
                                const semantic::ExpressionId value,
                                const frontend::SourceSpan& span) {
            if (index >= elements.size() || values[index]) {
                report(
                    "FSIM-ELAB-VHAGG-004",
                    index < elements.size()
                        ? "record aggregate element '" + elements[index].name
                            + "' is assigned more than once"
                        : "record aggregate has too many positional associations",
                    span);
                valid = false;
                return;
            }
            values[index] = lower_value(
                value, elements[index].subtype,
                "FSIM-ELAB-"
                "VHAGG",
                span);
            valid = valid && values[index].has_value();
        };
        for (std::size_t index = 0U;
            index < aggregate.associations.size(); ++index) {
            const auto& association = aggregate.associations[index];
            const auto span = hir_source_span(association.source);
            const auto spelling = normalized_vhdl_choice(
                association.choice_spelling);
            const auto names = association_names(association);
            if (spelling.empty() && names.empty()) {
                assign(positional++, association.value, span);
                continue;
            }
            if (spelling == "others"
                || (names.size() == 1U
                    && same_vhdl_identifier(names.front(), "others"))) {
                if (others) {
                    report(
                        "FSIM-ELAB-VHAGG-004",
                        "record aggregate has more than one others association",
                        span);
                    valid = false;
                } else {
                    others = index;
                }
                continue;
            }
            if (names.empty()) {
                report(
                    "FSIM-ELAB-VHAGG-008",
                    "record aggregate choices must be element names",
                    span);
                valid = false;
                continue;
            }
            for (const auto& name : names) {
                const auto element = std::ranges::find_if(
                    elements, [&](const auto& candidate) {
                        return same_vhdl_identifier(candidate.name, name);
                    });
                if (element == elements.end()) {
                    report(
                        "FSIM-ELAB-VHAGG-003",
                        "record aggregate type '" + definition->name
                            + "' has no element '" + name + "'",
                        span);
                    valid = false;
                    continue;
                }
                assign(
                    static_cast<std::size_t>(
                        std::distance(elements.begin(), element)),
                    association.value,
                    span);
            }
        }
        if (others) {
            const auto& association = aggregate.associations[*others];
            const auto span = hir_source_span(association.source);
            for (std::size_t index = 0U; index < values.size(); ++index) {
                if (!values[index]) {
                    assign(index, association.value, span);
                }
            }
        }
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (!values[index]) {
                report(
                    "FSIM-ELAB-VHAGG-005",
                    "record aggregate is missing element '"
                        + elements[index].name + "'",
                    aggregate_span);
                valid = false;
            }
        }
        if (!valid) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            expected_width, aggregate_domain);
        auto initial = PackedLogic4(expected_width, Logic4::zero);
        if (aggregate_domain == frontend::ValueDomain::Logic9) {
            initial.fill(runtime::Logic9::zero);
        }
        process_.operations.emplace_back(LoadConstant {
            destination, std::move(initial) });
        auto remaining = expected_width;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const auto subtype = hir_effective_vhdl_subtype(
                elements[index].subtype);
            const auto width = subtype
                ? vhdl_subtype_width(*subtype)
                : std::nullopt;
            if (!width || *width > remaining
                || remaining - *width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            remaining -= *width;
            process_.operations.emplace_back(Insert {
                destination,
                destination,
                *values[index],
                static_cast<std::uint32_t>(remaining),
            });
        }
        if (remaining != 0U) {
            return std::nullopt;
        }
        return destination;
    }

    if (!predefined_array_element
        && (definition == nullptr
            || definition->form != semantic::vhdl::TypeForm::array
            || !definition->element_subtype)) {
        report(
            "FSIM-ELAB-VHAGG-001",
            "a VHDL aggregate requires an array or record context",
            aggregate_span);
        return std::nullopt;
    }
    auto dimensions = effective->constraints;
    if (dimensions.empty() && definition != nullptr) {
        for (const auto& dimension : definition->array_dimensions) {
            if (!dimension.constraint) {
                break;
            }
            dimensions.push_back(*dimension.constraint);
        }
    }
    std::optional<semantic::vhdl::RangeConstraint> resolved_range;
    if (!dimensions.empty()) {
        resolved_range = dimensions.front();
    }
    if (resolved_range && !resolved_range->left
        && resolved_range->left_expression) {
        resolved_range->left = hir_constant_integer(
            *resolved_range->left_expression);
    }
    if (resolved_range && !resolved_range->right
        && resolved_range->right_expression) {
        resolved_range->right = hir_constant_integer(
            *resolved_range->right_expression);
    }
    auto* range = resolved_range ? &*resolved_range : nullptr;
    std::optional<semantic::vhdl::SubtypeIndication> element;
    if (predefined_array_element) {
        element = predefined_array_element;
    } else if (range != nullptr && range->left && range->right
        && dimensions.size() > 1U) {
        const auto outer_count
            = index_distance(*range->left, *range->right) + 1U;
        if (outer_count != 0U && expected_width % outer_count == 0U) {
            auto nested = *effective;
            nested.constraints.assign(
                std::next(dimensions.begin()), dimensions.end());
            nested.executable_width = expected_width / outer_count;
            element = hir_effective_vhdl_subtype(nested);
        }
    } else if (definition != nullptr && definition->element_subtype) {
        element = hir_effective_vhdl_subtype(
            *definition->element_subtype);
    }
    const auto element_width = element ? vhdl_subtype_width(*element)
                                       : std::nullopt;
    if (range == nullptr && element_width && *element_width != 0U
        && expected_width % *element_width == 0U) {
        const auto element_count = expected_width / *element_width;
        if (element_count != 0U
            && element_count - 1U
                <= static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            semantic::vhdl::RangeConstraint materialized;
            materialized.left = static_cast<std::int64_t>(
                element_count - 1U);
            materialized.right = 0;
            materialized.descending = true;
            resolved_range = std::move(materialized);
            range = &*resolved_range;
        }
    }
    if (!element || !element_width || range == nullptr || range->null
        || !range->left || !range->right
        || expected_width % *element_width != 0U) {
        report(
            "FSIM-ELAB-VHARRAYAGG-002",
            "VHDL array aggregate contextual layout is inconsistent",
            aggregate_span);
        return std::nullopt;
    }
    const auto element_count = expected_width / *element_width;
    if (index_distance(*range->left, *range->right) + 1U
        != element_count) {
        report(
            "FSIM-ELAB-VHARRAYAGG-002",
            "VHDL array aggregate range and packed width disagree",
            aggregate_span);
        return std::nullopt;
    }
    std::vector<std::optional<RegisterId>> values(element_count);
    std::optional<std::size_t> others;
    std::size_t positional { };
    bool valid = true;
    const auto assign_offset = [&](const std::size_t offset,
                                   const semantic::ExpressionId value,
                                   const frontend::SourceSpan& span) {
        if (offset >= values.size() || values[offset]) {
            report(
                "FSIM-ELAB-VHARRAYAGG-004",
                offset < values.size()
                    ? "VHDL array aggregate index is assigned more than once"
                    : "VHDL array aggregate has too many positional associations",
                span);
            valid = false;
            return;
        }
        values[offset] = lower_value(
            value, *element, "FSIM-ELAB-" "VHARRAYAGG", span);
        valid = valid && values[offset].has_value();
    };
    const auto assign_index = [&](const std::int64_t index,
                                  const semantic::ExpressionId value,
                                  const frontend::SourceSpan& span) {
        const auto low = std::min(*range->left, *range->right);
        const auto high = std::max(*range->left, *range->right);
        if (index < low || index > high) {
            report(
                "FSIM-ELAB-VHARRAYAGG-003",
                "VHDL array aggregate index " + std::to_string(index)
                    + " is outside the contextual range",
                span);
            valid = false;
            return;
        }
        assign_offset(
            static_cast<std::size_t>(
                index_distance(index, *range->right)),
            value,
            span);
    };
    for (std::size_t association_index = 0U;
        association_index < aggregate.associations.size();
        ++association_index) {
        const auto& association = aggregate.associations[association_index];
        const auto span = hir_source_span(association.source);
        const auto spelling = normalized_vhdl_choice(
            association.choice_spelling);
        if (spelling.empty() && association.choices.empty()) {
            if (positional >= element_count) {
                assign_offset(element_count, association.value, span);
            } else {
                assign_offset(
                    element_count - 1U - positional,
                    association.value,
                    span);
            }
            ++positional;
            continue;
        }
        if (spelling == "others") {
            if (others) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-004",
                    "VHDL array aggregate has more than one others association",
                    span);
                valid = false;
            } else {
                others = association_index;
            }
            continue;
        }
        const auto assign_choice = [&](const auto& self,
                                       const semantic::ExpressionId choice_id)
            -> void {
            const auto choice = specialized_hir_unit_->find_expression(
                choice_id);
            if (!choice || choice->vhdl == nullptr) {
                valid = false;
                return;
            }
            const auto& source = *choice->vhdl;
            if (source.kind == semantic::vhdl::ExpressionKind::binary
                && source.text == "|"
                && source.operands.size() == 2U) {
                self(self, source.operands[0]);
                self(self, source.operands[1]);
                return;
            }
            if (source.kind == semantic::vhdl::ExpressionKind::name
                && same_vhdl_identifier(source.text, "others")) {
                if (association.choices.size() != 1U || others) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-004",
                        "invalid or duplicate others array association",
                        hir_source_span(source.source));
                    valid = false;
                } else {
                    others = association_index;
                }
                return;
            }
            if (source.kind == semantic::vhdl::ExpressionKind::binary
                && (source.text == "to" || source.text == "downto")
                && source.operands.size() == 2U) {
                const auto left = hir_constant_integer(source.operands[0]);
                const auto right = hir_constant_integer(source.operands[1]);
                if (!left || !right) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array aggregate range choices require locally static bounds",
                        hir_source_span(source.source));
                    valid = false;
                    return;
                }
                const auto descending = source.text == "downto";
                if ((descending && *left < *right)
                    || (!descending && *left > *right)) {
                    return;
                }
                auto index = *left;
                while (true) {
                    assign_index(index, association.value, span);
                    if (index == *right) {
                        break;
                    }
                    index += descending ? -1 : 1;
                }
                return;
            }
            if (source.kind == semantic::vhdl::ExpressionKind::call
                && (source.text == "'range"
                    || source.text == "'reverse_range")) {
                const auto attribute = hir_vhdl_attribute_profile(
                    choice_id, hir_process_scope_);
                if (!attribute || !attribute->array_prefix
                    || !attribute->discrete_range) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array range choices require a concrete, "
                            "bounded array range",
                        hir_source_span(source.source));
                    valid = false;
                    return;
                }
                const auto& selected_range = *attribute->discrete_range;
                const auto null_range = selected_range.descending
                    ? selected_range.left < selected_range.right
                    : selected_range.left > selected_range.right;
                if (null_range) {
                    return;
                }
                const auto low = std::min(
                    selected_range.left, selected_range.right);
                const auto high = std::max(
                    selected_range.left, selected_range.right);
                if (low < std::min(*range->left, *range->right)
                    || high > std::max(*range->left, *range->right)) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array aggregate index is outside the "
                            "contextual range",
                        hir_source_span(source.source));
                    valid = false;
                    return;
                }
                if (index_distance(
                        selected_range.left, selected_range.right)
                    >= static_cast<std::uint64_t>(values.size())) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array range choice exceeds the contextual "
                            "range",
                        hir_source_span(source.source));
                    valid = false;
                    return;
                }
                auto index = selected_range.left;
                while (true) {
                    assign_index(
                        index, association.value, span);
                    if (index == selected_range.right) {
                        break;
                    }
                    if (selected_range.descending) {
                        if (index
                            == std::numeric_limits<std::int64_t>::min()) {
                            valid = false;
                            return;
                        }
                        --index;
                    } else {
                        if (index
                            == std::numeric_limits<std::int64_t>::max()) {
                            valid = false;
                            return;
                        }
                        ++index;
                    }
                }
                return;
            }
            const auto index = hir_constant_integer(choice_id);
            if (!index) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate choices require locally static indices",
                    hir_source_span(source.source));
                valid = false;
                return;
            }
            assign_index(*index, association.value, span);
        };
        for (const auto choice_id : association.choices) {
            assign_choice(assign_choice, choice_id);
        }
    }
    if (others) {
        const auto& association = aggregate.associations[*others];
        const auto span = hir_source_span(association.source);
        for (std::size_t offset = 0U; offset < values.size(); ++offset) {
            if (!values[offset]) {
                assign_offset(offset, association.value, span);
            }
        }
    }
    for (const auto& value : values) {
        if (!value) {
            report(
                "FSIM-ELAB-VHARRAYAGG-005",
                "VHDL array aggregate is missing an index",
                aggregate_span);
            valid = false;
        }
    }
    if (!valid) {
        return std::nullopt;
    }
    std::vector<RegisterId> operands;
    operands.reserve(values.size());
    for (auto value = values.rbegin(); value != values.rend(); ++value) {
        operands.push_back(**value);
    }
    const auto destination = allocate_register(
        expected_width, aggregate_domain);
    process_.operations.emplace_back(Concatenate {
        destination,
        std::move(operands),
        static_cast<std::uint32_t>(expected_width),
    });
    return destination;
}

bool Lowerer::can_lower_hir_systemverilog_packed_pattern(
    const semantic::ExpressionId expression_id,
    const semantic::sv::TypeReference& type,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr
        || !type.target.target.valid()) {
        return false;
    }
    const auto definition = specialized_hir_unit_->find_type(
        type.target.target);
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!definition || definition->systemverilog == nullptr
        || !expression || expression->systemverilog == nullptr) {
        return false;
    }
    const auto& declared = *definition->systemverilog;
    const auto& source = *expression->systemverilog;
    constexpr auto tagged_prefix
        = std::string_view { "@sv-tagged:" };
    const auto value_supported = [&](const semantic::ExpressionId value,
                                     const semantic::sv::TypeReference& expected)
        -> bool {
        const auto candidate = specialized_hir_unit_->find_expression(value);
        if (!candidate || candidate->systemverilog == nullptr) {
            return false;
        }
        const auto& input = *candidate->systemverilog;
        if (input.kind == semantic::sv::ExpressionKind::assignment_pattern
            || (input.kind == semantic::sv::ExpressionKind::call
                && input.text.starts_with(tagged_prefix))) {
            return can_lower_hir_systemverilog_packed_pattern(
                value, expected, process_scope);
        }
        std::unordered_set<std::uint32_t> visiting;
        return can_lower_hir_expression(value, process_scope, visiting);
    };
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text.starts_with(tagged_prefix)) {
        if (declared.form != semantic::sv::TypeForm::tagged_union
            || source.operands.size() != 1U) {
            return false;
        }
        const auto member_name = std::string_view { source.text }.substr(
            tagged_prefix.size());
        const auto member = std::ranges::find(
            declared.members, member_name,
            &semantic::sv::PackedMember::name);
        return member != declared.members.end()
            && value_supported(source.operands.front(), member->type);
    }
    if (source.kind != semantic::sv::ExpressionKind::assignment_pattern
        || source.text != "sv-pattern"
        || (declared.form
                != semantic::sv::TypeForm::packed_structure
            && declared.form != semantic::sv::TypeForm::packed_union)
        || source.associations.empty()) {
        return false;
    }
    std::vector<bool> selected(declared.members.size());
    std::size_t positional { };
    std::optional<semantic::ExpressionId> default_value;
    for (const auto& association : source.associations) {
        std::size_t index { };
        if (association.choice_spelling == "default") {
            if (default_value) {
                return false;
            }
            default_value = association.value;
            continue;
        }
        if (association.choice_spelling == "@key") {
            if (association.choices.size() != 1U) {
                return false;
            }
            const auto key = specialized_hir_unit_->find_expression(
                association.choices.front());
            if (!key || key->systemverilog == nullptr
                || key->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                return false;
            }
            const auto member = std::ranges::find(
                declared.members,
                key->systemverilog->text,
                &semantic::sv::PackedMember::name);
            if (member == declared.members.end()) {
                return false;
            }
            index = static_cast<std::size_t>(std::distance(
                declared.members.begin(), member));
        } else if (association.choice_spelling.empty()) {
            index = positional++;
        } else {
            return false;
        }
        if (index >= declared.members.size() || selected[index]
            || !value_supported(
                association.value, declared.members[index].type)) {
            return false;
        }
        selected[index] = true;
    }
    for (std::size_t index { }; index < selected.size(); ++index) {
        if (!selected[index] && default_value
            && !value_supported(
                *default_value, declared.members[index].type)) {
            return false;
        }
    }
    return true;
}

std::optional<RegisterId>
Lowerer::lower_hir_systemverilog_packed_pattern(
    const semantic::ExpressionId expression_id,
    const semantic::sv::TypeReference& type,
    const std::size_t expected_width)
{
    if (specialized_hir_unit_ == nullptr
        || expected_width == 0U || !type.target.target.valid()) {
        return std::nullopt;
    }
    const auto definition = specialized_hir_unit_->find_type(
        type.target.target);
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!definition || definition->systemverilog == nullptr
        || !expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& declared = *definition->systemverilog;
    const auto& source = *expression->systemverilog;
    constexpr auto tagged_prefix
        = std::string_view { "@sv-tagged:" };
    const auto domain = hir_systemverilog_type_four_state(type)
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    const auto destination = allocate_register(expected_width, domain);
    process_.operations.emplace_back(LoadConstant {
        destination, PackedLogic4(expected_width, Logic4::zero) });
    const auto lower_value = [&](const semantic::ExpressionId value,
                                 const semantic::sv::TypeReference& expected,
                                 const std::size_t width)
        -> std::optional<RegisterId> {
        const auto candidate = specialized_hir_unit_->find_expression(value);
        if (!candidate || candidate->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& input = *candidate->systemverilog;
        if (input.kind == semantic::sv::ExpressionKind::assignment_pattern
            || (input.kind == semantic::sv::ExpressionKind::call
                && input.text.starts_with(tagged_prefix))) {
            return lower_hir_systemverilog_packed_pattern(
                value, expected, width);
        }
        return lower_hir_expression(value, width);
    };
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text.starts_with(tagged_prefix)) {
        if (declared.form != semantic::sv::TypeForm::tagged_union
            || source.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto member_name = std::string_view { source.text }.substr(
            tagged_prefix.size());
        const auto member = std::ranges::find(
            declared.members, member_name,
            &semantic::sv::PackedMember::name);
        if (member == declared.members.end()) {
            return std::nullopt;
        }
        const auto member_index = static_cast<std::size_t>(std::distance(
            declared.members.begin(), member));
        const auto member_offset = hir_systemverilog_member_offset(
            declared, member_index);
        const auto width = hir_systemverilog_type_width(member->type);
        const auto tag_width = std::max<std::size_t>(
            1U,
            static_cast<std::size_t>(
                std::bit_width(declared.members.size() - 1U)));
        if (!member_offset || !width || *width == 0U
            || *member_offset
                > std::numeric_limits<std::uint32_t>::max()
            || *width > std::numeric_limits<std::uint32_t>::max()
            || tag_width > expected_width
            || expected_width - tag_width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto value = lower_value(
            source.operands.front(), member->type, *width);
        if (!value) {
            return std::nullopt;
        }
        process_.operations.emplace_back(Insert {
            destination,
            destination,
            *value,
            static_cast<std::uint32_t>(*member_offset),
        });
        const auto tag = allocate_register(
            tag_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            tag,
            unsigned_value(
                static_cast<std::size_t>(std::distance(
                    declared.members.begin(), member)),
                tag_width),
        });
        process_.operations.emplace_back(Insert {
            destination,
            destination,
            tag,
            static_cast<std::uint32_t>(
                expected_width - tag_width),
        });
        return destination;
    }
    if (source.kind != semantic::sv::ExpressionKind::assignment_pattern
        || source.text != "sv-pattern"
        || (declared.form
                != semantic::sv::TypeForm::packed_structure
            && declared.form != semantic::sv::TypeForm::packed_union)
        || source.associations.empty()) {
        return std::nullopt;
    }
    std::vector<bool> selected(declared.members.size());
    std::size_t positional { };
    std::optional<semantic::ExpressionId> default_value;
    const auto write_member = [&](const std::size_t index,
                                  const semantic::ExpressionId value) {
        const auto& member = declared.members[index];
        const auto member_offset = hir_systemverilog_member_offset(
            declared, index);
        const auto width = hir_systemverilog_type_width(member.type);
        if (!member_offset || !width || *width == 0U
            || *member_offset
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto lowered = lower_value(value, member.type, *width);
        if (!lowered) {
            return false;
        }
        process_.operations.emplace_back(Insert {
            destination,
            destination,
            *lowered,
            static_cast<std::uint32_t>(*member_offset),
        });
        return true;
    };
    for (const auto& association : source.associations) {
        std::size_t index { };
        if (association.choice_spelling == "default") {
            if (default_value) {
                return std::nullopt;
            }
            default_value = association.value;
            continue;
        }
        if (association.choice_spelling == "@key") {
            if (association.choices.size() != 1U) {
                return std::nullopt;
            }
            const auto key = specialized_hir_unit_->find_expression(
                association.choices.front());
            if (!key || key->systemverilog == nullptr
                || key->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                return std::nullopt;
            }
            const auto member = std::ranges::find(
                declared.members,
                key->systemverilog->text,
                &semantic::sv::PackedMember::name);
            if (member == declared.members.end()) {
                return std::nullopt;
            }
            index = static_cast<std::size_t>(std::distance(
                declared.members.begin(), member));
        } else if (association.choice_spelling.empty()) {
            index = positional++;
        } else {
            return std::nullopt;
        }
        if (index >= declared.members.size() || selected[index]) {
            return std::nullopt;
        }
        selected[index] = true;
        if (!write_member(index, association.value)) {
            return std::nullopt;
        }
    }
    if (default_value) {
        for (std::size_t index { }; index < selected.size(); ++index) {
            if (!selected[index]
                && !write_member(index, *default_value)) {
                return std::nullopt;
            }
        }
    }
    return destination;
}

bool Lowerer::is_hir_systemverilog_coverage_call(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call) {
        return false;
    }
    const auto& name = expression->systemverilog->text;
    return name == "$get_coverage"
        || name == "$get_inst_coverage"
        || name == "$coverage_control"
        || name == "$coverage_get"
        || name == "$coverage_get_max"
        || name == "$coverage_merge"
        || name == "$coverage_save";
}

std::optional<RegisterId> Lowerer::lower_hir_systemverilog_coverage_call(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const auto span = hir_source_span(source.source);
    const auto result = [&](const RegisterId destination) {
        return expected_width != 0U && expected_width != 32U
            ? resize_register(destination, expected_width, true)
            : destination;
    };
    const auto invalid = [&](const std::string_view code,
                             const std::string& message) {
        report(std::string { code }, message, span);
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(0U, 32U) });
        return result(destination);
    };
    const auto named = std::ranges::any_of(
        source.argument_names,
        [](const std::string& name) { return !name.empty(); });
    if (source.text == "$get_coverage"
        || source.text == "$get_inst_coverage") {
        if (language_ != frontend::Language::SystemVerilog2017
            || named || !source.operands.empty()) {
            return invalid(
                "FSIM-ELAB-SVCOV-001",
                source.text + " requires SystemVerilog and no arguments");
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CoverageQuery {
            destination,
            source.text == "$get_inst_coverage"
                ? CoverageQueryKind::overall_instance
                : CoverageQueryKind::overall_type,
        });
        return expected_width != 0U && expected_width != 64U
            ? resize_register(destination, expected_width, false)
            : destination;
    }
    const auto lower_integer = [&](const semantic::ExpressionId operand)
        -> std::optional<RegisterId> {
        auto lowered = lower_hir_expression(operand, 32U);
        if (lowered && register_width(*lowered) != 32U) {
            lowered = resize_register(
                *lowered, 32U, hir_expression_signed(operand));
        }
        return lowered;
    };
    const auto lower_selector = [&](const semantic::ExpressionId operand)
        -> std::pair<std::optional<StringRegisterId>, bool> {
        if (hir_expression_is_string(operand, hir_process_scope_)) {
            return { lower_hir_string_expression(operand), false };
        }
        const auto selector = specialized_hir_unit_->find_expression(
            operand);
        if (!selector || selector->systemverilog == nullptr
            || selector->systemverilog->kind
                != semantic::sv::ExpressionKind::name
            || selector->systemverilog->text.empty()) {
            return { std::nullopt, true };
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            destination, selector->systemverilog->text });
        return { destination, true };
    };
    if (source.text == "$coverage_control") {
        if (language_ != frontend::Language::SystemVerilog2017
            || named || source.operands.size() != 4U) {
            return invalid(
                "FSIM-COV-038",
                "$coverage_control requires SystemVerilog and positional "
                "control, coverage type, scope, and module or instance "
                "selector arguments");
        }
        const auto command = lower_integer(source.operands[0]);
        const auto coverage_type = lower_integer(source.operands[1]);
        const auto scope = lower_integer(source.operands[2]);
        const auto [selector, selector_is_instance]
            = lower_selector(source.operands[3]);
        if (!command || !coverage_type || !scope || !selector) {
            return invalid(
                "FSIM-COV-038",
                "$coverage_control selector must be a string-valued module "
                "name or hierarchical instance name");
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(CoverageControl {
            destination,
            *command,
            *coverage_type,
            *scope,
            *selector,
            hierarchy_,
            selector_is_instance,
        });
        return result(destination);
    }

    const auto file_call = source.text == "$coverage_merge"
        || source.text == "$coverage_save";
    if (language_ != frontend::Language::SystemVerilog2017
        || named
        || source.operands.size() != (file_call ? 2U : 3U)
        || (file_call
            && !hir_expression_is_string(
                source.operands[1], hir_process_scope_))) {
        return invalid(
            "FSIM-COV-039",
            source.text
                + (file_call
                        ? " requires SystemVerilog, a positional integer "
                          "coverage type, and a positional string filename"
                        : " requires SystemVerilog and positional coverage "
                          "type, scope, and module or instance selector "
                          "arguments"));
    }
    const auto coverage_type = lower_integer(source.operands[0]);
    if (!coverage_type) {
        return std::nullopt;
    }
    std::optional<RegisterId> scope;
    std::optional<StringRegisterId> selector;
    std::optional<StringRegisterId> filename;
    bool selector_is_instance { };
    if (file_call) {
        filename = lower_hir_string_expression(source.operands[1]);
        if (!filename) {
            return std::nullopt;
        }
    } else {
        scope = lower_integer(source.operands[1]);
        auto selected = lower_selector(source.operands[2]);
        selector = selected.first;
        selector_is_instance = selected.second;
        if (!scope || !selector) {
            return invalid(
                "FSIM-COV-039",
                source.text
                    + " selector must be a string-valued module name or "
                      "hierarchical instance name");
        }
    }
    const auto kind = source.text == "$coverage_get"
        ? SystemVerilogCoverageAccessKind::get
        : source.text == "$coverage_get_max"
        ? SystemVerilogCoverageAccessKind::get_max
        : source.text == "$coverage_merge"
        ? SystemVerilogCoverageAccessKind::merge
        : SystemVerilogCoverageAccessKind::save;
    const auto destination = allocate_register(
        32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(CoverageAccess {
        destination,
        kind,
        *coverage_type,
        scope,
        selector,
        file_call ? std::string { } : hierarchy_,
        selector_is_instance,
        filename,
    });
    return result(destination);
}

std::optional<RegisterId> Lowerer::lower_hir_membership_expression(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || expression->systemverilog->text != "inside") {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const auto source_span = hir_source_span(source.source);
    if (source.operands.size() < 2U) {
        report(
            "FSIM-ELAB-SVMEMBER-002",
            "an inside expression requires a left operand and a nonempty list",
            source_span);
        return std::nullopt;
    }

    const auto integral_operand = [&](
                                      const semantic::ExpressionId operand,
                                      const std::string_view diagnostic_code,
                                      const std::string_view diagnostic_message)
        -> std::optional<InsideIntegralOperand> {
        const auto record = specialized_hir_unit_->find_expression(operand);
        const auto declaration_id = hir_referenced_declaration(operand);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        const auto composite = record
                && record->systemverilog != nullptr
            ? record->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern
                || record->systemverilog->kind
                    == semantic::sv::ExpressionKind::string_literal
                || (record->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && record->systemverilog->text.starts_with(
                        "@inside-"))
            : true;
        const auto container = declaration
                && declaration->systemverilog != nullptr
                && declaration->systemverilog->type
            ? declaration->systemverilog->type->container_form.has_value()
                || declaration->systemverilog->type->value_form
                    == semantic::sv::TypeForm::string
            : false;
        const auto width = hir_expression_width(
            operand, hir_process_scope_);
        const auto domain = hir_expression_domain(
            operand, hir_process_scope_);
        if (composite || container || !width || *width == 0U
            || !domain || *domain == frontend::ValueDomain::String
            || *domain == frontend::ValueDomain::Unknown) {
            report(
                std::string { diagnostic_code },
                std::string { diagnostic_message },
                record && record->systemverilog != nullptr
                    ? hir_source_span(record->systemverilog->source)
                    : source_span);
            return std::nullopt;
        }
        const auto value = lower_hir_expression(operand, *width);
        if (!value) {
            return std::nullopt;
        }
        return InsideIntegralOperand {
            *value,
            register_width(*value),
            hir_expression_signed(operand),
        };
    };

    const auto lhs = integral_operand(
        source.operands.front(),
        "FSIM-ELAB-SVMEMBER-003",
        "bounded inside membership requires scalar integral operands");
    if (!lhs) {
        return std::nullopt;
    }
    const auto result = allocate_register(
        1U, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(LoadConstant {
        result, PackedLogic4(1U, Logic4::zero) });
    std::vector<InstructionIndex> matched_branches;

    for (std::size_t index = 1U; index < source.operands.size(); ++index) {
        const auto item_id = source.operands[index];
        const auto item = specialized_hir_unit_->find_expression(item_id);
        if (!item || item->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& candidate = *item->systemverilog;
        const auto range = candidate.kind
                == semantic::sv::ExpressionKind::call
            && (candidate.text == "@inside-range"
                || candidate.text == "@inside-absolute-tolerance"
                || candidate.text == "@inside-relative-tolerance");
        std::optional<RegisterId> matched;
        if (range) {
            if (candidate.operands.size() != 2U) {
                report(
                    "FSIM-ELAB-SVMEMBER-006",
                    "an inside range requires exactly one low and high bound",
                    hir_source_span(candidate.source));
                return std::nullopt;
            }
            auto low = integral_operand(
                candidate.operands.front(),
                "FSIM-ELAB-SVMEMBER-004",
                "inside range bounds must be integral expressions");
            auto high = integral_operand(
                candidate.operands.back(),
                "FSIM-ELAB-SVMEMBER-004",
                "inside range bounds must be integral expressions");
            if (!low || !high) {
                return std::nullopt;
            }
            const auto tolerance = candidate.text != "@inside-range";
            if (tolerance
                && systemverilog_standard_
                    != frontend::StandardRevision::SystemVerilog2023) {
                report(
                    "FSIM-ELAB-SVTOLERANCE-001",
                    "inside tolerance ranges require the exact "
                    "SystemVerilog-2023 profile",
                    hir_source_span(candidate.source));
                return std::nullopt;
            }
            if (tolerance) {
                auto amount = high->value;
                if (register_width(amount) != low->width) {
                    amount = resize_register(
                        amount, low->width, high->signed_value);
                }
                if (candidate.text == "@inside-relative-tolerance") {
                    const auto arithmetic_width = std::max<std::size_t>(
                        64U,
                        low->width
                                > std::numeric_limits<std::size_t>::max()
                                    / 2U
                            ? low->width
                            : low->width * 2U);
                    auto center = low->value;
                    if (register_width(center) != arithmetic_width) {
                        center = resize_register(
                            center, arithmetic_width, low->signed_value);
                    }
                    if (register_width(amount) != arithmetic_width) {
                        amount = resize_register(
                            amount, arithmetic_width, high->signed_value);
                    }
                    const auto product = allocate_register(
                        arithmetic_width, register_domain(center));
                    process_.operations.emplace_back(Binary {
                        low->signed_value
                            ? BinaryOperator::multiply_signed
                            : BinaryOperator::multiply_unsigned,
                        product,
                        center,
                        amount,
                    });
                    const auto hundred = allocate_register(
                        arithmetic_width, register_domain(center));
                    process_.operations.emplace_back(LoadConstant {
                        hundred,
                        unsigned_value(100U, arithmetic_width),
                    });
                    const auto quotient = allocate_register(
                        arithmetic_width, register_domain(center));
                    process_.operations.emplace_back(Binary {
                        low->signed_value
                            ? BinaryOperator::divide_signed
                            : BinaryOperator::divide_unsigned,
                        quotient,
                        product,
                        hundred,
                    });
                    amount = resize_register(
                        quotient, low->width, low->signed_value);
                }
                const auto lower = allocate_register(
                    low->width, register_domain(low->value));
                const auto upper = allocate_register(
                    low->width, register_domain(low->value));
                process_.operations.emplace_back(Binary {
                    low->signed_value
                        ? BinaryOperator::subtract_signed
                        : BinaryOperator::subtract_unsigned,
                    lower,
                    low->value,
                    amount,
                });
                process_.operations.emplace_back(Binary {
                    low->signed_value
                        ? BinaryOperator::add_signed
                        : BinaryOperator::add_unsigned,
                    upper,
                    low->value,
                    amount,
                });
                low = InsideIntegralOperand {
                    lower, low->width, low->signed_value
                };
                high = InsideIntegralOperand {
                    upper, low->width, low->signed_value
                };
            }
            const auto valid_operands = size_integral_comparison(
                *low, *high);
            const auto low_operands = size_integral_comparison(
                *lhs, *low);
            const auto high_operands = size_integral_comparison(
                *lhs, *high);
            const auto valid = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            const auto above_low = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            const auto below_high = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            const auto within_lower = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            matched = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary {
                valid_operands.signed_value
                    ? BinaryOperator::less_equal_signed
                    : BinaryOperator::less_equal_unsigned,
                valid,
                valid_operands.lhs,
                valid_operands.rhs,
            });
            process_.operations.emplace_back(Binary {
                low_operands.signed_value
                    ? BinaryOperator::greater_equal_signed
                    : BinaryOperator::greater_equal_unsigned,
                above_low,
                low_operands.lhs,
                low_operands.rhs,
            });
            process_.operations.emplace_back(Binary {
                high_operands.signed_value
                    ? BinaryOperator::less_equal_signed
                    : BinaryOperator::less_equal_unsigned,
                below_high,
                high_operands.lhs,
                high_operands.rhs,
            });
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and,
                within_lower,
                valid,
                above_low,
            });
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and,
                *matched,
                within_lower,
                below_high,
            });
            if (tolerance) {
                const auto above_swapped = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto below_swapped = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto within_swapped = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto swapped = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Binary {
                    high_operands.signed_value
                        ? BinaryOperator::greater_equal_signed
                        : BinaryOperator::greater_equal_unsigned,
                    above_swapped,
                    high_operands.lhs,
                    high_operands.rhs,
                });
                process_.operations.emplace_back(Binary {
                    low_operands.signed_value
                        ? BinaryOperator::less_equal_signed
                        : BinaryOperator::less_equal_unsigned,
                    below_swapped,
                    low_operands.lhs,
                    low_operands.rhs,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_and,
                    within_swapped,
                    above_swapped,
                    below_swapped,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_or,
                    swapped,
                    *matched,
                    within_swapped,
                });
                matched = swapped;
            }
        } else {
            const auto value = integral_operand(
                item_id,
                "FSIM-ELAB-SVMEMBER-004",
                "inside members must be integral expressions");
            if (!value) {
                return std::nullopt;
            }
            const auto comparison = size_integral_comparison(
                *lhs, *value);
            matched = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary {
                BinaryOperator::wildcard_equal,
                *matched,
                comparison.lhs,
                comparison.rhs,
            });
        }
        const auto accumulated = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_or,
            accumulated,
            result,
            *matched,
        });
        process_.operations.emplace_back(CopyRegister {
            result, accumulated });
        if (index + 1U < source.operands.size()) {
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                result,
                0U,
                static_cast<InstructionIndex>(branch + 1U),
                UnknownBranchPolicy::when_false,
            });
            matched_branches.push_back(branch);
        }
    }
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto branch : matched_branches) {
        process_.operations[branch] = Branch {
            result,
            end,
            static_cast<InstructionIndex>(branch + 1U),
            UnknownBranchPolicy::when_false,
        };
    }
    return result;
}

std::optional<RegisterId> Lowerer::lower_hir_container_element_index(
    const HirContainerElementBinding& element)
{
    if (element.type == nullptr || element.indices.empty()
        || (element.indices.size() > 1U
            && element.type->dimensions.size() != element.indices.size())) {
        return std::nullopt;
    }
    if (element.indices.size() == 1U) {
        if (element.type->string_indices) {
            return lower_hir_string_expression(element.indices.front());
        }
        const auto width = std::max<std::size_t>(
            element.type->index_width, 1U);
        auto index = lower_hir_expression(element.indices.front(), width);
        if (index && register_width(*index) != width) {
            index = resize_register(
                *index, width,
                hir_expression_signed(element.indices.front()));
        }
        return index;
    }

    std::optional<RegisterId> linear;
    for (std::size_t dimension { };
        dimension < element.indices.size(); ++dimension) {
        const auto expression = element.indices[dimension];
        const auto& bounds = element.type->dimensions[dimension];
        const auto low = std::min(bounds.first, bounds.second);
        const auto high = std::max(bounds.first, bounds.second);
        std::optional<RegisterId> ordinal;
        if (const auto constant = hir_constant_integer(expression)) {
            if (*constant < low || *constant > high) {
                return std::nullopt;
            }
            const auto value = static_cast<std::uint64_t>(
                bounds.first >= bounds.second
                    ? static_cast<std::int64_t>(bounds.first) - *constant
                    : *constant - bounds.first);
            ordinal = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                *ordinal, unsigned_value(value, 32U) });
        } else {
            auto index = lower_hir_expression(expression, 32U);
            if (!index) {
                return std::nullopt;
            }
            if (register_width(*index) != 32U) {
                index = resize_register(
                    *index, 32U, hir_expression_signed(expression));
            }
            process_.operations.emplace_back(IntegerCheck {
                *index, low, high });
            const auto declared_left = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                declared_left, integer_value(bounds.first) });
            ordinal = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::subtract,
                *ordinal,
                bounds.first >= bounds.second ? declared_left : *index,
                bounds.first >= bounds.second ? *index : declared_left,
            });
        }
        if (!linear) {
            linear = ordinal;
            continue;
        }
        const auto count = static_cast<std::int64_t>(high) - low + 1;
        const auto count_register = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            count_register, integer_value(count) });
        const auto scaled = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            scaled,
            *linear,
            count_register,
        });
        const auto combined = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            combined,
            scaled,
            *ordinal,
        });
        linear = combined;
    }
    return linear;
}

std::optional<RegisterId>
Lowerer::lower_hir_packed_container_aggregate(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto profile = hir_packed_container_aggregate_profile(
        expression_id);
    const auto binding = hir_container_object_binding(expression_id);
    if (!profile || !binding || binding->type == nullptr) {
        return std::nullopt;
    }
    struct Leaf {
        std::vector<std::uint32_t> members;
        const ContainerType* type { };
    };
    std::vector<Leaf> leaves;
    const auto collect = [&](const auto& self,
                             const ContainerType& type,
                             std::vector<std::uint32_t> path) -> bool {
        if (type.element_kind == ContainerElementKind::Aggregate) {
            for (std::size_t index { };
                index < type.element_types.size(); ++index) {
                if (index > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                auto child = path;
                child.push_back(static_cast<std::uint32_t>(index));
                if (!self(self, type.element_types[index],
                        std::move(child))) {
                    return false;
                }
            }
            return true;
        }
        if (type.element_kind != ContainerElementKind::Packed
            || type.element_width == 0U) {
            return false;
        }
        leaves.push_back(Leaf { std::move(path), &type });
        return true;
    };
    if (!collect(collect, *binding->type, { }) || leaves.empty()) {
        return std::nullopt;
    }
    const auto container = binding->local
        ? *binding->local
        : allocate_container_register(*binding->type);
    if (!binding->local) {
        process_.operations.emplace_back(ReadContainerObject {
            container, binding->object });
    }
    const auto index = allocate_register(
        32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        index, integer_value(0) });
    std::vector<RegisterId> values;
    values.reserve(leaves.size());
    for (const auto& leaf : leaves) {
        const auto value = allocate_register(
            leaf.type->element_width,
            leaf.type->two_state ? frontend::ValueDomain::Bit2
                                 : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerAggregateRead {
            value,
            container,
            index,
            leaf.members,
            false,
            false,
        });
        values.push_back(value);
    }
    auto result = allocate_register(profile->width, profile->domain);
    process_.operations.emplace_back(Concatenate {
        result,
        std::move(values),
        static_cast<std::uint32_t>(profile->width),
    });
    if (expected_width != 0U && expected_width != profile->width) {
        result = resize_register(result, expected_width, false);
    }
    return result;
}

std::optional<Lowerer::HirLoweredContainerElement>
Lowerer::lower_hir_container_element_path(
    const HirContainerElementBinding& element)
{
    if (element.type == nullptr || element.selected_type == nullptr
        || element.indices.empty()) {
        return std::nullopt;
    }
    const auto root = element.local
        ? *element.local : allocate_container_register(*element.type);
    if (!element.local) {
        process_.operations.emplace_back(ReadContainerObject {
            root, element.object });
    }

    HirLoweredContainerElement result;
    result.root = root;
    result.container = root;
    result.type = element.type;
    std::size_t consumed { };
    for (;;) {
        const auto dimensions = result.type->fixed
            ? result.type->dimensions.size() : 1U;
        if (dimensions == 0U
            || consumed + dimensions > element.indices.size()) {
            return std::nullopt;
        }
        result.indices.assign(
            element.indices.begin()
                + static_cast<std::ptrdiff_t>(consumed),
            element.indices.begin()
                + static_cast<std::ptrdiff_t>(consumed + dimensions));
        consumed += dimensions;
        if (consumed == element.indices.size()) {
            return result.type == element.selected_type
                ? std::optional { std::move(result) } : std::nullopt;
        }
        if (dimensions != 1U
            || result.type->element_kind
                != ContainerElementKind::Container
            || result.type->element_types.size() != 1U
            || result.type->string_indices) {
            return std::nullopt;
        }
        HirContainerElementBinding level = element;
        level.type = result.type;
        level.selected_type = result.type;
        level.indices = result.indices;
        const auto index = lower_hir_container_element_index(level);
        if (!index) {
            return std::nullopt;
        }
        const auto& child_type = result.type->element_types.front();
        const auto child = allocate_container_register(child_type);
        process_.operations.emplace_back(ContainerElementRead {
            child,
            result.container,
            *index,
            result.type->signed_indices,
        });
        result.parents.push_back(HirLoweredContainerElement::Parent {
            result.container, *index, result.type->signed_indices });
        result.container = child;
        result.type = &child_type;
    }
}

void Lowerer::publish_hir_container_element_path(
    const HirContainerElementBinding& element,
    const HirLoweredContainerElement& path)
{
    auto child = path.container;
    for (auto parent = path.parents.rbegin();
        parent != path.parents.rend(); ++parent) {
        process_.operations.emplace_back(ContainerElementWrite {
            parent->container,
            parent->index,
            child,
            parent->signed_index,
        });
        child = parent->container;
    }
    if (!element.local) {
        process_.operations.emplace_back(WriteContainerObject {
            element.object, path.root, std::nullopt });
    }
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_standard_function(
    const semantic::ExpressionId expression_id,
    const HirVhdlStandardFunctionProfile& profile)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (profile.invalid_numeric_size) {
        const auto size_expression = specialized_hir_unit_->find_expression(
            source.operands[1]);
        const auto size_source = size_expression
                && size_expression->vhdl != nullptr
            ? size_expression->vhdl->source
            : source.source;
        report(
            "FSIM-ELAB-VHNUM-002",
            std::string { profile.name }
                + " requires a positive locally static result size "
                  "representable by SimIR",
            hir_source_span(size_source));
        return std::nullopt;
    }
    if (profile.kind == HirVhdlStandardFunctionKind::psl_query) {
        if (!source.operands.empty()) {
            return std::nullopt;
        }
        using Kind = VhdlPslApiKind;
        const auto kind = profile.name == "pslassertfailed"
            ? Kind::assert_failed
            : profile.name == "psliscovered"
            ? Kind::is_covered
            : profile.name == "getpslcoverassert"
            ? Kind::get_cover_assert
            : Kind::is_assert_covered;
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(VhdlPslApi {
            kind, destination, std::nullopt });
        return destination;
    }
    if (source.operands.empty()) {
        return std::nullopt;
    }
    const auto source_width
        = profile.kind == HirVhdlStandardFunctionKind::reduction
        ? hir_vhdl_expression_runtime_width(
              source.operands[0], hir_process_scope_)
        : hir_expression_width(
              source.operands[0], hir_process_scope_);
    if (!source_width) {
        report(
            profile.kind == HirVhdlStandardFunctionKind::numeric_resize
                    || profile.kind
                        == HirVhdlStandardFunctionKind::numeric_shift
                ? "FSIM-ELAB-VHNUM-003"
                : "FSIM-ELAB-VHLOGIC-002",
            std::string { profile.name }
                + " requires a nonempty bounded operand",
            hir_source_span(source.source));
        return std::nullopt;
    }
    if (*source_width == 0U
        && profile.kind == HirVhdlStandardFunctionKind::reduction) {
        const bool identity = profile.name == "and_reduce"
            || profile.name == "nor_reduce"
            || profile.name == "xnor_reduce";
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_logic9_msb_string(
                identity ? "1" : "0"),
        });
        return destination;
    }
    if (*source_width == 0U) {
        report(
            profile.kind == HirVhdlStandardFunctionKind::numeric_resize
                    || profile.kind
                        == HirVhdlStandardFunctionKind::numeric_shift
                ? "FSIM-ELAB-VHNUM-003"
                : "FSIM-ELAB-VHLOGIC-002",
            std::string { profile.name }
                + " requires a nonempty bounded operand",
            hir_source_span(source.source));
        return std::nullopt;
    }
    auto value = lower_hir_expression(source.operands[0], *source_width);
    if (!value) {
        return std::nullopt;
    }
    if (profile.kind
        == HirVhdlStandardFunctionKind::numeric_to_integer) {
        auto signed_operand = hir_expression_signed(source.operands[0]);
        if (profile.name == "conv_integer" && !signed_operand) {
            const auto context = hir_vhdl_synopsys_numeric_context(
                source.scope);
            signed_operand = context.signed_visible
                && !context.unsigned_visible;
        }
        // VHDL integer is a bounded signed type.  An unsigned conversion can
        // therefore use only the positive-value bits, while a signed
        // conversion uses the complete predefined integer width.  Round-trip
        // through that representable width before conversion so both runtime
        // engines diagnose discarded significant bits identically.
        const auto value_width = signed_operand
            ? profile.width
            : profile.width - 1U;
        const auto narrowed = resize_register(
            *value, value_width, signed_operand);
        const auto restored = resize_register(
            narrowed, *source_width, signed_operand);

        // A metavalue is not an integer-range failure: the IEEE numeric
        // packages warn and return zero.  Keep the range assertion disabled
        // for that case, then select zero as the conversion result.
        const auto self_equal = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal, self_equal, *value, *value });
        const auto one = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            one, PackedLogic4(1U, Logic4::one) });
        const auto known = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, known, self_equal, one });

        const auto operand = specialized_hir_unit_->find_expression(
            source.operands[0]);
        const auto operand_source = operand && operand->vhdl != nullptr
            ? operand->vhdl->source
            : source.source;
        const auto operand_span = hir_source_span(operand_source);
        const auto location = SourceLocation {
            operand_span.source_name.str(),
            static_cast<std::uint32_t>(operand_span.begin.line),
            static_cast<std::uint32_t>(operand_span.begin.column),
        };
        process_.operations.emplace_back(Assert {
            known,
            "numeric to_integer detected a metavalue and returned zero",
            AssertionSeverity::warning,
            location,
        });

        const auto fits = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal, fits, *value, restored });
        const auto fits_or_unknown = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ConditionalSelect {
            fits_or_unknown, known, fits, one });
        process_.operations.emplace_back(Assert {
            fits_or_unknown,
            "VHDL numeric to_integer operand is outside the predefined "
            "integer range",
            AssertionSeverity::failure,
            location,
        });

        const auto resized = resize_register(
            narrowed, profile.width, signed_operand);
        const auto zero = allocate_register(
            profile.width, register_domain(resized));
        process_.operations.emplace_back(LoadConstant {
            zero, PackedLogic4(profile.width, Logic4::zero) });
        const auto selected = allocate_register(
            profile.width, register_domain(resized));
        process_.operations.emplace_back(ConditionalSelect {
            selected, known, resized, zero });
        if (register_domain(selected)
            == frontend::ValueDomain::Integer) {
            return selected;
        }
        const auto destination = allocate_register(
            profile.width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(CopyRegister {
            destination, selected });
        return destination;
    }
    if (profile.kind == HirVhdlStandardFunctionKind::numeric_resize) {
        auto resized = resize_register(
            *value, profile.width, profile.signed_value);
        if (register_domain(resized) != profile.domain) {
            const auto destination = allocate_register(
                profile.width, profile.domain);
            process_.operations.emplace_back(CopyRegister {
                destination, resized });
            resized = destination;
        }
        return resized;
    }
    if (profile.kind == HirVhdlStandardFunctionKind::numeric_shift) {
        const auto amount_width = hir_expression_width(
            source.operands[1], hir_process_scope_).value_or(32U);
        const auto amount = lower_hir_expression(
            source.operands[1], amount_width);
        if (!amount) {
            return std::nullopt;
        }
        const auto operation = profile.name == "shift_left"
                || profile.name == "shl"
            ? "sll"
            : profile.name == "rotate_left" ? "rol"
            : profile.name == "rotate_right" ? "ror"
            : profile.signed_value ? "sra" : "srl";
        const auto destination = allocate_register(
            profile.width, profile.domain);
        process_.operations.emplace_back(Shift {
            lowered_shift_operator(operation, profile.signed_value),
            destination,
            *value,
            *amount,
            true,
        });
        return destination;
    }
    if (profile.kind
        == HirVhdlStandardFunctionKind::logic_unknown_predicate) {
        if (register_domain(*value) != frontend::ValueDomain::Logic9) {
            report(
                "FSIM-ELAB-VHLOGIC-002",
                "is_x requires a standard-logic operand",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto logic_constant = [&](const runtime::Logic9 state) {
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(LoadConstant {
                destination,
                PackedLogic4::from_logic9_msb_string(
                    std::string(1U, runtime::to_char(state))),
            });
            return destination;
        };
        const auto scalar = [&](const std::size_t bit) {
            if (*source_width == 1U) {
                return *value;
            }
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Extract {
                destination, *value, static_cast<std::uint32_t>(bit), 1U });
            return destination;
        };
        const auto exact = [&](const RegisterId operand,
                               const runtime::Logic9 state) {
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                destination,
                operand,
                logic_constant(state),
            });
            return destination;
        };
        const auto boolean_constant = [&](const bool state) {
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant {
                destination, unsigned_value(state ? 1U : 0U, 1U) });
            return destination;
        };
        auto any_unknown = boolean_constant(false);
        for (std::size_t bit { }; bit < *source_width; ++bit) {
            const auto element = scalar(bit);
            auto known = exact(element, runtime::Logic9::zero);
            for (const auto state : {
                     runtime::Logic9::one,
                     runtime::Logic9::l,
                     runtime::Logic9::h }) {
                const auto match = exact(element, state);
                const auto combined = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::bit_or, combined, known, match });
                known = combined;
            }
            const auto unknown = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LogicalNot {
                unknown, known });
            const auto combined = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_or,
                combined,
                any_unknown,
                unknown,
            });
            any_unknown = combined;
        }
        return any_unknown;
    }
    if (profile.kind == HirVhdlStandardFunctionKind::logic_conversion) {
        if (register_domain(*value) != frontend::ValueDomain::Bit2
            && register_domain(*value) != frontend::ValueDomain::Logic9) {
            report(
                "FSIM-ELAB-VHLOGIC-002",
                std::string { profile.name }
                    + " requires a bit or standard-logic operand",
                hir_source_span(source.source));
            return std::nullopt;
        }
        if (register_domain(*value) == frontend::ValueDomain::Logic9) {
            return *value;
        }
        const auto destination = allocate_register(
            profile.width, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(CopyRegister {
            destination, *value });
        return destination;
    }
    if (profile.kind == HirVhdlStandardFunctionKind::reduction) {
        const auto scalar = [&](const std::size_t bit) {
            if (*source_width == 1U) {
                return *value;
            }
            const auto destination = allocate_register(
                1U, register_domain(*value));
            process_.operations.emplace_back(Extract {
                destination, *value, static_cast<std::uint32_t>(bit), 1U });
            return destination;
        };
        auto result = scalar(0U);
        const auto and_family = profile.name == "and_reduce"
            || profile.name == "nand_reduce";
        const auto operation = and_family ? BinaryOperator::bit_and
            : profile.name == "or_reduce" || profile.name == "nor_reduce"
            ? BinaryOperator::bit_or
            : BinaryOperator::bit_xor;
        for (std::size_t bit = 1U; bit < *source_width; ++bit) {
            const auto combined = allocate_register(
                1U, register_domain(result));
            process_.operations.emplace_back(Binary {
                operation, combined, result, scalar(bit) });
            result = combined;
        }
        if (profile.name == "nand_reduce" || profile.name == "nor_reduce"
            || profile.name == "xnor_reduce") {
            const auto negated = allocate_register(
                1U, register_domain(result));
            process_.operations.emplace_back(UnaryNot { negated, result });
            return negated;
        }
        return result;
    }
    if (register_domain(*value) != frontend::ValueDomain::Logic9) {
        report(
            "FSIM-ELAB-VHLOGIC-002",
            std::string { profile.name }
                + " requires a standard-logic operand",
            hir_source_span(source.source));
        return std::nullopt;
    }
    auto xmap = runtime::Logic9::zero;
    if (source.operands.size() == 2U) {
        const auto mapping = specialized_hir_unit_->find_expression(
            source.operands[1]);
        const auto parsed = mapping && mapping->vhdl != nullptr
            && mapping->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && mapping->vhdl->text.size() == 3U
            ? runtime::parse_logic9(mapping->vhdl->text[1])
            : std::nullopt;
        if (!parsed || (*parsed != runtime::Logic9::zero
                           && *parsed != runtime::Logic9::one)) {
            report(
                "FSIM-ELAB-VHLOGIC-003",
                std::string { profile.name }
                    + " supports only a static bit xmap",
                mapping && mapping->vhdl != nullptr
                    ? hir_source_span(mapping->vhdl->source)
                    : hir_source_span(source.source));
            return std::nullopt;
        }
        xmap = *parsed;
    }
    const auto constant = [&](const frontend::ValueDomain domain,
                              const runtime::Logic9 state) {
        const auto destination = allocate_register(1U, domain);
        if (domain == frontend::ValueDomain::Logic9) {
            process_.operations.emplace_back(LoadConstant {
                destination,
                PackedLogic4::from_logic9_msb_string(
                    std::string(1U, runtime::to_char(state))),
            });
        } else {
            process_.operations.emplace_back(LoadConstant {
                destination,
                unsigned_value(
                    state == runtime::Logic9::one ? 1U : 0U, 1U),
            });
        }
        return destination;
    };
    const auto scalar_at = [&](const std::size_t bit) {
        if (*source_width == 1U) {
            return *value;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(Extract {
            destination, *value, static_cast<std::uint32_t>(bit), 1U });
        return destination;
    };
    const auto exact = [&](const RegisterId scalar,
                           const runtime::Logic9 state) {
        const auto literal = constant(frontend::ValueDomain::Logic9, state);
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, destination, scalar, literal });
        return destination;
    };
    std::array<runtime::Logic9, 9U> table { };
    if (profile.kind == HirVhdlStandardFunctionKind::bit_conversion
        || profile.name == "to_01") {
        table.fill(xmap);
    } else {
        table.fill(runtime::Logic9::x);
        if (profile.name == "to_x01z") {
            table[static_cast<std::size_t>(runtime::Logic9::z)]
                = runtime::Logic9::z;
        } else if (profile.name == "to_ux01") {
            table[static_cast<std::size_t>(runtime::Logic9::u)]
                = runtime::Logic9::u;
        }
    }
    table[static_cast<std::size_t>(runtime::Logic9::zero)]
        = runtime::Logic9::zero;
    table[static_cast<std::size_t>(runtime::Logic9::l)]
        = runtime::Logic9::zero;
    table[static_cast<std::size_t>(runtime::Logic9::one)]
        = runtime::Logic9::one;
    table[static_cast<std::size_t>(runtime::Logic9::h)]
        = runtime::Logic9::one;
    std::vector<RegisterId> bits(*source_width);
    for (std::size_t bit { }; bit < *source_width; ++bit) {
        const auto scalar = scalar_at(bit);
        auto mapped = constant(profile.domain, table.front());
        for (std::size_t state = 1U; state < table.size(); ++state) {
            const auto condition = exact(
                scalar, static_cast<runtime::Logic9>(state));
            const auto mapped_value = constant(profile.domain, table[state]);
            const auto selected = allocate_register(1U, profile.domain);
            process_.operations.emplace_back(ConditionalSelect {
                selected, condition, mapped_value, mapped });
            mapped = selected;
        }
        bits[*source_width - bit - 1U] = mapped;
    }
    if (*source_width == 1U) {
        return bits.front();
    }
    const auto destination = allocate_register(
        *source_width, profile.domain);
    process_.operations.emplace_back(Concatenate {
        destination, std::move(bits),
        static_cast<std::uint32_t>(*source_width) });
    return destination;
}

std::optional<RegisterId> Lowerer::lower_hir_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width,
    const frontend::SystemVerilogScalarKind scalar_context)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto pattern_binding
        = hir_case_pattern_binding(expression_id)) {
        if (!pattern_binding->value) {
            return std::nullopt;
        }
        auto binding = *pattern_binding->value;
        if (expected_width != 0U
            && register_width(binding) != expected_width) {
            binding = resize_register(
                binding, expected_width, pattern_binding->signed_value);
        }
        return binding;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        const auto formal_expression
            = specialized_hir_unit_->find_expression(expression_id);
        const auto actual_expression
            = specialized_hir_unit_->find_expression(*actual);
        auto formal_declaration = formal_expression
                && formal_expression->vhdl != nullptr
                && formal_expression->vhdl->referenced_name
                && formal_expression->vhdl->referenced_name->selected
            ? formal_expression->vhdl->referenced_name->selected
            : std::nullopt;
        if (!formal_declaration && formal_expression
            && formal_expression->vhdl != nullptr) {
            formal_declaration = semantic::CompiledDesignResolver {
                *specialized_hir_unit_, hir_generic_binding_frames_
            }.resolve_expression_name(expression_id).unique();
        }
        const auto formal = formal_declaration
            ? specialized_hir_unit_->find_declaration(*formal_declaration)
            : std::nullopt;
        if (actual_expression && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && formal && formal->vhdl != nullptr
            && formal->vhdl->subtype) {
            const auto context = hir_effective_vhdl_subtype(
                *formal->vhdl->subtype);
            const auto formal_width = context
                ? vhdl_subtype_width(*context)
                : std::nullopt;
            const auto contextual_width
                = formal_width && *formal_width != 0U
                ? formal_width
                : expected_width != 0U
                ? std::optional { expected_width }
                : std::nullopt;
            if (!contextual_width || *contextual_width == 0U) {
                return std::nullopt;
            }
            return lower_hir_vhdl_aggregate(
                *actual, *contextual_width,
                &*formal->vhdl->subtype);
        }
        return lower_hir_expression(*actual, expected_width, scalar_context);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return lower_hir_expression(*actual, expected_width, scalar_context);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = lower_hir_expression(
            declaration->expression, expected_width, scalar_context);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && !expression->systemverilog->text.starts_with(
            "@sv-sync-new:")) {
        const auto synchronization = lower_hir_synchronization_expression(
            expression_id, expected_width);
        if (synchronization.handled) {
            return synchronization.succeeded
                ? synchronization.value
                : std::nullopt;
        }
    }
    if (is_hir_vhdl_vital_memory_expression(expression_id)) {
        return lower_hir_vhdl_vital_memory_expression(
            expression_id, expected_width);
    }
    if (const auto attempt = lower_hir_vhdl_vital_expression(
            expression_id, expected_width);
        attempt.handled) {
        return attempt.value;
    }
    if (const auto attempt = lower_hir_vhdl_float_function(
            expression_id, expected_width);
        attempt.handled) {
        return attempt.value;
    }
    if (const auto attempt = lower_hir_vhdl_fixed_function(
            expression_id, expected_width);
        attempt.handled) {
        return attempt.value;
    }
    if (const auto standard = hir_vhdl_standard_function_profile(
            expression_id)) {
        return lower_hir_vhdl_standard_function(
            expression_id, *standard);
    }
    const auto referenced = hir_referenced_declaration(expression_id);
    constexpr auto vhdl_qualified_prefix
        = std::string_view { "@vhdl-qualified:" };
    const auto qualified_profile = expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && expression->vhdl->text.starts_with(vhdl_qualified_prefix)
        ? hir_vhdl_conversion_profile(expression_id, expected_width)
        : std::nullopt;
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text.starts_with(vhdl_qualified_prefix)
        && !qualified_profile) {
        const auto type_name = std::string_view {
            expression->vhdl->text
        }.substr(vhdl_qualified_prefix.size());
        const auto declaration = referenced
            ? specialized_hir_unit_->find_declaration(*referenced)
            : std::nullopt;
        const auto declared_type = declaration
            && declaration->vhdl != nullptr
            && (declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::type
                || declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::subtype);
        auto canonical_type = std::string { type_name };
        std::ranges::transform(
            canonical_type, canonical_type.begin(), [](const char value) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(value)));
            });
        const auto predefined_definite_type
            = canonical_type == "bit" || canonical_type == "boolean"
            || canonical_type == "integer" || canonical_type == "natural"
            || canonical_type == "positive"
            || canonical_type == "time"
            || canonical_type == "std_logic"
            || canonical_type == "std_ulogic";
        const auto code = predefined_definite_type
            ? std::string_view { "FSIM-ELAB-VHQUAL-003" }
            : declared_type
            ? std::string_view { "FSIM-ELAB-VHQUAL-002" }
            : std::string_view { "FSIM-ELAB-VHQUAL-001" };
        report(
            std::string { code },
            predefined_definite_type
                ? "VHDL qualified expression operand is not compatible "
                  "with its type mark"
                : declared_type
                ? "VHDL qualified expression type mark '"
                    + std::string { type_name }
                    + "' is indefinite in this context"
                : "VHDL qualified expression type mark '"
                    + std::string { type_name }
                    + "' does not denote a visible type",
            hir_source_span(expression->vhdl->source));
        const auto width = expected_width == 0U ? 32U : expected_width;
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(0U, width) });
        return destination;
    }
    if (expression->vhdl != nullptr && qualified_profile) {
        const auto& operands = expression->vhdl->operands;
        const auto operand_width = operands.size() == 1U
            ? hir_expression_width(operands.front(), hir_process_scope_)
            : std::nullopt;
        const auto operand_domain = operands.size() == 1U
            ? hir_expression_domain(operands.front(), hir_process_scope_)
            : std::nullopt;
        const auto operand_subtype = operands.size() == 1U
            ? hir_vhdl_expression_subtype(operands.front())
            : std::nullopt;
        const auto target = referenced
            ? specialized_hir_unit_->find_declaration(*referenced)
            : std::nullopt;
        const auto target_type = target && target->vhdl != nullptr
            ? target->vhdl->declared_type
            : std::nullopt;
        const auto root_type = [&](semantic::TypeId type) {
            std::unordered_set<std::uint32_t> visiting;
            while (type.valid()
                && visiting.insert(type.value()).second) {
                const auto definition
                    = specialized_hir_unit_->find_type(type);
                if (!definition || definition->vhdl == nullptr
                    || (definition->vhdl->form
                            != semantic::vhdl::TypeForm::subtype
                        && definition->vhdl->form
                            != semantic::vhdl::TypeForm::alias)
                    || !definition->vhdl->base.type_mark.target.valid()) {
                    break;
                }
                type = definition->vhdl->base.type_mark.target;
            }
            return type;
        };
        const auto nominal_mismatch = target_type && operand_subtype
            && operand_subtype->type_mark.target.valid()
            && root_type(*target_type)
                != root_type(operand_subtype->type_mark.target);
        const auto operand = operands.size() == 1U
            ? specialized_hir_unit_->find_expression(operands.front())
            : std::nullopt;
        const auto aggregate_operand = operand
            && operand->vhdl != nullptr
            && operand->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate;
        const auto contextual_string_domain = [&] {
            if (!operand || operand->vhdl == nullptr
                || operand->vhdl->kind
                    != semantic::vhdl::ExpressionKind::string_literal
                || !operand->vhdl->decoded_string) {
                return false;
            }
            const auto accepted = [&](const char value) {
                const auto upper = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(value)));
                if (qualified_profile->domain
                    == frontend::ValueDomain::Bit2) {
                    return upper == '0' || upper == '1';
                }
                if (qualified_profile->domain
                    == frontend::ValueDomain::Logic9) {
                    return upper == 'U' || upper == 'X'
                        || upper == '0' || upper == '1'
                        || upper == 'Z' || upper == 'W'
                        || upper == 'L' || upper == 'H'
                        || upper == '-';
                }
                return false;
            };
            return std::ranges::all_of(
                *operand->vhdl->decoded_string, accepted);
        }();
        const auto context_dependent_domain = aggregate_operand
            || contextual_string_domain;
        const auto layout_mismatch = operands.size() != 1U
            || (!aggregate_operand && operand_width
                && *operand_width != qualified_profile->width)
            || (!context_dependent_domain
                && operand_domain
                        && *operand_domain
                            != frontend::ValueDomain::Unknown
                        && *operand_domain != qualified_profile->domain);
        if (nominal_mismatch || layout_mismatch) {
            report(
                "FSIM-ELAB-VHQUAL-003",
                "VHDL qualified expression operand is not compatible "
                "with its type mark",
                hir_source_span(expression->vhdl->source));
            return std::nullopt;
        }
    }
    if (expression->vhdl != nullptr
        && (expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal)) {
        const auto referenced_declaration = referenced
            ? specialized_hir_unit_->find_declaration(*referenced)
            : std::nullopt;
        const auto exact_literal = referenced_declaration
                && referenced_declaration->vhdl != nullptr
                && referenced_declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::enumeration_literal
            ? referenced
            : std::nullopt;
        const semantic::vhdl::EnumerationLiteral* selected = nullptr;
        bool ambiguous { };
        const auto consider_type = [&](const auto& type) {
            if (type.enumeration_literals.empty()) {
                return;
            }
            const auto literal = std::ranges::find_if(
                type.enumeration_literals,
                [&](const auto& candidate) {
                    if (exact_literal) {
                        return candidate.declaration == *exact_literal;
                    }
                    if (expression->vhdl->text.starts_with('\'')) {
                        return candidate.spelling
                            == expression->vhdl->text;
                    }
                    return std::ranges::equal(
                        candidate.spelling,
                        expression->vhdl->text,
                        [](const char left, const char right) {
                            return std::tolower(
                                       static_cast<unsigned char>(left))
                                == std::tolower(
                                    static_cast<unsigned char>(right));
                        });
                });
            if (literal == type.enumeration_literals.end()) {
                return;
            }
            if (!exact_literal) {
                auto maximum_ordinal
                    = type.enumeration_literals.size() - 1U;
                std::size_t ordinal_width { 1U };
                while ((maximum_ordinal >>= 1U) != 0U) {
                    ++ordinal_width;
                }
                if (expected_width != 0U
                    && ordinal_width != expected_width) {
                    return;
                }
            }
            if (selected != nullptr
                && selected->declaration != literal->declaration) {
                ambiguous = true;
                return;
            }
            selected = &*literal;
        };
        for (const auto& type : specialized_hir_unit_->vhdl_types()) {
            consider_type(type);
        }
        for (const auto& type :
            specialized_hir_unit_->design().vhdl_hir.types()) {
            consider_type(type);
        }
        if (ambiguous) {
            selected = nullptr;
        }
        if (selected != nullptr) {
            const auto width = expected_width != 0U
                ? std::optional { expected_width }
                : hir_expression_width(
                      expression_id, hir_process_scope_);
            if (!width || *width == 0U) {
                return std::nullopt;
            }
            const auto domain = hir_expression_domain(
                expression_id, hir_process_scope_)
                                    .value_or(
                                        frontend::ValueDomain::Bit2);
            const auto destination = allocate_register(
                *width, domain);
            process_.operations.emplace_back(LoadConstant {
                destination, unsigned_value(selected->ordinal, *width),
            });
            return destination;
        }
    }
    if (const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
        selection && selection->member == 3U) {
        return lower_hir_vhdl_environment_call_path_scalar_selection(
            expression_id, expected_width);
    }
    if (hir_vhdl_assert_expression_api(expression_id)) {
        return lower_hir_vhdl_assert_expression(
            expression_id, expected_width);
    }
    if (is_hir_vhdl_environment_expression(expression_id)) {
        return lower_hir_vhdl_environment_expression(
            expression_id, expected_width);
    }
    if (is_hir_vhdl_reflection_expression(expression_id)) {
        return lower_hir_vhdl_reflection_expression(
            expression_id, expected_width);
    }
    if (hir_expression_is_residual(expression_id)) {
        return std::nullopt;
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "@vhdl-external") {
        constexpr auto profile_prefix
            = std::string_view { "@fsim-vhdl-external:" };
        const auto& external = *expression->vhdl;
        const auto target = external.operands.size() == 2U
            ? specialized_hir_unit_->find_expression(
                  external.operands.front())
            : std::nullopt;
        const auto profile = std::string_view { external.nominal_type };
        const auto separator = profile.starts_with(profile_prefix)
            ? profile.find(':', profile_prefix.size())
            : std::string_view::npos;
        std::size_t declared_width { };
        unsigned declared_domain { };
        const auto width_text = separator == std::string_view::npos
            ? std::string_view { }
            : profile.substr(
                  profile_prefix.size(), separator - profile_prefix.size());
        const auto domain_text = separator == std::string_view::npos
            ? std::string_view { }
            : profile.substr(separator + 1U);
        const auto width_parse = std::from_chars(
            width_text.data(), width_text.data() + width_text.size(),
            declared_width);
        const auto domain_parse = std::from_chars(
            domain_text.data(), domain_text.data() + domain_text.size(),
            declared_domain);
        const auto signal = target && target->vhdl != nullptr
                && target->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name
            ? design_.signal_by_name_.find(target->vhdl->text)
            : design_.signal_by_name_.end();
        const auto profile_domain
            = static_cast<frontend::ValueDomain>(declared_domain);
        const bool valid_profile = !width_text.empty()
            && width_parse.ec == std::errc { }
            && width_parse.ptr == width_text.data() + width_text.size()
            && !domain_text.empty() && domain_parse.ec == std::errc { }
            && domain_parse.ptr == domain_text.data() + domain_text.size();
        if (!valid_profile || signal == design_.signal_by_name_.end()
            || signal->second >= design_.signal_info_.size()
            || design_.signal_info_[signal->second].width != declared_width
            || design_.signal_info_[signal->second].source_domain
                != profile_domain) {
            report(
                "FSIM-ELAB-VHEXTERNAL-001",
                "VHDL external signal name does not match a bounded signal "
                "with the declared subtype",
                hir_source_span(external.source));
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[signal->second];
        auto destination = allocate_register(
            info.width, info.source_domain);
        process_.operations.emplace_back(ReadSignal {
            destination,
            signal->second,
            sample_concurrent_assertion_reads_
                ? SignalReadKind::sampled
                : SignalReadKind::current,
        });
        implicit_signal_dependencies_.push_back(signal->second);
        if (expected_width != 0U && expected_width != info.width) {
            destination = resize_register(
                destination, expected_width, info.is_signed);
        }
        return destination;
    }
    if (const auto handle = hir_systemverilog_interface_handle(
            expression_id)) {
        auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(*handle, 64U) });
        if (expected_width != 0U && expected_width != 64U) {
            destination = resize_register(
                destination, expected_width, false);
        }
        return destination;
    }
    if (const auto signal = hir_direct_signal(expression_id)) {
        const auto& info = design_.signal_info_[*signal];
        auto destination = allocate_register(
            info.width, info.source_domain);
        process_.operations.emplace_back(ReadSignal {
            destination,
            *signal,
            sample_concurrent_assertion_reads_
                ? SignalReadKind::sampled
                : SignalReadKind::current,
        });
        implicit_signal_dependencies_.push_back(*signal);
        if (expected_width != 0U && expected_width != info.width) {
            destination = resize_register(
                destination, expected_width, info.is_signed);
        }
        return destination;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::name) {
        const auto& name = expression->systemverilog->text;
        const auto separator = name.find('.');
        if (separator != std::string::npos
            && std::ranges::find(
                   design_.roots_, name.substr(0U, separator))
                != design_.roots_.end()) {
            report(
                "FSIM-ELAB-ROOT-001",
                "cross-root hierarchical reference '" + name
                    + "' is not a direct signal on a selected root",
                hir_source_span(expression->systemverilog->source));
            return std::nullopt;
        }
    }
    const auto invalid_integral_result = [&]() {
        const auto width = expected_width == 0U ? 32U : expected_width;
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(0U, width) });
        return std::optional<RegisterId> { destination };
    };
    const auto runtime_binding = referenced
        ? hir_runtime_binding(*referenced, hir_process_scope_, false)
        : std::nullopt;
    const auto writable_declaration = [&] {
        if (!referenced) {
            return false;
        }
        const auto declaration = specialized_hir_unit_->find_declaration(
            *referenced);
        if (!declaration) {
            return false;
        }
        if (declaration->systemverilog != nullptr) {
            const auto form = declaration->systemverilog->form;
            return form == semantic::sv::DeclarationForm::port
                || form == semantic::sv::DeclarationForm::net
                || form == semantic::sv::DeclarationForm::variable;
        }
        if (declaration->vhdl == nullptr) {
            return false;
        }
        const auto form = declaration->vhdl->form;
        return form == semantic::vhdl::DeclarationForm::port
            || form == semantic::vhdl::DeclarationForm::signal
            || form == semantic::vhdl::DeclarationForm::variable;
    }();
    const auto specialization_name = [&] {
        if (runtime_binding || writable_declaration) {
            return false;
        }
        if (expression->systemverilog != nullptr) {
            return expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name;
        }
        return expression->vhdl != nullptr
            && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name;
    }();
    const auto& residuals
        = specialized_hir_unit_->specialization().residual_expressions;
    const auto specialization_fold_candidate = specialization_name
        || std::ranges::binary_search(residuals, expression_id)
        || (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call);
    // Only defined pure VHDL functions and selections rooted directly at a
    // declared constant are eligible: all other calls or selections may
    // observe runtime state or effects and must remain on ordinary lowering.
    const auto vhdl_constant_candidate = [&] {
        if (expression->vhdl == nullptr) {
            return false;
        }
        const auto& source = *expression->vhdl;
        if (source.kind == semantic::vhdl::ExpressionKind::call) {
            const auto resolution = resolve_hir_vhdl_function_call(
                expression_id, hir_process_scope_, expected_width);
            const auto declaration = resolution
                ? specialized_hir_unit_->find_declaration(resolution->body)
                : std::nullopt;
            const auto callable = declaration
                    && declaration->vhdl != nullptr
                    ? declaration->vhdl->callable
                    : std::nullopt;
            const bool eligible = callable && callable->function
                && callable->pure && callable->defined;
            return eligible;
        }
        if ((source.kind != semantic::vhdl::ExpressionKind::index
                && source.kind != semantic::vhdl::ExpressionKind::slice)
            || source.operands.empty()) {
            return false;
        }
        const auto base = hir_referenced_declaration(
            source.operands.front());
        return base && hir_constant_initializer(*base).has_value();
    }();
    // The constant evaluator does not carry instantiated-package bindings
    // through pure-call bodies. Inspect the transitive HIR before folding;
    // package-dependent expressions keep their existing runtime lowering.
    const auto vhdl_constant_uses_specialized_package_binding = [&] {
        if (!vhdl_constant_candidate) {
            return false;
        }
        std::unordered_set<std::uint32_t> visited_expressions;
        std::unordered_set<std::uint32_t> visited_declarations;
        std::unordered_set<std::uint32_t> visited_statements;
        std::vector<semantic::DeclarationId> pending_declarations;
        const auto expression_uses_binding
            = [&](const auto& self,
                  const semantic::ExpressionId candidate) -> bool {
            if (!visited_expressions.insert(candidate.value()).second) {
                return false;
            }
            const auto record
                = specialized_hir_unit_->find_expression(candidate);
            if (!record || record->vhdl == nullptr) {
                return false;
            }
            const auto& source = *record->vhdl;
            if (source.referenced_name) {
                if (source.kind == semantic::vhdl::ExpressionKind::call) {
                    const auto resolution = resolve_hir_vhdl_function_call(
                        candidate, source.scope, 0U);
                    if (resolution) {
                        if (!resolution->generic_bindings.empty()) {
                            return true;
                        }
                        pending_declarations.push_back(resolution->body);
                    }
                } else {
                    const auto packages
                        = semantic::CompiledDesignResolver {
                              *specialized_hir_unit_,
                              hir_generic_binding_frames_
                          }
                              .resolve_vhdl_package_members(
                                  *source.referenced_name, source.scope);
                    if (std::ranges::any_of(
                            packages.candidates,
                            [](const auto& package) {
                                return !package.generic_bindings.empty();
                            })) {
                        return true;
                    }
                }
            }
            return std::ranges::any_of(
                source.operands,
                [&](const semantic::ExpressionId operand) {
                    return self(self, operand);
                });
        };
        const auto statement_uses_binding
            = [&](const auto& self,
                  const semantic::StatementId candidate) -> bool {
            if (!visited_statements.insert(candidate.value()).second) {
                return false;
            }
            const auto record
                = specialized_hir_unit_->find_statement(candidate);
            if (!record || record->vhdl == nullptr) {
                return false;
            }
            const auto& source = *record->vhdl;
            const auto expression_uses = [&](
                const std::optional<semantic::ExpressionId> expression_id) {
                return expression_id
                    && expression_uses_binding(
                        expression_uses_binding, *expression_id);
            };
            if (expression_uses(source.target)
                || expression_uses(source.value)
                || expression_uses(source.condition)
                || expression_uses(source.loop_initial)
                || expression_uses(source.loop_limit)
                || expression_uses(source.guard)
                || expression_uses(source.report)
                || expression_uses(source.severity)
                || std::ranges::any_of(
                    source.waveform,
                    [&](const auto& waveform) {
                        return expression_uses_binding(
                            expression_uses_binding, waveform.value);
                    })
                || std::ranges::any_of(
                    source.procedure_arguments,
                    [&](const auto& argument) {
                        return expression_uses_binding(
                            expression_uses_binding, argument.actual);
                    })) {
                return true;
            }
            const auto nested_statement_uses = [&](
                const auto& statements) {
                return std::ranges::any_of(
                    statements,
                    [&](const semantic::StatementId nested) {
                        return self(self, nested);
                    });
            };
            if (nested_statement_uses(source.statements)
                || nested_statement_uses(source.else_statements)) {
                return true;
            }
            return std::ranges::any_of(
                source.alternatives,
                [&](const auto& alternative) {
                    return std::ranges::any_of(
                               alternative.choices,
                               [&](const semantic::ExpressionId choice) {
                                   return expression_uses_binding(
                                       expression_uses_binding, choice);
                               })
                        || nested_statement_uses(
                            alternative.statements);
                });
        };

        if (expression_uses_binding(
                expression_uses_binding, expression_id)) {
            return true;
        }
        while (!pending_declarations.empty()) {
            const auto declaration_id = pending_declarations.back();
            pending_declarations.pop_back();
            if (!visited_declarations.insert(
                    declaration_id.value()).second) {
                continue;
            }
            const auto declaration
                = specialized_hir_unit_->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            if (std::ranges::any_of(
                    declaration->vhdl->children,
                    [&](const semantic::DeclarationId child_id) {
                        const auto child
                            = specialized_hir_unit_->find_declaration(
                                child_id);
                        return child && child->vhdl != nullptr
                            && child->vhdl->initializer
                            && expression_uses_binding(
                                expression_uses_binding,
                                *child->vhdl->initializer);
                    })
                || std::ranges::any_of(
                    declaration->vhdl->statements,
                    [&](const semantic::StatementId statement) {
                        return statement_uses_binding(
                            statement_uses_binding, statement);
                    })) {
                return true;
            }
        }
        return false;
    }();
    const auto safe_vhdl_constant_candidate = vhdl_constant_candidate
        && !vhdl_constant_uses_specialized_package_binding;
    // An unconstrained packed function result can have a one-bit HIR
    // placeholder even when its direct constant target supplies a fully
    // constrained shape. Use that context only when the call is uniquely the
    // initializer of a constrained packed constant and the evaluator result
    // later matches the target's exact width, bounds, and domain.
    struct VhdlConstantTargetShape {
        std::size_t width { };
        HirPackedRange range;
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    };
    const auto vhdl_constant_target_shape = [&]()
        -> std::optional<VhdlConstantTargetShape> {
        if (!safe_vhdl_constant_candidate || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call) {
            return std::nullopt;
        }
        std::optional<VhdlConstantTargetShape> result;
        // The initializer's owning constant may be in a linked unit rather
        // than the currently specialized unit.
        for (const auto& declaration :
            specialized_hir_unit_->design().vhdl_hir.declarations()) {
            if (declaration.form
                    != semantic::vhdl::DeclarationForm::constant
                || !declaration.initializer
                || *declaration.initializer != expression_id
                || !declaration.subtype) {
                continue;
            }
            // The expression identity must name exactly one direct constant
            // initializer; shared or ambiguous ownership has no contextual
            // subtype proof and keeps the ordinary call shape checks.
            if (result) {
                return std::nullopt;
            }
            const auto subtype = hir_effective_vhdl_subtype(
                *declaration.subtype);
            if (!subtype || subtype->unconstrained
                || subtype->constraints.size() != 1U) {
                return std::nullopt;
            }
            const auto& constraint = subtype->constraints.front();
            if (constraint.kind
                    != semantic::vhdl::RangeKind::array_index
                || constraint.null) {
                return std::nullopt;
            }
            const auto left = constraint.left
                ? constraint.left
                : constraint.left_expression
                ? hir_constant_integer(*constraint.left_expression)
                : std::nullopt;
            const auto right = constraint.right
                ? constraint.right
                : constraint.right_expression
                ? hir_constant_integer(*constraint.right_expression)
                : std::nullopt;
            if (!left || !right
                || (*left != *right
                    && constraint.descending != (*left > *right))) {
                return std::nullopt;
            }
            const auto distance = index_distance(*left, *right);
            if (distance
                    >= std::numeric_limits<std::size_t>::max()
                || (subtype->executable_width
                    && *subtype->executable_width != distance + 1U)) {
                return std::nullopt;
            }
            const auto width = static_cast<std::size_t>(distance + 1U);
            if (width == 0U
                || (expected_width != 0U && expected_width != width)) {
                return std::nullopt;
            }
            const auto domain = [&]() {
                switch (subtype->domain) {
                case semantic::vhdl::ValueDomain::bit2:
                    return frontend::ValueDomain::Bit2;
                case semantic::vhdl::ValueDomain::logic4:
                    return frontend::ValueDomain::Logic4;
                case semantic::vhdl::ValueDomain::logic9:
                    return frontend::ValueDomain::Logic9;
                default:
                    return frontend::ValueDomain::Unknown;
                }
            }();
            if (domain == frontend::ValueDomain::Unknown) {
                return std::nullopt;
            }
            result = VhdlConstantTargetShape {
                width,
                HirPackedRange {
                    *left, *right, constraint.descending },
                domain,
            };
        }
        return result;
    }();
    const auto fold_candidate = specialization_fold_candidate
        || safe_vhdl_constant_candidate;
    const auto expression_contains_call
        = [&](const auto& self,
              const semantic::ExpressionId candidate,
              std::unordered_set<std::uint32_t>& visiting) -> bool {
        if (!visiting.insert(candidate.value()).second) {
            return false;
        }
        const auto record = specialized_hir_unit_->find_expression(
            candidate);
        if (!record || record->systemverilog == nullptr) {
            return false;
        }
        if (record->systemverilog->kind
            == semantic::sv::ExpressionKind::call) {
            return true;
        }
        return std::ranges::any_of(
            record->systemverilog->operands,
            [&](const semantic::ExpressionId operand) {
                return self(self, operand, visiting);
            });
    };
    const auto side_effect_free_callable
        = [&](const semantic::DeclarationId declaration_id) -> bool {
        const auto declaration
            = specialized_hir_unit_->find_declaration(declaration_id);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->callable
            || !declaration->systemverilog->callable->function) {
            return false;
        }
        if (declaration->systemverilog->callable->lifetime
                != semantic::sv::Lifetime::automatic
            && std::ranges::any_of(
                declaration->systemverilog->children,
                [&](const semantic::DeclarationId child) {
                    const auto local
                        = specialized_hir_unit_->find_declaration(child);
                    return local && local->systemverilog != nullptr
                        && local->systemverilog->form
                            == semantic::sv::DeclarationForm::variable;
                })) {
            return false;
        }
        std::unordered_set<std::uint32_t> local_declarations {
            declaration_id.value()
        };
        for (const auto formal :
            declaration->systemverilog->callable->formals) {
            const auto record
                = specialized_hir_unit_->find_declaration(formal);
            if (!record || record->systemverilog == nullptr
                || record->systemverilog->direction
                    != semantic::sv::Direction::input) {
                // Output, inout, and ref arguments remain observable in the
                // caller even when the function result itself is constant.
                // They therefore cannot be erased by specialization-time
                // constant evaluation.
                return false;
            }
            local_declarations.insert(formal.value());
        }
        for (const auto child : declaration->systemverilog->children) {
            local_declarations.insert(child.value());
        }
        const auto expression_is_call_free
            = [&](const std::optional<semantic::ExpressionId>
                      candidate_expression) {
            if (!candidate_expression) {
                return true;
            }
            std::unordered_set<std::uint32_t> visiting;
            return !expression_contains_call(
                expression_contains_call, *candidate_expression, visiting);
        };
        const auto statement_is_side_effect_free
            = [&](const auto& statement_self,
                  const semantic::StatementId statement_id) -> bool {
            const auto statement
                = specialized_hir_unit_->find_statement(statement_id);
            if (!statement || statement->systemverilog == nullptr) {
                return false;
            }
            const auto& source = *statement->systemverilog;
            switch (source.kind) {
            case semantic::sv::StatementKind::assignment: {
                if (!source.target || !source.value
                    || !expression_is_call_free(source.value)) {
                    return false;
                }
                const auto target = hir_referenced_declaration(
                    *source.target);
                return target
                    && local_declarations.contains(target->value());
            }
            case semantic::sv::StatementKind::return_statement:
                return expression_is_call_free(source.value);
            case semantic::sv::StatementKind::null_statement:
                return true;
            case semantic::sv::StatementKind::block:
                return std::ranges::all_of(
                    source.statements,
                    [&](const semantic::StatementId nested) {
                        return statement_self(statement_self, nested);
                    });
            default:
                return false;
            }
        };
        const auto result = std::ranges::all_of(
            declaration->systemverilog->statements,
            [&](const semantic::StatementId statement) {
                return statement_is_side_effect_free(
                    statement_is_side_effect_free, statement);
            });
        return result;
    };
    std::unordered_set<std::uint32_t> call_expression_guard;
    const auto contains_systemverilog_call
        = [&](const auto& self, const semantic::ExpressionId candidate)
        -> bool {
        if (!call_expression_guard.insert(candidate.value()).second) {
            return false;
        }
        const auto record = specialized_hir_unit_->find_expression(
            candidate);
        if (!record) {
            return false;
        }
        if (record->systemverilog != nullptr
            && record->systemverilog->kind
                == semantic::sv::ExpressionKind::call) {
            if (record->systemverilog->text == "$isunbounded"
                && specialized_hir_unit_->evaluate_integral_expression(
                    candidate)) {
                return false;
            }
            const auto declaration = hir_referenced_declaration(candidate);
            if (!declaration
                || !side_effect_free_callable(*declaration)) {
                return true;
            }
        }
        const auto operands = record->systemverilog != nullptr
            ? std::span<const semantic::ExpressionId> {
                  record->systemverilog->operands
              }
            : record->vhdl != nullptr
            ? std::span<const semantic::ExpressionId> {
                  record->vhdl->operands
              }
            : std::span<const semantic::ExpressionId> { };
        return std::ranges::any_of(operands,
            [&](const semantic::ExpressionId operand) {
                return self(self, operand);
            });
    };
    const auto systemverilog_call = fold_candidate
        && contains_systemverilog_call(
            contains_systemverilog_call, expression_id);
    // A SystemVerilog function may update enclosing variables. Fold only
    // retained-HIR functions whose assignments are confined to their return,
    // formals, and local declarations. Potentially effectful calls must stay
    // executable after specialization.
    if (!systemverilog_call && fold_candidate) {
        const auto width = expected_width != 0U
            ? std::optional { expected_width }
            : hir_expression_width(expression_id, hir_process_scope_);
        if (expression->systemverilog != nullptr && width
            && *width != 0U) {
            std::string error;
            const auto value = evaluate_hir_systemverilog_constant(
                *specialized_hir_unit_, expression_id, error);
            if (value && !value->unbounded) {
                const auto domain = hir_expression_domain(
                    expression_id, hir_process_scope_)
                                        .value_or(value->domain);
                const auto destination = allocate_register(*width, domain);
                process_.operations.emplace_back(LoadConstant {
                    destination, resize_hir_constant(*value, *width) });
                return destination;
            }
        }
        if (safe_vhdl_constant_candidate) {
            // The semantic evaluator binds each pure call's positional
            // actuals to its formals and returns only fully static values.
            // A missing or mismatched value deliberately falls through to
            // the original runtime-capable lowering path below.
            const auto value
                = specialized_hir_unit_->evaluate_vhdl_constant_expression(
                    expression_id);
            auto packed = value
                ? std::get_if<semantic::SpecializedHirVhdlPackedValue>(
                      &*value)
                : nullptr;
            const auto inferred_width = hir_expression_width(
                expression_id, hir_process_scope_);
            const auto range = hir_expression_range(
                expression_id, hir_process_scope_);
            const auto domain = hir_expression_domain(
                expression_id, hir_process_scope_);
            std::optional<semantic::SpecializedHirVhdlPackedValue>
                constant_rooted_slice;
            // The semantic slice evaluator can miss hierarchy-local bounds.
            // Reconstruct only a slice directly rooted at one declared
            // constant, and only from its independently evaluated packed
            // initializer and exact constrained subtype. Base evaluation is
            // work-bounded by the semantic API; the selected copy cannot
            // exceed that base or the SimIR register-width limit.
            if (packed == nullptr && !range
                && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::slice
                && expression->vhdl->operands.size() == 3U
                && (expression->vhdl->text == "to"
                    || expression->vhdl->text == "downto")) {
                const auto& source = *expression->vhdl;
                const auto base_expression = source.operands.front();
                const auto base_id
                    = hir_referenced_declaration(base_expression);
                const auto base_declaration = base_id
                    ? specialized_hir_unit_->find_declaration(*base_id)
                    : std::nullopt;
                const auto initializer = base_id
                    ? hir_constant_initializer(*base_id)
                    : std::nullopt;
                const auto base_subtype = base_declaration
                        && base_declaration->vhdl != nullptr
                        && base_declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::constant
                        && base_declaration->vhdl->subtype
                    ? hir_effective_vhdl_subtype(
                          *base_declaration->vhdl->subtype)
                    : std::nullopt;
                const auto base_width = hir_expression_width(
                    base_expression, hir_process_scope_);
                const auto base_range = hir_expression_range(
                    base_expression, hir_process_scope_);
                const auto base_domain = base_subtype
                    ? vhdl_subtype_domain(base_subtype->domain)
                    : frontend::ValueDomain::Unknown;
                const auto base_value
                    = base_id && initializer
                    ? specialized_hir_unit_->evaluate_vhdl_constant_expression(
                          base_expression)
                    : std::nullopt;
                const auto base_packed = base_value
                    ? std::get_if<
                          semantic::SpecializedHirVhdlPackedValue>(
                          &*base_value)
                    : nullptr;
                const auto left = hir_constant_integer(
                    source.operands[1U]);
                const auto right = hir_constant_integer(
                    source.operands[2U]);
                if (base_subtype && !base_subtype->unconstrained
                    && base_subtype->constraints.size() == 1U
                    && base_subtype->constraints.front().kind
                        == semantic::vhdl::RangeKind::array_index
                    && !base_subtype->constraints.front().null
                    && base_width && base_range && base_packed
                    && left && right
                    && (base_domain == frontend::ValueDomain::Bit2
                        || base_domain == frontend::ValueDomain::Logic4
                        || base_domain == frontend::ValueDomain::Logic9)
                    && domain == base_domain) {
                    const auto& declared_range
                        = base_subtype->constraints.front();
                    const auto declared_left = declared_range.left
                        ? declared_range.left
                        : declared_range.left_expression
                        ? hir_constant_integer(
                              *declared_range.left_expression)
                        : std::nullopt;
                    const auto declared_right = declared_range.right
                        ? declared_range.right
                        : declared_range.right_expression
                        ? hir_constant_integer(
                              *declared_range.right_expression)
                        : std::nullopt;
                    if (declared_left && declared_right
                        && (*declared_left == *declared_right
                            || declared_range.descending
                                == (*declared_left > *declared_right))) {
                        const auto base_distance = index_distance(
                            *declared_left, *declared_right);
                        const auto selected_distance = index_distance(
                            *left, *right);
                        if (base_distance
                                < std::numeric_limits<std::size_t>::max()
                            && selected_distance
                                < std::numeric_limits<std::size_t>::max()) {
                            const auto declared_width
                                = static_cast<std::size_t>(base_distance + 1U);
                            const auto selected_width
                                = static_cast<std::size_t>(
                                      selected_distance + 1U);
                            const bool descending
                                = source.text == "downto";
                            const auto lower_bound = std::min(
                                *declared_left, *declared_right);
                            const auto upper_bound = std::max(
                                *declared_left, *declared_right);
                            if (declared_width == *base_width
                                && declared_width == base_packed->bits.size()
                                && base_packed->left_bound == *declared_left
                                && base_packed->right_bound == *declared_right
                                && base_range->left == *declared_left
                                && base_range->right == *declared_right
                                && base_range->descending
                                    == declared_range.descending
                                && (!base_subtype->executable_width
                                    || *base_subtype->executable_width
                                        == declared_width)
                                && descending == declared_range.descending
                                && (*left == *right
                                    || (descending
                                        ? *left > *right
                                        : *left < *right))
                                && *left >= lower_bound
                                && *left <= upper_bound
                                && *right >= lower_bound
                                && *right <= upper_bound
                                && inferred_width
                                && selected_width != 0U
                                && selected_width == *inferred_width
                                && (expected_width == 0U
                                    || selected_width == expected_width)
                                && declared_width
                                    <= std::numeric_limits<std::uint32_t>::max()
                                && selected_width
                                    <= std::numeric_limits<std::uint32_t>::max()) {
                                const auto offset = descending
                                    ? index_distance(
                                          *declared_left, *left)
                                    : index_distance(
                                          *left, *declared_left);
                                if (offset <= base_packed->bits.size()
                                    && selected_width
                                        <= base_packed->bits.size()
                                            - static_cast<std::size_t>(offset)) {
                                    constant_rooted_slice
                                        = semantic::SpecializedHirVhdlPackedValue {
                                            base_packed->bits.substr(
                                                static_cast<std::size_t>(offset),
                                                selected_width),
                                            *left,
                                            *right,
                                        };
                                }
                            }
                        }
                    }
                }
            }
            if (constant_rooted_slice) {
                packed = &*constant_rooted_slice;
            }
            const bool contextual_target_shape_matches
                = packed != nullptr && vhdl_constant_target_shape
                && packed->bits.size()
                    == vhdl_constant_target_shape->width
                && packed->left_bound
                    == vhdl_constant_target_shape->range.left
                && packed->right_bound
                    == vhdl_constant_target_shape->range.right
                && domain == vhdl_constant_target_shape->domain;
            const bool packed_domain = domain
                && (*domain == frontend::ValueDomain::Bit2
                    || *domain == frontend::ValueDomain::Logic4
                    || *domain == frontend::ValueDomain::Logic9);
            const bool two_state_bits = domain
                    != frontend::ValueDomain::Bit2
                || (packed != nullptr
                    && std::ranges::all_of(
                        packed->bits,
                        [](const char bit) {
                            return bit == '0' || bit == '1';
                        }));
            const bool inferred_width_matches = packed != nullptr
                && ((inferred_width
                        && packed->bits.size() == *inferred_width)
                    || contextual_target_shape_matches);
            const bool expected_width_matches = packed != nullptr
                && (expected_width == 0U
                    || expected_width == packed->bits.size());
            const bool range_matches = packed != nullptr && range
                && packed->left_bound == range->left
                && packed->right_bound == range->right;
            const bool constant_slice_bounds_match = [&] {
                if (packed == nullptr || range
                    || expression->vhdl == nullptr
                    || expression->vhdl->kind
                        != semantic::vhdl::ExpressionKind::slice
                    || expression->vhdl->operands.size() != 3U
                    || (expression->vhdl->text != "to"
                        && expression->vhdl->text != "downto")) {
                    return false;
                }
                const auto base = hir_referenced_declaration(
                    expression->vhdl->operands.front());
                const auto left = hir_constant_integer(
                    expression->vhdl->operands[1U]);
                const auto right = hir_constant_integer(
                    expression->vhdl->operands[2U]);
                if (!base || !hir_constant_initializer(*base)
                    || !left || !right
                    || packed->left_bound != *left
                    || packed->right_bound != *right) {
                    return false;
                }
                return *left == *right
                    || (expression->vhdl->text == "downto"
                        ? *left > *right
                        : *left < *right);
            }();
            const bool shape_range_matches
                = range_matches || constant_slice_bounds_match
                || contextual_target_shape_matches;
            // Preserve the expression's declared shape; never resize or
            // reinterpret evaluator bits to make an incompatible type fit.
            // A named subtype may hide its selected slice range from
            // hir_expression_range; in that case accept only selector bounds
            // independently resolved to constants and matched to evaluator.
            if (packed != nullptr && packed_domain && inferred_width
                && (range || constant_slice_bounds_match
                    || contextual_target_shape_matches)
                && !packed->bits.empty()
                && two_state_bits
                && inferred_width_matches
                && expected_width_matches
                && shape_range_matches
                && packed->bits.size()
                    <= std::numeric_limits<std::uint32_t>::max()) {
                try {
                    auto constant = *domain
                            == frontend::ValueDomain::Logic9
                        ? PackedLogic4::from_logic9_msb_string(
                              packed->bits)
                        : PackedLogic4::from_msb_string(packed->bits);
                    const auto destination = allocate_register(
                        packed->bits.size(), *domain);
                    process_.operations.emplace_back(LoadConstant {
                        destination, std::move(constant) });
                    return destination;
                } catch (const std::invalid_argument&) {
                    // Unsupported or malformed packed values remain on the
                    // ordinary lowering path rather than being coerced.
                }
            }
        }
        const auto value = hir_vhdl_active_package_constant(expression_id)
            ? hir_constant_integer(expression_id)
            : specialized_hir_unit_->evaluate_integral_expression(
                  expression_id);
        if (value && width && *width != 0U) {
            const auto domain = hir_expression_domain(
                expression_id, hir_process_scope_)
                                    .value_or(
                                        language_
                                                == frontend::Language::Vhdl2008
                                            ? frontend::ValueDomain::Integer
                                            : frontend::ValueDomain::Bit2);
            const auto destination = allocate_register(*width, domain);
            process_.operations.emplace_back(LoadConstant {
                destination,
                unsigned_value(
                    static_cast<std::uint64_t>(*value), *width),
            });
            return destination;
        }
    }
    const auto source = expression_leaf(*expression);
    std::optional<frontend::SystemVerilogScalarConstant> scalar_literal;
    std::string scalar_literal_error;
    if (expression->systemverilog != nullptr
        && expression->systemverilog->decimal_literal) {
        scalar_literal = evaluate_hir_systemverilog_scalar_constant(
            *specialized_hir_unit_, expression_id, scalar_literal_error);
        auto target = scalar_context;
        if (target == frontend::SystemVerilogScalarKind::None) {
            target = static_cast<frontend::SystemVerilogScalarKind>(
                expression->systemverilog->scalar_kind);
        }
        if (scalar_literal
            && target != frontend::SystemVerilogScalarKind::None) {
            scalar_literal
                = frontend::convert_systemverilog_scalar_constant(
                    *scalar_literal, target, scalar_literal_error);
        }
        if (!scalar_literal) {
            report(
                "FSIM-ELAB-SVSCALAR-001",
                "cannot convert scalar literal '"
                    + expression->systemverilog->text + "': "
                    + scalar_literal_error,
                hir_source_span(expression->systemverilog->source));
            return std::nullopt;
        }
    }
    const auto real_literal = expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::integer_literal
        ? systemverilog_real_literal(expression->systemverilog->text)
        : expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::real_literal
        ? systemverilog_real_literal(expression->vhdl->text)
        : std::nullopt;
    const auto overloaded_vhdl_operator
        = expression->vhdl != nullptr
        && (expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::unary
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::binary)
        && expression->vhdl->referenced_name
        && resolve_hir_vhdl_function_call(
            expression_id, hir_process_scope_, expected_width);
    if (overloaded_vhdl_operator) {
        return lower_hir_function_call(expression_id, expected_width);
    }
    std::optional<RegisterId> result;
    if (expression->systemverilog != nullptr && source.call
        && systemverilog_sampled_value_call(source.text)) {
        const auto& sampled = *expression->systemverilog;
        std::vector<std::optional<semantic::ExpressionId>> arguments;
        if (!sampled.call_arguments.empty()) {
            arguments.reserve(sampled.call_arguments.size());
            std::ranges::transform(
                sampled.call_arguments, std::back_inserter(arguments),
                [](const semantic::sv::CallAssociation& association) {
                    return association.actual;
                });
        } else {
            arguments.reserve(sampled.operands.size());
            std::ranges::transform(
                sampled.operands, std::back_inserter(arguments),
                [](const semantic::ExpressionId operand) {
                    return std::optional { operand };
                });
        }
        const auto argument = [&](const std::size_t index)
            -> std::optional<semantic::ExpressionId> {
            return index < arguments.size() ? arguments[index]
                                            : std::nullopt;
        };
        const auto argument_record = [&](const std::size_t index) {
            const auto id = argument(index);
            return id ? specialized_hir_unit_->find_expression(*id)
                      : std::optional<semantic::CompiledExpressionView> { };
        };
        const auto argument_span = [&](const std::size_t index) {
            const auto record = argument_record(index);
            return record && record->systemverilog != nullptr
                ? hir_source_span(record->systemverilog->source)
                : hir_source_span(sampled.source);
        };
        const bool past = source.text == "$past";
        const bool global = source.text.ends_with("_gclk");
        const auto maximum_arity = global ? 1U : past ? 4U
                                                      : 2U;
        const bool named = std::ranges::any_of(
                               sampled.argument_names, [](const std::string& name) {
                                   return !name.empty();
                               })
            || std::ranges::any_of(sampled.call_arguments, [](const semantic::sv::CallAssociation& association) {
                   return association.formal.has_value();
               });
        const auto signal_expression = argument_record(0U);
        if (language_ != frontend::Language::SystemVerilog2017 || named
            || arguments.empty() || arguments.size() > maximum_arity
            || !signal_expression
            || signal_expression->systemverilog == nullptr
            || signal_expression->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            report(
                "FSIM-ELAB-SVSAMPLE-001",
                std::string { source.text }
                    + " currently requires one direct packed signal"
                    + (past
                            ? ", optional constant positive depth, direct "
                              "gate, and direct clocking event"
                            : " and an optional direct clocking event"),
                hir_source_span(sampled.source));
            return std::nullopt;
        }
        const auto signal = hir_direct_signal(*arguments.front());
        const auto width = signal
                && *signal < design_.signal_info_.size()
            ? std::optional<std::size_t> {
                  design_.signal_info_[*signal].width
              }
            : std::nullopt;
        if (!signal || !width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVSAMPLE-001",
                std::string { source.text }
                    + " argument must be a visible, statically sized packed "
                      "signal",
                argument_span(0U));
            return std::nullopt;
        }
        std::uint32_t ticks { 1U };
        if (past && argument(1U)) {
            const auto value = hir_constant_integer(*argument(1U));
            if (!value || *value <= 0
                || static_cast<std::uint64_t>(*value) > 4096U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past depth must be a positive elaboration-time "
                    "constant no greater than 4096",
                    argument_span(1U));
                return std::nullopt;
            }
            ticks = static_cast<std::uint32_t>(*value);
        }
        std::optional<SignalId> gate;
        if (past && argument(2U)) {
            const auto gate_expression = argument_record(2U);
            if (!gate_expression
                || gate_expression->systemverilog == nullptr
                || gate_expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past gating currently requires one direct packed "
                    "signal",
                    argument_span(2U));
                return std::nullopt;
            }
            gate = hir_direct_signal(*argument(2U));
            if (!gate || *gate >= design_.signal_info_.size()
                || design_.signal_info_[*gate].width != 1U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past gating signal must be visible and scalar",
                    argument_span(2U));
                return std::nullopt;
            }
        }
        const auto clock_index = past ? 3U : 1U;
        std::optional<SignalId> clock;
        SampledClockEdge clock_edge { SampledClockEdge::any };
        if (argument(clock_index)) {
            const auto event = argument_record(clock_index);
            const auto event_source
                = event && event->systemverilog != nullptr
                ? event->systemverilog
                : nullptr;
            const auto event_signal
                = event_source && event_source->operands.size() == 1U
                ? specialized_hir_unit_->find_expression(
                      event_source->operands.front())
                : std::optional<semantic::CompiledExpressionView> { };
            if (!event_source
                || event_source->kind != semantic::sv::ExpressionKind::call
                || event_source->text != "@sv-clocking-event"
                || event_source->operands.size() != 1U
                || !event_signal || event_signal->systemverilog == nullptr
                || event_signal->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    std::string { source.text }
                        + " clocking event must name one direct signal",
                    argument_span(clock_index));
                return std::nullopt;
            }
            clock = hir_direct_signal(event_source->operands.front());
            if (!clock || *clock >= design_.signal_info_.size()
                || design_.signal_info_[*clock].width != 1U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    std::string { source.text }
                        + " clocking signal must be visible and scalar",
                    argument_span(clock_index));
                return std::nullopt;
            }
            clock_edge = event_source->clocking_edge
                    == semantic::sv::EdgeKind::positive
                ? SampledClockEdge::positive
                : event_source->clocking_edge
                    == semantic::sv::EdgeKind::negative
                ? SampledClockEdge::negative
                : SampledClockEdge::any;
        }
        const auto kind = source.text == "$sampled"
            ? SignalReadKind::sampled
            : source.text == "$rose" || source.text == "$rose_gclk"
            ? SignalReadKind::rose
            : source.text == "$fell" || source.text == "$fell_gclk"
            ? SignalReadKind::fell
            : source.text == "$stable" || source.text == "$stable_gclk"
            ? SignalReadKind::stable
            : source.text == "$changed" || source.text == "$changed_gclk"
            ? SignalReadKind::changed
            : source.text == "$future_gclk"
            ? SignalReadKind::future
            : source.text == "$rising_gclk"
            ? SignalReadKind::rising
            : source.text == "$falling_gclk"
            ? SignalReadKind::falling
            : source.text == "$steady_gclk"
            ? SignalReadKind::steady
            : source.text == "$changing_gclk"
            ? SignalReadKind::changing
            : SignalReadKind::past;
        const auto result_width = kind == SignalReadKind::sampled
                || kind == SignalReadKind::past
                || kind == SignalReadKind::future
            ? *width
            : 1U;
        const auto result_domain = kind == SignalReadKind::sampled
                || kind == SignalReadKind::past
                || kind == SignalReadKind::future
            ? design_.signal_info_[*signal].source_domain
            : frontend::ValueDomain::Bit2;
        const auto destination = allocate_register(
            result_width, result_domain);
        process_.operations.emplace_back(ReadSignal {
            destination,
            *signal,
            kind,
            ticks,
            clock,
            clock_edge,
            gate,
        });
        return destination;
    }
    const auto streaming = expression->systemverilog != nullptr
        && source.call
        && (source.text == "@stream-left"
            || source.text == "@stream-right");
    if (streaming) {
        if (source.operands.size() < 2U) {
            return std::nullopt;
        }
        const auto slice_size = hir_constant_integer(
            source.operands.front());
        if (!slice_size || *slice_size <= 0
            || static_cast<std::uint64_t>(*slice_size)
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        std::vector<RegisterId> operands;
        operands.reserve(source.operands.size() - 1U);
        std::size_t width { };
        for (std::size_t index = 1U;
            index < source.operands.size(); ++index) {
            const auto operand_width = hir_expression_width(
                source.operands[index], hir_process_scope_);
            if (!operand_width || *operand_width == 0U
                || *operand_width
                    > std::numeric_limits<std::uint32_t>::max() - width) {
                return std::nullopt;
            }
            const auto operand = lower_hir_expression(
                source.operands[index], *operand_width);
            if (!operand) {
                return std::nullopt;
            }
            width += *operand_width;
            operands.push_back(*operand);
        }
        const auto domain = hir_expression_domain(
            expression_id, hir_process_scope_);
        if (!domain || width == 0U
            || width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        auto packed = allocate_register(width, *domain);
        process_.operations.emplace_back(Concatenate {
            packed,
            std::move(operands),
            static_cast<std::uint32_t>(width),
        });
        if (source.text == "@stream-left"
            && static_cast<std::size_t>(*slice_size) < width) {
            std::vector<RegisterId> slices;
            for (std::size_t offset { }; offset < width;) {
                const auto chunk = std::min(
                    static_cast<std::size_t>(*slice_size),
                    width - offset);
                const auto slice = allocate_register(chunk, *domain);
                process_.operations.emplace_back(Extract {
                    slice,
                    packed,
                    static_cast<std::uint32_t>(offset),
                    static_cast<std::uint32_t>(chunk),
                });
                slices.push_back(slice);
                offset += chunk;
            }
            const auto reordered = allocate_register(width, *domain);
            process_.operations.emplace_back(Concatenate {
                reordered,
                std::move(slices),
                static_cast<std::uint32_t>(width),
            });
            packed = reordered;
        }
        return packed;
    } else if (source.binary && source.operands.size() == 2U
        && expression->systemverilog != nullptr) {
        const bool supported_container_equality
            = source.text == "==" || source.text == "!="
            || source.text == "===" || source.text == "!==";
        const auto type_call = [&](const semantic::ExpressionId id) {
            const auto operand = specialized_hir_unit_->find_expression(id);
            return operand && operand->systemverilog != nullptr
                && operand->systemverilog->kind
                == semantic::sv::ExpressionKind::call
                && operand->systemverilog->text == "@sv-type";
        };
        const auto lhs_type_call = type_call(source.operands[0]);
        const auto rhs_type_call = type_call(source.operands[1]);
        if (supported_container_equality
            && (lhs_type_call || rhs_type_call)) {
            const auto lhs = specialized_hir_unit_->find_expression(
                source.operands[0]);
            const auto rhs = specialized_hir_unit_->find_expression(
                source.operands[1]);
            if (!lhs_type_call || !rhs_type_call
                || lhs->systemverilog->operands.size() != 1U
                || rhs->systemverilog->operands.size() != 1U) {
                report(
                    "FSIM-ELAB-SVTYPE-006",
                    "type equality requires type(expression) on both sides",
                    hir_source_span(source.source));
                return std::nullopt;
            }

            struct TypeProfile {
                std::optional<semantic::TypeId> nominal_type;
                std::optional<semantic::sv::TypeForm> container_form;
                std::vector<std::pair<std::int64_t, std::int64_t>>
                    dimensions;
                std::size_t width { };
                bool four_state { };
                bool signed_value { };

                bool operator==(const TypeProfile&) const = default;
            };
            const auto declaration_type = [&](
                                                  const semantic::DeclarationId
                                                      declaration_id)
                -> std::optional<semantic::sv::TypeReference> {
                const auto declaration
                    = specialized_hir_unit_->find_declaration(
                        declaration_id);
                if (!declaration || declaration->systemverilog == nullptr) {
                    return std::nullopt;
                }
                const auto& selected = *declaration->systemverilog;
                if (selected.type) {
                    return selected.type;
                }
                if (selected.callable && selected.callable->function) {
                    return selected.callable->return_type;
                }
                if (selected.declared_type) {
                    semantic::sv::TypeReference type_reference;
                    type_reference.target.target = *selected.declared_type;
                    type_reference.target.spelling = selected.name;
                    return type_reference;
                }
                return std::nullopt;
            };
            const auto expression_type = [&](const semantic::ExpressionId id)
                -> std::optional<semantic::sv::TypeReference> {
                if (const auto declaration_id
                    = hir_referenced_declaration(id)) {
                    if (const auto type
                        = declaration_type(*declaration_id)) {
                        return type;
                    }
                }
                const auto operand
                    = specialized_hir_unit_->find_expression(id);
                if (!operand || operand->systemverilog == nullptr
                    || operand->systemverilog->nominal_type.empty()) {
                    return std::nullopt;
                }
                if (const auto declaration_id
                    = hir_systemverilog_named_type_declaration(
                        operand->systemverilog->nominal_type,
                        operand->systemverilog->scope)) {
                    if (const auto type
                        = declaration_type(*declaration_id)) {
                        return type;
                    }
                }
                semantic::sv::TypeReference type_reference;
                type_reference.target.spelling
                    = operand->systemverilog->nominal_type;
                type_reference.signed_value
                    = operand->systemverilog->signed_value;
                return type_reference;
            };
            const auto profile = [&](const semantic::ExpressionId id)
                -> std::optional<TypeProfile> {
                const auto operand
                    = specialized_hir_unit_->find_expression(id);
                auto type = expression_type(id);
                if (!operand || operand->systemverilog == nullptr) {
                    return std::nullopt;
                }
                if (type) {
                    const semantic::CompiledDesignResolver resolver {
                        *specialized_hir_unit_, hir_generic_binding_frames_
                    };
                    type = resolver.effective_systemverilog_type(
                               *type, operand->systemverilog->scope)
                        .value_or(*type);
                }
                TypeProfile candidate_profile;
                candidate_profile.width = type
                    ? hir_systemverilog_type_width(*type).value_or(0U)
                    : 0U;
                if (candidate_profile.width == 0U) {
                    candidate_profile.width = hir_expression_width(
                        id, operand->systemverilog->scope)
                                       .value_or(0U);
                }
                if (candidate_profile.width == 0U) {
                    return std::nullopt;
                }
                const auto domain = hir_expression_domain(
                    id, operand->systemverilog->scope);
                candidate_profile.four_state = type
                    ? hir_systemverilog_type_four_state(*type)
                    : domain && *domain != frontend::ValueDomain::Bit2;
                candidate_profile.signed_value = type
                    ? type->signed_value : hir_expression_signed(id);
                candidate_profile.container_form = type
                    ? type->container_form : std::nullopt;
                if (type) {
                    for (const auto& dimension :
                        type->unpacked_dimensions) {
                        const auto left = dimension.left
                            ? dimension.left
                            : dimension.left_expression
                            ? specialized_hir_unit_
                                  ->evaluate_integral_expression(
                                      *dimension.left_expression)
                            : std::nullopt;
                        const auto right = dimension.right
                            ? dimension.right
                            : dimension.right_expression
                            ? specialized_hir_unit_
                                  ->evaluate_integral_expression(
                                      *dimension.right_expression)
                            : std::nullopt;
                        if (!left || !right) {
                            return std::nullopt;
                        }
                        candidate_profile.dimensions.emplace_back(
                            *left, *right);
                    }
                }
                if (!type) {
                    return candidate_profile;
                }
                std::unordered_set<std::uint32_t> visiting;
                const auto nominal = [&](const auto& self,
                                         const semantic::sv::TypeReference&
                                             reference)
                    -> std::optional<semantic::TypeId> {
                    if (!reference.target.target.valid()) {
                        const auto declaration_id
                            = hir_systemverilog_named_type_declaration(
                                reference.target.spelling,
                                operand->systemverilog->scope);
                        const auto declaration = declaration_id
                            ? specialized_hir_unit_->find_declaration(
                                  *declaration_id)
                            : std::nullopt;
                        if (!declaration
                            || declaration->systemverilog == nullptr) {
                            return std::nullopt;
                        }
                        const auto& declaration_source
                            = *declaration->systemverilog;
                        if (declaration_source.type) {
                            return self(self, *declaration_source.type);
                        }
                        if (declaration_source.default_type) {
                            return self(
                                self, *declaration_source.default_type);
                        }
                        return std::nullopt;
                    }
                    const auto type_id = reference.target.target;
                    if (!visiting.insert(type_id.value()).second) {
                        return std::nullopt;
                    }
                    if (const auto binding
                        = hir_systemverilog_type_parameter_binding(type_id)) {
                        const auto resolved = self(self, *binding);
                        visiting.erase(type_id.value());
                        return resolved;
                    }
                    const auto definition
                        = specialized_hir_unit_->find_type(type_id);
                    if (!definition
                        || definition->systemverilog == nullptr) {
                        visiting.erase(type_id.value());
                        return std::nullopt;
                    }
                    const auto& declared = *definition->systemverilog;
                    using Form = semantic::sv::TypeForm;
                    if (declared.form == Form::enumeration
                        || declared.form == Form::packed_structure
                        || declared.form == Form::packed_union
                        || declared.form == Form::unpacked_structure
                        || declared.form == Form::tagged_union
                        || declared.form == Form::unpacked_union) {
                        visiting.erase(type_id.value());
                        return type_id;
                    }
                    const auto resolved = self(self, declared.base);
                    visiting.erase(type_id.value());
                    return resolved;
                };
                candidate_profile.nominal_type = nominal(nominal, *type);
                return candidate_profile;
            };

            const auto lhs_profile = profile(
                lhs->systemverilog->operands.front());
            const auto rhs_profile = profile(
                rhs->systemverilog->operands.front());
            if (!lhs_profile || !rhs_profile) {
                report(
                    "FSIM-ELAB-SVTYPE-006",
                    "type equality requires statically typed operands",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            auto equal = *lhs_profile == *rhs_profile;
            if (source.text == "!=" || source.text == "!==") {
                equal = !equal;
            }
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                destination, unsigned_value(equal ? 1U : 0U, 1U) });
            return destination;
        }
        const auto lhs_type = hir_static_container_expression_type(
            source.operands[0]);
        const auto rhs_type = hir_static_container_expression_type(
            source.operands[1]);
        if (lhs_type || rhs_type) {
            if (!supported_container_equality) {
                report(
                    "FSIM-ELAB-SVEQUAL-001",
                    "bounded whole-container comparison supports only "
                    "SystemVerilog ==, !=, ===, and !==",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (!lhs_type || !rhs_type) {
                report(
                    "FSIM-ELAB-SVEQUAL-002",
                    "whole-container equality requires two typed container "
                    "operands",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (*lhs_type != *rhs_type) {
                report(
                    "FSIM-ELAB-SVEQUAL-003",
                    "whole-container equality requires an exactly compatible "
                    "kind and profile",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto lhs = lower_hir_static_container_value(
                source.operands[0]);
            const auto rhs = lower_hir_static_container_value(
                source.operands[1]);
            if (!lhs || !rhs || lhs->type != rhs->type) {
                return std::nullopt;
            }
            const bool case_equal = source.text == "==="
                || source.text == "!==";
            const auto compared = allocate_register(
                1U,
                case_equal ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(CompareContainers {
                compared, lhs->value, rhs->value, case_equal });
            if (source.text == "==" || source.text == "===") {
                return compared;
            }
            const auto inverted = allocate_register(
                1U,
                case_equal ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(UnaryNot {
                inverted, compared });
            return inverted;
        }
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == ".len"
        && expression->systemverilog->operands.size() == 1U
        && hir_expression_is_string(
            expression->systemverilog->operands.front(),
            hir_process_scope_)) {
        const auto source_string = lower_hir_string_expression(
            expression->systemverilog->operands.front());
        if (!source_string) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(StringLength {
            destination, *source_string });
        result = expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == ".getc"
            || expression->systemverilog->text == ".compare"
            || expression->systemverilog->text == ".icompare"
            || expression->systemverilog->text == ".atoi"
            || expression->systemverilog->text == ".atohex"
            || expression->systemverilog->text == ".atooct"
            || expression->systemverilog->text == ".atobin"
            || expression->systemverilog->text == ".atoreal")
        && !expression->systemverilog->operands.empty()
        && hir_expression_is_string(
            expression->systemverilog->operands.front(),
            hir_process_scope_)) {
        const auto& call = *expression->systemverilog;
        const bool conversion = call.text == ".atoi"
            || call.text == ".atohex" || call.text == ".atooct"
            || call.text == ".atobin" || call.text == ".atoreal";
        const auto expected_arguments = conversion ? 1U : 2U;
        if (call.operands.size() != expected_arguments) {
            report(
                "FSIM-ELAB-SVSTRING-018",
                "runtime string method '" + call.text
                    + (conversion ? "' takes no arguments"
                                  : "' requires one argument"),
                hir_source_span(call.source));
            return std::nullopt;
        }
        const auto source_string = lower_hir_string_expression(
            call.operands.front());
        if (!source_string) {
            return std::nullopt;
        }
        StringMethod method;
        method.source = *source_string;
        const auto method_width = call.text == ".atoreal" ? 64U : 32U;
        method.destination = allocate_register(
            method_width,
            call.text == ".getc" ? frontend::ValueDomain::Bit2
                                  : frontend::ValueDomain::Integer);
        if (conversion) {
            method.operation = call.text == ".atoi"
                ? StringMethodOperator::atoi
                : call.text == ".atohex"
                ? StringMethodOperator::atohex
                : call.text == ".atooct"
                ? StringMethodOperator::atooct
                : call.text == ".atobin"
                ? StringMethodOperator::atobin
                : StringMethodOperator::atoreal;
        } else if (call.text == ".getc") {
            auto index = lower_hir_expression(call.operands[1], 32U);
            if (!index) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "getc index must be a signed 32-bit integral value",
                    hir_source_span(call.source));
                return std::nullopt;
            }
            if (register_width(*index) != 32U) {
                index = resize_register(
                    *index, 32U,
                    hir_expression_signed(call.operands[1]));
            }
            method.operation = StringMethodOperator::getc;
            method.first = *index;
        } else {
            if (!hir_expression_is_string(
                    call.operands[1], hir_process_scope_)) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "compare argument must be a runtime string value",
                    hir_source_span(call.source));
                return std::nullopt;
            }
            const auto argument = lower_hir_string_expression(
                call.operands[1]);
            if (!argument) {
                return std::nullopt;
            }
            method.operation = call.text == ".compare"
                ? StringMethodOperator::compare
                : StringMethodOperator::icompare;
            method.argument = *argument;
        }
        process_.operations.emplace_back(method);
        result = expected_width != 0U
                && expected_width != method_width
            ? std::optional { resize_register(
                  method.destination, expected_width, false) }
            : std::optional { method.destination };
    } else if (const auto aggregate
        = lower_hir_packed_container_aggregate(
            expression_id, expected_width)) {
        result = aggregate;
    } else if (const auto aggregate_member
        = hir_container_aggregate_selection(expression_id)) {
        const auto index = lower_hir_container_element_index(
            aggregate_member->element);
        if (!index) {
            return std::nullopt;
        }
        const auto container = aggregate_member->element.local
            ? *aggregate_member->element.local
            : allocate_container_register(
                  *aggregate_member->element.type);
        if (!aggregate_member->element.local) {
            process_.operations.emplace_back(ReadContainerObject {
                container, aggregate_member->element.object });
        }
        const auto domain = aggregate_member->leaf.two_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        const auto destination = allocate_register(
            aggregate_member->leaf.element_width, domain);
        process_.operations.emplace_back(ContainerAggregateRead {
            destination,
            container,
            *index,
            aggregate_member->members,
            aggregate_member->element.indices.size() > 1U
                || aggregate_member->element.type->signed_indices,
            aggregate_member->element.indices.size() > 1U,
        });
        if (!aggregate_member->element.local) {
            const auto alias = std::ranges::find_if(
                design_.container_signal_aliases_.rbegin(),
                design_.container_signal_aliases_.rend(),
                [&](const ContainerSignalAlias& candidate) {
                    return candidate.object
                            == aggregate_member->element.object
                        && candidate.readable;
                });
            if (alias != design_.container_signal_aliases_.rend()) {
                implicit_signal_dependencies_.push_back(alias->signal);
            }
        }
        result = expected_width != 0U
                && expected_width
                    != aggregate_member->leaf.element_width
            ? std::optional { resize_register(
                  destination, expected_width,
                  aggregate_member->leaf.signed_elements) }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::index
        && expression->systemverilog->operands.size() == 2U
        && hir_expression_is_string(
            expression->systemverilog->operands.front(),
            hir_process_scope_)) {
        const auto& indexed = *expression->systemverilog;
        const auto source_string = lower_hir_string_expression(
            indexed.operands.front());
        const auto index_width = hir_expression_width(
            indexed.operands.back(), hir_process_scope_)
                                     .value_or(32U);
        const auto index = lower_hir_expression(
            indexed.operands.back(), index_width);
        if (!source_string || !index) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(StringIndex {
            destination,
            *source_string,
            *index,
            hir_expression_signed(indexed.operands.back()),
        });
        result = expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    } else if (const auto file_enumeration
        = hir_vhdl_standard_enumeration_literal(expression_id)) {
        const auto width = expected_width == 0U ? 2U : expected_width;
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(*file_enumeration, width) });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::index
        && expression->systemverilog->operands.size() == 2U
        && hir_static_container_expression_type(
            expression->systemverilog->operands.front())
        && !hir_container_object_binding(
            expression->systemverilog->operands.front())) {
        const auto& indexed_expression = *expression->systemverilog;
        const auto container = lower_hir_static_container_value(
            indexed_expression.operands.front());
        if (!container) {
            return std::nullopt;
        }
        HirContainerElementBinding element;
        element.type = &container->type;
        element.indices.push_back(indexed_expression.operands.back());
        element.width = container->type.element_width;
        element.domain = container->type.two_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        element.signed_value = container->type.signed_elements;
        const auto index = lower_hir_container_element_index(element);
        if (!index || element.width == 0U) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            element.width, element.domain);
        process_.operations.emplace_back(ContainerRead {
            destination,
            container->value,
            *index,
            container->type.signed_indices,
            false,
            false,
        });
        result = expected_width != 0U
                && expected_width != element.width
            ? std::optional { resize_register(
                  destination, expected_width, element.signed_value) }
            : std::optional { destination };
    } else if (const auto element
        = hir_container_element_binding(expression_id)) {
        const auto index = lower_hir_container_element_index(*element);
        if (!index) {
            return std::nullopt;
        }
        const auto container = element->local
            ? *element->local
            : allocate_container_register(*element->type);
        if (!element->local) {
            process_.operations.emplace_back(ReadContainerObject {
                container, element->object });
        }
        const auto destination = allocate_register(
            element->width, element->domain);
        process_.operations.emplace_back(ContainerRead {
            destination,
            container,
            *index,
            element->indices.size() > 1U
                || element->type->signed_indices,
            element->indices.size() > 1U,
            element->type->string_indices,
        });
        if (!element->local) {
            const auto alias = std::ranges::find_if(
                design_.container_signal_aliases_.rbegin(),
                design_.container_signal_aliases_.rend(),
                [&](const ContainerSignalAlias& candidate) {
                    return candidate.object == element->object
                        && candidate.readable;
                });
            if (alias != design_.container_signal_aliases_.rend()) {
                implicit_signal_dependencies_.push_back(alias->signal);
            }
        }
        result = expected_width != 0U
                && expected_width != element->width
            ? std::optional { resize_register(
                  destination, expected_width, element->signed_value) }
            : std::optional { destination };
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::string_literal
        && expression->vhdl->decoded_string
        && !expression->vhdl->decoded_string->empty()) {
        auto decoded = *expression->vhdl->decoded_string;
        std::ranges::transform(
            decoded,
            decoded.begin(),
            [](const char value) {
                return static_cast<char>(
                    std::toupper(static_cast<unsigned char>(value)));
            });
        if (!std::ranges::all_of(
                decoded,
                [](const char value) {
                    return value == 'U' || value == 'X'
                        || value == '0' || value == '1'
                        || value == 'Z' || value == 'W'
                        || value == 'L' || value == 'H'
                        || value == '-';
                })) {
            return std::nullopt;
        }
        const auto two_state = std::ranges::all_of(
            decoded,
            [](const char value) {
                return value == '0' || value == '1';
            });
        const auto destination = allocate_register(
            decoded.size(),
            two_state ? frontend::ValueDomain::Bit2
                      : frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            destination,
            two_state
                ? PackedLogic4::from_msb_string(decoded)
                : PackedLogic4::from_logic9_msb_string(decoded),
        });
        result = destination;
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && (hir_vhdl_signal_attribute_profile(
                expression_id, hir_process_scope_)
            || vhdl_signal_attribute_diagnostic_code(
                expression->vhdl->text))) {
        result = lower_hir_vhdl_signal_attribute(
            expression_id, expected_width);
    } else if (is_hir_vhdl_vital_mux2(expression_id)) {
        result = lower_hir_vhdl_vital_mux2(
            expression_id, expected_width);
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "'pos"
        && expression->vhdl->operands.size() == 2U
        && is_hir_vhdl_reflection_expression(
            expression->vhdl->operands.back())) {
        result = lower_hir_expression(
            expression->vhdl->operands.back(), expected_width);
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text.starts_with("'")) {
        result = lower_hir_vhdl_attribute(expression_id, expected_width);
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::class_null) {
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, PackedLogic4::from_aval_bval(64U, 0U, 0U) });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::class_allocation) {
        const auto& allocation = *expression->systemverilog;
        if (allocation.class_identity.empty()) {
            return std::nullopt;
        }
        std::vector<RegisterId> actuals;
        std::vector<std::uint8_t> actual_kinds;
        actuals.reserve(allocation.operands.size());
        actual_kinds.reserve(allocation.operands.size());
        for (const auto operand : allocation.operands) {
            if (hir_expression_is_string(operand, hir_process_scope_)) {
                const auto actual = lower_hir_string_expression(operand);
                if (!actual) {
                    return std::nullopt;
                }
                actuals.push_back(*actual);
                actual_kinds.push_back(1U);
                continue;
            }
            const auto width = hir_expression_width(
                operand, hir_process_scope_);
            if (!width || *width == 0U) {
                return std::nullopt;
            }
            const auto actual = lower_hir_expression(operand, *width);
            if (!actual) {
                return std::nullopt;
            }
            actuals.push_back(*actual);
            actual_kinds.push_back(0U);
        }
        auto names = allocation.argument_names;
        if (names.empty()) {
            names.resize(actuals.size());
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassAllocate {
            destination,
            allocation.class_identity,
            allocation.class_identity,
            std::move(actuals),
            std::move(actual_kinds),
            std::move(names),
        });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::class_cast) {
        const auto& cast = *expression->systemverilog;
        if (cast.operands.size() != 2U
            || cast.class_member_identity.empty()) {
            return std::nullopt;
        }
        const auto prior = lower_hir_expression(
            cast.operands.front(), 64U);
        const auto value = lower_hir_expression(
            cast.operands.back(), 64U);
        if (!prior || !value || register_width(*prior) != 64U
            || register_width(*value) != 64U) {
            return std::nullopt;
        }
        const auto success = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ClassMethodCall {
            success,
            *value,
            "@checked-cast:" + cast.class_member_identity,
            { },
            { },
            { },
            1U,
            false,
        });
        const auto selected = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ConditionalSelect {
            selected, success, *value, *prior });
        if (!lower_hir_packed_copy_out(
                cast.operands.front(), selected)) {
            return std::nullopt;
        }
        result = success;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::update) {
        const auto& update = *expression->systemverilog;
        if (update.operands.size() != 1U
            || (update.text != "pre++" && update.text != "pre--"
                && update.text != "post++" && update.text != "post--")) {
            return std::nullopt;
        }
        const auto target = capture_hir_packed_update_target(
            update.operands.front());
        if (!target) {
            return std::nullopt;
        }
        const auto updated = lower_hir_packed_update_value(
            *target,
            update.text.ends_with("++") ? "+" : "-",
            std::nullopt);
        if (!updated
            || !write_hir_packed_update_target(*target, *updated)) {
            return std::nullopt;
        }
        result = update.text.starts_with("post")
            ? target->captured
            : *updated;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == ".size"
            || expression->systemverilog->text == ".sum"
            || expression->systemverilog->text == ".product"
            || expression->systemverilog->text == ".and"
            || expression->systemverilog->text == ".or"
            || expression->systemverilog->text == ".xor"
            || expression->systemverilog->text == ".exists"
            || expression->systemverilog->text == ".first"
            || expression->systemverilog->text == ".last"
            || expression->systemverilog->text == ".next"
            || expression->systemverilog->text == ".prev"
            || expression->systemverilog->text == ".pop_front"
            || expression->systemverilog->text == ".pop_back"
            || expression->systemverilog->text == ".reverse"
            || expression->systemverilog->text == ".sort"
            || expression->systemverilog->text == ".rsort"
            || expression->systemverilog->text == ".shuffle"
            || expression->systemverilog->text == ".min"
            || expression->systemverilog->text == ".max"
            || expression->systemverilog->text == ".unique"
            || expression->systemverilog->text == ".unique_index"
            || expression->systemverilog->text == ".find"
            || expression->systemverilog->text == ".find_index"
            || expression->systemverilog->text == ".find_first"
            || expression->systemverilog->text == ".find_first_index"
            || expression->systemverilog->text == ".find_last"
            || expression->systemverilog->text == ".find_last_index")) {
        result = lower_hir_systemverilog_container_expression(
            expression_id, expected_width);
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text.starts_with(
            "@sv-container-index:")) {
        const auto& access = *expression->systemverilog;
        if (access.operands.size() != 2U) {
            return std::nullopt;
        }
        const auto receiver = lower_hir_expression(
            access.operands[0], 64U);
        const auto index_width = hir_expression_width(
            access.operands[1], hir_process_scope_)
                                     .value_or(32U);
        const auto index = lower_hir_expression(
            access.operands[1], index_width);
        if (!receiver || register_width(*receiver) != 64U || !index) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            "@container-read:"
                + access.text.substr(
                    std::string_view { "@sv-container-index:" }.size()),
            { *index },
            { "" },
            { static_cast<std::uint8_t>(
                frontend::PortDirection::Input) },
            64U,
            false,
        });
        result = expected_width != 0U && expected_width != 64U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text.starts_with(
            "@sv-container-method:")) {
        const auto& call = *expression->systemverilog;
        constexpr auto prefix
            = std::string_view { "@sv-container-method:" };
        const auto operation = std::string_view { call.text }.substr(
            prefix.size());
        const auto pop = operation.starts_with("pop_front:");
        const auto query_size = operation.starts_with("size:");
        if ((!pop && !query_size) || call.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto receiver = lower_hir_expression(
            call.operands.front(), 64U);
        if (!receiver || register_width(*receiver) != 64U) {
            return std::nullopt;
        }
        const auto result_width = pop ? 64U : 32U;
        const auto destination = allocate_register(
            result_width,
            pop ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Integer);
        const auto property = operation.substr(
            pop ? std::string_view { "pop_front:" }.size()
                : std::string_view { "size:" }.size());
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            (pop ? "@container-pop-front:" : "@container-size:")
                + std::string { property },
            { },
            { },
            { },
            static_cast<std::uint32_t>(result_width),
            false,
        });
        result = expected_width != 0U
                && expected_width != result_width
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == "$left"
            || expression->systemverilog->text == "$right"
            || expression->systemverilog->text == "$low"
            || expression->systemverilog->text == "$high"
            || expression->systemverilog->text == "$increment"
            || expression->systemverilog->text == "$size"
            || expression->systemverilog->text == "$bits"
            || expression->systemverilog->text == "$dimensions"
            || expression->systemverilog->text
                == "$unpacked_dimensions")) {
        const auto& call = *expression->systemverilog;
        emit_debug_point(
            DebugPointKind::call, hir_source_span(call.source));
        const auto value = hir_systemverilog_container_query(expression_id);
        if (!value) {
            const auto receiver_type = !call.operands.empty()
                ? hir_static_container_expression_type(
                      call.operands.front())
                : std::nullopt;
            const auto packed_bits = call.text == "$bits"
                    && call.operands.size() == 1U && !receiver_type
                ? hir_expression_width(
                      call.operands.front(), hir_process_scope_)
                : std::nullopt;
            if (packed_bits && *packed_bits != 0U) {
                const auto destination = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(
                        static_cast<std::uint64_t>(*packed_bits), 32U),
                });
                result = expected_width != 0U && expected_width != 32U
                    ? std::optional {
                          resize_register(destination, expected_width, false)
                      }
                    : std::optional { destination };
                return result;
            }
            if (call.operands.size() == 2U) {
                const auto requested
                    = hir_constant_integer(call.operands.back());
                const auto rank = receiver_type
                    && receiver_type->fixed
                    && !receiver_type->dimensions.empty()
                    ? receiver_type->dimensions.size()
                    : 1U;
                if (!requested || *requested < 1
                    || static_cast<std::uint64_t>(*requested) > rank) {
                    report(
                        receiver_type
                            ? "FSIM-ELAB-SVQUERY-002"
                            : "FSIM-ELAB-086",
                        call.text
                            + (receiver_type
                                ? " requires a constant unpacked dimension "
                                  "inside the declared rank"
                                : " packed-array dimension must be the "
                                  "constant value one"),
                        hir_source_span(call.source));
                    return invalid_integral_result();
                }
            }
            const bool runtime_dimension
                = call.operands.size() == 1U
                || (call.operands.size() == 2U
                    && (call.text == "$left" || call.text == "$right"
                        || call.text == "$low" || call.text == "$high"
                        || call.text == "$increment"
                        || call.text == "$size")
                    && hir_constant_integer(call.operands.back())
                        == std::optional<std::int64_t> { 1 });
            if (!runtime_dimension) {
                report(
                    "FSIM-ELAB-SVQUERY-001",
                    call.text
                        + " requires a direct one-dimensional "
                          "SystemVerilog container object",
                    hir_source_span(call.source));
                return invalid_integral_result();
            }
            if (receiver_type && !receiver_type->fixed
                && !receiver_type->associative
                && (call.text == "$left" || call.text == "$low"
                    || call.text == "$increment")) {
                const auto destination = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(
                        call.text == "$increment"
                            ? std::numeric_limits<std::uint32_t>::max()
                            : 0U,
                        32U),
                });
                result = expected_width != 0U && expected_width != 32U
                    ? std::optional {
                          resize_register(
                              destination, expected_width, false)
                      }
                    : std::optional { destination };
                return result;
            }
            const auto container = lower_hir_static_container_value(
                call.operands.front());
            if (!container || container->type.fixed) {
                report(
                    "FSIM-ELAB-SVQUERY-001",
                    call.text
                        + " requires a typed container object, slice, or "
                          "function result",
                    hir_source_span(call.source));
                return invalid_integral_result();
            }
            if (container->type.associative
                && call.text != "$size" && call.text != "$bits") {
                report(
                    "FSIM-ELAB-SVQUERY-003",
                    call.text
                        + " has no finite bound for an associative array",
                    hir_source_span(call.source));
                return invalid_integral_result();
            }
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            if (call.text == "$left" || call.text == "$low") {
                process_.operations.emplace_back(LoadConstant {
                    destination, unsigned_value(0U, 32U) });
            } else if (call.text == "$increment") {
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(
                        std::numeric_limits<std::uint32_t>::max(), 32U),
                });
            } else {
                const auto size = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ContainerSize {
                    size, container->value });
                if (call.text == "$size") {
                    process_.operations.emplace_back(CopyRegister {
                        destination, size });
                } else if (call.text == "$right"
                    || call.text == "$high") {
                    const auto one = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        one, unsigned_value(1U, 32U) });
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::subtract_unsigned,
                        destination,
                        size,
                        one,
                    });
                } else if (call.text == "$bits") {
                    const auto element_bits
                        = hir_systemverilog_container_bit_width(
                            container->type);
                    if (!element_bits
                        || *element_bits
                            > std::numeric_limits<std::uint32_t>::max()) {
                        return std::nullopt;
                    }
                    const auto element_width = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        element_width,
                        unsigned_value(*element_bits, 32U),
                    });
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::multiply_unsigned,
                        destination,
                        size,
                        element_width,
                    });
                } else {
                    return std::nullopt;
                }
            }
            result = expected_width != 0U && expected_width != 32U
                ? std::optional {
                      resize_register(destination, expected_width, false)
                  }
                : std::optional { destination };
            return result;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(static_cast<std::uint32_t>(*value), 32U),
        });
        result = expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, false)
              }
            : std::optional { destination };
    } else if (const auto property
        = hir_class_property_profile(expression_id)) {
        const auto destination = allocate_register(
            property->width, property->domain);
        if (property->static_storage) {
            process_.operations.emplace_back(ClassStaticPropertyRead {
                destination,
                property->identity,
                static_cast<std::uint32_t>(property->width),
            });
        } else {
            const auto receiver = lower_hir_expression(
                *property->receiver, 64U);
            if (!receiver || register_width(*receiver) != 64U) {
                return std::nullopt;
            }
            process_.operations.emplace_back(ClassPropertyRead {
                destination,
                *receiver,
                property->identity,
                static_cast<std::uint32_t>(property->width),
            });
        }
        result = expected_width != 0U
                && expected_width != property->width
            ? std::optional {
                  resize_register(destination,
                      expected_width, property->signed_value)
              }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::name
        && expression->systemverilog->text.ends_with(".triggered")) {
        const auto event = hir_systemverilog_event_signal(
            expression_id, hir_process_scope_);
        if (!event) {
            report(
                "FSIM-ELAB-SVEVENT-008",
                "triggered property receiver is not a named event",
                hir_source_span(expression->systemverilog->source));
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(EventTriggered {
            destination, *event });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && (expression->systemverilog->kind
                == semantic::sv::ExpressionKind::class_method_call
            || expression->systemverilog->kind
                == semantic::sv::ExpressionKind::class_static_method_call)) {
        result = lower_hir_class_method_call(
            expression_id, expected_width);
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::name
        && expression->systemverilog->text == "this"
        && hir_class_receiver_register_) {
        result = *hir_class_receiver_register_;
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::binary
        && expression->vhdl->operands.size() == 2U
        && (expression->vhdl->text == "="
            || expression->vhdl->text == "/=")
        && hir_expression_is_string(
            expression->vhdl->operands[0], hir_process_scope_)
        && hir_expression_is_string(
            expression->vhdl->operands[1], hir_process_scope_)) {
        const auto lhs = lower_hir_string_expression(
            expression->vhdl->operands[0]);
        const auto rhs = lower_hir_string_expression(
            expression->vhdl->operands[1]);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(CompareStrings {
            destination,
            *lhs,
            *rhs,
            expression->vhdl->text == "/=",
        });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::binary
        && expression->systemverilog->operands.size() == 2U
        && (expression->systemverilog->text == "=="
            || expression->systemverilog->text == "!=")
        && hir_expression_is_string(
            expression->systemverilog->operands[0], hir_process_scope_)
        && hir_expression_is_string(
            expression->systemverilog->operands[1], hir_process_scope_)) {
        const auto lhs = lower_hir_string_expression(
            expression->systemverilog->operands[0]);
        const auto rhs = lower_hir_string_expression(
            expression->systemverilog->operands[1]);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(CompareStrings {
            destination,
            *lhs,
            *rhs,
            expression->systemverilog->text == "!=",
        });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == "$onehot"
            || expression->systemverilog->text == "$onehot0"
            || expression->systemverilog->text == "$countones"
            || expression->systemverilog->text == "$countbits")) {
        const auto& call = *expression->systemverilog;
        const bool count_bits = call.text == "$countbits";
        const bool one_hot = call.text == "$onehot"
            || call.text == "$onehot0";
        if (call.operands.empty()
            || (!count_bits && call.operands.size() != 1U)
            || (count_bits && call.operands.size() < 2U)) {
            return std::nullopt;
        }
        const auto operand = call.operands.front();
        const auto width = hir_expression_width(
            operand, hir_process_scope_);
        auto source_value = width && *width != 0U
            ? lower_hir_expression(operand, *width)
            : std::nullopt;
        if (!source_value) {
            return std::nullopt;
        }
        emit_debug_point(
            DebugPointKind::call, hir_source_span(call.source));
        if (one_hot) {
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Reduction {
                call.text == "$onehot"
                    ? ReductionOperator::one_hot
                    : ReductionOperator::one_hot_or_zero,
                destination,
                *source_value,
            });
            result = expected_width != 0U && expected_width != 1U
                ? std::optional {
                      resize_register(destination, expected_width, false)
                  }
                : std::optional { destination };
        } else if (call.text == "$countones") {
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(CountOnes {
                destination, *source_value });
            result = expected_width != 0U && expected_width != 32U
                ? std::optional {
                      resize_register(destination, expected_width, true)
                  }
                : std::optional { destination };
        } else {
            std::uint8_t state_mask { };
            for (const auto control :
                 std::span { call.operands }.subspan(1U)) {
                std::string error;
                const auto constant = evaluate_hir_systemverilog_constant(
                    *specialized_hir_unit_, control, error);
                const auto spelling = constant
                    ? constant->packed.to_msb_string()
                    : std::string { };
                if (!constant || constant->width != 1U
                    || spelling.size() != 1U) {
                    report(
                        "FSIM-ELAB-089",
                        "$countbits control arguments must be constant "
                        "single-bit values",
                        hir_source_span(call.source));
                    return invalid_integral_result();
                }
                switch (spelling.front()) {
                case '0':
                    state_mask |= 0x01U;
                    break;
                case '1':
                    state_mask |= 0x02U;
                    break;
                case 'x':
                case 'X':
                    state_mask |= 0x04U;
                    break;
                case 'z':
                case 'Z':
                    state_mask |= 0x08U;
                    break;
                default:
                    report(
                        "FSIM-ELAB-089",
                        "$countbits control arguments must select 0, 1, X, "
                        "or Z",
                        hir_source_span(call.source));
                    return invalid_integral_result();
                }
            }
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(CountBits {
                destination, *source_value, state_mask });
            result = expected_width != 0U && expected_width != 32U
                ? std::optional {
                      resize_register(destination, expected_width, true)
                  }
                : std::optional { destination };
        }
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == "$signed"
            || expression->systemverilog->text == "$unsigned")
        && expression->systemverilog->operands.size() == 1U) {
        emit_debug_point(DebugPointKind::call,
            hir_source_span(expression->systemverilog->source));
        const auto operand = expression->systemverilog->operands.front();
        const auto operand_width = hir_expression_width(
            operand, hir_process_scope_)
                                       .value_or(1U);
        result = lower_hir_expression(operand, operand_width);
        if (result && expected_width != 0U
            && register_width(*result) != expected_width) {
            result = resize_register(
                *result,
                expected_width,
                expression->systemverilog->text == "$signed");
        }
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == "$system") {
        const auto& call = *expression->systemverilog;
        const auto named = std::ranges::any_of(
            call.argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ != frontend::Language::SystemVerilog2017
            || named || call.operands.size() > 1U
            || (!call.operands.empty()
                && !hir_expression_is_string(
                    call.operands.front(), hir_process_scope_))) {
            report(
                "FSIM-ELAB-SVSYS-001",
                "$system requires SystemVerilog and zero or one command "
                "string",
                hir_source_span(call.source));
            return std::nullopt;
        }
        emit_debug_point(
            DebugPointKind::call, hir_source_span(call.source));
        const auto command = call.operands.empty()
            ? std::optional<StringRegisterId> { }
            : lower_hir_string_expression(call.operands.front());
        if (!call.operands.empty() && !command) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(SystemCommand {
            command, destination });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == "$q_full") {
        const auto& call = *expression->systemverilog;
        const bool named = std::ranges::any_of(
            call.argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ != frontend::Language::SystemVerilog2017
            || named || call.operands.size() != 2U) {
            report(
                "FSIM-ELAB-SVQUEUE-001",
                "$q_full requires a 32-bit integer queue ID and writable "
                "integer status",
                hir_source_span(call.source));
            return std::nullopt;
        }
        auto queue_id = lower_hir_expression(call.operands[0], 32U);
        if (!queue_id) {
            return std::nullopt;
        }
        if (register_width(*queue_id) != 32U) {
            queue_id = resize_register(
                *queue_id,
                32U,
                hir_expression_signed(call.operands[0]));
        }
        const auto status = allocate_register(
            32U, frontend::ValueDomain::Integer);
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::full;
        operation.queue_id = *queue_id;
        operation.status = status;
        operation.result = destination;
        process_.operations.emplace_back(std::move(operation));
        if (!lower_hir_packed_copy_out(call.operands[1], status)) {
            report(
                "FSIM-ELAB-SVQUEUE-001",
                "$q_full status argument must be a writable 32-bit integer",
                hir_source_span(call.source));
            return std::nullopt;
        }
        result = expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(destination, expected_width, true)
              }
            : std::optional { destination };
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == "$isunknown") {
        const auto& call = *expression->systemverilog;
        if (language_ != frontend::Language::SystemVerilog2017
            || call.operands.size() != 1U) {
            report(
                "FSIM-ELAB-084",
                "$isunknown requires SystemVerilog and exactly one packed "
                "argument",
                hir_source_span(call.source));
            return std::nullopt;
        }
        emit_debug_point(
            DebugPointKind::call, hir_source_span(call.source));
        const auto operand = call.operands.front();
        const auto width = hir_expression_width(
            operand, hir_process_scope_)
                               .value_or(expected_width);
        const auto source_value = width != 0U
            ? lower_hir_expression(operand, width)
            : std::nullopt;
        if (!source_value) {
            return std::nullopt;
        }
        const auto self_equal = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal,
            self_equal,
            *source_value,
            *source_value,
        });
        const auto unknown = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            unknown, PackedLogic4(1U, Logic4::x) });
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            destination,
            self_equal,
            unknown,
        });
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == "std::randomize") {
        const auto& call = *expression->systemverilog;
        if (language_ != frontend::Language::SystemVerilog2017
            || call.operands.empty()) {
            report(
                "FSIM-ELAB-SVRAND-001",
                "std::randomize requires one or more local packed "
                "arguments in SystemVerilog",
                hir_source_span(call.source));
            return invalid_integral_result();
        }
        auto inline_constraints = hir_inline_constraints(
            expression_id, hir_process_scope_);
        if (!inline_constraints) {
            return std::nullopt;
        }
        ScopeRandomize operation;
        operation.destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        operation.inline_constraints = std::move(*inline_constraints);
        operation.maximum_domain_values = std::size_t { 1U } << 20U;
        for (const auto operand : call.operands) {
            const auto argument = specialized_hir_unit_->find_expression(
                operand);
            const auto declaration_id = hir_referenced_declaration(operand);
            if (!argument || argument->systemverilog == nullptr
                || argument->systemverilog->kind
                    != semantic::sv::ExpressionKind::name
                || !declaration_id) {
                report(
                    "FSIM-ELAB-SVRAND-002",
                    "std::randomize arguments must be writable local "
                    "identifiers",
                    argument && argument->systemverilog != nullptr
                        ? hir_source_span(argument->systemverilog->source)
                        : hir_source_span(call.source));
                return invalid_integral_result();
            }
            const auto declaration
                = specialized_hir_unit_->find_declaration(*declaration_id);
            const auto binding = hir_runtime_binding(
                *declaration_id, hir_process_scope_, true);
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::variable
                || !declaration->systemverilog->type
                || !binding
                || binding->kind != HirRuntimeBindingKind::local
                || !binding->local) {
                report(
                    "FSIM-ELAB-SVRAND-003",
                    "std::randomize argument '"
                        + argument->systemverilog->text
                        + "' is not a supported packed local",
                    hir_source_span(argument->systemverilog->source));
                return invalid_integral_result();
            }
            const auto& type = *declaration->systemverilog->type;
            if (binding->width == 0U
                || binding->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVRAND-004",
                    "std::randomize packed locals require a nonempty "
                    "width representable by SimIR metadata",
                    hir_source_span(argument->systemverilog->source));
                return invalid_integral_result();
            }
            const auto definition = type.target.target.valid()
                ? specialized_hir_unit_->find_type(type.target.target)
                : std::nullopt;
            const auto enumeration = definition
                    && definition->systemverilog != nullptr
                    && definition->systemverilog->form
                        == semantic::sv::TypeForm::enumeration
                ? definition->systemverilog
                : nullptr;
            ScopeRandomizeTarget target;
            target.target = *binding->local;
            target.canonical_identity = process_.name + "::"
                + declaration->systemverilog->name;
            target.width = static_cast<std::uint32_t>(binding->width);
            target.signed_value = binding->signed_value;
            target.nominal_type = !type.class_identity.empty()
                ? type.class_identity
                : type.target.spelling;
            target.domain_kind = enumeration != nullptr
                ? ScopeRandomizeDomainKind::enumeration
                : binding->domain == frontend::ValueDomain::Integer
                ? ScopeRandomizeDomainKind::integer
                : ScopeRandomizeDomainKind::bit_vector;
            if (enumeration != nullptr) {
                for (std::size_t ordinal { };
                    ordinal < enumeration->enumeration_literals.size();
                    ++ordinal) {
                    target.domain.push_back(
                        PackedLogic4::from_aval_bval(
                            binding->width, ordinal, 0U));
                }
            }
            operation.targets.push_back(std::move(target));
        }
        const auto destination = operation.destination;
        process_.operations.emplace_back(std::move(operation));
        result = destination;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == "$urandom"
            || expression->systemverilog->text == "$random"
            || expression->systemverilog->text == "$urandom_range")) {
        const auto& call = *expression->systemverilog;
        const auto range = call.text == "$urandom_range";
        const auto valid_arguments = range
            ? !call.operands.empty() && call.operands.size() <= 2U
            : call.operands.empty();
        if (language_ == frontend::Language::Vhdl2008
            || (call.text != "$random"
                && language_ != frontend::Language::SystemVerilog2017)
            || !valid_arguments) {
            report(
                "FSIM-ELAB-104",
                range
                    ? "$urandom_range requires one maximum and an optional "
                      "minimum argument in SystemVerilog"
                    : call.text + " requires no arguments"
                        + (call.text == "$urandom"
                                ? " in SystemVerilog"
                                : ""),
                hir_source_span(call.source));
            return std::nullopt;
        }
        emit_debug_point(
            DebugPointKind::call, hir_source_span(call.source));
        std::optional<RegisterId> maximum;
        std::optional<RegisterId> minimum;
        if (range) {
            maximum = lower_hir_expression(call.operands[0], 32U);
            if (maximum && register_width(*maximum) != 32U) {
                maximum = resize_register(
                    *maximum, 32U,
                    hir_expression_signed(call.operands[0]));
            }
            if (call.operands.size() == 2U) {
                minimum = lower_hir_expression(call.operands[1], 32U);
                if (minimum && register_width(*minimum) != 32U) {
                    minimum = resize_register(
                        *minimum, 32U,
                        hir_expression_signed(call.operands[1]));
                }
            }
            if (!maximum
                || (call.operands.size() == 2U && !minimum)) {
                return std::nullopt;
            }
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(RandomValue {
            destination,
            range
                ? RandomKind::urandom_range
                : call.text == "$urandom"
                ? RandomKind::urandom
                : RandomKind::random,
            maximum,
            minimum,
        });
        result = destination;
    } else if (const auto cast = hir_systemverilog_cast_profile(
                   expression_id)) {
        const auto& operand = expression->systemverilog->operands.front();
        const auto source_width = hir_expression_width(
            operand, hir_process_scope_)
                                      .value_or(cast->width);
        auto lowered = lower_hir_expression(operand, source_width);
        if (!lowered) {
            return std::nullopt;
        }
        if (register_width(*lowered) != cast->width) {
            lowered = resize_register(
                *lowered, cast->width, hir_expression_signed(operand));
        }
        if (is_two_state_domain(cast->domain)
            && !is_two_state_domain(register_domain(*lowered))) {
            lowered = convert_to_two_state(*lowered);
        }
        if (register_domain(*lowered) != cast->domain) {
            const auto destination = allocate_register(
                cast->width, cast->domain);
            process_.operations.emplace_back(CopyRegister {
                destination, *lowered });
            lowered = destination;
        }
        result = *lowered;
    } else if (const auto selection = hir_vhdl_array_selection(
                   expression_id)) {
        const auto binding = hir_runtime_binding(
            selection->declaration, hir_process_scope_, true);
        if (selection->root_width == 0U
            || selection->root_width
                > std::numeric_limits<std::uint32_t>::max()
            || selection->width == 0U
            || selection->width
                > std::numeric_limits<std::uint32_t>::max()
            || selection->offset > selection->root_width
            || selection->width
                > selection->root_width - selection->offset) {
            return std::nullopt;
        }
        std::optional<RegisterId> root;
        if (binding) {
            if (selection->root_width != binding->width) {
                return std::nullopt;
            }
            if (binding->kind == HirRuntimeBindingKind::local) {
                root = binding->local;
            } else if (binding->signal) {
                root = allocate_register(binding->width, binding->domain);
                process_.operations.emplace_back(ReadSignal {
                    *root,
                    *binding->signal,
                    sample_concurrent_assertion_reads_
                        ? SignalReadKind::sampled
                        : SignalReadKind::current,
                });
                implicit_signal_dependencies_.push_back(*binding->signal);
            }
        } else {
            // Constant array declarations have no runtime binding. Materialize
            // only an exactly typed, bounded packed-array projection here;
            // dynamic selectors still use the normal checked offset path.
            const auto declaration = specialized_hir_unit_->find_declaration(
                selection->declaration);
            using DeclarationForm
                = semantic::vhdl::DeclarationForm;
            if (!declaration || declaration->vhdl == nullptr
                || (declaration->vhdl->form
                        != DeclarationForm::constant
                    && declaration->vhdl->form
                        != DeclarationForm::generic_constant)
                || !declaration->vhdl->subtype) {
                return std::nullopt;
            }
            const auto array_value
                = specialized_hir_unit_->evaluate_vhdl_packed_array_declaration(
                    selection->declaration);
            const auto declared_subtype = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            if (!array_value || !declared_subtype
                || !declared_subtype->type_mark.target.valid()) {
                return std::nullopt;
            }

            const semantic::vhdl::TypeDefinition* array_definition = nullptr;
            auto array_type_id = declared_subtype->type_mark.target;
            std::unordered_set<std::uint32_t> visited_types;
            while (array_type_id.valid()
                && visited_types.insert(array_type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(
                    array_type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::array) {
                    array_definition = type->vhdl;
                    break;
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                array_type_id = type->vhdl->base.type_mark.target;
            }
            if (array_definition == nullptr
                || array_definition->array_dimensions.size() != 1U
                || !array_definition->element_subtype
                || array_value->elements.empty()) {
                return std::nullopt;
            }

            using VhdlRangeBounds
                = std::pair<std::int64_t, std::int64_t>;
            const auto range_bounds = [&](
                const semantic::vhdl::RangeConstraint& range)
                -> std::optional<VhdlRangeBounds> {
                if (range.kind
                    != semantic::vhdl::RangeKind::array_index) {
                    return std::nullopt;
                }
                const auto left = range.left
                    ? range.left
                    : range.left_expression
                    ? hir_constant_integer(*range.left_expression)
                    : std::nullopt;
                const auto right = range.right
                    ? range.right
                    : range.right_expression
                    ? hir_constant_integer(*range.right_expression)
                    : std::nullopt;
                if (!left || !right || range.null
                    || (*left != *right
                        && range.descending != (*left > *right))) {
                    return std::nullopt;
                }
                return std::pair { *left, *right };
            };
            std::optional<VhdlRangeBounds> outer_range;
            if (!declared_subtype->constraints.empty()) {
                if (declared_subtype->constraints.size() == 1U) {
                    outer_range = range_bounds(
                        declared_subtype->constraints.front());
                }
            } else if (array_definition->array_dimensions.front().constraint) {
                outer_range = range_bounds(
                    *array_definition->array_dimensions.front().constraint);
            }
            if (!outer_range
                || array_value->left_bound != outer_range->first
                || array_value->right_bound != outer_range->second) {
                return std::nullopt;
            }

            const auto outer_distance = index_distance(
                outer_range->first, outer_range->second);
            if (outer_distance == std::numeric_limits<std::uint64_t>::max()
                || outer_distance + 1U != array_value->elements.size()) {
                return std::nullopt;
            }

            const auto element_subtype = hir_effective_vhdl_subtype(
                *array_definition->element_subtype);
            if (!element_subtype) {
                return std::nullopt;
            }
            const auto element_domain = vhdl_subtype_domain(
                element_subtype->domain);
            if (element_domain == frontend::ValueDomain::Unknown
                || element_domain != selection->domain
                || element_domain
                    != vhdl_subtype_domain(array_value->element_domain)) {
                return std::nullopt;
            }

            std::optional<VhdlRangeBounds> element_range;
            if (element_subtype->constraints.size() == 1U) {
                element_range = range_bounds(
                    element_subtype->constraints.front());
            } else if (element_subtype->constraints.empty()
                && element_subtype->type_mark.target.valid()) {
                auto element_type_id = element_subtype->type_mark.target;
                std::unordered_set<std::uint32_t> visited_element_types;
                while (element_type_id.valid()
                    && visited_element_types.insert(
                           element_type_id.value()).second) {
                    const auto type = specialized_hir_unit_->find_type(
                        element_type_id);
                    if (!type || type->vhdl == nullptr) {
                        break;
                    }
                    if (type->vhdl->form
                        == semantic::vhdl::TypeForm::array) {
                        if (type->vhdl->array_dimensions.size() == 1U
                            && type->vhdl->array_dimensions.front()
                                   .constraint) {
                            element_range = range_bounds(
                                *type->vhdl->array_dimensions.front().constraint);
                        }
                        break;
                    }
                    if (type->vhdl->form
                            != semantic::vhdl::TypeForm::subtype
                        && type->vhdl->form
                            != semantic::vhdl::TypeForm::alias) {
                        break;
                    }
                    element_type_id = type->vhdl->base.type_mark.target;
                }
            }
            if (!element_range) {
                return std::nullopt;
            }

            const auto element_distance = index_distance(
                element_range->first, element_range->second);
            if (element_distance
                    == std::numeric_limits<std::uint64_t>::max()
                || element_distance
                    >= std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto element_width = static_cast<std::size_t>(
                element_distance + 1U);
            if (element_width == 0U
                || array_value->elements.size()
                    > std::numeric_limits<std::size_t>::max()
                        / element_width) {
                return std::nullopt;
            }
            const auto flat_width
                = array_value->elements.size() * element_width;
            if (flat_width != selection->root_width
                || flat_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }

            std::string flat_bits;
            flat_bits.reserve(flat_width);
            for (const auto& array_element : array_value->elements) {
                if (array_element.left_bound != element_range->first
                    || array_element.right_bound != element_range->second
                    || array_element.bits.size() != element_width) {
                    return std::nullopt;
                }
                flat_bits.append(array_element.bits);
            }
            try {
                auto value = element_domain
                        == frontend::ValueDomain::Logic9
                    ? PackedLogic4::from_logic9_msb_string(flat_bits)
                    : PackedLogic4::from_msb_string(flat_bits);
                if (value.width() != flat_width) {
                    return std::nullopt;
                }
                root = allocate_register(flat_width, element_domain);
                process_.operations.emplace_back(LoadConstant {
                    *root, std::move(value) });
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        }
        if (!root) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            selection->width, selection->domain);
        if (selection->dynamic_indices.empty()) {
            if (selection->offset
                > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            process_.operations.emplace_back(Extract {
                destination,
                *root,
                static_cast<std::uint32_t>(selection->offset),
                static_cast<std::uint32_t>(selection->width),
            });
        } else {
            const auto offset = lower_hir_vhdl_array_offset(
                *selection);
            if (!offset || selection->root_width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            process_.operations.emplace_back(DynamicPartSelect {
                destination,
                *root,
                *offset,
                static_cast<std::int64_t>(selection->root_width - 1U),
                0,
                static_cast<std::uint32_t>(selection->width),
                true,
                true,
                is_two_state_domain(register_domain(*root)),
                0U,
            });
        }
        result = destination;
    } else if (const auto member = hir_vhdl_member_selection(
                   expression_id)) {
        const auto binding = hir_runtime_binding(
            member->declaration, hir_process_scope_, true);
        const auto root_width = member->root.valid()
            ? hir_expression_width(member->root, hir_process_scope_)
            : binding
            ? std::optional { binding->width }
            : std::nullopt;
        if (!root_width || *root_width == 0U
            || member->offset > *root_width
            || member->width > *root_width - member->offset
            || member->offset
                > std::numeric_limits<std::uint32_t>::max()
            || member->width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        std::optional<RegisterId> root;
        if (member->root.valid()) {
            root = lower_hir_expression(member->root, *root_width);
        } else if (binding && binding->kind == HirRuntimeBindingKind::local) {
            root = binding->local;
        } else if (binding && binding->signal) {
            root = allocate_register(binding->width, binding->domain);
            process_.operations.emplace_back(ReadSignal {
                *root,
                *binding->signal,
                sample_concurrent_assertion_reads_
                    ? SignalReadKind::sampled
                    : SignalReadKind::current,
            });
            implicit_signal_dependencies_.push_back(*binding->signal);
        }
        if (!root) {
            return std::nullopt;
        }
        const auto member_value = allocate_register(
            member->width, member->domain);
        process_.operations.emplace_back(Extract {
            member_value,
            *root,
            static_cast<std::uint32_t>(member->offset),
            static_cast<std::uint32_t>(member->width),
        });
        if (!member->index) {
            result = member_value;
        } else {
            const auto range = member->subtype.constraints.empty()
                ? nullptr
                : &member->subtype.constraints.front();
            const auto left = range == nullptr
                ? std::nullopt
                : range->left
                ? range->left
                : range->left_expression
                ? hir_constant_integer(*range->left_expression)
                : std::nullopt;
            const auto right = range == nullptr
                ? std::nullopt
                : range->right
                ? range->right
                : range->right_expression
                ? hir_constant_integer(*range->right_expression)
                : std::nullopt;
            const auto selected = hir_constant_integer(*member->index);
            const auto in_range = left && right && selected
                && *selected >= std::min(*left, *right)
                && *selected <= std::max(*left, *right);
            if (!range || range->null || !in_range
                || (*left != *right
                    && range->descending != (*left > *right))) {
                return std::nullopt;
            }
            const auto ordinal = index_distance(*selected, *right);
            if (member->element_width == 0U
                || ordinal
                    > std::numeric_limits<std::size_t>::max()
                        / member->element_width) {
                return std::nullopt;
            }
            const auto offset = static_cast<std::size_t>(ordinal)
                * member->element_width;
            if (offset > member->width
                || member->element_width > member->width - offset
                || offset > std::numeric_limits<std::uint32_t>::max()
                || member->element_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                member->element_width, member->domain);
            process_.operations.emplace_back(Extract {
                destination,
                member_value,
                static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(member->element_width),
            });
            result = destination;
        }
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text.starts_with("@vhdl-member:")
        && expression->vhdl->operands.size() == 1U) {
        constexpr auto prefix = std::string_view { "@vhdl-member:" };
        const auto member_name
            = std::string_view { expression->vhdl->text }.substr(
                prefix.size());
        const auto base_type = hir_vhdl_composite_root_type(
            expression->vhdl->operands.front());
        const auto definition = base_type
                && base_type->first == semantic::vhdl::TypeForm::record
            ? specialized_hir_unit_->find_type(base_type->second)
            : std::nullopt;
        if (!definition || definition->vhdl == nullptr
            || definition->vhdl->form
                != semantic::vhdl::TypeForm::record) {
            return std::nullopt;
        }
        const auto& elements = definition->vhdl->record_elements;
        const auto selected = std::ranges::find_if(
            elements,
            [&](const semantic::vhdl::RecordElement& record_element) {
                return same_vhdl_identifier(
                    record_element.name, member_name);
            });
        report(
            "FSIM-ELAB-VHCOMPOP-004",
            selected == elements.end()
                ? "VHDL record selection names unknown member '"
                    + std::string { member_name } + "'"
                : "VHDL record member '" + std::string { member_name }
                    + "' has no bounded executable read layout",
            hir_source_span(expression->vhdl->source));
        return invalid_integral_result();
    } else if (const auto conversion = hir_vhdl_conversion_profile(
                   expression_id, expected_width)) {
        const auto operand = expression->vhdl->operands.front();
        const auto operand_expression
            = specialized_hir_unit_->find_expression(operand);
        const auto selected = hir_vhdl_type_declaration(expression_id);
        const auto declaration = selected
            ? specialized_hir_unit_->find_declaration(*selected)
            : std::nullopt;
        auto target_subtype
            = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        if (!target_subtype) {
            auto type_name = std::string_view { expression->vhdl->text };
            if (type_name.starts_with(qualified_prefix)) {
                type_name.remove_prefix(qualified_prefix.size());
            }
            if (const auto separator = type_name.find_last_of(".:");
                separator != std::string_view::npos) {
                type_name.remove_prefix(separator + 1U);
            }
            const auto predefined_array
                = same_vhdl_identifier(type_name, "bit_vector")
                || same_vhdl_identifier(type_name, "std_logic_vector")
                || same_vhdl_identifier(type_name, "std_ulogic_vector")
                || same_vhdl_identifier(type_name, "signed")
                || same_vhdl_identifier(type_name, "unsigned");
            if (predefined_array && conversion->width != 0U
                && conversion->width
                    <= static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                target_subtype.emplace();
                target_subtype->type_mark.spelling
                    = std::string { type_name };
                target_subtype->domain
                    = conversion->domain == frontend::ValueDomain::Bit2
                    ? semantic::vhdl::ValueDomain::bit2
                    : semantic::vhdl::ValueDomain::logic9;
                target_subtype->executable_width = conversion->width;
                semantic::vhdl::RangeConstraint constraint;
                constraint.left = static_cast<std::int64_t>(
                    conversion->width - 1U);
                constraint.right = 0;
                constraint.descending = true;
                target_subtype->constraints.push_back(
                    std::move(constraint));
            }
        }
        const auto target_type = declaration
                && declaration->vhdl != nullptr
                && declaration->vhdl->declared_type
            ? specialized_hir_unit_->find_type(
                  *declaration->vhdl->declared_type)
            : std::nullopt;
        const auto* composite_definition = target_type
                && target_type->vhdl != nullptr
                && (target_type->vhdl->form
                        == semantic::vhdl::TypeForm::array
                    || target_type->vhdl->form
                        == semantic::vhdl::TypeForm::record)
            ? target_type->vhdl
            : nullptr;
        if (target_subtype && composite_definition != nullptr) {
            if (target_subtype->domain
                == semantic::vhdl::ValueDomain::unknown) {
                target_subtype->domain = composite_definition->base.domain;
            }
            if (!target_subtype->executable_width
                || *target_subtype->executable_width == 0U) {
                target_subtype->executable_width
                    = composite_definition->base.executable_width;
            }
        }
        if (!expression->vhdl->text.starts_with(qualified_prefix)) {
            const auto operand_domain = hir_expression_domain(
                operand, hir_process_scope_);
            auto conversion_compatible = operand_domain
                && *operand_domain == conversion->domain;
            const auto operand_subtype
                = hir_vhdl_expression_subtype(operand);
            const auto predefined_array = [](std::string_view spelling) {
                if (const auto separator = spelling.find_last_of(".:");
                    separator != std::string_view::npos) {
                    spelling.remove_prefix(separator + 1U);
                }
                return same_vhdl_identifier(spelling, "bit_vector")
                    || same_vhdl_identifier(
                        spelling, "std_logic_vector")
                    || same_vhdl_identifier(
                        spelling, "std_ulogic_vector")
                    || same_vhdl_identifier(spelling, "signed")
                    || same_vhdl_identifier(spelling, "unsigned");
            };
            const auto target_predefined_array
                = target_subtype
                && predefined_array(target_subtype->type_mark.spelling);
            const auto operand_predefined_array
                = operand_subtype
                && predefined_array(operand_subtype->type_mark.spelling);
            if (target_predefined_array && operand_predefined_array) {
                const auto operand_width = hir_expression_width(
                    operand, hir_process_scope_);
                conversion_compatible = operand_width
                    && *operand_width == conversion->width;
            }
            const auto root_type = [&](semantic::TypeId type) {
                std::unordered_set<std::uint32_t> visiting;
                while (type.valid()
                    && visiting.insert(type.value()).second) {
                    const auto definition
                        = specialized_hir_unit_->find_type(type);
                    if (!definition || definition->vhdl == nullptr
                        || (definition->vhdl->form
                                != semantic::vhdl::TypeForm::subtype
                            && definition->vhdl->form
                                != semantic::vhdl::TypeForm::alias)
                        || !definition->vhdl->base.type_mark.target.valid()) {
                        break;
                    }
                    type = definition->vhdl->base.type_mark.target;
                }
                return type;
            };
            if (target_subtype
                && operand_subtype
                && target_subtype->type_mark.target.valid()
                && operand_subtype->type_mark.target.valid()) {
                const auto target_root = root_type(
                    target_subtype->type_mark.target);
                const auto operand_root = root_type(
                    operand_subtype->type_mark.target);
                const auto target_definition
                    = specialized_hir_unit_->find_type(target_root);
                const auto operand_definition
                    = specialized_hir_unit_->find_type(operand_root);
                const auto target_form = target_definition
                        && target_definition->vhdl != nullptr
                    ? target_definition->vhdl->form
                    : semantic::vhdl::TypeForm::unresolved;
                const auto operand_form = operand_definition
                        && operand_definition->vhdl != nullptr
                    ? operand_definition->vhdl->form
                    : semantic::vhdl::TypeForm::unresolved;
                const auto target_array
                    = target_form == semantic::vhdl::TypeForm::array;
                const auto operand_array
                    = operand_form == semantic::vhdl::TypeForm::array;
                const auto target_record
                    = target_form == semantic::vhdl::TypeForm::record;
                const auto operand_record
                    = operand_form == semantic::vhdl::TypeForm::record;
                const auto target_enumeration
                    = target_form
                    == semantic::vhdl::TypeForm::enumeration;
                const auto operand_enumeration
                    = operand_form
                    == semantic::vhdl::TypeForm::enumeration;
                if (target_array && operand_array) {
                    // Closely related array conversions may change the
                    // signed/unsigned interpretation while retaining rank,
                    // element type, and executable shape.
                    conversion_compatible = true;
                }
                if (!target_array && !operand_array
                    && !target_record && !operand_record
                    && target_root == operand_root) {
                    conversion_compatible = true;
                }
                if ((target_record || operand_record)
                    && target_root != operand_root) {
                    conversion_compatible = false;
                }
                if ((target_enumeration || operand_enumeration)
                    && target_root != operand_root) {
                    conversion_compatible = false;
                }
                if (conversion_compatible
                    && (target_array || operand_array)) {
                    conversion_compatible = target_array && operand_array;
                    const auto& target_constraints
                        = target_subtype->constraints;
                    const auto& operand_constraints
                        = operand_subtype->constraints;
                    const auto target_rank = std::max(
                        target_constraints.size(),
                        target_definition->vhdl
                            ->array_dimensions.size());
                    const auto operand_rank = std::max(
                        operand_constraints.size(),
                        operand_definition->vhdl
                            ->array_dimensions.size());
                    conversion_compatible = conversion_compatible
                        && target_rank == operand_rank;
                    for (std::size_t index { };
                        conversion_compatible
                        && index < std::min(target_constraints.size(),
                            operand_constraints.size()); ++index) {
                        const auto& target_constraint
                            = target_constraints[index];
                        const auto& operand_constraint
                            = operand_constraints[index];
                        const auto target_length
                            = target_constraint.left
                                && target_constraint.right
                            ? std::optional<std::uint64_t> {
                                  index_distance(
                                      *target_constraint.left,
                                      *target_constraint.right)
                                  + 1U }
                            : std::nullopt;
                        const auto operand_length
                            = operand_constraint.left
                                && operand_constraint.right
                            ? std::optional<std::uint64_t> {
                                  index_distance(
                                      *operand_constraint.left,
                                      *operand_constraint.right)
                                  + 1U }
                            : std::nullopt;
                        conversion_compatible
                            = target_constraint.descending
                                == operand_constraint.descending
                            && (!target_length || !operand_length
                                || *target_length == *operand_length)
                            && (!target_constraint.left
                                || !target_constraint.right
                                || !operand_constraint.left
                                || !operand_constraint.right
                                || (*target_constraint.left
                                        == *operand_constraint.left
                                    && *target_constraint.right
                                        == *operand_constraint.right));
                    }
                    const auto element_root = [&](const auto& definition)
                        -> std::optional<semantic::TypeId> {
                        if (!definition->vhdl->element_subtype
                            || !definition->vhdl->element_subtype
                                    ->type_mark.target.valid()) {
                            return std::nullopt;
                        }
                        return root_type(definition->vhdl
                                ->element_subtype->type_mark.target);
                    };
                    const auto target_element
                        = element_root(target_definition);
                    const auto operand_element
                        = element_root(operand_definition);
                    if (target_element && operand_element
                        && *target_element != *operand_element) {
                        conversion_compatible = false;
                    } else if (!target_element || !operand_element) {
                        const auto* target_element_subtype
                            = target_definition->vhdl->element_subtype
                            ? &*target_definition->vhdl->element_subtype
                            : nullptr;
                        const auto* operand_element_subtype
                            = operand_definition->vhdl->element_subtype
                            ? &*operand_definition->vhdl->element_subtype
                            : nullptr;
                        if (target_element_subtype == nullptr
                            || operand_element_subtype == nullptr) {
                            conversion_compatible = false;
                        } else if (target_element_subtype->domain
                                != semantic::vhdl::ValueDomain::unknown
                            && operand_element_subtype->domain
                                != semantic::vhdl::ValueDomain::unknown
                            && target_element_subtype->domain
                                != operand_element_subtype->domain) {
                            conversion_compatible = false;
                        } else {
                            const auto simple_type_name = [](
                                std::string_view spelling) {
                                const auto separator
                                    = spelling.find_last_of(".:");
                                return spelling.substr(
                                    separator == std::string_view::npos
                                        ? 0U
                                        : separator + 1U);
                            };
                            const auto target_element_name
                                = simple_type_name(
                                    target_element_subtype
                                        ->type_mark.spelling);
                            const auto operand_element_name
                                = simple_type_name(
                                    operand_element_subtype
                                        ->type_mark.spelling);
                            const auto resolved_logic_pair
                                = (same_vhdl_identifier(
                                       target_element_name, "std_logic")
                                      && same_vhdl_identifier(
                                          operand_element_name,
                                          "std_ulogic"))
                                || (same_vhdl_identifier(
                                        target_element_name,
                                        "std_ulogic")
                                    && same_vhdl_identifier(
                                        operand_element_name,
                                        "std_logic"));
                            if (!target_element_name.empty()
                                && !operand_element_name.empty()
                                && !same_vhdl_identifier(
                                    target_element_name,
                                    operand_element_name)
                                && !resolved_logic_pair) {
                                conversion_compatible = false;
                            }
                        }
                    }
                }
            }
            const auto contextual_enumeration_literal
                = target_type && target_type->vhdl != nullptr
                && !target_type->vhdl->enumeration_literals.empty()
                && operand_expression
                && operand_expression->vhdl != nullptr
                && std::ranges::any_of(
                    target_type->vhdl->enumeration_literals,
                    [&](const auto& literal) {
                        return same_vhdl_identifier(
                            literal.spelling,
                            operand_expression->vhdl->text);
                    });
            conversion_compatible = conversion_compatible
                || contextual_enumeration_literal;
            if (!conversion_compatible) {
                report(
                    "FSIM-ELAB-VHCONV-003",
                    "VHDL conversion operand is not closely related to "
                    "the target type or changes its executable shape",
                    hir_source_span(expression->vhdl->source));
                return invalid_integral_result();
            }
        }
        auto target_type_id = target_subtype
            ? target_subtype->type_mark.target
            : semantic::TypeId { };
        std::unordered_set<std::uint32_t> visiting_target_types;
        auto target_definition = target_type_id.valid()
            ? specialized_hir_unit_->find_type(target_type_id)
            : std::nullopt;
        while (target_definition
            && target_definition->vhdl != nullptr
            && (target_definition->vhdl->form
                    == semantic::vhdl::TypeForm::subtype
                || target_definition->vhdl->form
                    == semantic::vhdl::TypeForm::alias)
            && target_definition->vhdl->base.type_mark.target.valid()
            && visiting_target_types.insert(target_type_id.value()).second) {
            target_type_id
                = target_definition->vhdl->base.type_mark.target;
            target_definition
                = specialized_hir_unit_->find_type(target_type_id);
        }
        const auto enumeration_target = target_definition
            && target_definition->vhdl != nullptr
            && target_definition->vhdl->form
                == semantic::vhdl::TypeForm::enumeration;
        const auto enumeration_ordinal_target = enumeration_target
            && conversion->domain == frontend::ValueDomain::Bit2;
        const auto scalar_range_target
            = conversion->domain == frontend::ValueDomain::Integer
            || enumeration_ordinal_target;
        auto runtime_range = scalar_range_target
            ? conversion->integer_range
            : std::nullopt;
        if (!runtime_range && scalar_range_target && target_subtype
            && !expression->vhdl->text.starts_with(qualified_prefix)) {
            const auto constraint = std::ranges::find_if(
                target_subtype->constraints,
                [](const semantic::vhdl::RangeConstraint& candidate) {
                    return candidate.kind
                            == semantic::vhdl::RangeKind::integer
                        || candidate.kind
                            == semantic::vhdl::RangeKind::enumeration
                        || candidate.kind
                            == semantic::vhdl::RangeKind::discrete;
                });
            if (constraint != target_subtype->constraints.end()) {
                const auto left = constraint->left
                    ? constraint->left
                    : constraint->left_expression
                    ? hir_constant_integer(*constraint->left_expression)
                    : std::nullopt;
                const auto right = constraint->right
                    ? constraint->right
                    : constraint->right_expression
                    ? hir_constant_integer(*constraint->right_expression)
                    : std::nullopt;
                if (left && right) {
                    runtime_range = frontend::IntegerRange {
                        *left, *right, constraint->descending
                    };
                }
            }
        }
        std::optional<RegisterId> contextual_enumeration_value;
        if (enumeration_target && operand_expression
            && operand_expression->vhdl != nullptr
            && (operand_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name
                || operand_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::logic_literal)) {
            const auto literal = std::ranges::find_if(
                target_definition->vhdl->enumeration_literals,
                [&](const semantic::vhdl::EnumerationLiteral& candidate) {
                    return same_vhdl_identifier(
                        candidate.spelling,
                        operand_expression->vhdl->text);
                });
            if (literal
                != target_definition->vhdl->enumeration_literals.end()) {
                const auto destination = allocate_register(
                    conversion->width, conversion->domain);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(literal->ordinal, conversion->width),
                });
                contextual_enumeration_value = destination;
            }
        }
        auto lowered = contextual_enumeration_value
            ? contextual_enumeration_value
            : operand_expression
                && operand_expression->vhdl != nullptr
                && operand_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && target_subtype
            ? lower_hir_vhdl_aggregate(
                  operand, conversion->width, &*target_subtype)
            : lower_hir_expression(operand, conversion->width);
        if (!lowered) {
            return std::nullopt;
        }
        if (register_width(*lowered) != conversion->width) {
            lowered = resize_register(
                *lowered,
                conversion->width,
                conversion->signed_value);
        }
        if (is_two_state_domain(conversion->domain)
            && !is_two_state_domain(register_domain(*lowered))) {
            lowered = convert_to_two_state(*lowered);
        }
        if (register_domain(*lowered) != conversion->domain) {
            const auto destination = allocate_register(
                conversion->width, conversion->domain);
            process_.operations.emplace_back(CopyRegister {
                destination, *lowered });
            lowered = destination;
        }
        if (runtime_range) {
            auto checked = *lowered;
            if (enumeration_ordinal_target) {
                if (const auto constant = hir_constant_integer(operand);
                    constant && *constant >= 0) {
                    checked = allocate_register(
                        32U, frontend::ValueDomain::Integer);
                    process_.operations.emplace_back(LoadConstant {
                        checked,
                        unsigned_value(
                            static_cast<std::uint64_t>(*constant), 32U),
                    });
                }
            }
            process_.operations.emplace_back(IntegerCheck {
                checked,
                std::min(runtime_range->left, runtime_range->right),
                std::max(runtime_range->left, runtime_range->right),
            });
        }
        result = *lowered;
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "is_x"
        && expression->vhdl->operands.size() == 1U) {
        const auto operand = expression->vhdl->operands.front();
        const auto source_width = hir_expression_width(
            operand, hir_process_scope_);
        const auto source_domain = hir_expression_domain(
            operand, hir_process_scope_);
        if (!source_width || *source_width == 0U
            || source_domain
                != frontend::ValueDomain::Logic9
            || (expected_width != 0U && expected_width != 1U)) {
            report(
                "FSIM-ELAB-VHLOGIC-002",
                "is_x requires a nonempty standard-logic operand and "
                "a scalar result",
                hir_source_span(expression->vhdl->source));
            return std::nullopt;
        }
        const auto lowered_source = lower_hir_expression(
            operand, *source_width);
        if (!lowered_source) {
            return std::nullopt;
        }
        const auto constant = [&](const frontend::ValueDomain domain,
                                  const runtime::Logic9 state) {
            const auto destination = allocate_register(1U, domain);
            if (domain == frontend::ValueDomain::Logic9) {
                runtime::PackedLogic4 value(1U);
                value.fill(state);
                process_.operations.emplace_back(
                    runtime::simir::LoadConstant {
                        destination, std::move(value) });
            } else {
                process_.operations.emplace_back(
                    runtime::simir::LoadConstant {
                        destination,
                        unsigned_value(
                            state == runtime::Logic9::one ? 1U : 0U,
                            1U),
                    });
            }
            return destination;
        };
        const auto scalar_at = [&](const std::size_t bit) {
            if (*source_width == 1U) {
                return *lowered_source;
            }
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(runtime::simir::Extract {
                destination,
                *lowered_source,
                static_cast<std::uint32_t>(bit),
                1U,
            });
            return destination;
        };
        const auto exact = [&](const runtime::simir::RegisterId scalar,
                               const runtime::Logic9 state) {
            const auto literal = constant(
                frontend::ValueDomain::Logic9, state);
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(runtime::simir::Binary {
                runtime::simir::BinaryOperator::case_equal,
                destination,
                scalar,
                literal,
            });
            return destination;
        };
        auto any_unknown = constant(
            frontend::ValueDomain::Boolean, runtime::Logic9::zero);
        for (std::size_t bit { }; bit < *source_width; ++bit) {
            const auto scalar = scalar_at(bit);
            auto known = exact(scalar, runtime::Logic9::zero);
            for (const auto state : {
                     runtime::Logic9::one,
                     runtime::Logic9::l,
                     runtime::Logic9::h }) {
                const auto match = exact(scalar, state);
                const auto combined = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(runtime::simir::Binary {
                    runtime::simir::BinaryOperator::bit_or,
                    combined,
                    known,
                    match,
                });
                known = combined;
            }
            const auto unknown = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(runtime::simir::LogicalNot {
                unknown, known });
            const auto combined = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(runtime::simir::Binary {
                runtime::simir::BinaryOperator::bit_or,
                combined,
                any_unknown,
                unknown,
            });
            any_unknown = combined;
        }
        result = any_unknown;
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->operands.size() == 1U
        && !is_hir_vhdl_file_call(expression_id)
        && !is_hir_vhdl_access_expression(expression_id)
        && !is_hir_vhdl_physical_expression(expression_id)
        && !is_hir_vhdl_protected_expression(expression_id)
        && expression->vhdl->text != "rising_edge"
        && expression->vhdl->text != "falling_edge"
        && !expression->vhdl->text.starts_with("@vhdl-qualified:")) {
        auto type_name = std::string { expression->vhdl->text };
        std::ranges::transform(
            type_name, type_name.begin(), [](const char value) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(value)));
            });
        const auto declaration_id = hir_referenced_declaration(
            expression_id);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        const auto declared_type = declaration
            && declaration->vhdl != nullptr
            && (declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::type
                || declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::subtype);
        const auto predefined_type = type_name == "bit"
            || type_name == "boolean" || type_name == "integer"
            || type_name == "natural" || type_name == "positive"
            || type_name == "std_logic" || type_name == "std_ulogic";
        if (!declared_type && !predefined_type) {
            if (expression->vhdl->referenced_name
                && !resolve_hir_vhdl_function_call(
                    expression_id, hir_process_scope_, expected_width)) {
                const auto candidates = hir_vhdl_callable_candidates(
                    *expression->vhdl->referenced_name,
                    expression->vhdl->scope);
                report(
                    candidates.size() > 1U
                        ? "FSIM-ELAB-VHOVER-001"
                        : candidates.empty()
                        ? "FSIM-ELAB-VHNAME-001"
                        : "FSIM-ELAB-VHOVER-002",
                    candidates.size() > 1U
                        ? "VHDL function call ambiguously matches visible overloads"
                        : candidates.empty()
                        ? "VHDL function call names no visible function"
                        : "VHDL function call has no matching overload",
                    hir_source_span(expression->vhdl->source));
                return invalid_integral_result();
            }
            return lower_hir_function_call(expression_id, expected_width);
        }
        const auto subtype = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
        const auto type = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->declared_type
            ? specialized_hir_unit_->find_type(
                  *declaration->vhdl->declared_type)
            : std::nullopt;
        const auto indefinite = (subtype && subtype->unconstrained)
            || (type && type->vhdl != nullptr
                && (type->vhdl->base.unconstrained
                    || std::ranges::any_of(
                        type->vhdl->array_dimensions,
                        [](const auto& dimension) {
                            return dimension.unconstrained;
                        })));
        report(
            indefinite
                ? "FSIM-ELAB-VHCONV-002"
                : "FSIM-ELAB-VHCONV-003",
            indefinite
                ? "VHDL conversion target type is indefinite in this context"
                : "VHDL conversion operand is incompatible with its target type",
            hir_source_span(expression->vhdl->source));
        return invalid_integral_result();
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text.starts_with("@vhdl-qualified:")) {
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        const auto type_name = std::string_view {
            expression->vhdl->text
        }.substr(qualified_prefix.size());
        const auto selected = expression->vhdl->referenced_name
            ? expression->vhdl->referenced_name->selected
            : std::nullopt;
        const auto declaration = selected
            ? specialized_hir_unit_->find_declaration(*selected)
            : std::nullopt;
        const auto subtype = declaration
                && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
        const auto invisible = !declaration
            || declaration->vhdl == nullptr;
        report(
            invisible
                ? "FSIM-ELAB-VHQUAL-001"
                : "FSIM-ELAB-VHQUAL-002",
            invisible
                ? "VHDL qualified expression type mark '"
                    + std::string { type_name }
                    + "' does not denote a visible type"
                : "VHDL qualified expression type mark '"
                    + std::string { type_name }
                    + (subtype && subtype->unconstrained
                            ? "' is indefinite in this context"
                            : "' has no executable qualification profile"),
            hir_source_span(expression->vhdl->source));
        return invalid_integral_result();
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && (expression->vhdl->text == "rising_edge"
            || expression->vhdl->text == "falling_edge")
        && expression->vhdl->operands.size() == 1U) {
        report(
            "FSIM-ELAB-045",
            "a VHDL edge predicate is executable only as the sole, "
            "else-free outer statement of a sensitive process",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && systemverilog_random_distribution_kind(
            expression->systemverilog->text)) {
        const auto& call = *expression->systemverilog;
        const auto kind = *systemverilog_random_distribution_kind(
            call.text);
        const auto three_arguments
            = kind == runtime::simir::RandomDistributionKind::uniform
            || kind == runtime::simir::RandomDistributionKind::normal
            || kind == runtime::simir::RandomDistributionKind::erlang;
        const auto required_arity = three_arguments ? 3U : 2U;
        if (call.operands.size() != required_arity) {
            report(
                "FSIM-ELAB-SVRAND-008",
                call.text
                    + " requires a direct writable seed followed by "
                    + std::to_string(required_arity - 1U)
                    + " integer argument"
                    + (three_arguments ? "s" : ""),
                hir_source_span(call.source));
            return invalid_integral_result();
        }
        const auto seed_declaration = hir_target_declaration(
            call.operands.front());
        const auto seed_binding = seed_declaration
            ? hir_runtime_binding(
                  *seed_declaration, hir_process_scope_, true)
            : std::nullopt;
        if (!seed_binding || seed_binding->width < 32U
            || (seed_binding->signal
                && read_only_signals_.contains(*seed_binding->signal))) {
            report(
                "FSIM-ELAB-SVRAND-008",
                call.text
                    + " seed must be a writable packed integer variable "
                      "at least 32 bits wide",
                hir_source_span(call.source));
            return invalid_integral_result();
        }
        auto seed = seed_binding->local;
        if (seed_binding->signal) {
            seed = allocate_register(
                seed_binding->width, seed_binding->domain);
            process_.operations.emplace_back(ReadSignal {
                *seed,
                *seed_binding->signal,
                SignalReadKind::current,
            });
            implicit_signal_dependencies_.push_back(
                *seed_binding->signal);
        }
        if (!seed) {
            return std::nullopt;
        }
        if (seed_binding->width != 32U) {
            seed = resize_register(*seed, 32U, true);
        }
        const auto lower_argument = [&](const std::size_t index)
            -> std::optional<RegisterId> {
            const auto scalar_kind = hir_systemverilog_scalar_kind(
                call.operands[index]);
            if (scalar_kind
                    == frontend::SystemVerilogScalarKind::ShortReal
                || scalar_kind
                    == frontend::SystemVerilogScalarKind::Real
                || scalar_kind
                    == frontend::SystemVerilogScalarKind::Realtime
                || scalar_kind
                    == frontend::SystemVerilogScalarKind::Chandle) {
                return std::nullopt;
            }
            auto value = lower_hir_expression(
                call.operands[index], 32U);
            if (value && register_width(*value) != 32U) {
                value = resize_register(
                    *value, 32U,
                    hir_expression_signed(call.operands[index]));
            }
            return value;
        };
        const auto first = lower_argument(1U);
        const auto second = three_arguments
            ? lower_argument(2U)
            : std::optional<RegisterId> { };
        if (!first || (three_arguments && !second)) {
            report(
                "FSIM-ELAB-SVRAND-008",
                call.text + " arguments must be integral",
                hir_source_span(call.source));
            return invalid_integral_result();
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        const auto span = hir_source_span(call.source);
        process_.operations.emplace_back(RandomDistribution {
            destination,
            *seed,
            kind,
            *first,
            second,
            SourceLocation {
                span.source_name.str(),
                static_cast<std::uint32_t>(span.begin.line),
                static_cast<std::uint32_t>(span.begin.column),
            },
        });
        auto updated_seed = *seed;
        if (seed_binding->width != 32U) {
            updated_seed = resize_register(
                updated_seed, seed_binding->width, true);
        }
        if (seed_binding->local
            && updated_seed != *seed_binding->local) {
            process_.operations.emplace_back(CopyRegister {
                *seed_binding->local, updated_seed });
        } else if (seed_binding->signal) {
            process_.operations.emplace_back(WriteBlocking {
                *seed_binding->signal, updated_seed });
        }
        result = destination;
    } else if (hir_systemverilog_math_function(expression_id)) {
        result = lower_hir_systemverilog_math_call(expression_id);
    } else if (is_hir_systemverilog_coverage_call(expression_id)) {
        result = lower_hir_systemverilog_coverage_call(
            expression_id, expected_width);
    } else if (is_hir_systemverilog_file_call(expression_id)) {
        result = lower_hir_systemverilog_file_call(
            expression_id, expected_width);
    } else if (is_hir_vhdl_access_expression(expression_id)) {
        result = lower_hir_vhdl_access_expression(
            expression_id, expected_width);
    } else if (is_hir_vhdl_physical_expression(expression_id)) {
        result = lower_hir_vhdl_physical_expression(
            expression_id, expected_width);
    } else if (is_hir_vhdl_protected_expression(expression_id)) {
        result = lower_hir_vhdl_protected_expression(
            expression_id, expected_width);
    } else if (is_hir_vhdl_file_call(expression_id)) {
        result = lower_hir_vhdl_file_call(expression_id);
    } else if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text == "inside") {
        result = lower_hir_membership_expression(expression_id);
    } else if (source.call && source.text == "@sv-type") {
        report(
            "FSIM-ELAB-SVTYPE-006",
            "the SystemVerilog type operator is only a value inside a type "
            "equality comparison",
            hir_source_span(source.source));
        return std::nullopt;
    } else if (source.call) {
        if (expression->vhdl != nullptr
            && expression->vhdl->referenced_name
            && !resolve_hir_vhdl_function_call(
                expression_id, hir_process_scope_, expected_width)) {
            auto unresolved = *expression->vhdl->referenced_name;
            unresolved.selected.reset();
            unresolved.overloads.clear();
            const auto candidates = hir_vhdl_callable_candidates(
                unresolved, expression->vhdl->scope);
            std::size_t compatible_candidates { };
            for (const auto candidate : candidates) {
                const auto resolution = hir_callable_resolution(candidate);
                const auto callable = resolution
                    ? specialized_hir_unit_->find_declaration(
                          resolution->body)
                    : std::nullopt;
                const auto type = resolution
                    ? hir_callable_type(resolution->body, expected_width)
                    : std::nullopt;
                const auto actuals = resolution
                    ? bind_hir_function_actuals(
                          expression_id, resolution->body)
                    : std::nullopt;
                if (!resolution || !callable
                    || callable->vhdl == nullptr
                    || !callable->vhdl->callable
                    || !callable->vhdl->callable->function
                    || !callable->vhdl->nested_scope || !type || !actuals
                    || (expected_width != 0U
                        && type->width != expected_width)) {
                    continue;
                }
                const auto& formals
                    = callable->vhdl->callable->formals;
                bool compatible = formals.size() == actuals->size();
                for (std::size_t index { };
                    compatible && index < formals.size(); ++index) {
                    const auto formal = hir_callable_formal_binding(
                        formals[index], *callable->vhdl->nested_scope,
                        (*actuals)[index], hir_process_scope_);
                    const auto actual
                        = specialized_hir_unit_->find_expression(
                            (*actuals)[index]);
                    const auto contextual_literal = actual
                        && actual->vhdl != nullptr
                        && (actual->vhdl->kind
                                == semantic::vhdl::ExpressionKind::string_literal
                            || actual->vhdl->kind
                                == semantic::vhdl::ExpressionKind::aggregate);
                    const auto actual_width = contextual_literal
                        ? formal
                            ? std::optional<std::size_t> { formal->width }
                            : std::optional<std::size_t> { }
                        : hir_expression_width(
                              (*actuals)[index], hir_process_scope_);
                    const auto actual_domain = contextual_literal
                        ? formal
                            ? std::optional<frontend::ValueDomain> {
                                  formal->domain
                              }
                            : std::optional<frontend::ValueDomain> { }
                        : hir_expression_domain(
                              (*actuals)[index], hir_process_scope_);
                    compatible = formal && actual_width && actual_domain
                        && *actual_width == formal->width
                        && *actual_domain == formal->domain;
                }
                compatible_candidates += compatible ? 1U : 0U;
            }
            if (compatible_candidates > 1U) {
                report(
                    "FSIM-ELAB-VHOVER-001",
                    std::string { "VHDL function call '" }
                        + std::string { source.text }
                        + "' is ambiguous among visible overloads",
                    hir_source_span(source.source));
                return invalid_integral_result();
            }
        }
        result = lower_hir_function_call(expression_id, expected_width);
    } else if (const auto declaration = source.index || source.slice
                || source.unary || source.binary
            ? std::optional<semantic::DeclarationId> { }
            : hir_referenced_declaration(expression_id)) {
        const auto sv_member = hir_systemverilog_member_selection(
            expression_id);
        const auto binding = hir_runtime_binding(
            *declaration, hir_process_scope_, true);
        if (sv_member && sv_member->tagged) {
            const auto& tagged = *sv_member->tagged;
            if (tagged.tag_width
                    > std::numeric_limits<std::uint32_t>::max()
                || tagged.offset
                    > std::numeric_limits<std::uint32_t>::max()
                || tagged.payload_width
                    > std::numeric_limits<std::uint32_t>::max()
                        - tagged.offset) {
                return std::nullopt;
            }
        }
        const auto select_sv_member = [&](const RegisterId whole,
                                          const RegisterId selected) {
            if (!sv_member || !sv_member->tagged) {
                return selected;
            }
            const auto& tagged = *sv_member->tagged;
            const auto tag = allocate_register(
                tagged.tag_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Extract {
                tag,
                whole,
                static_cast<std::uint32_t>(
                    tagged.offset + tagged.payload_width),
                static_cast<std::uint32_t>(tagged.tag_width),
            });
            const auto expected = allocate_register(
                tagged.tag_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                expected,
                unsigned_value(tagged.tag_value, tagged.tag_width),
            });
            const auto active = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                active,
                tag,
                expected,
            });
            const auto inactive = allocate_register(
                sv_member->width, sv_member->domain);
            process_.operations.emplace_back(LoadConstant {
                inactive,
                PackedLogic4(
                    sv_member->width,
                    is_two_state_domain(sv_member->domain)
                        ? Logic4::zero
                        : Logic4::x),
            });
            const auto member_result = allocate_register(
                sv_member->width, sv_member->domain);
            process_.operations.emplace_back(ConditionalSelect {
                member_result, active, selected, inactive });
            return member_result;
        };
        if (binding && binding->kind == HirRuntimeBindingKind::local) {
            if (!binding->local) {
                return std::nullopt;
            }
            if (sv_member) {
                if (sv_member->offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || sv_member->width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    sv_member->width, sv_member->domain);
                process_.operations.emplace_back(Extract {
                    destination,
                    *binding->local,
                    static_cast<std::uint32_t>(sv_member->offset),
                    static_cast<std::uint32_t>(sv_member->width),
                });
                result = select_sv_member(
                    *binding->local, destination);
            } else {
                result = binding->local;
            }
        } else if (binding && binding->signal) {
            const auto destination = allocate_register(
                binding->width, binding->domain);
            if (binding->width != 0U) {
                process_.operations.emplace_back(ReadSignal {
                    destination,
                    *binding->signal,
                    sample_concurrent_assertion_reads_
                        ? SignalReadKind::sampled
                        : SignalReadKind::current,
                });
                implicit_signal_dependencies_.push_back(*binding->signal);
            }
            if (sv_member) {
                if (sv_member->offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || sv_member->width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                const auto selected = allocate_register(
                    sv_member->width, sv_member->domain);
                process_.operations.emplace_back(Extract {
                    selected,
                    destination,
                    static_cast<std::uint32_t>(sv_member->offset),
                    static_cast<std::uint32_t>(sv_member->width),
                });
                result = select_sv_member(destination, selected);
            } else {
                result = destination;
            }
        } else if (const auto initializer
            = hir_constant_initializer(*declaration)) {
            const auto width = hir_expression_width(
                expression_id, hir_process_scope_)
                                   .value_or(expected_width);
            const auto constant_value
                = specialized_hir_unit_->evaluate_integral_expression(
                    expression_id);
            if (constant_value) {
                const auto result_width = expected_width != 0U
                    ? expected_width
                    : width;
                if (result_width == 0U) {
                    return std::nullopt;
                }
                const auto domain = hir_expression_domain(
                    expression_id, hir_process_scope_)
                                        .value_or(language_
                                                    == frontend::Language::Vhdl2008
                                                ? frontend::ValueDomain::Integer
                                                : frontend::ValueDomain::Bit2);
                const auto destination = allocate_register(
                    result_width, domain);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(
                        static_cast<std::uint64_t>(*constant_value),
                        result_width),
                });
                result = destination;
            } else {
                const auto initializer_expression
                    = specialized_hir_unit_->find_expression(*initializer);
                const auto declaration_record
                    = specialized_hir_unit_->find_declaration(*declaration);
                const auto contextual_aggregate
                    = initializer_expression
                    && initializer_expression->vhdl != nullptr
                    && initializer_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate
                    && declaration_record
                    && declaration_record->vhdl != nullptr
                    && declaration_record->vhdl->subtype;
                const auto contextual_packed_pattern
                    = initializer_expression
                    && initializer_expression->systemverilog != nullptr
                    && declaration_record
                    && declaration_record->systemverilog != nullptr
                    && declaration_record->systemverilog->type
                    ? hir_systemverilog_packed_pattern_operand(
                          *initializer,
                          *declaration_record->systemverilog->type)
                    : std::nullopt;
                if (contextual_aggregate) {
                    result = lower_hir_vhdl_aggregate(
                        *initializer, width,
                        &*declaration_record->vhdl->subtype);
                } else if (contextual_packed_pattern) {
                    result = lower_hir_systemverilog_packed_pattern(
                        *contextual_packed_pattern,
                        *declaration_record->systemverilog->type,
                        width);
                } else {
                    result = lower_hir_expression(*initializer, width);
                }
            }
        }
    } else if (scalar_literal) {
        const auto width = scalar_literal->kind
                == frontend::SystemVerilogScalarKind::ShortReal
            ? 32U
            : 64U;
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_aval_bval(
                width, scalar_literal->bits, 0U),
        });
        result = destination;
    } else if (real_literal) {
        using ScalarKind = frontend::SystemVerilogScalarKind;
        const auto literal_kind
            = scalar_context == ScalarKind::ShortReal
                || scalar_context == ScalarKind::Real
                || scalar_context == ScalarKind::Realtime
            ? scalar_context
            : expected_width == 32U
            ? ScalarKind::ShortReal
            : ScalarKind::Real;
        const auto scalar = literal_kind == ScalarKind::ShortReal
            ? runtime::SystemVerilogScalarValue::shortreal(
                  static_cast<float>(*real_literal))
            : literal_kind == ScalarKind::Realtime
            ? runtime::SystemVerilogScalarValue::realtime(*real_literal)
            : runtime::SystemVerilogScalarValue::real(*real_literal);
        const auto encoded = runtime::encode_systemverilog_scalar_payload(
            scalar);
        if (!encoded) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            encoded.value.width(), frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, encoded.value });
        result = destination;
    } else if (source.integer || source.boolean || source.logic) {
        if (auto literal = hir_literal(source, expected_width, language_)) {
            const auto destination = allocate_register(
                literal->value.width(), literal->domain);
            process_.operations.emplace_back(
                LoadConstant { destination, std::move(literal->value) });
            result = destination;
        }
    } else if ((source.index || source.slice)
        && source.operands.size() == (source.index ? 2U : 3U)) {
        const auto constant_selection = hir_constant_selection(
            expression_id, hir_process_scope_);
        const auto dynamic_part_width = source.slice
            ? hir_dynamic_part_width(expression_id, hir_process_scope_)
            : std::nullopt;
        const auto vhdl_dynamic_slice
            = source.slice && expression->vhdl != nullptr
            && (!hir_constant_integer(source.operands[1])
                || !hir_constant_integer(source.operands[2]));
        const auto invalid_vhdl_constant_slice
            = source.slice && expression->vhdl != nullptr
            && !vhdl_dynamic_slice && !constant_selection;
        if (invalid_vhdl_constant_slice) {
            report(
                "FSIM-ELAB-068",
                "a VHDL slice requires constant in-range bounds with a "
                "direction compatible with the selected array",
                hir_source_span(expression->vhdl->source));
            return std::nullopt;
        }
        const auto source_width = hir_expression_width(
            source.operands.front(), hir_process_scope_);
        const auto selected_subtype = source.index
                && expression->vhdl != nullptr
            ? hir_vhdl_expression_subtype(expression_id)
            : std::nullopt;
        const auto selected_width = selected_subtype
            ? vhdl_subtype_width(*selected_subtype)
            : std::nullopt;
        const auto result_width = constant_selection
            ? std::optional { constant_selection->width }
            : source.index
            ? selected_width.value_or(1U)
            : dynamic_part_width
            ? dynamic_part_width
            : vhdl_dynamic_slice && expected_width != 0U
            ? std::optional { expected_width }
            : std::nullopt;
        if (!source_width && source.index
            && expression->vhdl != nullptr) {
            // A context-dependent intrinsic prefix can fail before an indexed
            // expression has a concrete layout. Give the intrinsic boundary
            // one opportunity to issue its precise profile diagnostic rather
            // than collapsing the failure into the generic HIR fallback.
            if (const auto attempt = lower_hir_vhdl_vital_expression(
                    source.operands.front(), 0U);
                attempt.handled) {
                return std::nullopt;
            }
        }
        if (!source_width || *source_width == 0U
            || !result_width || *result_width == 0U
            || (constant_selection
                && constant_selection->offset
                    > std::numeric_limits<std::uint32_t>::max())
            || *result_width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto input = lower_hir_expression(
            source.operands.front(), *source_width);
        if (!input) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            *result_width, register_domain(*input));
        if (constant_selection) {
            process_.operations.emplace_back(Extract {
                destination,
                *input,
                static_cast<std::uint32_t>(constant_selection->offset),
                static_cast<std::uint32_t>(constant_selection->width),
            });
        } else if (vhdl_dynamic_slice) {
            const auto dynamic = lower_hir_vhdl_dynamic_slice(
                expression_id, *source_width, *result_width);
            if (!dynamic) {
                return std::nullopt;
            }
            process_.operations.emplace_back(DynamicPartSelect {
                destination,
                *input,
                dynamic->base,
                dynamic->left,
                dynamic->right,
                dynamic->width,
                dynamic->increasing,
                dynamic->source_descending,
                is_two_state_domain(register_domain(*input)),
                dynamic->base_offset,
            });
        } else {
            if (source.index && *result_width > 1U) {
                if (*source_width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    return std::nullopt;
                }
                const auto offset
                    = lower_hir_vhdl_dynamic_element_offset(
                        source.operands[0], source.operands[1],
                        *result_width);
                if (!offset) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(DynamicPartSelect {
                    destination,
                    *input,
                    *offset,
                    static_cast<std::int64_t>(*source_width - 1U),
                    0,
                    static_cast<std::uint32_t>(*result_width),
                    true,
                    true,
                    is_two_state_domain(register_domain(*input)),
                    0U,
                });
            } else {
                const auto dynamic = lower_hir_dynamic_index(
                    source.operands[0], source.operands[1], *source_width);
                if (!dynamic) {
                    return std::nullopt;
                }
                if (source.index) {
                    process_.operations.emplace_back(DynamicExtract {
                        destination, *input, *dynamic });
                } else {
                    process_.operations.emplace_back(DynamicPartSelect {
                        destination,
                        *input,
                        dynamic->index,
                        dynamic->left,
                        dynamic->right,
                        static_cast<std::uint32_t>(*result_width),
                        source.text == "+:",
                        dynamic->left >= dynamic->right,
                        is_two_state_domain(register_domain(*input)),
                        dynamic->base_offset,
                    });
                }
            }
        }
        result = destination;
    } else if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::aggregate
        && !expression->vhdl->associations.empty()) {
        result = lower_hir_vhdl_aggregate(
            expression_id, expected_width);
    } else if (source.concatenation && source.operands.empty()) {
        report(
            "FSIM-ELAB-069",
            "an empty concatenation has no executable width",
            hir_source_span(source.source));
        return std::nullopt;
    } else if (source.concatenation) {
        const auto zero_width_replication =
            [&](const semantic::ExpressionId operand_id) {
                const auto operand
                    = specialized_hir_unit_->find_expression(operand_id);
                if (!operand || operand->systemverilog == nullptr
                    || operand->systemverilog->kind
                        != semantic::sv::ExpressionKind::replication
                    || operand->systemverilog->operands.size() < 2U) {
                    return false;
                }
                const auto count = hir_constant_integer(
                    operand->systemverilog->operands.front());
                return count && *count == 0;
            };
        std::vector<RegisterId> operands;
        operands.reserve(source.operands.size());
        std::size_t width { };
        for (const auto operand_id : source.operands) {
            if (zero_width_replication(operand_id)) {
                continue;
            }
            const auto operand_width = hir_expression_width(
                operand_id, hir_process_scope_);
            if (!operand_width || *operand_width == 0U
                || *operand_width
                    > std::numeric_limits<std::size_t>::max() - width) {
                return std::nullopt;
            }
            const auto operand = lower_hir_expression(
                operand_id, *operand_width);
            if (!operand) {
                return std::nullopt;
            }
            width += register_width(*operand);
            operands.push_back(*operand);
        }
        const auto domain = hir_expression_domain(
            expression_id, hir_process_scope_);
        if (!domain || width == 0U
            || width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto destination = allocate_register(width, *domain);
        process_.operations.emplace_back(Concatenate {
            destination,
            std::move(operands),
            static_cast<std::uint32_t>(width),
        });
        result = destination;
    } else if (expression->vhdl != nullptr && source.binary
        && source.text == "&" && source.operands.size() == 2U) {
        const auto left_width = hir_expression_width(
            source.operands[0], hir_process_scope_);
        const auto right_width = hir_expression_width(
            source.operands[1], hir_process_scope_);
        if (!left_width || !right_width || *left_width == 0U
            || *right_width == 0U) {
            return std::nullopt;
        }
        auto left = lower_hir_expression(
            source.operands[0], *left_width);
        auto right = lower_hir_expression(
            source.operands[1], *right_width);
        const auto domain = hir_expression_domain(
            expression_id, hir_process_scope_);
        if (!left || !right || !domain) {
            return std::nullopt;
        }
        const auto left_runtime_width = register_width(*left);
        const auto right_runtime_width = register_width(*right);
        if (left_runtime_width
            > std::numeric_limits<std::size_t>::max()
                - right_runtime_width) {
            return std::nullopt;
        }
        const auto width = left_runtime_width + right_runtime_width;
        if (width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (register_domain(*left) != *domain) {
            const auto converted = allocate_register(
                left_runtime_width, *domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *left });
            left = converted;
        }
        if (register_domain(*right) != *domain) {
            const auto converted = allocate_register(
                right_runtime_width, *domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *right });
            right = converted;
        }
        const auto destination = allocate_register(width, *domain);
        process_.operations.emplace_back(Concatenate {
            destination,
            { *left, *right },
            static_cast<std::uint32_t>(width),
        });
        result = destination;
    } else if (source.replication && source.operands.size() >= 2U) {
        const auto count = hir_constant_integer(source.operands.front());
        const auto result_width = hir_expression_width(
            expression_id, hir_process_scope_);
        const auto domain = hir_expression_domain(
            expression_id, hir_process_scope_);
        if (!count || *count <= 0 || !result_width || !domain
            || *result_width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        std::vector<RegisterId> group_operands;
        std::size_t group_width { };
        for (std::size_t index = 1U;
            index < source.operands.size(); ++index) {
            const auto operand_width = hir_expression_width(
                source.operands[index], hir_process_scope_);
            if (!operand_width || *operand_width == 0U) {
                return std::nullopt;
            }
            const auto operand = lower_hir_expression(
                source.operands[index], *operand_width);
            if (!operand) {
                return std::nullopt;
            }
            group_width += register_width(*operand);
            group_operands.push_back(*operand);
        }
        auto block = group_operands.front();
        if (group_operands.size() != 1U) {
            block = allocate_register(group_width, *domain);
            process_.operations.emplace_back(Concatenate {
                block,
                std::move(group_operands),
                static_cast<std::uint32_t>(group_width),
            });
        }
        auto block_width = group_width;
        auto remaining = static_cast<std::uint64_t>(*count);
        std::optional<RegisterId> replicated;
        std::size_t replicated_width { };
        while (remaining != 0U) {
            if ((remaining & 1U) != 0U) {
                if (!replicated) {
                    replicated = block;
                    replicated_width = block_width;
                } else {
                    const auto width = replicated_width + block_width;
                    const auto combined = allocate_register(width, *domain);
                    process_.operations.emplace_back(Concatenate {
                        combined,
                        { *replicated, block },
                        static_cast<std::uint32_t>(width),
                    });
                    replicated = combined;
                    replicated_width = width;
                }
            }
            remaining >>= 1U;
            if (remaining != 0U) {
                const auto width = block_width * 2U;
                const auto doubled = allocate_register(width, *domain);
                process_.operations.emplace_back(Concatenate {
                    doubled,
                    { block, block },
                    static_cast<std::uint32_t>(width),
                });
                block = doubled;
                block_width = width;
            }
        }
        if (!replicated || replicated_width != *result_width) {
            return std::nullopt;
        }
        result = *replicated;
    } else if (source.conditional && source.operands.size() == 3U) {
        const auto condition_domain = hir_expression_domain(
            source.operands[0], hir_process_scope_);
        const auto condition_width = hir_expression_width(
            source.operands[0], hir_process_scope_);
        const auto vhdl_2019_logic_condition
            = vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
            && condition_width == std::optional<std::size_t> { 1U }
            && condition_domain
            && (*condition_domain == frontend::ValueDomain::Bit2
                || *condition_domain == frontend::ValueDomain::Logic4
                || *condition_domain == frontend::ValueDomain::Logic9);
        if (expression->vhdl != nullptr
            && (!condition_domain
                || *condition_domain
                    != frontend::ValueDomain::Boolean)
            && !vhdl_2019_logic_condition) {
            report(
                "FSIM-ELAB-VHCOND-003",
                "a VHDL conditional-expression condition must have type "
                "boolean",
                hir_source_span(source.source));
            return std::nullopt;
        }
        std::optional<RegisterId> condition;
        if (expression->systemverilog != nullptr && !condition_width) {
            // A generated SV body can retain a genvar reference in the HIR
            // expression while its specialization has already made the
            // condition an elaboration-time constant. Such a reference has
            // no runtime width/domain profile, but its truth value is still
            // well-defined and can be represented by a one-bit condition.
            if (const auto constant = hir_constant_integer(
                    source.operands[0])) {
                const auto normalized = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    normalized,
                    PackedLogic4(
                        1U,
                        *constant != 0 ? Logic4::one : Logic4::zero),
                });
                condition = normalized;
            }
        } else if (condition_width) {
            condition = lower_hir_expression(
                source.operands[0], *condition_width);
        }
        const auto intrinsic_width = hir_expression_width(
            expression_id, hir_process_scope_);
        const auto value_width = expected_width != 0U
            ? std::optional { expected_width }
            : intrinsic_width;
        auto domain = hir_expression_domain(
            expression_id, hir_process_scope_);
        const auto trace_generated_conditional =
            [&](const std::string_view reason) {
                if (std::getenv("FSIM_HIR_TRACE_GENERATED_ASSIGNMENT")
                    == nullptr) {
                    return;
                }
                const auto domain_value = [](const auto candidate) {
                    return candidate
                        ? static_cast<int>(*candidate)
                        : -1;
                };
                const auto width_value = [](const auto candidate) {
                    return candidate
                        ? static_cast<long long>(*candidate)
                        : -1LL;
                };
                std::cerr << "[fsim-hir-lower] generated-conditional"
                          << " expression=" << expression_id.value()
                          << " reason=" << reason
                          << " condition=" << source.operands[0].value()
                          << " true=" << source.operands[1].value()
                          << " false=" << source.operands[2].value()
                          << " expected_width=" << expected_width
                          << " condition_width="
                          << width_value(condition_width)
                          << " intrinsic_width="
                          << width_value(intrinsic_width)
                          << " value_width=" << width_value(value_width)
                          << " domain=" << domain_value(domain)
                          << " condition_domain="
                          << domain_value(condition_domain) << '\n';
            };
        if (!condition || !value_width) {
            trace_generated_conditional(
                !condition ? "condition-value" : "value-width");
            return std::nullopt;
        }
        const auto constant_condition = expression->systemverilog != nullptr
            ? hir_constant_integer(source.operands[0])
            : std::nullopt;
        if (constant_condition) {
            // A specialization-known SystemVerilog condition selects one
            // executable arm. Do not lower the unreachable arm: generated
            // code commonly retains a syntactically valid but out-of-range
            // part-select there. Runtime/unknown conditions continue through
            // the full three-way lowering below.
            const auto selected = *constant_condition != 0
                ? source.operands[1]
                : source.operands[2];
            auto lowered = lower_hir_expression(selected, *value_width);
            if (!lowered) {
                trace_generated_conditional("sv-constant-value");
                return std::nullopt;
            }
            if (register_width(*lowered) != *value_width) {
                lowered = resize_register(
                    *lowered, *value_width,
                    hir_expression_signed(selected));
            }
            if (!domain) {
                domain = hir_expression_domain(selected, hir_process_scope_);
            }
            if (!domain) {
                trace_generated_conditional("sv-constant-domain");
                return std::nullopt;
            }
            if (register_domain(*lowered) != *domain) {
                const auto converted = allocate_register(
                    *value_width, *domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *lowered });
                lowered = converted;
            }
            return lowered;
        } else if (vhdl_2019_logic_condition) {
            const auto one = allocate_register(
                1U, register_domain(*condition));
            process_.operations.emplace_back(LoadConstant {
                one, PackedLogic4(1U, Logic4::one) });
            const auto normalized = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                register_domain(*condition)
                        == frontend::ValueDomain::Logic9
                    ? BinaryOperator::vhdl_match_equal
                    : BinaryOperator::case_equal,
                normalized,
                *condition,
                one,
            });
            condition = normalized;
        }
        if (register_width(*condition) != 1U) {
            const auto inverted = allocate_register(
                1U, register_domain(*condition));
            process_.operations.emplace_back(
                LogicalNot { inverted, *condition });
            const auto normalized = allocate_register(
                1U, register_domain(*condition));
            process_.operations.emplace_back(
                LogicalNot { normalized, inverted });
            condition = normalized;
        }
        if (expression->vhdl != nullptr) {
            const auto when_true_domain = hir_expression_domain(
                source.operands[1], hir_process_scope_);
            const auto when_false_domain = hir_expression_domain(
                source.operands[2], hir_process_scope_);
            if (when_true_domain && when_false_domain
                && *when_true_domain != *when_false_domain) {
                report(
                    "FSIM-ELAB-VHCOND-002",
                    "VHDL conditional-expression alternatives must have "
                    "compatible value domains",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (!domain) {
                // A zero/one-only VHDL string or bit-string literal is
                // contextually typed. When the other alternative has an
                // intrinsic domain, select it before lowering either branch
                // so the literal is converted into that domain instead of
                // fixing the result to its temporary two-state register.
                domain = when_true_domain
                    ? when_true_domain
                    : when_false_domain;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *condition, 0U, 0U, UnknownBranchPolicy::error });

            const auto when_true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_true = lower_hir_expression(
                source.operands[1], *value_width);
            if (!when_true) {
                trace_generated_conditional("vhdl-true-value");
                return std::nullopt;
            }
            const bool inferred_domain = !domain;
            if (!domain) {
                domain = register_domain(*when_true);
            }
            if (register_width(*when_true) != *value_width) {
                when_true = resize_register(
                    *when_true, *value_width,
                    hir_expression_signed(source.operands[1]));
            }
            if (register_domain(*when_true) != *domain) {
                const auto converted = allocate_register(
                    *value_width, *domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *when_true });
                when_true = converted;
            }
            const auto destination = allocate_register(
                *value_width, *domain);
            process_.operations.emplace_back(CopyRegister {
                destination, *when_true });
            const auto finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });

            const auto when_false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_false = lower_hir_expression(
                source.operands[2], *value_width);
            if (!when_false
                || (inferred_domain
                    && register_domain(*when_false) != *domain)) {
                if (when_false && inferred_domain) {
                    report(
                        "FSIM-ELAB-VHCOND-002",
                        "VHDL conditional-expression alternatives must "
                        "have compatible value domains",
                        hir_source_span(source.source));
                }
                trace_generated_conditional("vhdl-false-value");
                return std::nullopt;
            }
            if (register_width(*when_false) != *value_width) {
                when_false = resize_register(
                    *when_false, *value_width,
                    hir_expression_signed(source.operands[2]));
            }
            if (register_domain(*when_false) != *domain) {
                const auto converted = allocate_register(
                    *value_width, *domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *when_false });
                when_false = converted;
            }
            process_.operations.emplace_back(CopyRegister {
                destination, *when_false });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *condition,
                when_true_start,
                when_false_start,
                UnknownBranchPolicy::error,
            };
            process_.operations[finish] = Jump { end };
            result = destination;
        } else {
            const auto true_domain = hir_expression_domain(
                source.operands[1], hir_process_scope_);
            const auto false_domain = hir_expression_domain(
                source.operands[2], hir_process_scope_);
            if (!domain && true_domain && false_domain) {
                if (*true_domain == *false_domain) {
                    domain = true_domain;
                } else {
                    const auto packed_integral = [](const auto candidate) {
                        return candidate == frontend::ValueDomain::Bit2
                            || candidate == frontend::ValueDomain::Logic4
                            || candidate == frontend::ValueDomain::Integer;
                    };
                    if (packed_integral(*true_domain)
                        && packed_integral(*false_domain)) {
                        // A context-determined SystemVerilog conditional
                        // combines integral alternatives even when one is an
                        // unsized integer constant. Preserve four-state
                        // behavior whenever either alternative requires it.
                        domain = *true_domain
                                    == frontend::ValueDomain::Logic4
                                || *false_domain
                                    == frontend::ValueDomain::Logic4
                            ? frontend::ValueDomain::Logic4
                            : frontend::ValueDomain::Bit2;
                    }
                }
            }
            if (!domain) {
                trace_generated_conditional("sv-domain");
                return std::nullopt;
            }
            const auto destination = allocate_register(
                *value_width, *domain);
            const auto lower_alternative = [&](
                                               const semantic::ExpressionId id)
                -> std::optional<RegisterId> {
                auto value = lower_hir_expression(id, *value_width);
                if (!value) {
                    return std::nullopt;
                }
                if (register_width(*value) != *value_width) {
                    value = resize_register(
                        *value, *value_width, hir_expression_signed(id));
                }
                if (register_domain(*value) != *domain) {
                    const auto converted = allocate_register(
                        *value_width, *domain);
                    process_.operations.emplace_back(CopyRegister {
                        converted, *value });
                    value = converted;
                }
                return value;
            };
            const auto lowered_condition_domain
                = register_domain(*condition);
            const auto one = allocate_register(
                1U, lowered_condition_domain);
            const auto zero = allocate_register(
                1U, lowered_condition_domain);
            process_.operations.emplace_back(LoadConstant {
                one, PackedLogic4(1U, Logic4::one) });
            process_.operations.emplace_back(LoadConstant {
                zero, PackedLogic4(1U, Logic4::zero) });
            const auto is_true = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            const auto is_false = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal, is_true, *condition, one });
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal, is_false, *condition, zero });

            const auto select_true = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                is_true, 0U, 0U, UnknownBranchPolicy::when_false });
            const auto true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto when_true = lower_alternative(source.operands[1]);
            if (!when_true) {
                trace_generated_conditional("sv-true-value");
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyRegister {
                destination, *when_true });
            const auto true_finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });

            const auto select_false = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                is_false, 0U, 0U, UnknownBranchPolicy::when_false });
            const auto false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto when_false = lower_alternative(source.operands[2]);
            if (!when_false) {
                trace_generated_conditional("sv-false-value");
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyRegister {
                destination, *when_false });
            const auto false_finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });

            const auto unknown_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto unknown_true = lower_alternative(source.operands[1]);
            const auto unknown_false = lower_alternative(source.operands[2]);
            if (!unknown_true || !unknown_false) {
                trace_generated_conditional(
                    !unknown_true ? "sv-unknown-true-value"
                                  : "sv-unknown-false-value");
                return std::nullopt;
            }
            process_.operations.emplace_back(ConditionalSelect {
                destination, *condition, *unknown_true, *unknown_false,
            });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[select_true] = Branch {
                is_true,
                true_start,
                select_false,
                UnknownBranchPolicy::when_false,
            };
            process_.operations[select_false] = Branch {
                is_false,
                false_start,
                unknown_start,
                UnknownBranchPolicy::when_false,
            };
            process_.operations[true_finish] = Jump { end };
            process_.operations[false_finish] = Jump { end };
            result = destination;
        }
    } else if (source.unary && source.operands.size() == 1U) {
        using ScalarKind = frontend::SystemVerilogScalarKind;
        const auto scalar_kind = hir_systemverilog_scalar_kind(
            expression_id, scalar_context);
        const auto scalar_width = scalar_kind == ScalarKind::ShortReal
            ? 32U
            : scalar_kind != ScalarKind::None ? 64U
                                              : 0U;
        const auto operand_width = scalar_width != 0U
            ? scalar_width
            : expression->vhdl != nullptr && expected_width != 0U
                && (source.text == "+" || source.text == "-")
            ? expected_width
            : hir_expression_width(
                  source.operands[0], hir_process_scope_)
                  .value_or(std::max<std::size_t>(expected_width, 1U));
        if (expression->vhdl != nullptr && source.text == "-"
            && operand_width != 0U && operand_width <= 64U) {
            const auto operand_expression
                = specialized_hir_unit_->find_expression(
                    source.operands.front());
            const auto literal = operand_expression
                    && operand_expression->vhdl != nullptr
                    && operand_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::integer_literal
                ? unsigned_decimal(operand_expression->vhdl->text)
                : std::nullopt;
            const auto minimum_magnitude = operand_width == 64U
                ? std::uint64_t { 1 } << 63U
                : std::uint64_t { 1 } << (operand_width - 1U);
            if (literal && *literal == minimum_magnitude) {
                const auto destination = allocate_register(
                    operand_width, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(minimum_magnitude, operand_width),
                });
                return destination;
            }
        }
        auto operand = lower_hir_expression(
            source.operands[0], operand_width, scalar_kind);
        if (operand) {
            if (expression->vhdl != nullptr && source.text == "??") {
                if (register_width(*operand) != 1U) {
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                const auto one = allocate_register(
                    1U, register_domain(*operand));
                process_.operations.emplace_back(LoadConstant {
                    one, PackedLogic4(1U, Logic4::one) });
                process_.operations.emplace_back(Binary {
                    register_domain(*operand)
                            == frontend::ValueDomain::Logic9
                        ? BinaryOperator::vhdl_match_equal
                        : BinaryOperator::case_equal,
                    destination,
                    *operand,
                    one,
                });
                result = destination;
            } else if (source.text == "+") {
                result = *operand;
            } else if (source.text == "-") {
                const auto real_scalar
                    = scalar_kind == ScalarKind::ShortReal
                    || scalar_kind == ScalarKind::Real
                    || scalar_kind == ScalarKind::Realtime;
                if (real_scalar) {
                    const auto sign_mask = allocate_register(
                        scalar_width,
                        frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        sign_mask,
                        PackedLogic4::from_aval_bval(
                            scalar_width,
                            std::uint64_t { 1 }
                                << (scalar_width - 1U),
                            0U),
                    });
                    const auto destination = allocate_register(
                        scalar_width, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::bit_xor,
                        destination,
                        *operand,
                        sign_mask,
                    });
                    result = destination;
                } else if (register_domain(*operand)
                    == frontend::ValueDomain::Integer) {
                    const auto destination = allocate_register(
                        register_width(*operand),
                        frontend::ValueDomain::Integer);
                    process_.operations.emplace_back(IntegerUnary {
                        IntegerUnaryOperator::negate,
                        destination,
                        *operand,
                    });
                    result = destination;
                } else {
                    const auto zero = allocate_register(
                        register_width(*operand),
                        register_domain(*operand));
                    process_.operations.emplace_back(LoadConstant {
                        zero,
                        PackedLogic4(
                            register_width(*operand), Logic4::zero),
                    });
                    const auto destination = allocate_register(
                        register_width(*operand),
                        register_domain(*operand));
                    process_.operations.emplace_back(Binary {
                        hir_expression_signed(source.operands.front())
                            ? BinaryOperator::subtract_signed
                            : BinaryOperator::subtract_unsigned,
                        destination,
                        zero,
                        *operand,
                    });
                    result = destination;
                }
            } else if (source.text == "abs") {
                if (language_ != frontend::Language::Vhdl2008
                    || (register_domain(*operand)
                            != frontend::ValueDomain::Integer
                        && !hir_expression_signed(
                            source.operands.front()))) {
                    report(
                        "FSIM-ELAB-082",
                        "the bounded packed abs operator requires a signed "
                        "VHDL operand",
                        hir_source_span(source.source));
                    return std::nullopt;
                }
                if (register_domain(*operand)
                    == frontend::ValueDomain::Integer) {
                    const auto destination = allocate_register(
                        register_width(*operand),
                        frontend::ValueDomain::Integer);
                    process_.operations.emplace_back(IntegerUnary {
                        IntegerUnaryOperator::absolute,
                        destination,
                        *operand,
                    });
                    result = destination;
                } else {
                    const auto zero = allocate_register(
                        register_width(*operand),
                        register_domain(*operand));
                    process_.operations.emplace_back(LoadConstant {
                        zero,
                        PackedLogic4(
                            register_width(*operand), Logic4::zero),
                    });
                    const auto negated = allocate_register(
                        register_width(*operand),
                        register_domain(*operand));
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::subtract_signed,
                        negated,
                        zero,
                        *operand,
                    });
                    const auto sign = allocate_register(
                        1U, register_domain(*operand));
                    process_.operations.emplace_back(Extract {
                        sign,
                        *operand,
                        static_cast<std::uint32_t>(
                            register_width(*operand) - 1U),
                        1U,
                    });
                    const auto destination = allocate_register(
                        register_width(*operand),
                        register_domain(*operand));
                    process_.operations.emplace_back(ConditionalSelect {
                        destination,
                        sign,
                        negated,
                        *operand,
                    });
                    result = destination;
                }
            } else if (source.text == "!" || source.text == "not") {
                const auto destination = allocate_register(
                    source.text == "!" ? 1U : register_width(*operand),
                    source.text == "!"
                        ? frontend::ValueDomain::Logic4
                        : register_domain(*operand));
                if (source.text == "!") {
                    process_.operations.emplace_back(
                        LogicalNot { destination, *operand });
                } else {
                    process_.operations.emplace_back(
                        UnaryNot { destination, *operand });
                }
                result = destination;
            } else if (source.text == "~") {
                const auto destination = allocate_register(
                    register_width(*operand), register_domain(*operand));
                process_.operations.emplace_back(
                    UnaryNot { destination, *operand });
                result = destination;
            } else if (hir_reduction_operator(source.text)) {
                const auto source_domain = register_domain(*operand);
                const auto result_domain
                    = source_domain == frontend::ValueDomain::Bit2
                        || source_domain
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
                auto operation = ReductionOperator::bit_xor;
                if (source.text == "&" || source.text == "~&"
                    || source.text == "and"
                    || source.text == "nand") {
                    operation = ReductionOperator::bit_and;
                } else if (source.text == "|"
                    || source.text == "~|" || source.text == "or"
                    || source.text == "nor") {
                    operation = ReductionOperator::bit_or;
                }
                const auto destination = allocate_register(
                    1U, result_domain);
                process_.operations.emplace_back(Reduction {
                    operation, destination, *operand });
                const auto inverted = source.text.starts_with("~")
                    || source.text == "^~" || source.text == "nand"
                    || source.text == "nor" || source.text == "xnor";
                if (inverted) {
                    const auto complement = allocate_register(
                        1U, result_domain);
                    process_.operations.emplace_back(UnaryNot {
                        complement, destination });
                    result = complement;
                } else {
                    result = destination;
                }
            }
        }
    } else if (source.binary && source.operands.size() == 2U) {
        if (language_ != frontend::Language::Vhdl2008
            && (source.text == "&&" || source.text == "||")) {
            const auto lhs_width = hir_expression_width(
                source.operands[0], hir_process_scope_)
                                       .value_or(1U);
            auto lhs = lower_hir_expression(
                source.operands[0], lhs_width);
            if (!lhs) {
                return std::nullopt;
            }
            if (register_width(*lhs) != 1U) {
                const auto inverted = allocate_register(
                    1U, register_domain(*lhs));
                process_.operations.emplace_back(
                    LogicalNot { inverted, *lhs });
                const auto normalized = allocate_register(
                    1U, register_domain(*lhs));
                process_.operations.emplace_back(
                    LogicalNot { normalized, inverted });
                lhs = normalized;
            }
            const auto decisive_value = source.text == "||"
                ? Logic4::one
                : Logic4::zero;
            const auto decisive = allocate_register(
                1U, register_domain(*lhs));
            process_.operations.emplace_back(LoadConstant {
                decisive, PackedLogic4(1U, decisive_value) });
            const auto is_decisive = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                is_decisive,
                *lhs,
                decisive,
            });
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                is_decisive, 0U, 0U, UnknownBranchPolicy::when_false });

            const auto decisive_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LoadConstant {
                destination, PackedLogic4(1U, decisive_value) });
            const auto finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });

            const auto evaluate_rhs = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto rhs_width = hir_expression_width(
                source.operands[1], hir_process_scope_)
                                       .value_or(1U);
            const auto rhs = lower_hir_expression(
                source.operands[1], rhs_width);
            if (!rhs) {
                return std::nullopt;
            }
            process_.operations.emplace_back(LogicalBinary {
                source.text == "&&"
                    ? LogicalBinaryOperator::logical_and
                    : LogicalBinaryOperator::logical_or,
                destination,
                *lhs,
                *rhs,
            });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                is_decisive,
                decisive_start,
                evaluate_rhs,
                UnknownBranchPolicy::when_false,
            };
            process_.operations[finish] = Jump { end };
            result = destination;
        } else {
        const auto vhdl_enumeration_root = [&](
                                               const semantic::ExpressionId operand)
            -> std::optional<semantic::TypeId> {
            if (language_ != frontend::Language::Vhdl2008) {
                return std::nullopt;
            }
            const auto subtype = hir_vhdl_expression_subtype(operand);
            if (!subtype || !subtype->type_mark.target.valid()) {
                return std::nullopt;
            }
            auto type_id = subtype->type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()
                && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::enumeration) {
                    return type_id;
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            return std::nullopt;
        };
        const auto lhs_enumeration = vhdl_enumeration_root(
            source.operands[0]);
        const auto rhs_enumeration = vhdl_enumeration_root(
            source.operands[1]);
        if ((lhs_enumeration || rhs_enumeration)
            && !comparison_operator(source.text)) {
            report(
                "FSIM-ELAB-VHENUM-003",
                "operator '" + std::string { source.text }
                    + "' is not defined for VHDL enumeration values",
                hir_source_span(source.source));
            return std::nullopt;
        }
        if (lhs_enumeration && rhs_enumeration
            && *lhs_enumeration != *rhs_enumeration) {
            report(
                "FSIM-ELAB-VHENUM-002",
                "VHDL enumeration comparison requires operands of the "
                "same nominal type",
                hir_source_span(source.source));
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008
            && comparison_operator(source.text)) {
            const auto lhs_composite = hir_vhdl_composite_root_type(
                source.operands[0]);
            const auto rhs_composite = hir_vhdl_composite_root_type(
                source.operands[1]);
            if (source.text.starts_with('?')
                && (lhs_composite || rhs_composite)) {
                report(
                    "FSIM-ELAB-VHDLMATCH-004",
                    "VHDL matching equality does not accept composite "
                    "operands",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (source.text.starts_with('?')) {
                const auto matching_domain = [](const auto domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Logic9;
                };
                const auto lhs_domain = hir_expression_domain(
                    source.operands[0], hir_process_scope_);
                const auto rhs_domain = hir_expression_domain(
                    source.operands[1], hir_process_scope_);
                if (!lhs_domain || !rhs_domain
                    || !matching_domain(*lhs_domain)
                    || !matching_domain(*rhs_domain)) {
                    report(
                        "FSIM-ELAB-VHDLMATCH-004",
                        "VHDL matching equality requires bit or std_ulogic "
                        "scalar or array operands",
                        hir_source_span(source.source));
                    return std::nullopt;
                }
            }
            const auto array_relational
                = source.text == "<" || source.text == "<="
                || source.text == ">" || source.text == ">=";
            const auto array_operand = [](const auto& composite) {
                return composite
                    && composite->first
                        == semantic::vhdl::TypeForm::array;
            };
            if (array_relational
                && (array_operand(lhs_composite)
                    || array_operand(rhs_composite))) {
                report(
                    "FSIM-ELAB-VHARRAY-007",
                    "only equality and inequality are supported for VHDL "
                    "array values in the bounded runtime",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (lhs_composite && rhs_composite
                && lhs_composite->first == rhs_composite->first
                && lhs_composite->second != rhs_composite->second) {
                const auto record = lhs_composite->first
                    == semantic::vhdl::TypeForm::record;
                report(
                    record ? "FSIM-ELAB-VHCOMPOP-001"
                           : "FSIM-ELAB-VHARRAY-006",
                    record
                        ? "VHDL record comparison requires operands of the "
                          "same nominal type"
                        : "VHDL array comparison requires compatible "
                          "nominal array types",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if ((source.text == "=" || source.text == "/=")
                && (array_operand(lhs_composite)
                    || array_operand(rhs_composite))) {
                const auto lhs_width
                    = hir_vhdl_expression_runtime_width(
                        source.operands[0], hir_process_scope_);
                const auto rhs_width
                    = hir_vhdl_expression_runtime_width(
                        source.operands[1], hir_process_scope_);
                if (lhs_width && rhs_width
                    && (*lhs_width == 0U || *rhs_width == 0U)) {
                    auto equal = *lhs_width == *rhs_width;
                    if (source.text == "/=") {
                        equal = !equal;
                    }
                    const auto destination = allocate_register(
                        1U, frontend::ValueDomain::Boolean);
                    process_.operations.emplace_back(LoadConstant {
                        destination,
                        unsigned_value(equal ? 1U : 0U, 1U),
                    });
                    return destination;
                }
            }
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const auto arithmetic = source.text == "+"
                || source.text == "-" || source.text == "*"
                || source.text == "/" || source.text == "rem"
                || source.text == "mod" || source.text == "**";
            const auto relational = source.text == "<"
                || source.text == "<=" || source.text == ">"
                || source.text == ">=";
            const auto equality = source.text == "="
                || source.text == "/=";
            const auto std_logic_vector_operand = [&](
                                                        const semantic::ExpressionId operand) {
                const auto subtype = hir_vhdl_expression_subtype(operand);
                if (!subtype) {
                    return false;
                }
                auto name = std::string_view {
                    subtype->type_mark.spelling
                };
                if (const auto separator = name.find_last_of('.');
                    separator != std::string_view::npos) {
                    name.remove_prefix(separator + 1U);
                }
                return std::ranges::equal(
                    name, std::string_view { "std_logic_vector" },
                    [](const char left, const char right) {
                        return std::tolower(
                                   static_cast<unsigned char>(left))
                            == std::tolower(
                                static_cast<unsigned char>(right));
                    });
            };
            const auto numeric_context
                = hir_vhdl_synopsys_numeric_context(
                    expression->vhdl->scope);
            if ((arithmetic || relational || equality)
                && numeric_context.signed_visible
                && numeric_context.unsigned_visible
                && (std_logic_vector_operand(source.operands[0])
                    || std_logic_vector_operand(source.operands[1]))) {
                report(
                    "FSIM-ELAB-VHSYN-001",
                    "std_logic_signed and std_logic_unsigned expose "
                    "conflicting std_logic_vector overloads; remove one "
                    "use clause or add an explicit numeric conversion",
                    hir_source_span(source.source));
                return invalid_integral_result();
            }
            const auto lhs_domain = hir_expression_domain(
                source.operands[0], hir_process_scope_);
            const auto rhs_domain = hir_expression_domain(
                source.operands[1], hir_process_scope_);
            const auto contextual_integer
                = (lhs_domain
                       && *lhs_domain
                           == frontend::ValueDomain::Integer)
                || (rhs_domain
                    && *rhs_domain
                        == frontend::ValueDomain::Integer);
            const auto real_operand = [&](const semantic::ExpressionId id) {
                const auto candidate
                    = specialized_hir_unit_->find_expression(id);
                if (candidate && candidate->vhdl != nullptr
                    && candidate->vhdl->kind
                        == semantic::vhdl::ExpressionKind::real_literal) {
                    return true;
                }
                const auto subtype = hir_vhdl_expression_subtype(id);
                const auto is_real = [&](const auto& value) {
                    auto spelling = std::string_view {
                        value.type_mark.spelling
                    };
                    if (const auto separator = spelling.find_last_of(".:");
                        separator != std::string_view::npos) {
                        spelling.remove_prefix(separator + 1U);
                    }
                    return same_vhdl_identifier(spelling, "real");
                };
                if (subtype && is_real(*subtype)) {
                    return true;
                }
                const auto effective = subtype
                    ? hir_effective_vhdl_subtype(*subtype)
                    : std::nullopt;
                return effective && is_real(*effective);
            };
            if ((arithmetic || relational)
                && hir_expression_signed(source.operands[0])
                    != hir_expression_signed(source.operands[1])
                && !contextual_integer
                && !real_operand(source.operands[0])
                && !real_operand(source.operands[1])) {
                report(
                    relational ? "FSIM-ELAB-066"
                               : "FSIM-ELAB-067",
                    "mixed signed/unsigned VHDL operands require an "
                    "explicit conversion",
                    hir_source_span(source.source));
                return std::nullopt;
            }
        }
        const auto lhs_boolean_domain = hir_expression_domain(
            source.operands[0], hir_process_scope_);
        const auto rhs_boolean_domain = hir_expression_domain(
            source.operands[1], hir_process_scope_);
        const auto vhdl_short_circuit
            = language_ == frontend::Language::Vhdl2008
            && (source.text == "and" || source.text == "or")
            && lhs_boolean_domain
            && *lhs_boolean_domain == frontend::ValueDomain::Boolean
            && rhs_boolean_domain
            && *rhs_boolean_domain == frontend::ValueDomain::Boolean;
        if (vhdl_short_circuit) {
            const auto lhs = lower_hir_expression(
                source.operands[0], 1U);
            if (!lhs || register_width(*lhs) != 1U) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *lhs, 0U, 0U, UnknownBranchPolicy::error });
            const auto short_path = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(CopyRegister {
                destination, *lhs });
            const auto finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });
            const auto rhs_path = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto rhs = lower_hir_expression(
                source.operands[1], 1U);
            if (!rhs || register_width(*rhs) != 1U) {
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyRegister {
                destination, *rhs });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *lhs,
                source.text == "and" ? rhs_path : short_path,
                source.text == "and" ? short_path : rhs_path,
                UnknownBranchPolicy::error,
            };
            process_.operations[finish] = Jump { end };
            return destination;
        }
        const auto lhs_width = hir_expression_width(
            source.operands[0], hir_process_scope_)
                                   .value_or(std::max<std::size_t>(expected_width, 1U));
        const auto rhs_width = hir_expression_width(
            source.operands[1], hir_process_scope_)
                                   .value_or(std::max<std::size_t>(expected_width, 1U));
        using ScalarKind = frontend::SystemVerilogScalarKind;
        using ScalarOperator
            = runtime::SystemVerilogScalarBinaryOperator;
        const auto scalar_operation = [&]()
            -> std::optional<ScalarOperator> {
            if (source.text == "+") {
                return ScalarOperator::Add;
            }
            if (source.text == "-") {
                return ScalarOperator::Subtract;
            }
            if (source.text == "*") {
                return ScalarOperator::Multiply;
            }
            if (source.text == "/") {
                return ScalarOperator::Divide;
            }
            if (source.text == "==" || source.text == "===") {
                return ScalarOperator::Equal;
            }
            if (source.text == "!=" || source.text == "!==") {
                return ScalarOperator::NotEqual;
            }
            if (source.text == "<") {
                return ScalarOperator::Less;
            }
            if (source.text == "<=") {
                return ScalarOperator::LessEqual;
            }
            if (source.text == ">") {
                return ScalarOperator::Greater;
            }
            if (source.text == ">=") {
                return ScalarOperator::GreaterEqual;
            }
            return std::nullopt;
        }();
        auto lhs_scalar = hir_systemverilog_scalar_kind(
            source.operands[0], scalar_context);
        auto rhs_scalar = hir_systemverilog_scalar_kind(
            source.operands[1], scalar_context);
        if (lhs_scalar == ScalarKind::None
            && rhs_scalar != ScalarKind::None) {
            lhs_scalar = rhs_scalar;
        }
        if (rhs_scalar == ScalarKind::None
            && lhs_scalar != ScalarKind::None) {
            rhs_scalar = lhs_scalar;
        }
        if (!scalar_operation
            && (lhs_scalar != ScalarKind::None
                || rhs_scalar != ScalarKind::None
                || scalar_context != ScalarKind::None)) {
            report(
                "FSIM-ELAB-SVSCALAR-002",
                "runtime scalar operator '" + std::string { source.text }
                    + "' is outside the executable arithmetic and "
                      "comparison subset",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto scalar_width = [](const ScalarKind kind) {
            return kind == ScalarKind::ShortReal ? 32U : 64U;
        };
        if (scalar_operation && lhs_scalar != ScalarKind::None
            && rhs_scalar != ScalarKind::None) {
            const auto lhs = lower_hir_expression(
                source.operands[0], scalar_width(lhs_scalar), lhs_scalar);
            const auto rhs = lower_hir_expression(
                source.operands[1], scalar_width(rhs_scalar), rhs_scalar);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            const auto comparison
                = *scalar_operation >= ScalarOperator::Equal;
            const auto result_kind = comparison
                ? ScalarKind::None
                : scalar_context != ScalarKind::None
                ? scalar_context
                : lhs_scalar;
            const auto destination = allocate_register(
                comparison ? 1U : scalar_width(result_kind),
                frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                *scalar_operation,
                destination,
                *lhs,
                *rhs,
                lhs_scalar,
                rhs_scalar,
                result_kind,
            });
            result = destination;
        } else if (shift_operator(source.text)) {
            const auto value_width
                = language_ == frontend::Language::SystemVerilog2017
                ? std::max(lhs_width,
                      std::max<std::size_t>(expected_width, 1U))
                : lhs_width;
            auto value = lower_hir_expression(
                source.operands[0], value_width);
            const auto amount = lower_hir_expression(
                source.operands[1], rhs_width);
            if (!value || !amount) {
                return std::nullopt;
            }
            if (language_ == frontend::Language::SystemVerilog2017
                && register_width(*value) != value_width) {
                value = resize_register(
                    *value, value_width,
                    hir_expression_signed(source.operands.front()));
            }
            if (language_ == frontend::Language::Vhdl2008
                && register_domain(*amount)
                    != frontend::ValueDomain::Integer) {
                report(
                    "FSIM-ELAB-070",
                    "a dynamic VHDL packed shift or rotate count must "
                    "have the base integer subtype",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto value_domain = register_domain(*value);
            const auto domain = value_domain
                    == frontend::ValueDomain::Logic9
                ? frontend::ValueDomain::Logic9
                : is_two_state_domain(value_domain)
                    && is_two_state_domain(register_domain(*amount))
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            const auto destination = allocate_register(
                register_width(*value), domain);
            process_.operations.emplace_back(Shift {
                lowered_shift_operator(
                    source.text,
                    hir_expression_signed(source.operands.front())),
                destination,
                *value,
                *amount,
                language_ == frontend::Language::Vhdl2008,
            });
            result = destination;
        } else {
            const auto scalar_result = comparison_operator(source.text)
                || source.text == "&&" || source.text == "||";
            const auto systemverilog_power
                = language_ != frontend::Language::Vhdl2008
                && source.text == "**";
            if (language_ == frontend::Language::Vhdl2008
                && source.text == "**") {
                auto exponent
                    = specialized_hir_unit_->evaluate_integral_expression(
                        source.operands[1]);
                if (!exponent && active_hir_callable_
                    && *active_hir_callable_ < hir_callable_frames_.size()) {
                    const auto formal = hir_referenced_declaration(
                        source.operands[1]);
                    const auto& bindings = hir_callable_frames_[
                        *active_hir_callable_].static_integer_bindings;
                    const auto binding = formal
                        ? std::ranges::find_if(
                              bindings,
                              [&](const auto& candidate) {
                                  return candidate.first == *formal;
                              })
                        : bindings.end();
                    if (binding != bindings.end()) {
                        // A statically known call actual may bound the
                        // integer-power shape, while value lowering still
                        // reads the formal's invocation register.
                        exponent = binding->second;
                    }
                }
                if (!exponent || *exponent < 0) {
                    const auto exponent_expression
                        = specialized_hir_unit_->find_expression(
                            source.operands[1]);
                    const auto exponent_source
                        = exponent_expression
                            && exponent_expression->vhdl != nullptr
                        ? exponent_expression->vhdl->source
                        : source.source;
                    report("FSIM-ELAB-091",
                        "bounded VHDL integer exponentiation requires "
                        "a locally static nonnegative exponent",
                        hir_source_span(exponent_source));
                    return std::nullopt;
                }
            }
            const auto operand_width = scalar_result
                ? std::max(lhs_width, rhs_width)
                : systemverilog_power
                ? std::max(lhs_width,
                      std::max<std::size_t>(expected_width, 1U))
                : std::max({ lhs_width, rhs_width,
                      std::max<std::size_t>(expected_width, 1U) });
            const auto lower_operand = [&](
                                           const semantic::ExpressionId operand,
                                           const std::size_t width,
                                           const semantic::ExpressionId context) {
                const auto selected
                    = specialized_hir_unit_->find_expression(operand);
                const auto subtype = language_
                        == frontend::Language::Vhdl2008
                    ? hir_vhdl_expression_subtype(context)
                    : std::nullopt;
                if (subtype) {
                    if (const auto status
                        = hir_vhdl_environment_status_literal(
                            operand, *subtype)) {
                        const auto destination = allocate_register(
                            width, frontend::ValueDomain::Bit2);
                        process_.operations.emplace_back(LoadConstant {
                            destination,
                            unsigned_value(*status, width),
                        });
                        return std::optional { destination };
                    }
                }
                return selected && selected->vhdl != nullptr
                        && selected->vhdl->kind
                            == semantic::vhdl::ExpressionKind::aggregate
                        && subtype
                    ? lower_hir_vhdl_aggregate(
                          operand, width, &*subtype)
                    : lower_hir_expression(operand, width);
            };
            auto lhs = lower_operand(
                source.operands[0], operand_width, source.operands[1]);
            auto rhs = lower_operand(
                source.operands[1],
                systemverilog_power ? rhs_width : operand_width,
                source.operands[0]);
            if (lhs && rhs) {
                const auto packed_domain = [](const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Logic4
                        || domain == frontend::ValueDomain::Logic9;
                };
                if (language_ == frontend::Language::Vhdl2008
                    && register_domain(*lhs) != register_domain(*rhs)
                    && packed_domain(register_domain(*lhs))
                    && packed_domain(register_domain(*rhs))) {
                    const auto common_domain
                        = register_domain(*lhs) == frontend::ValueDomain::Logic9
                            || register_domain(*rhs)
                                == frontend::ValueDomain::Logic9
                        ? frontend::ValueDomain::Logic9
                        : register_domain(*lhs)
                                == frontend::ValueDomain::Logic4
                            || register_domain(*rhs)
                                == frontend::ValueDomain::Logic4
                        ? frontend::ValueDomain::Logic4
                        : frontend::ValueDomain::Bit2;
                    if (register_domain(*lhs) != common_domain) {
                        const auto converted = allocate_register(
                            register_width(*lhs), common_domain);
                        process_.operations.emplace_back(CopyRegister {
                            converted, *lhs });
                        lhs = converted;
                    }
                    if (register_domain(*rhs) != common_domain) {
                        const auto converted = allocate_register(
                            register_width(*rhs), common_domain);
                        process_.operations.emplace_back(CopyRegister {
                            converted, *rhs });
                        rhs = converted;
                    }
                }
                if (source.text == "&&" || source.text == "||") {
                    const auto destination = allocate_register(
                        1U, frontend::ValueDomain::Logic4);
                    process_.operations.emplace_back(LogicalBinary {
                        source.text == "&&"
                            ? LogicalBinaryOperator::logical_and
                            : LogicalBinaryOperator::logical_or,
                        destination,
                        *lhs,
                        *rhs,
                    });
                    result = destination;
                } else {
                    const auto signed_operands
                        = hir_expression_signed(source.operands[0])
                        && hir_expression_signed(source.operands[1]);
                    auto integer_operation
                        = std::optional<IntegerBinaryOperator> { };
                    if (language_ == frontend::Language::Vhdl2008
                        && register_domain(*lhs)
                            == frontend::ValueDomain::Integer
                        && register_domain(*rhs)
                            == frontend::ValueDomain::Integer
                        && !scalar_result) {
                        if (source.text == "+") {
                            integer_operation = IntegerBinaryOperator::add;
                        } else if (source.text == "-") {
                            integer_operation = IntegerBinaryOperator::subtract;
                        } else if (source.text == "*") {
                            integer_operation = IntegerBinaryOperator::multiply;
                        } else if (source.text == "/") {
                            integer_operation = IntegerBinaryOperator::divide;
                        } else if (source.text == "rem") {
                            integer_operation = IntegerBinaryOperator::remainder;
                        } else if (source.text == "mod") {
                            integer_operation = IntegerBinaryOperator::modulo;
                        }
                    }
                    if (integer_operation) {
                        if (register_width(*lhs) != operand_width) {
                            lhs = resize_register(*lhs, operand_width, true);
                        }
                        if (register_width(*rhs) != operand_width) {
                            rhs = resize_register(*rhs, operand_width, true);
                        }
                        const auto destination = allocate_register(
                            operand_width, frontend::ValueDomain::Integer);
                        process_.operations.emplace_back(IntegerBinary {
                            *integer_operation, destination, *lhs, *rhs });
                        result = destination;
                    } else {
                        auto operation = std::optional<BinaryOperator> { };
                        if (source.text == "+") {
                            operation = signed_operands
                                ? BinaryOperator::add_signed
                                : BinaryOperator::add_unsigned;
                        } else if (source.text == "-") {
                            operation = signed_operands
                                ? BinaryOperator::subtract_signed
                                : BinaryOperator::subtract_unsigned;
                        } else if (source.text == "*") {
                            operation = signed_operands
                                ? BinaryOperator::multiply_signed
                                : BinaryOperator::multiply_unsigned;
                        } else if (source.text == "/") {
                            operation = signed_operands
                                ? BinaryOperator::divide_signed
                                : BinaryOperator::divide_unsigned;
                        } else if (source.text == "%") {
                            operation = signed_operands
                                ? BinaryOperator::remainder_signed
                                : BinaryOperator::modulo_unsigned;
                        } else if (source.text == "rem") {
                            operation = signed_operands
                                ? BinaryOperator::remainder_signed
                                : BinaryOperator::modulo_unsigned;
                        } else if (source.text == "mod") {
                            operation = signed_operands
                                ? BinaryOperator::modulo_signed
                                : BinaryOperator::modulo_unsigned;
                        } else if (source.text == "**") {
                            operation = signed_operands
                                ? BinaryOperator::power_signed
                                : BinaryOperator::power_unsigned;
                        } else if (source.text == "<") {
                            operation = signed_operands
                                ? BinaryOperator::less_signed
                                : BinaryOperator::less_unsigned;
                        } else if (source.text == "<=") {
                            operation = signed_operands
                                ? BinaryOperator::less_equal_signed
                                : BinaryOperator::less_equal_unsigned;
                        } else if (source.text == ">") {
                            operation = signed_operands
                                ? BinaryOperator::greater_signed
                                : BinaryOperator::greater_unsigned;
                        } else if (source.text == ">=") {
                            operation = signed_operands
                                ? BinaryOperator::greater_equal_signed
                                : BinaryOperator::greater_equal_unsigned;
                        } else if (source.text == "&"
                            || source.text == "and") {
                            operation = BinaryOperator::bit_and;
                        } else if (source.text == "|"
                            || source.text == "or") {
                            operation = BinaryOperator::bit_or;
                        } else if (source.text == "^"
                            || source.text == "~^"
                            || source.text == "^~"
                            || source.text == "xor") {
                            operation = BinaryOperator::bit_xor;
                        } else if (source.text == "xnor") {
                            operation = BinaryOperator::bit_xor;
                        } else if (source.text == "nand") {
                            operation = BinaryOperator::bit_and;
                        } else if (source.text == "nor") {
                            operation = BinaryOperator::bit_or;
                        } else if (language_
                                == frontend::Language::Vhdl2008
                            && (source.text == "=" || source.text == "/="
                                || source.text == "?="
                                || source.text == "?/=")) {
                            operation = source.text.starts_with('?')
                                ? BinaryOperator::vhdl_match_equal
                                : BinaryOperator::case_equal;
                        } else if (source.text == "==") {
                            operation = BinaryOperator::equal;
                        } else if (source.text == "==="
                            || source.text == "!==") {
                            operation = BinaryOperator::case_equal;
                        } else if (source.text == "!=") {
                            operation = BinaryOperator::not_equal;
                        } else if (source.text == "==?"
                            || source.text == "!=?") {
                            operation = BinaryOperator::wildcard_equal;
                        }
                        if (!operation) {
                            return std::nullopt;
                        }
                        const auto width = scalar_result ? 1U : operand_width;
                        if (register_width(*lhs) != operand_width) {
                            lhs = resize_register(
                                *lhs, operand_width, signed_operands);
                        }
                        if (register_width(*rhs) != operand_width) {
                            rhs = resize_register(
                                *rhs, operand_width, signed_operands);
                        }
                        const auto exact_comparison = source.text == "==="
                            || source.text == "!=="
                            || (language_ == frontend::Language::Vhdl2008
                                && (source.text == "="
                                    || source.text == "/="));
                        const auto domain = scalar_result
                            ? language_ == frontend::Language::Vhdl2008
                                ? frontend::ValueDomain::Boolean
                                : exact_comparison
                                ? frontend::ValueDomain::Bit2
                                : frontend::ValueDomain::Logic4
                            : register_domain(*lhs);
                        const auto destination = allocate_register(width, domain);
                        process_.operations.emplace_back(Binary {
                            *operation, destination, *lhs, *rhs });
                        const auto bitwise_inversion
                            = source.text == "~^" || source.text == "^~"
                            || source.text == "xnor"
                            || source.text == "nand"
                            || source.text == "nor";
                        if (source.text == "!==" || source.text == "/="
                            || source.text == "!=?" || source.text == "?/="
                            || bitwise_inversion) {
                            const auto inverted = allocate_register(
                                bitwise_inversion ? width : 1U, domain);
                            if (bitwise_inversion) {
                                process_.operations.emplace_back(
                                    UnaryNot { inverted, destination });
                            } else {
                                process_.operations.emplace_back(
                                    LogicalNot { inverted, destination });
                            }
                            result = inverted;
                        } else {
                            result = destination;
                        }
                    }
                }
            }
        }
        }
    }
    if (!result) {
        return std::nullopt;
    }
    const auto scalar_result = source.binary
        && (source.text == "==" || source.text == "!="
            || source.text == "===" || source.text == "!=="
            || source.text == "==?" || source.text == "!=?"
            || source.text == "<" || source.text == "<="
            || source.text == ">" || source.text == ">="
            || source.text == "&&" || source.text == "||");
    const auto context_determined
        = (language_ == frontend::Language::Vhdl2008
              && source.conditional)
        || (language_ != frontend::Language::Vhdl2008
            && ((source.binary && !scalar_result)
                || (source.unary
                    && (source.text == "+" || source.text == "-"
                        || source.text == "~"))
                || source.conditional));
    const auto span = hir_source_span(source.source);
    process_.expression_profiles.push_back(ExpressionProfile {
        SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column),
        },
        static_cast<std::uint32_t>(register_width(*result)),
        hir_expression_signed(expression_id),
        context_determined
            ? ExpressionSizingKind::context_determined
            : ExpressionSizingKind::self_determined,
        profile_domain(register_domain(*result)),
    });
    return result;
}

std::optional<StringRegisterId> Lowerer::lower_hir_string_expression(
    const semantic::ExpressionId expression_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return lower_hir_string_expression(*actual);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return lower_hir_string_expression(*actual);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = lower_hir_string_expression(
            declaration->expression);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    if (const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
        selection && selection->member < 3U) {
        return lower_hir_vhdl_environment_call_path_string_selection(
            expression_id);
    }
    if (const auto element = hir_container_element_binding(expression_id);
        element && element->selected_type != nullptr
        && element->selected_type->element_kind
            == ContainerElementKind::String) {
        const auto path = lower_hir_container_element_path(*element);
        if (!path) {
            return std::nullopt;
        }
        HirContainerElementBinding selected = *element;
        selected.type = path->type;
        selected.selected_type = path->type;
        selected.indices = path->indices;
        const auto index = lower_hir_container_element_index(selected);
        if (!index) {
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(ContainerStringRead {
            destination,
            path->container,
            *index,
            path->indices.size() > 1U || path->type->signed_indices,
            path->indices.size() > 1U,
            path->type->string_indices,
            { },
        });
        if (!element->local) {
            const auto alias = std::ranges::find_if(
                design_.container_signal_aliases_.rbegin(),
                design_.container_signal_aliases_.rend(),
                [&](const ContainerSignalAlias& candidate) {
                    return candidate.object == element->object
                        && candidate.readable;
                });
            if (alias != design_.container_signal_aliases_.rend()) {
                implicit_signal_dependencies_.push_back(alias->signal);
            }
        }
        return destination;
    }
    // A specialized parameter name must be materialized from its canonical
    // actual identity before following the declaration initializer.  The
    // initializer is the source-level default and is intentionally retained
    // in HIR; using it here would silently restore that default for runtime
    // string consumers such as $display and $write.  The evaluator only
    // produces a value for static expressions, so runtime string objects and
    // callable locals continue through the storage paths below.
    if (expression->systemverilog != nullptr) {
        const auto constant
            = specialized_hir_unit_->evaluate_string_expression(
                expression_id);
        if (constant && constant->size() <= maximum_string_bytes) {
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                destination, *constant });
            return destination;
        }
    }
    if (expression->vhdl != nullptr) {
        const auto& source = *expression->vhdl;
        const auto api = frontend::vhdl_simulator_api(source.text);
        if (source.kind == semantic::vhdl::ExpressionKind::call
            && (source.text == "@vhdl-member:all"
                || source.text == "@vhdl-dereference")
            && source.operands.size() == 1U) {
            const auto declaration = hir_target_declaration(
                source.operands.front());
            const auto binding = declaration
                ? hir_string_binding(
                      *declaration, hir_process_scope_, true)
                : std::nullopt;
            if (binding && binding->kind
                    == HirStringBindingKind::local) {
                return binding->local;
            }
            if (binding && binding->object) {
                const auto destination = allocate_string_register();
                process_.operations.emplace_back(ReadStringObject {
                    destination, *binding->object });
                return destination;
            }
        }
        const auto getenv_call = [&]()
            -> const semantic::vhdl::Expression* {
            if (api == frontend::VhdlSimulatorApi::getenv) {
                return &source;
            }
            if (source.kind != semantic::vhdl::ExpressionKind::call
                || (source.text != "@vhdl-member:all"
                    && source.text != "@vhdl-dereference")
                || source.operands.size() != 1U) {
                return nullptr;
            }
            const auto operand = specialized_hir_unit_->find_expression(
                source.operands.front());
            return operand && operand->vhdl != nullptr
                    && frontend::vhdl_simulator_api(
                        operand->vhdl->text)
                        == frontend::VhdlSimulatorApi::getenv
                ? operand->vhdl
                : nullptr;
        }();
        if (getenv_call != nullptr) {
            const bool names_valid
                = getenv_call->argument_names.empty()
                || (getenv_call->argument_names.size() == 1U
                    && (getenv_call->argument_names.front().empty()
                        || getenv_call->argument_names.front() == "name"));
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
                || getenv_call->kind
                    != semantic::vhdl::ExpressionKind::call
                || getenv_call->operands.size() != 1U
                || !names_valid) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV GETENV requires VHDL-2019 and one STRING "
                    "NAME actual",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto name = lower_hir_string_expression(
                getenv_call->operands.front());
            if (!name) {
                const auto actual = specialized_hir_unit_->find_expression(
                    getenv_call->operands.front());
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV GETENV NAME must be STRING-compatible",
                    actual && actual->vhdl != nullptr
                        ? hir_source_span(actual->vhdl->source)
                        : hir_source_span(source.source));
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(VhdlEnvironmentGetenv {
                destination, *name });
            return destination;
        }
        const bool environment_identity
            = api == frontend::VhdlSimulatorApi::vhdl_version
            || api == frontend::VhdlSimulatorApi::tool_type
            || api == frontend::VhdlSimulatorApi::tool_vendor
            || api == frontend::VhdlSimulatorApi::tool_name
            || api == frontend::VhdlSimulatorApi::tool_edition
            || api == frontend::VhdlSimulatorApi::tool_version;
        if (environment_identity) {
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
                || (source.kind != semantic::vhdl::ExpressionKind::name
                    && source.kind
                        != semantic::vhdl::ExpressionKind::call)
                || !source.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV environment identity functions require "
                    "VHDL-2019 and accept no arguments",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto value
                = api == frontend::VhdlSimulatorApi::vhdl_version
                ? std::string { "2019" }
                : api == frontend::VhdlSimulatorApi::tool_type
                ? std::string { "SIMULATION" }
                : api == frontend::VhdlSimulatorApi::tool_vendor
                ? std::string { "fsim project" }
                : api == frontend::VhdlSimulatorApi::tool_name
                ? std::string { "fsim" }
                : api == frontend::VhdlSimulatorApi::tool_edition
                ? std::string { "community" }
                : std::string { fsim::version };
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                destination, value });
            return destination;
        }
        const bool source_identity
            = api == frontend::VhdlSimulatorApi::file_name
            || api == frontend::VhdlSimulatorApi::file_path;
        if (source_identity) {
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
                || (source.kind != semantic::vhdl::ExpressionKind::name
                    && source.kind
                        != semantic::vhdl::ExpressionKind::call)
                || !source.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV source-location functions require VHDL-2019 "
                    "and accept no arguments",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto span = hir_source_span(source.source);
            const auto path = std::filesystem::path {
                span.source_name.str() }.lexically_normal();
            const auto value = api == frontend::VhdlSimulatorApi::file_name
                ? path.filename().generic_string()
                : api == frontend::VhdlSimulatorApi::file_path
                ? path.generic_string()
                : std::to_string(span.begin.line);
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                destination, value });
            return destination;
        }
        if (const auto selected
            = hir_vhdl_environment_directory_string_selection(
                expression_id)) {
            const auto local = hir_local_container_registers_.find(
                selected->directory.value());
            if (local == hir_local_container_registers_.end()) {
                report(
                    "FSIM-ELAB-VHENV-007",
                    "STD.ENV DIRECTORY selection requires a DIRECTORY variable",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            auto index = allocate_register(
                32U, frontend::ValueDomain::Integer);
            if (!selected->index) {
                process_.operations.emplace_back(LoadConstant {
                    index,
                    PackedLogic4::from_aval_bval(32U, 0U, 0U),
                });
            } else {
                auto source_index = lower_hir_expression(
                    *selected->index, 32U);
                if (!source_index) {
                    return std::nullopt;
                }
                if (register_width(*source_index) != 32U) {
                    source_index = resize_register(
                        *source_index, 32U, true);
                }
                const auto one = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    one,
                    PackedLogic4::from_aval_bval(32U, 1U, 0U),
                });
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::add,
                    index,
                    *source_index,
                    one,
                });
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                destination,
                local->second,
                index,
                true,
                false,
                false,
                { },
            });
            return destination;
        }
        if (api == frontend::VhdlSimulatorApi::dir_separator
            || (api == frontend::VhdlSimulatorApi::dir_workingdir
                && source.operands.empty())) {
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV directory functions require VHDL-2019",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(VhdlEnvironmentDirectory {
                api == frontend::VhdlSimulatorApi::dir_separator
                    ? VhdlEnvironmentDirectoryKind::separator
                    : VhdlEnvironmentDirectoryKind::get_working_directory,
                std::nullopt,
                destination,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            });
            return destination;
        }
        if (api
            == frontend::VhdlSimulatorApi::get_vhdl_assert_format) {
            return lower_hir_vhdl_assert_string_expression(expression_id);
        }
        if (api == frontend::VhdlSimulatorApi::to_string) {
            return lower_hir_vhdl_environment_string_expression(
                expression_id);
        }
        if (is_hir_vhdl_reflection_string_expression(expression_id)) {
            return lower_hir_vhdl_reflection_string_expression(
                expression_id);
        }
        if (source.kind == semantic::vhdl::ExpressionKind::string_literal
            && source.decoded_string) {
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                destination, *source.decoded_string });
            return destination;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::binary
            && source.text == "&" && source.operands.size() == 2U) {
            const auto left = lower_hir_string_expression(
                source.operands.front());
            const auto right = lower_hir_string_expression(
                source.operands.back());
            if (!left || !right) {
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(ConcatenateStrings {
                destination, { *left, *right } });
            return destination;
        }
        const auto logic_string_function
            = source.kind == semantic::vhdl::ExpressionKind::call
            ? vhdl_logic_string_function_name(source.text)
            : std::nullopt;
        if (logic_string_function) {
            const auto span = hir_source_span(source.source);
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2008) {
                report(
                    "FSIM-ELAB-VHSTD-001",
                    "predefined IEEE function '"
                        + std::string { *logic_string_function }
                        + "' requires VHDL-2008",
                    span);
                return std::nullopt;
            }
            if (source.operands.size() != 1U) {
                report(
                    "FSIM-ELAB-VHLOGIC-001",
                    std::string { *logic_string_function }
                        + " requires exactly one packed value",
                    span);
                return std::nullopt;
            }
            const auto operand_id = source.operands.front();
            const auto width = hir_expression_width(
                operand_id, hir_process_scope_);
            const auto operand = specialized_hir_unit_->find_expression(
                operand_id);
            std::optional<LoweredLiteral> literal;
            if (operand && operand->vhdl != nullptr && width
                && operand->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal
                && operand->vhdl->decoded_string) {
                auto decoded = *operand->vhdl->decoded_string;
                std::ranges::transform(
                    decoded, decoded.begin(), [](const char value) {
                        return static_cast<char>(std::toupper(
                            static_cast<unsigned char>(value)));
                    });
                const auto two_state = std::ranges::all_of(
                    decoded, [](const char value) {
                        return value == '0' || value == '1';
                    });
                try {
                    literal = LoweredLiteral {
                        two_state
                            ? PackedLogic4::from_msb_string(decoded)
                            : PackedLogic4::from_logic9_msb_string(decoded),
                        two_state ? frontend::ValueDomain::Bit2
                                  : frontend::ValueDomain::Logic9,
                    };
                } catch (const std::invalid_argument&) {
                    literal = std::nullopt;
                }
            } else if (operand && operand->vhdl != nullptr && width) {
                literal = hir_literal(
                    expression_leaf(*operand),
                    *width,
                    frontend::Language::Vhdl2008);
            }
            if (!literal && width) {
                if (const auto constant = specialized_hir_unit_
                        ->evaluate_integral_expression(
                            operand_id)) {
                    literal = LoweredLiteral {
                        unsigned_value(
                            static_cast<std::uint64_t>(*constant), *width),
                        frontend::ValueDomain::Bit2,
                    };
                }
            }
            if (!width || *width == 0U || !operand
                || operand->vhdl == nullptr || !literal) {
                report(
                    "FSIM-ELAB-VHLOGIC-003",
                    std::string { *logic_string_function }
                        + " requires a nonempty static packed value",
                    operand && operand->vhdl != nullptr
                        ? hir_source_span(operand->vhdl->source)
                        : span);
                return std::nullopt;
            }
            const bool binary
                = *logic_string_function == "to_string"
                || *logic_string_function == "to_bstring"
                || *logic_string_function == "to_binary_string";
            const auto group
                = *logic_string_function == "to_ostring"
                    || *logic_string_function == "to_octal_string"
                ? 3U
                : 4U;
            const auto output_bytes = binary
                ? *width
                : (*width + group - 1U) / group;
            if (output_bytes > maximum_string_bytes) {
                report(
                    "FSIM-ELAB-VHLOGIC-003",
                    std::string { *logic_string_function }
                        + " result exceeds the bounded runtime string "
                          "storage limit",
                    hir_source_span(operand->vhdl->source));
                return std::nullopt;
            }
            auto bits = literal->value.to_msb_string();
            std::string text;
            if (binary) {
                text = std::move(bits);
            } else {
                const auto digits = output_bytes;
                text.assign(digits, '0');
                constexpr std::string_view digits_text {
                    "0123456789ABCDEF"
                };
                for (std::size_t digit = 0; digit < digits; ++digit) {
                    unsigned value { };
                    for (std::size_t bit = 0; bit < group; ++bit) {
                        const auto offset = digit * group + bit;
                        if (offset >= bits.size()) {
                            continue;
                        }
                        const auto state
                            = bits[bits.size() - offset - 1U];
                        if (state != '0' && state != '1') {
                            report(
                                "FSIM-ELAB-VHLOGIC-003",
                                std::string { *logic_string_function }
                                    + " static octal/hex profile requires "
                                      "only 0/1 states",
                                hir_source_span(operand->vhdl->source));
                            return std::nullopt;
                        }
                        if (state == '1') {
                            value |= 1U << bit;
                        }
                    }
                    text[digits - digit - 1U] = digits_text[value];
                }
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                destination, std::move(text) });
            return destination;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::call) {
            return lower_hir_string_function_call(expression_id);
        }
        if (source.kind != semantic::vhdl::ExpressionKind::name) {
            return std::nullopt;
        }
        const auto declaration = hir_referenced_declaration(expression_id);
        if (!declaration) {
            return std::nullopt;
        }
        if (const auto initializer = hir_constant_initializer(*declaration);
            initializer && *initializer != expression_id) {
            return lower_hir_string_expression(*initializer);
        }
        const auto binding = hir_string_binding(
            *declaration, hir_process_scope_, true);
        return binding && binding->kind == HirStringBindingKind::local
                && binding->local
            ? binding->local
            : std::nullopt;
    }
    if (expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::string_literal) {
        if (!source.decoded_string) {
            report(
                "FSIM-ELAB-SVSTRING-006",
                "string literal has no decoded byte value",
                hir_source_span(source.source));
            return std::nullopt;
        }
        if (source.decoded_string->size() > maximum_string_bytes) {
            report(
                "FSIM-ELAB-SVSTRING-007",
                "string literal exceeds the 4096-byte limit",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            destination, *source.decoded_string });
        return destination;
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "$sformatf") {
        return lower_hir_string_format_expression(expression_id);
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && (source.text == ".toupper"
            || source.text == ".tolower"
            || source.text == ".substr")
        && !source.operands.empty()
        && hir_expression_is_string(
            source.operands.front(), hir_process_scope_)) {
        const auto expected = source.text == ".substr" ? 3U : 1U;
        if (source.operands.size() != expected) {
            report(
                "FSIM-ELAB-SVSTRING-018",
                "runtime string method '" + source.text
                    + "' has an incompatible argument count",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto source_string = lower_hir_string_expression(
            source.operands.front());
        if (!source_string) {
            return std::nullopt;
        }
        StringMethod method;
        method.operation = source.text == ".toupper"
            ? StringMethodOperator::toupper
            : source.text == ".tolower"
            ? StringMethodOperator::tolower
            : StringMethodOperator::substr;
        method.source = *source_string;
        method.string_destination = allocate_string_register();
        if (source.text == ".substr") {
            auto first = lower_hir_expression(source.operands[1], 32U);
            auto second = lower_hir_expression(source.operands[2], 32U);
            if (!first || !second) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "substr indices must be signed 32-bit integral values",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (register_width(*first) != 32U) {
                first = resize_register(
                    *first, 32U,
                    hir_expression_signed(source.operands[1]));
            }
            if (register_width(*second) != 32U) {
                second = resize_register(
                    *second, 32U,
                    hir_expression_signed(source.operands[2]));
            }
            method.first = *first;
            method.second = *second;
        }
        process_.operations.emplace_back(method);
        return method.string_destination;
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "$typename") {
        if (language_ != frontend::Language::SystemVerilog2017
            || source.operands.size() != 1U) {
            report(
                "FSIM-ELAB-SVTYPENAME-001",
                "$typename requires exactly one SystemVerilog expression "
                "or type",
                hir_source_span(source.source));
            return std::nullopt;
        }
        const auto operand_id = source.operands.front();
        const auto operand = specialized_hir_unit_->find_expression(
            operand_id);
        if (!operand || operand->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& operand_source = *operand->systemverilog;
        const auto declaration_id = hir_referenced_declaration(operand_id);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        std::string text;
        if (operand_source.kind == semantic::sv::ExpressionKind::name
            && declaration && declaration->systemverilog != nullptr
            && (declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::typedef_declaration
                || declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::nettype_declaration
                || declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::type_parameter)) {
            text = operand_source.text;
        } else if (operand_source.kind
                == semantic::sv::ExpressionKind::name
            && !declaration && !operand_source.text.empty()) {
            text = operand_source.text;
        } else if (declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type) {
            text = hir_systemverilog_type_name(
                *specialized_hir_unit_,
                *declaration->systemverilog->type);
        } else if (!operand_source.nominal_type.empty()) {
            text = operand_source.nominal_type;
        } else {
            switch (operand_source.scalar_kind) {
            case semantic::sv::ScalarKind::short_real:
                text = "shortreal";
                break;
            case semantic::sv::ScalarKind::real:
                text = "real";
                break;
            case semantic::sv::ScalarKind::realtime:
                text = "realtime";
                break;
            case semantic::sv::ScalarKind::time:
                text = "time";
                break;
            case semantic::sv::ScalarKind::chandle:
                text = "chandle";
                break;
            case semantic::sv::ScalarKind::none:
                break;
            }
            if (text.empty()
                && operand_source.kind
                    == semantic::sv::ExpressionKind::string_literal) {
                text = "string";
            } else if (text.empty()) {
                const auto width = hir_expression_width(
                    operand_id, hir_process_scope_);
                if (width && *width != 0U) {
                    if (operand_source.kind
                            == semantic::sv::ExpressionKind::integer_literal
                        && *width == 32U
                        && operand_source.decimal_literal) {
                        text = "int";
                    } else {
                        text = "logic";
                        if (hir_expression_signed(operand_id)) {
                            text += " signed";
                        }
                        if (*width != 1U) {
                            text += "[" + std::to_string(*width - 1U)
                                + ":0]";
                        }
                    }
                }
            }
        }
        if (text.empty() || text.size() > maximum_string_bytes) {
            report(
                "FSIM-ELAB-SVTYPENAME-001",
                "$typename argument has no bounded statically known "
                "SystemVerilog type name",
                hir_source_span(operand_source.source));
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            destination, std::move(text) });
        return destination;
    }
    const auto declaration = hir_referenced_declaration(expression_id);
    if (declaration) {
        if (const auto initializer = hir_constant_initializer(*declaration);
            initializer && *initializer != expression_id) {
            return lower_hir_string_expression(*initializer);
        }
    }
    if (source.kind == semantic::sv::ExpressionKind::concatenation
        && !source.operands.empty()) {
        std::vector<StringRegisterId> operands;
        operands.reserve(source.operands.size());
        for (const auto operand : source.operands) {
            const auto lowered = lower_hir_string_expression(operand);
            if (!lowered) {
                return std::nullopt;
            }
            operands.push_back(*lowered);
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(ConcatenateStrings {
            destination, std::move(operands) });
        return destination;
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "?:" && source.operands.size() == 3U) {
        const auto condition_width = hir_expression_width(
            source.operands[0], hir_process_scope_)
                                         .value_or(1U);
        auto condition = lower_hir_expression(
            source.operands[0], condition_width);
        if (!condition) {
            return std::nullopt;
        }
        if (register_width(*condition) != 1U) {
            const auto zero = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(
                LogicalNot { zero, *condition });
            const auto truth = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LogicalNot { truth, zero });
            condition = truth;
        }

        const auto destination = allocate_string_register();
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            *condition, 0U, 0U, UnknownBranchPolicy::error });

        const auto when_true = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto true_value = lower_hir_string_expression(
            source.operands[1]);
        if (!true_value) {
            return std::nullopt;
        }
        process_.operations.emplace_back(CopyStringRegister {
            destination, *true_value });
        const auto finish = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });

        const auto when_false = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto false_value = lower_hir_string_expression(
            source.operands[2]);
        if (!false_value) {
            return std::nullopt;
        }
        process_.operations.emplace_back(CopyStringRegister {
            destination, *false_value });
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            *condition,
            when_true,
            when_false,
            UnknownBranchPolicy::error,
        };
        process_.operations[finish] = Jump { end };
        return destination;
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && declaration) {
        const auto callable = specialized_hir_unit_->find_declaration(
            *declaration);
        if (callable && callable->systemverilog != nullptr
            && callable->systemverilog->form
                == semantic::sv::DeclarationForm::function
            && callable->systemverilog->type
            && callable->systemverilog->type->value_form
                == semantic::sv::TypeForm::string) {
            return lower_hir_string_function_call(expression_id);
        }
    }
    const auto binding = declaration
        ? hir_string_binding(
              *declaration, hir_process_scope_, true)
        : std::nullopt;
    if (!binding) {
        return std::nullopt;
    }
    if (binding->kind == HirStringBindingKind::local) {
        return binding->local;
    }
    if (!binding->object) {
        return std::nullopt;
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(ReadStringObject {
        destination, *binding->object });
    return destination;
}

std::optional<Lowerer::HirPackedUpdateTarget>
Lowerer::capture_hir_packed_update_target(
    const semantic::ExpressionId target)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    if (!expression) {
        return std::nullopt;
    }
    if (expression->vhdl != nullptr) {
        const auto& source = *expression->vhdl;
        if (source.kind != semantic::vhdl::ExpressionKind::name) {
            return std::nullopt;
        }
        const auto declaration = hir_target_declaration(target);
        auto binding = declaration
            ? hir_runtime_binding(
                  *declaration, hir_process_scope_, true)
            : std::nullopt;
        if (!binding
            || (binding->signal
                && read_only_signals_.contains(*binding->signal))
            || binding->width == 0U) {
            return std::nullopt;
        }
        const auto captured = allocate_register(
            binding->width, binding->domain);
        if (binding->kind == HirRuntimeBindingKind::local) {
            if (!binding->local) {
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyRegister {
                captured, *binding->local });
        } else {
            if (!binding->signal) {
                return std::nullopt;
            }
            process_.operations.emplace_back(ReadSignal {
                captured, *binding->signal });
        }
        const auto signed_value = binding->signed_value;
        return HirPackedUpdateTarget {
            std::move(*binding),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            source.text,
            register_width(captured),
            register_domain(captured),
            signed_value,
            false,
            false,
            captured,
        };
    }
    if (expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const bool name = source.kind == semantic::sv::ExpressionKind::name;
    const bool index = source.kind == semantic::sv::ExpressionKind::index;
    const bool slice = source.kind == semantic::sv::ExpressionKind::slice;
    if (!name && !index && !slice) {
        return std::nullopt;
    }
    const auto declaration = hir_target_declaration(target);
    auto binding = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, true)
        : std::nullopt;
    const auto selected = index || slice;
    if (!binding
        || (selected
            && (source.operands.size() != (index ? 2U : 3U)
                || hir_target_declaration(source.operands.front())
                    != declaration))) {
        return std::nullopt;
    }
    if (binding->signal
        && read_only_signals_.contains(*binding->signal)) {
        return std::nullopt;
    }

    const auto member = hir_systemverilog_member_selection(
        selected ? source.operands.front() : target);
    auto constant_selection = selected
        ? hir_root_constant_selection(target, hir_process_scope_)
        : std::nullopt;
    if (member) {
        if (member->offset > std::numeric_limits<std::uint32_t>::max()
            || member->width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (constant_selection) {
            if (member->offset
                > std::numeric_limits<std::size_t>::max()
                    - constant_selection->offset) {
                return std::nullopt;
            }
            constant_selection->offset += member->offset;
        } else if (!selected) {
            constant_selection = HirConstantSelection {
                member->offset, member->width
            };
        }
    }
    const auto dynamic_part_width = slice && !constant_selection
        ? hir_dynamic_part_width(target, hir_process_scope_)
        : std::nullopt;
    const auto width = constant_selection
        ? std::optional { constant_selection->width }
        : index              ? std::optional<std::size_t> { 1U }
        : dynamic_part_width ? dynamic_part_width
        : !selected          ? std::optional {
              member ? member->width : binding->width
          }
                    : std::nullopt;
    if (!width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    std::optional<DynamicIndex> dynamic_selection;
    if (selected && !constant_selection) {
        dynamic_selection = lower_hir_dynamic_index(
            source.operands.front(), source.operands[1],
            member ? member->width : binding->width,
            member ? static_cast<std::uint32_t>(member->offset) : 0U);
        if (!dynamic_selection) {
            return std::nullopt;
        }
    }

    const auto domain = member ? member->domain : binding->domain;
    const auto whole = allocate_register(binding->width, binding->domain);
    if (binding->kind == HirRuntimeBindingKind::local) {
        if (!binding->local) {
            return std::nullopt;
        }
        process_.operations.emplace_back(CopyRegister {
            whole, *binding->local });
    } else {
        if (!binding->signal) {
            return std::nullopt;
        }
        process_.operations.emplace_back(ReadSignal {
            whole, *binding->signal });
    }
    const auto captured = allocate_register(*width, domain);
    if (constant_selection) {
        process_.operations.emplace_back(Extract {
            captured,
            whole,
            static_cast<std::uint32_t>(constant_selection->offset),
            static_cast<std::uint32_t>(*width),
        });
    } else if (index && dynamic_selection) {
        process_.operations.emplace_back(DynamicExtract {
            captured, whole, *dynamic_selection });
    } else if (slice && dynamic_selection && dynamic_part_width) {
        process_.operations.emplace_back(DynamicPartSelect {
            captured,
            whole,
            dynamic_selection->index,
            dynamic_selection->left,
            dynamic_selection->right,
            static_cast<std::uint32_t>(*dynamic_part_width),
            source.text == "+:",
            dynamic_selection->left >= dynamic_selection->right,
            is_two_state_domain(domain),
            dynamic_selection->base_offset,
        });
    } else {
        process_.operations.emplace_back(CopyRegister {
            captured, whole });
    }
    const bool signed_value = member
        ? member->signed_value
        : binding->signed_value;
    return HirPackedUpdateTarget {
        std::move(*binding),
        constant_selection,
        dynamic_selection,
        dynamic_part_width,
        source.text,
        *width,
        domain,
        signed_value,
        index,
        slice,
        captured,
    };
}

std::optional<RegisterId> Lowerer::lower_hir_packed_update_value(
    const HirPackedUpdateTarget& target,
    const std::string_view operation,
    const std::optional<semantic::ExpressionId> rhs_expression)
{
    const auto rhs_width = rhs_expression
        ? hir_expression_width(*rhs_expression, hir_process_scope_)
              .value_or(target.width)
        : target.width;
    auto rhs = rhs_expression
        ? lower_hir_expression(
              *rhs_expression,
              operation == "<<" || operation == ">>"
                      || operation == "<<<" || operation == ">>>"
                  ? rhs_width
                  : target.width)
        : std::optional<RegisterId> { };
    if (!rhs_expression) {
        rhs = allocate_register(target.width, target.domain);
        process_.operations.emplace_back(LoadConstant {
            *rhs, unsigned_value(1U, target.width) });
    }
    if (!rhs) {
        return std::nullopt;
    }
    const auto shift = operation == "<<" || operation == ">>"
        || operation == "<<<" || operation == ">>>";
    if (!shift && register_width(*rhs) != target.width) {
        rhs = resize_register(
            *rhs,
            target.width,
            rhs_expression && hir_expression_signed(*rhs_expression));
    }
    if (shift) {
        const auto destination = allocate_register(
            target.width,
            is_two_state_domain(target.domain)
                    && is_two_state_domain(register_domain(*rhs))
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        const auto shift_operation
            = lowered_shift_operator(operation, target.signed_value);
        process_.operations.emplace_back(Shift {
            shift_operation,
            destination,
            target.captured,
            *rhs,
            false,
        });
        return destination;
    }

    const bool signed_operands = target.signed_value
        || (rhs_expression && hir_expression_signed(*rhs_expression));
    if (target.domain == frontend::ValueDomain::Integer) {
        auto integer = std::optional<IntegerBinaryOperator> { };
        if (operation == "+") {
            integer = IntegerBinaryOperator::add;
        } else if (operation == "-") {
            integer = IntegerBinaryOperator::subtract;
        } else if (operation == "*") {
            integer = IntegerBinaryOperator::multiply;
        } else if (operation == "/") {
            integer = IntegerBinaryOperator::divide;
        } else if (operation == "%") {
            integer = IntegerBinaryOperator::remainder;
        }
        if (integer) {
            const auto destination = allocate_register(
                target.width, target.domain);
            process_.operations.emplace_back(IntegerBinary {
                *integer, destination, target.captured, *rhs });
            return destination;
        }
    }
    auto binary = std::optional<BinaryOperator> { };
    if (operation == "+") {
        binary = signed_operands ? BinaryOperator::add_signed
                                 : BinaryOperator::add_unsigned;
    } else if (operation == "-") {
        binary = signed_operands ? BinaryOperator::subtract_signed
                                 : BinaryOperator::subtract_unsigned;
    } else if (operation == "*") {
        binary = signed_operands ? BinaryOperator::multiply_signed
                                 : BinaryOperator::multiply_unsigned;
    } else if (operation == "/") {
        binary = signed_operands ? BinaryOperator::divide_signed
                                 : BinaryOperator::divide_unsigned;
    } else if (operation == "%") {
        binary = signed_operands ? BinaryOperator::remainder_signed
                                 : BinaryOperator::modulo_unsigned;
    } else if (operation == "&") {
        binary = BinaryOperator::bit_and;
    } else if (operation == "|") {
        binary = BinaryOperator::bit_or;
    } else if (operation == "^") {
        binary = BinaryOperator::bit_xor;
    }
    if (!binary) {
        return std::nullopt;
    }
    const auto destination = allocate_register(target.width, target.domain);
    process_.operations.emplace_back(Binary {
        *binary, destination, target.captured, *rhs });
    return destination;
}

bool Lowerer::write_hir_packed_update_target(
    const HirPackedUpdateTarget& target,
    const RegisterId source)
{
    auto value = source;
    if (register_width(value) != target.width) {
        value = resize_register(value, target.width, target.signed_value);
    }
    if (register_domain(value) != target.domain) {
        const auto converted = allocate_register(
            target.width, target.domain);
        process_.operations.emplace_back(CopyRegister {
            converted, value });
        value = converted;
    }
    if (!target.constant_selection && !target.index && !target.slice
        && target.binding.domain == frontend::ValueDomain::Integer
        && target.binding.integer_range) {
        process_.operations.emplace_back(IntegerCheck {
            value,
            std::min(target.binding.integer_range->left,
                target.binding.integer_range->right),
            std::max(target.binding.integer_range->left,
                target.binding.integer_range->right),
        });
    }
    if (target.binding.kind == HirRuntimeBindingKind::local) {
        if (!target.binding.local) {
            return false;
        }
        if (target.constant_selection) {
            process_.operations.emplace_back(Insert {
                *target.binding.local,
                *target.binding.local,
                value,
                static_cast<std::uint32_t>(
                    target.constant_selection->offset),
            });
        } else if (target.index && target.dynamic_selection) {
            process_.operations.emplace_back(DynamicInsert {
                *target.binding.local,
                *target.binding.local,
                value,
                *target.dynamic_selection,
            });
        } else if (target.slice && target.dynamic_selection
            && target.dynamic_part_width) {
            process_.operations.emplace_back(DynamicPartInsert {
                *target.binding.local,
                *target.binding.local,
                value,
                DynamicPartIndex {
                    target.dynamic_selection->index,
                    target.dynamic_selection->left,
                    target.dynamic_selection->right,
                    target.dynamic_selection->base_offset,
                    static_cast<std::uint32_t>(
                        *target.dynamic_part_width),
                    target.selection_operation == "+:",
                    target.dynamic_selection->left
                        >= target.dynamic_selection->right,
                },
            });
        } else {
            process_.operations.emplace_back(CopyRegister {
                *target.binding.local, value });
        }
        return true;
    }
    if (!target.binding.signal
        || read_only_signals_.contains(*target.binding.signal)) {
        return false;
    }
    if (target.constant_selection) {
        process_.operations.emplace_back(WriteBlockingSlice {
            *target.binding.signal,
            value,
            static_cast<std::uint32_t>(
                target.constant_selection->offset),
        });
    } else if (target.index && target.dynamic_selection) {
        process_.operations.emplace_back(WriteBlockingDynamicSlice {
            *target.binding.signal, value, *target.dynamic_selection });
    } else if (target.slice && target.dynamic_selection
        && target.dynamic_part_width) {
        process_.operations.emplace_back(WriteBlockingDynamicPartSlice {
            *target.binding.signal,
            value,
            DynamicPartIndex {
                target.dynamic_selection->index,
                target.dynamic_selection->left,
                target.dynamic_selection->right,
                target.dynamic_selection->base_offset,
                static_cast<std::uint32_t>(*target.dynamic_part_width),
                target.selection_operation == "+:",
                target.dynamic_selection->left
                    >= target.dynamic_selection->right,
            },
        });
    } else {
        process_.operations.emplace_back(WriteBlocking {
            *target.binding.signal, value });
    }
    return true;
}

bool Lowerer::lower_hir_packed_copy_out(
    const semantic::ExpressionId target,
    const RegisterId source)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    if (!expression) {
        return false;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::assignment_pattern) {
        const auto& pattern = *expression->systemverilog;
        if (pattern.associations.empty()
            || std::ranges::any_of(
                pattern.associations,
                [](const semantic::sv::AssignmentPatternAssociation&
                        association) {
                    return !association.choice_spelling.empty()
                        || !association.choices.empty();
                })) {
            report(
                "FSIM-ELAB-SVASSIGN-002",
                "an assignment-pattern target requires positional lvalues",
                hir_source_span(pattern.source));
            return true;
        }
        std::vector<std::size_t> widths;
        widths.reserve(pattern.associations.size());
        std::size_t total_width { };
        for (const auto& association : pattern.associations) {
            const auto width = hir_expression_width(
                association.value, hir_process_scope_);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                || total_width
                    > std::numeric_limits<std::uint32_t>::max() - *width) {
                return false;
            }
            widths.push_back(*width);
            total_width += *width;
        }
        if (register_width(source) != total_width) {
            return false;
        }
        std::uint32_t offset { };
        for (std::size_t reverse = pattern.associations.size();
            reverse != 0U; --reverse) {
            const auto index = reverse - 1U;
            const auto value = allocate_register(
                widths[index], register_domain(source));
            process_.operations.emplace_back(Extract {
                value,
                source,
                offset,
                static_cast<std::uint32_t>(widths[index]),
            });
            if (!lower_hir_packed_copy_out(
                    pattern.associations[index].value, value)) {
                return false;
            }
            offset += static_cast<std::uint32_t>(widths[index]);
        }
        return true;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && (expression->systemverilog->text == "@stream-left"
            || expression->systemverilog->text == "@stream-right")) {
        const auto& streaming = *expression->systemverilog;
        if (streaming.operands.size() < 2U) {
            return false;
        }
        const auto slice_size = hir_constant_integer(
            streaming.operands.front());
        if (!slice_size || *slice_size <= 0
            || static_cast<std::uint64_t>(*slice_size)
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        std::vector<std::size_t> widths;
        widths.reserve(streaming.operands.size() - 1U);
        std::size_t total_width { };
        for (std::size_t index = 1U;
            index < streaming.operands.size(); ++index) {
            const auto width = hir_expression_width(
                streaming.operands[index], hir_process_scope_);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                || total_width
                    > std::numeric_limits<std::uint32_t>::max() - *width) {
                return false;
            }
            widths.push_back(*width);
            total_width += *width;
        }
        if (register_width(source) != total_width) {
            return false;
        }
        auto distributed = source;
        if (streaming.text == "@stream-left"
            && static_cast<std::size_t>(*slice_size) < total_width) {
            std::vector<RegisterId> slices;
            for (std::size_t offset { }; offset < total_width;) {
                const auto width = std::min(
                    static_cast<std::size_t>(*slice_size),
                    total_width - offset);
                const auto slice = allocate_register(
                    width, register_domain(source));
                process_.operations.emplace_back(Extract {
                    slice,
                    source,
                    static_cast<std::uint32_t>(offset),
                    static_cast<std::uint32_t>(width),
                });
                slices.push_back(slice);
                offset += width;
            }
            distributed = allocate_register(
                total_width, register_domain(source));
            process_.operations.emplace_back(Concatenate {
                distributed,
                std::move(slices),
                static_cast<std::uint32_t>(total_width),
            });
        }
        std::uint32_t offset { };
        for (std::size_t reverse = widths.size(); reverse != 0U; --reverse) {
            const auto index = reverse - 1U;
            const auto value = allocate_register(
                widths[index], register_domain(distributed));
            process_.operations.emplace_back(Extract {
                value,
                distributed,
                offset,
                static_cast<std::uint32_t>(widths[index]),
            });
            if (!lower_hir_packed_copy_out(
                    streaming.operands[index + 1U], value)) {
                return false;
            }
            offset += static_cast<std::uint32_t>(widths[index]);
        }
        return true;
    }
    if (const auto element = hir_container_element_binding(target)) {
        if (element->read_only || register_width(source) != element->width) {
            return false;
        }
        const auto index = lower_hir_container_element_index(*element);
        if (!index) {
            return false;
        }
        auto value = source;
        if (register_domain(value) != element->domain) {
            const auto converted = allocate_register(
                element->width, element->domain);
            process_.operations.emplace_back(CopyRegister {
                converted, value });
            value = converted;
        }
        if (element->local) {
            process_.operations.emplace_back(ContainerWrite {
                *element->local,
                *index,
                value,
                element->indices.size() > 1U
                    || element->type->signed_indices,
                element->indices.size() > 1U,
                element->type->string_indices,
            });
        } else if (element->type->string_indices) {
            const auto container = allocate_container_register(
                *element->type);
            process_.operations.emplace_back(ReadContainerObject {
                container, element->object });
            process_.operations.emplace_back(ContainerWrite {
                container,
                *index,
                value,
                element->indices.size() > 1U
                    || element->type->signed_indices,
                element->indices.size() > 1U,
                true,
            });
            process_.operations.emplace_back(WriteContainerObject {
                element->object, container, std::nullopt });
        } else {
            process_.operations.emplace_back(WriteContainerObjectElement {
                element->object,
                *index,
                value,
                element->indices.size() > 1U
                    || element->type->signed_indices,
                element->indices.size() > 1U,
                false,
                std::nullopt,
                std::nullopt,
            });
        }
        return true;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::concatenation) {
        const auto& operands = expression->systemverilog->operands;
        if (operands.empty()) {
            return false;
        }
        std::vector<std::size_t> widths;
        widths.reserve(operands.size());
        std::size_t total_width { };
        for (const auto operand : operands) {
            const auto width = hir_expression_width(
                operand, hir_process_scope_);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                || total_width
                    > std::numeric_limits<std::uint32_t>::max() - *width) {
                return false;
            }
            widths.push_back(*width);
            total_width += *width;
        }
        if (register_width(source) != total_width) {
            return false;
        }
        std::vector<RegisterId> values(operands.size());
        std::uint32_t offset { };
        for (std::size_t reverse = operands.size();
            reverse != 0U; --reverse) {
            const auto index = reverse - 1U;
            values[index] = allocate_register(
                widths[index], register_domain(source));
            process_.operations.emplace_back(Extract {
                values[index],
                source,
                offset,
                static_cast<std::uint32_t>(widths[index]),
            });
            offset += static_cast<std::uint32_t>(widths[index]);
        }
        for (std::size_t reverse = operands.size();
            reverse != 0U; --reverse) {
            const auto index = reverse - 1U;
            if (!lower_hir_packed_copy_out(
                    operands[index], values[index])) {
                return false;
            }
        }
        return true;
    }
    std::span<const semantic::ExpressionId> operands;
    std::string_view selection_operation;
    bool index { };
    bool slice { };
    bool name { };
    if (expression->systemverilog != nullptr) {
        const auto& selected = *expression->systemverilog;
        operands = selected.operands;
        selection_operation = selected.text;
        name = selected.kind == semantic::sv::ExpressionKind::name;
        index = selected.kind == semantic::sv::ExpressionKind::index;
        slice = selected.kind == semantic::sv::ExpressionKind::slice;
    } else {
        const auto& selected = *expression->vhdl;
        operands = selected.operands;
        selection_operation = selected.text;
        name = selected.kind == semantic::vhdl::ExpressionKind::name;
        index = selected.kind == semantic::vhdl::ExpressionKind::index;
        slice = selected.kind == semantic::vhdl::ExpressionKind::slice;
    }
    if (!name && !index && !slice) {
        return false;
    }
    const auto declaration = hir_target_declaration(target);
    const auto binding = declaration
        ? hir_runtime_binding(
              *declaration, hir_process_scope_, true)
        : std::nullopt;
    const auto selected = index || slice;
    if (!binding
        || (selected
            && (operands.size() != (index ? 2U : 3U)
                || hir_referenced_declaration(operands.front())
                    != declaration))) {
        return false;
    }
    const auto member = hir_systemverilog_member_selection(
        selected ? operands.front() : target);
    auto constant_selection = selected
        ? hir_constant_selection(target, hir_process_scope_)
        : std::nullopt;
    if (member) {
        if (member->offset
                > std::numeric_limits<std::uint32_t>::max()
            || member->width
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        if (constant_selection) {
            if (member->offset
                > std::numeric_limits<std::size_t>::max()
                    - constant_selection->offset) {
                return false;
            }
            constant_selection->offset += member->offset;
        } else if (!selected) {
            constant_selection = HirConstantSelection {
                member->offset, member->width
            };
        }
    }
    const auto dynamic_part_width = slice && !constant_selection
        ? hir_dynamic_part_width(target, hir_process_scope_)
        : std::nullopt;
    const auto target_width = constant_selection
        ? std::optional { constant_selection->width }
        : index              ? std::optional<std::size_t> { 1U }
        : dynamic_part_width ? dynamic_part_width
        : !selected          ? std::optional {
              member ? member->width : binding->width
          }
                    : std::nullopt;
    if (!target_width || *target_width == 0U
        || *target_width > std::numeric_limits<std::uint32_t>::max()
        || register_width(source) != *target_width) {
        return false;
    }
    std::optional<DynamicIndex> dynamic_selection;
    if (selected && !constant_selection) {
        const auto source_width = member ? member->width : binding->width;
        dynamic_selection = lower_hir_dynamic_index(
            operands.front(), operands[1], source_width,
            member ? static_cast<std::uint32_t>(member->offset) : 0U);
        if (!dynamic_selection) {
            return false;
        }
    }

    auto copy_source = source;
    const auto target_domain = member ? member->domain : binding->domain;
    if (register_domain(copy_source) != target_domain) {
        const auto converted = allocate_register(
            *target_width, target_domain);
        process_.operations.emplace_back(CopyRegister {
            converted, copy_source });
        copy_source = converted;
    }
    if (!selected && !member
        && binding->domain == frontend::ValueDomain::Integer
        && binding->integer_range) {
        process_.operations.emplace_back(IntegerCheck {
            copy_source,
            std::min(binding->integer_range->left,
                binding->integer_range->right),
            std::max(binding->integer_range->left,
                binding->integer_range->right),
        });
    }
    if (binding->kind == HirRuntimeBindingKind::local) {
        if (!binding->local) {
            return false;
        }
        if (constant_selection) {
            process_.operations.emplace_back(Insert {
                *binding->local,
                *binding->local,
                copy_source,
                static_cast<std::uint32_t>(
                    constant_selection->offset),
            });
        } else if (index && dynamic_selection) {
            process_.operations.emplace_back(DynamicInsert {
                *binding->local,
                *binding->local,
                copy_source,
                *dynamic_selection,
            });
        } else if (slice && dynamic_selection && dynamic_part_width) {
            process_.operations.emplace_back(DynamicPartInsert {
                *binding->local,
                *binding->local,
                copy_source,
                DynamicPartIndex {
                    dynamic_selection->index,
                    dynamic_selection->left,
                    dynamic_selection->right,
                    dynamic_selection->base_offset,
                    static_cast<std::uint32_t>(*dynamic_part_width),
                    selection_operation == "+:",
                    dynamic_selection->left
                        >= dynamic_selection->right,
                },
            });
        } else {
            process_.operations.emplace_back(CopyRegister {
                *binding->local, copy_source });
        }
        return true;
    }
    if (!binding->signal
        || read_only_signals_.contains(*binding->signal)) {
        return false;
    }
    if (constant_selection) {
        process_.operations.emplace_back(WriteBlockingSlice {
            *binding->signal,
            copy_source,
            static_cast<std::uint32_t>(constant_selection->offset),
        });
    } else if (index && dynamic_selection) {
        process_.operations.emplace_back(WriteBlockingDynamicSlice {
            *binding->signal, copy_source, *dynamic_selection });
    } else if (slice && dynamic_selection && dynamic_part_width) {
        process_.operations.emplace_back(WriteBlockingDynamicPartSlice {
            *binding->signal,
            copy_source,
            DynamicPartIndex {
                dynamic_selection->index,
                dynamic_selection->left,
                dynamic_selection->right,
                dynamic_selection->base_offset,
                static_cast<std::uint32_t>(*dynamic_part_width),
                selection_operation == "+:",
                dynamic_selection->left >= dynamic_selection->right,
            },
        });
    } else {
        process_.operations.emplace_back(WriteBlocking {
            *binding->signal, copy_source });
    }
    return true;
}

bool Lowerer::lower_hir_string_copy_out(
    const semantic::ExpressionId target,
    const StringRegisterId source)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return false;
    }
    const auto declaration = hir_target_declaration(target);
    const auto binding = declaration
        ? hir_string_binding(
              *declaration, hir_process_scope_, true)
        : std::nullopt;
    if (!binding) {
        return false;
    }
    if (binding->kind == HirStringBindingKind::local) {
        if (!binding->local) {
            return false;
        }
        process_.operations.emplace_back(CopyStringRegister {
            *binding->local, source });
        return true;
    }
    if (!binding->object
        || read_only_string_objects_.contains(*binding->object)) {
        return false;
    }
    process_.operations.emplace_back(WriteStringObject {
        *binding->object, source });
    return true;
}

} // namespace fsim::elaboration
