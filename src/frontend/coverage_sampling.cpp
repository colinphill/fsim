// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_sampling.hpp"
#include "fsim/frontend/coverage_cross_inventory.hpp"
#include "fsim/frontend/coverage_limits.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

namespace fsim::frontend {

std::optional<double>
SystemVerilogCoverageSampleValue::real_value() const noexcept
{
    if (scalar_kind == SystemVerilogScalarKind::ShortReal) {
        return static_cast<double>(
            std::bit_cast<float>(static_cast<std::uint32_t>(scalar_bits)));
    }
    if (scalar_kind == SystemVerilogScalarKind::Real
        || scalar_kind == SystemVerilogScalarKind::Realtime) {
        return std::bit_cast<double>(scalar_bits);
    }
    return std::nullopt;
}

namespace {

    [[nodiscard]] bool valid_binary_plane(const std::string_view plane)
    {
        return std::ranges::all_of(plane, [](const char bit) {
            return bit == '0' || bit == '1';
        });
    }

    [[nodiscard]] std::string scalar_coverage_plane(
        const std::uint64_t value,
        const std::uint32_t width)
    {
        std::string result(width, '0');
        for (std::uint32_t offset = 0U; offset < width; ++offset) {
            const auto bit = width - offset - 1U;
            result[offset] = ((value >> bit) & 1U) != 0U ? '1' : '0';
        }
        return result;
    }

    [[nodiscard]] std::string normalized_coverage_plane(
        std::string plane,
        const std::size_t width,
        const bool signed_value,
        const char unsigned_extension)
    {
        if (plane.size() > width) {
            plane.erase(0U, plane.size() - width);
        } else if (plane.size() < width) {
            const auto extension = signed_value && !plane.empty()
                ? plane.front()
                : unsigned_extension;
            plane.insert(0U, width - plane.size(), extension);
        }
        return plane;
    }

    [[nodiscard]] bool sample_has_unknown(
        const SystemVerilogCoverageSampleValue& sample)
    {
        return sample.unknown_mask != 0U
            || sample.unknown_bits.find('1') != std::string::npos;
    }

    [[nodiscard]] std::string sample_identity(
        const SystemVerilogCoverageSampleValue& sample)
    {
        if (const auto real = sample.real_value()) {
            char buffer[64] { };
            const auto converted = std::to_chars(
                std::begin(buffer), std::end(buffer), *real,
                std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            return converted.ec == std::errc { }
                ? std::string { buffer, converted.ptr }
                : std::string { "nonfinite" };
        }
        if (sample.value_bits.empty() && !sample_has_unknown(sample)
            && sample.width <= 64U) {
            return std::to_string(sample.value);
        }
        auto raw_value = sample.value_bits;
        auto raw_unknown = sample.unknown_bits;
        if (raw_value.empty() && sample.width <= 64U) {
            raw_value = scalar_coverage_plane(
                static_cast<std::uint64_t>(sample.value), sample.width);
            raw_unknown = scalar_coverage_plane(
                sample.unknown_mask, sample.width);
        }
        if (raw_value.empty()) {
            return "invalid";
        }
        auto value = normalized_coverage_plane(
            std::move(raw_value), sample.width, sample.signed_value, '0');
        auto unknown = raw_unknown.empty()
            ? std::string(sample.width, '0')
            : normalized_coverage_plane(
                  std::move(raw_unknown), sample.width, sample.signed_value, '0');
        std::string digits;
        digits.reserve(sample.width);
        for (std::size_t index = 0U; index < sample.width; ++index) {
            digits.push_back(unknown[index] == '0'
                    ? value[index]
                    : value[index] == '1' ? 'x'
                                          : 'z');
        }
        return std::to_string(sample.width) + "'"
            + (sample.signed_value ? "s" : "") + "b" + digits;
    }

    [[nodiscard]] std::string bin_identity(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageDeclaration& coverpoint,
        const SystemVerilogCoverageBin& bin,
        const SystemVerilogCoverageSampleValue* automatic_value)
    {
        const auto& origin = coverpoint.origin_covergroup_identity.empty()
            ? declaration.canonical_identity
            : coverpoint.origin_covergroup_identity;
        auto identity = origin + "::" + coverpoint.name
            + "." + bin.name;
        if (automatic_value != nullptr) {
            identity += "[" + sample_identity(*automatic_value) + "]";
        }
        return identity;
    }

    bool record_hit(
        SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageDeclaration& coverpoint,
        const SystemVerilogCoverageBin& bin,
        const SystemVerilogCoverageSampleValue& value,
        SystemVerilogCoverageSampleResult& result,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto automatic_value
            = bin.selection == SystemVerilogCoverageBinSelection::Automatic
            ? &value
            : nullptr;
        auto identity = bin_identity(
            declaration, coverpoint, bin, automatic_value);
        auto hit = std::ranges::find(
            instance.bin_hits, identity, &SystemVerilogCoverageBinHit::identity);
        if (hit == instance.bin_hits.end()) {
            SystemVerilogCoverageBinHit created;
            created.coverage_declaration_index = coverpoint.declaration_index;
            created.bin_declaration_index = bin.declaration_index;
            created.identity = identity;
            if (automatic_value != nullptr) {
                created.automatic_value = value.value;
                created.automatic_value_bits = value.value_bits;
                created.automatic_unknown_bits = value.unknown_bits;
                created.automatic_width = value.width;
                created.automatic_signed = value.signed_value;
            }
            created.hit_count = 1U;
            created.at_least = bin.at_least;
            created.covered = created.hit_count >= created.at_least;
            result.threshold_reached = created.covered;
            instance.bin_hits.push_back(std::move(created));
        } else {
            if (hit->hit_count == std::numeric_limits<std::uint64_t>::max()) {
                result.hit_count_overflow = true;
                diagnostics.push_back(Diagnostic {
                    DiagnosticSeverity::Error,
                    "FSIM-SV-COV-002",
                    "coverage hit count overflow for bin '" + identity + "'",
                    bin.span,
                    { } });
                return false;
            }
            ++hit->hit_count;
            hit->covered = hit->hit_count >= hit->at_least;
            result.threshold_reached = hit->covered;
        }
        result.hit_bin_identities.push_back(std::move(identity));
        return true;
    }

    [[nodiscard]] int compare_coverage_planes(
        const std::string_view left,
        const std::string_view right,
        const bool signed_value)
    {
        if (signed_value && left.front() != right.front()) {
            return left.front() == '1' ? -1 : 1;
        }
        if (left < right) {
            return -1;
        }
        return left == right ? 0 : 1;
    }

    [[nodiscard]] bool wide_value_match(
        const SystemVerilogCoverageBinValue& candidate,
        const SystemVerilogCoverageSampleValue& sample)
    {
        if (sample.width == 0U) {
            return false;
        }
        auto raw_sample_value = sample.value_bits;
        auto raw_sample_unknown = sample.unknown_bits;
        if (raw_sample_value.empty()) {
            if (sample.width > 64U) {
                return false;
            }
            raw_sample_value = scalar_coverage_plane(
                static_cast<std::uint64_t>(sample.value), sample.width);
            raw_sample_unknown = scalar_coverage_plane(
                sample.unknown_mask, sample.width);
        } else if (raw_sample_unknown.empty()) {
            raw_sample_unknown.assign(raw_sample_value.size(), '0');
        }
        if (!valid_binary_plane(raw_sample_value)
            || !valid_binary_plane(raw_sample_unknown)) {
            return false;
        }
        auto sample_value = normalized_coverage_plane(
            std::move(raw_sample_value), sample.width, sample.signed_value, '0');
        auto sample_unknown = normalized_coverage_plane(
            std::move(raw_sample_unknown), sample.width, sample.signed_value, '0');

        if (!candidate.exact_bits.empty() || candidate.exact_value) {
            auto exact_bits = candidate.exact_bits;
            auto exact_unknown = candidate.exact_unknown_bits;
            if (exact_bits.empty()) {
                exact_bits = scalar_coverage_plane(
                    static_cast<std::uint64_t>(*candidate.exact_value),
                    candidate.width);
            }
            if (exact_unknown.empty()) {
                exact_unknown.assign(exact_bits.size(), '0');
            }
            if (!valid_binary_plane(exact_bits)
                || !valid_binary_plane(exact_unknown)) {
                return false;
            }
            const auto width = std::max<std::size_t>(
                candidate.width, sample.width);
            return normalized_coverage_plane(
                       std::move(exact_bits), width, candidate.exact_signed, '0')
                == normalized_coverage_plane(
                    std::move(sample_value), width, sample.signed_value, '0')
                && normalized_coverage_plane(
                       std::move(exact_unknown), width,
                       candidate.exact_signed, '0')
                == normalized_coverage_plane(
                    std::move(sample_unknown), width,
                    sample.signed_value, '0');
        }

        if ((!candidate.range_left_bits.empty()
                && !candidate.range_right_bits.empty())
            || (candidate.range_left && candidate.range_right)) {
            auto left_bits = candidate.range_left_bits;
            auto right_bits = candidate.range_right_bits;
            if (left_bits.empty()) {
                left_bits = scalar_coverage_plane(
                    static_cast<std::uint64_t>(*candidate.range_left),
                    candidate.width);
                right_bits = scalar_coverage_plane(
                    static_cast<std::uint64_t>(*candidate.range_right),
                    candidate.width);
            }
            if (!valid_binary_plane(left_bits)
                || !valid_binary_plane(right_bits)
                || sample_unknown.find('1') != std::string::npos) {
                return false;
            }
            const auto width = std::max<std::size_t>(
                { candidate.width, sample.width,
                    left_bits.size(), right_bits.size() });
            auto left = normalized_coverage_plane(
                std::move(left_bits), width,
                candidate.range_left_signed, '0');
            auto right = normalized_coverage_plane(
                std::move(right_bits), width,
                candidate.range_right_signed, '0');
            const bool signed_range = candidate.range_left_signed
                && candidate.range_right_signed;
            auto sampled = normalized_coverage_plane(
                std::move(sample_value), width,
                signed_range && sample.signed_value, '0');
            if (compare_coverage_planes(left, right, signed_range) > 0) {
                std::swap(left, right);
            }
            return compare_coverage_planes(sampled, left, signed_range) >= 0
                && compare_coverage_planes(sampled, right, signed_range) <= 0;
        }

        if ((!candidate.wildcard_value_bits.empty()
                && !candidate.wildcard_mask_bits.empty())
            || candidate.wildcard) {
            auto wildcard_bits = candidate.wildcard_value_bits;
            auto wildcard_mask = candidate.wildcard_mask_bits;
            if (wildcard_bits.empty()) {
                wildcard_bits = scalar_coverage_plane(
                    candidate.wildcard_value, candidate.width);
                wildcard_mask = scalar_coverage_plane(
                    candidate.wildcard_mask, candidate.width);
            }
            if (!valid_binary_plane(wildcard_bits)
                || !valid_binary_plane(wildcard_mask)) {
                return false;
            }
            const auto width = std::max<std::size_t>(
                candidate.width, sample.width);
            const auto expected = normalized_coverage_plane(
                std::move(wildcard_bits), width, false, '0');
            const auto mask = normalized_coverage_plane(
                std::move(wildcard_mask), width, false, '1');
            sample_value = normalized_coverage_plane(
                std::move(sample_value), width, sample.signed_value, '0');
            sample_unknown = normalized_coverage_plane(
                std::move(sample_unknown), width, sample.signed_value, '0');
            for (std::size_t index = 0U; index < width; ++index) {
                if (mask[index] == '1'
                    && (sample_unknown[index] == '1'
                        || sample_value[index] != expected[index])) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    bool values_match(
        const std::vector<SystemVerilogCoverageBinValue>& values,
        const SystemVerilogCoverageSampleValue& sample)
    {
        return std::ranges::any_of(
            values,
            [&](const SystemVerilogCoverageBinValue& candidate) {
                if (const auto sampled_real = sample.real_value()) {
                    if (!std::isfinite(*sampled_real)) {
                        return false;
                    }
                    if (candidate.exact_real_bits
                        && *sampled_real
                            == std::bit_cast<double>(
                                *candidate.exact_real_bits)) {
                        return true;
                    }
                    if (candidate.range_left_real_bits
                        && candidate.range_right_real_bits) {
                        const auto left = std::bit_cast<double>(
                            *candidate.range_left_real_bits);
                        const auto right = std::bit_cast<double>(
                            *candidate.range_right_real_bits);
                        const auto lower = std::min(left, right);
                        const auto upper = std::max(left, right);
                        const auto lower_inclusive = left <= right
                            ? candidate.range_left_inclusive
                            : candidate.range_right_inclusive;
                        const auto upper_inclusive = left <= right
                            ? candidate.range_right_inclusive
                            : candidate.range_left_inclusive;
                        return (lower_inclusive
                                ? *sampled_real >= lower
                                : *sampled_real > lower)
                            && (upper_inclusive
                                ? *sampled_real <= upper
                                : *sampled_real < upper);
                    }
                    return false;
                }
                if (!sample.value_bits.empty() || !candidate.exact_bits.empty()
                    || !candidate.exact_unknown_bits.empty()
                    || !candidate.range_left_bits.empty()
                    || !candidate.range_right_bits.empty()
                    || !candidate.wildcard_value_bits.empty()
                    || !candidate.wildcard_mask_bits.empty()) {
                    return wide_value_match(candidate, sample);
                }
                if (sample.unknown_mask == 0U
                    && candidate.exact_value == sample.value) {
                    return true;
                }
                if (candidate.range_left && candidate.range_right) {
                    const auto lower = std::min(
                        *candidate.range_left, *candidate.range_right);
                    const auto upper = std::max(
                        *candidate.range_left, *candidate.range_right);
                    if (sample.unknown_mask == 0U
                        && sample.value >= lower && sample.value <= upper) {
                        return true;
                    }
                }
                if (candidate.wildcard) {
                    const auto sampled = static_cast<std::uint64_t>(sample.value);
                    if ((sample.unknown_mask & candidate.wildcard_mask) != 0U) {
                        return false;
                    }
                    return (sampled & candidate.wildcard_mask)
                        == (candidate.wildcard_value & candidate.wildcard_mask);
                }
                return false;
            });
    }

    bool guard_allows(
        const std::vector<Token>& tokens,
        const SystemVerilogCoverageSampleValue& sample);

    bool exact_match(
        const SystemVerilogCoverageBin& bin,
        const SystemVerilogCoverageSampleValue& value)
    {
        const bool base_match = bin.values.empty()
            ? !bin.with_tokens.empty()
            : values_match(bin.values, value);
        return base_match && guard_allows(bin.with_tokens, value);
    }

    enum class GuardTruth { False,
        True,
        Unknown };

    struct GuardOperand {
        boost::multiprecision::cpp_int value { };
        std::optional<double> real;
        bool known { };
    };

    struct NamedGuardSample {
        std::string_view name;
        const SystemVerilogCoverageSampleValue* value { };
    };

    [[nodiscard]] GuardOperand sample_guard_operand(
        const SystemVerilogCoverageSampleValue& sample)
    {
        if (const auto real = sample.real_value()) {
            return std::isfinite(*real)
                ? GuardOperand { { }, *real, true }
                : GuardOperand { };
        }
        if (sample_has_unknown(sample))
            return { };
        auto bits = sample.value_bits;
        if (bits.empty()) {
            if (sample.width > 64U)
                return { };
            bits = scalar_coverage_plane(
                static_cast<std::uint64_t>(sample.value), sample.width);
        }
        bits = normalized_coverage_plane(
            std::move(bits), sample.width, sample.signed_value, '0');
        boost::multiprecision::cpp_int value { };
        for (const auto bit : bits) {
            value <<= 1U;
            if (bit == '1')
                ++value;
        }
        if (sample.signed_value && !bits.empty() && bits.front() == '1')
            value -= boost::multiprecision::cpp_int { 1 } << bits.size();
        return { std::move(value), std::nullopt, true };
    }

    [[nodiscard]] GuardOperand number_guard_operand(
        std::string spelling)
    {
        spelling.erase(
            std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
        if (spelling.empty())
            return { };
        if (spelling.find_first_of(".eE") != std::string::npos) {
            double value { };
            const auto parsed = std::from_chars(
                spelling.data(), spelling.data() + spelling.size(), value,
                std::chars_format::general);
            return parsed.ec == std::errc { }
                    && parsed.ptr == spelling.data() + spelling.size()
                    && std::isfinite(value)
                ? GuardOperand { { }, value, true }
                : GuardOperand { };
        }
        unsigned base { 10U };
        bool signed_value { };
        std::optional<std::size_t> width;
        auto digits = std::string_view { spelling };
        if (const auto apostrophe = spelling.find('\'');
            apostrophe != std::string::npos) {
            if (apostrophe != 0U) {
                std::size_t parsed_width { };
                const auto width_text = std::string_view { spelling }.substr(
                    0U, apostrophe);
                for (const auto character : width_text) {
                    if (!std::isdigit(static_cast<unsigned char>(character)))
                        return { };
                    if (parsed_width
                        > (std::numeric_limits<std::size_t>::max()
                              - static_cast<std::size_t>(character - '0'))
                            / 10U) {
                        return { };
                    }
                    parsed_width = parsed_width * 10U
                        + static_cast<std::size_t>(character - '0');
                }
                width = parsed_width;
            }
            auto position = apostrophe + 1U;
            if (position < spelling.size()
                && (spelling[position] == 's' || spelling[position] == 'S')) {
                signed_value = true;
                ++position;
            }
            if (position >= spelling.size())
                return { };
            const auto base_character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(spelling[position])));
            base = base_character == 'b' ? 2U
                : base_character == 'o'  ? 8U
                : base_character == 'd'  ? 10U
                : base_character == 'h'  ? 16U
                                         : 0U;
            if (base == 0U)
                return { };
            digits = std::string_view { spelling }.substr(position + 1U);
        }
        if (digits.empty())
            return { };
        boost::multiprecision::cpp_int value { };
        for (const auto character : digits) {
            unsigned digit { };
            if (character >= '0' && character <= '9')
                digit = static_cast<unsigned>(character - '0');
            else if (character >= 'a' && character <= 'f')
                digit = static_cast<unsigned>(character - 'a') + 10U;
            else if (character >= 'A' && character <= 'F')
                digit = static_cast<unsigned>(character - 'A') + 10U;
            else
                return { };
            if (digit >= base)
                return { };
            value *= base;
            value += digit;
        }
        if (signed_value && width && *width != 0U
            && ((value >> (*width - 1U)) & 1U) != 0U) {
            value -= boost::multiprecision::cpp_int { 1 } << *width;
        }
        return { std::move(value), std::nullopt, true };
    }

    std::span<const Token> strip_guard_parentheses(
        std::span<const Token> tokens)
    {
        while (tokens.size() >= 2U
            && tokens.front().kind == TokenKind::LeftParen
            && tokens.back().kind == TokenKind::RightParen) {
            int depth { };
            bool encloses_all { true };
            for (std::size_t index = 0; index < tokens.size(); ++index) {
                if (tokens[index].kind == TokenKind::LeftParen)
                    ++depth;
                if (tokens[index].kind == TokenKind::RightParen)
                    --depth;
                if (depth == 0 && index + 1U != tokens.size()) {
                    encloses_all = false;
                    break;
                }
            }
            if (!encloses_all)
                break;
            tokens = tokens.subspan(1U, tokens.size() - 2U);
        }
        return tokens;
    }

    std::optional<std::size_t> guard_operator(
        const std::span<const Token> tokens,
        const TokenKind sought)
    {
        int parentheses { };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen)
                ++parentheses;
            if (tokens[index].kind == TokenKind::RightParen)
                --parentheses;
            if (tokens[index].kind == sought && parentheses == 0)
                return index;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> guard_binary_operator(
        const std::span<const Token> tokens,
        const std::span<const TokenKind> sought)
    {
        int parentheses { };
        for (std::size_t index = tokens.size(); index-- > 0U;) {
            if (tokens[index].kind == TokenKind::RightParen)
                ++parentheses;
            if (tokens[index].kind == TokenKind::LeftParen)
                --parentheses;
            if (parentheses == 0 && index != 0U
                && std::ranges::find(sought, tokens[index].kind)
                    != sought.end()) {
                return index;
            }
        }
        return std::nullopt;
    }

    GuardOperand guard_operand(
        std::span<const Token> tokens,
        const SystemVerilogCoverageSampleValue& sample,
        const std::span<const NamedGuardSample> named_samples = { })
    {
        tokens = strip_guard_parentheses(tokens);
        constexpr TokenKind bitwise_or[] = { TokenKind::Pipe };
        constexpr TokenKind bitwise_xor[] = { TokenKind::Caret };
        constexpr TokenKind bitwise_and[] = { TokenKind::Ampersand };
        constexpr TokenKind shifts[] = {
            TokenKind::ShiftLeft, TokenKind::ArithmeticShiftLeft,
            TokenKind::ShiftRight, TokenKind::ArithmeticShiftRight
        };
        constexpr TokenKind additive[] = { TokenKind::Plus, TokenKind::Minus };
        constexpr TokenKind multiplicative[] = {
            TokenKind::Star, TokenKind::Slash, TokenKind::Percent
        };
        std::optional<std::size_t> operation;
        for (const auto operators : {
                 std::span<const TokenKind> { bitwise_or },
                 std::span<const TokenKind> { bitwise_xor },
                 std::span<const TokenKind> { bitwise_and },
                 std::span<const TokenKind> { shifts },
                 std::span<const TokenKind> { additive },
                 std::span<const TokenKind> { multiplicative } }) {
            operation = guard_binary_operator(tokens, operators);
            if (operation)
                break;
        }
        if (operation) {
            const auto left = guard_operand(
                tokens.first(*operation), sample, named_samples);
            const auto right = guard_operand(
                tokens.subspan(*operation + 1U), sample, named_samples);
            if (!left.known || !right.known) {
                return { };
            }
            const auto kind = tokens[*operation].kind;
            if (left.real || right.real) {
                if (kind == TokenKind::Pipe || kind == TokenKind::Caret
                    || kind == TokenKind::Ampersand
                    || kind == TokenKind::Percent
                    || kind == TokenKind::ShiftLeft
                    || kind == TokenKind::ArithmeticShiftLeft
                    || kind == TokenKind::ShiftRight
                    || kind == TokenKind::ArithmeticShiftRight) {
                    return { };
                }
                const auto left_value = left.real.value_or(
                    left.value.convert_to<double>());
                const auto right_value = right.real.value_or(
                    right.value.convert_to<double>());
                if (kind == TokenKind::Slash && right_value == 0.0) {
                    return { };
                }
                const auto value = kind == TokenKind::Plus
                    ? left_value + right_value
                    : kind == TokenKind::Minus
                    ? left_value - right_value
                    : kind == TokenKind::Star
                    ? left_value * right_value
                    : left_value / right_value;
                return std::isfinite(value)
                    ? GuardOperand { { }, value, true }
                    : GuardOperand { };
            }
            if ((kind == TokenKind::Slash || kind == TokenKind::Percent)
                && right.value == 0) {
                return { };
            }
            if (kind == TokenKind::Pipe)
                return { left.value | right.value, std::nullopt, true };
            if (kind == TokenKind::Caret)
                return { left.value ^ right.value, std::nullopt, true };
            if (kind == TokenKind::Ampersand)
                return { left.value & right.value, std::nullopt, true };
            if (kind == TokenKind::Plus)
                return { left.value + right.value, std::nullopt, true };
            if (kind == TokenKind::Minus)
                return { left.value - right.value, std::nullopt, true };
            if (kind == TokenKind::Star)
                return { left.value * right.value, std::nullopt, true };
            if (kind == TokenKind::Slash)
                return { left.value / right.value, std::nullopt, true };
            if (kind == TokenKind::Percent)
                return { left.value % right.value, std::nullopt, true };
            if (right.value < 0
                || right.value
                    > std::numeric_limits<std::size_t>::max()) {
                return { };
            }
            const auto amount = right.value.convert_to<std::size_t>();
            if (kind == TokenKind::ShiftLeft
                || kind == TokenKind::ArithmeticShiftLeft) {
                return { left.value << amount, std::nullopt, true };
            }
            return { left.value >> amount, std::nullopt, true };
        }
        if (tokens.size() == 1U && tokens.front().kind == TokenKind::Identifier) {
            if (!named_samples.empty()) {
                const auto found = std::ranges::find(
                    named_samples,
                    tokens.front().text,
                    &NamedGuardSample::name);
                return found != named_samples.end() && found->value
                    ? sample_guard_operand(*found->value)
                    : GuardOperand { };
            }
            return sample_guard_operand(sample);
        }
        bool negative { };
        if (tokens.size() == 2U && tokens.front().kind == TokenKind::Minus) {
            negative = true;
            tokens = tokens.subspan(1U);
        }
        if (tokens.size() != 1U || tokens.front().kind != TokenKind::Number) {
            return { };
        }
        auto value = number_guard_operand(tokens.front().text);
        if (negative && value.known) {
            if (value.real) value.real = -*value.real;
            else value.value = -value.value;
        }
        return value;
    }

    GuardTruth evaluate_guard(
        std::span<const Token> tokens,
        const SystemVerilogCoverageSampleValue& sample,
        const std::span<const NamedGuardSample> named_samples = { })
    {
        tokens = strip_guard_parentheses(tokens);
        if (tokens.empty())
            return GuardTruth::True;
        if (const auto operation = guard_operator(tokens, TokenKind::OrOr)) {
            const auto left = evaluate_guard(
                tokens.first(*operation), sample, named_samples);
            const auto right = evaluate_guard(
                tokens.subspan(*operation + 1U), sample, named_samples);
            if (left == GuardTruth::True || right == GuardTruth::True) {
                return GuardTruth::True;
            }
            return left == GuardTruth::Unknown || right == GuardTruth::Unknown
                ? GuardTruth::Unknown
                : GuardTruth::False;
        }
        if (const auto operation = guard_operator(tokens, TokenKind::AndAnd)) {
            const auto left = evaluate_guard(
                tokens.first(*operation), sample, named_samples);
            const auto right = evaluate_guard(
                tokens.subspan(*operation + 1U), sample, named_samples);
            if (left == GuardTruth::False || right == GuardTruth::False) {
                return GuardTruth::False;
            }
            return left == GuardTruth::Unknown || right == GuardTruth::Unknown
                ? GuardTruth::Unknown
                : GuardTruth::True;
        }
        if (tokens.front().text == "!") {
            const auto inner = evaluate_guard(
                tokens.subspan(1U), sample, named_samples);
            return inner == GuardTruth::True
                ? GuardTruth::False
                : inner == GuardTruth::False
                ? GuardTruth::True
                : GuardTruth::Unknown;
        }
        constexpr TokenKind comparisons[] = {
            TokenKind::EqualEqual, TokenKind::CaseEqual, TokenKind::NotEqual,
            TokenKind::CaseNotEqual, TokenKind::Less, TokenKind::LessEqual,
            TokenKind::Greater, TokenKind::GreaterEqual
        };
        for (const auto comparison : comparisons) {
            const auto operation = guard_operator(tokens, comparison);
            if (!operation)
                continue;
            const auto left = guard_operand(
                tokens.first(*operation), sample, named_samples);
            const auto right = guard_operand(
                tokens.subspan(*operation + 1U), sample, named_samples);
            if (!left.known || !right.known)
                return GuardTruth::Unknown;
            if (left.real || right.real) {
                const auto left_value = left.real.value_or(
                    left.value.convert_to<double>());
                const auto right_value = right.real.value_or(
                    right.value.convert_to<double>());
                const bool result = comparison == TokenKind::EqualEqual
                        || comparison == TokenKind::CaseEqual
                    ? left_value == right_value
                    : comparison == TokenKind::NotEqual
                            || comparison == TokenKind::CaseNotEqual
                    ? left_value != right_value
                    : comparison == TokenKind::Less
                    ? left_value < right_value
                    : comparison == TokenKind::LessEqual
                    ? left_value <= right_value
                    : comparison == TokenKind::Greater
                    ? left_value > right_value
                    : left_value >= right_value;
                return result ? GuardTruth::True : GuardTruth::False;
            }
            bool result { };
            if (comparison == TokenKind::EqualEqual
                || comparison == TokenKind::CaseEqual) {
                result = left.value == right.value;
            } else if (
                comparison == TokenKind::NotEqual
                || comparison == TokenKind::CaseNotEqual) {
                result = left.value != right.value;
            } else if (comparison == TokenKind::Less) {
                result = left.value < right.value;
            } else if (comparison == TokenKind::LessEqual) {
                result = left.value <= right.value;
            } else if (comparison == TokenKind::Greater) {
                result = left.value > right.value;
            } else {
                result = left.value >= right.value;
            }
            return result ? GuardTruth::True : GuardTruth::False;
        }
        const auto operand = guard_operand(tokens, sample, named_samples);
        if (!operand.known)
            return GuardTruth::Unknown;
        return operand.real ? (*operand.real != 0.0 ? GuardTruth::True
                                                   : GuardTruth::False)
            : operand.value != 0 ? GuardTruth::True : GuardTruth::False;
    }

    bool guard_allows(
        const std::vector<Token>& tokens,
        const SystemVerilogCoverageSampleValue& sample)
    {
        return tokens.empty()
            || evaluate_guard(tokens, sample) == GuardTruth::True;
    }

    void append_unique_progress(
        std::vector<SystemVerilogCoverageTransitionProgress>& progress,
        SystemVerilogCoverageTransitionProgress candidate)
    {
        const auto duplicate = std::ranges::any_of(
            progress,
            [&](const SystemVerilogCoverageTransitionProgress& existing) {
                return existing.coverage_declaration_index
                    == candidate.coverage_declaration_index
                    && existing.bin_declaration_index
                    == candidate.bin_declaration_index
                    && existing.sequence_index == candidate.sequence_index
                    && existing.step_index == candidate.step_index
                    && existing.repetition_count == candidate.repetition_count
                    && existing.samples_since_step == candidate.samples_since_step;
            });
        if (!duplicate)
            progress.push_back(std::move(candidate));
    }

    bool advance_transition_bin(
        SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCoverageDeclaration& coverpoint,
        const SystemVerilogCoverageBin& bin,
        const SystemVerilogCoverageSampleValue& value)
    {
        std::vector<SystemVerilogCoverageTransitionProgress> retained;
        std::vector<SystemVerilogCoverageTransitionProgress> active;
        for (const auto& progress : instance.transition_progress) {
            if (progress.coverage_declaration_index
                    == coverpoint.declaration_index
                && progress.bin_declaration_index == bin.declaration_index) {
                active.push_back(progress);
            } else {
                retained.push_back(progress);
            }
        }

        bool completed { };
        for (const auto& progress : active) {
            if (progress.sequence_index >= bin.transitions.size())
                continue;
            const auto& sequence = bin.transitions[progress.sequence_index];
            if (progress.step_index >= sequence.steps.size())
                continue;
            const auto& step = sequence.steps[progress.step_index];
            const auto maximum = step.repetition.maximum.value_or(65'536U);
            const auto repeat_kind = step.repetition.kind;
            const bool repeating = repeat_kind
                != SystemVerilogCoverageTransitionRepetitionKind::None;
            const bool step_matches = values_match(step.values, value);
            if (repeating && step_matches
                && progress.repetition_count < maximum) {
                auto repeated = progress;
                ++repeated.repetition_count;
                repeated.samples_since_step = 0U;
                if (repeated.step_index + 1U == sequence.steps.size()
                    && repeated.repetition_count >= step.repetition.minimum) {
                    completed = true;
                } else {
                    append_unique_progress(retained, std::move(repeated));
                }
            }

            const bool repetition_satisfied = progress.repetition_count >= step.repetition.minimum;
            if (progress.step_index + 1U < sequence.steps.size()
                && repetition_satisfied) {
                const auto age = progress.samples_since_step + 1U;
                const auto& delay = sequence.delays[progress.step_index];
                const bool nonconsecutive = repeat_kind
                    == SystemVerilogCoverageTransitionRepetitionKind::Nonconsecutive;
                const bool within_delay = age >= delay.minimum
                    && (nonconsecutive || age <= delay.maximum);
                const auto& next_step = sequence.steps[progress.step_index + 1U];
                if (within_delay && values_match(next_step.values, value)) {
                    auto advanced = progress;
                    ++advanced.step_index;
                    advanced.repetition_count = 1U;
                    advanced.samples_since_step = 0U;
                    if (advanced.step_index + 1U == sequence.steps.size()
                        && advanced.repetition_count
                            >= next_step.repetition.minimum) {
                        completed = true;
                    } else {
                        append_unique_progress(retained, std::move(advanced));
                    }
                }
                const bool wait_for_delay = age < delay.maximum;
                const bool wait_for_more_repetitions = repeating
                    && repeat_kind
                        != SystemVerilogCoverageTransitionRepetitionKind::Consecutive
                    && progress.repetition_count < maximum;
                if (wait_for_delay || nonconsecutive || wait_for_more_repetitions) {
                    auto waiting = progress;
                    waiting.samples_since_step = age;
                    append_unique_progress(retained, std::move(waiting));
                }
            } else if (repeating
                && repeat_kind
                    != SystemVerilogCoverageTransitionRepetitionKind::Consecutive
                && progress.repetition_count < maximum) {
                auto waiting = progress;
                ++waiting.samples_since_step;
                append_unique_progress(retained, std::move(waiting));
            }
        }

        for (std::size_t sequence_index = 0;
            sequence_index < bin.transitions.size(); ++sequence_index) {
            const auto& sequence = bin.transitions[sequence_index];
            if (sequence.steps.empty()
                || !values_match(sequence.steps.front().values, value)) {
                continue;
            }
            SystemVerilogCoverageTransitionProgress started;
            started.coverage_declaration_index = coverpoint.declaration_index;
            started.bin_declaration_index = bin.declaration_index;
            started.sequence_index = sequence_index;
            started.repetition_count = 1U;
            append_unique_progress(retained, std::move(started));
        }
        instance.transition_progress = std::move(retained);
        return completed;
    }

    void reset_transition_bin(
        SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCoverageDeclaration& coverpoint,
        const SystemVerilogCoverageBin& bin)
    {
        std::erase_if(
            instance.transition_progress,
            [&](const SystemVerilogCoverageTransitionProgress& progress) {
                return progress.coverage_declaration_index
                    == coverpoint.declaration_index
                    && progress.bin_declaration_index == bin.declaration_index;
            });
    }

    bool cross_intersect_match(
        const std::span<const Token> tokens,
        const SystemVerilogCoverageSampleValue& sample)
    {
        const auto sampled = sample_guard_operand(sample);
        if (!sampled.known || tokens.size() < 3U
            || tokens.front().kind != TokenKind::LeftBrace
            || tokens.back().kind != TokenKind::RightBrace) {
            return false;
        }
        const auto values = tokens.subspan(1U, tokens.size() - 2U);
        std::size_t begin { };
        int brackets { };
        for (std::size_t index = 0; index <= values.size(); ++index) {
            const bool separator = index == values.size()
                || (values[index].kind == TokenKind::Comma && brackets == 0);
            if (!separator) {
                if (values[index].kind == TokenKind::LeftBracket)
                    ++brackets;
                if (values[index].kind == TokenKind::RightBracket)
                    --brackets;
                continue;
            }
            const auto candidate = values.subspan(begin, index - begin);
            if (candidate.size() == 1U) {
                const auto operand = guard_operand(candidate, sample);
                if (operand.known && operand.value == sampled.value)
                    return true;
            } else if (candidate.size() == 5U
                && candidate.front().kind == TokenKind::LeftBracket
                && candidate[2].kind == TokenKind::Colon
                && candidate.back().kind == TokenKind::RightBracket) {
                const auto left = guard_operand(candidate.subspan(1U, 1U), sample);
                const auto right = guard_operand(candidate.subspan(3U, 1U), sample);
                if (left.known && right.known) {
                    const auto lower = std::min(left.value, right.value);
                    const auto upper = std::max(left.value, right.value);
                    if (sampled.value >= lower && sampled.value <= upper)
                        return true;
                }
            }
            begin = index + 1U;
        }
        return false;
    }

    struct CrossFactorDomain {
        std::string_view name;
        std::vector<SystemVerilogCoverageSampleValue> values;
    };

    [[nodiscard]] std::optional<std::size_t> top_level_keyword(
        const std::span<const Token> tokens,
        const std::string_view keyword)
    {
        int parentheses { };
        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen)
                ++parentheses;
            if (tokens[index].kind == TokenKind::RightParen)
                --parentheses;
            if (parentheses == 0 && tokens[index].text == keyword)
                return index;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> matching_parenthesis(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        if (left >= tokens.size()
            || tokens[left].kind != TokenKind::LeftParen) {
            return std::nullopt;
        }
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen)
                ++depth;
            if (tokens[index].kind == TokenKind::RightParen)
                --depth;
            if (depth == 0)
                return index;
        }
        return std::nullopt;
    }

    [[nodiscard]] SystemVerilogCoverageSampleValue bin_exact_sample(
        const SystemVerilogCoverageBinValue& value)
    {
        auto unknown_mask = std::uint64_t { };
        if (value.exact_unknown_bits.empty() && value.width <= 64U) {
            unknown_mask = value.wildcard ? value.wildcard_mask : 0U;
        }
        return {
            value.exact_value.value_or(0), unknown_mask, value.width,
            value.exact_bits, value.exact_unknown_bits, value.exact_signed
        };
    }

    [[nodiscard]] std::string coverage_integer_plane(
        boost::multiprecision::cpp_int value,
        const std::size_t width)
    {
        const auto modulus = boost::multiprecision::cpp_int { 1 } << width;
        value %= modulus;
        if (value < 0)
            value += modulus;
        std::string result(width, '0');
        for (std::size_t offset = 0U; offset < width; ++offset) {
            const auto bit = width - offset - 1U;
            result[offset] = ((value >> bit) & 1U) != 0U ? '1' : '0';
        }
        return result;
    }

    [[nodiscard]] bool append_cross_candidate(
        std::vector<SystemVerilogCoverageSampleValue>& result,
        std::unordered_set<std::string>& identities,
        SystemVerilogCoverageSampleValue candidate,
        const SystemVerilogCoverageBin& bin)
    {
        if (!guard_allows(bin.with_tokens, candidate))
            return true;
        const auto identity = sample_identity(candidate);
        if (identities.insert(identity).second) {
            if (result.size() >= kSystemVerilogCoverageMaximumWork)
                return false;
            result.push_back(std::move(candidate));
        }
        return true;
    }

    [[nodiscard]] std::optional<std::vector<SystemVerilogCoverageSampleValue>>
    cross_bin_domain(
        const SystemVerilogCoverageBin& bin,
        const SystemVerilogCoverageSampleValue& current,
        const SystemVerilogCoverageDeclaration& coverpoint)
    {
        std::vector<SystemVerilogCoverageSampleValue> result;
        std::unordered_set<std::string> identities;
        if (bin.selection == SystemVerilogCoverageBinSelection::Automatic) {
            result.push_back(current);
            return result;
        }
        if (bin.selection == SystemVerilogCoverageBinSelection::Default) {
            if (current.width == 0U
                || current.width >= std::numeric_limits<std::size_t>::digits) {
                return std::nullopt;
            }
            const auto combinations = std::size_t { 1 } << current.width;
            if (combinations > kSystemVerilogCoverageMaximumWork)
                return std::nullopt;
            for (std::size_t value = 0U; value < combinations; ++value) {
                SystemVerilogCoverageSampleValue candidate {
                    static_cast<std::int64_t>(value), 0U, current.width,
                    { }, { }, current.signed_value
                };
                const auto claimed = std::ranges::any_of(
                    coverpoint.bins,
                    [&](const SystemVerilogCoverageBin& other) {
                        return &other != &bin
                            && other.selection
                            == SystemVerilogCoverageBinSelection::Explicit
                            && other.transitions.empty()
                            && guard_allows(other.iff_tokens, candidate)
                            && exact_match(other, candidate);
                    });
                if (!claimed
                    && !append_cross_candidate(
                        result, identities, std::move(candidate), bin)) {
                    return std::nullopt;
                }
            }
            return result;
        }
        if (bin.selection != SystemVerilogCoverageBinSelection::Explicit
            || !bin.transitions.empty() || bin.values.empty()) {
            return std::nullopt;
        }
        for (const auto& value : bin.values) {
            if (value.wildcard) {
                auto bits = value.wildcard_value_bits;
                auto mask = value.wildcard_mask_bits;
                if (bits.empty() || mask.empty()) {
                    bits = scalar_coverage_plane(
                        value.wildcard_value, value.width);
                    mask = scalar_coverage_plane(
                        value.wildcard_mask, value.width);
                }
                if (!valid_binary_plane(bits) || !valid_binary_plane(mask)
                    || bits.size() != value.width
                    || mask.size() != value.width) {
                    return std::nullopt;
                }
                std::vector<std::size_t> variable_bits;
                for (std::size_t index = 0U; index < mask.size(); ++index) {
                    if (mask[index] == '0')
                        variable_bits.push_back(index);
                }
                std::size_t combinations { 1U };
                for ([[maybe_unused]] const auto index : variable_bits) {
                    if (combinations
                        > kSystemVerilogCoverageMaximumWork / 2U) {
                        return std::nullopt;
                    }
                    combinations *= 2U;
                }
                for (std::size_t combination = 0U;
                    combination < combinations; ++combination) {
                    auto candidate_bits = bits;
                    for (std::size_t bit = 0U; bit < variable_bits.size(); ++bit) {
                        candidate_bits[variable_bits[bit]]
                            = ((combination >> bit) & 1U) != 0U ? '1' : '0';
                    }
                    SystemVerilogCoverageSampleValue candidate {
                        0, 0U, value.width,
                        value.width > 64U ? candidate_bits : std::string { },
                        { }, false
                    };
                    if (value.width <= 64U) {
                        std::uint64_t scalar { };
                        for (const auto bit : candidate_bits) {
                            scalar = (scalar << 1U)
                                | static_cast<std::uint64_t>(bit == '1');
                        }
                        candidate.value = static_cast<std::int64_t>(scalar);
                    }
                    if (!append_cross_candidate(
                            result, identities, std::move(candidate), bin)) {
                        return std::nullopt;
                    }
                }
                continue;
            }
            if (value.exact_value || !value.exact_bits.empty()
                || !value.exact_unknown_bits.empty()) {
                if (!append_cross_candidate(
                        result, identities, bin_exact_sample(value), bin)) {
                    return std::nullopt;
                }
                continue;
            }
            if ((!value.range_left || !value.range_right)
                && (value.range_left_bits.empty()
                    || value.range_right_bits.empty())) {
                return std::nullopt;
            }
            SystemVerilogCoverageSampleValue left {
                value.range_left.value_or(0), 0U, value.width,
                value.range_left_bits, { }, value.range_left_signed
            };
            SystemVerilogCoverageSampleValue right {
                value.range_right.value_or(0), 0U, value.width,
                value.range_right_bits, { }, value.range_right_signed
            };
            const auto left_value = sample_guard_operand(left);
            const auto right_value = sample_guard_operand(right);
            if (!left_value.known || !right_value.known)
                return std::nullopt;
            const auto ascending = left_value.value <= right_value.value;
            auto candidate_value = left_value.value;
            for (;;) {
                const auto bits = coverage_integer_plane(
                    candidate_value, value.width);
                SystemVerilogCoverageSampleValue candidate {
                    0, 0U, value.width,
                    value.width > 64U ? bits : std::string { }, { },
                    value.range_left_signed && value.range_right_signed
                };
                if (value.width <= 64U) {
                    std::uint64_t scalar { };
                    for (const auto bit : bits) {
                        scalar = (scalar << 1U)
                            | static_cast<std::uint64_t>(bit == '1');
                    }
                    candidate.value = static_cast<std::int64_t>(scalar);
                }
                if (!append_cross_candidate(
                        result, identities, std::move(candidate), bin))
                    return std::nullopt;
                if (candidate_value == right_value.value)
                    break;
                candidate_value += ascending ? 1 : -1;
                if (result.size() >= kSystemVerilogCoverageMaximumWork)
                    return std::nullopt;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::vector<CrossFactorDomain>>
    cross_factor_domains(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageDeclaration& cross,
        const SystemVerilogCovergroupSampleResult& sampled,
        const std::span<const SystemVerilogCovergroupSampleInput> inputs,
        bool& work_exceeded)
    {
        std::vector<CrossFactorDomain> result;
        result.reserve(cross.cross_operands.size());
        std::size_t product { 1U };
        for (const auto& operand : cross.cross_operands) {
            if (!operand.resolved_declaration_index
                || *operand.resolved_declaration_index
                    >= declaration.coverage_declarations.size()) {
                return std::nullopt;
            }
            const auto sampled_coverpoint = std::ranges::find(
                sampled.coverpoints,
                *operand.resolved_declaration_index,
                &SystemVerilogCovergroupSampledCoverpoint::coverage_declaration_index);
            const auto input = std::ranges::find(
                inputs,
                *operand.resolved_declaration_index,
                &SystemVerilogCovergroupSampleInput::coverage_declaration_index);
            if (sampled_coverpoint == sampled.coverpoints.end()
                || !sampled_coverpoint->result.selected_bin_identity
                || input == inputs.end()) {
                return std::nullopt;
            }
            const auto& coverpoint = declaration.coverage_declarations[*operand.resolved_declaration_index];
            const auto selected_bin = std::ranges::find_if(
                coverpoint.bins,
                [&](const SystemVerilogCoverageBin& bin) {
                    const auto automatic_value = bin.selection
                            == SystemVerilogCoverageBinSelection::Automatic
                        ? &input->value
                        : nullptr;
                    return bin_identity(
                               declaration, coverpoint, bin, automatic_value)
                        == *sampled_coverpoint->result.selected_bin_identity;
                });
            if (selected_bin == coverpoint.bins.end())
                return std::nullopt;
            auto domain = cross_bin_domain(
                *selected_bin, input->value, coverpoint);
            if (!domain || domain->empty()
                || domain->size()
                    > kSystemVerilogCoverageMaximumWork / product) {
                work_exceeded = true;
                return std::nullopt;
            }
            product *= domain->size();
            result.push_back({ operand.name, std::move(*domain) });
        }
        return result;
    }

    [[nodiscard]] bool cross_with_matches(
        const std::span<const Token> predicate,
        const std::span<const CrossFactorDomain> factors,
        const boost::multiprecision::cpp_int& required,
        const bool match_all)
    {
        if (factors.empty())
            return false;
        std::vector<NamedGuardSample> samples(factors.size());
        boost::multiprecision::cpp_int matches { };
        bool failed_all { };
        const auto visit = [&](const auto& self, const std::size_t index) -> bool {
            if (index == factors.size()) {
                const auto truth = evaluate_guard(
                    predicate, *samples.front().value, samples);
                if (truth == GuardTruth::True) {
                    ++matches;
                    return !match_all && matches >= required;
                }
                failed_all = true;
                return match_all;
            }
            samples[index].name = factors[index].name;
            for (const auto& value : factors[index].values) {
                samples[index].value = &value;
                if (self(self, index + 1U))
                    return true;
            }
            return false;
        };
        if (required == 0)
            return true;
        const auto short_circuited = visit(visit, 0U);
        return match_all ? !failed_all : short_circuited;
    }

    bool cross_selection_match(
        std::span<const Token> tokens,
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageDeclaration& cross,
        const SystemVerilogCovergroupSampleResult& sampled,
        const std::span<const SystemVerilogCovergroupSampleInput> inputs,
        bool& work_exceeded)
    {
        tokens = strip_guard_parentheses(tokens);
        if (tokens.empty())
            return false;
        if (const auto with = top_level_keyword(tokens, "with")) {
            const auto left = *with + 1U;
            const auto right = matching_parenthesis(tokens, left);
            if (*with == 0U || !right || *right == left + 1U)
                return false;
            boost::multiprecision::cpp_int required { 1 };
            bool match_all { };
            auto suffix = *right + 1U;
            if (suffix < tokens.size()) {
                if (tokens[suffix].text != "matches")
                    return false;
                auto matches_tokens = strip_guard_parentheses(
                    tokens.subspan(suffix + 1U));
                if (matches_tokens.size() == 1U
                    && matches_tokens.front().text == "$") {
                    match_all = true;
                } else {
                    const auto parsed = guard_operand(
                        matches_tokens,
                        SystemVerilogCoverageSampleValue { });
                    if (!parsed.known || parsed.value < 0)
                        return false;
                    required = parsed.value;
                }
            }
            if (!cross_selection_match(
                    tokens.first(*with), declaration, cross, sampled, inputs,
                    work_exceeded)) {
                return false;
            }
            const auto factors = cross_factor_domains(
                declaration, cross, sampled, inputs, work_exceeded);
            return factors && cross_with_matches(tokens.subspan(left + 1U, *right - left - 1U), *factors, required, match_all);
        }
        if (const auto operation = guard_operator(tokens, TokenKind::OrOr)) {
            return cross_selection_match(
                       tokens.first(*operation), declaration, cross, sampled,
                       inputs, work_exceeded)
                || cross_selection_match(
                    tokens.subspan(*operation + 1U), declaration, cross,
                    sampled, inputs, work_exceeded);
        }
        if (const auto operation = guard_operator(tokens, TokenKind::AndAnd)) {
            return cross_selection_match(
                       tokens.first(*operation), declaration, cross, sampled,
                       inputs, work_exceeded)
                && cross_selection_match(
                    tokens.subspan(*operation + 1U), declaration, cross,
                    sampled, inputs, work_exceeded);
        }
        if (tokens.front().text == "!") {
            return !cross_selection_match(
                tokens.subspan(1U), declaration, cross, sampled, inputs,
                work_exceeded);
        }
        if (tokens.size() == 1U && tokens.front().kind == TokenKind::Identifier
            && tokens.front().text == cross.name) {
            return true;
        }
        if (tokens.size() < 4U || tokens.front().text != "binsof"
            || tokens[1].kind != TokenKind::LeftParen
            || tokens[2].kind != TokenKind::Identifier) {
            return false;
        }
        std::size_t right { 2U };
        int depth { 1 };
        for (; right < tokens.size(); ++right) {
            if (tokens[right].kind == TokenKind::LeftParen)
                ++depth;
            if (tokens[right].kind == TokenKind::RightParen)
                --depth;
            if (depth == 0)
                break;
        }
        if (right >= tokens.size())
            return false;
        const auto operand = std::ranges::find(
            cross.cross_operands,
            tokens[2].text,
            &SystemVerilogCoverageCrossOperand::name);
        if (operand == cross.cross_operands.end()
            || !operand->resolved_declaration_index) {
            return false;
        }
        const auto coverpoint = std::ranges::find(
            sampled.coverpoints,
            *operand->resolved_declaration_index,
            &SystemVerilogCovergroupSampledCoverpoint::coverage_declaration_index);
        if (coverpoint == sampled.coverpoints.end()
            || !coverpoint->result.selected_bin_identity) {
            return false;
        }
        std::optional<std::string_view> bin_name;
        std::size_t suffix = right + 1U;
        if (right >= 5U && tokens[3].kind == TokenKind::Dot
            && tokens[4].kind == TokenKind::Identifier) {
            bin_name = tokens[4].text;
        } else if (suffix + 1U < tokens.size()
            && tokens[suffix].kind == TokenKind::Dot
            && tokens[suffix + 1U].kind == TokenKind::Identifier) {
            bin_name = tokens[suffix + 1U].text;
            suffix += 2U;
        }
        if (bin_name) {
            const auto marker = "." + std::string { *bin_name };
            const auto position = coverpoint->result.selected_bin_identity->rfind(marker);
            if (position == std::string::npos)
                return false;
            const auto after = position + marker.size();
            if (after != coverpoint->result.selected_bin_identity->size()
                && (*coverpoint->result.selected_bin_identity)[after] != '[') {
                return false;
            }
        }
        if (suffix == tokens.size())
            return true;
        if (suffix + 1U >= tokens.size()
            || tokens[suffix].text != "intersect") {
            return false;
        }
        const auto input = std::ranges::find(
            inputs,
            *operand->resolved_declaration_index,
            &SystemVerilogCovergroupSampleInput::coverage_declaration_index);
        return input != inputs.end()
            && cross_intersect_match(tokens.subspan(suffix + 1U), input->value);
    }

} // namespace

bool systemverilog_coverage_with_allows(
    const std::span<const Token> tokens,
    const SystemVerilogCoverageSampleValue& value)
{
    return tokens.empty()
        || evaluate_guard(tokens, value) == GuardTruth::True;
}

SystemVerilogCoverageSampleResult sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::size_t coverage_declaration_index,
    const SystemVerilogCoverageSampleValue& value,
    std::vector<Diagnostic>& diagnostics)
{
    SystemVerilogCoverageSampleResult result;
    if (instance.declaration_identity != declaration.canonical_identity
        || coverage_declaration_index
            >= declaration.coverage_declarations.size()) {
        return result;
    }
    const auto& coverpoint = declaration.coverage_declarations[coverage_declaration_index];
    if (coverpoint.kind
        != SystemVerilogCoverageDeclarationKind::Coverpoint) {
        return result;
    }
    if (!guard_allows(coverpoint.iff_tokens, value))
        return result;

    auto previous = std::ranges::find(
        instance.previous_samples,
        coverpoint.declaration_index,
        &SystemVerilogCoveragePreviousSample::coverage_declaration_index);
    const bool had_previous = previous != instance.previous_samples.end();

    const SystemVerilogCoverageBin* regular_match { };
    const SystemVerilogCoverageBin* illegal_match { };
    const SystemVerilogCoverageBin* ignore_match { };
    bool has_transition_bins { };
    for (const auto& bin : coverpoint.bins) {
        if (bin.selection != SystemVerilogCoverageBinSelection::Explicit) {
            continue;
        }
        if (!guard_allows(bin.iff_tokens, value))
            continue;
        has_transition_bins = has_transition_bins || !bin.transitions.empty();
        const bool matched = bin.transitions.empty()
            ? exact_match(bin, value)
            : advance_transition_bin(instance, coverpoint, bin, value);
        if (!matched)
            continue;
        if (bin.kind == SystemVerilogCoverageBinKind::Ignore) {
            if (!ignore_match)
                ignore_match = &bin;
        } else if (bin.kind == SystemVerilogCoverageBinKind::Illegal) {
            if (!illegal_match)
                illegal_match = &bin;
        } else if (!regular_match) {
            regular_match = &bin;
        }
    }
    const SystemVerilogCoverageBin* selected = ignore_match
        ? ignore_match
        : illegal_match ? illegal_match
                        : regular_match;
    if (!selected) {
        const bool active_transition = std::ranges::any_of(
            instance.transition_progress,
            [&](const SystemVerilogCoverageTransitionProgress& progress) {
                return progress.coverage_declaration_index
                    == coverpoint.declaration_index;
            });
        const auto fallback_selection = has_transition_bins
            ? SystemVerilogCoverageBinSelection::DefaultSequence
            : SystemVerilogCoverageBinSelection::Default;
        const auto found = std::ranges::find_if(
            coverpoint.bins,
            [&](const SystemVerilogCoverageBin& bin) {
                return bin.selection == fallback_selection
                    && guard_allows(bin.iff_tokens, value)
                    && (!has_transition_bins
                        || (had_previous && !active_transition));
            });
        if (found != coverpoint.bins.end())
            selected = &*found;
    }
    if (!selected && !has_transition_bins && !sample_has_unknown(value)) {
        const auto found = std::ranges::find_if(
            coverpoint.bins,
            [&](const SystemVerilogCoverageBin& bin) {
                return bin.selection
                    == SystemVerilogCoverageBinSelection::Automatic
                    && guard_allows(bin.iff_tokens, value);
            });
        if (found != coverpoint.bins.end())
            selected = &*found;
    }

    if (!value.is_real() && !had_previous) {
        SystemVerilogCoveragePreviousSample created;
        created.coverage_declaration_index = coverpoint.declaration_index;
        created.value = value.value;
        created.unknown_mask = value.unknown_mask;
        created.width = value.width;
        created.value_bits = value.value_bits;
        created.unknown_bits = value.unknown_bits;
        created.signed_value = value.signed_value;
        instance.previous_samples.push_back(std::move(created));
    } else if (!value.is_real()) {
        previous->value = value.value;
        previous->unknown_mask = value.unknown_mask;
        previous->width = value.width;
        previous->value_bits = value.value_bits;
        previous->unknown_bits = value.unknown_bits;
        previous->signed_value = value.signed_value;
    }
    if (!selected)
        return result;
    const auto automatic_value
        = selected->selection == SystemVerilogCoverageBinSelection::Automatic
        ? &value
        : nullptr;
    result.selected_bin_identity = bin_identity(
        declaration, coverpoint, *selected, automatic_value);
    if (!selected->transitions.empty()) {
        reset_transition_bin(instance, coverpoint, *selected);
    }
    if (selected->weight == 0U) {
        result.zero_weight_excluded = true;
        return result;
    }
    if (selected->kind == SystemVerilogCoverageBinKind::Ignore) {
        result.ignored = true;
        return result;
    }

    if (!record_hit(
            instance,
            declaration,
            coverpoint,
            *selected,
            value,
            result,
            diagnostics)) {
        return result;
    }
    if (selected->kind == SystemVerilogCoverageBinKind::Illegal) {
        result.illegal = true;
        const auto identity = result.hit_bin_identities.back();
        SystemVerilogCoverageIllegalBinReport report;
        report.bin_identity = identity;
        report.sampled_value = value.value;
        report.sampled_unknown_mask = value.unknown_mask;
        report.sampled_width = value.width;
        report.sampled_value_bits = value.value_bits;
        report.sampled_unknown_bits = value.unknown_bits;
        report.sampled_signed = value.signed_value;
        report.sampled_scalar_kind = value.scalar_kind;
        report.sampled_scalar_bits = value.scalar_bits;
        report.span = selected->span;
        instance.illegal_bin_reports.push_back(std::move(report));
        diagnostics.push_back(Diagnostic {
            DiagnosticSeverity::Error,
            "FSIM-SV-COV-001",
            "sampled value " + sample_identity(value)
                + " matched illegal coverage bin '" + identity + "'",
            selected->span,
            { } });
    }
    return result;
}

SystemVerilogCoverageSampleResult sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::size_t coverage_declaration_index,
    const std::int64_t value,
    std::vector<Diagnostic>& diagnostics)
{
    return sample_systemverilog_coverpoint(
        instance,
        declaration,
        coverage_declaration_index,
        SystemVerilogCoverageSampleValue { value, 0U, 64U },
        diagnostics);
}

SystemVerilogCovergroupSampleResult sample_systemverilog_covergroup(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupSampleInput> inputs,
    std::vector<Diagnostic>& diagnostics)
{
    SystemVerilogCovergroupSampleResult result;
    if (instance.declaration_identity != declaration.canonical_identity) {
        return result;
    }
    if (!validate_systemverilog_coverage_sample_resources(
            instance, declaration, inputs.size(), diagnostics)) {
        return result;
    }
    std::vector<std::size_t> seen;
    seen.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (input.coverage_declaration_index
                >= declaration.coverage_declarations.size()
            || declaration.coverage_declarations[input.coverage_declaration_index].kind
                != SystemVerilogCoverageDeclarationKind::Coverpoint
            || std::ranges::find(seen, input.coverage_declaration_index)
                != seen.end()) {
            return { };
        }
        seen.push_back(input.coverage_declaration_index);
    }

    const auto has_cross_with = std::ranges::any_of(
        declaration.coverage_declarations,
        [](const SystemVerilogCoverageDeclaration& coverage) {
            return coverage.kind
                == SystemVerilogCoverageDeclarationKind::Cross
                && std::ranges::any_of(
                    coverage.bins,
                    [](const SystemVerilogCoverageBin& bin) {
                        return std::ranges::any_of(
                            bin.cross_selection_tokens,
                            [](const Token& token) {
                                return token.text == "with";
                            });
                    });
        });
    const auto original_instance = has_cross_with
        ? std::optional<SystemVerilogCovergroupInstance> { instance }
        : std::nullopt;
    const auto original_diagnostic_count = diagnostics.size();
    if (!initialize_systemverilog_cross_inventory(
            instance, declaration, diagnostics)) {
        return result;
    }

    result.coverpoints.reserve(inputs.size());
    for (const auto& input : inputs) {
        result.coverpoints.push_back({ input.coverage_declaration_index,
            sample_systemverilog_coverpoint(
                instance,
                declaration,
                input.coverage_declaration_index,
                input.value,
                diagnostics) });
    }

    for (const auto& cross : declaration.coverage_declarations) {
        if (cross.kind != SystemVerilogCoverageDeclarationKind::Cross)
            continue;
        std::vector<std::string> tuple;
        bool excluded { };
        bool complete { !cross.cross_operands.empty() };
        for (const auto& operand : cross.cross_operands) {
            if (!operand.resolved_declaration_index) {
                complete = false;
                break;
            }
            const auto sampled = std::ranges::find(
                result.coverpoints,
                *operand.resolved_declaration_index,
                &SystemVerilogCovergroupSampledCoverpoint::coverage_declaration_index);
            if (sampled == result.coverpoints.end()
                || !sampled->result.selected_bin_identity) {
                complete = false;
                break;
            }
            tuple.push_back(*sampled->result.selected_bin_identity);
            excluded = excluded || sampled->result.ignored || sampled->result.illegal;
        }
        if (!complete)
            continue;
        const SystemVerilogCoverageBin* regular_cross_bin { };
        const SystemVerilogCoverageBin* illegal_cross_bin { };
        const SystemVerilogCoverageBin* ignore_cross_bin { };
        for (const auto& bin : cross.bins) {
            bool work_exceeded { };
            if (!cross_selection_match(
                    bin.cross_selection_tokens, declaration, cross, result,
                    inputs, work_exceeded)) {
                if (work_exceeded && original_instance) {
                    instance = *original_instance;
                    diagnostics.resize(original_diagnostic_count);
                    diagnostics.push_back(Diagnostic {
                        DiagnosticSeverity::Error,
                        "FSIM-SV-COV-005",
                        "coverage cross selection exceeds the governed exact "
                        "candidate-domain work budget",
                        bin.span,
                        { } });
                    return { };
                }
                continue;
            }
            if (bin.kind == SystemVerilogCoverageBinKind::Ignore) {
                if (!ignore_cross_bin)
                    ignore_cross_bin = &bin;
            } else if (bin.kind == SystemVerilogCoverageBinKind::Illegal) {
                if (!illegal_cross_bin)
                    illegal_cross_bin = &bin;
            } else if (!regular_cross_bin) {
                regular_cross_bin = &bin;
            }
        }
        const auto selected_cross_bin = ignore_cross_bin
            ? ignore_cross_bin
            : illegal_cross_bin ? illegal_cross_bin
                                : regular_cross_bin;
        if (!cross.bins.empty() && !selected_cross_bin
            && !declaration.effective_cross_retain_auto_bins)
            continue;
        const auto weight = selected_cross_bin
            ? selected_cross_bin->weight
            : cross.effective_weight;
        const auto goal = selected_cross_bin
            ? selected_cross_bin->goal
            : cross.effective_goal;
        const auto at_least = selected_cross_bin
            ? selected_cross_bin->at_least
            : cross.effective_at_least;
        excluded = excluded || (selected_cross_bin && selected_cross_bin->kind != SystemVerilogCoverageBinKind::Regular);
        excluded = excluded || weight == 0U;
        const auto& origin = cross.origin_covergroup_identity.empty()
            ? declaration.canonical_identity
            : cross.origin_covergroup_identity;
        auto identity = origin + "::" + cross.name;
        if (selected_cross_bin) {
            identity += "." + selected_cross_bin->name;
        } else {
            identity += "<";
            for (std::size_t index = 0; index < tuple.size(); ++index) {
                if (index != 0U)
                    identity += ",";
                identity += tuple[index];
            }
            identity += ">";
        }
        auto state = std::ranges::find(
            instance.cross_bin_state,
            identity,
            &SystemVerilogCoverageCrossBinState::identity);
        if (state == instance.cross_bin_state.end()) {
            SystemVerilogCoverageCrossBinState created;
            created.coverage_declaration_index = cross.declaration_index;
            if (selected_cross_bin) {
                created.bin_declaration_index = selected_cross_bin->declaration_index;
            }
            created.identity = identity;
            created.operand_bin_identities = tuple;
            created.weight = weight;
            created.goal = goal;
            created.at_least = at_least;
            created.excluded = excluded;
            created.hit_count = excluded ? 0U : 1U;
            created.exclusion_count = excluded ? 1U : 0U;
            created.covered = !excluded && created.hit_count >= created.at_least;
            instance.cross_bin_state.push_back(std::move(created));
        } else if (excluded) {
            state->excluded = true;
            if (state->exclusion_count != std::numeric_limits<std::uint64_t>::max()) {
                ++state->exclusion_count;
            }
        } else {
            if (state->hit_count == std::numeric_limits<std::uint64_t>::max()) {
                diagnostics.push_back(Diagnostic {
                    DiagnosticSeverity::Error,
                    "FSIM-SV-COV-002",
                    "coverage hit count overflow for cross bin '" + identity + "'",
                    selected_cross_bin ? selected_cross_bin->span : cross.span,
                    { } });
                continue;
            }
            ++state->hit_count;
            state->covered = state->hit_count >= state->at_least;
        }
        if (excluded) {
            result.excluded_cross_bin_identities.push_back(std::move(identity));
        } else {
            result.hit_cross_bin_identities.push_back(std::move(identity));
        }
    }
    return result;
}

} // namespace fsim::frontend
