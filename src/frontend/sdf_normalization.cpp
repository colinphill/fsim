// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

    enum class DecimalStatus { Valid,
        Invalid,
        Overflow };

    struct DecimalResult {
        DecimalStatus status { DecimalStatus::Invalid };
        SdfExactDecimal value;
    };

    [[nodiscard]] std::string ascii_lower(const std::string_view text)
    {
        std::string result { text };
        std::ranges::transform(result, result.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return result;
    }

    [[nodiscard]] bool checked_add(const std::int64_t left,
        const std::int64_t right, std::int64_t& result) noexcept
    {
        if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::min() - right)) {
            return false;
        }
        result = left + right;
        return true;
    }

    [[nodiscard]] std::optional<std::int64_t> parse_exponent(
        std::string_view spelling) noexcept
    {
        bool negative = false;
        if (!spelling.empty() && (spelling.front() == '+' || spelling.front() == '-')) {
            negative = spelling.front() == '-';
            spelling.remove_prefix(1U);
        }
        if (spelling.empty())
            return std::nullopt;
        std::uint64_t magnitude = 0U;
        const auto converted = std::from_chars(
            spelling.data(), spelling.data() + spelling.size(), magnitude);
        if (converted.ec != std::errc { }
            || converted.ptr != spelling.data() + spelling.size()) {
            return std::nullopt;
        }
        constexpr auto maximum = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
        if ((!negative && magnitude > maximum)
            || (negative && magnitude > maximum + 1U)) {
            return std::nullopt;
        }
        if (!negative)
            return static_cast<std::int64_t>(magnitude);
        if (magnitude == maximum + 1U)
            return std::numeric_limits<std::int64_t>::min();
        return -static_cast<std::int64_t>(magnitude);
    }

    [[nodiscard]] DecimalResult parse_decimal(std::string_view spelling)
    {
        DecimalResult result;
        bool negative = false;
        if (!spelling.empty() && (spelling.front() == '+' || spelling.front() == '-')) {
            negative = spelling.front() == '-';
            spelling.remove_prefix(1U);
        }
        const auto exponent_position = spelling.find_first_of("eE");
        const auto significand = spelling.substr(0U, exponent_position);
        std::int64_t explicit_exponent = 0;
        if (exponent_position != std::string_view::npos) {
            const auto parsed = parse_exponent(spelling.substr(exponent_position + 1U));
            if (!parsed) {
                result.status = DecimalStatus::Overflow;
                return result;
            }
            explicit_exponent = *parsed;
        }

        std::string digits;
        digits.reserve(significand.size());
        bool saw_dot = false;
        std::size_t fractional_digits = 0U;
        for (const char character : significand) {
            if (character == '.' && !saw_dot) {
                saw_dot = true;
                continue;
            }
            if (character < '0' || character > '9')
                return result;
            digits.push_back(character);
            if (saw_dot)
                ++fractional_digits;
        }
        if (digits.empty())
            return result;
        const auto first_nonzero = digits.find_first_not_of('0');
        if (first_nonzero == std::string::npos) {
            result.status = DecimalStatus::Valid;
            return result;
        }
        digits.erase(0U, first_nonzero);
        std::size_t trailing_zeros = 0U;
        while (trailing_zeros < digits.size()
            && digits[digits.size() - trailing_zeros - 1U] == '0') {
            ++trailing_zeros;
        }
        if (trailing_zeros != 0U)
            digits.resize(digits.size() - trailing_zeros);

        if (fractional_digits
                > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
            || trailing_zeros
                > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            result.status = DecimalStatus::Overflow;
            return result;
        }
        std::int64_t exponent = 0;
        if (!checked_add(explicit_exponent,
                -static_cast<std::int64_t>(fractional_digits), exponent)
            || !checked_add(exponent, static_cast<std::int64_t>(trailing_zeros),
                exponent)) {
            result.status = DecimalStatus::Overflow;
            return result;
        }
        result.value.negative = negative;
        result.value.coefficient = std::move(digits);
        result.value.exponent10 = exponent;
        result.value.canonical = (negative ? "-" : "") + result.value.coefficient
            + 'e' + std::to_string(exponent);
        result.status = DecimalStatus::Valid;
        return result;
    }

    [[nodiscard]] std::string exact_value_canonical(const SdfExactValue& value)
    {
        if (value.kind == SdfExactValueKind::Empty)
            return "()";
        if (value.kind == SdfExactValueKind::Scalar)
            return value.components.front()->canonical;
        std::string result;
        bool first = true;
        for (const auto& component : value.components) {
            if (!first)
                result.push_back(':');
            if (component)
                result += component->canonical;
            first = false;
        }
        return result;
    }

    [[nodiscard]] std::optional<SdfExactValue> parse_exact_value(
        const std::vector<SdfSyntaxAtom>& atoms, DecimalStatus& status)
    {
        status = DecimalStatus::Valid;
        SdfExactValue result;
        if (atoms.empty()) {
            result.canonical = exact_value_canonical(result);
            return result;
        }
        const auto colon_count = std::ranges::count_if(atoms, [](const auto& atom) {
            return atom.kind == SdfTokenKind::Colon;
        });
        if (colon_count == 0) {
            if (atoms.size() != 1U || atoms.front().kind != SdfTokenKind::Number) {
                status = DecimalStatus::Invalid;
                return std::nullopt;
            }
            auto parsed = parse_decimal(atoms.front().spelling);
            status = parsed.status;
            if (status != DecimalStatus::Valid)
                return std::nullopt;
            result.kind = SdfExactValueKind::Scalar;
            result.components.front() = std::move(parsed.value);
            result.canonical = exact_value_canonical(result);
            return result;
        }
        if (colon_count != 2) {
            status = DecimalStatus::Invalid;
            return std::nullopt;
        }
        result.kind = SdfExactValueKind::Triple;
        std::size_t component = 0U;
        for (const auto& atom : atoms) {
            if (atom.kind == SdfTokenKind::Colon) {
                ++component;
                continue;
            }
            if (component >= result.components.size()
                || atom.kind != SdfTokenKind::Number || result.components[component]) {
                status = DecimalStatus::Invalid;
                return std::nullopt;
            }
            auto parsed = parse_decimal(atom.spelling);
            status = parsed.status;
            if (status != DecimalStatus::Valid)
                return std::nullopt;
            result.components[component] = std::move(parsed.value);
        }
        result.canonical = exact_value_canonical(result);
        return result;
    }

    [[nodiscard]] std::optional<SdfExactValue> parse_exact_value_text(
        const std::vector<std::string>& spellings, DecimalStatus& status)
    {
        std::vector<SdfSyntaxAtom> atoms;
        atoms.reserve(spellings.size());
        for (const auto& spelling : spellings) {
            atoms.push_back(SdfSyntaxAtom {
                spelling == ":" ? SdfTokenKind::Colon : SdfTokenKind::Number,
                spelling, { } });
        }
        return parse_exact_value(atoms, status);
    }

    [[nodiscard]] std::optional<SdfTimeUnit> time_unit(
        const std::string_view spelling) noexcept
    {
        const auto unit = ascii_lower(spelling);
        if (unit == "s")
            return SdfTimeUnit::Second;
        if (unit == "ms")
            return SdfTimeUnit::Millisecond;
        if (unit == "us")
            return SdfTimeUnit::Microsecond;
        if (unit == "ns")
            return SdfTimeUnit::Nanosecond;
        if (unit == "ps")
            return SdfTimeUnit::Picosecond;
        if (unit == "fs")
            return SdfTimeUnit::Femtosecond;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::int64_t femtosecond_exponent(
        const SdfTimeUnit unit) noexcept
    {
        switch (unit) {
        case SdfTimeUnit::Second:
            return 15;
        case SdfTimeUnit::Millisecond:
            return 12;
        case SdfTimeUnit::Microsecond:
            return 9;
        case SdfTimeUnit::Nanosecond:
            return 6;
        case SdfTimeUnit::Picosecond:
            return 3;
        case SdfTimeUnit::Femtosecond:
            return 0;
        }
        return 0;
    }

    [[nodiscard]] std::optional<SdfExactDecimal> shift_decimal(
        SdfExactDecimal value, const std::int64_t exponent)
    {
        if (value.coefficient == "0")
            return value;
        if (!checked_add(value.exponent10, exponent, value.exponent10))
            return std::nullopt;
        value.canonical = (value.negative ? "-" : "") + value.coefficient + 'e'
            + std::to_string(value.exponent10);
        return value;
    }

    [[nodiscard]] std::optional<SdfExactValue> shift_value(
        SdfExactValue value, const std::int64_t exponent)
    {
        for (auto& component : value.components) {
            if (!component)
                continue;
            component = shift_decimal(std::move(*component), exponent);
            if (!component)
                return std::nullopt;
        }
        value.canonical = exact_value_canonical(value);
        return value;
    }

    [[nodiscard]] std::string decode_escaped(const std::string_view spelling)
    {
        std::string result;
        result.reserve(spelling.size());
        bool escaped = false;
        for (const char character : spelling) {
            if (!escaped && character == '\\') {
                escaped = true;
                continue;
            }
            result.push_back(character);
            escaped = false;
        }
        return result;
    }

    [[nodiscard]] std::string decode_string(const std::string_view spelling)
    {
        if (spelling.size() < 2U || spelling.front() != '"'
            || spelling.back() != '"') {
            return std::string { spelling };
        }
        return decode_escaped(spelling.substr(1U, spelling.size() - 2U));
    }

    void append_field(std::string& output, const std::string_view value)
    {
        output += std::to_string(value.size());
        output.push_back(':');
        output += value;
    }

    [[nodiscard]] std::optional<SdfNormalizedName> normalize_name(
        const std::string_view spelling, const char divider)
    {
        SdfNormalizedName result;
        std::string segment;
        segment.reserve(spelling.size());
        bool escaped = false;
        for (const char character : spelling) {
            if (!escaped && character == '\\') {
                escaped = true;
                continue;
            }
            if (!escaped && character == divider) {
                if (segment.empty())
                    return std::nullopt;
                result.segments.push_back(std::move(segment));
                segment.clear();
                continue;
            }
            segment.push_back(character);
            escaped = false;
        }
        if (escaped || segment.empty())
            return std::nullopt;
        result.segments.push_back(std::move(segment));
        result.canonical = "name";
        for (const auto& value : result.segments)
            append_field(result.canonical, value);
        return result;
    }

    [[nodiscard]] bool time_value_parent(const SdfConstructKind parent) noexcept
    {
        return parent != SdfConstructKind::PathPulsePercent
            && parent != SdfConstructKind::LabelEntry;
    }

    class SdfNormalizer {
    public:
        SdfNormalizer(SdfFile& file, std::vector<Diagnostic>& diagnostics)
            : file_(file)
            , diagnostics_(diagnostics)
        {
            if (const auto* header = file_.find_header(SdfHeaderKind::Divider);
                header && header->canonical_value.size() == 1U) {
                divider_ = header->canonical_value.front();
            }
        }

        void run()
        {
            normalize_headers();
            for (auto& cell : file_.cells) {
                normalize_cell(cell);
                normalize_nodes(cell.declarations);
            }
        }

    private:
        struct NodeFrame {
            SdfSyntaxNode* node { };
            SdfConstructKind parent { SdfConstructKind::Cell };
        };

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), span, { } });
        }

        void normalize_headers()
        {
            for (auto& header : file_.headers) {
                if (header.kind == SdfHeaderKind::Voltage
                    || header.kind == SdfHeaderKind::Temperature) {
                    DecimalStatus status { };
                    header.exact_value = parse_exact_value_text(
                        header.value_spellings, status);
                    if (!header.exact_value && status == DecimalStatus::Overflow) {
                        diagnose("FSIM-SDF-NORM-001",
                            "SDF header decimal exponent exceeds the exact normalization range",
                            header.span);
                    }
                } else if (header.kind == SdfHeaderKind::Timescale) {
                    normalize_timescale(header);
                }
            }
        }

        void normalize_timescale(const SdfHeaderRecord& header)
        {
            if (header.value_spellings.size() != 2U) {
                diagnose("FSIM-SDF-NORM-003",
                    "SDF timescale cannot be normalized exactly", header.span);
                return;
            }
            auto magnitude = parse_decimal(header.value_spellings.front());
            const auto unit = time_unit(header.value_spellings.back());
            if (magnitude.status != DecimalStatus::Valid || !unit
                || magnitude.value.negative || magnitude.value.coefficient != "1"
                || magnitude.value.exponent10 < 0
                || magnitude.value.exponent10 > 2) {
                diagnose("FSIM-SDF-NORM-003",
                    "SDF timescale has an invalid exact scale or unit", header.span);
                return;
            }
            auto scaled = shift_decimal(magnitude.value, femtosecond_exponent(*unit));
            if (!scaled) {
                diagnose("FSIM-SDF-NORM-003",
                    "SDF timescale overflows exact femtosecond scaling", header.span);
                return;
            }
            file_.normalized_timescale = SdfNormalizedTimescale {
                *unit, std::move(magnitude.value), std::move(*scaled), { }
            };
            file_.normalized_timescale->canonical
                = file_.normalized_timescale->femtoseconds.canonical + "fs";
        }

        void normalize_cell(SdfCell& cell)
        {
            if (cell.instance_kind != SdfInstanceSelectorKind::Exact)
                return;
            cell.normalized_instance = normalize_name(cell.instance_spelling, divider_);
            if (!cell.normalized_instance) {
                diagnose("FSIM-SDF-NORM-004",
                    "SDF instance has an empty segment or incomplete escaped name",
                    cell.span);
            }
        }

        void normalize_nodes(std::vector<SdfSyntaxNode>& roots)
        {
            std::vector<NodeFrame> pending;
            for (auto& root : roots)
                pending.push_back(NodeFrame { &root, SdfConstructKind::Cell });
            while (!pending.empty()) {
                const auto frame = pending.back();
                pending.pop_back();
                normalize_node(*frame.node, frame.parent);
                for (auto& child : frame.node->children)
                    pending.push_back(NodeFrame { &child, frame.node->kind });
            }
        }

        [[nodiscard]] std::string canonical_atom(const SdfSyntaxAtom& atom,
            const SourceSpan& owner_span)
        {
            if (atom.kind == SdfTokenKind::Number) {
                const auto parsed = parse_decimal(atom.spelling);
                if (parsed.status != DecimalStatus::Valid) {
                    diagnose("FSIM-SDF-NORM-001",
                        "SDF decimal exponent exceeds the exact normalization range",
                        atom.span);
                    return "invalid-number";
                }
                return "number:" + parsed.value.canonical;
            }
            if (atom.kind == SdfTokenKind::String) {
                std::string result = "string";
                append_field(result, decode_string(atom.spelling));
                return result;
            }
            if (atom.kind == SdfTokenKind::Keyword)
                return "keyword:" + ascii_lower(atom.spelling);
            if (atom.kind == SdfTokenKind::Identifier
                || atom.kind == SdfTokenKind::EscapedIdentifier) {
                if (atom.spelling.find(divider_) != std::string::npos
                    || atom.spelling.find('\\') != std::string::npos) {
                    if (const auto name = normalize_name(atom.spelling, divider_))
                        return name->canonical;
                }
                std::string result = "identifier";
                append_field(result, decode_escaped(atom.spelling));
                return result;
            }
            std::string result = to_string(atom.kind);
            append_field(result, atom.spelling);
            (void)owner_span;
            return result;
        }

        void normalize_node(SdfSyntaxNode& node, const SdfConstructKind parent)
        {
            node.canonical_atoms.clear();
            node.canonical_atoms.reserve(node.atoms.size());
            if (node.kind != SdfConstructKind::Value) {
                for (const auto& atom : node.atoms)
                    node.canonical_atoms.push_back(canonical_atom(atom, node.span));
            }
            if (node.kind == SdfConstructKind::Value)
                normalize_value(node, parent);

            node.canonical_identity = to_string(node.kind);
            node.canonical_identity.push_back('{');
            if (node.kind == SdfConstructKind::Edge) {
                node.canonical_identity += ascii_lower(node.keyword_spelling);
            } else if (node.exact_value) {
                node.canonical_identity += node.exact_value->canonical;
            } else {
                for (const auto& atom : node.canonical_atoms)
                    append_field(node.canonical_identity, atom);
            }
            node.canonical_identity.push_back('}');
        }

        void normalize_value(SdfSyntaxNode& node, const SdfConstructKind parent)
        {
            DecimalStatus status { };
            node.exact_value = parse_exact_value(node.atoms, status);
            if (!node.exact_value) {
                diagnose(status == DecimalStatus::Overflow ? "FSIM-SDF-NORM-001"
                                                           : "FSIM-SDF-NORM-002",
                    status == DecimalStatus::Overflow
                        ? "SDF decimal exponent exceeds the exact normalization range"
                        : "SDF value cannot be normalized without changing its triple shape",
                    node.span);
                return;
            }
            for (const auto& component : node.exact_value->components) {
                if (component)
                    node.canonical_atoms.push_back("number:" + component->canonical);
                else if (node.exact_value->kind == SdfExactValueKind::Triple)
                    node.canonical_atoms.emplace_back("missing");
            }
            if (!file_.normalized_timescale || !time_value_parent(parent))
                return;
            node.scaled_femtoseconds = shift_value(*node.exact_value,
                file_.normalized_timescale->femtoseconds.exponent10);
            if (!node.scaled_femtoseconds) {
                diagnose("FSIM-SDF-NORM-003",
                    "SDF value overflows exact femtosecond scaling", node.span);
            }
        }

        SdfFile& file_;
        std::vector<Diagnostic>& diagnostics_;
        char divider_ { '.' };
    };

} // namespace

void normalize_sdf(SdfFile& file, std::vector<Diagnostic>& diagnostics)
{
    SdfNormalizer(file, diagnostics).run();
}

SdfExactIntegerResult sdf_exact_integer_at(const SdfExactDecimal& value,
    const std::int64_t target_exponent10, const std::size_t max_digits)
{
    if (value.coefficient == "0")
        return { SdfExactConversionError::None, "0" };
    std::int64_t delta = 0;
    if ((target_exponent10 < 0
            && value.exponent10
                > std::numeric_limits<std::int64_t>::max()
                    + target_exponent10)
        || (target_exponent10 > 0
            && value.exponent10
                < std::numeric_limits<std::int64_t>::min()
                    + target_exponent10)) {
        return { SdfExactConversionError::Overflow, { } };
    }
    delta = value.exponent10 - target_exponent10;
    std::string digits = value.coefficient;
    if (delta < 0) {
        if (delta == std::numeric_limits<std::int64_t>::min())
            return { SdfExactConversionError::Lossy, { } };
        const auto removed = static_cast<std::uint64_t>(-delta);
        if (removed >= digits.size())
            return { SdfExactConversionError::Lossy, { } };
        const auto first_removed = digits.size() - static_cast<std::size_t>(removed);
        if (!std::ranges::all_of(digits.begin()
                    + static_cast<std::ptrdiff_t>(first_removed),
                digits.end(), [](const char character) { return character == '0'; })) {
            return { SdfExactConversionError::Lossy, { } };
        }
        digits.resize(first_removed);
    } else {
        const auto appended = static_cast<std::uint64_t>(delta);
        if (appended > max_digits || digits.size() > max_digits - appended)
            return { SdfExactConversionError::Overflow, { } };
        digits.append(static_cast<std::size_t>(appended), '0');
    }
    if (digits.size() > max_digits)
        return { SdfExactConversionError::Overflow, { } };
    if (value.negative)
        digits.insert(digits.begin(), '-');
    return { SdfExactConversionError::None, std::move(digits) };
}

const char* to_string(const SdfExactValueKind kind) noexcept
{
    switch (kind) {
    case SdfExactValueKind::Empty:
        return "empty";
    case SdfExactValueKind::Scalar:
        return "scalar";
    case SdfExactValueKind::Triple:
        return "triple";
    }
    return "empty";
}

const char* to_string(const SdfTimeUnit unit) noexcept
{
    switch (unit) {
    case SdfTimeUnit::Second:
        return "s";
    case SdfTimeUnit::Millisecond:
        return "ms";
    case SdfTimeUnit::Microsecond:
        return "us";
    case SdfTimeUnit::Nanosecond:
        return "ns";
    case SdfTimeUnit::Picosecond:
        return "ps";
    case SdfTimeUnit::Femtosecond:
        return "fs";
    }
    return "fs";
}

const char* to_string(const SdfExactConversionError error) noexcept
{
    switch (error) {
    case SdfExactConversionError::None:
        return "none";
    case SdfExactConversionError::Overflow:
        return "overflow";
    case SdfExactConversionError::Lossy:
        return "lossy";
    }
    return "overflow";
}

} // namespace fsim::frontend
