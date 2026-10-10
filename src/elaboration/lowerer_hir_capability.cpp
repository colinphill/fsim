// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "fsim/frontend/parser.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

namespace fsim::elaboration {
namespace {

    constexpr std::size_t maximum_hir_loop_iterations = 1'000'000U;

    bool same_hir_identifier(
        const std::string_view left,
        const std::string_view right,
        const bool vhdl)
    {
        if (!vhdl) {
            return left == right;
        }
        const auto extended = [](const std::string_view value) {
            return value.size() >= 2U && value.front() == '\\'
                && value.back() == '\\';
        };
        if (extended(left) || extended(right)) {
            return left == right;
        }
        return std::ranges::equal(
            left,
            right,
            [](const char lhs, const char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs))
                    == std::tolower(static_cast<unsigned char>(rhs));
            });
    }

    bool simple_hir_identifier(
        const std::string_view value,
        const bool vhdl)
    {
        if (value.empty()) {
            return false;
        }
        if (vhdl && value.size() >= 2U && value.front() == '\\'
            && value.back() == '\\') {
            return true;
        }
        if (vhdl && value.size() >= 3U && value.front() == '\''
            && value.back() == '\'') {
            return true;
        }
        return std::ranges::all_of(value, [](const char raw) {
            const auto character = static_cast<unsigned char>(raw);
            return std::isalnum(character) != 0 || raw == '_'
                || raw == '$' || raw == '\\';
        });
    }

    bool systemverilog_genvar_identity(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::ScopeId use_scope,
        const std::string_view name)
    {
        if (specialization.language() != semantic::Language::system_verilog
            || name.empty()) {
            return false;
        }
        const auto identity = std::ranges::find_if(
            specialization.specialization().hierarchy_identities,
            [&](const semantic::SpecializedHirNamedIdentity& candidate) {
                return candidate.name == name && !candidate.identity.empty();
            });
        if (identity
            == specialization.specialization().hierarchy_identities.end()) {
            return false;
        }
        const auto unit = specialization.design().find_unit(
            specialization.unit());
        if (!unit || unit->systemverilog == nullptr) {
            return false;
        }

        const auto& scopes = specialization.design().semantics.scopes();
        const auto scope_within = [&](semantic::ScopeId scope,
                                      const semantic::ScopeId owner) {
            std::set<semantic::ScopeId> visited;
            while (scope.valid() && scope.value() < scopes.size()
                && visited.insert(scope).second) {
                if (scope == owner) {
                    return true;
                }
                const auto parent = scopes[scope.value()].parent;
                if (!parent) {
                    return false;
                }
                scope = *parent;
            }
            return false;
        };
        const auto contains = [&](const auto& self,
                                  const semantic::sv::GenerateRegion& region)
            -> bool {
            if (region.kind == semantic::sv::GenerateKind::iterative
                && region.iterator == name
                && region.initial && region.condition && region.iteration
                && scope_within(use_scope, region.scope)) {
                return true;
            }
            return std::ranges::any_of(
                region.nested,
                [&](const semantic::sv::GenerateRegion& nested) {
                    return self(self, nested);
                });
        };
        return std::ranges::any_of(
            unit->systemverilog->generates,
            [&](const semantic::sv::GenerateRegion& region) {
                return contains(contains, region);
            });
    }

    struct VhdlEnvironmentTimeRecordMember {
        semantic::vhdl::SubtypeIndication subtype;
        std::size_t offset { };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
    };

    std::optional<VhdlEnvironmentTimeRecordMember>
    vhdl_environment_time_record_member(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& owner,
        const semantic::ScopeId use_scope,
        const std::string_view member)
    {
        // STD.ENV is compiler-owned and has no synthetic package/type unit in
        // compiled HIR. Keep its intrinsic packed record layout available to
        // ordinary HIR member selection without manufacturing an owning type.
        if (!vhdl_environment_time_record_subtype(
                specialization, owner, use_scope)) {
            return std::nullopt;
        }

        struct LayoutMember {
            std::string_view name;
            std::size_t offset;
            std::size_t width;
            bool enumeration;
        };
        constexpr auto layout = std::to_array<LayoutMember>({
            { "microsecond", 451U, 64U, false },
            { "second", 387U, 64U, false },
            { "minute", 323U, 64U, false },
            { "hour", 259U, 64U, false },
            { "day", 195U, 64U, false },
            { "month", 131U, 64U, false },
            { "year", 67U, 64U, false },
            { "weekday", 64U, 3U, true },
            { "dayofyear", 0U, 64U, false },
        });
        const auto selected = std::ranges::find_if(
            layout, [&](const LayoutMember& candidate) {
                return same_hir_identifier(
                    candidate.name, member, true);
            });
        if (selected == layout.end()) {
            return std::nullopt;
        }

        semantic::vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = selected->enumeration
            ? "std.env.dayofweek" : "integer";
        subtype.domain = selected->enumeration
            ? semantic::vhdl::ValueDomain::bit2
            : semantic::vhdl::ValueDomain::integer;
        subtype.executable_width = selected->width;
        subtype.integer_storage_width = selected->enumeration
            ? 0U : static_cast<std::uint8_t>(selected->width);
        subtype.signed_value = !selected->enumeration;
        return VhdlEnvironmentTimeRecordMember {
            std::move(subtype),
            selected->offset,
            selected->width,
            selected->enumeration
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Integer,
            !selected->enumeration,
        };
    }

    class VhdlUnspecifiedInferenceScanner final {
    public:
        explicit VhdlUnspecifiedInferenceScanner(
            const semantic::SpecializedHirUnit& unit)
            : unit_ { unit }
        {
        }

        std::optional<semantic::SourceSpanId> scan(
            const semantic::ProcessId process)
        {
            const auto record = unit_.find_process(process);
            if (!record || record->vhdl == nullptr) {
                return std::nullopt;
            }
            for (const auto declaration : record->vhdl->declarations) {
                if (const auto failure = scan_declaration(declaration)) {
                    return failure;
                }
            }
            for (const auto& sensitivity : record->vhdl->sensitivities) {
                if (sensitivity.expression) {
                    if (const auto failure = scan_expression(
                            *sensitivity.expression)) {
                        return failure;
                    }
                }
            }
            for (const auto statement : record->vhdl->statements) {
                if (const auto failure = scan_statement(statement)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        std::optional<semantic::SourceSpanId> scan(
            const semantic::StatementId statement)
        {
            return scan_statement(statement);
        }

    private:
        bool callable_uses_unspecified_types(
            const semantic::DeclarationId declaration) const
        {
            const auto record = unit_.find_declaration(declaration);
            if (!record || record->vhdl == nullptr
                || !record->vhdl->callable) {
                return false;
            }
            return std::ranges::any_of(
                record->vhdl->callable->formals,
                [&](const semantic::DeclarationId formal) {
                    const auto formal_record = unit_.find_declaration(formal);
                    return formal_record && formal_record->vhdl != nullptr
                        && formal_record->vhdl->subtype
                        && formal_record->vhdl->subtype->unspecified_class
                        != semantic::vhdl::UnspecifiedTypeClass::none;
                });
        }

        bool unresolved_unspecified_name(
            const semantic::vhdl::Name& name) const
        {
            return (name.selected
                       && callable_uses_unspecified_types(*name.selected))
                || std::ranges::any_of(
                    name.overloads,
                    [&](const semantic::DeclarationId candidate) {
                        return callable_uses_unspecified_types(candidate);
                    });
        }

        std::optional<semantic::SourceSpanId> scan_expression(
            const semantic::ExpressionId expression)
        {
            if (!visited_expressions_.insert(expression.value()).second) {
                return std::nullopt;
            }
            const auto record = unit_.find_expression(expression);
            if (!record || record->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& source = *record->vhdl;
            const auto callable_expression
                = source.kind == semantic::vhdl::ExpressionKind::call
                || source.kind == semantic::vhdl::ExpressionKind::index
                || source.kind == semantic::vhdl::ExpressionKind::unary
                || source.kind == semantic::vhdl::ExpressionKind::binary;
            if (callable_expression
                && !source.unspecified_type_inference_unique
                && source.referenced_name
                && unresolved_unspecified_name(*source.referenced_name)) {
                return source.source;
            }
            for (const auto operand : source.operands) {
                if (const auto failure = scan_expression(operand)) {
                    return failure;
                }
            }
            for (const auto& association : source.associations) {
                for (const auto choice : association.choices) {
                    if (const auto failure = scan_expression(choice)) {
                        return failure;
                    }
                }
                if (const auto failure = scan_expression(association.value)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        std::optional<semantic::SourceSpanId> scan_delay_value(
            const semantic::vhdl::DelayValue& value)
        {
            return value.expression
                ? scan_expression(*value.expression)
                : std::nullopt;
        }

        std::optional<semantic::SourceSpanId> scan_delay(
            const semantic::vhdl::Delay& delay)
        {
            if (const auto failure = scan_delay_value(delay.primary)) {
                return failure;
            }
            for (const auto* value : {
                     delay.minimum ? &*delay.minimum : nullptr,
                     delay.typical ? &*delay.typical : nullptr,
                     delay.maximum ? &*delay.maximum : nullptr }) {
                if (value != nullptr) {
                    if (const auto failure = scan_delay_value(*value)) {
                        return failure;
                    }
                }
            }
            for (const auto& additional : delay.additional) {
                if (const auto failure = scan_delay(additional)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        std::optional<semantic::SourceSpanId> scan_declaration(
            const semantic::DeclarationId declaration)
        {
            if (!visited_declarations_.insert(declaration.value()).second) {
                return std::nullopt;
            }
            const auto record = unit_.find_declaration(declaration);
            if (!record || record->vhdl == nullptr) {
                return std::nullopt;
            }
            if (record->vhdl->initializer) {
                if (const auto failure = scan_expression(
                        *record->vhdl->initializer)) {
                    return failure;
                }
            }
            for (const auto child : record->vhdl->children) {
                if (const auto failure = scan_declaration(child)) {
                    return failure;
                }
            }
            for (const auto statement : record->vhdl->statements) {
                if (const auto failure = scan_statement(statement)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        std::optional<semantic::SourceSpanId> scan_statement(
            const semantic::StatementId statement)
        {
            if (!visited_statements_.insert(statement.value()).second) {
                return std::nullopt;
            }
            const auto record = unit_.find_statement(statement);
            if (!record || record->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& source = *record->vhdl;
            // Expression calls retain the frontend's per-occurrence
            // inference result. Procedure-call statements currently do not;
            // diagnosing solely from the procedure profile would reject a
            // valid single-profile call. Their argument expressions are
            // still scanned below, while failed procedure lowering keeps its
            // specific overload diagnostic.
            for (const auto expression : {
                     source.target, source.value, source.condition,
                     source.loop_initial, source.loop_limit,
                     source.report, source.severity }) {
                if (expression) {
                    if (const auto failure = scan_expression(*expression)) {
                        return failure;
                    }
                }
            }
            for (const auto& sensitivity : source.sensitivities) {
                if (sensitivity.expression) {
                    if (const auto failure = scan_expression(
                            *sensitivity.expression)) {
                        return failure;
                    }
                }
            }
            for (const auto& argument : source.procedure_arguments) {
                if (const auto failure = scan_expression(argument.actual)) {
                    return failure;
                }
            }
            if (source.delay) {
                if (const auto failure = scan_delay(*source.delay)) {
                    return failure;
                }
            }
            if (source.rejection_limit) {
                if (const auto failure = scan_delay(*source.rejection_limit)) {
                    return failure;
                }
            }
            if (source.disconnection_delay) {
                if (const auto failure = scan_delay(
                        *source.disconnection_delay)) {
                    return failure;
                }
            }
            for (const auto& element : source.waveform) {
                if (const auto failure = scan_expression(element.value)) {
                    return failure;
                }
                if (element.delay) {
                    if (const auto failure = scan_delay(*element.delay)) {
                        return failure;
                    }
                }
            }
            for (const auto declaration : source.declarations) {
                if (const auto failure = scan_declaration(declaration)) {
                    return failure;
                }
            }
            for (const auto child : source.statements) {
                if (const auto failure = scan_statement(child)) {
                    return failure;
                }
            }
            for (const auto child : source.else_statements) {
                if (const auto failure = scan_statement(child)) {
                    return failure;
                }
            }
            for (const auto& alternative : source.alternatives) {
                for (const auto choice : alternative.choices) {
                    if (const auto failure = scan_expression(choice)) {
                        return failure;
                    }
                }
                for (const auto child : alternative.statements) {
                    if (const auto failure = scan_statement(child)) {
                        return failure;
                    }
                }
            }
            return std::nullopt;
        }

        const semantic::SpecializedHirUnit& unit_;
        std::unordered_set<std::uint32_t> visited_expressions_;
        std::unordered_set<std::uint32_t> visited_statements_;
        std::unordered_set<std::uint32_t> visited_declarations_;
    };

    std::optional<std::uint64_t> unsigned_digits(
        const std::string_view text,
        const unsigned radix)
    {
        if (text.empty() || radix < 2U || radix > 16U) {
            return std::nullopt;
        }
        std::uint64_t result { };
        bool found_digit = false;
        for (const char raw : text) {
            if (raw == '_') {
                continue;
            }
            const auto digit = static_cast<char>(
                std::tolower(static_cast<unsigned char>(raw)));
            const auto value = digit >= '0' && digit <= '9'
                ? static_cast<unsigned>(digit - '0')
                : digit >= 'a' && digit <= 'f'
                ? static_cast<unsigned>(digit - 'a' + 10)
                : 16U;
            if (value >= radix
                || result > (std::numeric_limits<std::uint64_t>::max() - value)
                        / radix) {
                return std::nullopt;
            }
            found_digit = true;
            result = result * radix + value;
        }
        return found_digit ? std::optional { result } : std::nullopt;
    }

    std::optional<std::int64_t> constant_integer_text(
        const std::string_view text)
    {
        auto normalized = std::string { text };
        std::ranges::transform(
            normalized,
            normalized.begin(),
            [](const char value) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(value)));
            });
        if (normalized == "true" || normalized == "'1'"
            || normalized == "1'b1" || normalized == "'1") {
            return 1;
        }
        if (normalized == "false" || normalized == "'0'"
            || normalized == "1'b0" || normalized == "'0") {
            return 0;
        }
        auto digits = std::string_view { normalized };
        bool negative = false;
        auto radix = 10U;
        const auto quote = digits.find('\'');
        if (quote != std::string_view::npos) {
            digits.remove_prefix(quote + 1U);
            if (digits.empty() || digits.front() == 's'
                || digits.front() == 'S') {
                return std::nullopt;
            }
            const auto base = static_cast<char>(
                std::tolower(static_cast<unsigned char>(digits.front())));
            radix = base == 'b' ? 2U : base == 'o' ? 8U
                : base == 'd'                      ? 10U
                : base == 'h'                      ? 16U
                                                   : 0U;
            if (radix == 0U) {
                return std::nullopt;
            }
            digits.remove_prefix(1U);
        } else if (!digits.empty()
            && (digits.front() == '+' || digits.front() == '-')) {
            negative = digits.front() == '-';
            digits.remove_prefix(1U);
        }
        const auto value = unsigned_digits(digits, radix);
        const auto maximum = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
        if (!value || *value > maximum + (negative ? 1U : 0U)) {
            return std::nullopt;
        }
        if (negative) {
            return *value == maximum + 1U
                ? std::numeric_limits<std::int64_t>::min()
                : -static_cast<std::int64_t>(*value);
        }
        return static_cast<std::int64_t>(*value);
    }

    std::optional<std::size_t> literal_width(
        const std::string_view text,
        const std::size_t fallback)
    {
        const auto quote = text.find('\'');
        if (quote == std::string_view::npos || quote == 0U) {
            return fallback;
        }
        const auto width = unsigned_decimal(text.substr(0, quote));
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(*width);
    }

    bool systemverilog_reduction_operator(
        const std::string_view operation) noexcept
    {
        return operation == "&" || operation == "|"
            || operation == "^" || operation == "~&"
            || operation == "~|" || operation == "~^"
            || operation == "^~";
    }

    bool supported_unary_operator(
        const frontend::Language language,
        const std::string_view operation,
        const frontend::ValueDomain domain)
    {
        if (language == frontend::Language::Vhdl2008) {
            if (operation == "+" || operation == "-") {
                return domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Logic9
                    || domain == frontend::ValueDomain::Integer;
            }
            if (operation == "abs") {
                return domain == frontend::ValueDomain::Integer
                    || domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Logic9;
            }
            if (operation == "??") {
                return domain == frontend::ValueDomain::Boolean
                    || domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Logic9;
            }
            return operation == "not"
                && (domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Logic9
                    || domain == frontend::ValueDomain::Boolean);
        }
        const auto packed = domain == frontend::ValueDomain::Bit2
            || domain == frontend::ValueDomain::Logic4
            || domain == frontend::ValueDomain::Logic9
            || domain == frontend::ValueDomain::Boolean;
        return operation == "!" || operation == "~"
            || (systemverilog_reduction_operator(operation) && packed);
    }

    bool scalar_domain(frontend::ValueDomain domain);

    bool vhdl_equality_operator(const std::string_view operation)
    {
        return operation == "=" || operation == "/="
            || operation == "?=" || operation == "?/=";
    }

    bool vhdl_relational_operator(const std::string_view operation)
    {
        return operation == "<" || operation == "<="
            || operation == ">" || operation == ">=";
    }

    bool vhdl_logic_character(const char value) noexcept
    {
        const auto normalized = static_cast<char>(
            std::toupper(static_cast<unsigned char>(value)));
        return normalized == 'U' || normalized == 'X'
            || normalized == '0' || normalized == '1'
            || normalized == 'Z' || normalized == 'W'
            || normalized == 'L' || normalized == 'H'
            || normalized == '-';
    }

    // The code of a VHDL character literal ('A'), which is a STD.STANDARD
    // CHARACTER value unless context selects another enumeration type.
    std::optional<unsigned char> vhdl_character_literal_code(
        const semantic::vhdl::Expression& source) noexcept
    {
        if (source.kind != semantic::vhdl::ExpressionKind::logic_literal
            || source.text.size() != 3U || source.text.front() != '\''
            || source.text.back() != '\'') {
            return std::nullopt;
        }
        return static_cast<unsigned char>(source.text[1]);
    }

    std::optional<std::uint64_t> vhdl_character_control_literal_code(
        const std::string_view name)
    {
        static constexpr std::array<std::string_view, 32U> low {
            "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
            "bs", "ht", "lf", "vt", "ff", "cr", "so", "si",
            "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
            "can", "em", "sub", "esc", "fsp", "gsp", "rsp", "usp",
        };
        std::string lower { name };
        std::ranges::transform(lower, lower.begin(), [](const char value) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(value)));
        });
        if (const auto found = std::ranges::find(low, lower);
            found != low.end()) {
            return static_cast<std::uint64_t>(found - low.begin());
        }
        if (lower == "del") {
            return 127U;
        }
        if (lower.size() == 4U && lower.front() == 'c') {
            const auto code = unsigned_decimal(
                std::string_view { lower }.substr(1U));
            if (code && *code >= 128U && *code <= 159U) {
                return *code;
            }
        }
        return std::nullopt;
    }

    // A character literal that cannot denote a standard-logic or BIT value
    // is self-determined as an 8-bit CHARACTER.
    bool vhdl_non_logic_character_literal(
        const semantic::vhdl::Expression& source) noexcept
    {
        const auto code = vhdl_character_literal_code(source);
        return code && !vhdl_logic_character(static_cast<char>(*code));
    }

    bool vhdl_comparison_operator(const std::string_view operation)
    {
        return vhdl_equality_operator(operation)
            || vhdl_relational_operator(operation);
    }

    bool supported_binary_operator(
        const frontend::Language language,
        const std::string_view operation,
        const frontend::ValueDomain left,
        const frontend::ValueDomain right)
    {
        if (language == frontend::Language::Vhdl2008) {
            const auto packed = [](const frontend::ValueDomain domain) {
                return domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Logic9;
            };
            if (vhdl_equality_operator(operation)) {
                return (left == right && scalar_domain(left))
                    || (packed(left) && packed(right));
            }
            if (left == frontend::ValueDomain::Integer
                && right == frontend::ValueDomain::Integer) {
                return operation == "+" || operation == "-"
                    || operation == "*" || operation == "/"
                    || operation == "rem" || operation == "mod"
                    || operation == "**"
                    || vhdl_relational_operator(operation);
            }
            const auto logical = operation == "and" || operation == "or"
                || operation == "xor" || operation == "xnor"
                || operation == "nand" || operation == "nor";
            if (logical && packed(left) && packed(right)) {
                return true;
            }
            if ((packed(left) && right == frontend::ValueDomain::Integer)
                || (left == frontend::ValueDomain::Integer && packed(right))
                || (packed(left) && packed(right))) {
                return operation == "+" || operation == "-"
                    || operation == "*" || operation == "/"
                    || operation == "rem" || operation == "mod"
                    || operation == "**"
                    || vhdl_relational_operator(operation)
                    || operation == "sll" || operation == "srl"
                    || operation == "sla" || operation == "sra"
                    || operation == "rol" || operation == "ror";
            }
            return logical
                && ((left == frontend::ValueDomain::Boolean
                        && right == frontend::ValueDomain::Boolean)
                    || (packed(left) && packed(right)));
        }
        return operation == "+" || operation == "-" || operation == "*"
            || operation == "/" || operation == "%"
            || operation == "<" || operation == "<="
            || operation == ">" || operation == ">="
            || operation == "&" || operation == "|" || operation == "^"
            || operation == "~^" || operation == "^~"
            || operation == "==" || operation == "!="
            || operation == "===" || operation == "!=="
            || operation == "&&" || operation == "||"
            || operation == "==?" || operation == "!=?"
            || operation == "<<" || operation == ">>"
            || operation == "<<<" || operation == ">>>"
            || operation == "**";
    }

    bool scalar_domain(const frontend::ValueDomain domain)
    {
        return domain == frontend::ValueDomain::Bit2
            || domain == frontend::ValueDomain::Logic4
            || domain == frontend::ValueDomain::Logic9
            || domain == frontend::ValueDomain::Boolean
            || domain == frontend::ValueDomain::Integer;
    }

    frontend::ValueDomain vhdl_domain(
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

    std::optional<std::size_t> vhdl_runtime_width(
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

    const semantic::vhdl::TypeDefinition* vhdl_array_definition(
        const semantic::SpecializedHirUnit& unit,
        semantic::TypeId type_id)
    {
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid()
            && visited.insert(type_id.value()).second) {
            const auto type = unit.find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return nullptr;
            }
            const auto& definition = *type->vhdl;
            if (definition.form == semantic::vhdl::TypeForm::array) {
                return &definition;
            }
            if ((definition.form != semantic::vhdl::TypeForm::subtype
                    && definition.form
                        != semantic::vhdl::TypeForm::alias)
                || !definition.base.type_mark.target.valid()) {
                return nullptr;
            }
            type_id = definition.base.type_mark.target;
        }
        return nullptr;
    }

    const semantic::vhdl::TypeDefinition* vhdl_enumeration_definition(
        const semantic::SpecializedHirUnit& unit,
        semantic::TypeId type_id)
    {
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid()
            && visited.insert(type_id.value()).second) {
            const auto type = unit.find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return nullptr;
            }
            const auto& definition = *type->vhdl;
            if (!definition.enumeration_literals.empty()) {
                return &definition;
            }
            if (definition.form != semantic::vhdl::TypeForm::subtype
                && definition.form != semantic::vhdl::TypeForm::alias) {
                return nullptr;
            }
            type_id = definition.base.type_mark.target;
        }
        return nullptr;
    }

    std::optional<semantic::vhdl::ValueDomain>
    vhdl_predefined_packed_vector_domain(const std::string_view source)
    {
        constexpr auto builtin_prefix = std::string_view { "@builtin:" };
        auto spelling = source;
        if (spelling.starts_with(builtin_prefix)) {
            spelling.remove_prefix(builtin_prefix.size());
        }
        if (same_hir_identifier(spelling, "bit_vector", true)
            || same_hir_identifier(
                spelling, "standard.bit_vector", true)) {
            return semantic::vhdl::ValueDomain::bit2;
        }
        return std::nullopt;
    }

    bool vhdl_null_array_subtype(
        const semantic::SpecializedHirUnit& unit,
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        if (!subtype.executable_width
            || *subtype.executable_width != 0U) {
            return false;
        }
        if (subtype.type_mark.target.valid()
            && vhdl_array_definition(
                   unit, subtype.type_mark.target)
                != nullptr) {
            return true;
        }
        auto spelling = std::string_view { subtype.type_mark.spelling };
        if (const auto separator = spelling.find_last_of(".:");
            separator != std::string_view::npos) {
            spelling.remove_prefix(separator + 1U);
        }
        return same_hir_identifier(spelling, "bit_vector", true)
            || same_hir_identifier(
                spelling, "std_logic_vector", true)
            || same_hir_identifier(
                spelling, "std_ulogic_vector", true)
            || same_hir_identifier(spelling, "signed", true)
            || same_hir_identifier(spelling, "unsigned", true);
    }

    std::optional<frontend::IntegerRange> vhdl_integer_range(
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        if (subtype.domain != semantic::vhdl::ValueDomain::integer) {
            return std::nullopt;
        }
        const auto found = std::ranges::find_if(
            subtype.constraints,
            [](const semantic::vhdl::RangeConstraint& constraint) {
                return (constraint.kind
                            == semantic::vhdl::RangeKind::integer
                           || constraint.kind
                               == semantic::vhdl::RangeKind::discrete
                           || constraint.kind
                               == semantic::vhdl::RangeKind::enumeration)
                    && constraint.left && constraint.right;
            });
        if (found == subtype.constraints.end()) {
            return std::nullopt;
        }
        return frontend::IntegerRange {
            *found->left,
            *found->right,
            found->descending,
        };
    }

    bool supported_systemverilog_integer_literal(const std::string_view text)
    {
        if (systemverilog_real_literal(text)) {
            return true;
        }
        const auto quote = text.find('\'');
        if (quote == std::string_view::npos) {
            return constant_integer_text(text).has_value();
        }
        if (quote == 0U) {
            return false;
        }
        const auto width = unsigned_decimal(text.substr(0U, quote));
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        auto digits = text.substr(quote + 1U);
        if (digits.empty() || digits.front() == 's' || digits.front() == 'S') {
            return false;
        }
        const auto base = static_cast<char>(
            std::tolower(static_cast<unsigned char>(digits.front())));
        return base == 'b' || base == 'o' || base == 'd' || base == 'h';
    }

    bool has_runtime_systemverilog_call(
        const semantic::SpecializedHirUnit& unit,
        const semantic::ExpressionId expression_id)
    {
        std::unordered_set<std::uint32_t> visiting;
        const auto inspect = [&](const auto& self,
                                 const semantic::ExpressionId candidate) -> bool {
            if (!candidate.valid()
                || !visiting.insert(candidate.value()).second) {
                return false;
            }
            const auto record = unit.find_expression(candidate);
            if (!record || record->systemverilog == nullptr) {
                return false;
            }
            const auto& source = *record->systemverilog;
            const auto referenced_call
                = source.kind == semantic::sv::ExpressionKind::call
                && source.referenced_name
                && ((source.referenced_name->selected
                        && source.referenced_name->selected->valid())
                    || std::ranges::any_of(
                        source.referenced_name->overloads,
                        [](const semantic::DeclarationId declaration) {
                            return declaration.valid();
                        }));
            return referenced_call
                || std::ranges::any_of(
                    source.operands,
                    [&](const semantic::ExpressionId operand) {
                        return self(self, operand);
                    });
        };
        return inspect(inspect, expression_id);
    }

} // namespace

bool vhdl_environment_time_record_subtype(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::SubtypeIndication& owner,
    const semantic::ScopeId use_scope)
{
    auto type = owner;
    std::unordered_set<std::uint32_t> visiting;
    while (true) {
        auto type_name = std::string_view { type.type_mark.spelling };
        constexpr auto builtin_prefix
            = std::string_view { "@builtin:" };
        if (type_name.starts_with(builtin_prefix)) {
            type_name.remove_prefix(builtin_prefix.size());
        }
        if (same_hir_identifier(
                type_name, "std.env.time_record", true)) {
            return true;
        }
        if (!type.type_mark.target.valid()) {
            return same_hir_identifier(type_name, "time_record", true)
                && semantic::CompiledDesignResolver { specialization }
                       .vhdl_builtin_type_visible(
                           type.type_mark.spelling, use_scope);
        }
        if (!visiting.insert(type.type_mark.target.value()).second) {
            return false;
        }
        const auto definition = specialization.find_type(
            type.type_mark.target);
        if (!definition || definition->vhdl == nullptr
            || (definition->vhdl->form
                    != semantic::vhdl::TypeForm::subtype
                && definition->vhdl->form
                    != semantic::vhdl::TypeForm::alias)) {
            return false;
        }
        type = definition->vhdl->base;
    }
}

void Lowerer::set_specialized_hir_unit(
    const semantic::SpecializedHirUnit* const unit) noexcept
{
    clear_hir_effective_vhdl_subtype_cache();
    clear_hir_expression_resolution_cache();
    clear_hir_vhdl_subtype_name_cache();
    clear_hir_pure_integral_attempt_cache();
    specialized_hir_unit_ = unit;
    vhdl_standard_ = frontend::VhdlStandard::Vhdl2008;
    systemverilog_standard_
        = frontend::StandardRevision::SystemVerilog2017;
    if (unit == nullptr) {
        return;
    }
    const auto selected = unit->design().find_unit(unit->unit());
    if (!selected) {
        return;
    }
    if (selected->vhdl != nullptr) {
        const auto& standard = selected->vhdl->standard;
        if (standard == "1987") {
            vhdl_standard_ = frontend::VhdlStandard::Vhdl1987;
        } else if (standard == "1993") {
            vhdl_standard_ = frontend::VhdlStandard::Vhdl1993;
        } else if (standard == "2000") {
            vhdl_standard_ = frontend::VhdlStandard::Vhdl2000;
        } else if (standard == "2002") {
            vhdl_standard_ = frontend::VhdlStandard::Vhdl2002;
        } else if (standard == "2019") {
            vhdl_standard_ = frontend::VhdlStandard::Vhdl2019;
        }
        return;
    }
    if (selected->systemverilog == nullptr) {
        return;
    }
    const auto& standard = selected->systemverilog->standard;
    if (standard == "2005") {
        systemverilog_standard_
            = frontend::StandardRevision::SystemVerilog2005;
    } else if (standard == "2009") {
        systemverilog_standard_
            = frontend::StandardRevision::SystemVerilog2009;
    } else if (standard == "2012") {
        systemverilog_standard_
            = frontend::StandardRevision::SystemVerilog2012;
    } else if (standard == "2023") {
        systemverilog_standard_
            = frontend::StandardRevision::SystemVerilog2023;
    }
}

void Lowerer::set_hir_container_declaration_bindings(
    const ContainerDeclarationBindings* const bindings) noexcept
{
    container_declaration_bindings_ = bindings;
}

void Lowerer::set_systemverilog_interface_handles(
    const std::unordered_map<std::string, std::uint64_t>* const handles,
    const std::unordered_map<std::string, std::string>* const types)
    noexcept
{
    systemverilog_interface_handles_ = handles;
    if (types != nullptr) {
        systemverilog_interface_types_ = types;
    }
}

std::string Lowerer::hir_generic_interface_type(
    const std::string_view receiver) const
{
    if (systemverilog_interface_types_ == nullptr || receiver.empty()) {
        return { };
    }
    const auto path = hierarchy_.empty()
        ? std::string { receiver }
        : hierarchy_ + "." + std::string { receiver };
    const auto found = systemverilog_interface_types_->find(path);
    return found == systemverilog_interface_types_->end()
        ? std::string { }
        : found->second;
}

frontend::SourceSpan Lowerer::hir_source_span(
    const semantic::SourceSpanId source) const
{
    frontend::SourceSpan result;
    if (specialized_hir_unit_ == nullptr) {
        return result;
    }
    const auto& model = specialized_hir_unit_->design().semantics;
    const auto& spans = model.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = {
        static_cast<std::size_t>(span.begin.offset),
        span.begin.line,
        span.begin.column,
    };
    result.end = {
        static_cast<std::size_t>(span.end.offset),
        span.end.line,
        span.end.column,
    };
    const auto& files = model.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    const auto& expansions = model.expansions();
    auto expansion = span.expansion;
    while (expansion && expansion->valid()
        && expansion->value() < expansions.size()) {
        const auto& record = expansions[expansion->value()];
        result.expansion_stack.push_back(record.description);
        expansion = record.parent;
    }
    std::ranges::reverse(result.expansion_stack);
    return result;
}

bool Lowerer::hir_vhdl_active_package_constant(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr
        || hir_generic_binding_frames_.empty()) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name
        || !declaration || declaration->vhdl == nullptr
        || declaration->vhdl->form
            != semantic::vhdl::DeclarationForm::constant) {
        return false;
    }
    const auto& scopes
        = specialized_hir_unit_->design().semantics.scopes();
    const auto owner_unit = [&](const semantic::ScopeId scope)
        -> std::optional<semantic::UnitId> {
        const auto record = std::ranges::find(
            scopes, scope, &semantic::Scope::id);
        return record != scopes.end()
            ? std::optional { record->unit }
            : std::nullopt;
    };
    const auto owner = owner_unit(declaration->vhdl->scope);
    const auto frame_has_package_generic = [&](
                                               const std::vector<HirGenericBinding>& frame) {
        return std::ranges::any_of(
            frame,
            [&](const HirGenericBinding& binding) {
                const auto formal
                    = specialized_hir_unit_->find_declaration(
                        binding.formal);
                return formal && formal->vhdl != nullptr
                    && formal->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_constant
                    && owner_unit(formal->vhdl->scope) == owner;
            });
    };
    return owner && std::ranges::any_of(hir_generic_binding_frames_, frame_has_package_generic);
}

std::optional<std::int64_t> Lowerer::hir_constant_integer(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return hir_constant_integer(*actual);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_constant_integer(*actual);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = hir_constant_integer(declaration->expression);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto frame_sensitive_package_constant
        = hir_vhdl_active_package_constant(expression_id);
    std::unordered_set<std::uint32_t> visiting;
    const auto inspect = [&](const auto& self,
                             const semantic::ExpressionId candidate)
        -> std::optional<std::int64_t> {
        if (!candidate.valid()
            || !visiting.insert(candidate.value()).second) {
            return std::nullopt;
        }
        const auto finish = [&](std::optional<std::int64_t> result) {
            visiting.erase(candidate.value());
            return result;
        };
        if (active_hir_callable_
            && *active_hir_callable_ < hir_callable_frames_.size()) {
            const auto declaration = hir_referenced_declaration(candidate);
            if (declaration) {
                const auto& bindings = hir_callable_frames_[
                    *active_hir_callable_].static_integer_bindings;
                const auto binding = std::ranges::find_if(
                    bindings, [&](const auto& entry) {
                        return entry.first == *declaration;
                    });
                if (binding != bindings.end()) {
                    return finish(binding->second);
                }
            }
        }
        if (const auto actual = hir_generic_actual(candidate)) {
            return finish(self(self, *actual));
        }
        if (!frame_sensitive_package_constant) {
            if (const auto specialized
                = hir_pure_integral_attempt(candidate)) {
                return finish(specialized);
            }
        }
        const auto expression = specialized_hir_unit_->find_expression(
            candidate);
        if (!expression) {
            return finish(std::nullopt);
        }
        if (expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call) {
            const auto attribute = hir_vhdl_attribute_profile(
                candidate, hir_process_scope_);
            if (attribute && attribute->constant) {
                return finish(attribute->constant);
            }
            if (expression->vhdl->operands.size() == 1U) {
                const auto target = hir_referenced_declaration(candidate);
                const auto declaration = target
                    ? specialized_hir_unit_->find_declaration(*target)
                    : std::nullopt;
                const auto type_conversion = declaration
                    && declaration->vhdl != nullptr
                    && (declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::type
                        || declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::subtype);
                if (type_conversion
                    || expression->vhdl->text.starts_with(
                        "@vhdl-qualified:")) {
                    return finish(self(
                        self, expression->vhdl->operands.front()));
                }
            }
        }
        if (expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal) {
            collect_hir_vhdl_character_contexts();
            if (hir_vhdl_character_context_literals_.contains(
                    candidate.value())) {
                if (const auto code
                    = hir_vhdl_character_literal_code(candidate, 0U)) {
                    return finish(static_cast<std::int64_t>(*code));
                }
            }
        }
        if (expression->vhdl != nullptr
            && (expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name
                || expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::logic_literal)) {
            const auto spelling
                = std::string_view { expression->vhdl->text };
            std::optional<std::int64_t> ordinal;
            std::optional<semantic::DeclarationId> literal_declaration;
            bool ambiguous { };
            const auto consider_types = [&](const auto& types) {
                for (const auto& type : types) {
                    for (const auto& literal :
                        type.enumeration_literals) {
                        if (!same_hir_identifier(
                                literal.spelling, spelling, true)) {
                            continue;
                        }
                        if (literal_declaration
                            && *literal_declaration
                                != literal.declaration) {
                            ambiguous = true;
                            continue;
                        }
                        literal_declaration = literal.declaration;
                        ordinal = literal.ordinal;
                    }
                }
            };
            consider_types(specialized_hir_unit_->vhdl_types());
            consider_types(
                specialized_hir_unit_->design().vhdl_hir.types());
            if (ordinal && !ambiguous) {
                return finish(ordinal);
            }
            if (const auto code
                = hir_vhdl_character_literal_code(candidate, 0U)) {
                return finish(static_cast<std::int64_t>(*code));
            }
        }
        if (!frame_sensitive_package_constant
            && hir_expression_is_residual(candidate)) {
            return finish(std::nullopt);
        }
        auto declaration = hir_referenced_declaration(candidate);
        if (!declaration && expression->vhdl != nullptr
            && expression->vhdl->referenced_name) {
            for (const auto candidate_declaration :
                hir_vhdl_package_member_candidates(
                    *expression->vhdl->referenced_name,
                    expression->vhdl->scope)) {
                const auto record = specialized_hir_unit_->find_declaration(
                    candidate_declaration);
                if (!record || record->vhdl == nullptr
                    || (record->vhdl->form
                            != semantic::vhdl::DeclarationForm::constant
                        && record->vhdl->form
                            != semantic::vhdl::DeclarationForm::
                                generic_constant)) {
                    continue;
                }
                if (declaration && *declaration != candidate_declaration) {
                    declaration.reset();
                    break;
                }
                declaration = candidate_declaration;
            }
        }
        if (declaration) {
            const auto record
                = specialized_hir_unit_->find_declaration(*declaration);
            if (record && record->vhdl != nullptr
                && record->vhdl->form
                    == semantic::vhdl::DeclarationForm::
                        enumeration_literal) {
                const auto ordinal = [&](const auto& types)
                    -> std::optional<std::int64_t> {
                    for (const auto& type : types) {
                        const auto literal = std::ranges::find(
                            type.enumeration_literals, *declaration,
                            &semantic::vhdl::EnumerationLiteral::
                                declaration);
                        if (literal != type.enumeration_literals.end()) {
                            return literal->ordinal;
                        }
                    }
                    return std::nullopt;
                };
                if (const auto value = ordinal(
                        specialized_hir_unit_->vhdl_types())) {
                    return finish(value);
                }
                if (const auto value = ordinal(
                        specialized_hir_unit_->design().vhdl_hir.types())) {
                    return finish(value);
                }
            }
            const auto initializer = hir_constant_initializer(*declaration);
            return finish(initializer
                    ? self(self, *initializer)
                    : std::nullopt);
        }
        std::span<const semantic::ExpressionId> operands;
        std::string_view operation;
        if (expression->systemverilog != nullptr) {
            const auto& source = *expression->systemverilog;
            if (source.kind
                    == semantic::sv::ExpressionKind::integer_literal
                || source.kind
                    == semantic::sv::ExpressionKind::boolean_literal
                || source.kind
                    == semantic::sv::ExpressionKind::logic_literal) {
                return finish(constant_integer_text(source.text));
            }
            if (const auto cast = hir_systemverilog_cast_profile(candidate);
                cast && source.operands.size() == 1U) {
                return finish(self(self, source.operands.front()));
            }
            if (source.kind != semantic::sv::ExpressionKind::unary
                && source.kind != semantic::sv::ExpressionKind::binary) {
                return finish(std::nullopt);
            }
            operands = source.operands;
            operation = source.text;
        } else {
            const auto& source = *expression->vhdl;
            if (source.kind
                    == semantic::vhdl::ExpressionKind::integer_literal
                || source.kind
                    == semantic::vhdl::ExpressionKind::boolean_literal
                || source.kind
                    == semantic::vhdl::ExpressionKind::logic_literal) {
                return finish(constant_integer_text(source.text));
            }
            if (source.kind != semantic::vhdl::ExpressionKind::unary
                && source.kind != semantic::vhdl::ExpressionKind::binary) {
                return finish(std::nullopt);
            }
            operands = source.operands;
            operation = source.text;
        }
        if (operands.size() == 1U) {
            const auto operand = self(self, operands.front());
            if (!operand) {
                return finish(std::nullopt);
            }
            if (operation.empty() || operation == "+") {
                return finish(*operand);
            }
            if (operation == "-") {
                return finish(
                    *operand == std::numeric_limits<std::int64_t>::min()
                        ? std::nullopt
                        : std::optional<std::int64_t> { -*operand });
            }
            if (operation == "!" || operation == "not") {
                return finish(*operand == 0 ? 1 : 0);
            }
            if (operation == "~") {
                return finish(~*operand);
            }
            if (operation == "abs") {
                return finish(
                    *operand == std::numeric_limits<std::int64_t>::min()
                        ? std::nullopt
                        : std::optional<std::int64_t> {
                              *operand < 0 ? -*operand : *operand });
            }
            return finish(std::nullopt);
        }
        if (operands.size() != 2U) {
            return finish(std::nullopt);
        }
        const auto left = self(self, operands.front());
        const auto right = self(self, operands.back());
        if (!left || !right) {
            return finish(std::nullopt);
        }
        const auto checked_add = [](const std::int64_t lhs,
                                     const std::int64_t rhs)
            -> std::optional<std::int64_t> {
            if ((rhs > 0
                    && lhs
                        > std::numeric_limits<std::int64_t>::max() - rhs)
                || (rhs < 0
                    && lhs
                        < std::numeric_limits<std::int64_t>::min() - rhs)) {
                return std::nullopt;
            }
            return lhs + rhs;
        };
        const auto checked_subtract = [](const std::int64_t lhs,
                                          const std::int64_t rhs)
            -> std::optional<std::int64_t> {
            if ((rhs < 0
                    && lhs
                        > std::numeric_limits<std::int64_t>::max() + rhs)
                || (rhs > 0
                    && lhs
                        < std::numeric_limits<std::int64_t>::min() + rhs)) {
                return std::nullopt;
            }
            return lhs - rhs;
        };
        const auto checked_multiply = [](const std::int64_t lhs,
                                          const std::int64_t rhs)
            -> std::optional<std::int64_t> {
            if (lhs == 0 || rhs == 0) {
                return 0;
            }
            if (lhs > 0
                && ((rhs > 0
                        && lhs
                            > std::numeric_limits<std::int64_t>::max()
                                / rhs)
                    || (rhs < 0
                        && rhs
                            < std::numeric_limits<std::int64_t>::min()
                                / lhs))) {
                return std::nullopt;
            }
            if (lhs < 0
                && ((rhs > 0
                        && lhs
                            < std::numeric_limits<std::int64_t>::min()
                                / rhs)
                    || (rhs < 0
                        && lhs
                            < std::numeric_limits<std::int64_t>::max()
                                / rhs))) {
                return std::nullopt;
            }
            return lhs * rhs;
        };
        if (operation == "+") {
            return finish(checked_add(*left, *right));
        }
        if (operation == "-") {
            return finish(checked_subtract(*left, *right));
        }
        if (operation == "*") {
            return finish(checked_multiply(*left, *right));
        }
        if (operation == "/" || operation == "div") {
            return finish(
                *right == 0
                        || (*left
                                == std::numeric_limits<std::int64_t>::min()
                            && *right == -1)
                    ? std::nullopt
                    : std::optional<std::int64_t> { *left / *right });
        }
        if (operation == "%" || operation == "mod"
            || operation == "rem") {
            return finish(
                *right == 0
                        || (*left
                                == std::numeric_limits<std::int64_t>::min()
                            && *right == -1)
                    ? std::nullopt
                    : std::optional<std::int64_t> { *left % *right });
        }
        if (operation == "<") {
            return finish(*left < *right ? 1 : 0);
        }
        if (operation == "<=") {
            return finish(*left <= *right ? 1 : 0);
        }
        if (operation == ">") {
            return finish(*left > *right ? 1 : 0);
        }
        if (operation == ">=") {
            return finish(*left >= *right ? 1 : 0);
        }
        if (operation == "==" || operation == "==="
            || operation == "=") {
            return finish(*left == *right ? 1 : 0);
        }
        if (operation == "!=" || operation == "!=="
            || operation == "/=") {
            return finish(*left != *right ? 1 : 0);
        }
        if (operation == "&&" || operation == "and") {
            return finish(*left != 0 && *right != 0 ? 1 : 0);
        }
        if (operation == "||" || operation == "or") {
            return finish(*left != 0 || *right != 0 ? 1 : 0);
        }
        if (operation == "&") {
            return finish(*left & *right);
        }
        if (operation == "|") {
            return finish(*left | *right);
        }
        if (operation == "^" || operation == "xor") {
            return finish(*left ^ *right);
        }
        if (operation == "xnor") {
            return finish(~(*left ^ *right));
        }
        if (operation == "<<" || operation == "sll"
            || operation == "sla") {
            if (*right < 0 || *right >= 63 || *left < 0
                || static_cast<std::uint64_t>(*left)
                    > (static_cast<std::uint64_t>(
                           std::numeric_limits<std::int64_t>::max())
                        >> static_cast<unsigned>(*right))) {
                return finish(std::nullopt);
            }
            return finish(*left << static_cast<unsigned>(*right));
        }
        if (operation == ">>" || operation == "srl"
            || operation == "sra") {
            return finish(
                *right < 0 || *right >= 63
                    ? std::nullopt
                    : std::optional<std::int64_t> {
                          *left >> static_cast<unsigned>(*right) });
        }
        return finish(std::nullopt);
    };
    return inspect(inspect, expression_id);
}

std::optional<semantic::ExpressionId> Lowerer::hir_constant_initializer(
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration) {
        return std::nullopt;
    }
    if (declaration->systemverilog != nullptr) {
        const auto& source = *declaration->systemverilog;
        return source.form == semantic::sv::DeclarationForm::parameter
                || source.form
                    == semantic::sv::DeclarationForm::local_parameter
            ? source.initializer
            : std::nullopt;
    }
    const auto& source = *declaration->vhdl;
    return source.form == semantic::vhdl::DeclarationForm::constant
            || source.form
                == semantic::vhdl::DeclarationForm::generic_constant
        ? source.initializer
        : std::nullopt;
}

std::optional<std::optional<Lowerer::HirPackedRange>>
Lowerer::hir_vhdl_callable_formal_range(
    const semantic::DeclarationId declaration) const
{
    if (!active_hir_callable_
        || *active_hir_callable_ >= hir_callable_frames_.size()) {
        return std::nullopt;
    }
    const auto& ranges = hir_callable_frames_[*active_hir_callable_]
                             .vhdl_formal_ranges;
    const auto found = ranges.find(declaration.value());
    if (found == ranges.end()) {
        return std::nullopt;
    }
    std::optional<std::optional<HirPackedRange>> result;
    result.emplace(found->second);
    return result;
}

std::optional<Lowerer::HirPackedRange> Lowerer::hir_expression_range(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_expression_range(*actual, process_scope);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = hir_expression_range(
            declaration->expression, process_scope);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    const auto width = hir_expression_width(expression_id, process_scope);
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    if (const auto member = hir_systemverilog_member_selection(
            expression_id);
        member && member->range) {
        return member->range;
    }
    if (expression->systemverilog != nullptr) {
        // A multidimensional packed array is indexed by its outermost
        // dimension.
        const auto shape = hir_systemverilog_packed_shape(expression_id);
        if (!shape.empty()) {
            return shape.front();
        }
    }
    if (expression->vhdl != nullptr) {
        if (expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name) {
            const auto declaration = hir_target_declaration(expression_id);
            const auto callable_range = declaration
                ? hir_vhdl_callable_formal_range(*declaration)
                : std::nullopt;
            if (callable_range) {
                if (!*callable_range) {
                    return std::nullopt;
                }
                const auto& range = **callable_range;
                const auto distance = index_distance(
                    range.left, range.right);
                if (distance
                        == std::numeric_limits<std::uint64_t>::max()
                    || distance + 1U != *width
                    || (range.left != range.right
                        && range.descending
                            != (range.left > range.right))) {
                    return std::nullopt;
                }
                return range;
            }
        }
        const auto declaration = hir_target_declaration(expression_id);
        const auto binding = declaration
            ? hir_runtime_binding(*declaration, process_scope, false)
            : std::nullopt;
        const VhdlArrayMetadata* occurrence_array = nullptr;
        if (binding && binding->signal
            && *binding->signal < design_.signal_info_.size()) {
            occurrence_array = design_.signal_info_[*binding->signal]
                                   .vhdl_array.get();
        }
        if (occurrence_array != nullptr
            && !occurrence_array->dimensions.empty()) {
            const auto& dimension
                = occurrence_array->dimensions.front();
            if (dimension.range && !dimension.null
                && dimension.stride != 0U) {
                const auto count = index_distance(
                                       dimension.range->left,
                                       dimension.range->right)
                    + 1U;
                if (count
                        <= std::numeric_limits<std::size_t>::max()
                            / dimension.stride
                    && count * dimension.stride == *width) {
                    return HirPackedRange {
                        dimension.range->left,
                        dimension.range->right,
                        dimension.range->descending,
                    };
                }
            }
        }
        const auto subtype = hir_vhdl_expression_subtype(expression_id);
        const semantic::vhdl::RangeConstraint* selected_range = nullptr;
        const semantic::vhdl::TypeDefinition* array = nullptr;
        if (subtype && !subtype->constraints.empty()) {
            selected_range = &subtype->constraints.front();
        }
        if (subtype && subtype->type_mark.target.valid()) {
            array = vhdl_array_definition(
                *specialized_hir_unit_, subtype->type_mark.target);
            if (selected_range == nullptr && array != nullptr
                && !array->array_dimensions.empty()
                && array->array_dimensions.front().constraint) {
                selected_range
                    = &*array->array_dimensions.front().constraint;
            }
        }
        if (selected_range != nullptr && !selected_range->null) {
            const auto left = selected_range->left
                ? selected_range->left
                : selected_range->left_expression
                ? hir_constant_integer(
                      *selected_range->left_expression)
                : std::nullopt;
            const auto right = selected_range->right
                ? selected_range->right
                : selected_range->right_expression
                ? hir_constant_integer(
                      *selected_range->right_expression)
                : std::nullopt;
            auto element_width = std::optional<std::size_t> { 1U };
            if (array != nullptr) {
                const auto element = array->element_subtype
                    ? hir_effective_vhdl_subtype(*array->element_subtype)
                    : std::nullopt;
                element_width = element
                    ? vhdl_runtime_width(*element)
                    : std::nullopt;
            }
            const auto count = left && right
                ? std::optional {
                      index_distance(*left, *right) + 1U
                  }
                : std::nullopt;
            const auto descending
                = selected_range->direction_from_bounds && left && right
                ? *left > *right
                : selected_range->descending;
            if (left && right && count && element_width
                && *count
                    <= std::numeric_limits<std::size_t>::max()
                        / *element_width
                && *count * *element_width == *width
                && (*left == *right || descending == (*left > *right))) {
                return HirPackedRange { *left, *right, descending };
            }
        }
    }
    auto selected = hir_referenced_declaration(expression_id);
    if (!selected && expression->systemverilog != nullptr) {
        // Selecting an unpacked element preserves its declared packed range;
        // it does not acquire the default [width-1:0] expression range.
        if (const auto element = hir_container_element_binding(expression_id);
            element && element->selected_type == element->type) {
            selected = element->declaration;
        }
    }
    if (selected) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *selected);
        if (!declaration) {
            return std::nullopt;
        }
        if (declaration->systemverilog != nullptr) {
            const auto& source = *declaration->systemverilog;
            if (source.type && source.type->packed_range
                && source.type->packed_range->left
                && source.type->packed_range->right) {
                const auto& range = *source.type->packed_range;
                if (index_distance(*range.left, *range.right) + 1U
                        != *width
                    || (*range.left != *range.right
                        && range.descending
                            != (*range.left > *range.right))) {
                    return std::nullopt;
                }
                return HirPackedRange {
                    *range.left, *range.right, range.descending
                };
            }
        } else {
            const auto& source = *declaration->vhdl;
            if (source.subtype && !source.subtype->constraints.empty()) {
                const auto& range = source.subtype->constraints.front();
                if (!range.null && range.left && range.right
                    && index_distance(*range.left, *range.right) + 1U
                        == *width
                    && (*range.left == *range.right
                        || range.descending
                            == (*range.left > *range.right))) {
                    return HirPackedRange {
                        *range.left, *range.right, range.descending
                    };
                }
                return std::nullopt;
            }
            if (*width != 1U) {
                return std::nullopt;
            }
        }
    } else if (expression->vhdl != nullptr) {
        return std::nullopt;
    }
    if (*width - 1U
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return HirPackedRange {
        static_cast<std::int64_t>(*width - 1U), 0, true
    };
}

std::optional<Lowerer::HirConstantSelection>
Lowerer::hir_constant_selection(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    std::span<const semantic::ExpressionId> operands;
    std::string_view operation;
    bool index = false;
    bool slice = false;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        operands = source.operands;
        operation = source.text;
        index = source.kind == semantic::sv::ExpressionKind::index;
        slice = source.kind == semantic::sv::ExpressionKind::slice;
    } else {
        const auto& source = *expression->vhdl;
        operands = source.operands;
        operation = source.text;
        index = source.kind == semantic::vhdl::ExpressionKind::index;
        slice = source.kind == semantic::vhdl::ExpressionKind::slice;
    }
    if ((!index && !slice) || operands.size() != (index ? 2U : 3U)) {
        return std::nullopt;
    }
    if (std::ranges::any_of(
            std::span { operands }.subspan(1U),
            [&](const semantic::ExpressionId bound) {
                return has_runtime_systemverilog_call(
                    *specialized_hir_unit_, bound);
            })) {
        return std::nullopt;
    }
    if (index && expression->systemverilog != nullptr) {
        const auto declaration_id = hir_referenced_declaration(
            operands.front());
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        const auto* type = declaration
                && declaration->systemverilog != nullptr
            ? declaration->systemverilog->type
                ? &*declaration->systemverilog->type
                : nullptr
            : nullptr;
        if (type != nullptr
            && type->container_form
                == semantic::sv::TypeForm::static_array
            && type->unpacked_dimensions.size() == 1U
            && type->executable_width) {
            const auto& dimension = type->unpacked_dimensions.front();
            const auto boundary = [&](
                                      const std::optional<std::int64_t> value,
                                      const std::optional<semantic::ExpressionId>
                                          residual) {
                return value   ? value
                    : residual ? hir_constant_integer(*residual)
                               : std::nullopt;
            };
            const auto left = boundary(
                dimension.left, dimension.left_expression);
            const auto right = boundary(
                dimension.right, dimension.right_expression);
            const auto selected = hir_constant_integer(operands[1]);
            if (!left || !right || !selected
                || *selected < std::min(*left, *right)
                || *selected > std::max(*left, *right)
                || *type->executable_width
                    > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            const auto element_width = static_cast<std::size_t>(
                *type->executable_width);
            const auto ordinal = index_distance(*selected, *right);
            if (element_width == 0U
                || ordinal
                    > std::numeric_limits<std::size_t>::max()
                        / element_width) {
                return std::nullopt;
            }
            return HirConstantSelection {
                static_cast<std::size_t>(ordinal) * element_width,
                element_width,
            };
        }
    }
    if (expression->vhdl != nullptr) {
        const auto subtype = hir_vhdl_expression_subtype(
            operands.front());
        const auto* type = subtype && subtype->type_mark.target.valid()
            ? vhdl_array_definition(
                  *specialized_hir_unit_, subtype->type_mark.target)
            : nullptr;
        if (subtype && type != nullptr && type->element_subtype) {
            const auto element = hir_effective_vhdl_subtype(
                *type->element_subtype);
            auto element_width = element
                ? vhdl_runtime_width(*element)
                : std::nullopt;
            const auto constraint_bound = [&](
                                              const std::optional<
                                                  std::int64_t> value,
                                              const std::optional<
                                                  semantic::ExpressionId>
                                                  residual) {
                return value ? value
                    : residual
                    ? hir_constant_integer(*residual)
                    : std::nullopt;
            };
            const auto dimension_count = !subtype->constraints.empty()
                ? subtype->constraints.size()
                : type->array_dimensions.size();
            for (std::size_t dimension = 1U;
                element_width && dimension < dimension_count;
                ++dimension) {
                const semantic::vhdl::RangeConstraint* constraint = nullptr;
                if (dimension < subtype->constraints.size()) {
                    constraint = &subtype->constraints[dimension];
                } else if (dimension
                        < type->array_dimensions.size()
                    && type->array_dimensions[dimension].constraint) {
                    constraint
                        = &*type->array_dimensions[dimension].constraint;
                }
                const auto left = constraint != nullptr
                    ? constraint_bound(
                          constraint->left,
                          constraint->left_expression)
                    : std::nullopt;
                const auto right = constraint != nullptr
                    ? constraint_bound(
                          constraint->right,
                          constraint->right_expression)
                    : std::nullopt;
                const auto count = left && right && !constraint->null
                    ? index_distance(*left, *right) + 1U
                    : 0U;
                if (count == 0U
                    || count
                        > std::numeric_limits<std::size_t>::max()
                            / *element_width) {
                    element_width.reset();
                    break;
                }
                *element_width *= static_cast<std::size_t>(count);
            }
            const semantic::vhdl::RangeConstraint* range = nullptr;
            if (!subtype->constraints.empty()) {
                range = &subtype->constraints.front();
            } else if (!type->array_dimensions.empty()
                && type->array_dimensions.front().constraint) {
                range = &*type->array_dimensions.front().constraint;
            }
            const auto range_left = range != nullptr
                ? constraint_bound(
                      range->left, range->left_expression)
                : std::nullopt;
            const auto range_right = range != nullptr
                ? constraint_bound(
                      range->right, range->right_expression)
                : std::nullopt;
            if (element_width && *element_width != 0U && range
                && !range->null && range_left && range_right) {
                const auto in_range = [&](const std::int64_t value) {
                    return value
                        >= std::min(*range_left, *range_right)
                        && value
                        <= std::max(*range_left, *range_right);
                };
                if (index) {
                    const auto selected = hir_constant_integer(operands[1]);
                    if (!selected || !in_range(*selected)) {
                        return std::nullopt;
                    }
                    const auto ordinal = index_distance(
                        *selected, *range_right);
                    if (ordinal
                        > std::numeric_limits<std::size_t>::max()
                            / *element_width) {
                        return std::nullopt;
                    }
                    return HirConstantSelection {
                        static_cast<std::size_t>(ordinal) * *element_width,
                        *element_width,
                    };
                }
                const auto left = hir_constant_integer(operands[1]);
                const auto right = hir_constant_integer(operands[2]);
                const auto explicit_direction = operation == "to"
                    || operation == "downto";
                const auto selected_descending = explicit_direction
                    ? operation == "downto"
                    : left && right && *left > *right;
                if (left && right && in_range(*left) && in_range(*right)
                    && selected_descending == range->descending) {
                    const auto null = explicit_direction
                        && (selected_descending ? *left < *right
                                               : *left > *right);
                    const auto count = null
                        ? std::uint64_t { }
                        : index_distance(*left, *right) + 1U;
                    const auto ordinal = index_distance(
                        *right, *range_right);
                    if (count
                            > std::numeric_limits<std::size_t>::max()
                                / *element_width
                        || ordinal
                            > std::numeric_limits<std::size_t>::max()
                                / *element_width) {
                        return std::nullopt;
                    }
                    return HirConstantSelection {
                        static_cast<std::size_t>(ordinal) * *element_width,
                        static_cast<std::size_t>(count) * *element_width,
                    };
                }
            }
        }
    }
    const auto source_width = hir_expression_width(
        operands.front(), process_scope);
    const auto range = hir_expression_range(
        operands.front(), process_scope);
    if (!source_width || !range
        || std::ranges::any_of(
            std::span { operands }.subspan(1U),
            [&](const auto bound) {
                return hir_expression_is_residual(bound);
            })) {
        return std::nullopt;
    }
    const auto in_range = [&](const std::int64_t value) {
        return value >= std::min(range->left, range->right)
            && value <= std::max(range->left, range->right);
    };
    // The selected dimension's element width: one bit for a vector, the
    // width of an element for the outer dimension of a multidimensional
    // packed array (IEEE 1800-2017 7.4.5).
    std::size_t element_width { 1U };
    if (expression->systemverilog != nullptr) {
        const auto count = index_distance(range->left, range->right) + 1U;
        if (count != 0U && count < *source_width
            && *source_width % count == 0U) {
            element_width = static_cast<std::size_t>(*source_width / count);
        }
    }
    if (index) {
        const auto selected = hir_constant_integer(operands[1]);
        if (!selected || !in_range(*selected)) {
            return std::nullopt;
        }
        const auto offset = index_distance(*selected, range->right)
            * element_width;
        return offset < *source_width
            ? std::optional { HirConstantSelection {
                  static_cast<std::size_t>(offset), element_width } }
            : std::nullopt;
    }

    const auto first = hir_constant_integer(operands[1]);
    const auto second = hir_constant_integer(operands[2]);
    if (!first || !second) {
        return std::nullopt;
    }
    std::int64_t left = *first;
    std::int64_t right = *second;
    std::uint64_t width { };
    if (operation == "+:" || operation == "-:") {
        if (*second <= 0) {
            return std::nullopt;
        }
        const auto distance = *second - 1;
        std::int64_t lower { };
        std::int64_t upper { };
        if (operation == "+:") {
            if (*first
                > std::numeric_limits<std::int64_t>::max() - distance) {
                return std::nullopt;
            }
            lower = *first;
            upper = *first + distance;
        } else {
            if (*first
                < std::numeric_limits<std::int64_t>::min() + distance) {
                return std::nullopt;
            }
            lower = *first - distance;
            upper = *first;
        }
        left = range->descending ? upper : lower;
        right = range->descending ? lower : upper;
        width = static_cast<std::uint64_t>(*second);
    } else {
        const auto explicit_direction = expression->vhdl != nullptr
            && (operation == "to" || operation == "downto");
        const auto selected_descending = explicit_direction
            ? operation == "downto"
            : left >= right;
        if (selected_descending != range->descending) {
            return std::nullopt;
        }
        const auto null = explicit_direction
            && (selected_descending ? left < right : left > right);
        width = null ? 0U : index_distance(left, right) + 1U;
    }
    if (!in_range(left) || !in_range(right)
        || width > std::numeric_limits<std::size_t>::max() / element_width) {
        return std::nullopt;
    }
    width *= element_width;
    const auto offset = index_distance(right, range->right) * element_width;
    if (offset > *source_width
        || (width != 0U && offset >= *source_width)
        || width > *source_width - offset) {
        return std::nullopt;
    }
    return HirConstantSelection {
        static_cast<std::size_t>(offset),
        static_cast<std::size_t>(width),
    };
}

std::optional<Lowerer::HirConstantSelection>
Lowerer::hir_root_constant_selection(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    auto selection = hir_constant_selection(
        expression_id, process_scope);
    if (!selection || specialized_hir_unit_ == nullptr) {
        return selection;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    std::span<const semantic::ExpressionId> operands;
    bool selected = false;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        operands = source.operands;
        selected = source.kind == semantic::sv::ExpressionKind::index
            || source.kind == semantic::sv::ExpressionKind::slice;
    } else {
        const auto& source = *expression->vhdl;
        operands = source.operands;
        selected = source.kind == semantic::vhdl::ExpressionKind::index
            || source.kind == semantic::vhdl::ExpressionKind::slice;
    }
    if (!selected || operands.empty()) {
        return selection;
    }
    const auto parent_expression = specialized_hir_unit_->find_expression(
        operands.front());
    const auto parent_selected = parent_expression
        && ((parent_expression->systemverilog != nullptr
                && (parent_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::index
                    || parent_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::slice))
            || (parent_expression->vhdl != nullptr
                && (parent_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::index
                    || parent_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::slice)));
    if (!parent_selected) {
        return selection;
    }
    const auto parent = hir_root_constant_selection(
        operands.front(), process_scope);
    if (!parent || selection->offset > parent->width
        || selection->width > parent->width - selection->offset
        || parent->offset
            > std::numeric_limits<std::size_t>::max()
                - selection->offset) {
        return std::nullopt;
    }
    selection->offset += parent->offset;
    return selection;
}

bool Lowerer::hir_expression_signed(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (const auto method = hir_systemverilog_enumeration_method(
            expression_id)) {
        return method->method == "num" || (method->method != "name"
            && method->signed_value);
    }
    if (const auto binding = hir_case_pattern_binding(expression_id)) {
        return binding->signed_value;
    }
    if (const auto parameter = hir_hierarchical_parameter(expression_id)) {
        return parameter->signed_value;
    }
    if (const auto signal = hir_direct_signal_binding(expression_id);
        signal && signal->signed_value) {
        return true;
    }
    if (hir_systemverilog_interface_handle(expression_id)) {
        return false;
    }
    if (const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id)) {
        return selection->member == 3U;
    }
    if (const auto selection = hir_vhdl_array_selection(expression_id)) {
        return selection->signed_value;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        if (const auto declared = hir_systemverilog_parameter_type(
                expression_id)) {
            return declared->signed_value;
        }
        return hir_expression_signed(*actual);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_expression_signed(*actual);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return false;
        }
        const auto result = hir_expression_signed(declaration->expression);
        hir_let_frames_.pop_back();
        return result;
    }
    std::unordered_set<std::uint32_t> visiting;
    const auto inspect = [&](const auto& self,
                             const semantic::ExpressionId candidate) -> bool {
        if (!candidate.valid()
            || !visiting.insert(candidate.value()).second) {
            return false;
        }
        const auto expression = specialized_hir_unit_->find_expression(
            candidate);
        if (!expression) {
            return false;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name
            && !hir_referenced_declaration(candidate)
            && systemverilog_genvar_identity(
                *specialized_hir_unit_, expression->systemverilog->scope,
                expression->systemverilog->text)) {
            // An iterative-generate variable is an implicit signed 32-bit
            // SystemVerilog integer. Its value is retained in the active
            // hierarchy specialization rather than as a declaration.
            return true;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->signed_value) {
            return true;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::unary
            && systemverilog_reduction_operator(
                expression->systemverilog->text)) {
            return false;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && expression->systemverilog->text == "inside") {
            return false;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && (expression->systemverilog->text == "$signed"
                || expression->systemverilog->text == "$unsigned")) {
            return expression->systemverilog->text == "$signed";
        }
        if (expression->vhdl != nullptr
            && frontend::vhdl_simulator_api(expression->vhdl->text)
                == frontend::VhdlSimulatorApi::file_line) {
            return true;
        }
        if (expression->vhdl != nullptr
            && frontend::vhdl_simulator_api(expression->vhdl->text)
                == frontend::VhdlSimulatorApi::get_vhdl_assert_count) {
            return true;
        }
        if (expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::integer_literal) {
            // VHDL integer literals are values of a signed integer type.
            // Preserve that semantic classification so integer-to-vector
            // conversions extend a negative literal with its sign bit.
            return true;
        }
        if (const auto cast = hir_systemverilog_cast_profile(candidate)) {
            return cast->signed_value;
        }
        if (const auto conversion = hir_vhdl_conversion_profile(candidate)) {
            return conversion->signed_value;
        }
        if (const auto profile = hir_vhdl_float_function_profile(candidate)) {
            return profile->signed_value;
        }
        if (const auto profile = hir_vhdl_fixed_function_profile(candidate)) {
            return profile->signed_value;
        }
        if (const auto standard = hir_vhdl_standard_function_profile(
                candidate)) {
            return standard->signed_value;
        }
        if (const auto member = hir_systemverilog_member_selection(
                candidate)) {
            return member->signed_value;
        }
        if (const auto member = hir_vhdl_member_selection(candidate)) {
            return member->signed_value;
        }
        if (const auto element = hir_container_element_binding(candidate)) {
            return element->signed_value;
        }
        if (expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name) {
            // A port read through its connected actual still has the
            // port's declared signedness (IEEE 1800-2017 23.3.3.7).
            const semantic::CompiledDesignResolver resolver {
                *specialized_hir_unit_, hir_generic_binding_frames_
            };
            const auto formal
                = resolver.resolve_systemverilog_expression(candidate)
                      .unique();
            const auto port = formal
                ? specialized_hir_unit_->find_declaration(*formal)
                : std::nullopt;
            if (port && port->systemverilog != nullptr
                && port->systemverilog->form
                    == semantic::sv::DeclarationForm::port
                && port->systemverilog->type) {
                return port->systemverilog->type->signed_value;
            }
        }
        if (const auto declaration_id = hir_referenced_declaration(candidate)) {
            const auto declaration = specialized_hir_unit_->find_declaration(
                *declaration_id);
            if (!declaration) {
                return false;
            }
            const auto declaration_name
                = declaration->systemverilog != nullptr
                ? std::string_view { declaration->systemverilog->name }
                : std::string_view { declaration->vhdl->name };
            const auto signal = signals_.find(
                std::string { declaration_name });
            if (signal != signals_.end()
                && signal->second < design_.signal_info_.size()
                && design_.signal_info_[signal->second].is_signed) {
                return true;
            }
            if (declaration->systemverilog != nullptr) {
                const auto& source = *declaration->systemverilog;
                // A parameter without a type or a range takes the
                // signedness of its value (IEEE 1800-2017 6.20.2).
                if ((source.form == semantic::sv::DeclarationForm::parameter
                        || source.form
                            == semantic::sv::DeclarationForm::local_parameter)
                    && source.initializer && source.type
                    && source.type->target.spelling == "implicit"
                    && !source.type->packed_range) {
                    return self(self, *source.initializer);
                }
                return source.type && source.type->signed_value;
            }
            const auto subtype = declaration->vhdl->subtype
                ? hir_effective_vhdl_subtype(
                      *declaration->vhdl->subtype)
                : std::nullopt;
            if (subtype) {
                auto type_name = std::string_view {
                    subtype->type_mark.spelling
                };
                if (const auto separator = type_name.find_last_of('.');
                    separator != std::string_view::npos) {
                    type_name.remove_prefix(separator + 1U);
                }
                const auto numeric_context
                    = hir_vhdl_synopsys_numeric_context(
                        expression->vhdl->scope);
                if (same_hir_identifier(
                        type_name, "std_logic_vector", true)
                    && numeric_context.signed_visible
                    && !numeric_context.unsigned_visible) {
                    return true;
                }
            }
            return subtype
                && (subtype->signed_value
                    || subtype->domain
                        == semantic::vhdl::ValueDomain::integer);
        }
        const auto binary_signed = [&](const std::string_view operation,
                                       const auto& operands,
                                       const bool vhdl) {
            if (operands.size() != 2U) {
                return false;
            }
            if (operation == "<<" || operation == ">>"
                || operation == "<<<" || operation == ">>>"
                || operation == "sll" || operation == "srl"
                || operation == "sla" || operation == "sra"
                || operation == "rol" || operation == "ror") {
                return self(self, operands.front());
            }
            if (operation == "==" || operation == "==="
                || operation == "!==" || operation == "!="
                || operation == "=" || operation == "/="
                || operation == "?=" || operation == "?/="
                || operation == "<" || operation == "<="
                || operation == ">" || operation == ">="
                || operation == "&&" || operation == "||"
                || (vhdl && operation == "&")) {
                return false;
            }
            return self(self, operands[0])
                && self(self, operands[1]);
        };
        if (expression->systemverilog != nullptr) {
            const auto& source = *expression->systemverilog;
            using Kind = semantic::sv::ExpressionKind;
            if ((source.kind == Kind::unary
                    || source.kind == Kind::update)
                && source.operands.size() == 1U) {
                return self(self, source.operands.front());
            }
            if (source.kind == Kind::binary) {
                return binary_signed(
                    source.text, source.operands, false);
            }
            if (source.kind == Kind::call && source.text == "?:"
                && source.operands.size() == 3U) {
                return self(self, source.operands[1])
                    && self(self, source.operands[2]);
            }
            return false;
        }
        const auto& source = *expression->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        if ((source.kind == Kind::unary || source.kind == Kind::update)
            && source.operands.size() == 1U) {
            return self(self, source.operands.front());
        }
        if (source.kind == Kind::binary) {
            return binary_signed(source.text, source.operands, true);
        }
        if (source.kind == Kind::conditional
            && source.operands.size() == 3U) {
            return self(self, source.operands[1])
                && self(self, source.operands[2]);
        }
        return false;
    };
    return inspect(inspect, expression_id);
}

bool Lowerer::hir_dynamic_index_supported(
    const semantic::ExpressionId source,
    const semantic::ExpressionId index,
    const semantic::ScopeId process_scope) const
{
    const auto runtime_call = specialized_hir_unit_ != nullptr
        && has_runtime_systemverilog_call(*specialized_hir_unit_, index);
    if ((!runtime_call && hir_constant_integer(index))
        || hir_expression_is_residual(index)) {
        return false;
    }
    const auto range = hir_expression_range(source, process_scope);
    const auto source_width = hir_expression_width(source, process_scope);
    const auto index_width = hir_expression_width(index, process_scope);
    const auto index_domain = hir_expression_domain(index, process_scope);
    if (!range || !source_width || *source_width == 0U
        || !index_width || *index_width == 0U || !index_domain) {
        return false;
    }
    const auto range_width = index_distance(range->left, range->right) + 1U;
    const auto index_expression = specialized_hir_unit_->find_expression(index);
    if (!index_expression) {
        return false;
    }
    if (index_expression->systemverilog != nullptr) {
        return range_width == *source_width
            && scalar_domain(*index_domain)
            && range->left >= std::numeric_limits<std::int32_t>::min()
            && range->left <= std::numeric_limits<std::int32_t>::max()
            && range->right >= std::numeric_limits<std::int32_t>::min()
            && range->right <= std::numeric_limits<std::int32_t>::max();
    }
    const auto subtype = hir_vhdl_expression_subtype(source);
    const auto array = subtype && subtype->type_mark.target.valid()
        ? vhdl_array_definition(
              *specialized_hir_unit_, subtype->type_mark.target)
        : nullptr;
    const auto predefined_array = subtype && array == nullptr
        && subtype->constraints.size() == 1U
        && (same_hir_identifier(
                subtype->type_mark.spelling, "bit_vector", true)
            || same_hir_identifier(
                subtype->type_mark.spelling, "std_logic_vector", true)
            || same_hir_identifier(
                subtype->type_mark.spelling, "std_ulogic_vector", true)
            || same_hir_identifier(
                subtype->type_mark.spelling, "signed", true)
            || same_hir_identifier(
                subtype->type_mark.spelling, "unsigned", true));
    const auto element = array != nullptr && array->element_subtype
        ? hir_effective_vhdl_subtype(*array->element_subtype)
        : std::nullopt;
    const auto element_width = predefined_array
        ? std::optional<std::size_t> { 1U }
        : element
        ? vhdl_runtime_width(*element)
        : std::nullopt;
    if ((!predefined_array
            && (array == nullptr
                || array->array_dimensions.size() != 1U))
        || (!predefined_array && !element) || !element_width
        || (!predefined_array
            && !scalar_domain(vhdl_domain(element->domain)))
        || range_width
            > std::numeric_limits<std::size_t>::max() / *element_width
        || range_width * *element_width != *source_width) {
        return false;
    }
    return ((*index_domain == frontend::ValueDomain::Integer
                && (*index_width == 32U || *index_width == 64U))
            || hir_vhdl_enumeration_index(index, process_scope))
        && range_width - 1U
        <= static_cast<std::uint64_t>(
            std::numeric_limits<std::int32_t>::max());
}

std::optional<semantic::TypeId>
Lowerer::hir_vhdl_loop_parameter_enumeration(
    const semantic::ExpressionId value) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto& unit = *specialized_hir_unit_;
    const auto expression = unit.find_expression(value);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
        || !expression->vhdl->referenced_name
        || !expression->vhdl->referenced_name->selected) {
        return std::nullopt;
    }
    const auto declaration = unit.find_declaration(
        *expression->vhdl->referenced_name->selected);
    if (!declaration || declaration->vhdl == nullptr
        || declaration->vhdl->form
            != semantic::vhdl::DeclarationForm::constant) {
        return std::nullopt;
    }
    const auto loop_scope = declaration->vhdl->scope;
    const auto& statements = unit.design().vhdl_hir.statements();
    const auto declared_loop = std::ranges::find_if(
        statements, [&](const semantic::vhdl::Statement& statement) {
            return statement.nested_scope == loop_scope
                && statement.loop_initial.has_value();
        });
    if (declared_loop == statements.end()) {
        return std::nullopt;
    }
    const auto specialized_loop = unit.find_statement(declared_loop->id);
    const auto* loop = specialized_loop && specialized_loop->vhdl != nullptr
            && specialized_loop->vhdl->loop_initial
        ? specialized_loop->vhdl
        : &*declared_loop;
    const auto enumeration_of = [&](const std::optional<
                                    semantic::vhdl::SubtypeIndication>&
                                        subtype)
        -> std::optional<semantic::TypeId> {
        const auto* definition = subtype
            ? vhdl_enumeration_definition(unit, subtype->type_mark.target)
            : nullptr;
        return definition != nullptr ? std::optional { definition->id }
                                     : std::nullopt;
    };
    const auto initial = unit.find_expression(*loop->loop_initial);
    if (!initial || initial->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& range = *initial->vhdl;
    if (range.kind == semantic::vhdl::ExpressionKind::call
        && !range.operands.empty()
        && (range.text == "'left" || range.text == "'right")) {
        return enumeration_of(hir_vhdl_type_actual(range.operands.front()));
    }
    if (range.kind == semantic::vhdl::ExpressionKind::call
        && !range.operands.empty()
        && (range.text == "'range" || range.text == "'reverse_range")) {
        const auto prefix = range.operands.front();
        auto subtype = hir_vhdl_type_actual(prefix);
        if (!subtype) {
            subtype = hir_vhdl_expression_subtype(prefix);
        }
        if (subtype && !subtype->type_mark.target.valid()) {
            if (auto effective = hir_effective_vhdl_subtype(*subtype)) {
                subtype = std::move(effective);
            }
        }
        std::size_t dimension { };
        if (range.operands.size() == 2U) {
            const auto selected = hir_constant_integer(range.operands[1]);
            if (!selected || *selected < 1) {
                return std::nullopt;
            }
            dimension = static_cast<std::size_t>(*selected - 1);
        }
        const auto* array = subtype
            ? vhdl_array_definition(unit, subtype->type_mark.target)
            : nullptr;
        if (array == nullptr
            || dimension >= array->array_dimensions.size()) {
            return std::nullopt;
        }
        const auto& index_subtype
            = array->array_dimensions[dimension].index_subtype;
        if (index_subtype.selected) {
            const auto index_type = unit.find_declaration(
                *index_subtype.selected);
            const auto* definition = index_type
                    && index_type->vhdl != nullptr
                    && index_type->vhdl->declared_type
                ? vhdl_enumeration_definition(
                      unit, *index_type->vhdl->declared_type)
                : nullptr;
            return definition != nullptr
                ? std::optional { definition->id }
                : std::nullopt;
        }
        // `T range <>` index subtypes are named but not bound.
        auto name = std::string_view { index_subtype.spelling };
        if (const auto separator = name.find_last_of(".:");
            separator != std::string_view::npos) {
            name.remove_prefix(separator + 1U);
        }
        std::optional<semantic::TypeId> found;
        for (const auto& type : unit.vhdl_types()) {
            if (!type.enumeration_literals.empty()
                && same_hir_identifier(type.name, name, true)) {
                if (found && *found != type.id) {
                    return std::nullopt;
                }
                found = type.id;
            }
        }
        return found;
    }
    return enumeration_of(hir_vhdl_expression_subtype(*loop->loop_initial));
}

std::optional<std::string> Lowerer::hir_vhdl_array_type_family(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    using Builtin = semantic::vhdl::BuiltinTypeIdentity;
    if (subtype.builtin_type == Builtin::ieee_std_logic_1164_std_logic_vector
        || subtype.builtin_type
            == Builtin::ieee_std_logic_1164_std_ulogic_vector) {
        return std::string { "std_ulogic_vector" };
    }
    const auto family_of_name = [](std::string_view name)
        -> std::optional<std::string> {
        if (const auto separator = name.find_last_of(".:");
            separator != std::string_view::npos) {
            name.remove_prefix(separator + 1U);
        }
        std::string lowered;
        for (const auto character : name) {
            lowered.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
        if (lowered == "std_logic_vector" || lowered == "std_ulogic_vector") {
            return std::string { "std_ulogic_vector" };
        }
        if (lowered == "unsigned" || lowered == "signed"
            || lowered == "bit_vector" || lowered == "string"
            || lowered == "boolean_vector" || lowered == "integer_vector") {
            return lowered;
        }
        return std::nullopt;
    };
    if (specialized_hir_unit_ != nullptr
        && subtype.type_mark.target.valid()) {
        auto type_id = subtype.type_mark.target;
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid() && visited.insert(type_id.value()).second) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            const auto& definition = *type->vhdl;
            if (const auto known = family_of_name(definition.name)) {
                return known;
            }
            if (definition.form == semantic::vhdl::TypeForm::array) {
                return "type:" + std::to_string(definition.id.value());
            }
            if (definition.form != semantic::vhdl::TypeForm::subtype
                && definition.form != semantic::vhdl::TypeForm::alias) {
                return std::nullopt;
            }
            type_id = definition.base.type_mark.target;
        }
    }
    return family_of_name(subtype.type_mark.spelling);
}

bool Lowerer::hir_vhdl_enumeration_index(
    const semantic::ExpressionId index,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(index);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto width = hir_expression_width(index, process_scope);
    const auto domain = hir_expression_domain(index, process_scope);
    if (!width || *width == 0U || *width > 31U || !domain
        || *domain == frontend::ValueDomain::Integer
        || (*domain == frontend::ValueDomain::Logic9 && *width != 1U)
        || !scalar_domain(*domain)) {
        return false;
    }
    const auto subtype = hir_vhdl_expression_subtype(index);
    return subtype
        && vhdl_enumeration_definition(
               *specialized_hir_unit_, subtype->type_mark.target)
        != nullptr;
}

std::optional<std::size_t> Lowerer::hir_dynamic_part_width(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind != semantic::sv::ExpressionKind::slice
        || (source.text != "+:" && source.text != "-:")
        || source.operands.size() != 3U) {
        return std::nullopt;
    }
    const auto runtime_base = has_runtime_systemverilog_call(
        *specialized_hir_unit_, source.operands[1]);
    if (!runtime_base && hir_constant_integer(source.operands[1])) {
        return std::nullopt;
    }
    const auto width = hir_constant_integer(source.operands[2]);
    if (!width || hir_expression_is_residual(source.operands[2])
        || *width <= 0
        || static_cast<std::uint64_t>(*width)
            > std::numeric_limits<std::uint32_t>::max()
        || !hir_dynamic_index_supported(
            source.operands[0], source.operands[1], process_scope)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*width);
}

std::optional<semantic::DeclarationId> Lowerer::hir_target_declaration(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    if (expression->systemverilog != nullptr) {
        semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        const auto declaration
            = resolver.resolve_systemverilog_target(expression_id).unique();
        if (!declaration) {
            return std::nullopt;
        }
        if (hir_local_registers_.contains(declaration->value())
            || hir_local_string_registers_.contains(declaration->value())
            || hir_local_container_registers_.contains(
                declaration->value())) {
            return declaration;
        }
        return resolver.actual_declaration(*declaration)
            .value_or(*declaration);
    }
    auto candidate = expression_id;
    std::unordered_set<std::uint32_t> visiting;
    while (candidate.valid()
        && visiting.insert(candidate.value()).second) {
        if (const auto declaration = hir_referenced_declaration(candidate)) {
            return declaration;
        }
        const auto candidate_expression
            = specialized_hir_unit_->find_expression(
            candidate);
        if (!candidate_expression) {
            return std::nullopt;
        }
        const auto selection_source = [&]()
            -> std::optional<semantic::ExpressionId> {
            const auto& source = *candidate_expression->vhdl;
            if ((source.kind == semantic::vhdl::ExpressionKind::index
                    || source.kind
                        == semantic::vhdl::ExpressionKind::slice)
                && !source.operands.empty()) {
                return source.operands.front();
            }
            if (source.kind == semantic::vhdl::ExpressionKind::call
                && source.operands.size() == 1U
                && (source.text.starts_with("@vhdl-member:")
                    || source.text == "@vhdl-dereference")) {
                return source.operands.front();
            }
            return std::nullopt;
        }();
        if (!selection_source) {
            return std::nullopt;
        }
        candidate = *selection_source;
    }
    return std::nullopt;
}

std::optional<std::size_t> Lowerer::hir_loop_iteration_count(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement) {
        return std::nullopt;
    }
    std::optional<semantic::ExpressionId> initial;
    std::optional<semantic::ExpressionId> limit;
    std::optional<semantic::ExpressionId> condition;
    bool descending = false;
    bool exclusive = false;
    bool repeat = false;
    bool runtime = false;
    bool post_test = false;
    std::optional<HirPackedRange> vhdl_attribute_range;
    if (statement->systemverilog != nullptr) {
        const auto& source = *statement->systemverilog;
        if (source.kind != semantic::sv::StatementKind::loop) {
            return std::nullopt;
        }
        initial = source.loop_initial;
        limit = source.loop_limit;
        condition = source.condition;
        descending = source.loop_descending;
        exclusive = source.loop_limit_exclusive;
        repeat = source.loop_repeat;
        runtime = source.loop_runtime;
        post_test = source.loop_post_test;
        if (runtime
            && (source.loop_initial || source.loop_limit
                || source.target || source.value
                || source.loop_update_target
                || !source.loop_updates.empty()
                || !source.loop_variable.empty())) {
            return std::nullopt;
        }
    } else {
        const auto& source = *statement->vhdl;
        if (source.kind != semantic::vhdl::StatementKind::loop) {
            return std::nullopt;
        }
        initial = source.loop_initial;
        limit = source.loop_limit;
        condition = source.condition;
        descending = source.loop_descending;
        if (initial && !limit) {
            const auto attribute = hir_vhdl_attribute_profile(
                *initial, source.scope);
            if (attribute) {
                vhdl_attribute_range = attribute->discrete_range;
            }
        }
        if (vhdl_attribute_range) {
            descending = vhdl_attribute_range->descending;
        }
        runtime = !vhdl_attribute_range && (!initial || !limit);
    }
    if (runtime) {
        if (!condition || hir_expression_is_residual(*condition)) {
            return std::nullopt;
        }
        const auto truth = hir_constant_integer(*condition);
        if (!truth || *truth != 0) {
            return std::nullopt;
        }
        return post_test ? 1U : 0U;
    }
    if (!vhdl_attribute_range
        && (!initial || !limit || hir_expression_is_residual(*initial)
            || hir_expression_is_residual(*limit))) {
        return std::nullopt;
    }
    const auto first = vhdl_attribute_range
        ? std::optional { vhdl_attribute_range->left }
        : hir_constant_integer(*initial);
    const auto last = vhdl_attribute_range
        ? std::optional { vhdl_attribute_range->right }
        : hir_constant_integer(*limit);
    if (!first || !last) {
        return std::nullopt;
    }
    if (repeat) {
        if (*last < 0) {
            return 0U;
        }
        const auto count = static_cast<std::uint64_t>(*last);
        return count <= maximum_hir_loop_iterations
            ? std::optional { static_cast<std::size_t>(count) }
            : std::nullopt;
    }
    const auto null_range = descending
        ? (exclusive ? *first <= *last : *first < *last)
        : (exclusive ? *first >= *last : *first > *last);
    if (null_range) {
        return 0U;
    }
    const auto distance = index_distance(*first, *last);
    const auto count = exclusive ? distance : distance + 1U;
    return count <= maximum_hir_loop_iterations
        ? std::optional { static_cast<std::size_t>(count) }
        : std::nullopt;
}

std::optional<std::string> Lowerer::hir_name(
    const semantic::ExpressionId expression_id) const
{
    const auto selected = hir_referenced_declaration(expression_id);
    if (!selected || specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto declaration
        = specialized_hir_unit_->find_declaration(*selected)) {
        if (declaration->systemverilog != nullptr) {
            return declaration->systemverilog->name;
        }
        if (declaration->vhdl != nullptr) {
            return declaration->vhdl->name;
        }
    }
    return std::nullopt;
}

std::optional<semantic::DeclarationId>
Lowerer::hir_referenced_declaration(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }

    semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    if (expression->systemverilog != nullptr) {
        const auto declaration
            = resolver.resolve_systemverilog_expression(expression_id)
                  .unique();
        if (!declaration) {
            return std::nullopt;
        }
        if (hir_local_registers_.contains(declaration->value())
            || hir_local_string_registers_.contains(declaration->value())
            || hir_local_container_registers_.contains(
                declaration->value())) {
            return declaration;
        }
        // A value parameter with an explicit type or range converts its
        // actual (or default) to that type, so a name actual is a value
        // source rather than an alias (IEEE 1800-2017 6.20.2).
        const auto source = specialized_hir_unit_->find_declaration(
            *declaration);
        if (source && source->systemverilog != nullptr) {
            const auto& value = *source->systemverilog;
            if ((value.form == semantic::sv::DeclarationForm::parameter
                    || value.form
                        == semantic::sv::DeclarationForm::local_parameter)
                && value.type
                && (value.type->target.spelling != "implicit"
                    || value.type->packed_range)) {
                return declaration;
            }
        }
        return resolver.actual_declaration(*declaration)
            .value_or(*declaration);
    }
    if (expression->vhdl == nullptr
        || !expression->vhdl->referenced_name) {
        return std::nullopt;
    }
    const auto& reference = *expression->vhdl->referenced_name;
    const auto operator_expression
        = expression->vhdl->kind == semantic::vhdl::ExpressionKind::unary
        || expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::binary;
    if (operator_expression && !reference.selected
        && reference.overloads.empty()) {
        // Intrinsic operators carry their designator as a name for source
        // fidelity, but an empty frontend binding is not an invitation to
        // resolve an unrelated declaration with the same spelling. User
        // overloads retain selected/overload identities.
        return std::nullopt;
    }
    const auto reference_name = reference.canonical.empty()
        ? std::string_view { reference.spelling }
        : std::string_view { reference.canonical };
    const auto overloaded = !reference.overloads.empty();
    std::optional<semantic::CompiledDeclarationResolution>
        uncached_resolution;
    const semantic::CompiledDeclarationResolution* resolved
        = find_hir_expression_resolution_cache(expression_id);
    if (resolved == nullptr) {
        uncached_resolution.emplace(
            resolver.resolve_expression_name(expression_id));
        cache_hir_expression_resolution(
            expression_id, *uncached_resolution);
        resolved = &*uncached_resolution;
    }
    if (const auto declaration = resolved->unique()) {
        // Callable input formals with constant actuals also participate in
        // the generic-binding overlay so dependent subtype expressions can
        // be folded while the body is lowered.  Once a formal has runtime
        // storage, however, value expressions in the body must continue to
        // name that storage rather than being rewritten to the caller's
        // declaration.  This mirrors hir_generic_actual(), which gives a
        // materialized callable argument precedence over static substitution.
        if (hir_local_registers_.contains(declaration->value())
            || hir_local_string_registers_.contains(declaration->value())
            || hir_local_container_registers_.contains(
                declaration->value())) {
            return declaration;
        }
        return resolver.actual_declaration(*declaration)
            .value_or(*declaration);
    }
    if (resolved->status == semantic::CompiledResolutionStatus::ambiguous
        || resolved->status == semantic::CompiledResolutionStatus::invalid) {
        return std::nullopt;
    }
    // The VHDL frontend retains the root object in both `selected` and the
    // overload set for a dotted object name.  The full spelling is not a
    // package-selected declaration, so the semantic resolver intentionally
    // reports not-found and the member-aware fallback below resolves the
    // root.  Do not mistake that retained root identity for a callable
    // ambiguity and suppress record-member resolution.
    if (overloaded
        && reference_name.find('.') == std::string_view::npos) {
        return std::nullopt;
    }

    const auto expression_scope = expression->vhdl->scope;
    const auto packages = resolver.resolve_vhdl_package_members(
        reference, expression_scope);
    if (!packages.candidates.empty()) {
        const auto package = packages.unique();
        return package
            ? std::optional { package->member }
            : std::nullopt;
    }

    auto lookup_name = reference_name;
    std::string_view member_path;
    const auto separator = reference_name.find('.');
    if (separator != std::string_view::npos) {
        if (separator == 0U
            || separator + 1U >= reference_name.size()) {
            return std::nullopt;
        }
        lookup_name = reference_name.substr(0U, separator);
        member_path = reference_name.substr(separator + 1U);
    }
    if (!simple_hir_identifier(lookup_name, true)) {
        return std::nullopt;
    }

    const auto valid_member_path
        = [&](const semantic::DeclarationId id) {
        if (member_path.empty()) {
            return true;
        }
        const auto declaration
            = specialized_hir_unit_->find_declaration(id);
        if (!declaration) {
            return false;
        }
        if (declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return false;
        }
        auto current = hir_effective_vhdl_subtype(
            *declaration->vhdl->subtype);
        auto remaining = member_path;
        while (current && !remaining.empty()) {
            const auto member_separator = remaining.find('.');
            const auto segment = remaining.substr(
                0U, member_separator);
            // Compiler-supplied STD.ENV TIME_RECORD is intentionally a
            // predefined packed layout rather than an ordinary retained
            // record TypeDefinition.  Admit its standardized members at the
            // same resolver boundary used by hir_vhdl_member_selection();
            // otherwise a decoded dotted name is rejected before that
            // lowering helper can apply the predefined layout.
            if (const auto predefined
                = vhdl_environment_time_record_member(
                    *specialized_hir_unit_, *current,
                    declaration->vhdl->scope, segment)) {
                current = hir_effective_vhdl_subtype(
                    predefined->subtype);
                if (member_separator == std::string_view::npos) {
                    return current.has_value();
                }
                remaining.remove_prefix(member_separator + 1U);
                continue;
            }
            if (!current->type_mark.target.valid()) {
                return false;
            }
            const auto definition = specialized_hir_unit_->find_type(
                current->type_mark.target);
            if (!definition || definition->vhdl == nullptr
                || definition->vhdl->form
                    != semantic::vhdl::TypeForm::record) {
                return false;
            }
            const auto member = std::ranges::find_if(
                definition->vhdl->record_elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return same_hir_identifier(
                        element.name, segment, true);
                });
            if (member == definition->vhdl->record_elements.end()) {
                return false;
            }
            current = hir_effective_vhdl_subtype(member->subtype);
            if (member_separator == std::string_view::npos) {
                return current.has_value();
            }
            remaining.remove_prefix(member_separator + 1U);
        }
        return false;
    };
    const auto predicate = [&](const semantic::CompiledDeclarationView& view) {
        return view.vhdl != nullptr
            && valid_member_path(view.vhdl->id);
    };

    auto base_name = reference;
    base_name.spelling = lookup_name;
    base_name.canonical = lookup_name;
    base_name.selected.reset();
    base_name.overloads.clear();
    return resolver.resolve_vhdl(
                       base_name, expression_scope, predicate)
        .unique();
}

std::optional<semantic::DeclarationId>
Lowerer::hir_vhdl_type_declaration(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const semantic::CompiledDeclarationPredicate type_declaration
        = [](const semantic::CompiledDeclarationView& candidate) {
              if (candidate.vhdl == nullptr) {
                  return false;
              }
              using Form = semantic::vhdl::DeclarationForm;
              const auto form = candidate.vhdl->form;
              return form == Form::type || form == Form::subtype
                  || form == Form::generic_type;
          };
    return semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.resolve_expression_name(expression_id, type_declaration, true)
        .unique();
}

bool Lowerer::hir_expression_is_residual(
    const semantic::ExpressionId expression) const
{
    static_cast<void>(expression);
    // residual_expressions describes the compile-to-specialize handoff. At
    // this point the unit already owns its canonical actual identities, so
    // capability checks and lowering must inspect the effective HIR instead
    // of rejecting an expression merely because it depended on an actual at
    // compile time. Any still-unresolved name, type, or hierarchy dependency
    // fails in its specific evaluator or lowering path.
    return false;
}

std::optional<Lowerer::HirVirtualInterfaceMember>
Lowerer::hir_virtual_interface_member(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr
        || systemverilog_interface_handles_ == nullptr
        || systemverilog_interface_types_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    return hir_virtual_interface_member(
        expression->systemverilog->text, expression->systemverilog->scope);
}

std::optional<Lowerer::HirVirtualInterfaceMember>
Lowerer::hir_virtual_interface_member(
    const std::string_view name, const semantic::ScopeId scope) const
{
    if (specialized_hir_unit_ == nullptr
        || systemverilog_interface_handles_ == nullptr
        || systemverilog_interface_types_ == nullptr) {
        return std::nullopt;
    }
    const std::string text { name };
    const auto separator = text.find('.');
    if (separator == std::string::npos || separator == 0U) {
        return std::nullopt;
    }
    // The handle variable is the first segment of the name.
    auto declaration_id = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.resolve_systemverilog(
         std::string_view { text }.substr(0U, separator),
         scope,
         [](const semantic::CompiledDeclarationView& candidate) {
             return candidate.systemverilog != nullptr
                 && candidate.systemverilog->type.has_value()
                 && candidate.systemverilog->type->virtual_interface;
         },
         false)
                              .unique();
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || declaration->systemverilog->name != text.substr(0U, separator)
        || !declaration->systemverilog->type
        || !declaration->systemverilog->type->virtual_interface
        || declaration->systemverilog->type->interface_type.empty()) {
        return std::nullopt;
    }
    // Every interface instance must be elaborated: resolve in the second
    // lowering pass.
    if (hierarchical_reference_retry_) {
        hierarchical_reference_missed_ = true;
        return std::nullopt;
    }
    const auto& interface_type
        = declaration->systemverilog->type->interface_type;
    const auto member = text.substr(separator + 1U);
    HirVirtualInterfaceMember result;
    result.receiver = *declaration_id;
    std::set<std::uint64_t> handles;
    std::vector<std::string> paths;
    for (const auto& [path, type] : *systemverilog_interface_types_) {
        if (type == interface_type) {
            paths.push_back(path);
        }
    }
    std::ranges::sort(paths);
    for (const auto& path : paths) {
        const auto handle = systemverilog_interface_handles_->find(path);
        if (handle == systemverilog_interface_handles_->end()
            || !handles.insert(handle->second).second) {
            continue;
        }
        const auto signal = design_.signal_by_name_.find(path + "." + member);
        if (signal == design_.signal_by_name_.end()
            || signal->second >= design_.signal_info_.size()) {
            continue;
        }
        const auto& info = design_.signal_info_[signal->second];
        if (result.candidates.empty()) {
            result.width = info.width;
            result.domain = info.source_domain;
        } else if (info.width != result.width) {
            continue;
        }
        result.candidates.emplace_back(handle->second, signal->second);
    }
    if (result.candidates.empty() || result.width == 0U) {
        return std::nullopt;
    }
    return result;
}

std::optional<SignalId> Lowerer::hir_direct_signal(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    constexpr std::string_view indexed_member_prefix { "index." };
    constexpr std::string_view selected_member_prefix { "@sv-select:" };
    // `bus[2].data` outside procedural code is a member selection of the
    // indexed element.
    const auto* selected_index = [&]() -> const semantic::sv::Expression* {
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::call
            || !expression->systemverilog->text.starts_with(
                selected_member_prefix)
            || expression->systemverilog->operands.size() != 1U) {
            return nullptr;
        }
        const auto element = specialized_hir_unit_->find_expression(
            expression->systemverilog->operands.front());
        return element && element->systemverilog != nullptr
                && element->systemverilog->kind
                    == semantic::sv::ExpressionKind::index
                && element->systemverilog->text.find('.') == std::string::npos
                && element->systemverilog->operands.size() == 2U
            ? element->systemverilog
            : nullptr;
    }();
    if ((expression && expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::index
            && expression->systemverilog->text.starts_with(
                indexed_member_prefix)
            && expression->systemverilog->operands.size() == 2U)
        || selected_index != nullptr) {
        // A member of an element of an interface instance array,
        // `bus[2].data`, is that element instance's signal; the member
        // path follows `index.` in the selection's text.
        const auto& indexed = selected_index != nullptr
            ? *selected_index
            : *expression->systemverilog;
        const auto array = specialized_hir_unit_->find_expression(
            indexed.operands.front());
        if (!array || array->systemverilog == nullptr
            || array->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            // A nested generate instance, `g[1].h[2].x`.
            const auto path = hir_systemverilog_constant_path(expression_id);
            if (!path) {
                return std::nullopt;
            }
            if (const auto found = signals_.find(*path);
                found != signals_.end()
                && found->second < design_.signal_info_.size()) {
                return found->second;
            }
            if (const auto signal = hir_hierarchical_signal(*path)) {
                hierarchical_reference_used_ = true;
                return signal;
            }
            return std::nullopt;
        }
        const auto element = specialized_hir_unit_
                                 ->evaluate_integral_expression(
                                     indexed.operands.back());
        if (!element) {
            return std::nullopt;
        }
        const auto occurrence = array->systemverilog->text + "["
            + std::to_string(*element) + "]";
        const auto member = selected_index != nullptr
            ? expression->systemverilog->text.substr(
                  selected_member_prefix.size())
            : expression->systemverilog->text.substr(
                  indexed_member_prefix.size());
        // `loop[0].r`: a variable of one instance of a loop generate block
        // (27.4) is a hierarchical name.
        const auto generate_member = [&]() -> std::optional<SignalId> {
            const auto built = hir_systemverilog_constant_path(expression_id);
            const auto path = built ? *built : occurrence + "." + member;
            if (const auto found = signals_.find(path);
                found != signals_.end()
                && found->second < design_.signal_info_.size()) {
                return found->second;
            }
            if (const auto signal = hir_hierarchical_signal(path)) {
                hierarchical_reference_used_ = true;
                return signal;
            }
            return std::nullopt;
        };
        if (systemverilog_interface_handles_ == nullptr) {
            return generate_member();
        }
        // Otherwise the element must be an interface instance.
        std::optional<std::string> element_path;
        for (auto lexical = hierarchy_;;) {
            const auto candidate = lexical.empty()
                ? occurrence
                : lexical + "." + occurrence;
            if (systemverilog_interface_handles_->contains(candidate)) {
                element_path = candidate;
                break;
            }
            const auto separator = lexical.rfind('.');
            if (separator == std::string::npos) {
                if (!lexical.empty()) {
                    lexical.clear();
                    continue;
                }
                break;
            }
            lexical.resize(separator);
        }
        if (!element_path) {
            return generate_member();
        }
        if (const auto found = signals_.find(occurrence + "." + member);
            found != signals_.end()
            && found->second < design_.signal_info_.size()) {
            return found->second;
        }
        if (const auto found = design_.signal_by_name_.find(
                *element_path + "." + member);
            found != design_.signal_by_name_.end()
            && found->second < design_.signal_info_.size()) {
            hierarchical_reference_used_ = true;
            return found->second;
        }
        // The element instance may not be elaborated yet.
        hierarchical_reference_missed_ = true;
        return std::nullopt;
    }
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && expression->systemverilog->text.starts_with(
            selected_member_prefix)) {
        // `g[1].h[2].x` written as a selection of a nested element.
        const auto path = hir_systemverilog_constant_path(expression_id);
        if (!path || path->find('[') == std::string::npos) {
            return std::nullopt;
        }
        if (const auto found = signals_.find(*path);
            found != signals_.end()
            && found->second < design_.signal_info_.size()) {
            return found->second;
        }
        if (const auto signal = hir_hierarchical_signal(*path)) {
            hierarchical_reference_used_ = true;
            return signal;
        }
        return std::nullopt;
    }
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    if (const auto declaration = hir_referenced_declaration(expression_id)) {
        const auto lexical = hir_runtime_binding(
            *declaration, hir_process_scope_, false);
        if (lexical
            && lexical->kind == HirRuntimeBindingKind::local) {
            return std::nullopt;
        }
    }
    const auto& name = expression->systemverilog->text;
    if (const auto found = signals_.find(name);
        found != signals_.end()
        && found->second < design_.signal_info_.size()) {
        return found->second;
    }
    if (name.find('.') != std::string::npos
        && !hir_referenced_declaration(expression_id)) {
        if (const auto signal = hir_hierarchical_signal(name)) {
            hierarchical_reference_used_ = true;
            return signal;
        }
        hierarchical_reference_missed_ = true;
    }
    // A separately selected top-level may provide the conventional global
    // signaling surface used by vendor libraries (for example glbl.GSR).
    // Admit only a direct member of an explicit root alias. Deeper paths
    // remain illegal cross-root hierarchy shortcuts.
    const auto separator = name.find('.');
    if (separator == std::string::npos
        || name.find('.', separator + 1U) != std::string::npos
        || std::ranges::find(
               design_.roots_, name.substr(0U, separator))
            == design_.roots_.end()) {
        return std::nullopt;
    }
    const auto global = design_.signal_by_name_.find(name);
    return global != design_.signal_by_name_.end()
            && global->second < design_.signal_info_.size()
        ? std::optional { global->second }
        : std::nullopt;
}

std::optional<std::string> Lowerer::hir_systemverilog_constant_path(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    constexpr std::string_view selected_prefix { "@sv-select:" };
    constexpr std::string_view indexed_prefix { "index." };
    using Kind = semantic::sv::ExpressionKind;
    if (source.kind == Kind::name) {
        return source.text;
    }
    if (source.kind == Kind::call && source.text.starts_with(selected_prefix)
        && source.operands.size() == 1U) {
        const auto base = hir_systemverilog_constant_path(source.operands.front());
        return base
            ? std::optional { *base + "."
                  + source.text.substr(selected_prefix.size()) }
            : std::nullopt;
    }
    if (source.kind == Kind::index && source.operands.size() == 2U) {
        const auto base = hir_systemverilog_constant_path(source.operands.front());
        const auto index = specialized_hir_unit_->evaluate_integral_expression(
            source.operands.back());
        if (!base || !index) {
            return std::nullopt;
        }
        auto path = *base + "[" + std::to_string(*index) + "]";
        if (source.text.starts_with(indexed_prefix)) {
            path += "." + source.text.substr(indexed_prefix.size());
        } else if (!source.text.empty() && source.text != "index") {
            return std::nullopt;
        }
        return path;
    }
    return std::nullopt;
}

std::optional<SignalId> Lowerer::hir_hierarchical_signal(
    const std::string_view name) const
{
    // A hierarchical name is resolved from the innermost enclosing scope
    // outward (IEEE 1800-2017 23.8): first below the current instance, then
    // below each ancestor, and finally from the top. Only full hierarchical
    // paths are consulted; the single-root shortcut names are ambiguous.
    auto path = name;
    if (path.starts_with("$root.")) {
        path.remove_prefix(6U);
    }
    const auto lookup = [&](const std::string& candidate)
        -> std::optional<SignalId> {
        const auto found = design_.signal_by_name_.find(candidate);
        return found != design_.signal_by_name_.end()
                && found->second < design_.signal_info_.size()
            ? std::optional { found->second }
            : std::nullopt;
    };
    if (!name.starts_with("$root.")) {
        std::string_view scope = hierarchy_;
        while (!scope.empty()) {
            if (const auto signal = lookup(
                    std::string { scope } + "." + std::string { path })) {
                return signal;
            }
            const auto separator = scope.find_last_of('.');
            scope = separator == std::string_view::npos
                ? std::string_view { }
                : scope.substr(0U, separator);
        }
    }
    if (const auto signal = lookup(std::string { path })) {
        return signal;
    }
    // `ifc.mp.member` names a member through a modport of an interface
    // instance (IEEE 1800-2017 25.5): the member of the instance itself.
    const auto first = path.find('.');
    const auto second = first == std::string_view::npos
        ? std::string_view::npos
        : path.find('.', first + 1U);
    if (second == std::string_view::npos
        || systemverilog_interface_handles_ == nullptr) {
        return std::nullopt;
    }
    const auto instance = path.substr(0U, first);
    const auto member = path.substr(second + 1U);
    for (std::string_view scope = hierarchy_;;) {
        const auto prefix = scope.empty()
            ? std::string { instance }
            : std::string { scope } + "." + std::string { instance };
        if (systemverilog_interface_handles_->contains(prefix)) {
            return lookup(prefix + "." + std::string { member });
        }
        if (scope.empty()) {
            return std::nullopt;
        }
        const auto separator = scope.find_last_of('.');
        scope = separator == std::string_view::npos
            ? std::string_view { }
            : scope.substr(0U, separator);
    }
}

std::optional<Lowerer::HirRuntimeBinding>
Lowerer::hir_direct_signal_binding(
    const semantic::ExpressionId expression_id) const
{
    const auto signal = hir_direct_signal(expression_id);
    if (!signal) {
        return std::nullopt;
    }
    const auto& info = design_.signal_info_[*signal];
    HirRuntimeBinding result;
    result.declaration = hir_target_declaration(expression_id)
                             .value_or(semantic::DeclarationId { });
    result.kind = HirRuntimeBindingKind::signal;
    result.name = info.name;
    result.width = info.width;
    result.domain = info.source_domain;
    result.signed_value = info.is_signed;
    result.integer_range = info.integer_range;
    result.signal = *signal;
    const auto declaration = specialized_hir_unit_->find_declaration(
        result.declaration);
    if (declaration && declaration->systemverilog != nullptr
        && declaration->systemverilog->type) {
        const auto& type = *declaration->systemverilog->type;
        if (const auto width = hir_systemverilog_type_width(type)) {
            result.width = *width;
        }
        result.domain = hir_systemverilog_type_four_state(type)
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2;
        result.signed_value = type.signed_value;
    }
    return result;
}

std::optional<SignalId> Lowerer::hir_systemverilog_event_signal(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    constexpr auto triggered_suffix = std::string_view { ".triggered" };
    const auto& source = *expression->systemverilog;
    const bool triggered = source.text.ends_with(triggered_suffix);
    const auto event_name = triggered
        ? std::string_view { source.text }.substr(
              0U, source.text.size() - triggered_suffix.size())
        : std::string_view { source.text };

    if (const auto found = signals_.find(std::string { event_name });
        found != signals_.end()
        && found->second < design_.signal_info_.size()
        && design_.signal_info_[found->second].type_name == "event") {
        return found->second;
    }

    auto declaration = triggered
        ? semantic::CompiledDesignResolver {
              *specialized_hir_unit_, hir_generic_binding_frames_ }
              .resolve_systemverilog(event_name, source.scope)
              .unique()
        : hir_referenced_declaration(expression_id);
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, process_scope, false)
        : std::nullopt;
    return binding && binding->signal
            && *binding->signal < design_.signal_info_.size()
            && design_.signal_info_[*binding->signal].type_name == "event"
        ? binding->signal
        : std::nullopt;
}

std::optional<std::uint64_t>
Lowerer::hir_systemverilog_interface_handle(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr
        || systemverilog_interface_handles_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    std::string occurrence;
    if (source.kind == semantic::sv::ExpressionKind::name) {
        occurrence = source.text;
    } else if (source.kind == semantic::sv::ExpressionKind::index
        && source.operands.size() == 2U
        // `index.member` selects a member of the element, not the element.
        && (source.text.find('.') == std::string::npos
            || (!hierarchical_reference_retry_
                && !hir_direct_signal(expression_id)))) {
        const auto base = specialized_hir_unit_->find_expression(
            source.operands.front());
        const auto index = specialized_hir_unit_
                               ->evaluate_integral_expression(
                                   source.operands.back());
        if (!base || base->systemverilog == nullptr
            || base->systemverilog->kind
                != semantic::sv::ExpressionKind::name
            || !index) {
            return std::nullopt;
        }
        occurrence = base->systemverilog->text + "["
            + std::to_string(*index) + "]";
    } else {
        return std::nullopt;
    }

    auto path = hierarchy_.empty()
        ? occurrence
        : hierarchy_ + "." + occurrence;
    auto found = systemverilog_interface_handles_->find(path);
    auto lexical_path = hierarchy_;
    while (found == systemverilog_interface_handles_->end()
        && lexical_path.find('.') != std::string::npos) {
        lexical_path.resize(lexical_path.rfind('.'));
        path = lexical_path + "." + occurrence;
        found = systemverilog_interface_handles_->find(path);
    }
    if (found == systemverilog_interface_handles_->end()) {
        found = systemverilog_interface_handles_->find(occurrence);
    }
    return found == systemverilog_interface_handles_->end()
        ? std::nullopt
        : std::optional { found->second };
}

std::optional<std::vector<semantic::sv::Sensitivity>>
Lowerer::hir_default_clocking_event() const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto selected = specialized_hir_unit_->design().find_unit(
        specialized_hir_unit_->unit());
    if (!selected || selected->systemverilog == nullptr
        || !selected->systemverilog->default_clocking) {
        return std::nullopt;
    }
    const auto& unit = *selected->systemverilog;
    const auto& reference = unit.default_clocking->block;
    const auto name = reference.spelling;
    const auto block = std::ranges::find(
        unit.clocking_blocks, name, &semantic::sv::ClockingBlock::name);
    if (block == unit.clocking_blocks.end() || block->event.empty()) {
        return std::nullopt;
    }
    return block->event;
}

std::optional<Lowerer::HirRuntimeBinding>
Lowerer::hir_runtime_binding(
    const semantic::DeclarationId declaration_id,
    const semantic::ScopeId process_scope,
    const bool require_storage) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration) {
        return std::nullopt;
    }
    if (active_hir_callable_
        && *active_hir_callable_ < hir_callable_frames_.size()) {
        const auto& signal_formals
            = hir_callable_frames_[*active_hir_callable_].signal_formals;
        if (const auto found = signal_formals.find(declaration_id.value());
            found != signal_formals.end()) {
            return found->second;
        }
    }
    if (declaration->vhdl != nullptr
        && declaration->vhdl->form
            == semantic::vhdl::DeclarationForm::alias
        && declaration->vhdl->alias_target) {
        const auto& source = *declaration->vhdl;
        auto selected = source.alias_target->selected;
        if (!selected) {
            const auto target_name = source.alias_target->canonical.empty()
                ? std::string_view { source.alias_target->spelling }
                : std::string_view { source.alias_target->canonical };
            const auto& scopes
                = specialized_hir_unit_->design().semantics.scopes();
            auto scope = source.scope;
            for (std::size_t depth { }; depth <= scopes.size(); ++depth) {
                std::optional<semantic::DeclarationId> match;
                bool ambiguous { };
                const auto consider = [&](const auto& candidate) {
                    if (candidate.id == declaration_id
                        || candidate.scope != scope
                        || !same_hir_identifier(
                            candidate.name, target_name, true)) {
                        return;
                    }
                    if (match && *match != candidate.id) {
                        ambiguous = true;
                    } else {
                        match = candidate.id;
                    }
                };
                for (const auto& candidate :
                    specialized_hir_unit_->vhdl_declarations()) {
                    consider(candidate);
                }
                if (!match && !ambiguous) {
                    for (const auto& candidate :
                        specialized_hir_unit_->design().vhdl_hir
                            .declarations()) {
                        consider(candidate);
                    }
                }
                if (ambiguous) {
                    return std::nullopt;
                }
                if (match) {
                    selected = match;
                    break;
                }
                if (!scope.valid() || scope.value() >= scopes.size()
                    || !scopes[scope.value()].parent) {
                    break;
                }
                scope = *scopes[scope.value()].parent;
            }
        }
        if (selected && *selected != declaration_id) {
            return hir_runtime_binding(
                *selected, process_scope, require_storage);
        }
    }
    // A nested statement block advances process_scope, while storage for a
    // callable formal or outer-block variable remains keyed by declaration.
    // Keep that materialized storage local instead of reclassifying the name
    // as a hierarchy signal at the nested scope.
    const auto materialized_local = hir_local_registers_.contains(
        declaration_id.value());
    const auto debug_vhdl_binding_rejection =
        [&](const std::string_view phase,
            const semantic::vhdl::Declaration& source,
            const std::optional<semantic::vhdl::SubtypeIndication>& effective,
            const bool process_variable) {
            static const bool enabled
                = std::getenv("FSIM_DEBUG_HIR_VHDL_BINDING") != nullptr;
            if (!enabled
                || (!same_hir_identifier(source.name, "g", true)
                    && !same_hir_identifier(source.name, "r", true))) {
                return;
            }
            std::cerr << "FSIM_DEBUG_HIR_VHDL_BINDING phase=" << phase
                      << " declaration=" << declaration_id.value()
                      << " source_scope=" << source.scope.value()
                      << " process_scope=" << process_scope.value()
                      << " scope_match="
                      << (source.scope == process_scope)
                      << " materialized_local=" << materialized_local
                      << " require_storage=" << require_storage
                      << " process_variable=" << process_variable
                      << " form="
                      << static_cast<unsigned>(source.form);
            if (!effective) {
                std::cerr << " effective=none\n";
                return;
            }
            std::cerr << " type=" << effective->type_mark.target.value()
                      << " domain="
                      << static_cast<unsigned>(effective->domain)
                      << " width="
                      << effective->executable_width.value_or(0U)
                      << " unconstrained=" << effective->unconstrained
                      << " constraints=" << effective->constraints.size();
            for (const auto& constraint : effective->constraints) {
                std::cerr << " [kind="
                          << static_cast<unsigned>(constraint.kind)
                          << " left="
                          << constraint.left_expression.value_or(
                                 semantic::ExpressionId { })
                                 .value()
                          << " left_value=";
                if (constraint.left) {
                    std::cerr << *constraint.left;
                } else {
                    std::cerr << "none";
                }
                std::cerr << " right_value=";
                if (constraint.right) {
                    std::cerr << *constraint.right;
                } else {
                    std::cerr << "none";
                }
                std::cerr << " right="
                          << constraint.right_expression.value_or(
                                 semantic::ExpressionId { })
                                 .value()
                          << " null=" << constraint.null << ']';
            }
            std::cerr << "\n";
        };
    HirRuntimeBinding result;
    result.declaration = declaration_id;
    bool null_vhdl_array { };
    bool composite_vhdl_storage { };
    if (active_hir_callable_) {
        const auto& frame = hir_callable_frames_[*active_hir_callable_];
        const auto resolution = declaration->systemverilog != nullptr
                && declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::function
            ? hir_callable_resolution(declaration_id)
            : std::nullopt;
        if (frame.function && declaration->systemverilog != nullptr
            && (declaration_id == frame.declaration
                || (resolution
                    && resolution->body == frame.declaration))) {
            result.name = declaration->systemverilog->name;
            result.kind = HirRuntimeBindingKind::local;
            result.local = frame.result;
            result.width = frame.type.width;
            result.domain = frame.type.domain;
            result.signed_value = frame.type.signed_value;
            return result;
        }
    }
    const auto callable_formal = [&] {
        return std::ranges::any_of(
            specialized_hir_unit_->design().semantics.declarations(),
            [&](const semantic::Declaration& candidate) {
                const auto owner = specialized_hir_unit_->find_declaration(
                    candidate.id);
                if (!owner) {
                    return false;
                }
                if (owner->systemverilog != nullptr) {
                    const auto& source = *owner->systemverilog;
                    return source.nested_scope == process_scope
                        && source.callable
                        && std::ranges::find(
                               source.callable->formals,
                               declaration_id)
                        != source.callable->formals.end();
                }
                const auto& source = *owner->vhdl;
                return source.nested_scope == process_scope
                    && source.callable
                    && std::ranges::find(
                           source.callable->formals,
                           declaration_id)
                    != source.callable->formals.end();
            });
    };
    bool process_variable = false;
    std::optional<SignalId> package_constant;
    std::optional<semantic::vhdl::SubtypeIndication> debug_effective;
    if (declaration->systemverilog != nullptr) {
        const auto& source = *declaration->systemverilog;
        result.name = source.name;
        process_variable = materialized_local
            || (source.scope == process_scope
                && (source.form == semantic::sv::DeclarationForm::variable
                    || (source.form == semantic::sv::DeclarationForm::port
                        && callable_formal())));
        if (!process_variable
            && source.form != semantic::sv::DeclarationForm::port
            && source.form != semantic::sv::DeclarationForm::net
            && source.form != semantic::sv::DeclarationForm::variable) {
            return std::nullopt;
        }
        // A package variable binds to its single design-global signal.
        if (!process_variable && package_constant_signal_
            && source.form != semantic::sv::DeclarationForm::port) {
            package_constant = package_constant_signal_(declaration_id);
        }
        if (process_variable) {
            const auto width = source.type
                ? hir_systemverilog_type_width(*source.type)
                : std::nullopt;
            if (!width) {
                return std::nullopt;
            }
            result.width = *width;
            const auto four_state = hir_systemverilog_type_four_state(
                *source.type);
            result.domain = four_state
                ? frontend::ValueDomain::Logic4
                : frontend::ValueDomain::Bit2;
            result.signed_value = source.type->signed_value;
        }
    } else {
        const auto& source = *declaration->vhdl;
        result.name = source.name;
        result.vhdl_port
            = source.form == semantic::vhdl::DeclarationForm::port;
        if (result.vhdl_port) {
            result.vhdl_direction = source.direction;
        }
        const auto loop_parameter = source.form
                == semantic::vhdl::DeclarationForm::constant
            && source.scope.valid()
            && source.scope.value()
                < specialized_hir_unit_->design().semantics.scopes().size()
            && specialized_hir_unit_->design().semantics.scopes()[
                   source.scope.value()]
                       .name
                == "<loop>";
        process_variable = materialized_local
            || loop_parameter
            || (source.scope == process_scope
                && (source.form == semantic::vhdl::DeclarationForm::variable
                    || source.form
                        == semantic::vhdl::DeclarationForm::file
                    || (source.form == semantic::vhdl::DeclarationForm::port
                        && callable_formal())));
        // A composite constant the elaborator materialized as a read-only
        // signal, identified by its declaration span.
        const auto materialized_constant = [&] {
            if (process_variable
                || source.form != semantic::vhdl::DeclarationForm::constant) {
                return false;
            }
            const auto found = signals_.find(source.name);
            if (found != signals_.end()
                && found->second < design_.signal_info_.size()
                && read_only_signals_.contains(found->second)
                && design_.signal_info_[found->second].declaration_span
                    == hir_source_span(source.source)) {
                return true;
            }
            if (package_constant_signal_) {
                package_constant = package_constant_signal_(declaration_id);
            }
            return package_constant.has_value();
        };
        if (!process_variable
            && source.form != semantic::vhdl::DeclarationForm::port
            && source.form != semantic::vhdl::DeclarationForm::signal
            && source.form != semantic::vhdl::DeclarationForm::file
            && !(source.form
                    == semantic::vhdl::DeclarationForm::variable
                && source.shared)
            && !materialized_constant()) {
            debug_vhdl_binding_rejection(
                "scope-or-form", source, std::nullopt, process_variable);
            return std::nullopt;
        }
        if (process_variable) {
            if (source.form == semantic::vhdl::DeclarationForm::file
                || source.object_class
                    == semantic::vhdl::ObjectClass::file) {
                result.width = 32U;
                result.domain = frontend::ValueDomain::Bit2;
                result.signed_value = false;
                result.integer_range.reset();
            } else {
                auto local_subtype = source.subtype;
                if (local_subtype
                    && !local_subtype->type_mark.target.valid()) {
                    auto spelling = std::string_view {
                        local_subtype->type_mark.spelling
                    };
                    const auto separator = spelling.find_last_of('.');
                    if (separator != std::string_view::npos) {
                        spelling.remove_prefix(separator + 1U);
                    }
                    const auto cached_type
                        = find_hir_vhdl_subtype_name_cache(
                            source.scope, spelling);
                    std::optional<semantic::TypeId> matched_type;
                    if (cached_type != nullptr) {
                        matched_type = *cached_type;
                    } else {
                        std::optional<semantic::TypeId> matched;
                        std::optional<std::size_t> matched_distance;
                        bool ambiguous { };
                        const auto lexical_distance = [&](
                            const semantic::ScopeId scope)
                            -> std::optional<std::size_t> {
                            const auto& scopes = specialized_hir_unit_
                                                     ->design()
                                                     .semantics.scopes();
                            auto candidate = source.scope;
                            std::size_t distance { };
                            while (candidate.valid()
                                && candidate.value() < scopes.size()) {
                                if (candidate == scope) {
                                    return distance;
                                }
                                if (!scopes[candidate.value()].parent) {
                                    break;
                                }
                                candidate = *scopes[candidate.value()].parent;
                                ++distance;
                            }
                            return std::nullopt;
                        };
                        const auto consider_candidate =
                            [&](const auto& candidate) {
                                if ((candidate.form
                                            != semantic::vhdl::DeclarationForm::type
                                        && candidate.form
                                            != semantic::vhdl::DeclarationForm::subtype)
                                    || !candidate.declared_type
                                    || !same_hir_identifier(
                                        candidate.name, spelling, true)) {
                                    return;
                                }
                                const auto distance = lexical_distance(
                                    candidate.scope);
                                if (distance
                                    && (!matched_distance
                                        || *distance < *matched_distance)) {
                                    matched = candidate.declared_type;
                                    matched_distance = distance;
                                    ambiguous = false;
                                    return;
                                }
                                if (matched_distance) {
                                    if (distance
                                        && *distance == *matched_distance
                                        && matched
                                        && *matched
                                            != *candidate.declared_type) {
                                        ambiguous = true;
                                    }
                                    return;
                                }
                                if (matched
                                    && *matched != *candidate.declared_type) {
                                    ambiguous = true;
                                } else {
                                    matched = candidate.declared_type;
                                }
                            };
                        for (const auto& candidate :
                            specialized_hir_unit_->vhdl_declarations()) {
                            consider_candidate(candidate);
                        }
                        for (const auto& candidate :
                            specialized_hir_unit_->design().vhdl_hir
                                .declarations()) {
                            consider_candidate(candidate);
                        }
                        if (matched && !ambiguous) {
                            matched_type = matched;
                        }
                        cache_hir_vhdl_subtype_name(
                            source.scope, spelling, matched_type);
                    }
                    if (matched_type) {
                        local_subtype->type_mark.target = *matched_type;
                    }
                }
                // Bounds that depend on the active call frame, such as the
                // range of an unconstrained formal (`bit_vector(vec'range)`),
                // are folded per frame before the subtype is resolved.
                if (local_subtype && active_hir_callable_) {
                    for (auto& constraint : local_subtype->constraints) {
                        if (!constraint.left && constraint.left_expression) {
                            constraint.left = hir_constant_integer(
                                *constraint.left_expression);
                        }
                        if (!constraint.right
                            && constraint.right_expression) {
                            constraint.right = hir_constant_integer(
                                *constraint.right_expression);
                        }
                        if (constraint.direction_from_bounds
                            && constraint.left && constraint.right) {
                            constraint.descending
                                = *constraint.left > *constraint.right;
                            constraint.null = false;
                        }
                    }
                }
                auto effective = local_subtype
                    ? hir_effective_vhdl_subtype(*local_subtype)
                    : std::nullopt;
                if (!effective && local_subtype) {
                    effective = local_subtype;
                }
                if (effective
                    && vhdl_environment_time_record_subtype(
                        *specialized_hir_unit_, *effective,
                        source.scope)) {
                    effective->domain
                        = semantic::vhdl::ValueDomain::bit2;
                    effective->executable_width = 515U;
                    effective->integer_storage_width = 0U;
                    effective->signed_value = false;
                }
                const auto definition = effective
                        && effective->type_mark.target.valid()
                    ? specialized_hir_unit_->find_type(
                          effective->type_mark.target)
                    : std::nullopt;
                composite_vhdl_storage = definition
                    && definition->vhdl != nullptr
                    && (definition->vhdl->form
                            == semantic::vhdl::TypeForm::array
                        || definition->vhdl->form
                            == semantic::vhdl::TypeForm::record);
                null_vhdl_array = effective
                    && vhdl_null_array_subtype(
                        *specialized_hir_unit_, *effective);
                if (effective && definition
                    && definition->vhdl != nullptr
                    && (!effective->executable_width
                        || *effective->executable_width == 0U)) {
                    if (definition->vhdl->form
                        == semantic::vhdl::TypeForm::access) {
                        effective->executable_width = 32U;
                    } else if (definition->vhdl->form
                        == semantic::vhdl::TypeForm::physical) {
                        effective->executable_width
                            = vhdl_standard_
                                == frontend::VhdlStandard::Vhdl2019
                            ? 64U
                            : 32U;
                    } else if (!null_vhdl_array
                            && (definition->vhdl->form
                                == semantic::vhdl::TypeForm::array
                            || definition->vhdl->form
                                == semantic::vhdl::TypeForm::record)) {
                        effective->executable_width
                            = definition->vhdl->base.executable_width;
                    }
                }
                if (effective && definition
                    && definition->vhdl != nullptr
                    && effective->domain
                        == semantic::vhdl::ValueDomain::unknown) {
                    if (definition->vhdl->form
                        == semantic::vhdl::TypeForm::access) {
                        effective->domain
                            = semantic::vhdl::ValueDomain::bit2;
                    } else if (definition->vhdl->form
                        == semantic::vhdl::TypeForm::physical) {
                        effective->domain
                            = semantic::vhdl::ValueDomain::integer;
                    } else if (definition->vhdl->form
                            == semantic::vhdl::TypeForm::array
                        || definition->vhdl->form
                            == semantic::vhdl::TypeForm::record) {
                        effective->domain
                            = definition->vhdl->base.domain
                                == semantic::vhdl::ValueDomain::unknown
                            ? semantic::vhdl::ValueDomain::logic9
                            : definition->vhdl->base.domain;
                    }
                }
                debug_effective = effective;
                const auto local = hir_local_registers_.find(
                    declaration_id.value());
                if (local != hir_local_registers_.end()
                    && local->second < register_widths_.size()
                    && local->second < register_domains_.size()) {
                    result.width = register_widths_[local->second];
                    result.domain = register_domains_[local->second];
                } else {
                    const auto width = effective
                        ? vhdl_runtime_width(*effective)
                        : std::nullopt;
                    if (!effective || (!width && !null_vhdl_array)) {
                        debug_vhdl_binding_rejection(
                            "width", source, effective, process_variable);
                        return std::nullopt;
                    }
                    result.width = width.value_or(0U);
                    result.domain = vhdl_domain(effective->domain);
                }
                result.signed_value = (effective
                                          && effective->signed_value)
                    || result.domain == frontend::ValueDomain::Integer;
                result.integer_range = effective
                    ? vhdl_integer_range(*effective)
                    : std::nullopt;
            }
        }
    }
    if (!process_variable) {
        auto signal_id = package_constant;
        if (!signal_id) {
            auto found = signals_.end();
            if (active_hir_callable_) {
                const auto& frame
                    = hir_callable_frames_[*active_hir_callable_];
                if (frame.interface_receiver) {
                    found = signals_.find(
                        *frame.interface_receiver + "." + result.name);
                }
            }
            if (found == signals_.end()) {
                found = signals_.find(result.name);
            }
            if (found != signals_.end()) {
                signal_id = found->second;
            }
        }
        if (!signal_id || *signal_id >= design_.signal_info_.size()) {
            return std::nullopt;
        }
        result.kind = HirRuntimeBindingKind::signal;
        result.signal = *signal_id;
        result.name = design_.signal_info_[*signal_id].name;
        const auto& signal = design_.signal_info_[*signal_id];
        result.width = signal.width;
        result.domain = signal.source_domain;
        result.signed_value = signal.is_signed;
        result.integer_range = signal.integer_range;
        composite_vhdl_storage = signal.vhdl_array
            || !signal.packed_members.empty();
        if (!result.integer_range && signal.enumeration_range) {
            result.integer_range = frontend::IntegerRange {
                signal.enumeration_range->left,
                signal.enumeration_range->right,
                signal.enumeration_range->descending,
            };
        }
        null_vhdl_array = result.width == 0U
            && signal.vhdl_array
            && std::ranges::any_of(
                signal.vhdl_array->dimensions,
                [](const auto& dimension) { return dimension.null; });
        const auto valid_signal_profile = !result.name.empty()
            && (result.width != 0U || null_vhdl_array)
            && scalar_domain(result.domain)
            && (result.domain != frontend::ValueDomain::Integer
                || result.width == 32U || result.width == 64U
                || composite_vhdl_storage);
        if (!valid_signal_profile) {
            if (declaration->vhdl != nullptr) {
                debug_vhdl_binding_rejection(
                    "signal-profile", *declaration->vhdl, debug_effective,
                    process_variable);
            }
            return std::nullopt;
        }
        return result;
    }
    if (result.name.empty()
        || (result.width == 0U && !null_vhdl_array)
        || !scalar_domain(result.domain)
        || (result.domain == frontend::ValueDomain::Integer
            && result.width != 32U && result.width != 64U
            && !composite_vhdl_storage)) {
        if (declaration->vhdl != nullptr) {
            debug_vhdl_binding_rejection(
                "profile", *declaration->vhdl, debug_effective,
                process_variable);
        }
        return std::nullopt;
    }
    if (process_variable) {
        result.kind = HirRuntimeBindingKind::local;
        const auto found = hir_local_registers_.find(
            declaration_id.value());
        if (found != hir_local_registers_.end()) {
            result.local = found->second;
        } else if (require_storage) {
            if (declaration->vhdl != nullptr) {
                debug_vhdl_binding_rejection(
                    "storage", *declaration->vhdl, debug_effective,
                    process_variable);
            }
            return std::nullopt;
        }
        return result;
    }
    return std::nullopt;
}

std::optional<Lowerer::HirContainerObjectBinding>
Lowerer::hir_container_object_binding(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration_id = hir_referenced_declaration(
        expression_id);
    if (!declaration_id) {
        return std::nullopt;
    }
    return hir_container_object_binding_for_declaration(*declaration_id);
}

std::optional<Lowerer::HirContainerObjectBinding>
Lowerer::hir_container_object_binding_for_declaration(
    const semantic::DeclarationId declaration_value) const
{
    const std::optional<semantic::DeclarationId> declaration_id {
        declaration_value
    };
    const auto declaration
        = specialized_hir_unit_->find_declaration(*declaration_id);
    if (!declaration) {
        return std::nullopt;
    }
    const bool vhdl_directory = declaration->vhdl != nullptr
        && is_hir_vhdl_environment_directory(*declaration_id);
    const bool vhdl_call_path = declaration->vhdl != nullptr
        && is_hir_vhdl_environment_call_path(*declaration_id);
    const bool vhdl_environment_container
        = vhdl_directory || vhdl_call_path;
    if (!vhdl_environment_container
        && (declaration->systemverilog == nullptr
            || !declaration->systemverilog->type)) {
        return std::nullopt;
    }
    const auto& name = vhdl_environment_container
        ? declaration->vhdl->name
        : declaration->systemverilog->name;
    const auto local = hir_local_container_registers_.find(
        declaration_id->value());
    const auto local_type = hir_local_container_types_.find(
        declaration_id->value());
    if (local != hir_local_container_registers_.end()
        && local_type != hir_local_container_types_.end()) {
        return HirContainerObjectBinding {
            *declaration_id,
            name,
            { },
            local->second,
            &local_type->second,
            false,
        };
    }
    if (vhdl_environment_container) {
        return std::nullopt;
    }
    const auto object = container_objects_.find(name);
    if (object == container_objects_.end()) {
        return std::nullopt;
    }
    const auto info = std::ranges::find_if(
        design_.container_object_info_.rbegin(),
        design_.container_object_info_.rend(),
        [&](const ContainerObjectInfo& candidate) {
            return candidate.id == object->second;
        });
    if (info == design_.container_object_info_.rend()) {
        return std::nullopt;
    }
    const auto& type = info->type;
    return HirContainerObjectBinding {
        *declaration_id,
        name,
        object->second,
        std::nullopt,
        &type,
        read_only_container_objects_.contains(name)
            || read_only_container_objects_.contains(info->name),
    };
}

std::optional<Lowerer::HirContainerElementBinding>
Lowerer::hir_container_element_binding(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    std::vector<semantic::ExpressionId> indices;
    auto base = expression_id;
    while (true) {
        const auto expression = specialized_hir_unit_->find_expression(base);
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::index
            || expression->systemverilog->operands.size() != 2U) {
            break;
        }
        indices.push_back(expression->systemverilog->operands.back());
        base = expression->systemverilog->operands.front();
    }
    if (indices.empty()) {
        return std::nullopt;
    }
    std::ranges::reverse(indices);
    const auto object = hir_container_object_binding(base);
    if (!object || object->type == nullptr) {
        return std::nullopt;
    }
    auto selected_type = object->type;
    std::size_t consumed { };
    for (;;) {
        const auto dimensions = selected_type->fixed
            ? selected_type->dimensions.size() : 1U;
        if (dimensions == 0U || consumed + dimensions > indices.size()) {
            return std::nullopt;
        }
        consumed += dimensions;
        if (consumed == indices.size()) {
            break;
        }
        if (selected_type->element_kind != ContainerElementKind::Container
            || selected_type->element_types.size() != 1U) {
            return std::nullopt;
        }
        selected_type = &selected_type->element_types.front();
    }
    HirContainerElementBinding binding {
        object->declaration,
        object->name,
        object->object,
        object->local,
        object->type,
        selected_type,
        std::move(indices),
        selected_type->element_width,
        selected_type->two_state ? frontend::ValueDomain::Bit2
                                 : frontend::ValueDomain::Logic4,
        selected_type->signed_elements,
        object->read_only,
        std::nullopt,
    };
    binding.runtime_read_profile
        = hir_sv_runtime_container_read_profile(binding);
    return binding;
}

std::optional<Lowerer::HirContainerElementBinding::RuntimeReadProfile>
Lowerer::hir_sv_runtime_container_read_profile(
    const HirContainerElementBinding& element) const
{
    using Profile = HirContainerElementBinding::RuntimeReadProfile;
    using ElementKind = runtime::simir::ContainerElementKind;
    using TypeForm = semantic::sv::TypeForm;
    using DeclarationForm = semantic::sv::DeclarationForm;

    if (process_.scheduling_domain
            != ProcessSchedulingDomain::systemverilog
        || specialized_hir_unit_ == nullptr || element.type == nullptr
        || element.indices.empty()) {
        return std::nullopt;
    }

    const auto* runtime_container = element.type;
    const semantic::sv::TypeReference* source_leaf { };
    std::optional<semantic::sv::TypeReference> retained_source_leaf;
    semantic::ScopeId source_scope;
    if (element.declaration.valid()) {
        const auto declaration
            = specialized_hir_unit_->find_declaration(element.declaration);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type) {
            return std::nullopt;
        }
        source_scope = declaration->systemverilog->scope;
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        // Runtime ContainerType intentionally compresses ordinary source
        // leaves. Walk the retained HIR type tree by the same index levels
        // so defaults for typedefs and nested containers use the exact leaf.
        auto source_container = resolver.effective_systemverilog_type(
            *declaration->systemverilog->type, source_scope);
        if (!source_container) {
            return std::nullopt;
        }

        std::size_t consumed_indices { };
        while (true) {
            if (runtime_container == nullptr
                || !source_container->container_form
                || source_container->container_element_types.size() > 1U) {
                return std::nullopt;
            }
            const auto runtime_dimensions = runtime_container->fixed
                ? runtime_container->dimensions.size() : 1U;
            const auto source_dimensions
                = *source_container->container_form == TypeForm::static_array
                ? source_container->unpacked_dimensions.size() : 1U;
            if (runtime_dimensions == 0U
                || runtime_dimensions != source_dimensions
                || consumed_indices > element.indices.size()
                || runtime_dimensions
                    > element.indices.size() - consumed_indices) {
                return std::nullopt;
            }
            consumed_indices += runtime_dimensions;
            // An absent child entry is the HIR shorthand for a direct leaf;
            // nested containers retain their child explicitly.
            auto source_element = source_container->container_element_types
                    .empty()
                ? *source_container
                : source_container->container_element_types.front();
            if (source_container->container_element_types.empty()) {
                if (consumed_indices != element.indices.size()) {
                    return std::nullopt;
                }
                source_element.container_form.reset();
                source_element.queue_maximum.reset();
                source_element.associative_index.reset();
                source_element.unpacked_dimensions.clear();
                source_element.container_element_types.clear();
            }
            auto effective_element = resolver.effective_systemverilog_type(
                source_element, source_scope);
            if (!effective_element) {
                return std::nullopt;
            }
            if (consumed_indices == element.indices.size()) {
                if (element.selected_type != nullptr
                    && element.selected_type != runtime_container) {
                    return std::nullopt;
                }
                retained_source_leaf = std::move(*effective_element);
                source_leaf = &*retained_source_leaf;
                break;
            }
            if (runtime_container->element_kind != ElementKind::Container
                || runtime_container->element_types.size() != 1U) {
                return std::nullopt;
            }
            const auto& runtime_element
                = runtime_container->element_types.front();
            if (runtime_element.element_kind != ElementKind::Container) {
                return std::nullopt;
            }
            runtime_container = &runtime_element;
            source_container = std::move(effective_element);
        }
    }

    const auto* runtime_leaf = element.selected_type != nullptr
        ? element.selected_type : runtime_container;
    if (runtime_leaf == nullptr
        || runtime_leaf->element_width == 0U
        || element.width != runtime_leaf->element_width) {
        return std::nullopt;
    }
    if (source_leaf != nullptr
        && runtime_leaf->element_kind == ElementKind::Packed) {
        const auto source_width = hir_systemverilog_type_width(*source_leaf);
        if (!source_width || *source_width != runtime_leaf->element_width) {
            return std::nullopt;
        }
    }

    if (runtime_leaf->element_kind == ElementKind::Scalar) {
        switch (runtime_leaf->scalar_kind) {
        case frontend::SystemVerilogScalarKind::ShortReal:
        case frontend::SystemVerilogScalarKind::Real:
        case frontend::SystemVerilogScalarKind::Realtime:
        case frontend::SystemVerilogScalarKind::Chandle:
            return Profile { element.domain, false };
        case frontend::SystemVerilogScalarKind::Time: {
            if (source_leaf != nullptr) {
                const semantic::CompiledDesignResolver resolver {
                    *specialized_hir_unit_, hir_generic_binding_frames_
                };
                std::unordered_set<std::uint32_t> visiting_time_types;
                const auto is_time_type = [&](const auto& self,
                                              const semantic::sv::TypeReference& input,
                                              const semantic::ScopeId use_scope)
                    -> bool {
                    const auto reference
                        = resolver.effective_systemverilog_type(
                              input, use_scope).value_or(input);
                    if (reference.target.spelling == "time") {
                        return true;
                    }
                    if (!reference.target.target.valid()
                        || !visiting_time_types.insert(
                            reference.target.target.value()).second) {
                        return false;
                    }
                    const auto definition
                        = specialized_hir_unit_->find_type(
                            reference.target.target);
                    bool result { };
                    if (definition
                        && definition->systemverilog != nullptr) {
                        const auto type_declaration
                            = specialized_hir_unit_->find_declaration(
                                definition->systemverilog->declaration);
                        const auto declaration_form = type_declaration
                                && type_declaration->systemverilog != nullptr
                            ? type_declaration->systemverilog->form
                            : DeclarationForm::variable;
                        const auto follows_base
                            = definition->systemverilog->form
                                    == TypeForm::alias
                                || definition->systemverilog->form
                                    == TypeForm::type_parameter
                                || declaration_form
                                    == DeclarationForm::typedef_declaration
                                || declaration_form
                                    == DeclarationForm::type_parameter
                                || declaration_form
                                    == DeclarationForm::nettype_declaration;
                        if (!follows_base) {
                            visiting_time_types.erase(
                                reference.target.target.value());
                            return false;
                        }
                        const auto type_scope = type_declaration
                                && type_declaration->systemverilog != nullptr
                            ? type_declaration->systemverilog->scope
                            : use_scope;
                        result = self(
                            self, definition->systemverilog->base,
                            type_scope);
                    }
                    visiting_time_types.erase(
                        reference.target.target.value());
                    return result;
                };
                if (!is_time_type(is_time_type, *source_leaf, source_scope)) {
                    return std::nullopt;
                }
            }
            // The ordinary container profile currently marks scalar storage
            // two-state, but a missing Time element defaults to all X. Keep
            // that domain correction local to this guarded read.
            return Profile { frontend::ValueDomain::Logic4, true };
        }
        case frontend::SystemVerilogScalarKind::None:
            return std::nullopt;
        }
        return std::nullopt;
    }
    if (runtime_leaf->element_kind != ElementKind::Packed) {
        return std::nullopt;
    }

    if (source_leaf == nullptr) {
        if (!element.declaration.valid()
            && runtime_leaf->element_nominal_type.empty()
            && runtime_leaf->element_types.empty()) {
            return Profile {
                runtime_leaf->two_state
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4,
                !runtime_leaf->two_state,
            };
        }
        return std::nullopt;
    }

    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    std::unordered_set<std::uint32_t> visiting;
    const auto builtin_four_state = [](const std::string_view spelling)
        -> std::optional<bool> {
        const auto descriptor
            = frontend::systemverilog_integral_type_descriptor(spelling);
        if (!descriptor) {
            return std::nullopt;
        }
        if (descriptor->domain == frontend::ValueDomain::Logic4) {
            return true;
        }
        if (descriptor->domain == frontend::ValueDomain::Bit2) {
            return false;
        }
        return std::nullopt;
    };
    const auto four_state = [&](const auto& self,
                                const semantic::sv::TypeReference& input,
                                const semantic::ScopeId use_scope)
        -> std::optional<bool> {
        auto reference = resolver.effective_systemverilog_type(
            input, use_scope).value_or(input);
        if (reference.container_form) {
            return std::nullopt;
        }
        const auto spelling = std::string_view {
            reference.target.spelling
        };
        if (spelling == "time") {
            return true;
        }
        if (reference.target.target.valid()) {
            const auto type_id = reference.target.target;
            if (!visiting.insert(type_id.value()).second) {
                return std::nullopt;
            }
            const auto definition
                = specialized_hir_unit_->find_type(type_id);
            std::optional<bool> result;
            if (definition && definition->systemverilog != nullptr) {
                const auto& type = *definition->systemverilog;
                const auto type_declaration
                    = specialized_hir_unit_->find_declaration(
                        type.declaration);
                const auto type_scope = type_declaration
                        && type_declaration->systemverilog != nullptr
                    ? type_declaration->systemverilog->scope
                    : use_scope;
                switch (type.form) {
                case TypeForm::enumeration:
                    // The default follows the enum base type, not its first
                    // declared literal.
                    result = self(self, type.base, type_scope);
                    break;
                case TypeForm::alias:
                case TypeForm::type_parameter:
                    result = self(self, type.base, type_scope);
                    break;
                case TypeForm::packed_integral:
                    // Use the frontend's single authoritative builtin-domain
                    // table even when HIR carries a synthetic typedef.
                    if (const auto builtin
                        = builtin_four_state(spelling)) {
                        result = *builtin;
                    } else if (type_declaration
                        && type_declaration->systemverilog != nullptr
                        && (type_declaration->systemverilog->form
                                == DeclarationForm::typedef_declaration
                            || type_declaration->systemverilog->form
                                == DeclarationForm::type_parameter
                            || type_declaration->systemverilog->form
                                == DeclarationForm::nettype_declaration)) {
                        result = self(
                            self, type.base,
                            type_declaration->systemverilog->scope);
                    } else {
                        result = reference.four_state;
                    }
                    break;
                case TypeForm::packed_structure:
                case TypeForm::packed_union: {
                    if (type.members.empty()) {
                        break;
                    }
                    // Packed aggregates are one packed integral value. Their
                    // default domain is four-state when any member is.
                    bool aggregate_four_state { };
                    bool complete = true;
                    for (const auto& member : type.members) {
                        const auto member_four_state
                            = self(self, member.type, type_scope);
                        if (!member_four_state) {
                            complete = false;
                            break;
                        }
                        aggregate_four_state
                            = aggregate_four_state || *member_four_state;
                    }
                    if (complete) {
                        result = aggregate_four_state;
                    }
                    break;
                }
                default:
                    break;
                }
            }
            visiting.erase(type_id.value());
            return result;
        }

        // Resolve builtin spelling before value_form: an enum base can
        // retain the outer enum's form while naming a primitive type.
        if (const auto builtin = builtin_four_state(spelling)) {
            return builtin;
        }
        if (reference.value_form
            && *reference.value_form != TypeForm::packed_integral) {
            return std::nullopt;
        }
        if (reference.value_form == TypeForm::packed_integral) {
            return reference.four_state;
        }
        return std::nullopt;
    };
    const auto leaf_four_state
        = four_state(four_state, *source_leaf, source_scope);
    if (!leaf_four_state) {
        return std::nullopt;
    }
    return Profile {
        *leaf_four_state
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2,
        *leaf_four_state,
    };
}

std::optional<Lowerer::HirStaticContainerIndex>
Lowerer::hir_static_container_index(
    const HirContainerElementBinding& element) const
{
    if (specialized_hir_unit_ == nullptr || element.type == nullptr
        || element.type->dimensions.empty()
        || element.indices.size() != element.type->dimensions.size()) {
        return std::nullopt;
    }
    std::uint64_t count = 1U;
    std::uint64_t ordinal { };
    std::string suffix;
    for (std::size_t position = 0U; position < element.indices.size(); ++position) {
        const auto [dimension_left, dimension_right]
            = element.type->dimensions[position];
        const auto left = static_cast<std::int64_t>(dimension_left);
        const auto right = static_cast<std::int64_t>(dimension_right);
        const auto index_id = element.indices[position];
        const auto expression = specialized_hir_unit_->find_expression(index_id);
        // Every coordinate must be a pure, known constant. A dynamic or
        // unknown coordinate retains the ordinary aggregate dependency.
        if (!expression || expression->systemverilog == nullptr
            || hir_expression_is_residual(index_id)
            || has_runtime_systemverilog_call(*specialized_hir_unit_, index_id)) {
            return std::nullopt;
        }
        const auto constant = hir_constant_integer(index_id);
        const auto width = hir_expression_width(index_id, hir_process_scope_);
        if (!constant || !width || *width == 0U || *width > 64U) {
            return std::nullopt;
        }
        auto index = *constant;
        const bool signed_index = hir_expression_signed(index_id);
        if (*width < 64U) {
            const auto mask = (std::uint64_t { 1U } << *width) - 1U;
            const auto bits = static_cast<std::uint64_t>(index) & mask;
            const auto sign_bit = std::uint64_t { 1U } << (*width - 1U);
            if (signed_index && (bits & sign_bit) != 0U) {
                const auto magnitude = (std::uint64_t { 1U } << *width) - bits;
                index = -static_cast<std::int64_t>(magnitude);
            } else {
                index = static_cast<std::int64_t>(bits);
            }
        } else if (!signed_index && index < 0) {
            // The signed evaluator cannot represent an unsigned value above
            // INT64_MAX; do not reinterpret it as a negative coordinate.
            return std::nullopt;
        }
        if (index < std::numeric_limits<std::int32_t>::min()
            || index > std::numeric_limits<std::int32_t>::max()
            || index < std::min(left, right) || index > std::max(left, right)) {
            return std::nullopt;
        }
        const auto extent = static_cast<std::uint64_t>(
            left >= right ? left - right : right - left) + 1U;
        if (extent > std::numeric_limits<std::uint32_t>::max()
            || count > std::numeric_limits<std::uint32_t>::max() / extent) {
            return std::nullopt;
        }
        const auto coordinate = static_cast<std::uint64_t>(
            left >= right ? left - index : index - left);
        ordinal = ordinal * extent + coordinate;
        count *= extent;
        suffix += "[" + std::to_string(index) + "]";
    }
    return HirStaticContainerIndex {
        static_cast<std::uint32_t>(ordinal),
        static_cast<std::uint32_t>(count), std::move(suffix) };
}

std::optional<Lowerer::HirStaticContainerSignalExtract>
Lowerer::hir_static_element_signal_extract(
    const HirContainerElementBinding& element) const
{
    if (std::ranges::none_of(design_.container_element_signal_aliases_,
            [&](const auto& alias) { return alias.object == element.object; })) {
        return std::nullopt;
    }
    using ElementKind = runtime::simir::ContainerElementKind;
    if (specialized_hir_unit_ == nullptr
        || specialized_hir_unit_->language()
            != semantic::Language::system_verilog
        || element.local
        || element.type == nullptr
        || element.selected_type != element.type
        || element.indices.size() != element.type->dimensions.size()) {
        return std::nullopt;
    }

    const auto& type = *element.type;
    if (!type.fixed || type.queue || type.associative
        || type.string_indices || type.dimensions.empty()
        || !type.element_types.empty()
        || type.element_kind != ElementKind::Packed
        || type.element_width == 0U
        || type.element_width
            > std::numeric_limits<std::uint32_t>::max()
        || element.width != type.element_width
        || type.two_state
        || element.domain != frontend::ValueDomain::Logic4
        || element.signed_value != type.signed_elements) {
        return std::nullopt;
    }

    const auto& dimension = type.dimensions.front();
    if (type.index_left != dimension.first
        || type.index_right != dimension.second
        || element.object >= design_.container_objects_.size()) {
        return std::nullopt;
    }

    const auto selected_index = hir_static_container_index(element);
    const auto& object = design_.container_objects_[element.object];
    if (!selected_index
        || selected_index->count != object.initial_value.elements.size()) {
        return std::nullopt;
    }
    const auto ordinal = selected_index->ordinal;
    const auto object_info = std::ranges::find_if(
        design_.container_object_info_,
        [&](const ContainerObjectInfo& candidate) {
            return candidate.id == element.object;
        });
    if (object.slice_alias
        || object.initial_value.type != type
        || object_info == design_.container_object_info_.end()
        || object_info->is_port
        || object_info->slice_alias
        || object_info->type != type) {
        return std::nullopt;
    }

    const runtime::simir::ContainerElementSignalAlias* alias { };
    for (const auto& candidate : design_.container_element_signal_aliases_) {
        if (candidate.object != element.object
            || candidate.ordinal != static_cast<std::uint32_t>(ordinal)) {
            continue;
        }
        if (alias != nullptr || !candidate.readable) {
            return std::nullopt;
        }
        alias = &candidate;
    }
    if (alias == nullptr
        || std::ranges::any_of(
            design_.container_signal_aliases_,
            [&](const runtime::simir::ContainerSignalAlias& candidate) {
                return candidate.object == element.object;
            })) {
        return std::nullopt;
    }

    const auto signal_index = static_cast<std::size_t>(alias->signal);
    if (signal_index >= design_.signal_info_.size()
        || signal_index >= design_.signals_.size()) {
        return std::nullopt;
    }
    const auto& signal_info = design_.signal_info_[signal_index];
    const auto& signal = design_.signals_[signal_index];
    const auto expected_name = object_info->name + selected_index->suffix;
    if (signal_info.id != alias->signal
        || signal_info.name != expected_name
        || signal_info.vhdl_array != nullptr
        || signal_info.vhdl_access != nullptr
        || signal_info.vhdl_physical != nullptr
        || !signal_info.vhdl_mode_view_bindings.empty()
        || signal_info.source_domain != frontend::ValueDomain::Logic4
        || signal_info.is_signed != type.signed_elements
        || signal_info.width != type.element_width
        || signal.initial_value.width() != type.element_width
        || signal.initial_value.is_logic9()) {
        return std::nullopt;
    }

    return HirStaticContainerSignalExtract {
        alias->signal,
        static_cast<std::uint32_t>(element.width),
        0U,
        static_cast<std::uint32_t>(element.width),
        frontend::ValueDomain::Logic4,
    };
}

std::optional<Lowerer::HirStaticContainerSignalExtract>
Lowerer::hir_static_container_signal_extract(
    const HirContainerElementBinding& element) const
{
    if (const auto leaf = hir_static_element_signal_extract(element)) {
        return leaf;
    }
    using ElementKind = runtime::simir::ContainerElementKind;
    if (specialized_hir_unit_ == nullptr
        || specialized_hir_unit_->language()
            != semantic::Language::system_verilog
        || element.local
        || element.type == nullptr
        || element.selected_type != element.type
        || element.indices.size() != element.type->dimensions.size()) {
        return std::nullopt;
    }

    const auto& type = *element.type;
    if (!type.fixed || type.queue || type.associative
        || type.string_indices || type.dimensions.empty()
        || !type.element_types.empty()
        || type.element_kind != ElementKind::Packed
        || type.element_width == 0U
        || element.width != type.element_width
        || type.two_state
        || element.domain != frontend::ValueDomain::Logic4
        || element.signed_value != type.signed_elements) {
        return std::nullopt;
    }

    const auto& dimension = type.dimensions.front();
    if (type.index_left != dimension.first
        || type.index_right != dimension.second
        || element.object >= design_.container_objects_.size()) {
        return std::nullopt;
    }

    const auto selected_index = hir_static_container_index(element);
    if (!selected_index) {
        return std::nullopt;
    }
    const auto extent = static_cast<std::uint64_t>(selected_index->count);
    const auto element_width = static_cast<std::uint64_t>(
        type.element_width);
    const auto maximum_width = static_cast<std::uint64_t>(
        std::numeric_limits<std::uint32_t>::max());
    if (element_width == 0U
        || extent > maximum_width / element_width) {
        return std::nullopt;
    }
    const auto expected_backing_width = extent * element_width;
    if (expected_backing_width
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    const auto ordinal = selected_index->ordinal;
    const auto consumed_width
        = (static_cast<std::uint64_t>(ordinal) + 1U) * element_width;
    if (consumed_width > expected_backing_width) {
        return std::nullopt;
    }

    const auto& object = design_.container_objects_[element.object];
    const auto object_info = std::ranges::find_if(
        design_.container_object_info_,
        [&](const ContainerObjectInfo& candidate) {
            return candidate.id == element.object;
        });
    if (object.slice_alias
        || object.initial_value.elements.size() != selected_index->count
        || object.initial_value.type != type
        || object_info == design_.container_object_info_.end()
        || object_info->is_port
        || object_info->slice_alias
        || object_info->type != type) {
        return std::nullopt;
    }

    const runtime::simir::ContainerSignalAlias* alias { };
    for (const auto& candidate : design_.container_signal_aliases_) {
        if (candidate.object != element.object) {
            continue;
        }
        if (alias != nullptr || !candidate.readable) {
            return std::nullopt;
        }
        alias = &candidate;
    }
    if (alias == nullptr) {
        return std::nullopt;
    }

    const auto signal_index = static_cast<std::size_t>(alias->signal);
    if (signal_index >= design_.signal_info_.size()
        || signal_index >= design_.signals_.size()) {
        return std::nullopt;
    }
    const auto& signal_info = design_.signal_info_[signal_index];
    const auto& signal = design_.signals_[signal_index];
    if (signal_info.id != alias->signal
        || signal_info.name != object_info->name
        || signal_info.vhdl_array != nullptr
        || signal_info.vhdl_access != nullptr
        || signal_info.vhdl_physical != nullptr
        || !signal_info.vhdl_mode_view_bindings.empty()
        || signal_info.source_domain != frontend::ValueDomain::Logic4
        || signal_info.is_signed != type.signed_elements
        || signal.initial_value.is_logic9()) {
        return std::nullopt;
    }

    const auto bridge_width
        = runtime::simir::container_signal_bridge_width(type);
    if (!bridge_width
        || *bridge_width
            != static_cast<std::size_t>(expected_backing_width)
        || signal_info.width != *bridge_width
        || signal.initial_value.width() != *bridge_width) {
        return std::nullopt;
    }

    return HirStaticContainerSignalExtract {
        alias->signal,
        static_cast<std::uint32_t>(expected_backing_width),
        static_cast<std::uint32_t>(
            expected_backing_width - consumed_width),
        type.element_width,
        frontend::ValueDomain::Logic4,
    };
}

std::optional<Lowerer::HirContainerAggregateSelection>
Lowerer::hir_container_aggregate_selection(
    const semantic::ExpressionId expression_id, const bool allow_string) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    constexpr auto prefix = std::string_view { "@sv-select:" };
    std::vector<std::string_view> names;
    auto base = expression_id;
    for (;;) {
        const auto expression = specialized_hir_unit_->find_expression(base);
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::call
            || !expression->systemverilog->text.starts_with(prefix)
            || expression->systemverilog->operands.size() != 1U) {
            break;
        }
        const auto name = std::string_view {
            expression->systemverilog->text }.substr(prefix.size());
        if (name.empty()) {
            return std::nullopt;
        }
        names.push_back(name);
        base = expression->systemverilog->operands.front();
    }
    std::ranges::reverse(names);
    const auto base_expression
        = specialized_hir_unit_->find_expression(base);
    constexpr auto indexed_prefix = std::string_view { "index." };
    if (base_expression && base_expression->systemverilog != nullptr
        && base_expression->systemverilog->kind
            == semantic::sv::ExpressionKind::index
        && base_expression->systemverilog->text.starts_with(
            indexed_prefix)) {
        auto remaining = std::string_view {
            base_expression->systemverilog->text
        }.substr(indexed_prefix.size());
        std::vector<std::string_view> embedded;
        while (!remaining.empty()) {
            const auto separator = remaining.find('.');
            const auto segment = remaining.substr(0U, separator);
            if (segment.empty()) {
                return std::nullopt;
            }
            embedded.push_back(segment);
            if (separator == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(separator + 1U);
        }
        names.insert(names.begin(), embedded.begin(), embedded.end());
    }
    std::optional<HirContainerElementBinding> element;
    // A member of an unpacked structure variable, `s.a.b` (IEEE 1800-2017
    // 7.2): the structure is a singleton aggregate container.
    if (names.empty() && base_expression
        && base_expression->systemverilog != nullptr
        && base_expression->systemverilog->kind
            == semantic::sv::ExpressionKind::name) {
        const std::string_view text = base_expression->systemverilog->text;
        const auto separator = text.find('.');
        if (separator != std::string_view::npos && separator != 0U) {
            const auto structure_name = text.substr(0U, separator);
            auto declaration = hir_referenced_declaration(base);
            const auto named = declaration
                ? specialized_hir_unit_->find_declaration(*declaration)
                : std::nullopt;
            if (!named || named->systemverilog == nullptr
                || named->systemverilog->name != structure_name) {
                const semantic::CompiledDeclarationPredicate object
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.systemverilog != nullptr
                              && candidate.systemverilog->type.has_value()
                              && (candidate.systemverilog->form
                                      == semantic::sv::DeclarationForm::
                                          variable
                                  || candidate.systemverilog->form
                                      == semantic::sv::DeclarationForm::port);
                      };
                declaration = semantic::CompiledDesignResolver {
                    *specialized_hir_unit_, hir_generic_binding_frames_
                }.resolve_systemverilog(structure_name,
                     base_expression->systemverilog->scope, object, false)
                                  .unique();
            }
            const auto object = declaration
                ? hir_container_object_binding_for_declaration(*declaration)
                : std::nullopt;
            if (object && object->type != nullptr
                && object->type->aggregate_value) {
                auto remaining = text.substr(separator + 1U);
                while (!remaining.empty()) {
                    const auto next = remaining.find('.');
                    const auto segment = remaining.substr(0U, next);
                    if (segment.empty()) {
                        return std::nullopt;
                    }
                    names.push_back(segment);
                    if (next == std::string_view::npos) {
                        break;
                    }
                    remaining.remove_prefix(next + 1U);
                }
                element = HirContainerElementBinding {
                    object->declaration,
                    object->name,
                    object->object,
                    object->local,
                    object->type,
                    object->type,
                    { },
                    object->type->element_width,
                    object->type->two_state ? frontend::ValueDomain::Bit2
                                            : frontend::ValueDomain::Logic4,
                    object->type->signed_elements,
                    object->read_only,
                    std::nullopt,
                };
            }
        }
    }
    if (names.empty()) {
        return std::nullopt;
    }
    if (!element) {
        element = hir_container_element_binding(base);
    }
    if (!element || element->type == nullptr
        || element->type->element_kind
            != ContainerElementKind::Aggregate) {
        return std::nullopt;
    }
    const auto* current = element->type;
    std::vector<std::uint32_t> members;
    members.reserve(names.size());
    for (const auto name : names) {
        if (current->element_kind != ContainerElementKind::Aggregate
            || current->member_names.size()
                != current->element_types.size()) {
            return std::nullopt;
        }
        const auto member = std::ranges::find(
            current->member_names, name);
        if (member == current->member_names.end()) {
            return std::nullopt;
        }
        const auto index = static_cast<std::size_t>(std::distance(
            current->member_names.begin(), member));
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        members.push_back(static_cast<std::uint32_t>(index));
        current = &current->element_types[index];
    }
    const bool string_leaf = allow_string
        && current->element_kind == ContainerElementKind::String;
    if (!string_leaf
        && ((current->element_kind != ContainerElementKind::Packed
                && current->element_kind != ContainerElementKind::Scalar)
            || current->element_width == 0U)) {
        return std::nullopt;
    }
    return HirContainerAggregateSelection {
        std::move(*element), std::move(members), *current
    };
}

bool Lowerer::hir_sv_dynamic_aggregate_member_read_supported(
    const HirContainerAggregateSelection& selection) const
{
    using TypeForm = semantic::sv::TypeForm;

    const auto& element = selection.element;
    if (element.indices.empty()) {
        return false;
    }
    if (process_.scheduling_domain
            != ProcessSchedulingDomain::systemverilog
        || specialized_hir_unit_ == nullptr
        || !element.declaration.valid() || element.type == nullptr
        || element.type->fixed || element.type->queue
        || element.type->associative || element.type->string_indices
        || element.type->element_kind
            != runtime::simir::ContainerElementKind::Aggregate
        || element.indices.size() != 1U || selection.members.empty()
        || selection.leaf.element_width == 0U
        || selection.leaf.scalar_kind
            != frontend::SystemVerilogScalarKind::None
        || (selection.leaf.element_kind
                != runtime::simir::ContainerElementKind::Packed
            && selection.leaf.element_kind
                != runtime::simir::ContainerElementKind::Scalar)) {
        return false;
    }

    const auto declaration
        = specialized_hir_unit_->find_declaration(element.declaration);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type) {
        return false;
    }
    const auto source_scope = declaration->systemverilog->scope;
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    auto source_container = resolver.effective_systemverilog_type(
        *declaration->systemverilog->type, source_scope);
    if (!source_container
        || source_container->container_form != TypeForm::dynamic_array
        || source_container->container_element_types.size() > 1U) {
        return false;
    }
    auto source_element_type = source_container->container_element_types
            .empty()
        ? *source_container
        : source_container->container_element_types.front();
    if (source_container->container_element_types.empty()) {
        source_element_type.container_form.reset();
        source_element_type.queue_maximum.reset();
        source_element_type.associative_index.reset();
        source_element_type.unpacked_dimensions.clear();
        source_element_type.container_element_types.clear();
    }
    auto source_member_type = resolver.effective_systemverilog_type(
        source_element_type, source_scope);
    if (!source_member_type) {
        return false;
    }

    // Do not synthesize a member default when the source declares an explicit
    // struct-member initializer: Table 7-1 delegates that value to 7.2.2.
    for (const auto member_index : selection.members) {
        if (!source_member_type->target.target.valid()) {
            return false;
        }
        const auto definition = specialized_hir_unit_->find_type(
            source_member_type->target.target);
        if (!definition || definition->systemverilog == nullptr
            || definition->systemverilog->form
                != TypeForm::unpacked_structure
            || member_index >= definition->systemverilog->members.size()) {
            return false;
        }
        const auto& member
            = definition->systemverilog->members[member_index];
        if (member.initializer) {
            return false;
        }
        const auto type_declaration
            = specialized_hir_unit_->find_declaration(
                definition->systemverilog->declaration);
        const auto type_scope = type_declaration
                && type_declaration->systemverilog != nullptr
            ? type_declaration->systemverilog->scope
            : source_scope;
        source_member_type = resolver.effective_systemverilog_type(
            member.type, type_scope);
        if (!source_member_type) {
            return false;
        }
    }
    return true;
}

std::optional<Lowerer::HirPackedContainerAggregateProfile>
Lowerer::hir_packed_container_aggregate_profile(
    const semantic::ExpressionId expression_id) const
{
    const auto binding = hir_container_object_binding(expression_id);
    if (!binding || binding->type == nullptr
        || binding->type->element_kind
            != ContainerElementKind::Aggregate
        || !binding->type->aggregate_value) {
        return std::nullopt;
    }
    std::size_t width { };
    bool four_state { };
    const auto collect = [&](const auto& self,
                             const ContainerType& type) -> bool {
        if (type.element_kind == ContainerElementKind::Aggregate) {
            if (type.element_types.empty()
                || (!type.member_names.empty()
                    && type.member_names.size()
                        != type.element_types.size())) {
                return false;
            }
            return std::ranges::all_of(
                type.element_types,
                [&](const auto& member) { return self(self, member); });
        }
        if (type.element_kind != ContainerElementKind::Packed
            || type.element_width == 0U
            || type.element_width
                > std::numeric_limits<std::size_t>::max() - width) {
            return false;
        }
        width += type.element_width;
        four_state = four_state || !type.two_state;
        return true;
    };
    if (!collect(collect, *binding->type) || width == 0U
        || width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return HirPackedContainerAggregateProfile {
        width,
        four_state ? frontend::ValueDomain::Logic4
                   : frontend::ValueDomain::Bit2,
    };
}

std::optional<ContainerType>
Lowerer::hir_static_container_expression_type(
    const semantic::ExpressionId expression_id) const
{
    if (const auto selection = hir_static_container_selection_profile(
            expression_id)) {
        return selection->selected_type;
    }
    if (const auto binding = hir_container_object_binding(expression_id)) {
        return binding->type != nullptr
            ? std::optional { *binding->type }
            : std::nullopt;
    }
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "?:" && source.operands.size() == 3U) {
        const auto when_true = hir_static_container_expression_type(
            source.operands[1]);
        const auto when_false = hir_static_container_expression_type(
            source.operands[2]);
        return when_true && when_false && *when_true == *when_false
            ? when_true
            : std::nullopt;
    }
    if (source.kind == semantic::sv::ExpressionKind::call) {
        const auto declaration_id = hir_referenced_declaration(
            expression_id);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->form
                == semantic::sv::DeclarationForm::function
            && declaration->systemverilog->type) {
            return hir_systemverilog_container_type(
                *declaration->systemverilog->type);
        }
    }
    return std::nullopt;
}

bool Lowerer::hir_static_array_function_call(
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
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    return declaration && declaration->systemverilog != nullptr
        && declaration->systemverilog->form
            == semantic::sv::DeclarationForm::function
        && declaration->systemverilog->type
        && declaration->systemverilog->type->container_form
            == semantic::sv::TypeForm::static_array;
}

std::optional<std::uint64_t>
Lowerer::hir_systemverilog_container_bit_width(
    const ContainerType& type) const
{
    std::uint64_t bits { };
    if (type.element_kind == ContainerElementKind::Aggregate) {
        if (type.element_types.empty()) {
            return std::nullopt;
        }
        for (const auto& member : type.element_types) {
            const auto member_width = hir_systemverilog_container_bit_width(
                member);
            if (!member_width) {
                return std::nullopt;
            }
            if (type.union_aggregate) {
                bits = std::max(bits, *member_width);
            } else if (*member_width
                > std::numeric_limits<std::uint64_t>::max() - bits) {
                return std::nullopt;
            } else {
                bits += *member_width;
            }
        }
    } else if (type.element_kind == ContainerElementKind::Container
        && type.element_types.size() == 1U) {
        const auto nested = hir_systemverilog_container_bit_width(
            type.element_types.front());
        if (!nested) {
            return std::nullopt;
        }
        bits = *nested;
    } else {
        bits = type.element_width;
    }
    if (bits == 0U) {
        return std::nullopt;
    }
    if (!type.fixed) {
        return bits;
    }
    for (const auto& dimension : type.dimensions) {
        const auto extent = static_cast<std::uint64_t>(
            std::abs(static_cast<std::int64_t>(dimension.first)
                - dimension.second)
            + 1);
        if (extent != 0U
            && bits > std::numeric_limits<std::uint64_t>::max() / extent) {
            return std::nullopt;
        }
        bits *= extent;
    }
    return bits;
}

std::optional<std::int64_t>
Lowerer::hir_systemverilog_container_query(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const bool bound_query = call.text == "$left"
        || call.text == "$right" || call.text == "$low"
        || call.text == "$high" || call.text == "$increment";
    const bool size_query = call.text == "$size";
    const bool bits_query = call.text == "$bits";
    const bool dimensions_query = call.text == "$dimensions";
    const bool unpacked_dimensions_query
        = call.text == "$unpacked_dimensions";
    const bool accepts_dimension = bound_query || size_query;
    if (call.kind != semantic::sv::ExpressionKind::call
        || (!accepts_dimension && !bits_query
            && !dimensions_query && !unpacked_dimensions_query)
        || call.operands.empty()
        || call.operands.size() > (accepts_dimension ? 2U : 1U)) {
        return std::nullopt;
    }
    const auto receiver = call.operands.front();
    // A member selection references its aggregate's declaration; its size
    // is the member's, which the expression-width path provides.
    if (bits_query && hir_systemverilog_member_selection(receiver)) {
        return std::nullopt;
    }
    const auto receiver_expression
        = specialized_hir_unit_->find_expression(receiver);
    const auto named_type_declaration
        = receiver_expression
            && receiver_expression->systemverilog != nullptr
            && receiver_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name
        ? hir_systemverilog_named_type_declaration(
              receiver_expression->systemverilog->text,
              receiver_expression->systemverilog->scope)
        : std::nullopt;
    const auto selected_type = named_type_declaration
        ? std::nullopt
        : hir_static_container_expression_type(receiver);
    const auto declaration_id = named_type_declaration
        ? named_type_declaration
        : selected_type ? std::nullopt
                        : hir_referenced_declaration(receiver);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    // A query operand can designate a typedef or type parameter rather than
    // an object. Resolve that namespace before the ordinary expression path:
    // a value-namespace placeholder can have the same spelling and a scalar
    // fallback type. Keep nominal-type ownership in CompiledDesignResolver so
    // this lowering path cannot diverge from the rest of compiled-HIR lookup.
    const auto declared_type
        = [&]() -> std::optional<semantic::sv::TypeReference> {
        if (named_type_declaration && receiver_expression
            && receiver_expression->systemverilog != nullptr) {
            semantic::sv::TypeReference nominal;
            nominal.target.spelling
                = receiver_expression->systemverilog->text;
            nominal.target.source
                = receiver_expression->systemverilog->source;
            const auto resolved = semantic::CompiledDesignResolver {
                *specialized_hir_unit_, hir_generic_binding_frames_
            }.effective_systemverilog_type(
                nominal, receiver_expression->systemverilog->scope);
            if (resolved) {
                return resolved;
            }
        }
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *declaration->systemverilog;
        if (!record.type && !record.declared_type) {
            return std::nullopt;
        }
        auto result = record.type.value_or(
            semantic::sv::TypeReference { });
        if (record.declared_type) {
            result.target.target = *record.declared_type;
            if (result.target.spelling.empty()) {
                result.target.spelling = record.name;
            }
        }
        return result;
    }();
    // An unpacked aggregate type designator is not itself a container value.
    // ContainerType represents such values as boxed singletons so they can
    // use the common aggregate runtime.  Feeding that synthetic dimension to
    // a type query makes $bits(type_name) observe the box width instead of the
    // flattened declared type.  Keep aggregate type designators on the
    // TypeReference path while preserving ContainerType queries for array
    // typedefs and object expressions.
    const bool unpacked_aggregate_type_designator
        = named_type_declaration && declared_type
        && (declared_type->value_form
                == semantic::sv::TypeForm::unpacked_structure
            || declared_type->value_form
                == semantic::sv::TypeForm::unpacked_union)
        && !declared_type->container_form;
    const auto inferred_type
        = declared_type && !unpacked_aggregate_type_designator
        ? hir_systemverilog_container_type(*declared_type)
        : std::nullopt;
    const auto* type_pointer = selected_type
        ? &*selected_type
        : inferred_type ? &*inferred_type
                        : nullptr;
    if (type_pointer == nullptr) {
        const auto packed_width = declared_type
            ? hir_systemverilog_type_width(*declared_type)
            : hir_expression_width(receiver, hir_process_scope_);
        const auto* declared_type_pointer = declared_type
            ? &*declared_type
            : nullptr;
        if (!packed_width || *packed_width == 0U
            || declared_type_pointer == nullptr
            || declared_type_pointer->container_form) {
            return std::nullopt;
        }
        const auto requested = call.operands.size() == 2U
            ? hir_constant_integer(call.operands.back())
            : std::optional<std::int64_t> { 1 };
        if (!requested || *requested != 1) {
            return std::nullopt;
        }
        if (dimensions_query) {
            return 1;
        }
        if (unpacked_dimensions_query) {
            return 0;
        }
        if (bits_query) {
            return static_cast<std::int64_t>(*packed_width);
        }
        const auto effective_type
            = declared_type_pointer->target.target.valid()
            ? hir_systemverilog_type_parameter_binding(
                  declared_type_pointer->target.target)
            : std::nullopt;
        const auto* range = effective_type && effective_type->packed_range
            ? &*effective_type->packed_range
            : declared_type_pointer->packed_range
            ? &*declared_type_pointer->packed_range
            : nullptr;
        const auto boundary = [&](
                                  const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression_value)
            -> std::optional<std::int64_t> {
            if (value) {
                return value;
            }
            return expression_value
                ? specialized_hir_unit_->evaluate_integral_expression(
                      *expression_value)
                : std::nullopt;
        };
        const auto left = range
            ? boundary(range->left, range->left_expression)
            : std::optional<std::int64_t> {
                  static_cast<std::int64_t>(*packed_width - 1U) };
        const auto right = range
            ? boundary(range->right, range->right_expression)
            : std::optional<std::int64_t> { 0 };
        if (!left || !right) {
            return std::nullopt;
        }
        if (size_query) {
            return std::abs(*left - *right) + 1;
        }
        if (call.text == "$left") {
            return left;
        }
        if (call.text == "$right") {
            return right;
        }
        if (call.text == "$low") {
            return std::min(*left, *right);
        }
        if (call.text == "$high") {
            return std::max(*left, *right);
        }
        return *left >= *right ? 1 : -1;
    }
    const auto& type = *type_pointer;
    const auto rank = type.fixed && !type.dimensions.empty()
        ? type.dimensions.size()
        : 1U;
    const auto requested = call.operands.size() == 2U
        ? hir_constant_integer(call.operands.back())
        : std::optional<std::int64_t> { 1 };
    if (!requested || *requested < 1
        || static_cast<std::uint64_t>(*requested) > rank) {
        return std::nullopt;
    }
    if (dimensions_query) {
        const bool packed_element
            = type.element_kind == ContainerElementKind::Packed
            || type.element_kind == ContainerElementKind::Scalar;
        return static_cast<std::int64_t>(
            rank + (packed_element ? 1U : 0U));
    }
    if (unpacked_dimensions_query) {
        return static_cast<std::int64_t>(rank);
    }
    if (!type.fixed || type.dimensions.empty()) {
        return std::nullopt;
    }
    if (bits_query) {
        const auto total = hir_systemverilog_container_bit_width(type);
        if (!total
            || *total
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*total);
    }
    const auto& selected = type.dimensions[static_cast<std::size_t>(*requested - 1)];
    const auto left = static_cast<std::int64_t>(selected.first);
    const auto right = static_cast<std::int64_t>(selected.second);
    if (size_query) {
        return std::abs(left - right) + 1;
    }
    if (call.text == "$left") {
        return left;
    }
    if (call.text == "$right") {
        return right;
    }
    if (call.text == "$low") {
        return std::min(left, right);
    }
    if (call.text == "$high") {
        return std::max(left, right);
    }
    return left >= right ? 1 : -1;
}

std::optional<Lowerer::HirStringBinding>
Lowerer::hir_string_binding(
    const semantic::DeclarationId declaration_id,
    const semantic::ScopeId process_scope,
    const bool require_storage) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration) {
        return std::nullopt;
    }
    if (declaration->vhdl != nullptr) {
        const auto& source = *declaration->vhdl;
        const auto subtype = source.subtype
            ? hir_effective_vhdl_subtype(*source.subtype)
            : std::nullopt;
        if (!subtype
            || subtype->domain != semantic::vhdl::ValueDomain::string) {
            return std::nullopt;
        }
        const auto callable_formal = std::ranges::any_of(
            specialized_hir_unit_->design().semantics.declarations(),
            [&](const semantic::Declaration& candidate) {
                const auto owner = specialized_hir_unit_->find_declaration(
                    candidate.id);
                return owner && owner->vhdl != nullptr
                    && owner->vhdl->nested_scope == process_scope
                    && owner->vhdl->callable
                    && std::ranges::find(
                           owner->vhdl->callable->formals,
                           declaration_id)
                    != owner->vhdl->callable->formals.end();
            });
        const auto local = hir_local_string_registers_.contains(
                               declaration_id.value())
            || (source.scope == process_scope
                && (source.form
                        == semantic::vhdl::DeclarationForm::variable
                    || (source.form
                            == semantic::vhdl::DeclarationForm::port
                        && callable_formal)));
        if (!local || source.name.empty()) {
            return std::nullopt;
        }
        HirStringBinding result;
        result.declaration = declaration_id;
        result.kind = HirStringBindingKind::local;
        result.name = source.name;
        const auto found = hir_local_string_registers_.find(
            declaration_id.value());
        if (found != hir_local_string_registers_.end()) {
            result.local = found->second;
        } else if (require_storage) {
            return std::nullopt;
        }
        return result;
    }
    if (declaration->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *declaration->systemverilog;
    if (!source.type
        || source.type->value_form != semantic::sv::TypeForm::string) {
        return std::nullopt;
    }
    if (active_hir_callable_) {
        const auto& frame = hir_callable_frames_[*active_hir_callable_];
        const auto resolution
            = source.form == semantic::sv::DeclarationForm::function
            ? hir_callable_resolution(declaration_id)
            : std::nullopt;
        if (frame.function && frame.type.string
            && (declaration_id == frame.declaration
                || (resolution && resolution->body == frame.declaration))) {
            return HirStringBinding {
                declaration_id,
                HirStringBindingKind::local,
                source.name,
                frame.string_result,
                std::nullopt,
            };
        }
    }
    const auto callable_formal = std::ranges::any_of(
        specialized_hir_unit_->design().semantics.declarations(),
        [&](const semantic::Declaration& candidate) {
            const auto owner = specialized_hir_unit_->find_declaration(
                candidate.id);
            return owner && owner->systemverilog != nullptr
                && owner->systemverilog->nested_scope == process_scope
                && owner->systemverilog->callable
                && std::ranges::find(
                       owner->systemverilog->callable->formals,
                       declaration_id)
                != owner->systemverilog->callable->formals.end();
        });
    // Nested statement scopes retain the callable's declaration-keyed string
    // storage just as packed locals do.
    const auto local = hir_local_string_registers_.contains(
                           declaration_id.value())
        || (source.scope == process_scope
            && (source.form == semantic::sv::DeclarationForm::variable
                || (source.form == semantic::sv::DeclarationForm::port
                    && callable_formal)));
    HirStringBinding result;
    result.declaration = declaration_id;
    result.name = source.name;
    if (result.name.empty()) {
        return std::nullopt;
    }
    if (local) {
        result.kind = HirStringBindingKind::local;
        const auto found = hir_local_string_registers_.find(
            declaration_id.value());
        if (found != hir_local_string_registers_.end()) {
            result.local = found->second;
        } else if (require_storage) {
            return std::nullopt;
        }
        return result;
    }
    if (source.form != semantic::sv::DeclarationForm::port
        && source.form != semantic::sv::DeclarationForm::variable
        && source.form != semantic::sv::DeclarationForm::net) {
        return std::nullopt;
    }
    const auto found = string_objects_.find(result.name);
    if (found == string_objects_.end()) {
        return std::nullopt;
    }
    result.kind = HirStringBindingKind::object;
    result.object = found->second;
    return result;
}

bool Lowerer::hir_expression_is_string(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (const auto method = hir_systemverilog_enumeration_method(
            expression_id)) {
        return method->method == "name";
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_expression_is_string(*actual, process_scope);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return false;
        }
        const auto result = hir_expression_is_string(
            declaration->expression, process_scope);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return false;
    }
    if (expression->systemverilog != nullptr) {
        if (const auto member = hir_container_aggregate_selection(
                expression_id, true);
            member
            && member->leaf.element_kind == ContainerElementKind::String) {
            return true;
        }
        if (hir_class_property_profile(expression_id, true)) {
            return true;
        }
    }
    if (expression->vhdl != nullptr) {
        const auto& source = *expression->vhdl;
        const auto api = frontend::vhdl_simulator_api(source.text);
        const bool environment_string
            = api == frontend::VhdlSimulatorApi::getenv
            || api == frontend::VhdlSimulatorApi::vhdl_version
            || api == frontend::VhdlSimulatorApi::tool_type
            || api == frontend::VhdlSimulatorApi::tool_vendor
            || api == frontend::VhdlSimulatorApi::tool_name
            || api == frontend::VhdlSimulatorApi::tool_edition
            || api == frontend::VhdlSimulatorApi::tool_version
            || api == frontend::VhdlSimulatorApi::file_name
            || api == frontend::VhdlSimulatorApi::file_path
            || api == frontend::VhdlSimulatorApi::get_vhdl_assert_format;
        const bool dereferenced_getenv
            = source.kind == semantic::vhdl::ExpressionKind::call
            && (source.text == "@vhdl-member:all"
                || source.text == "@vhdl-dereference")
            && source.operands.size() == 1U
            && [&] {
                   const auto operand
                       = specialized_hir_unit_->find_expression(
                           source.operands.front());
                   return operand && operand->vhdl != nullptr
                       && frontend::vhdl_simulator_api(
                           operand->vhdl->text)
                           == frontend::VhdlSimulatorApi::getenv;
               }();
        const bool dereferenced_string_object
            = source.kind == semantic::vhdl::ExpressionKind::call
            && (source.text == "@vhdl-member:all"
                || source.text == "@vhdl-dereference")
            && source.operands.size() == 1U
            && [&] {
                   const auto declaration = hir_target_declaration(
                       source.operands.front());
                   return declaration
                       && hir_string_binding(
                           *declaration, process_scope, false);
               }();
        if (api == frontend::VhdlSimulatorApi::to_string
            || api == frontend::VhdlSimulatorApi::dir_separator
            || (api == frontend::VhdlSimulatorApi::dir_workingdir
                && source.operands.empty())
            || environment_string || dereferenced_getenv
            || dereferenced_string_object) {
            return true;
        }
        if (hir_vhdl_environment_directory_string_selection(
                expression_id)) {
            return true;
        }
        if (const auto selection
            = hir_vhdl_environment_call_path_member_selection(
                expression_id)) {
            return selection->member < 3U;
        }
        if (is_hir_vhdl_reflection_string_expression(expression_id)) {
            return true;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::string_literal) {
            return source.decoded_string.has_value();
        }
        if (source.kind == semantic::vhdl::ExpressionKind::call
            && source.text == "'image" && !source.operands.empty()
            && source.operands.size() <= 2U) {
            return true;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::call
            && vhdl_logic_string_function_name(source.text)) {
            return true;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::call) {
            const auto resolution = resolve_hir_vhdl_function_call(
                expression_id, process_scope, 0U);
            if (resolution) {
                const auto type = hir_callable_type(resolution->body);
                if (type && type->string) {
                    return true;
                }
            }
        }
        if (source.kind
                == semantic::vhdl::ExpressionKind::concatenation
            || (source.kind == semantic::vhdl::ExpressionKind::binary
                && source.text == "&")) {
            return !source.operands.empty()
                && std::ranges::all_of(
                    source.operands,
                    [&](const semantic::ExpressionId operand) {
                        return hir_expression_is_string(
                            operand, process_scope);
                    });
        }
        if (source.kind != semantic::vhdl::ExpressionKind::name) {
            return false;
        }
        const auto declaration = hir_referenced_declaration(expression_id);
        if (!declaration) {
            return false;
        }
        // A STRING generic or constant is a string even without a default
        // (its value comes from the actual, such as a `--generic`).
        if (const auto record
            = specialized_hir_unit_->find_declaration(*declaration);
            record && record->vhdl != nullptr && record->vhdl->subtype
            && record->vhdl->subtype->domain
                == semantic::vhdl::ValueDomain::string
            && (record->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_constant
                || record->vhdl->form
                    == semantic::vhdl::DeclarationForm::constant)) {
            return true;
        }
        if (const auto initializer = hir_constant_initializer(*declaration)) {
            return *initializer != expression_id
                && hir_expression_is_string(*initializer, process_scope);
        }
        return hir_string_binding(
            *declaration, process_scope, false)
            .has_value();
    }
    if (expression->systemverilog == nullptr) {
        return false;
    }
    if (const auto element = hir_container_element_binding(expression_id);
        element && element->selected_type != nullptr
        && element->selected_type->element_kind
            == ContainerElementKind::String) {
        return true;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::string_literal) {
        return source.decoded_string.has_value();
    }
    if (source.kind == semantic::sv::ExpressionKind::concatenation) {
        return !source.operands.empty()
            && std::ranges::all_of(
                source.operands,
                [&](const semantic::ExpressionId operand) {
                    return hir_expression_is_string(
                        operand, process_scope);
                });
    }
    if (source.kind == semantic::sv::ExpressionKind::replication
        && source.operands.size() >= 2U) {
        return std::all_of(
            source.operands.begin() + 1, source.operands.end(),
            [&](const semantic::ExpressionId operand) {
                return hir_expression_is_string(operand, process_scope);
            });
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "?:" && source.operands.size() == 3U) {
        return hir_expression_is_string(
                   source.operands[1], process_scope)
            && hir_expression_is_string(
                source.operands[2], process_scope);
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "$sformatf") {
        return true;
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && (source.text == ".toupper"
            || source.text == ".tolower"
            || source.text == ".substr")
        && !source.operands.empty()) {
        return hir_expression_is_string(
            source.operands.front(), process_scope);
    }
    if (source.kind == semantic::sv::ExpressionKind::call) {
        const auto declaration = hir_referenced_declaration(expression_id);
        const auto callable = declaration
            ? specialized_hir_unit_->find_declaration(*declaration)
            : std::nullopt;
        if (callable && callable->systemverilog != nullptr
            && callable->systemverilog->form
                == semantic::sv::DeclarationForm::function
            && callable->systemverilog->type
            && callable->systemverilog->type->value_form
                == semantic::sv::TypeForm::string) {
            return true;
        }
    }
    if (source.kind != semantic::sv::ExpressionKind::name) {
        return false;
    }
    const auto declaration = hir_referenced_declaration(expression_id);
    if (!declaration) {
        return false;
    }
    if (const auto initializer = hir_constant_initializer(*declaration)) {
        return *initializer != expression_id
            && hir_expression_is_string(*initializer, process_scope);
    }
    return hir_string_binding(
        *declaration, process_scope, false)
        .has_value();
}

bool Lowerer::can_lower_hir_string_expression(
    const semantic::ExpressionId expression,
    const semantic::ScopeId process_scope) const
{
    const auto source = specialized_hir_unit_->find_expression(expression);
    const bool typename_call = source
        && source->systemverilog != nullptr
        && source->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && source->systemverilog->text == "$typename";
    if (!typename_call
        && !hir_expression_is_string(expression, process_scope)) {
        return false;
    }
    if (!source) {
        return true;
    }
    if (source->vhdl != nullptr
        && source->vhdl->kind
            == semantic::vhdl::ExpressionKind::call) {
        const auto resolution = resolve_hir_vhdl_function_call(
            expression, process_scope, 0U);
        if (resolution) {
            const auto declaration = specialized_hir_unit_
                ->find_declaration(resolution->body);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->defined) {
                std::unordered_set<std::uint32_t> visiting;
                return can_lower_hir_function_call(
                    expression, process_scope, visiting, true);
            }
        }
        return true;
    }
    if (source->systemverilog == nullptr
        || source->systemverilog->kind
            != semantic::sv::ExpressionKind::call) {
        return true;
    }
    const auto& call = *source->systemverilog;
    if (call.text == "$typename") {
        return call.operands.size() == 1U;
    }
    if (call.text == "$sformatf") {
        return hir_string_format_expression_status(
                   expression, process_scope)
            == HirStringFormatStatus::valid;
    }
    if (call.text == ".toupper" || call.text == ".tolower") {
        return call.operands.size() == 1U
            && can_lower_hir_string_expression(
                call.operands.front(), process_scope);
    }
    if (call.text == ".substr") {
        if (call.operands.size() != 3U
            || !can_lower_hir_string_expression(
                call.operands.front(), process_scope)) {
            return false;
        }
        std::unordered_set<std::uint32_t> visiting;
        return can_lower_hir_expression(
                   call.operands[1], process_scope, visiting)
            && can_lower_hir_expression(
                call.operands[2], process_scope, visiting);
    }
    if (call.text != "?:") {
        const auto callable = hir_referenced_declaration(expression);
        if (!callable) {
            return true;
        }
        std::unordered_set<std::uint32_t> visiting;
        return can_lower_hir_function_call(
            expression, process_scope, visiting, true);
    }
    if (call.operands.size() != 3U) {
        return false;
    }
    std::unordered_set<std::uint32_t> visiting;
    return can_lower_hir_expression(
               call.operands[0],
               process_scope,
               visiting)
        && can_lower_hir_string_expression(
            call.operands[1], process_scope)
        && can_lower_hir_string_expression(
            call.operands[2], process_scope);
}

std::optional<Lowerer::HirVhdlAttributeProfile>
Lowerer::hir_vhdl_attribute_profile(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope,
    HirVhdlAttributeFailure* const failure) const
{
    const auto fail = [&](const HirVhdlAttributeFailure reason) {
        if (failure != nullptr) {
            *failure = reason;
        }
        return std::optional<HirVhdlAttributeProfile> { };
    };
    if (failure != nullptr) {
        *failure = HirVhdlAttributeFailure::none;
    }
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.operands.empty()) {
        return std::nullopt;
    }

    struct AttributePrefix {
        semantic::vhdl::SubtypeIndication subtype;
        const semantic::vhdl::TypeDefinition* definition { };
        std::optional<semantic::DeclarationId> declaration;
        bool type_prefix { };
    };
    const auto prefix_profile = [&]() -> std::optional<AttributePrefix> {
        const auto prefix = specialized_hir_unit_->find_expression(
            source.operands.front());
        const auto declaration_id = hir_referenced_declaration(
            source.operands.front());
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!prefix || prefix->vhdl == nullptr) {
            return std::nullopt;
        }
        if (!declaration || declaration->vhdl == nullptr) {
            const auto predefined = hir_vhdl_type_actual(
                source.operands.front());
            if (!predefined) {
                return std::nullopt;
            }
            const auto type = predefined->type_mark.target.valid()
                ? specialized_hir_unit_->find_type(
                      predefined->type_mark.target)
                : std::nullopt;
            return AttributePrefix {
                *predefined,
                type && type->vhdl != nullptr ? type->vhdl : nullptr,
                std::nullopt,
                true,
            };
        }
        const auto direct_type
            = declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::type
            || declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::subtype;
        auto subtype = declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : direct_type
            ? hir_vhdl_type_actual(source.operands.front())
            : std::nullopt;
        if (declaration_id) {
            const auto callable_range
                = hir_vhdl_callable_formal_range(*declaration_id);
            if (callable_range) {
                if (!*callable_range) {
                    return std::nullopt;
                }
                subtype = hir_vhdl_expression_subtype(
                    source.operands.front());
                if (!subtype) {
                    return std::nullopt;
                }
            }
        }
        auto type_id = declaration->vhdl->declared_type;
        if (!direct_type) {
            const auto separator = prefix->vhdl->text.find('.');
            if (separator != std::string::npos) {
                auto remaining
                    = std::string_view { prefix->vhdl->text }.substr(
                        separator + 1U);
                while (subtype && !remaining.empty()) {
                    if (!subtype->type_mark.target.valid()) {
                        return std::nullopt;
                    }
                    const auto record = specialized_hir_unit_->find_type(
                        subtype->type_mark.target);
                    if (!record || record->vhdl == nullptr
                        || record->vhdl->form
                            != semantic::vhdl::TypeForm::record) {
                        return std::nullopt;
                    }
                    const auto member_separator = remaining.find('.');
                    const auto member_name = remaining.substr(
                        0U, member_separator);
                    const auto member = std::ranges::find_if(
                        record->vhdl->record_elements,
                        [&](const semantic::vhdl::RecordElement& element) {
                            return same_hir_identifier(
                                element.name, member_name, true);
                        });
                    if (member
                        == record->vhdl->record_elements.end()) {
                        return std::nullopt;
                    }
                    subtype = hir_effective_vhdl_subtype(
                        member->subtype);
                    if (member_separator == std::string_view::npos) {
                        break;
                    }
                    remaining.remove_prefix(member_separator + 1U);
                }
                type_id = subtype && subtype->type_mark.target.valid()
                    ? std::optional { subtype->type_mark.target }
                    : std::nullopt;
            }
        }
        if (!subtype) {
            return std::nullopt;
        }
        if (!type_id && subtype->type_mark.target.valid()) {
            type_id = subtype->type_mark.target;
        }
        const auto type = type_id
            ? specialized_hir_unit_->find_type(*type_id)
            : std::nullopt;
        return AttributePrefix {
            *subtype,
            type && type->vhdl != nullptr ? type->vhdl : nullptr,
            declaration_id,
            direct_type,
        };
    }();
    if (!prefix_profile) {
        return std::nullopt;
    }

    const auto boundary = [&](const std::optional<std::int64_t> value,
                              const std::optional<semantic::ExpressionId>
                                  residual) {
        return value   ? value
            : residual ? hir_constant_integer(*residual)
                       : std::nullopt;
    };
    const auto concrete_range = [&](
                                    const semantic::vhdl::RangeConstraint&
                                        range)
        -> std::optional<semantic::vhdl::RangeConstraint> {
        auto result = range;
        result.left = boundary(range.left, range.left_expression);
        result.right = boundary(range.right, range.right_expression);
        if (result.direction_from_bounds && result.left && result.right) {
            result.descending = *result.left > *result.right;
            result.null = false;
        }
        return result.left && result.right
            ? std::optional { std::move(result) }
            : std::nullopt;
    };
    const auto range_length = [&](
                                  const semantic::vhdl::RangeConstraint&
                                      range) -> std::optional<std::int64_t> {
        if (!range.left || !range.right) {
            return std::nullopt;
        }
        const auto null_range = range.null
            || (!range.descending && *range.left > *range.right)
            || (range.descending && *range.left < *range.right);
        if (null_range) {
            return 0;
        }
        const auto distance = index_distance(*range.left, *range.right);
        if (distance
            >= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(distance + 1U);
    };

    const auto definition_for_subtype = [&]()
        -> const semantic::vhdl::TypeDefinition* {
        if (prefix_profile->subtype.type_mark.target.valid()) {
            const auto type = specialized_hir_unit_->find_type(
                prefix_profile->subtype.type_mark.target);
            if (type && type->vhdl != nullptr) {
                return type->vhdl;
            }
        }
        return prefix_profile->definition;
    };
    const auto* effective_definition = definition_for_subtype();

    const auto contextual_result_width = [&]()
        -> std::optional<std::size_t> {
        if (!active_hir_callable_ || !prefix_profile->declaration) {
            return std::nullopt;
        }
        const auto& frame = hir_callable_frames_[*active_hir_callable_];
        const auto callable = specialized_hir_unit_->find_declaration(
            frame.declaration);
        if (!callable || callable->vhdl == nullptr
            || !callable->vhdl->callable
            || !callable->vhdl->callable->return_identifier
            || frame.type.width == 0U) {
            return std::nullopt;
        }
        const auto return_identifier
            = *callable->vhdl->callable->return_identifier;
        const auto direct_result
            = return_identifier == *prefix_profile->declaration;
        const auto result_object = effective_definition != nullptr
            && return_identifier == effective_definition->declaration;
        if (!direct_result && !result_object) {
            return std::nullopt;
        }
        return frame.type.width;
    }();

    auto terminal_spelling = std::string_view {
        prefix_profile->subtype.type_mark.spelling
    };
    auto terminal_type = prefix_profile->subtype.type_mark.target;
    const semantic::vhdl::TypeDefinition* terminal_definition { };
    std::unordered_set<std::uint32_t> visited_terminal_types;
    while (terminal_type.valid()
        && visited_terminal_types.insert(terminal_type.value()).second) {
        const auto type = specialized_hir_unit_->find_type(terminal_type);
        if (!type || type->vhdl == nullptr) {
            break;
        }
        terminal_definition = type->vhdl;
        if (terminal_definition->form
                != semantic::vhdl::TypeForm::subtype
            && terminal_definition->form
                != semantic::vhdl::TypeForm::alias) {
            break;
        }
        terminal_spelling
            = terminal_definition->base.type_mark.spelling;
        terminal_type = terminal_definition->base.type_mark.target;
    }
    if (const auto separator = terminal_spelling.find_last_of(".:");
        separator != std::string_view::npos) {
        terminal_spelling.remove_prefix(separator + 1U);
    }
    const auto predefined_array = [&] {
        return same_hir_identifier(terminal_spelling, "bit_vector", true)
            || same_hir_identifier(
                terminal_spelling, "boolean_vector", true)
            || same_hir_identifier(terminal_spelling, "string", true)
            || same_hir_identifier(
                terminal_spelling, "std_logic_vector", true)
            || same_hir_identifier(
                terminal_spelling, "std_ulogic_vector", true)
            || same_hir_identifier(terminal_spelling, "signed", true)
            || same_hir_identifier(terminal_spelling, "unsigned", true);
    }();

    const auto subtype_array = std::ranges::any_of(
        prefix_profile->subtype.constraints,
        [](const semantic::vhdl::RangeConstraint& range) {
            return range.kind
                == semantic::vhdl::RangeKind::array_index;
        }) || predefined_array;
    const auto is_array = [&] {
        return (prefix_profile->definition
                   && prefix_profile->definition->form
                       == semantic::vhdl::TypeForm::array)
            || (effective_definition
                && effective_definition->form
                    == semantic::vhdl::TypeForm::array)
            || (terminal_definition
                && terminal_definition->form
                    == semantic::vhdl::TypeForm::array)
            || subtype_array;
    }();

    HirVhdlAttributeProfile result;
    if (is_array) {
        result.array_prefix = true;
        const auto* array = prefix_profile->definition
                && prefix_profile->definition->form
                    == semantic::vhdl::TypeForm::array
            ? prefix_profile->definition
            : effective_definition
                    && effective_definition->form
                        == semantic::vhdl::TypeForm::array
            ? effective_definition
            : terminal_definition
                    && terminal_definition->form
                        == semantic::vhdl::TypeForm::array
            ? terminal_definition
            : nullptr;
        const bool array_attribute = source.text == "'left"
            || source.text == "'right" || source.text == "'low"
            || source.text == "'high" || source.text == "'length"
            || source.text == "'ascending" || source.text == "'range"
            || source.text == "'reverse_range";
        if (!array_attribute || source.operands.size() > 2U) {
            return fail(HirVhdlAttributeFailure::array_prefix);
        }
        std::int64_t dimension { 1 };
        if (source.operands.size() == 2U) {
            const auto selected = hir_constant_integer(source.operands[1]);
            if (!selected) {
                return fail(HirVhdlAttributeFailure::array_dimension);
            }
            dimension = *selected;
        }
        const auto subtype_dimensions = std::ranges::count_if(
            prefix_profile->subtype.constraints,
            [](const semantic::vhdl::RangeConstraint& range) {
                return range.kind
                    == semantic::vhdl::RangeKind::array_index;
            });
        auto dimension_count = array != nullptr
            ? std::max(array->array_dimensions.size(),
                  static_cast<std::size_t>(subtype_dimensions))
            : static_cast<std::size_t>(subtype_dimensions);
        if (contextual_result_width) {
            dimension_count = std::max(
                dimension_count, std::size_t { 1U });
        }
        // An unconstrained STRING constant or generic takes its index range
        // from its static value: 1 to the value's length (IEEE 1076-2008
        // 5.3.2.2).
        std::optional<std::size_t> static_string_length;
        if (dimension_count == 0U && !source.operands.empty()) {
            auto text = specialized_hir_unit_->evaluate_string_expression(
                source.operands.front());
            if (!text) {
                if (const auto actual
                    = hir_generic_actual(source.operands.front())) {
                    text = specialized_hir_unit_->evaluate_string_expression(
                        *actual);
                }
            }
            if (text) {
                static_string_length = text->size();
                dimension_count = 1U;
            }
        }
        if (dimension <= 0
            || static_cast<std::uint64_t>(dimension)
                > std::numeric_limits<std::size_t>::max()
            || static_cast<std::uint64_t>(dimension)
                > dimension_count) {
            return fail(HirVhdlAttributeFailure::array_dimension);
        }
        const auto index = static_cast<std::size_t>(dimension - 1);
        std::optional<semantic::vhdl::RangeConstraint> range;
        if (index < prefix_profile->subtype.constraints.size()) {
            range = concrete_range(
                prefix_profile->subtype.constraints[index]);
        }
        if (!range && array != nullptr
            && index < array->array_dimensions.size()
            && array->array_dimensions[index].constraint) {
            range = concrete_range(
                *array->array_dimensions[index].constraint);
        }
        if (!range && static_string_length && index == 0U
            && *static_string_length
                <= static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            semantic::vhdl::RangeConstraint string_range;
            string_range.kind = semantic::vhdl::RangeKind::array_index;
            string_range.left = 1;
            string_range.right
                = static_cast<std::int64_t>(*static_string_length);
            string_range.descending = false;
            string_range.null = *static_string_length == 0U;
            range = std::move(string_range);
        }
        if (!range && contextual_result_width && index == 0U
            && *contextual_result_width - 1U
                <= static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            semantic::vhdl::RangeConstraint contextual_range;
            contextual_range.kind
                = semantic::vhdl::RangeKind::array_index;
            contextual_range.left = static_cast<std::int64_t>(
                *contextual_result_width - 1U);
            contextual_range.right = 0;
            contextual_range.descending = true;
            range = std::move(contextual_range);
        }
        if (!range) {
            return fail(HirVhdlAttributeFailure::array_prefix);
        }
        const auto left = *range->left;
        const auto right = *range->right;
        if (source.text == "'range"
            || source.text == "'reverse_range") {
            result.discrete_range = source.text == "'range"
                ? HirPackedRange { left, right, range->descending }
                : HirPackedRange { right, left, !range->descending };
            return result;
        }
        if (source.text == "'left") {
            result.constant = left;
        } else if (source.text == "'right") {
            result.constant = right;
        } else if (source.text == "'low") {
            result.constant = std::min(left, right);
        } else if (source.text == "'high") {
            result.constant = std::max(left, right);
        } else if (source.text == "'length") {
            result.constant = range_length(*range);
        } else if (source.text == "'ascending") {
            result.constant = range->descending ? 0 : 1;
            result.width = 1U;
            result.domain = frontend::ValueDomain::Boolean;
            return result;
        } else {
            return fail(HirVhdlAttributeFailure::array_prefix);
        }
        if (!result.constant) {
            return fail(HirVhdlAttributeFailure::array_prefix);
        }
        if (*result.constant < std::numeric_limits<std::int32_t>::min()
            || *result.constant
                > std::numeric_limits<std::int32_t>::max()) {
            return fail(HirVhdlAttributeFailure::array_result);
        }
        result.width = 32U;
        result.domain = frontend::ValueDomain::Integer;
        return result;
    }

    if (effective_definition
        && effective_definition->form
            == semantic::vhdl::TypeForm::record) {
        return fail(HirVhdlAttributeFailure::array_prefix);
    }
    const bool enumeration_prefix
        = (prefix_profile->definition
               && !prefix_profile->definition
                       ->enumeration_literals.empty())
        || (effective_definition
            && !effective_definition->enumeration_literals.empty())
        || (terminal_definition
            && !terminal_definition->enumeration_literals.empty());
    const bool implicit_object_value
        = !prefix_profile->type_prefix && enumeration_prefix
        && source.operands.size() == 1U
        && (source.text == "'pos" || source.text == "'succ"
            || source.text == "'pred" || source.text == "'leftof"
            || source.text == "'rightof");
    if ((!prefix_profile->type_prefix && !implicit_object_value)
        || source.operands.size() > 2U) {
        return fail(enumeration_prefix
                ? HirVhdlAttributeFailure::enumeration_profile
                : HirVhdlAttributeFailure::scalar_profile);
    }
    std::optional<semantic::vhdl::RangeConstraint> scalar_range;
    const auto constraint = std::ranges::find_if(
        prefix_profile->subtype.constraints,
        [](const semantic::vhdl::RangeConstraint& range) {
            return range.kind == semantic::vhdl::RangeKind::integer
                || range.kind == semantic::vhdl::RangeKind::enumeration
                || range.kind == semantic::vhdl::RangeKind::discrete;
        });
    if (constraint != prefix_profile->subtype.constraints.end()) {
        scalar_range = concrete_range(*constraint);
    }
    if (!scalar_range && prefix_profile->definition
        && prefix_profile->definition->scalar_range) {
        scalar_range = concrete_range(
            *prefix_profile->definition->scalar_range);
    }
    if (!scalar_range && effective_definition
        && effective_definition->scalar_range) {
        scalar_range = concrete_range(
            *effective_definition->scalar_range);
    }
    if (!scalar_range && terminal_definition
        && terminal_definition->scalar_range) {
        scalar_range = concrete_range(
            *terminal_definition->scalar_range);
    }
    if (!scalar_range
        && (same_hir_identifier(
                prefix_profile->subtype.type_mark.spelling, "bit", true)
            || same_hir_identifier(
                prefix_profile->subtype.type_mark.spelling,
                "boolean", true))) {
        scalar_range = semantic::vhdl::RangeConstraint {
            semantic::vhdl::RangeKind::enumeration,
            0,
            1,
            std::nullopt,
            std::nullopt,
            false,
            false,
            { },
        };
    }
    if (!scalar_range
        && (same_hir_identifier(terminal_spelling, "character", true)
            || same_hir_identifier(
                terminal_spelling, "severity_level", true))) {
        // Predefined enumerations without a HIR definition (IEEE 1076-2008
        // 16.3): CHARACTER has 256 positions, SEVERITY_LEVEL four.
        scalar_range = semantic::vhdl::RangeConstraint {
            semantic::vhdl::RangeKind::enumeration,
            0,
            same_hir_identifier(terminal_spelling, "character", true)
                ? 255
                : 3,
            std::nullopt,
            std::nullopt,
            false,
            false,
            { },
        };
    }
    if (!scalar_range) {
        const bool integer = same_hir_identifier(
            terminal_spelling, "integer", true);
        const bool natural = same_hir_identifier(
            terminal_spelling, "natural", true);
        const bool positive = same_hir_identifier(
            terminal_spelling, "positive", true);
        if (integer || natural || positive) {
            const auto first = integer
                ? vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                    ? std::numeric_limits<std::int64_t>::min()
                    : static_cast<std::int64_t>(
                          std::numeric_limits<std::int32_t>::min())
                : natural ? 0 : 1;
            const auto last
                = vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(
                      std::numeric_limits<std::int32_t>::max());
            scalar_range = semantic::vhdl::RangeConstraint {
                semantic::vhdl::RangeKind::integer,
                first,
                last,
                std::nullopt,
                std::nullopt,
                false,
                false,
                { },
            };
        }
    }
    const auto* enumeration = prefix_profile->definition
            && !prefix_profile->definition->enumeration_literals.empty()
        ? prefix_profile->definition
        : effective_definition
            && !effective_definition->enumeration_literals.empty()
        ? effective_definition
        : terminal_definition
            && !terminal_definition->enumeration_literals.empty()
        ? terminal_definition
        : nullptr;
    if (!scalar_range && enumeration != nullptr) {
        if (enumeration->enumeration_literals.empty()
            || enumeration->enumeration_literals.size() - 1U
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        scalar_range = semantic::vhdl::RangeConstraint {
            semantic::vhdl::RangeKind::enumeration,
            0,
            static_cast<std::int64_t>(
                enumeration->enumeration_literals.size() - 1U),
            std::nullopt,
            std::nullopt,
            false,
            false,
            { },
        };
    }
    if (!scalar_range || !scalar_range->left || !scalar_range->right) {
        return fail(enumeration_prefix
                ? HirVhdlAttributeFailure::enumeration_profile
                : HirVhdlAttributeFailure::scalar_profile);
    }
    const auto left = *scalar_range->left;
    const auto right = *scalar_range->right;
    const auto low = std::min(left, right);
    const auto high = std::max(left, right);
    if (source.text == "'range"
        || source.text == "'reverse_range") {
        result.discrete_range = source.text == "'range"
            ? HirPackedRange { left, right, scalar_range->descending }
            : HirPackedRange {
                  right, left, !scalar_range->descending
              };
        return result;
    }
    const auto prefix_width = vhdl_runtime_width(prefix_profile->subtype);
    const auto prefix_domain = vhdl_domain(prefix_profile->subtype.domain);
    if (!prefix_width || *prefix_width == 0U
        || !scalar_domain(prefix_domain)) {
        return fail(enumeration_prefix
                ? HirVhdlAttributeFailure::enumeration_profile
                : HirVhdlAttributeFailure::scalar_profile);
    }
    if (source.operands.size() == 1U && !implicit_object_value) {
        if (source.text == "'left") {
            result.constant = left;
        } else if (source.text == "'right") {
            result.constant = right;
        } else if (source.text == "'low") {
            result.constant = low;
        } else if (source.text == "'high") {
            result.constant = high;
        } else if (source.text == "'length") {
            result.constant = range_length(*scalar_range);
            result.width = 32U;
            result.domain = frontend::ValueDomain::Integer;
            return result.constant ? std::optional { result }
                                   : std::nullopt;
        } else if (source.text == "'ascending") {
            result.constant = scalar_range->descending ? 0 : 1;
            result.width = 1U;
            result.domain = frontend::ValueDomain::Boolean;
            return result;
        } else {
            return fail(HirVhdlAttributeFailure::scalar_profile);
        }
        result.width = *prefix_width;
        result.domain = prefix_domain;
        return result;
    }

    const auto value = implicit_object_value
        ? source.operands.front()
        : source.operands.back();
    const auto value_width = hir_expression_width(value, process_scope);
    const auto value_domain = hir_expression_domain(value, process_scope);
    const auto value_expression
        = specialized_hir_unit_->find_expression(value);
    auto value_spelling = value_expression
            && value_expression->vhdl != nullptr
        ? std::string_view { value_expression->vhdl->text }
        : std::string_view { };
    if (const auto separator = value_spelling.find_last_of(".:");
        separator != std::string_view::npos) {
        value_spelling.remove_prefix(separator + 1U);
    }
    const auto contextual_enumeration_literal
        = enumeration != nullptr
        && std::ranges::any_of(
            enumeration->enumeration_literals,
            [&](const auto& literal) {
                if (value_spelling.starts_with('\'')) {
                    return literal.spelling == value_spelling;
                }
                return std::ranges::equal(
                    literal.spelling, value_spelling,
                    [](const char left_character,
                        const char right_character) {
                        return std::tolower(
                                   static_cast<unsigned char>(
                                       left_character))
                            == std::tolower(
                                static_cast<unsigned char>(
                                    right_character));
                    });
            });
    bool contextual_enumeration_attribute { };
    if ((!value_width || *value_width == 0U || !value_domain
            || !scalar_domain(*value_domain))
        && !contextual_enumeration_literal) {
        return fail(enumeration_prefix
                ? HirVhdlAttributeFailure::enumeration_profile
                : HirVhdlAttributeFailure::scalar_profile);
    }
    if (enumeration != nullptr) {
        const auto enumeration_root = [&](const semantic::vhdl::SubtypeIndication& subtype)
            -> std::optional<semantic::TypeId> {
            auto type_id = subtype.type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()
                && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (!type->vhdl->enumeration_literals.empty()) {
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
        if (source.text == "'val") {
            if (*value_domain != frontend::ValueDomain::Integer) {
                return fail(
                    HirVhdlAttributeFailure::enumeration_profile);
            }
        } else {
            const auto value_subtype = hir_vhdl_expression_subtype(value);
            auto value_root = value_subtype
                ? enumeration_root(*value_subtype)
                : std::nullopt;
            if (!value_root || *value_root != enumeration->id) {
                if (const auto loop_root
                    = hir_vhdl_loop_parameter_enumeration(value);
                    loop_root && *loop_root == enumeration->id) {
                    value_root = loop_root;
                }
            }
            if ((!value_root || *value_root != enumeration->id)
                && value_expression
                && value_expression->vhdl != nullptr
                && value_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && !value_expression->vhdl->operands.empty()) {
                const auto nested_attribute
                    = value_expression->vhdl->text;
                const auto enumeration_valued
                    = nested_attribute == "'left"
                    || nested_attribute == "'right"
                    || nested_attribute == "'low"
                    || nested_attribute == "'high"
                    || nested_attribute == "'val"
                    || nested_attribute == "'succ"
                    || nested_attribute == "'pred"
                    || nested_attribute == "'leftof"
                    || nested_attribute == "'rightof";
                const auto nested_prefix = enumeration_valued
                    ? hir_vhdl_type_actual(
                          value_expression->vhdl->operands.front())
                    : std::nullopt;
                const auto nested_root = nested_prefix
                    ? enumeration_root(*nested_prefix)
                    : std::nullopt;
                if (nested_root && *nested_root == enumeration->id) {
                    value_root = nested_root;
                    contextual_enumeration_attribute = true;
                }
            }
            if ((!value_root || *value_root != enumeration->id)
                && !contextual_enumeration_literal) {
                return fail(
                    HirVhdlAttributeFailure::enumeration_profile);
            }
        }
    }
    result.value = value;
    result.value_width = contextual_enumeration_literal
            || contextual_enumeration_attribute
        ? *prefix_width
        : *value_width;
    result.lower_bound = low;
    result.upper_bound = high;
    if (source.text == "'pos") {
        result.kind = HirVhdlAttributeKind::position;
        result.width = 32U;
        result.domain = frontend::ValueDomain::Integer;
    } else if (source.text == "'val") {
        result.kind = HirVhdlAttributeKind::value;
    } else if (source.text == "'succ"
        || source.text == "'rightof") {
        result.kind = scalar_range->descending
                && source.text == "'rightof"
            ? HirVhdlAttributeKind::predecessor
            : HirVhdlAttributeKind::successor;
    } else if (source.text == "'pred"
        || source.text == "'leftof") {
        result.kind = scalar_range->descending
                && source.text == "'leftof"
            ? HirVhdlAttributeKind::successor
            : HirVhdlAttributeKind::predecessor;
    } else {
        return fail(enumeration_prefix
                ? HirVhdlAttributeFailure::enumeration_profile
                : HirVhdlAttributeFailure::scalar_profile);
    }
    if (const auto constant = hir_constant_integer(value)) {
        const bool inside = *constant >= low && *constant <= high;
        const bool valid = result.kind
                == HirVhdlAttributeKind::successor
            ? inside && *constant < high
            : result.kind == HirVhdlAttributeKind::predecessor
            ? inside && *constant > low
            : inside;
        if (!valid) {
            return fail(enumeration_prefix
                    ? HirVhdlAttributeFailure::enumeration_value
                    : HirVhdlAttributeFailure::scalar_value);
        }
    }
    if (result.kind != HirVhdlAttributeKind::position) {
        result.width = *prefix_width;
        result.domain = prefix_domain;
    }
    return result;
}

std::optional<Lowerer::HirVhdlSignalAttributeProfile>
Lowerer::hir_vhdl_signal_attribute_profile(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto timed = source.text == "'stable"
        || source.text == "'quiet" || source.text == "'delayed";
    if (source.operands.empty() || source.operands.size() > 2U
        || (!timed && source.operands.size() != 1U)) {
        return std::nullopt;
    }
    const auto declaration = hir_referenced_declaration(
        source.operands.front());
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, process_scope, false)
        : std::nullopt;
    if (!binding || binding->kind != HirRuntimeBindingKind::signal
        || !binding->signal
        || *binding->signal >= design_.signal_info_.size()) {
        return std::nullopt;
    }

    runtime::SimulationTick duration { };
    if (source.operands.size() == 2U) {
        auto magnitude = hir_constant_integer(source.operands.back());
        const auto physical = specialized_hir_unit_->find_expression(
            source.operands.back());
        if (!magnitude && physical && physical->vhdl != nullptr
            && physical->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && physical->vhdl->text.starts_with("@vhdl-physical:")
            && physical->vhdl->operands.size() == 1U) {
            magnitude = hir_constant_integer(
                physical->vhdl->operands.front());
        }
        if (!magnitude || *magnitude < 0) {
            return std::nullopt;
        }
        duration = static_cast<runtime::SimulationTick>(*magnitude);
    }

    HirVhdlSignalAttributeProfile result;
    result.signal = *binding->signal;
    result.duration = duration;
    result.width = 1U;
    result.domain = frontend::ValueDomain::Boolean;
    if (source.text == "'event") {
        result.kind = HirVhdlSignalAttributeKind::event;
    } else if (source.text == "'last_value") {
        result.kind = HirVhdlSignalAttributeKind::last_value;
        result.width = binding->width;
        result.domain = binding->domain;
    } else if (source.text == "'last_event") {
        result.kind = HirVhdlSignalAttributeKind::last_event;
        result.width = 64U;
        result.domain = frontend::ValueDomain::Bit2;
    } else if (source.text == "'last_active") {
        result.kind = HirVhdlSignalAttributeKind::last_active;
        result.width = 64U;
        result.domain = frontend::ValueDomain::Bit2;
    } else if (source.text == "'driving") {
        result.kind = HirVhdlSignalAttributeKind::driving;
    } else if (source.text == "'driving_value") {
        result.kind = HirVhdlSignalAttributeKind::driving_value;
        result.width = binding->width;
        result.domain = binding->domain;
    } else if (source.text == "'stable") {
        result.kind = HirVhdlSignalAttributeKind::stable;
    } else if (source.text == "'quiet") {
        result.kind = HirVhdlSignalAttributeKind::quiet;
    } else if (source.text == "'active") {
        result.kind = HirVhdlSignalAttributeKind::active;
    } else if (source.text == "'transaction") {
        result.kind = HirVhdlSignalAttributeKind::transaction;
    } else if (source.text == "'delayed") {
        result.kind = HirVhdlSignalAttributeKind::delayed;
        result.width = binding->width;
        result.domain = binding->domain;
    } else {
        return std::nullopt;
    }
    return result;
}

bool Lowerer::is_hir_vhdl_vital_mux2(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_vital_expression_profile(expression_id)) {
        return false;
    }
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call) {
        return false;
    }
    auto name = std::string_view { expression->vhdl->text };
    const auto separator = name.find_last_of('.');
    if (separator != std::string_view::npos) {
        name.remove_prefix(separator + 1U);
    }
    return same_hir_identifier(name, "vitalmux2", true);
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_original_integer_generic_subtype(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name
        || !expression->vhdl->referenced_name
        || !expression->vhdl->referenced_name->selected) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        *expression->vhdl->referenced_name->selected);
    if (!declaration || declaration->vhdl == nullptr
        || declaration->vhdl->form
            != semantic::vhdl::DeclarationForm::generic_constant
        || !declaration->vhdl->subtype) {
        return std::nullopt;
    }
    const auto subtype = hir_effective_vhdl_subtype(
        *declaration->vhdl->subtype);
    return subtype
            && subtype->domain == semantic::vhdl::ValueDomain::integer
        ? subtype
        : std::nullopt;
}

std::optional<std::int64_t> Lowerer::hir_vhdl_generate_iterator_value(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr
        || specialized_hir_unit_->language() != semantic::Language::vhdl
        || !hir_vhdl_generate_iterator_profile_frames_.insert(
            expression_id.value()).second) {
        return std::nullopt;
    }
    struct ProfileFrameGuard {
        std::unordered_set<std::uint32_t>& frames;
        std::uint32_t expression;

        ~ProfileFrameGuard() { frames.erase(expression); }
    } guard {
        hir_vhdl_generate_iterator_profile_frames_, expression_id.value()
    };

    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name
        || expression->vhdl->text.empty()
        || (expression->vhdl->referenced_name
            && expression->vhdl->referenced_name->selected)
        || hir_referenced_declaration(expression_id)) {
        return std::nullopt;
    }

    const auto& hierarchy_identities
        = specialized_hir_unit_->specialization().hierarchy_identities;
    const semantic::SpecializedHirNamedIdentity* identity { };
    for (const auto& candidate : hierarchy_identities) {
        if (!same_hir_identifier(
                candidate.name, expression->vhdl->text, true)) {
            continue;
        }
        if (identity != nullptr || candidate.identity.empty()) {
            return std::nullopt;
        }
        identity = &candidate;
    }
    if (identity == nullptr) {
        return std::nullopt;
    }
    std::int64_t identity_value { };
    const auto [identity_end, identity_error] = std::from_chars(
        identity->identity.data(),
        identity->identity.data() + identity->identity.size(),
        identity_value);
    if (identity_error != std::errc { }
        || identity_end
            != identity->identity.data() + identity->identity.size()) {
        return std::nullopt;
    }

    // A VHDL generate parameter has no declaration record. Tie it instead
    // to one lexical loop region and that occurrence's active identity.
    const auto unit = specialized_hir_unit_->design().find_unit(
        specialized_hir_unit_->unit());
    if (!unit || unit->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& scopes = specialized_hir_unit_->design().semantics.scopes();
    const auto scope_within = [&](semantic::ScopeId scope,
                                  const semantic::ScopeId owner) {
        std::set<semantic::ScopeId> visited;
        while (scope.valid() && scope.value() < scopes.size()
            && visited.insert(scope).second) {
            if (scope == owner) {
                return true;
            }
            const auto parent = scopes[scope.value()].parent;
            if (!parent) {
                return false;
            }
            scope = *parent;
        }
        return false;
    };
    const auto iterator_name = [&](const semantic::ExpressionId candidate_id) {
        const auto candidate = specialized_hir_unit_->find_expression(
            candidate_id);
        return candidate && candidate->vhdl != nullptr
            && candidate->vhdl->kind
                == semantic::vhdl::ExpressionKind::name
            && same_hir_identifier(
                candidate->vhdl->text, expression->vhdl->text, true)
            && (!candidate->vhdl->referenced_name
                || !candidate->vhdl->referenced_name->selected)
            && !hir_referenced_declaration(candidate_id);
    };
    const auto integer_constant = [&](const semantic::ExpressionId candidate) {
        const auto domain = hir_expression_domain(candidate, process_scope);
        return domain && *domain == frontend::ValueDomain::Integer
            && hir_constant_integer(candidate).has_value();
    };
    const semantic::vhdl::GenerateRegion* matched_region { };
    const auto find_region = [&](const auto& self,
                                 const semantic::vhdl::GenerateRegion& region)
        -> bool {
        if (region.kind == semantic::vhdl::GenerateKind::iterative
            && region.declaration.valid()
            && same_hir_identifier(
                region.iterator, expression->vhdl->text, true)
            && region.initial && region.condition && region.iteration
            && scope_within(expression->vhdl->scope, region.scope)) {
            if (matched_region != nullptr) {
                return false;
            }
            matched_region = &region;
        }
        for (const auto& nested : region.nested) {
            if (!self(self, nested)) {
                return false;
            }
        }
        return true;
    };
    for (const auto& region : unit->vhdl->generates) {
        if (!find_region(find_region, region)) {
            return std::nullopt;
        }
    }
    if (matched_region == nullptr
        || !integer_constant(*matched_region->initial)) {
        return std::nullopt;
    }
    const auto condition = specialized_hir_unit_->find_expression(
        *matched_region->condition);
    if (!condition || condition->vhdl == nullptr
        || condition->vhdl->kind
            != semantic::vhdl::ExpressionKind::binary
        || (condition->vhdl->text != "<="
            && condition->vhdl->text != ">=")
        || condition->vhdl->operands.size() != 2U
        || !iterator_name(condition->vhdl->operands.front())
        || !integer_constant(condition->vhdl->operands.back())) {
        return std::nullopt;
    }
    const auto iteration = specialized_hir_unit_->find_expression(
        *matched_region->iteration);
    if (!iteration || iteration->vhdl == nullptr
        || iteration->vhdl->kind
            != semantic::vhdl::ExpressionKind::binary
        || (iteration->vhdl->text != "+"
            && iteration->vhdl->text != "-")
        || iteration->vhdl->operands.size() != 2U
        || !iterator_name(iteration->vhdl->operands.front())
        || !integer_constant(iteration->vhdl->operands.back())
        || hir_constant_integer(iteration->vhdl->operands.back())
            != std::optional<std::int64_t> { 1 }) {
        return std::nullopt;
    }
    const auto value = hir_constant_integer(expression_id);
    if (!value || *value != identity_value) {
        return std::nullopt;
    }
    return value;
}

std::optional<frontend::ValueDomain> Lowerer::hir_expression_domain(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (hir_vhdl_now_expression(expression_id)) {
        return frontend::ValueDomain::Integer;
    }
    if (const auto method = hir_systemverilog_enumeration_method(
            expression_id)) {
        return method->method == "name" ? frontend::ValueDomain::String
            : method->method == "num" || !method->four_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
    }
    if (hir_vhdl_function_name(expression_id)) {
        const auto resolution = resolve_hir_vhdl_function_call(
            expression_id, process_scope, 0U);
        const auto type = resolution
            ? hir_callable_type(resolution->body, 0U)
            : std::nullopt;
        if (type && !type->string) {
            return type->domain;
        }
    }
    // An operand literal can leave the overload open (std_match on a string
    // literal); the call is still BOOLEAN when every candidate returns
    // BOOLEAN.
    if (const auto expression
        = specialized_hir_unit_->find_expression(expression_id);
        expression && expression->vhdl != nullptr
        && expression->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->referenced_name
        && !expression->vhdl->operands.empty()) {
        const auto candidates = hir_vhdl_callable_resolutions(
            *expression->vhdl->referenced_name, expression->vhdl->scope);
        const auto boolean = !candidates.empty()
            && std::ranges::all_of(candidates, [&](const auto& candidate) {
                   const auto candidate_type
                       = hir_callable_type(candidate.body, 0U);
                   return candidate_type && !candidate_type->string
                       && candidate_type->domain
                           == frontend::ValueDomain::Boolean;
               });
        if (boolean) {
            return frontend::ValueDomain::Boolean;
        }
    }
    if (const auto binding = hir_case_pattern_binding(expression_id)) {
        return binding->domain;
    }
    if (hir_vhdl_original_integer_generic_subtype(expression_id)) {
        return frontend::ValueDomain::Integer;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return hir_expression_domain(*actual, process_scope);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_expression_domain(*actual, process_scope);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = hir_expression_domain(
            declaration->expression, process_scope);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "@vhdl-null") {
        return frontend::ValueDomain::Bit2;
    }
    if (hir_vhdl_character_literal_code(expression_id, 0U)) {
        return frontend::ValueDomain::Bit2;
    }
    if (hir_vhdl_generate_iterator_value(
            expression_id, process_scope)) {
        return frontend::ValueDomain::Integer;
    }
    if (const auto signal = hir_direct_signal_binding(expression_id)) {
        return signal->domain;
    }
    if (hir_systemverilog_interface_handle(expression_id)) {
        return frontend::ValueDomain::Bit2;
    }
    if (const auto conversion = hir_vhdl_conversion_profile(expression_id)) {
        return conversion->domain;
    }
    if (const auto profile = hir_vhdl_float_function_profile(expression_id)) {
        return profile->domain;
    }
    if (const auto profile = hir_vhdl_fixed_function_profile(expression_id)) {
        return profile->domain;
    }
    if (const auto standard = hir_vhdl_standard_function_profile(
            expression_id)) {
        return standard->domain;
    }
    if (const auto vital = hir_vhdl_vital_expression_profile(
            expression_id)) {
        return vital->domain;
    }
    if (const auto api = hir_vhdl_assert_expression_api(expression_id)) {
        using Api = frontend::VhdlSimulatorApi;
        if (*api == Api::is_vhdl_assert_failed
            || *api == Api::get_vhdl_assert_enable) {
            return frontend::ValueDomain::Boolean;
        }
        if (*api == Api::get_vhdl_assert_count) {
            return frontend::ValueDomain::Integer;
        }
        if (*api == Api::get_vhdl_read_severity) {
            return frontend::ValueDomain::Bit2;
        }
    }
    if (hir_vhdl_standard_enumeration_literal(expression_id)) {
        return frontend::ValueDomain::Bit2;
    }
    if (is_hir_vhdl_environment_status_literal(expression_id)) {
        return frontend::ValueDomain::Bit2;
    }
    if (is_hir_vhdl_file_call(expression_id)) {
        return frontend::ValueDomain::Boolean;
    }
    if (const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
        selection && selection->member == 3U) {
        return frontend::ValueDomain::Integer;
    }
    if (is_hir_vhdl_environment_expression(expression_id)
        && expression->vhdl != nullptr) {
        using Api = frontend::VhdlSimulatorApi;
        const auto api = frontend::vhdl_simulator_api(
            expression->vhdl->text);
        if (api == Api::file_line) {
            return frontend::ValueDomain::Integer;
        }
        if (api == Api::dir_itemexists || api == Api::dir_itemisdir
            || api == Api::dir_itemisfile) {
            return frontend::ValueDomain::Boolean;
        }
        if (api == Api::dir_open || api == Api::dir_workingdir
            || api == Api::dir_createdir || api == Api::dir_deletedir
            || api == Api::dir_deletefile) {
            return frontend::ValueDomain::Bit2;
        }
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && (expression->vhdl->text == "rising_edge"
            || expression->vhdl->text == "falling_edge"
            || expression->vhdl->text == "is_x")
        && expression->vhdl->operands.size() == 1U) {
        return frontend::ValueDomain::Boolean;
    }
    if (const auto attribute = hir_vhdl_signal_attribute_profile(
            expression_id, process_scope)) {
        return attribute->domain;
    }
    if (is_hir_vhdl_vital_mux2(expression_id)) {
        return frontend::ValueDomain::Logic9;
    }
    if (const auto attribute = hir_vhdl_attribute_profile(
            expression_id, process_scope)) {
        return attribute->domain;
    }
    if (const auto member = hir_systemverilog_member_selection(
            expression_id)) {
        return member->domain;
    }
    if (const auto member = hir_container_aggregate_selection(
            expression_id)) {
        return member->leaf.two_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
    }
    if (const auto aggregate = hir_packed_container_aggregate_profile(
            expression_id)) {
        return aggregate->domain;
    }
    if (const auto selection = hir_vhdl_array_selection(expression_id)) {
        return selection->domain;
    }
    if (const auto member = hir_vhdl_member_selection(expression_id)) {
        return member->domain;
    }
    if (const auto element = hir_container_element_binding(expression_id)) {
        return element->domain;
    }
    if (const auto selected = hir_referenced_declaration(expression_id)) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *selected);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            const auto subtype = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            if (subtype) {
                const auto domain = vhdl_domain(subtype->domain);
                if (domain != frontend::ValueDomain::Unknown) {
                    return domain;
                }
            }
        }
        if (const auto binding = hir_runtime_binding(
                *selected, process_scope, false)) {
            return binding->domain;
        }
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->type) {
            const auto type = specialized_hir_unit_->find_type(
                declaration->systemverilog->type->target.target);
            if (type && type->systemverilog != nullptr
                && type->systemverilog->form
                    == semantic::sv::TypeForm::enumeration) {
                if (!hir_systemverilog_type_width(
                        *declaration->systemverilog->type)) {
                    return std::nullopt;
                }
                return hir_systemverilog_type_four_state(
                           *declaration->systemverilog->type)
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
            }
        }
        const auto initializer = hir_constant_initializer(*selected);
        // An x or z valued parameter has no integer value but still has
        // its declared or initializer type.
        const auto systemverilog_value_parameter = declaration
            && declaration->systemverilog != nullptr
            && (declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::parameter
                || declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::local_parameter);
        if (initializer
            && (systemverilog_value_parameter
                || hir_constant_integer(expression_id))) {
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->type
                && declaration->systemverilog->type->target.spelling
                    != "implicit"
                && hir_systemverilog_type_width(
                    *declaration->systemverilog->type)) {
                return hir_systemverilog_type_four_state(
                           *declaration->systemverilog->type)
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
            }
            if (const auto inferred = hir_expression_domain(
                    *initializer, process_scope)) {
                return inferred;
            }
        }
    }
    std::optional<semantic::DeclarationId> selected;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        if (source.kind == semantic::sv::ExpressionKind::name
            && !hir_referenced_declaration(expression_id)
            && systemverilog_genvar_identity(
                *specialized_hir_unit_, source.scope, source.text)) {
            // Generate variables are implicit signed 32-bit SystemVerilog
            // integers. Their active value is retained in specialization
            // hierarchy identities, not in declaration HIR.
            return frontend::ValueDomain::Integer;
        }
        constexpr auto container_index_prefix
            = std::string_view { "@sv-container-index:" };
        constexpr auto container_method_prefix
            = std::string_view { "@sv-container-method:" };
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "inside") {
            return frontend::ValueDomain::Logic4;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$isunknown"
                || source.text == "$onehot"
                || source.text == "$onehot0"
                || source.text == "$countones"
                || source.text == "$countbits")) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && systemverilog_sampled_value_call(source.text)) {
            if (!systemverilog_sampled_value_preserves_signal(
                    source.text)) {
                return frontend::ValueDomain::Bit2;
            }
            return source.operands.empty()
                ? std::optional { frontend::ValueDomain::Logic4 }
                : hir_expression_domain(
                      source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && is_hir_systemverilog_file_call(expression_id)) {
            return frontend::ValueDomain::Bit2;
        }
        if (hir_systemverilog_math_function(expression_id)) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_index_prefix)) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_method_prefix)) {
            const auto operation = std::string_view { source.text }.substr(
                container_method_prefix.size());
            if (operation.starts_with("pop_front:")) {
                return frontend::ValueDomain::Bit2;
            }
            if (operation.starts_with("size:")) {
                return frontend::ValueDomain::Integer;
            }
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "@stream-left"
                || source.text == "@stream-right")
            && source.operands.size() >= 2U) {
            auto result = frontend::ValueDomain::Bit2;
            for (std::size_t index = 1U;
                index < source.operands.size(); ++index) {
                const auto domain = hir_expression_domain(
                    source.operands[index], process_scope);
                if (!domain
                    || (*domain != frontend::ValueDomain::Bit2
                        && *domain != frontend::ValueDomain::Logic4)) {
                    return std::nullopt;
                }
                if (*domain == frontend::ValueDomain::Logic4) {
                    result = frontend::ValueDomain::Logic4;
                }
            }
            return result;
        }
        if (const auto cast = hir_systemverilog_cast_profile(
                expression_id)) {
            return cast->domain;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$signed" || source.text == "$unsigned")
            && source.operands.size() == 1U) {
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::name
            && source.text == "this"
            && hir_class_receiver_register_) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_property
            || source.kind
                == semantic::sv::ExpressionKind::class_static_property) {
            const auto profile = hir_class_property_profile(expression_id);
            return profile ? std::optional { profile->domain }
                           : std::nullopt;
        }
        if (source.kind
                == semantic::sv::ExpressionKind::class_method_call
            || source.kind
                == semantic::sv::ExpressionKind::class_static_method_call) {
            const auto profile = hir_class_method_profile(
                expression_id, process_scope);
            return profile ? std::optional { profile->result_domain }
                           : std::nullopt;
        }
        if (source.kind == semantic::sv::ExpressionKind::string_literal) {
            return frontend::ValueDomain::String;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$random" || source.text == "$urandom"
                || source.text == "$urandom_range")) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == ".len" && source.operands.size() == 1U
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == ".getc"
                || source.text == ".compare"
                || source.text == ".icompare"
                || source.text == ".atoi"
                || source.text == ".atohex"
                || source.text == ".atooct"
                || source.text == ".atobin"
                || source.text == ".atoreal")
            && !source.operands.empty()
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return source.text == ".getc"
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Integer;
        }
        if (source.kind == semantic::sv::ExpressionKind::index
            && source.operands.size() == 2U
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::boolean_literal) {
            return frontend::ValueDomain::Boolean;
        }
        if (source.kind == semantic::sv::ExpressionKind::logic_literal) {
            return frontend::ValueDomain::Logic4;
        }
        if (source.kind == semantic::sv::ExpressionKind::integer_literal) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_null
            || source.kind
                == semantic::sv::ExpressionKind::class_allocation) {
            return frontend::ValueDomain::Bit2;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_cast) {
            return frontend::ValueDomain::Logic4;
        }
        if (source.kind == semantic::sv::ExpressionKind::update
            && source.operands.size() == 1U) {
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::unary
            && source.operands.size() == 1U) {
            const auto operand_domain = hir_expression_domain(
                source.operands.front(), process_scope);
            if (systemverilog_reduction_operator(source.text)) {
                if (!operand_domain) {
                    return std::nullopt;
                }
                return *operand_domain == frontend::ValueDomain::Bit2
                        || *operand_domain
                            == frontend::ValueDomain::Boolean
                    ? std::optional { frontend::ValueDomain::Bit2 }
                    : std::optional { frontend::ValueDomain::Logic4 };
            }
            return source.text == "!"
                ? std::optional { frontend::ValueDomain::Logic4 }
                : operand_domain;
        }
        if (source.kind == semantic::sv::ExpressionKind::binary
            && source.operands.size() == 2U) {
            if ((source.text == "==" || source.text == "!=")
                && hir_expression_is_string(
                    source.operands[0], process_scope)
                && hir_expression_is_string(
                    source.operands[1], process_scope)) {
                return frontend::ValueDomain::Logic4;
            }
            if (source.text == "===" || source.text == "!==") {
                return frontend::ValueDomain::Bit2;
            }
            if (source.text == "==" || source.text == "!="
                || source.text == "==?" || source.text == "!=?"
                || source.text == "&&" || source.text == "||") {
                return frontend::ValueDomain::Logic4;
            }
            const auto left = hir_expression_domain(
                source.operands[0], process_scope);
            const auto right = hir_expression_domain(
                source.operands[1], process_scope);
            if (!left || !right) {
                return std::nullopt;
            }
            return *left == frontend::ValueDomain::Logic4
                    || *right == frontend::ValueDomain::Logic4
                ? frontend::ValueDomain::Logic4
                : frontend::ValueDomain::Bit2;
        }
        if ((source.kind == semantic::sv::ExpressionKind::index
                || source.kind == semantic::sv::ExpressionKind::slice)
            && !source.operands.empty()) {
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if ((source.kind == semantic::sv::ExpressionKind::concatenation
                || source.kind == semantic::sv::ExpressionKind::replication)
            && !source.operands.empty()) {
            const auto first = source.kind
                    == semantic::sv::ExpressionKind::replication
                ? 1U
                : 0U;
            if (first == source.operands.size()) {
                return std::nullopt;
            }
            auto result = frontend::ValueDomain::Bit2;
            for (std::size_t index = first;
                index < source.operands.size(); ++index) {
                auto domain = hir_expression_domain(
                    source.operands[index], process_scope);
                // A string literal operand is its two-state character bits
                // (IEEE 1800-2017 5.9).
                const auto operand = specialized_hir_unit_->find_expression(
                    source.operands[index]);
                if (domain
                    && (*domain == frontend::ValueDomain::Integer
                        || (*domain == frontend::ValueDomain::String
                            && operand && operand->systemverilog != nullptr
                            && operand->systemverilog->kind
                                == semantic::sv::ExpressionKind::
                                    string_literal))) {
                    domain = frontend::ValueDomain::Bit2;
                }
                if (!domain || (*domain != frontend::ValueDomain::Bit2 && *domain != frontend::ValueDomain::Logic4)) {
                    return std::nullopt;
                }
                if (*domain == frontend::ValueDomain::Logic4) {
                    result = frontend::ValueDomain::Logic4;
                }
            }
            return result;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "?:" && source.operands.size() == 3U) {
            const auto when_true = hir_expression_domain(
                source.operands[1], process_scope);
            const auto when_false = hir_expression_domain(
                source.operands[2], process_scope);
            const auto packed_integral = [](const auto domain) {
                return domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Integer;
            };
            if (!when_true || !when_false
                || !packed_integral(*when_true)
                || !packed_integral(*when_false)) {
                return std::nullopt;
            }
            return *when_true == frontend::ValueDomain::Logic4
                    || *when_false == frontend::ValueDomain::Logic4
                ? frontend::ValueDomain::Logic4
                : frontend::ValueDomain::Bit2;
        }
        if (source.referenced_name) {
            selected = source.referenced_name->selected;
        }
    } else {
        const auto& source = *expression->vhdl;
        if (source.kind == semantic::vhdl::ExpressionKind::boolean_literal) {
            return frontend::ValueDomain::Boolean;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::logic_literal) {
            return frontend::ValueDomain::Logic9;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::string_literal
            && source.decoded_string
            && !source.decoded_string->empty()
            && std::ranges::all_of(
                *source.decoded_string,
                [](const char value) {
                    const auto normalized = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(value)));
                    return normalized == 'U' || normalized == 'X'
                        || normalized == '0' || normalized == '1'
                        || normalized == 'Z' || normalized == 'W'
                        || normalized == 'L' || normalized == 'H'
                        || normalized == '-';
                })) {
            // A VHDL string or bit-string literal containing only zeroes and
            // ones is not intrinsically two-state. Its array type, and hence
            // its executable domain, comes from the surrounding context.
            // Non-binary standard-logic characters do constrain the literal
            // to the nine-state domain.
            return std::ranges::all_of(
                       *source.decoded_string,
                       [](const char value) {
                           return value == '0' || value == '1';
                       })
                ? std::nullopt
                : std::optional { frontend::ValueDomain::Logic9 };
        }
        if (source.kind == semantic::vhdl::ExpressionKind::integer_literal) {
            return frontend::ValueDomain::Integer;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::aggregate
            && !source.associations.empty()) {
            auto domain = frontend::ValueDomain::Bit2;
            for (const auto& association : source.associations) {
                const auto value_domain = hir_expression_domain(
                    association.value, process_scope);
                if (!value_domain || !scalar_domain(*value_domain)) {
                    return std::nullopt;
                }
                if (*value_domain == frontend::ValueDomain::Logic9) {
                    domain = frontend::ValueDomain::Logic9;
                } else if (domain != frontend::ValueDomain::Logic9
                    && *value_domain == frontend::ValueDomain::Logic4) {
                    domain = frontend::ValueDomain::Logic4;
                }
            }
            return domain;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::unary
            && source.operands.size() == 1U) {
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::vhdl::ExpressionKind::binary
            && source.operands.size() == 2U) {
            if (vhdl_comparison_operator(source.text)) {
                return frontend::ValueDomain::Boolean;
            }
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if ((source.kind == semantic::vhdl::ExpressionKind::index
                || source.kind == semantic::vhdl::ExpressionKind::slice)
            && !source.operands.empty()) {
            if (const auto subtype = hir_vhdl_expression_subtype(
                    expression_id)) {
                const auto domain = vhdl_domain(subtype->domain);
                if (scalar_domain(domain)) {
                    return domain;
                }
            }
            return hir_expression_domain(
                source.operands.front(), process_scope);
        }
        if (source.kind
                == semantic::vhdl::ExpressionKind::concatenation
            && !source.operands.empty()) {
            const auto domain = hir_expression_domain(
                source.operands.front(), process_scope);
            if (!domain
                || !std::ranges::all_of(
                    source.operands,
                    [&](const auto operand) {
                        return hir_expression_domain(
                                   operand, process_scope)
                            == domain;
                    })) {
                return std::nullopt;
            }
            return domain;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::conditional
            && source.operands.size() == 3U) {
            const auto when_true = hir_expression_domain(
                source.operands[1], process_scope);
            const auto when_false = hir_expression_domain(
                source.operands[2], process_scope);
            return when_true && when_true == when_false
                ? when_true
                : std::nullopt;
        }
        if (source.referenced_name) {
            selected = source.referenced_name->selected;
        }
    }
    if (!selected) {
        selected = hir_referenced_declaration(expression_id);
    }
    if (!selected) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(*selected);
    if (!declaration) {
        return std::nullopt;
    }
    if (declaration->systemverilog != nullptr
        && declaration->systemverilog->type) {
        return hir_systemverilog_type_four_state(
                   *declaration->systemverilog->type)
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2;
    }
    if (declaration->vhdl != nullptr) {
        const auto declared_subtype = declaration->vhdl->subtype
            ? declaration->vhdl->subtype
            : declaration->vhdl->callable
                    && declaration->vhdl->callable->function
                    && declaration->vhdl->callable->return_type
            ? declaration->vhdl->callable->return_type
            : std::nullopt;
        if (!declared_subtype) {
            return std::nullopt;
        }
        const auto subtype = hir_effective_vhdl_subtype(
            *declared_subtype);
        return subtype
            ? std::optional { vhdl_domain(subtype->domain) }
            : std::nullopt;
    }
    return std::nullopt;
}

bool Lowerer::hir_vhdl_function_name(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
        || !expression->vhdl->referenced_name) {
        return false;
    }
    if (const auto selected = hir_referenced_declaration(expression_id)) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *selected);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable
            || !declaration->vhdl->callable->function) {
            return false;
        }
    }
    return resolve_hir_vhdl_function_call(
               expression_id, hir_process_scope_, 0U)
        .has_value();
}

bool Lowerer::hir_vhdl_now_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
            && expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call)
        || !expression->vhdl->operands.empty()
        || (!same_hir_identifier(expression->vhdl->text, "now", true)
            && !same_hir_identifier(
                expression->vhdl->text, "std.standard.now", true))) {
        return false;
    }
    const auto selected = hir_referenced_declaration(expression_id);
    if (!selected) {
        return true;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        *selected);
    return declaration && declaration->vhdl != nullptr
        && declaration->vhdl->callable
        && !declaration->vhdl->callable->defined;
}

bool Lowerer::hir_vhdl_subtype_is_real(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    const auto real_name = [](const std::string_view name) {
        const auto separator = name.find_last_of(".:");
        return same_hir_identifier(
            separator == std::string_view::npos
                ? name
                : name.substr(separator + 1U),
            "real", true);
    };
    if (real_name(subtype.type_mark.spelling)) {
        return true;
    }
    auto type_id = subtype.type_mark.target;
    std::unordered_set<std::uint32_t> visited;
    while (type_id.valid() && visited.insert(type_id.value()).second) {
        const auto type = specialized_hir_unit_->find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return false;
        }
        if (real_name(type->vhdl->name)
            || real_name(type->vhdl->base.type_mark.spelling)) {
            return true;
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias
            && type->vhdl->form != semantic::vhdl::TypeForm::scalar) {
            return false;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    return false;
}

bool Lowerer::hir_vhdl_expression_is_real(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return hir_vhdl_expression_is_real(*actual);
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto& source = *expression->vhdl;
    switch (source.kind) {
    case semantic::vhdl::ExpressionKind::real_literal:
        return true;
    case semantic::vhdl::ExpressionKind::unary:
        return source.operands.size() == 1U
            && hir_vhdl_expression_is_real(source.operands.front());
    case semantic::vhdl::ExpressionKind::binary:
        // REAL op REAL, and REAL ** INTEGER (IEEE 1076-2008 9.2.7-9.2.8).
        // TIME * REAL and REAL * TIME are physical.
        if (source.operands.size() != 2U
            || (source.text != "+" && source.text != "-"
                && source.text != "*" && source.text != "/"
                && source.text != "**")) {
            return false;
        }
        {
            // universal_real * universal_integer, universal_integer *
            // universal_real and universal_real / universal_integer are
            // universal_real (IEEE 1076-2008 9.2.7).
            const auto universal_integer = [&](const semantic::ExpressionId id) {
                const auto operand = specialized_hir_unit_->find_expression(id);
                return operand && operand->vhdl != nullptr
                    && operand->vhdl->kind
                        == semantic::vhdl::ExpressionKind::integer_literal
                    && operand->vhdl->nominal_type.empty();
            };
            const auto left_real = hir_vhdl_expression_is_real(source.operands.front());
            const auto right_real = hir_vhdl_expression_is_real(source.operands.back());
            if ((source.text == "*" || source.text == "/") && left_real
                && universal_integer(source.operands.back())) {
                return true;
            }
            if (source.text == "*" && right_real
                && universal_integer(source.operands.front())) {
                return true;
            }
            return left_real && (source.text == "**" || right_real);
        }
    case semantic::vhdl::ExpressionKind::call: {
        if (source.text == "'left" || source.text == "'right"
            || source.text == "'high" || source.text == "'low"
            || source.text == "'value") {
            if (source.operands.empty()) {
                return false;
            }
            const auto prefix = hir_vhdl_type_actual(
                source.operands.front());
            return prefix && hir_vhdl_subtype_is_real(*prefix);
        }
        if (same_hir_identifier(source.text, "real", true)
            || same_hir_identifier(
                source.text, "std.standard.real", true)) {
            return true;
        }
        const auto resolution = resolve_hir_vhdl_function_call(
            expression_id, hir_process_scope_, 0U);
        if (resolution) {
            const auto declaration = specialized_hir_unit_
                ->find_declaration(resolution->body);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->return_type) {
                return hir_vhdl_subtype_is_real(
                    *declaration->vhdl->callable->return_type);
            }
        }
        break;
    }
    default:
        break;
    }
    const auto subtype = hir_vhdl_expression_subtype(expression_id);
    return subtype && hir_vhdl_subtype_is_real(*subtype);
}

frontend::SystemVerilogScalarKind Lowerer::hir_systemverilog_scalar_kind(
    const semantic::ExpressionId expression_id,
    const frontend::SystemVerilogScalarKind contextual_kind) const
{
    using Kind = frontend::SystemVerilogScalarKind;
    if (specialized_hir_unit_ == nullptr) {
        return Kind::None;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return hir_systemverilog_scalar_kind(*actual, contextual_kind);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_systemverilog_scalar_kind(*actual, contextual_kind);
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (expression && expression->vhdl != nullptr) {
        return hir_vhdl_expression_is_real(expression_id)
            ? Kind::Real
            : Kind::None;
    }
    if (!expression || expression->systemverilog == nullptr) {
        return Kind::None;
    }
    const auto& source = *expression->systemverilog;
    const auto spelling_kind = [](const std::string_view spelling) {
        if (spelling == "shortreal") {
            return Kind::ShortReal;
        }
        if (spelling == "real") {
            return Kind::Real;
        }
        if (spelling == "realtime") {
            return Kind::Realtime;
        }
        if (spelling == "time") {
            return Kind::Time;
        }
        if (spelling == "chandle") {
            return Kind::Chandle;
        }
        return Kind::None;
    };
    // A real cast has its target's scalar kind (6.24.1).
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text.starts_with("@sv-cast:")
        && source.operands.size() == 1U) {
        const auto cast_kind = spelling_kind(
            std::string_view { source.text }.substr(9U));
        if (cast_kind == Kind::ShortReal || cast_kind == Kind::Real
            || cast_kind == Kind::Realtime) {
            return cast_kind;
        }
    }
    const auto declaration_kind = [&](const semantic::DeclarationId id) {
        const auto declaration = specialized_hir_unit_->find_declaration(id);
        if (!declaration || declaration->systemverilog == nullptr) {
            return Kind::None;
        }
        const auto& record = *declaration->systemverilog;
        // An untyped parameter takes its value's type (6.20.2).
        if ((record.form == semantic::sv::DeclarationForm::parameter
                || record.form
                    == semantic::sv::DeclarationForm::local_parameter)
            && record.initializer && record.initializer != expression_id
            && (!record.type
                || (record.type->target.spelling == "implicit"
                    && !record.type->packed_range))) {
            // A user function call has its declared return type.
            const auto initializer = specialized_hir_unit_->find_expression(
                *record.initializer);
            const auto callee_id = initializer
                    && initializer->systemverilog != nullptr
                    && initializer->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                ? hir_referenced_declaration(*record.initializer)
                : std::nullopt;
            if (callee_id) {
                const auto callee = specialized_hir_unit_->find_declaration(
                    *callee_id);
                if (callee && callee->systemverilog != nullptr
                    && callee->systemverilog->callable) {
                    return spelling_kind(callee->systemverilog->callable
                                             ->return_type.target.spelling);
                }
            }
            return hir_systemverilog_scalar_kind(*record.initializer);
        }
        return record.type ? spelling_kind(record.type->target.spelling)
                           : Kind::None;
    };

    const auto retained_kind = static_cast<Kind>(source.scalar_kind);
    if (retained_kind != Kind::None) {
        return retained_kind;
    }
    if (source.kind == semantic::sv::ExpressionKind::name
        && source.text.find('.') != std::string::npos) {
        if (const auto parameter = hir_hierarchical_parameter(expression_id);
            parameter && parameter->scalar_kind != Kind::None) {
            return parameter->scalar_kind;
        }
    }
    // A real member of an unpacked structure (7.2).
    if (source.kind == semantic::sv::ExpressionKind::name
        || source.kind == semantic::sv::ExpressionKind::index
        || (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with("@sv-select:"))) {
        if (const auto member = hir_container_aggregate_selection(
                expression_id);
            member
            && member->leaf.element_kind
                == runtime::simir::ContainerElementKind::Scalar
            && member->leaf.scalar_kind != Kind::None) {
            return member->leaf.scalar_kind;
        }
    }

    if (source.kind == semantic::sv::ExpressionKind::integer_literal
        && (source.decimal_literal
            || systemverilog_real_literal(source.text))) {
        return contextual_kind == Kind::ShortReal
                || contextual_kind == Kind::Real
                || contextual_kind == Kind::Realtime
            ? contextual_kind
            : Kind::Real;
    }
    if (source.kind == semantic::sv::ExpressionKind::class_null) {
        return contextual_kind == Kind::Chandle
            ? contextual_kind
            : Kind::Chandle;
    }
    if ((source.kind == semantic::sv::ExpressionKind::unary
            || source.kind == semantic::sv::ExpressionKind::update)
        && source.operands.size() == 1U) {
        // Reductions and logical negation are integral (11.4.7).
        if (source.kind == semantic::sv::ExpressionKind::unary
            && (systemverilog_reduction_operator(source.text)
                || source.text == "!")) {
            return Kind::None;
        }
        return hir_systemverilog_scalar_kind(
            source.operands.front(), contextual_kind);
    }
    // A user function call has its declared return type, including one
    // reached through a package import.
    if (source.kind == semantic::sv::ExpressionKind::call
        && !source.text.starts_with("$") && !source.text.starts_with("@")
        && source.text != "?:") {
        if (const auto callee_id = hir_referenced_declaration(expression_id)) {
            const auto callee = specialized_hir_unit_->find_declaration(
                *callee_id);
            if (callee && callee->systemverilog != nullptr
                && callee->systemverilog->callable
                && callee->systemverilog->callable->function) {
                if (const auto kind = spelling_kind(
                        callee->systemverilog->callable->return_type.target
                            .spelling);
                    kind != Kind::None) {
                    return kind;
                }
            }
        }
    }
    // An element of a real array is real (7.4).
    if (source.kind == semantic::sv::ExpressionKind::index
        && !source.operands.empty()) {
        if (const auto kind = hir_systemverilog_scalar_kind(
                source.operands.front());
            kind == Kind::Real || kind == Kind::ShortReal
            || kind == Kind::Realtime) {
            return kind;
        }
    }
    // An arithmetic operation or conditional with a real operand is real
    // (IEEE 1800-2017 11.3.1); a comparison is integral.
    const auto combined = [&](const semantic::ExpressionId left,
                              const semantic::ExpressionId right) {
        const auto lhs = hir_systemverilog_scalar_kind(left);
        const auto rhs = hir_systemverilog_scalar_kind(right);
        const auto real = [](const Kind kind) {
            return kind == Kind::Real || kind == Kind::Realtime;
        };
        return real(lhs) || real(rhs) ? Kind::Real
            : lhs == Kind::ShortReal || rhs == Kind::ShortReal
            ? Kind::ShortReal
            : Kind::None;
    };
    if (source.kind == semantic::sv::ExpressionKind::binary
        && source.operands.size() == 2U) {
        if (source.text == "+" || source.text == "-" || source.text == "*"
            || source.text == "/" || source.text == "**") {
            if (const auto kind = combined(
                    source.operands[0], source.operands[1]);
                kind != Kind::None) {
                return kind;
            }
        } else if (source.text == "==" || source.text == "!="
            || source.text == "<" || source.text == "<="
            || source.text == ">" || source.text == ">="
            || source.text == "&&" || source.text == "||") {
            return Kind::None;
        }
    }
    if (source.kind == semantic::sv::ExpressionKind::call
        && source.text == "?:" && source.operands.size() == 3U) {
        if (const auto kind = combined(source.operands[1], source.operands[2]);
            kind != Kind::None) {
            return kind;
        }
    }
    if (source.referenced_name && source.referenced_name->selected) {
        if (const auto kind = declaration_kind(
                *source.referenced_name->selected);
            kind != Kind::None) {
            return kind;
        }
    }
    if (!source.nominal_type.empty()) {
        if (const auto kind = spelling_kind(source.nominal_type);
            kind != Kind::None) {
            return kind;
        }
    }
    return contextual_kind;
}

std::optional<semantic::sv::TypeReference>
Lowerer::hir_systemverilog_parameter_type(
    const semantic::ExpressionId expression_id) const
{
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id && specialized_hir_unit_
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *declaration->systemverilog;
    if ((source.form != semantic::sv::DeclarationForm::parameter
            && source.form != semantic::sv::DeclarationForm::local_parameter)
        || !source.type
        || (source.type->target.spelling == "implicit"
            && !source.type->packed_range)
        || source.type->container_form
        || source.type->target.spelling == "string") {
        return std::nullopt;
    }
    return *source.type;
}

std::vector<Lowerer::HirPackedRange>
Lowerer::hir_systemverilog_packed_shape(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return { };
    }
    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::index
        && source.operands.size() == 2U) {
        auto outer = hir_systemverilog_packed_shape(source.operands.front());
        if (outer.size() < 2U) {
            return { };
        }
        outer.erase(outer.begin());
        return outer;
    }
    if (source.kind != semantic::sv::ExpressionKind::name) {
        return { };
    }
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type
        || declaration->systemverilog->type->container_form) {
        return { };
    }
    // Follow typedef aliases to the declared packed dimensions. A typedef
    // from another unit (`$unit` or a package) keeps only its spelling.
    auto resolved = *declaration->systemverilog->type;
    if (resolved.packed_dimensions.empty()
        && !resolved.target.target.valid()
        && !resolved.target.spelling.empty()) {
        resolved = semantic::CompiledDesignResolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        }.effective_systemverilog_type(
             resolved, declaration->systemverilog->scope)
                       .value_or(resolved);
    }
    const auto* type = &resolved;
    for (std::size_t depth { };
        type->packed_dimensions.empty() && type->target.target.valid()
        && depth < 16U;
        ++depth) {
        const auto definition
            = specialized_hir_unit_->find_type(type->target.target);
        if (!definition || definition->systemverilog == nullptr
            || (definition->systemverilog->form
                    != semantic::sv::TypeForm::alias
                && definition->systemverilog->form
                    != semantic::sv::TypeForm::packed_integral)) {
            break;
        }
        type = &definition->systemverilog->base;
    }
    std::vector<HirPackedRange> shape;
    for (const auto& dimension : type->packed_dimensions) {
        const auto left = dimension.left ? dimension.left
            : dimension.left_expression
            ? hir_constant_integer(*dimension.left_expression)
            : std::nullopt;
        const auto right = dimension.right ? dimension.right
            : dimension.right_expression
            ? hir_constant_integer(*dimension.right_expression)
            : std::nullopt;
        if (!left || !right) {
            return { };
        }
        shape.push_back(HirPackedRange { *left, *right, *left >= *right });
    }
    return shape;
}

bool Lowerer::hir_actual_extension_signed(
    const semantic::ExpressionId actual,
    const bool formal_signed) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(actual)
        : std::nullopt;
    return expression && expression->systemverilog != nullptr
        ? hir_expression_signed(actual)
        : formal_signed;
}

std::optional<std::size_t> Lowerer::hir_index_target_width(
    const semantic::ExpressionId target) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::index
        && expression->systemverilog->operands.size() == 2U
        && hir_systemverilog_packed_shape(
               expression->systemverilog->operands.front())
                .size()
            > 1U) {
        return hir_expression_width(target, hir_process_scope_);
    }
    return std::size_t { 1U };
}

std::uint32_t Lowerer::hir_systemverilog_decimal_width(
    const semantic::ExpressionId expression,
    const runtime::simir::OutputFormat format,
    const bool suppress_leading_zero,
    const std::uint32_t minimum_width) const
{
    const auto scalar = hir_systemverilog_scalar_kind(expression);
    if (format != runtime::simir::OutputFormat::decimal
        || suppress_leading_zero || minimum_width != 0U
        || (scalar != frontend::SystemVerilogScalarKind::None
            && scalar != frontend::SystemVerilogScalarKind::Time)) {
        return minimum_width;
    }
    const auto width = hir_expression_width(expression, hir_process_scope_);
    if (!width || *width == 0U) {
        return minimum_width;
    }
    // The digits of 2^w - 1 (unsigned) or of 2^(w-1) plus a sign; neither
    // power of two is a power of ten, so the digit count is exact.
    const bool signed_value = hir_expression_signed(expression);
    const auto magnitude_bits = signed_value ? *width - 1U : *width;
    const auto digits = static_cast<std::uint32_t>(
        std::floor(static_cast<long double>(magnitude_bits)
            * std::log10(2.0L))) + 1U;
    return digits + (signed_value ? 1U : 0U);
}

bool Lowerer::hir_vhdl_character_typed(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    if (const auto code = vhdl_character_literal_code(*expression->vhdl)) {
        return vhdl_non_logic_character_literal(*expression->vhdl);
    }
    if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::name
        && !(expression->vhdl->referenced_name
            && (expression->vhdl->referenced_name->selected
                || !expression->vhdl->referenced_name->overloads.empty()))
        && vhdl_character_control_literal_code(expression->vhdl->text)) {
        return true;
    }
    const auto subtype = hir_vhdl_expression_subtype(expression_id);
    if (!subtype) {
        return false;
    }
    auto spelling = std::string_view { subtype->type_mark.spelling };
    auto target = subtype->type_mark.target;
    std::unordered_set<std::uint32_t> visited;
    while (target.valid() && visited.insert(target.value()).second) {
        const auto type = specialized_hir_unit_->find_type(target);
        if (!type || type->vhdl == nullptr
            || (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form != semantic::vhdl::TypeForm::alias)) {
            break;
        }
        spelling = type->vhdl->base.type_mark.spelling;
        target = type->vhdl->base.type_mark.target;
    }
    if (const auto separator = spelling.find_last_of('.');
        separator != std::string_view::npos) {
        spelling.remove_prefix(separator + 1U);
    }
    return same_hir_identifier(spelling, "character", true);
}

void Lowerer::collect_hir_vhdl_character_contexts() const
{
    if (specialized_hir_unit_ == nullptr) {
        return;
    }
    // Process and subprogram bodies live in the design-wide HIR rather than
    // the specialization overlay, so scan that once per HIR revision.
    const auto& hir = specialized_hir_unit_->design().vhdl_hir;
    if (hir_vhdl_character_context_hir_ == &hir
        && hir_vhdl_character_context_revision_ == hir.revision()) {
        return;
    }
    hir_vhdl_character_context_hir_ = &hir;
    hir_vhdl_character_context_revision_ = hir.revision();
    hir_vhdl_character_context_literals_.clear();
    const auto logic_literal = [&](const semantic::ExpressionId id) {
        const auto expression = specialized_hir_unit_->find_expression(id);
        return expression && expression->vhdl != nullptr
            && vhdl_character_literal_code(*expression->vhdl)
            && !vhdl_non_logic_character_literal(*expression->vhdl);
    };
    const auto mark = [&](const semantic::ExpressionId id) {
        if (logic_literal(id)) {
            hir_vhdl_character_context_literals_.insert(id.value());
        }
    };
    for (const auto& expression : hir.expressions()) {
        const auto& operands = expression.operands;
        if (expression.kind == semantic::vhdl::ExpressionKind::binary
            && operands.size() == 2U
            && vhdl_comparison_operator(expression.text)) {
            if (logic_literal(operands[0])
                && hir_vhdl_character_typed(operands[1])) {
                mark(operands[0]);
            } else if (logic_literal(operands[1])
                && hir_vhdl_character_typed(operands[0])) {
                mark(operands[1]);
            }
        } else if (expression.kind == semantic::vhdl::ExpressionKind::call
            && operands.size() == 2U && logic_literal(operands[1])
            && (expression.text == "'pos" || expression.text == "'succ"
                || expression.text == "'pred" || expression.text == "'image"
                || expression.text == "'leftof"
                || expression.text == "'rightof")) {
            const auto prefix = hir_vhdl_type_actual(operands[0]);
            if (prefix
                && same_hir_identifier(
                    prefix->type_mark.spelling, "character", true)) {
                mark(operands[1]);
            }
        } else if (expression.kind == semantic::vhdl::ExpressionKind::call
            && operands.size() == 1U
            && same_hir_identifier(expression.text,
                "@vhdl-qualified:character", true)) {
            mark(operands[0]);
        }
    }
    for (const auto& statement : hir.statements()) {
        const auto literal_value = (statement.value
                && logic_literal(*statement.value))
            || std::ranges::any_of(statement.waveform,
                [&](const semantic::vhdl::WaveformElement& element) {
                    return logic_literal(element.value);
                });
        if (statement.target && literal_value
            && hir_vhdl_character_typed(*statement.target)) {
            if (statement.value) {
                mark(*statement.value);
            }
            for (const auto& element : statement.waveform) {
                mark(element.value);
            }
        }
        const auto literal_choice = std::ranges::any_of(
            statement.alternatives,
            [&](const semantic::vhdl::CaseAlternative& alternative) {
                return std::ranges::any_of(
                    alternative.choices, logic_literal);
            });
        if (statement.condition && literal_choice
            && hir_vhdl_character_typed(*statement.condition)) {
            for (const auto& alternative : statement.alternatives) {
                for (const auto choice : alternative.choices) {
                    mark(choice);
                }
            }
        }
    }
    for (const auto& declaration : hir.declarations()) {
        if (!declaration.initializer || !declaration.subtype
            || !logic_literal(*declaration.initializer)) {
            continue;
        }
        auto spelling
            = std::string_view { declaration.subtype->type_mark.spelling };
        if (const auto separator = spelling.find_last_of('.');
            separator != std::string_view::npos) {
            spelling.remove_prefix(separator + 1U);
        }
        if (same_hir_identifier(spelling, "character", true)) {
            mark(*declaration.initializer);
        }
    }
}

std::optional<std::uint64_t> Lowerer::hir_vhdl_character_literal_code(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::name
        && (expected_width == 0U || expected_width == 8U)
        && !(expression->vhdl->referenced_name
            && (expression->vhdl->referenced_name->selected
                || !expression->vhdl->referenced_name->overloads.empty()))) {
        // CHARACTER's non-graphic literals (NUL, ..., DEL, C128, ...) when
        // no declaration claims the name (IEEE 1076-2008 16.3).
        return vhdl_character_control_literal_code(expression->vhdl->text);
    }
    collect_hir_vhdl_character_contexts();
    // A logic character ('0', 'X', ...) is a BIT or STD_ULOGIC value unless
    // its context is CHARACTER-typed; an 8-bit expected width alone does not
    // decide that (numeric_std `u + '1'` widens a STD_ULOGIC operand).
    const auto code = vhdl_character_literal_code(*expression->vhdl);
    if (!code || (expected_width != 0U && expected_width != 8U)
        || (!vhdl_non_logic_character_literal(*expression->vhdl)
            && !hir_vhdl_character_context_literals_.contains(
                expression_id.value()))) {
        return std::nullopt;
    }
    return *code;
}

std::optional<std::size_t> Lowerer::hir_vhdl_null_concatenation_operand(
    const std::span<const semantic::ExpressionId> operands) const
{
    if (operands.size() != 2U) {
        return std::nullopt;
    }
    const auto left = hir_expression_width(operands[0], hir_process_scope_);
    const auto right = hir_expression_width(operands[1], hir_process_scope_);
    if (!left || !right || (*left == 0U) == (*right == 0U)) {
        return std::nullopt;
    }
    return *left == 0U ? 1U : 0U;
}

std::optional<std::size_t> Lowerer::hir_expression_width(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (hir_vhdl_now_expression(expression_id)) {
        return 64U;
    }
    if (const auto parameter = hir_hierarchical_parameter(expression_id)) {
        return parameter->value.width();
    }
    if (const auto member = hir_virtual_interface_member(expression_id)) {
        return member->width;
    }
    if (const auto method = hir_systemverilog_enumeration_method(
            expression_id)) {
        return method->method == "num" ? std::optional<std::size_t> { 32U }
            : method->method == "name" ? std::nullopt
                                       : std::optional { method->width };
    }
    if (hir_vhdl_function_name(expression_id)) {
        const auto resolution = resolve_hir_vhdl_function_call(
            expression_id, process_scope, 0U);
        const auto type = resolution
            ? hir_callable_type(resolution->body, 0U)
            : std::nullopt;
        if (type && !type->string && type->width != 0U) {
            return type->width;
        }
    }
    if (const auto binding = hir_case_pattern_binding(expression_id)) {
        return binding->width;
    }
    if (const auto subtype
        = hir_vhdl_original_integer_generic_subtype(expression_id)) {
        if (const auto width = vhdl_runtime_width(*subtype)) {
            return width;
        }
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        // A SystemVerilog parameter with a type or a range converts its
        // value, default or override, to that type (IEEE 1800-2017 6.20.2).
        if (const auto declared = hir_systemverilog_parameter_type(
                expression_id)) {
            if (const auto width = hir_systemverilog_type_width(*declared)) {
                return width;
            }
        }
        return hir_expression_width(*actual, process_scope);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return hir_expression_width(*actual, process_scope);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!push_hir_let_frame(expression_id, *declaration)) {
            return std::nullopt;
        }
        const auto result = hir_expression_width(
            declaration->expression, process_scope);
        hir_let_frames_.pop_back();
        return result;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return std::nullopt;
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "@vhdl-null") {
        return 32U;
    }
    if (hir_vhdl_character_literal_code(expression_id, 0U)) {
        return 8U;
    }
    if (expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::index) {
        // An element select of a multidimensional packed array is as wide
        // as the remaining dimensions.
        const auto shape = hir_systemverilog_packed_shape(expression_id);
        if (!shape.empty()) {
            std::size_t width { 1U };
            for (const auto& dimension : shape) {
                const auto count
                    = index_distance(dimension.left, dimension.right) + 1U;
                if (count > std::numeric_limits<std::size_t>::max() / width) {
                    return std::nullopt;
                }
                width *= static_cast<std::size_t>(count);
            }
            return width;
        }
    }
    if (hir_vhdl_generate_iterator_value(
            expression_id, process_scope)) {
        return frontend::vhdl_predefined_integer_storage_width(
            vhdl_standard_);
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name) {
        // The unconstrained declaration subtype can expose a one-bit
        // placeholder; the active frame and exact captured range define
        // this invocation's actual packed formal width.
        const auto declaration = hir_target_declaration(expression_id);
        const auto callable_range = declaration
            ? hir_vhdl_callable_formal_range(*declaration)
            : std::nullopt;
        if (callable_range) {
            if (!*callable_range || !active_hir_callable_
                || *active_hir_callable_ >= hir_callable_frames_.size()) {
                return std::nullopt;
            }
            const auto& range = **callable_range;
            const auto distance = index_distance(range.left, range.right);
            if (distance == std::numeric_limits<std::uint64_t>::max()
                || distance + 1U
                    > std::numeric_limits<std::size_t>::max()
                || (range.left != range.right
                    && range.descending
                        != (range.left > range.right))) {
                return std::nullopt;
            }
            const auto formal_width = static_cast<std::size_t>(
                distance + 1U);
            const auto& frame
                = hir_callable_frames_[*active_hir_callable_];
            std::optional<std::size_t> formal_index;
            for (std::size_t index { };
                index < frame.formals.size(); ++index) {
                const auto body_formal = frame.formals[index]
                    == *declaration;
                const auto profile_formal
                    = index < frame.profile_formals.size()
                    && frame.profile_formals[index] == *declaration;
                if (!body_formal && !profile_formal) {
                    continue;
                }
                if (formal_index) {
                    return std::nullopt;
                }
                formal_index = index;
            }
            if (!formal_index || *formal_index >= frame.arguments.size()
                || *formal_index >= frame.argument_is_string.size()
                || *formal_index >= frame.argument_is_container.size()
                || frame.argument_is_string[*formal_index]
                || frame.argument_is_container[*formal_index]) {
                return std::nullopt;
            }
            const auto argument = frame.arguments[*formal_index];
            const auto domain = register_domain(argument);
            if (register_width(argument) != formal_width
                || domain == frontend::ValueDomain::Unknown
                || domain == frontend::ValueDomain::String) {
                return std::nullopt;
            }
            return formal_width;
        }
    }
    if (const auto selection
        = hir_vhdl_environment_call_path_member_selection(expression_id);
        selection && selection->member == 3U) {
        return 64U;
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "@vhdl-dereference"
        && expression->vhdl->operands.size() == 1U) {
        const auto subtype = hir_vhdl_expression_subtype(expression_id);
        return subtype ? vhdl_runtime_width(*subtype) : std::nullopt;
    }
    if (const auto signal = hir_direct_signal_binding(expression_id)) {
        return signal->width;
    }
    if (hir_systemverilog_interface_handle(expression_id)) {
        return 64U;
    }
    if (const auto conversion = hir_vhdl_conversion_profile(expression_id)) {
        return conversion->width;
    }
    if (const auto profile = hir_vhdl_float_function_profile(expression_id)) {
        return profile->width;
    }
    if (const auto profile = hir_vhdl_fixed_function_profile(expression_id)) {
        return profile->width;
    }
    if (const auto standard = hir_vhdl_standard_function_profile(
            expression_id)) {
        return standard->width;
    }
    if (const auto vital = hir_vhdl_vital_expression_profile(
            expression_id);
        vital && vital->width != 0U) {
        return vital->width;
    }
    if (const auto api = hir_vhdl_assert_expression_api(expression_id)) {
        using Api = frontend::VhdlSimulatorApi;
        if (*api == Api::is_vhdl_assert_failed
            || *api == Api::get_vhdl_assert_enable) {
            return 1U;
        }
        if (*api == Api::get_vhdl_assert_count) {
            return 64U;
        }
        if (*api == Api::get_vhdl_read_severity) {
            return 2U;
        }
    }
    if (hir_vhdl_standard_enumeration_literal(expression_id)) {
        return 2U;
    }
    if (is_hir_vhdl_environment_status_literal(expression_id)) {
        return 3U;
    }
    if (is_hir_vhdl_file_call(expression_id)) {
        return 1U;
    }
    if (is_hir_vhdl_environment_expression(expression_id)
        && expression->vhdl != nullptr) {
        using Api = frontend::VhdlSimulatorApi;
        const auto api = frontend::vhdl_simulator_api(
            expression->vhdl->text);
        if (api == Api::dir_itemexists || api == Api::dir_itemisdir
            || api == Api::dir_itemisfile) {
            return 1U;
        }
        if (api == Api::dir_open || api == Api::dir_workingdir
            || api == Api::dir_createdir || api == Api::dir_deletedir
            || api == Api::dir_deletefile) {
            return 3U;
        }
        if (api == Api::localtime || api == Api::gmtime) {
            return 515U;
        }
        if (expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::binary
            && expression->vhdl->operands.size() == 2U) {
            const auto record_difference
                = expression->vhdl->text == "-"
                && hir_vhdl_time_record_expression(
                    expression->vhdl->operands.front())
                && hir_vhdl_time_record_expression(
                    expression->vhdl->operands.back());
            return record_difference ? 64U : 515U;
        }
        return 64U;
    }
    if (expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && (expression->vhdl->text == "rising_edge"
            || expression->vhdl->text == "falling_edge"
            || expression->vhdl->text == "is_x")
        && expression->vhdl->operands.size() == 1U) {
        return 1U;
    }
    if (const auto attribute = hir_vhdl_signal_attribute_profile(
            expression_id, process_scope)) {
        return attribute->width;
    }
    if (is_hir_vhdl_vital_mux2(expression_id)) {
        return 1U;
    }
    if (const auto attribute = hir_vhdl_attribute_profile(
            expression_id, process_scope)) {
        return attribute->width;
    }
    if (const auto member = hir_systemverilog_member_selection(
            expression_id)) {
        return member->width;
    }
    if (const auto member = hir_container_aggregate_selection(
            expression_id)) {
        return member->leaf.element_width;
    }
    if (const auto aggregate = hir_packed_container_aggregate_profile(
            expression_id)) {
        return aggregate->width;
    }
    if (const auto selection = hir_vhdl_array_selection(expression_id)) {
        return selection->width;
    }
    if (const auto member = hir_vhdl_member_selection(expression_id)) {
        return member->index
            ? std::optional { member->element_width }
            : std::optional { member->width };
    }
    if (const auto element = hir_container_element_binding(expression_id)) {
        return element->width;
    }
    if (const auto selected = hir_referenced_declaration(expression_id)) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *selected);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            const auto subtype = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            if (subtype) {
                if (const auto width = vhdl_runtime_width(*subtype)) {
                    return width;
                }
                const auto type = subtype->type_mark.target.valid()
                    ? specialized_hir_unit_->find_type(
                          subtype->type_mark.target)
                    : std::nullopt;
                if (type && type->vhdl != nullptr
                    && type->vhdl->form
                        == semantic::vhdl::TypeForm::access) {
                    return 32U;
                }
            }
        }
        if (const auto binding = hir_runtime_binding(
                *selected, process_scope, false)) {
            return binding->width;
        }
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->type) {
            const auto type = specialized_hir_unit_->find_type(
                declaration->systemverilog->type->target.target);
            if (type && type->systemverilog != nullptr
                && type->systemverilog->form
                    == semantic::sv::TypeForm::enumeration) {
                if (const auto width = hir_systemverilog_type_width(
                        *declaration->systemverilog->type)) {
                    return width;
                }
                return std::nullopt;
            }
        }
        const auto initializer = hir_constant_initializer(*selected);
        // An x or z valued parameter has no integer value but still has
        // its declared or initializer type.
        const auto systemverilog_value_parameter = declaration
            && declaration->systemverilog != nullptr
            && (declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::parameter
                || declaration->systemverilog->form
                    == semantic::sv::DeclarationForm::local_parameter);
        if (initializer
            && (systemverilog_value_parameter
                || hir_constant_integer(expression_id))) {
            // A parameter with a type or a range has that width; without
            // either, it takes the width of its value (IEEE 1800-2017
            // 6.20.2).
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->type
                && (declaration->systemverilog->type->target.spelling
                        != "implicit"
                    || declaration->systemverilog->type->packed_range)) {
                if (const auto declared = hir_systemverilog_type_width(
                        *declaration->systemverilog->type)) {
                    return declared;
                }
            }
            if (const auto inferred = hir_expression_width(
                    *initializer, process_scope)) {
                return inferred;
            }
        }
    }
    std::optional<semantic::DeclarationId> selected;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        if (source.kind == semantic::sv::ExpressionKind::name
            && !hir_referenced_declaration(expression_id)
            && systemverilog_genvar_identity(
                *specialized_hir_unit_, source.scope, source.text)) {
            // The generate evaluator stores the active genvar value as a
            // hierarchy identity; its SystemVerilog type is integer.
            return 32U;
        }
        constexpr auto container_index_prefix
            = std::string_view { "@sv-container-index:" };
        constexpr auto container_method_prefix
            = std::string_view { "@sv-container-method:" };
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "inside") {
            return 1U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$isunknown"
                || source.text == "$onehot"
                || source.text == "$onehot0")) {
            return 1U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$countones"
                || source.text == "$countbits")) {
            return 32U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && systemverilog_sampled_value_call(source.text)) {
            if (!systemverilog_sampled_value_preserves_signal(
                    source.text)) {
                return 1U;
            }
            return source.operands.empty()
                ? std::optional<std::size_t> { 1U }
                : hir_expression_width(
                      source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && is_hir_systemverilog_file_call(expression_id)) {
            return 32U;
        }
        if (const auto function = hir_systemverilog_math_function(
                expression_id)) {
            using Function = runtime::SystemVerilogMathFunction;
            return *function == Function::Rtoi
                    || *function == Function::BitsToShortReal
                    || *function == Function::ShortRealToBits
                    || *function == Function::Stime
                ? 32U
                : 64U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_index_prefix)) {
            return 64U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_method_prefix)) {
            const auto operation = std::string_view { source.text }.substr(
                container_method_prefix.size());
            if (operation.starts_with("pop_front:")) {
                return 64U;
            }
            if (operation.starts_with("size:")) {
                return 32U;
            }
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "@stream-left"
                || source.text == "@stream-right")
            && source.operands.size() >= 2U) {
            std::size_t width { };
            for (std::size_t index = 1U;
                index < source.operands.size(); ++index) {
                const auto operand_width = hir_expression_width(
                    source.operands[index], process_scope);
                if (!operand_width || *operand_width == 0U
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max() - width) {
                    return std::nullopt;
                }
                width += *operand_width;
            }
            return width;
        }
        if (const auto cast = hir_systemverilog_cast_profile(
                expression_id)) {
            return cast->width;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$signed" || source.text == "$unsigned")
            && source.operands.size() == 1U) {
            return hir_expression_width(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::name
            && source.text == "this"
            && hir_class_receiver_register_) {
            return 64U;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_property
            || source.kind
                == semantic::sv::ExpressionKind::class_static_property) {
            const auto profile = hir_class_property_profile(expression_id);
            return profile ? std::optional { profile->width }
                           : std::nullopt;
        }
        if (source.kind
                == semantic::sv::ExpressionKind::class_method_call
            || source.kind
                == semantic::sv::ExpressionKind::class_static_method_call) {
            const auto profile = hir_class_method_profile(
                expression_id, process_scope);
            return profile ? std::optional { profile->result_width }
                           : std::nullopt;
        }
        if (source.kind == semantic::sv::ExpressionKind::boolean_literal) {
            return 1U;
        }
        if (source.kind == semantic::sv::ExpressionKind::logic_literal) {
            // An unsized based literal ('b1, 'hff) is 32 bits; an unbased
            // unsized one ('1, 'x) is self-determined as one bit (IEEE
            // 1800-2017 5.7.1).
            const auto based = source.text.size() >= 3U
                && source.text.front() == '\''
                && std::string_view { "bodhBODHsS" }.find(source.text[1])
                    != std::string_view::npos;
            return literal_width(source.text, based ? 32U : 1U);
        }
        if (source.kind == semantic::sv::ExpressionKind::integer_literal) {
            if (systemverilog_real_literal(source.text)) {
                return 64U;
            }
            return literal_width(source.text, 32U);
        }
        // A string literal in an integral context is eight bits per
        // character (IEEE 1800-2017 5.9); "" is one NUL byte.
        if (source.kind == semantic::sv::ExpressionKind::string_literal
            && source.decoded_string) {
            return std::max<std::size_t>(source.decoded_string->size(), 1U)
                * 8U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$random" || source.text == "$urandom"
                || source.text == "$urandom_range")) {
            return 32U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == ".len" && source.operands.size() == 1U
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return 32U;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == ".getc"
                || source.text == ".compare"
                || source.text == ".icompare"
                || source.text == ".atoi"
                || source.text == ".atohex"
                || source.text == ".atooct"
                || source.text == ".atobin"
                || source.text == ".atoreal")
            && !source.operands.empty()
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return source.text == ".atoreal" ? 64U : 32U;
        }
        if (source.kind == semantic::sv::ExpressionKind::index
            && source.operands.size() == 2U
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            return 32U;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_null
            || source.kind
                == semantic::sv::ExpressionKind::class_allocation) {
            return 64U;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_cast) {
            return 1U;
        }
        if (source.kind == semantic::sv::ExpressionKind::update
            && source.operands.size() == 1U) {
            return hir_expression_width(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::unary
            && source.operands.size() == 1U) {
            return source.text == "!"
                    || systemverilog_reduction_operator(source.text)
                ? std::optional<std::size_t> { 1U }
                : hir_expression_width(
                      source.operands.front(), process_scope);
        }
        if (source.kind == semantic::sv::ExpressionKind::binary
            && source.operands.size() == 2U) {
            if ((source.text == "==" || source.text == "!=")
                && hir_expression_is_string(
                    source.operands[0], process_scope)
                && hir_expression_is_string(
                    source.operands[1], process_scope)) {
                return 1U;
            }
            if (source.text == "==" || source.text == "!="
                || source.text == "===" || source.text == "!=="
                || source.text == "==?" || source.text == "!=?"
                || source.text == "&&" || source.text == "||"
                || source.text == "<" || source.text == "<="
                || source.text == ">" || source.text == ">="
                || source.text == "->" || source.text == "<->") {
                return 1U;
            }
            // A shift or power has the width of its left operand
            // (IEEE 1800-2017 11.6.1, Table 11-21).
            if (source.text == "<<" || source.text == ">>"
                || source.text == "<<<" || source.text == ">>>"
                || source.text == "**") {
                return hir_expression_width(
                    source.operands.front(), process_scope);
            }
            const auto left = hir_expression_width(
                source.operands.front(), process_scope);
            const auto right = hir_expression_width(
                source.operands.back(), process_scope);
            return left && right
                ? std::optional { std::max(*left, *right) }
                : std::nullopt;
        }
        if (source.kind == semantic::sv::ExpressionKind::index
            && source.operands.size() == 2U) {
            const auto selection = hir_constant_selection(
                expression_id, process_scope);
            return selection ? std::optional { selection->width }
                             : std::optional<std::size_t> { 1U };
        }
        if (source.kind == semantic::sv::ExpressionKind::slice
            && source.operands.size() == 3U) {
            const auto selection = hir_constant_selection(
                expression_id, process_scope);
            return selection ? std::optional { selection->width }
                             : hir_dynamic_part_width(
                                   expression_id, process_scope);
        }
        if (source.kind
                == semantic::sv::ExpressionKind::concatenation
            && !source.operands.empty()) {
            std::size_t width { };
            for (const auto operand : source.operands) {
                const auto operand_width = hir_expression_width(
                    operand, process_scope);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max() - width) {
                    return std::nullopt;
                }
                width += *operand_width;
            }
            return width == 0U ? std::nullopt
                               : std::optional { width };
        }
        if (source.kind == semantic::sv::ExpressionKind::replication
            && source.operands.size() >= 2U) {
            const auto count = hir_constant_integer(
                source.operands.front());
            std::size_t group_width { };
            for (std::size_t index = 1U;
                index < source.operands.size(); ++index) {
                const auto operand_width = hir_expression_width(
                    source.operands[index], process_scope);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - group_width) {
                    return std::nullopt;
                }
                group_width += *operand_width;
            }
            if (!count || *count < 0 || group_width == 0U
                || static_cast<std::uint64_t>(*count)
                    > std::numeric_limits<std::size_t>::max()
                        / group_width) {
                return std::nullopt;
            }
            if (*count == 0) {
                return 0U;
            }
            return group_width * static_cast<std::size_t>(*count);
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "?:" && source.operands.size() == 3U) {
            const auto when_true = hir_expression_width(
                source.operands[1], process_scope);
            const auto when_false = hir_expression_width(
                source.operands[2], process_scope);
            return when_true && when_false
                ? std::optional { std::max(*when_true, *when_false) }
                : std::nullopt;
        }
        if (source.referenced_name) {
            selected = source.referenced_name->selected;
        }
    } else {
        const auto& source = *expression->vhdl;
        if (source.kind == semantic::vhdl::ExpressionKind::boolean_literal
            || source.kind == semantic::vhdl::ExpressionKind::logic_literal) {
            return 1U;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::integer_literal) {
            return 32U;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::aggregate
            && !source.associations.empty()) {
            std::size_t width { };
            for (const auto& association : source.associations) {
                auto choice = association.choice_spelling;
                std::ranges::transform(
                    choice, choice.begin(), [](const char value) {
                        return static_cast<char>(std::tolower(
                            static_cast<unsigned char>(value)));
                    });
                if (choice.find("others") != std::string::npos) {
                    return std::nullopt;
                }
                const auto value_width = hir_expression_width(
                    association.value, process_scope);
                const auto count = std::max<std::size_t>(
                    association.choices.size(), 1U);
                if (!value_width || *value_width == 0U
                    || *value_width
                        > std::numeric_limits<std::size_t>::max() / count
                    || *value_width * count
                        > std::numeric_limits<std::size_t>::max() - width) {
                    return std::nullopt;
                }
                width += *value_width * count;
            }
            return width == 0U ? std::nullopt
                               : std::optional { width };
        }
        if (source.kind == semantic::vhdl::ExpressionKind::string_literal
            && source.decoded_string
            && !source.decoded_string->empty()
            && std::ranges::all_of(
                *source.decoded_string,
                [](const char value) {
                    const auto normalized = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(value)));
                    return normalized == 'U' || normalized == 'X'
                        || normalized == '0' || normalized == '1'
                        || normalized == 'Z' || normalized == 'W'
                        || normalized == 'L' || normalized == 'H'
                        || normalized == '-';
                })) {
            return source.decoded_string->size();
        }
        if (source.kind == semantic::vhdl::ExpressionKind::unary
            && source.operands.size() == 1U) {
            return hir_expression_width(
                source.operands.front(), process_scope);
        }
        if (source.kind == semantic::vhdl::ExpressionKind::binary
            && source.operands.size() == 2U) {
            if (source.text == "&") {
                const auto left = hir_expression_width(
                    source.operands.front(), process_scope);
                const auto right = hir_expression_width(
                    source.operands.back(), process_scope);
                if (!left || !right
                    || *left > std::numeric_limits<std::size_t>::max()
                        - *right) {
                    return std::nullopt;
                }
                return *left + *right;
            }
            return vhdl_comparison_operator(source.text)
                ? std::optional<std::size_t> { 1U }
                : hir_expression_width(
                      source.operands.front(), process_scope);
        }
        if (source.kind == semantic::vhdl::ExpressionKind::index
            && source.operands.size() == 2U) {
            const auto selection = hir_constant_selection(
                expression_id, process_scope);
            if (selection) {
                return selection->width;
            }
            const auto subtype = hir_vhdl_expression_subtype(
                expression_id);
            return subtype ? vhdl_runtime_width(*subtype)
                           : std::nullopt;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::slice
            && source.operands.size() == 3U) {
            const auto selection = hir_constant_selection(
                expression_id, process_scope);
            return selection
                ? std::optional { selection->width }
                : hir_dynamic_part_width(
                      expression_id, process_scope);
        }
        if (source.kind
                == semantic::vhdl::ExpressionKind::concatenation
            && !source.operands.empty()) {
            std::size_t width { };
            for (const auto operand : source.operands) {
                const auto operand_width = hir_expression_width(
                    operand, process_scope);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max() - width) {
                    return std::nullopt;
                }
                width += *operand_width;
            }
            return width == 0U ? std::nullopt
                               : std::optional { width };
        }
        if (source.kind == semantic::vhdl::ExpressionKind::conditional
            && source.operands.size() == 3U) {
            const auto when_true = hir_expression_width(
                source.operands[1], process_scope);
            const auto when_false = hir_expression_width(
                source.operands[2], process_scope);
            return when_true && when_true == when_false
                ? when_true
                : std::nullopt;
        }
        if (source.referenced_name) {
            selected = source.referenced_name->selected;
        }
    }
    if (!selected) {
        selected = hir_referenced_declaration(expression_id);
    }
    if (!selected) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(*selected);
    if (!declaration) {
        return std::nullopt;
    }
    if (declaration->systemverilog != nullptr) {
        const auto& source = *declaration->systemverilog;
        if (source.type) {
            return hir_systemverilog_type_width(*source.type);
        }
    } else {
        const auto& source = *declaration->vhdl;
        const auto declared_subtype = source.subtype
            ? source.subtype
            : source.callable && source.callable->function
                    && source.callable->return_type
            ? source.callable->return_type
            : std::nullopt;
        if (declared_subtype) {
            const auto subtype = hir_effective_vhdl_subtype(
                *declared_subtype);
            if (!subtype) {
                return std::nullopt;
            }
            if (const auto width = vhdl_runtime_width(*subtype)) {
                return width;
            }
            const auto type = subtype->type_mark.target.valid()
                ? specialized_hir_unit_->find_type(
                      subtype->type_mark.target)
                : std::nullopt;
            return type && type->vhdl != nullptr
                    && type->vhdl->form
                        == semantic::vhdl::TypeForm::access
                ? std::optional<std::size_t> { 32U }
                : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<semantic::DeclarationId>
Lowerer::hir_systemverilog_named_type_declaration(
    const std::string_view spelling,
    const semantic::ScopeId use_scope) const
{
    if (specialized_hir_unit_ == nullptr || spelling.empty()) {
        return std::nullopt;
    }
    const auto resolution = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_ }
                                .resolve_systemverilog_named_type(
                                    spelling, use_scope);
    return resolution.unique();
}

std::optional<std::size_t> Lowerer::hir_systemverilog_type_width(
    const semantic::sv::TypeReference& type) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const semantic::CompiledDesignResolver type_resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    std::unordered_set<std::uint32_t> visiting;
    const auto resolve = [&](const auto& self,
                             const semantic::sv::TypeReference& reference)
        -> std::optional<std::size_t> {
        if (reference.virtual_interface
            || !reference.interface_type.empty()
            || reference.value_form == semantic::sv::TypeForm::class_handle
            || !reference.class_identity.empty()) {
            return 64U;
        }
        std::optional<std::size_t> width;
        if (reference.packed_range) {
            const auto boundary = [&](
                                      const std::optional<std::int64_t> value,
                                      const std::optional<semantic::ExpressionId>
                                          expression) {
                return value ? value
                    : expression
                    ? specialized_hir_unit_
                          ->evaluate_integral_expression(*expression)
                    : std::nullopt;
            };
            const auto left = boundary(reference.packed_range->left,
                reference.packed_range->left_expression);
            const auto right = boundary(reference.packed_range->right,
                reference.packed_range->right_expression);
            if (left && right) {
                const auto distance = index_distance(*left, *right);
                if (distance < std::numeric_limits<std::size_t>::max()) {
                    width = static_cast<std::size_t>(distance + 1U);
                }
            }
            if (!width) {
                return std::nullopt;
            }
        }
        if (!width) {
            const auto effective
                = type_resolver.effective_systemverilog_type(
                    reference, specialized_hir_unit_->scope());
            if (effective && *effective != reference) {
                return self(self, *effective);
            }
        }
        if (!width && reference.target.target.valid()
            && visiting.insert(reference.target.target.value()).second) {
            if (const auto binding
                = hir_systemverilog_type_parameter_binding(
                    reference.target.target)) {
                width = self(self, *binding);
            } else if (const auto definition
                = specialized_hir_unit_->find_type(
                    reference.target.target);
                definition && definition->systemverilog != nullptr) {
                const auto& source = *definition->systemverilog;
                if ((source.form
                        == semantic::sv::TypeForm::packed_structure
                        || source.form
                            == semantic::sv::TypeForm::unpacked_structure)
                    && !source.members.empty()) {
                    std::size_t aggregate_width { };
                    for (const auto& member : source.members) {
                        const auto member_width = self(self, member.type);
                        if (!member_width || *member_width == 0U
                            || *member_width
                                > std::numeric_limits<std::size_t>::max()
                                    - aggregate_width) {
                            aggregate_width = 0U;
                            break;
                        }
                        aggregate_width += *member_width;
                    }
                    if (aggregate_width != 0U) {
                        width = aggregate_width;
                    }
                } else if ((source.form
                        == semantic::sv::TypeForm::packed_union
                        || source.form
                            == semantic::sv::TypeForm::unpacked_union)
                    && !source.members.empty()) {
                    std::size_t aggregate_width { };
                    for (const auto& member : source.members) {
                        const auto member_width = self(self, member.type);
                        if (!member_width || *member_width == 0U) {
                            aggregate_width = 0U;
                            break;
                        }
                        aggregate_width = std::max(
                            aggregate_width, *member_width);
                    }
                    if (aggregate_width != 0U) {
                        width = aggregate_width;
                    }
                } else if (source.form
                        == semantic::sv::TypeForm::tagged_union
                    && !source.members.empty()) {
                    std::size_t payload_width { };
                    for (const auto& member : source.members) {
                        const auto member_width = self(self, member.type);
                        if (!member_width || *member_width == 0U) {
                            payload_width = 0U;
                            break;
                        }
                        payload_width = std::max(
                            payload_width, *member_width);
                    }
                    const auto tag_width = std::max<std::size_t>(
                        1U,
                        static_cast<std::size_t>(
                            std::bit_width(source.members.size() - 1U)));
                    if (payload_width != 0U
                        && tag_width
                            <= std::numeric_limits<std::size_t>::max()
                                - payload_width) {
                        width = payload_width + tag_width;
                    }
                } else {
                    width = self(self, source.base);
                }
            }
            visiting.erase(reference.target.target.value());
        }
        if (!width && reference.executable_width
            && *reference.executable_width != 0U
            && *reference.executable_width
                <= std::numeric_limits<std::size_t>::max()) {
            width = static_cast<std::size_t>(
                *reference.executable_width);
        }
        if (!width) {
            const auto spelling
                = std::string_view { reference.target.spelling };
            if (spelling == "bit" || spelling == "logic"
                || spelling == "reg") {
                width = 1U;
            } else if (spelling == "byte") {
                width = 8U;
            } else if (spelling == "shortint") {
                width = 16U;
            } else if (spelling == "int" || spelling == "integer") {
                width = 32U;
            } else if (spelling == "longint" || spelling == "time") {
                width = 64U;
            }
        }
        if (!width || reference.container_form != semantic::sv::TypeForm::static_array) {
            return width;
        }
        for (const auto& dimension : reference.unpacked_dimensions) {
            const auto left = dimension.left ? dimension.left
                : dimension.left_expression
                ? specialized_hir_unit_->evaluate_integral_expression(
                      *dimension.left_expression)
                : std::nullopt;
            const auto right = dimension.right ? dimension.right
                : dimension.right_expression
                ? specialized_hir_unit_->evaluate_integral_expression(
                      *dimension.right_expression)
                : std::nullopt;
            if (!left || !right) {
                return std::nullopt;
            }
            const auto count = index_distance(*left, *right) + 1U;
            if (count == 0U
                || count > std::numeric_limits<std::size_t>::max()
                        / *width) {
                return std::nullopt;
            }
            *width *= static_cast<std::size_t>(count);
        }
        return width;
    };
    return resolve(resolve, type);
}

std::optional<semantic::sv::TypeReference>
Lowerer::hir_systemverilog_type_parameter_binding(
    const semantic::TypeId type) const
{
    if (specialized_hir_unit_ == nullptr || !type.valid()) {
        return std::nullopt;
    }
    const auto definition = specialized_hir_unit_->find_type(type);
    const auto declaration = definition
            && definition->systemverilog != nullptr
        ? specialized_hir_unit_->find_declaration(
              definition->systemverilog->declaration)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || declaration->systemverilog->form
            != semantic::sv::DeclarationForm::type_parameter) {
        return std::nullopt;
    }
    semantic::sv::TypeReference reference;
    reference.target.target = type;
    const auto effective = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.effective_systemverilog_type(
        reference, specialized_hir_unit_->scope());
    return effective && *effective != reference
        ? effective
        : std::nullopt;
}

bool Lowerer::hir_systemverilog_type_four_state(
    const semantic::sv::TypeReference& type) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    std::unordered_set<std::uint32_t> visiting;
    const auto resolve = [&](const auto& self,
                             const semantic::sv::TypeReference& reference)
        -> bool {
        if (reference.four_state) {
            return true;
        }
        const auto effective = resolver.effective_systemverilog_type(
            reference, specialized_hir_unit_->scope());
        if (effective && *effective != reference) {
            return self(self, *effective);
        }
        if (!reference.target.target.valid()) {
            return false;
        }
        if (!visiting.insert(reference.target.target.value()).second) {
            return false;
        }
        bool result = false;
        if (const auto definition
            = specialized_hir_unit_->find_type(
                reference.target.target);
            definition && definition->systemverilog != nullptr) {
            const auto& declared = *definition->systemverilog;
            result = self(self, declared.base)
                || std::ranges::any_of(
                    declared.members,
                    [&](const auto& member) {
                        return self(self, member.type);
                    });
        }
        visiting.erase(reference.target.target.value());
        return result;
    };
    return resolve(resolve, type);
}

std::optional<Lowerer::HirSystemVerilogCastProfile>
Lowerer::hir_systemverilog_cast_profile(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    constexpr auto prefix = std::string_view { "@sv-cast:" };
    if (source.kind != semantic::sv::ExpressionKind::call
        || !source.text.starts_with(prefix)
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto type_name = std::string_view { source.text }.substr(
        prefix.size());
    if (type_name.empty()) {
        return std::nullopt;
    }

    const auto builtin = [&]()
        -> std::optional<HirSystemVerilogCastProfile> {
        if (type_name == "bit") {
            return HirSystemVerilogCastProfile {
                1U, frontend::ValueDomain::Bit2, false, std::nullopt
            };
        }
        if (type_name == "logic" || type_name == "reg") {
            return HirSystemVerilogCastProfile {
                1U, frontend::ValueDomain::Logic4, false, std::nullopt
            };
        }
        if (type_name == "byte" || type_name == "shortint"
            || type_name == "int" || type_name == "longint") {
            const auto width = type_name == "byte" ? 8U
                : type_name == "shortint"          ? 16U
                : type_name == "int"               ? 32U
                                                   : 64U;
            return HirSystemVerilogCastProfile {
                width, frontend::ValueDomain::Integer, true, std::nullopt
            };
        }
        if (type_name == "integer") {
            return HirSystemVerilogCastProfile {
                32U, frontend::ValueDomain::Logic4, true, std::nullopt
            };
        }
        if (type_name == "time") {
            return HirSystemVerilogCastProfile {
                64U, frontend::ValueDomain::Logic4, false, std::nullopt
            };
        }
        return std::nullopt;
    }();
    if (builtin) {
        return builtin;
    }
    // `signed'(x)` and `unsigned'(x)` change only the signedness, and
    // `const'(x)` nothing; the operand keeps its width (6.24.1).
    if (type_name == "signed" || type_name == "unsigned"
        || type_name == "const") {
        const auto operand = source.operands.front();
        const auto width = hir_expression_width(operand, hir_process_scope_);
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        return HirSystemVerilogCastProfile {
            *width,
            hir_expression_domain(operand, hir_process_scope_)
                .value_or(frontend::ValueDomain::Logic4),
            type_name == "const" ? hir_expression_signed(operand)
                                 : type_name == "signed",
            std::nullopt,
        };
    }

    // SystemVerilog sized casts use a decimal constant as the type spelling,
    // for example `3'(value)`.  The frontend retains that spelling in the
    // synthetic cast call, rather than manufacturing a declaration for it.
    if (const auto width = unsigned_decimal(type_name);
        width && *width != 0U
        && *width <= std::numeric_limits<std::size_t>::max()) {
        const auto operand = source.operands.front();
        return HirSystemVerilogCastProfile {
            static_cast<std::size_t>(*width),
            hir_expression_domain(operand, hir_process_scope_)
                .value_or(frontend::ValueDomain::Logic4),
            hir_expression_signed(operand),
            std::nullopt,
        };
    }

    const auto sized_cast = [&](const semantic::DeclarationId candidate)
        -> std::optional<HirSystemVerilogCastProfile> {
        const auto declaration = specialized_hir_unit_->find_declaration(
            candidate);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto form = declaration->systemverilog->form;
        if (form != semantic::sv::DeclarationForm::parameter
            && form != semantic::sv::DeclarationForm::local_parameter) {
            return std::nullopt;
        }
        const auto width = specialized_hir_unit_
                               ->evaluate_integral_declaration(candidate);
        if (!width || *width <= 0
            || static_cast<std::uint64_t>(*width)
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return HirSystemVerilogCastProfile {
            static_cast<std::size_t>(*width),
            hir_expression_domain(
                source.operands.front(), hir_process_scope_)
                .value_or(frontend::ValueDomain::Logic4),
            hir_expression_signed(source.operands.front()),
            std::nullopt,
        };
    };
    semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto resolve_cast_declaration
        = [&](const semantic::CompiledDeclarationPredicate& predicate) {
              if (source.referenced_name) {
                  auto name = *source.referenced_name;
                  name.spelling = type_name;
                  return resolver.resolve_systemverilog_name(
                      name, source.scope, predicate);
              }
              return resolver.resolve_systemverilog(
                  type_name, source.scope, predicate);
          };
    const auto size_declaration
        = [](const semantic::CompiledDeclarationView& candidate) {
              if (candidate.systemverilog == nullptr) {
                  return false;
              }
              using Form = semantic::sv::DeclarationForm;
              return candidate.systemverilog->form == Form::parameter
                  || candidate.systemverilog->form
                      == Form::local_parameter;
          };
    const auto size_resolution
        = resolve_cast_declaration(size_declaration);
    if (const auto declaration = size_resolution.unique()) {
        if (const auto profile = sized_cast(*declaration)) {
            return profile;
        }
    }

    const auto type_declaration
        = [](const semantic::CompiledDeclarationView& candidate) {
              if (candidate.systemverilog == nullptr) {
                  return false;
              }
              using Form = semantic::sv::DeclarationForm;
              const auto& record = *candidate.systemverilog;
              return (record.form == Form::type_parameter
                         || record.form == Form::typedef_declaration
                         || record.form == Form::nettype_declaration)
                  && (record.type || record.default_type);
          };
    const auto type_resolution
        = resolve_cast_declaration(type_declaration);
    const auto declaration = type_resolution.unique();
    if (!declaration) {
        return std::nullopt;
    }
    const auto view = specialized_hir_unit_->find_declaration(
        *declaration);
    if (!view || view->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& record = *view->systemverilog;
    auto type = record.type ? *record.type : *record.default_type;
    auto nominal_type = type.target.target.valid()
        ? std::optional { type.target.target }
        : std::nullopt;
    if (record.declared_type) {
        type.target.target = *record.declared_type;
        type.target.spelling = record.name;
        nominal_type = record.declared_type;
    }
    type = resolver.effective_systemverilog_type(type, source.scope)
               .value_or(type);
    const auto width = hir_systemverilog_type_width(type);
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    return HirSystemVerilogCastProfile {
        *width,
        hir_systemverilog_type_four_state(type)
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2,
        type.signed_value,
        nominal_type,
    };
}

std::optional<semantic::ExpressionId>
Lowerer::hir_systemverilog_packed_pattern_operand(
    const semantic::ExpressionId expression_id,
    const semantic::sv::TypeReference& target_type) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto packed_pattern = [](const semantic::sv::Expression& source) {
        return source.kind == semantic::sv::ExpressionKind::assignment_pattern
            || (source.kind == semantic::sv::ExpressionKind::call
                && source.text.starts_with("@sv-tagged:"));
    };
    const auto& source = *expression->systemverilog;
    if (packed_pattern(source)) {
        return expression_id;
    }
    if (!target_type.target.target.valid()
        || source.kind != semantic::sv::ExpressionKind::call
        || !source.text.starts_with("@sv-cast:")
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto operand = specialized_hir_unit_->find_expression(
        source.operands.front());
    const auto definition = specialized_hir_unit_->find_type(
        target_type.target.target);
    const auto cast = hir_systemverilog_cast_profile(expression_id);
    const auto target_width = hir_systemverilog_type_width(target_type);
    if (!operand || operand->systemverilog == nullptr
        || !packed_pattern(*operand->systemverilog)
        || !definition || definition->systemverilog == nullptr
        || !cast || !cast->nominal_type
        || *cast->nominal_type != target_type.target.target
        || !target_width || cast->width != *target_width) {
        return std::nullopt;
    }
    return source.operands.front();
}

std::optional<Lowerer::HirVhdlConversionProfile>
Lowerer::hir_vhdl_conversion_profile(
    const semantic::ExpressionId expression_id,
    const std::optional<std::size_t> contextual_width) const
{
    // Kept in the profile API for the expression-lowering call site, but the
    // VHDL type conversion's intrinsic result shape is not context-sized.
    static_cast<void>(contextual_width);
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    constexpr auto qualified_prefix
        = std::string_view { "@vhdl-qualified:" };
    if (source.kind != semantic::vhdl::ExpressionKind::call
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    auto type_name = std::string_view { source.text };
    if (type_name.starts_with(qualified_prefix)) {
        type_name.remove_prefix(qualified_prefix.size());
    }
    auto canonical = std::string { type_name };
    std::ranges::transform(
        canonical, canonical.begin(), [](const char character) {
            return static_cast<char>(std::tolower(
                static_cast<unsigned char>(character)));
        });
    if (canonical == "integer" || canonical == "natural"
        || canonical == "positive" || canonical == "time") {
        const auto width = static_cast<std::size_t>(
            vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                ? 64U
                : 32U);
        const auto maximum = width == 64U
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(
                  std::numeric_limits<std::int32_t>::max());
        return HirVhdlConversionProfile {
            width,
            frontend::ValueDomain::Integer,
            true,
            canonical == "natural"
                ? std::optional { frontend::IntegerRange {
                      0, maximum, false } }
                : canonical == "positive"
                ? std::optional { frontend::IntegerRange {
                      1, maximum, false } }
                : std::nullopt,
        };
    }
    if (canonical == "real" || canonical == "std.standard.real") {
        return HirVhdlConversionProfile {
            64U, frontend::ValueDomain::Bit2, true, std::nullopt
        };
    }
    const auto predefined_array_domain = [&]()
        -> std::optional<frontend::ValueDomain> {
        if (canonical == "bit_vector") {
            return frontend::ValueDomain::Bit2;
        }
        if (canonical == "std_logic_vector"
            || canonical == "std_ulogic_vector") {
            return frontend::ValueDomain::Logic9;
        }
        if (canonical == "signed" || canonical == "unsigned") {
            const auto operand_domain = hir_expression_domain(
                source.operands.front(), semantic::ScopeId { });
            if (operand_domain
                && (*operand_domain == frontend::ValueDomain::Bit2
                    || *operand_domain
                        == frontend::ValueDomain::Logic9)) {
                return operand_domain;
            }
            return frontend::ValueDomain::Logic9;
        }
        return std::nullopt;
    }();
    if (predefined_array_domain) {
        const auto operand_width = hir_expression_width(
            source.operands.front(), semantic::ScopeId { });
        // VHDL conversions to unconstrained predefined array types preserve
        // the operand's length; an enclosing comparison or assignment does
        // not resize the converted value.
        const auto width = operand_width;
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        return HirVhdlConversionProfile {
            *width, *predefined_array_domain, false, std::nullopt
        };
    }
    const auto selected = hir_vhdl_type_declaration(expression_id);
    if (!selected) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        *selected);
    if (!declaration || declaration->vhdl == nullptr
        || (declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::type
            && declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::subtype)) {
        return std::nullopt;
    }
    auto declared_subtype = declaration->vhdl->subtype;
    if (!declared_subtype && declaration->vhdl->declared_type) {
        declared_subtype.emplace();
        declared_subtype->type_mark.target
            = *declaration->vhdl->declared_type;
        declared_subtype->type_mark.spelling
            = declaration->vhdl->name;
    }
    auto subtype = declared_subtype
        ? hir_effective_vhdl_subtype(*declared_subtype)
        : std::nullopt;
    const auto declared_type = declaration->vhdl->declared_type
        ? specialized_hir_unit_->find_type(
              *declaration->vhdl->declared_type)
        : std::nullopt;
    const auto composite_definition = declared_type
            && declared_type->vhdl != nullptr
            && (declared_type->vhdl->form
                    == semantic::vhdl::TypeForm::array
                || declared_type->vhdl->form
                    == semantic::vhdl::TypeForm::record)
        ? declared_type->vhdl
        : nullptr;
    if (subtype && composite_definition != nullptr) {
        // A composite declaration's nominal subtype can name its base rather
        // than itself. Fill only missing executable layout fields from the
        // declaration's own definition while preserving that nominal target.
        if (subtype->domain == semantic::vhdl::ValueDomain::unknown) {
            subtype->domain = composite_definition->base.domain;
        }
        if (!subtype->executable_width
            || *subtype->executable_width == 0U) {
            subtype->executable_width
                = composite_definition->base.executable_width;
        }
    }
    const auto width = subtype ? vhdl_runtime_width(*subtype)
                               : std::nullopt;
    if (!subtype || !width || *width == 0U
        || !scalar_domain(vhdl_domain(subtype->domain))) {
        return std::nullopt;
    }
    return HirVhdlConversionProfile {
        *width,
        vhdl_domain(subtype->domain),
        subtype->signed_value
            || subtype->domain == semantic::vhdl::ValueDomain::integer,
        vhdl_integer_range(*subtype),
    };
}

std::optional<std::size_t>
Lowerer::hir_vhdl_expression_runtime_width(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    const auto width = hir_expression_width(expression_id, process_scope);
    if (width && *width != 0U) {
        return width;
    }
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto subtype = hir_vhdl_expression_subtype(expression_id);
    if (subtype
        && vhdl_null_array_subtype(
            *specialized_hir_unit_, *subtype)) {
        return std::size_t { };
    }
    auto effective = subtype
        ? hir_effective_vhdl_subtype(*subtype)
        : std::nullopt;
    if (!effective && subtype) {
        effective = subtype;
    }
    return effective
            && vhdl_null_array_subtype(
                *specialized_hir_unit_, *effective)
        ? std::optional<std::size_t> { 0U }
        : std::nullopt;
}

std::optional<Lowerer::HirVhdlStandardFunctionProfile>
Lowerer::hir_vhdl_standard_function_profile(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call
        || !expression->vhdl->referenced_name) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    auto name = std::string_view { source.text };
    const auto separator = name.find_last_of('.');
    if (separator != std::string_view::npos) {
        name.remove_prefix(separator + 1U);
    }
    const auto bit_conversion = name == "to_bit"
        || name == "to_bitvector" || name == "to_bit_vector"
        || name == "to_bv";
    const auto logic_conversion = name == "to_stdulogic"
        || name == "to_stdlogicvector"
        || name == "to_std_logic_vector" || name == "to_slv"
        || name == "to_stdulogicvector"
        || name == "to_std_ulogic_vector" || name == "to_sulv";
    const auto logic_mapping = name == "to_01" || name == "to_x01"
        || name == "to_x01z" || name == "to_ux01";
    const auto logic_unknown_predicate = name == "is_x";
    const auto reduction = name == "and_reduce" || name == "nand_reduce"
        || name == "or_reduce" || name == "nor_reduce"
        || name == "xor_reduce" || name == "xnor_reduce";
    const auto numeric_to_integer = name == "to_integer"
        || name == "conv_integer";
    const auto numeric_resize = name == "to_signed"
        || name == "to_unsigned" || name == "resize"
        || name == "conv_signed" || name == "conv_unsigned"
        || name == "conv_std_logic_vector" || name == "ext"
        || name == "sxt";
    const auto numeric_shift = name == "shift_left"
        || name == "shift_right" || name == "rotate_left"
        || name == "rotate_right" || name == "shl" || name == "shr";
    const auto psl_query
        = same_hir_identifier(source.text, "std.env.pslassertfailed", true)
        || same_hir_identifier(source.text, "std.env.psliscovered", true)
        || same_hir_identifier(
            source.text, "std.env.getpslcoverassert", true)
        || same_hir_identifier(
            source.text, "std.env.pslisassertcovered", true);
    if (!bit_conversion && !logic_conversion && !logic_mapping
        && !logic_unknown_predicate
        && !reduction && !numeric_to_integer && !numeric_resize
        && !numeric_shift && !psl_query) {
        return std::nullopt;
    }
    semantic::CompiledDesignResolver resolver { *specialized_hir_unit_ };
    if (!resolver.vhdl_standard_package_member_visible(
            *source.referenced_name, source.scope)) {
        return std::nullopt;
    }
    if ((bit_conversion || logic_conversion || logic_mapping
            || logic_unknown_predicate || reduction)
        && source.operands.empty()) {
        return std::nullopt;
    }
    if (((numeric_to_integer || logic_unknown_predicate)
            && source.operands.size() != 1U)
        || ((numeric_resize || numeric_shift)
            && source.operands.size() != 2U)
        || (psl_query && !source.operands.empty())) {
        return std::nullopt;
    }
    if (psl_query) {
        return HirVhdlStandardFunctionProfile {
            HirVhdlStandardFunctionKind::psl_query,
            name,
            1U,
            frontend::ValueDomain::Boolean,
            false,
        };
    }
    if (numeric_to_integer) {
        return HirVhdlStandardFunctionProfile {
            HirVhdlStandardFunctionKind::numeric_to_integer,
            name,
            static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_)),
            frontend::ValueDomain::Integer,
            true,
        };
    }
    if (logic_unknown_predicate) {
        return HirVhdlStandardFunctionProfile {
            HirVhdlStandardFunctionKind::logic_unknown_predicate,
            name,
            1U,
            frontend::ValueDomain::Boolean,
            false,
        };
    }
    if (numeric_resize) {
        auto requested = hir_constant_integer(source.operands[1]);
        if (!requested && active_hir_callable_
            && *active_hir_callable_ < hir_callable_frames_.size()) {
            const auto formal = hir_referenced_declaration(
                source.operands[1]);
            const auto& frame = hir_callable_frames_[
                *active_hir_callable_];
            const auto& bindings = frame.static_integer_bindings;
            const auto binding = formal
                ? std::ranges::find_if(
                      bindings,
                      [&](const auto& candidate) {
                          return candidate.first == *formal;
                      })
                : bindings.end();
            if (binding != bindings.end()) {
                // A statically known call actual may determine the result
                // shape of a local VHDL object, while the formal still has
                // its normal invocation register for value lowering.
                requested = binding->second;
            }
            if (!requested) {
                const auto static_formal_actual =
                    [&](const semantic::DeclarationId candidate)
                    -> std::optional<semantic::ExpressionId> {
                    const auto is_static_formal = std::ranges::any_of(
                        frame.static_integer_bindings,
                        [&](const auto& item) {
                            return item.first == candidate;
                        });
                    if (!is_static_formal) {
                        return std::nullopt;
                    }
                    auto body_formal = candidate;
                    const auto profile = std::ranges::find(
                        frame.profile_formals, candidate);
                    if (profile != frame.profile_formals.end()) {
                        const auto index = static_cast<std::size_t>(
                            profile - frame.profile_formals.begin());
                        if (index >= frame.formals.size()) {
                            return std::nullopt;
                        }
                        body_formal = frame.formals[index];
                    }
                    for (auto actual = frame.generic_bindings.rbegin();
                         actual != frame.generic_bindings.rend(); ++actual) {
                        if (actual->formal == body_formal
                            && actual->expression) {
                            return actual->expression;
                        }
                    }
                    return std::nullopt;
                };
                requested
                    = specialized_hir_unit_->evaluate_integral_expression(
                        source.operands[1], static_formal_actual);
            }
        }
        const auto signed_result = name == "to_signed"
            || name == "conv_signed" || name == "sxt"
            || (name == "conv_std_logic_vector"
                && hir_expression_signed(source.operands[0]))
            || (name == "resize"
                && hir_expression_signed(source.operands[0]));
        if (!requested || *requested <= 0
            || static_cast<std::uint64_t>(*requested)
                > std::numeric_limits<std::uint32_t>::max()) {
            // Keep recognized standard and Synopsys numeric conversions on
            // the intrinsic path even when their result size is malformed.
            // Returning no profile sends the call through ordinary overload
            // lowering, which loses the numeric-size diagnostic and can
            // classify signed and unsigned conversions inconsistently.
            return HirVhdlStandardFunctionProfile {
                HirVhdlStandardFunctionKind::numeric_resize,
                name,
                1U,
                frontend::ValueDomain::Logic9,
                signed_result,
                true,
            };
        }
        return HirVhdlStandardFunctionProfile {
            HirVhdlStandardFunctionKind::numeric_resize,
            name,
            static_cast<std::size_t>(*requested),
            frontend::ValueDomain::Logic9,
            signed_result,
        };
    }
    const auto operand_width = reduction
        ? hir_vhdl_expression_runtime_width(
              source.operands[0], hir_process_scope_)
        : hir_expression_width(
              source.operands[0], hir_process_scope_);
    if (!operand_width || (*operand_width == 0U && !reduction)) {
        return std::nullopt;
    }
    if (numeric_shift) {
        return HirVhdlStandardFunctionProfile {
            HirVhdlStandardFunctionKind::numeric_shift,
            name,
            *operand_width,
            hir_expression_domain(source.operands[0], hir_process_scope_)
                .value_or(frontend::ValueDomain::Logic9),
            hir_expression_signed(source.operands[0]),
        };
    }
    return HirVhdlStandardFunctionProfile {
        reduction ? HirVhdlStandardFunctionKind::reduction
        : bit_conversion ? HirVhdlStandardFunctionKind::bit_conversion
        : logic_conversion ? HirVhdlStandardFunctionKind::logic_conversion
                           : HirVhdlStandardFunctionKind::logic_mapping,
        name,
        reduction ? 1U : *operand_width,
        reduction ? frontend::ValueDomain::Logic9
        : bit_conversion ? frontend::ValueDomain::Bit2
                         : frontend::ValueDomain::Logic9,
        false,
    };
}

Lowerer::HirVhdlSynopsysNumericContext
Lowerer::hir_vhdl_synopsys_numeric_context(
    const semantic::ScopeId scope) const
{
    HirVhdlSynopsysNumericContext result;
    if (specialized_hir_unit_ == nullptr) {
        return result;
    }
    const auto& design = specialized_hir_unit_->design();
    const auto semantic_scope = std::ranges::find(
        design.semantics.scopes(), scope, &semantic::Scope::id);
    if (semantic_scope == design.semantics.scopes().end()) {
        return result;
    }
    const auto owner = std::ranges::find(
        design.vhdl_hir.units(), semantic_scope->unit,
        &semantic::vhdl::Unit::id);
    if (owner == design.vhdl_hir.units().end()) {
        return result;
    }
    std::unordered_set<std::uint32_t> visiting;
    const auto inspect = [&](const auto& self,
                             const semantic::vhdl::Unit& unit) -> void {
        if (!visiting.insert(unit.id.value()).second) {
            return;
        }
        for (const auto& item : unit.context) {
            for (const auto& selected : item.selected_names) {
                const auto spelling = selected.canonical.empty()
                    ? std::string_view { selected.spelling }
                    : std::string_view { selected.canonical };
                if (item.kind == semantic::vhdl::ContextKind::use_clause) {
                    const auto visible = [&](const std::string_view package) {
                        const auto position = spelling.find(package);
                        return position != std::string_view::npos
                            && (position == 0U
                                || spelling[position - 1U] == '.')
                            && (position + package.size() == spelling.size()
                                || spelling[position + package.size()] == '.');
                    };
                    result.signed_visible
                        = result.signed_visible
                        || visible("std_logic_signed");
                    result.unsigned_visible
                        = result.unsigned_visible
                        || visible("std_logic_unsigned");
                    continue;
                }
                if (item.kind
                    != semantic::vhdl::ContextKind::context_reference) {
                    continue;
                }
                auto context_name = spelling;
                const auto separator = context_name.find_last_of('.');
                if (separator != std::string_view::npos) {
                    context_name.remove_prefix(separator + 1U);
                }
                for (const auto& context : design.vhdl_hir.units()) {
                    if (context.kind == semantic::vhdl::UnitKind::context
                        && same_hir_identifier(
                            context.name, context_name, true)) {
                        self(self, context);
                    }
                }
            }
        }
        visiting.erase(unit.id.value());
    };
    inspect(inspect, *owner);
    return result;
}

std::optional<std::size_t> Lowerer::hir_systemverilog_member_offset(
    const semantic::sv::TypeDefinition& type,
    const std::size_t member_index) const
{
    if (member_index >= type.members.size()) {
        return std::nullopt;
    }
    if (type.form == semantic::sv::TypeForm::packed_union
        || type.form == semantic::sv::TypeForm::tagged_union) {
        return 0U;
    }
    if (type.form != semantic::sv::TypeForm::packed_structure) {
        return std::nullopt;
    }

    std::size_t offset { };
    for (auto index = member_index + 1U; index < type.members.size();
        ++index) {
        const auto width = hir_systemverilog_type_width(
            type.members[index].type);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::size_t>::max() - offset) {
            return std::nullopt;
        }
        offset += *width;
    }
    return offset;
}

std::optional<Lowerer::HirPackedMemberSelection>
Lowerer::hir_systemverilog_member_selection(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type) {
        return std::nullopt;
    }
    const auto& root = declaration->systemverilog->name;
    if (root.empty() || source.text.size() <= root.size()
        || !source.text.starts_with(root)
        || source.text[root.size()] != '.') {
        return std::nullopt;
    }

    auto remaining = std::string_view { source.text }.substr(
        root.size() + 1U);
    auto current = *declaration->systemverilog->type;
    std::size_t offset { };
    std::optional<HirPackedMemberSelection::TaggedMember> tagged;
    const semantic::CompiledDesignResolver type_resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    while (!remaining.empty()) {
        // Types from other units (an imported package typedef) keep only
        // their spelling across object linking.
        if (!current.target.target.valid()) {
            current = type_resolver.effective_systemverilog_type(
                current, declaration->systemverilog->scope)
                          .value_or(current);
        }
        if (!current.target.target.valid()) {
            return std::nullopt;
        }
        auto definition = specialized_hir_unit_->find_type(
            current.target.target);
        // A typedef of a structure (for example one imported from a
        // package) denotes the structure.
        std::unordered_set<std::uint32_t> aliases;
        while (definition && definition->systemverilog != nullptr
            && definition->systemverilog->form
                == semantic::sv::TypeForm::alias
            && definition->systemverilog->base.target.target.valid()
            && aliases.insert(definition->systemverilog->base.target
                                  .target.value()).second) {
            definition = specialized_hir_unit_->find_type(
                definition->systemverilog->base.target.target);
        }
        if (!definition || definition->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& declared = *definition->systemverilog;
        if (declared.form != semantic::sv::TypeForm::packed_structure
            && declared.form != semantic::sv::TypeForm::packed_union
            && declared.form != semantic::sv::TypeForm::tagged_union) {
            return std::nullopt;
        }
        const auto separator = remaining.find('.');
        const auto segment = remaining.substr(0U, separator);
        const auto member = std::ranges::find(
            declared.members, segment, &semantic::sv::PackedMember::name);
        if (member == declared.members.end()) {
            return std::nullopt;
        }
        const auto member_index = static_cast<std::size_t>(
            std::distance(declared.members.begin(), member));
        if (separator == std::string_view::npos
            && declared.form
                == semantic::sv::TypeForm::tagged_union) {
            std::size_t payload_width { };
            for (const auto& candidate : declared.members) {
                const auto candidate_width
                    = hir_systemverilog_type_width(candidate.type);
                if (!candidate_width || *candidate_width == 0U) {
                    return std::nullopt;
                }
                payload_width = std::max(
                    payload_width, *candidate_width);
            }
            const auto tag_width = std::max<std::size_t>(
                1U,
                static_cast<std::size_t>(
                    std::bit_width(declared.members.size() - 1U)));
            if (payload_width
                    > std::numeric_limits<std::size_t>::max()
                        - tag_width
                || offset
                    > std::numeric_limits<std::size_t>::max()
                        - payload_width - tag_width) {
                return std::nullopt;
            }
            tagged = HirPackedMemberSelection::TaggedMember {
                offset,
                payload_width + tag_width,
                payload_width,
                tag_width,
                member_index,
            };
        }
        const auto member_offset = hir_systemverilog_member_offset(
            declared, member_index);
        if (!member_offset
            || *member_offset
                > std::numeric_limits<std::size_t>::max() - offset) {
            return std::nullopt;
        }
        offset += *member_offset;
        const auto width = hir_systemverilog_type_width(member->type);
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        if (separator == std::string_view::npos) {
            const auto member_range = [&]()
                -> std::optional<HirPackedRange> {
                if (member->type.packed_range) {
                    const auto& range = *member->type.packed_range;
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
                    if (left && right
                        && index_distance(*left, *right) + 1U == *width
                        && (*left == *right
                            || range.descending == (*left > *right))) {
                        return HirPackedRange {
                            *left, *right, range.descending
                        };
                    }
                }
                if (*width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    return std::nullopt;
                }
                return HirPackedRange {
                    static_cast<std::int64_t>(*width - 1U), 0, true
                };
            }();
            return HirPackedMemberSelection {
                *declaration_id,
                offset,
                *width,
                member_range,
                hir_systemverilog_type_four_state(member->type)
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2,
                member->type.signed_value,
                std::move(tagged),
            };
        }
        current = member->type;
        remaining.remove_prefix(separator + 1U);
    }
    return std::nullopt;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_expression_subtype(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    if (const auto subtype
        = hir_vhdl_original_integer_generic_subtype(expression_id)) {
        return subtype;
    }
    if (const auto actual = hir_generic_actual(expression_id)) {
        return hir_vhdl_expression_subtype(*actual);
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.builtin_operator
        != semantic::vhdl::BuiltinOperatorIdentity::none) {
        using Operator = semantic::vhdl::BuiltinOperatorIdentity;
        using Kind = semantic::vhdl::ExpressionKind;
        const auto vector_identity = [](const auto identity) {
            return identity
                    == semantic::vhdl::BuiltinTypeIdentity::
                        ieee_std_logic_1164_std_logic_vector
                || identity
                    == semantic::vhdl::BuiltinTypeIdentity::
                        ieee_std_logic_1164_std_ulogic_vector;
        };
        const auto unary_not
            = source.kind == Kind::unary
            && source.operands.size() == 1U
            && source.builtin_operator == Operator::ieee_std_logic_1164_not
            && same_hir_identifier(source.text, "not", true);
        const auto binary_logic = source.kind == Kind::binary
            && source.operands.size() == 2U
            && ((source.builtin_operator
                    == Operator::ieee_std_logic_1164_and
                    && same_hir_identifier(source.text, "and", true))
                || (source.builtin_operator
                        == Operator::ieee_std_logic_1164_or
                    && same_hir_identifier(source.text, "or", true))
                || (source.builtin_operator
                        == Operator::ieee_std_logic_1164_nand
                    && same_hir_identifier(source.text, "nand", true))
                || (source.builtin_operator
                        == Operator::ieee_std_logic_1164_nor
                    && same_hir_identifier(source.text, "nor", true))
                || (source.builtin_operator
                        == Operator::ieee_std_logic_1164_xor
                    && same_hir_identifier(source.text, "xor", true))
                || (source.builtin_operator
                        == Operator::ieee_std_logic_1164_xnor
                    && same_hir_identifier(source.text, "xnor", true)));
        if (!unary_not && !binary_logic) {
            return std::nullopt;
        }
        const auto vector_operand = [&](const semantic::ExpressionId operand)
            -> std::optional<std::pair<
                semantic::vhdl::SubtypeIndication, std::size_t>> {
            const auto subtype = hir_vhdl_expression_subtype(operand);
            const auto width = hir_expression_width(
                operand, hir_process_scope_);
            const auto domain = hir_expression_domain(
                operand, hir_process_scope_);
            const auto range = hir_expression_range(
                operand, hir_process_scope_);
            if (!subtype || !vector_identity(subtype->builtin_type)
                || subtype->domain
                    != semantic::vhdl::ValueDomain::logic9
                || !width || *width == 0U
                || domain != frontend::ValueDomain::Logic9 || !range) {
                return std::nullopt;
            }
            const auto distance = index_distance(
                range->left, range->right);
            if (distance
                    == std::numeric_limits<std::uint64_t>::max()
                || distance + 1U != *width
                || (range->left != range->right
                    && range->descending
                        != (range->left > range->right))) {
                return std::nullopt;
            }
            return std::pair { *subtype, *width };
        };
        const auto left = vector_operand(source.operands.front());
        if (!left) {
            return std::nullopt;
        }
        if (binary_logic) {
            const auto right = vector_operand(source.operands.back());
            if (!right || right->second != left->second
                || right->first.builtin_type
                    != left->first.builtin_type) {
                return std::nullopt;
            }
        }
        if (left->second
            > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        auto result = left->first;
        result.domain = semantic::vhdl::ValueDomain::logic9;
        result.executable_width = left->second;
        result.unconstrained = false;
        result.signed_value = false;
        result.constraints.clear();
        semantic::vhdl::RangeConstraint normalized_range;
        normalized_range.kind
            = semantic::vhdl::RangeKind::array_index;
        normalized_range.left = 1;
        normalized_range.right = static_cast<std::int64_t>(left->second);
        normalized_range.descending = false;
        normalized_range.source = source.source;
        result.constraints.push_back(std::move(normalized_range));
        return result;
    }
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text == "@vhdl-dereference"
        && source.operands.size() == 1U) {
        const auto prefix = hir_vhdl_expression_subtype(
            source.operands.front());
        auto type_id = prefix
                && prefix->type_mark.target.valid()
            ? prefix->type_mark.target
            : semantic::TypeId { };
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid()
            && visited.insert(type_id.value()).second) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            if (type->vhdl->form
                    == semantic::vhdl::TypeForm::access
                && type->vhdl->designated_subtype) {
                return hir_effective_vhdl_subtype(
                    *type->vhdl->designated_subtype);
            }
            if (type->vhdl->form
                    != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form
                    != semantic::vhdl::TypeForm::alias) {
                break;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        return std::nullopt;
    }
    if (source.kind == semantic::vhdl::ExpressionKind::name) {
        const auto separator = source.text.find('.');
        const auto declaration_id = separator != std::string::npos
            ? hir_referenced_declaration(expression_id)
            : std::nullopt;
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        auto subtype = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
        const auto first_name = separator == std::string::npos
            ? std::string_view { source.text }
            : std::string_view { source.text }.substr(0U, separator);
        if (subtype && declaration && declaration->vhdl != nullptr
            && separator != std::string::npos
            && !same_hir_identifier(
                declaration->vhdl->name, first_name, true)) {
            // A linked package-selected name already identifies the final
            // declaration. Only walk dotted record members when the selected
            // declaration is the prefix object itself.
            return subtype;
        }
        auto remaining = separator == std::string::npos
            ? std::string_view { }
            : std::string_view { source.text }.substr(separator + 1U);
        while (subtype && !remaining.empty()) {
            auto type_id = subtype->type_mark.target;
            const semantic::vhdl::TypeDefinition* definition { };
            std::unordered_set<std::uint32_t> visiting;
            while (type_id.valid()
                && visiting.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    break;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::record) {
                    definition = type->vhdl;
                    break;
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    break;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            if (definition == nullptr) {
                subtype.reset();
                break;
            }
            const auto member_separator = remaining.find('.');
            const auto member_name = remaining.substr(0U, member_separator);
            const auto member = std::ranges::find_if(
                definition->record_elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return same_hir_identifier(
                        element.name, member_name, true);
                });
            if (member == definition->record_elements.end()) {
                subtype.reset();
                break;
            }
            subtype = hir_effective_vhdl_subtype(member->subtype);
            if (member_separator == std::string_view::npos) {
                remaining = { };
            } else {
                remaining.remove_prefix(member_separator + 1U);
            }
        }
        if (subtype && separator != std::string::npos
            && remaining.empty()) {
            return subtype;
        }
    }
    if (const auto selection = hir_vhdl_array_selection(expression_id)) {
        return selection->subtype;
    }
    if (const auto member = hir_vhdl_member_selection(expression_id)) {
        if (!member->index) {
            return member->subtype;
        }
        if (member->subtype.type_mark.target.valid()) {
            const auto array = vhdl_array_definition(
                *specialized_hir_unit_,
                member->subtype.type_mark.target);
            if (array != nullptr && array->element_subtype) {
                return hir_effective_vhdl_subtype(
                    *array->element_subtype);
            }
        }
        auto element = member->subtype;
        element.constraints.clear();
        element.executable_width = member->element_width;
        return element;
    }
    if ((source.kind == semantic::vhdl::ExpressionKind::index
            || source.kind == semantic::vhdl::ExpressionKind::slice)
        && !source.operands.empty()) {
        const auto base = hir_vhdl_expression_subtype(
            source.operands.front());
        if (!base || !base->type_mark.target.valid()) {
            return std::nullopt;
        }
        const auto array = vhdl_array_definition(
            *specialized_hir_unit_, base->type_mark.target);
        if (array == nullptr || !array->element_subtype) {
            return source.kind == semantic::vhdl::ExpressionKind::slice
                ? base
                : std::nullopt;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::slice) {
            if (source.operands.size() != 3U
                || (source.text != "to"
                    && source.text != "downto")) {
                return base;
            }
            auto selected = *base;
            semantic::vhdl::RangeConstraint constraint;
            constraint.kind = semantic::vhdl::RangeKind::array_index;
            constraint.left_expression = source.operands[1];
            constraint.right_expression = source.operands[2];
            constraint.left = hir_constant_integer(source.operands[1]);
            constraint.right = hir_constant_integer(source.operands[2]);
            constraint.descending = source.text == "downto";
            constraint.source = source.source;
            if (constraint.left && constraint.right) {
                constraint.null = constraint.descending
                    ? *constraint.left < *constraint.right
                    : *constraint.left > *constraint.right;
            }
            if (selected.constraints.empty()) {
                selected.constraints.push_back(constraint);
            } else {
                selected.constraints.front() = constraint;
            }
            if (constraint.null) {
                selected.executable_width = 0U;
            } else {
                const auto occurrence_range = hir_expression_range(
                    source.operands.front(), hir_process_scope_);
                const auto occurrence_width = hir_expression_width(
                    source.operands.front(), hir_process_scope_);
                if (constraint.left && constraint.right
                    && occurrence_range && occurrence_width) {
                    const auto occurrence_count = index_distance(
                                                      occurrence_range->left,
                                                      occurrence_range->right)
                        + 1U;
                    const auto selected_count = index_distance(
                                                    *constraint.left,
                                                    *constraint.right)
                        + 1U;
                    const auto stride
                        = *occurrence_width / occurrence_count;
                    if (occurrence_count != 0U
                        && *occurrence_width % occurrence_count == 0U
                        && stride != 0U
                        && selected_count
                            <= std::numeric_limits<std::uint64_t>::max()
                                / stride) {
                        selected.executable_width = selected_count
                            * stride;
                    }
                }
            }
            return selected;
        }
        if (base->constraints.size() > 1U) {
            auto reduced = *base;
            reduced.constraints.erase(reduced.constraints.begin());
            reduced.executable_width.reset();
            reduced.unconstrained = false;
            return hir_effective_vhdl_subtype(reduced);
        }
        return hir_effective_vhdl_subtype(
            *array->element_subtype);
    }
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && !source.operands.empty()
        && (source.text == "'val" || source.text == "'succ"
            || source.text == "'pred" || source.text == "'leftof"
            || source.text == "'rightof")) {
        return hir_vhdl_expression_subtype(source.operands.front());
    }
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.operands.size() == 1U) {
        const semantic::CompiledDeclarationPredicate type_declaration
            = [](const semantic::CompiledDeclarationView& candidate) {
                  if (candidate.vhdl == nullptr) {
                      return false;
                  }
                  using Form = semantic::vhdl::DeclarationForm;
                  const auto form = candidate.vhdl->form;
                  return form == Form::type || form == Form::subtype
                      || form == Form::generic_type;
              };
        semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        auto resolved = resolver.resolve_expression_name(
            expression_id, type_declaration);
        if (resolved.status
            == semantic::CompiledResolutionStatus::not_found) {
            semantic::vhdl::Name name;
            constexpr auto qualified_prefix
                = std::string_view { "@vhdl-qualified:" };
            auto spelling = std::string_view { source.text };
            if (spelling.starts_with(qualified_prefix)) {
                spelling.remove_prefix(qualified_prefix.size());
            }
            name.spelling = std::string { spelling };
            name.canonical = name.spelling;
            resolved = resolver.resolve_vhdl(
                name, source.scope, type_declaration);
        }
        if (const auto selected = resolved.unique()) {
            const auto declaration
                = specialized_hir_unit_->find_declaration(*selected);
            if (declaration && declaration->vhdl != nullptr) {
                semantic::vhdl::SubtypeIndication subtype;
                if (declaration->vhdl->subtype) {
                    subtype = *declaration->vhdl->subtype;
                }
                if (declaration->vhdl->declared_type) {
                    subtype.type_mark.target
                        = *declaration->vhdl->declared_type;
                    subtype.type_mark.spelling
                        = declaration->vhdl->name;
                }
                if (const auto effective
                    = hir_effective_vhdl_subtype(subtype)) {
                    return effective;
                }
            }
        }
    }
    if ((source.kind == semantic::vhdl::ExpressionKind::call
            || source.kind == semantic::vhdl::ExpressionKind::unary
            || source.kind == semantic::vhdl::ExpressionKind::binary)
        && source.referenced_name) {
        const auto resolution = resolve_hir_vhdl_function_call(
            expression_id, hir_process_scope_, 0U);
        if (resolution) {
            hir_generic_binding_frames_.push_back(
                resolution->generic_bindings);
            const auto declaration
                = specialized_hir_unit_->find_declaration(
                    resolution->body);
            const auto subtype = declaration
                    && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype
                ? hir_effective_vhdl_subtype(
                      *declaration->vhdl->subtype)
                : std::nullopt;
            hir_generic_binding_frames_.pop_back();
            if (subtype) {
                return subtype;
            }
        }
    }
    if (const auto selected = hir_referenced_declaration(expression_id)) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            *selected);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            auto subtype = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            const auto callable_range
                = hir_vhdl_callable_formal_range(*selected);
            if (subtype && callable_range && *callable_range) {
                const auto binding = hir_runtime_binding(
                    *selected, hir_process_scope_, false);
                const auto& packed_range = **callable_range;
                const auto distance = index_distance(
                    packed_range.left, packed_range.right);
                if (!binding || binding->width == 0U
                    || distance
                        == std::numeric_limits<std::uint64_t>::max()
                    || distance + 1U != binding->width
                    || (packed_range.left != packed_range.right
                        && packed_range.descending
                            != (packed_range.left > packed_range.right))) {
                    return std::nullopt;
                }
                semantic::vhdl::RangeConstraint constraint;
                constraint.kind
                    = semantic::vhdl::RangeKind::array_index;
                constraint.left = packed_range.left;
                constraint.right = packed_range.right;
                constraint.descending = packed_range.descending;
                const auto existing = std::ranges::find_if(
                    subtype->constraints,
                    [](const auto& candidate) {
                        return candidate.kind
                            == semantic::vhdl::RangeKind::array_index;
                    });
                if (existing == subtype->constraints.end()) {
                    subtype->constraints.insert(
                        subtype->constraints.begin(),
                        std::move(constraint));
                } else {
                    *existing = std::move(constraint);
                }
                subtype->unconstrained = false;
                subtype->executable_width = binding->width;
            }
            return subtype;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<semantic::vhdl::TypeForm, semantic::TypeId>>
Lowerer::hir_vhdl_composite_root_type(
    const semantic::ExpressionId expression_id) const
{
    const auto subtype = hir_vhdl_expression_subtype(expression_id);
    if (!subtype || !subtype->type_mark.target.valid()
        || specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    auto type_id = subtype->type_mark.target;
    std::unordered_set<std::uint32_t> visited;
    while (type_id.valid() && visited.insert(type_id.value()).second) {
        const auto type = specialized_hir_unit_->find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return std::nullopt;
        }
        if (type->vhdl->form == semantic::vhdl::TypeForm::record
            || type->vhdl->form == semantic::vhdl::TypeForm::array) {
            return std::pair { type->vhdl->form, type_id };
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            return std::nullopt;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    return std::nullopt;
}

std::optional<Lowerer::HirVhdlArraySelection>
Lowerer::hir_vhdl_array_selection(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto call = source.kind
        == semantic::vhdl::ExpressionKind::call;
    const auto indexed = source.kind
        == semantic::vhdl::ExpressionKind::index;
    if ((!call && !indexed) || source.operands.empty()
        || (indexed && source.operands.size() < 2U)
        || (call && !source.argument_names.empty()
            && (source.argument_names.size() != source.operands.size()
                || std::ranges::any_of(
                    source.argument_names,
                    [](const std::string& name) {
                        return !name.empty();
                    })))) {
        return std::nullopt;
    }
    const auto selectors = indexed
        ? std::span { source.operands }.subspan(1U)
        : std::span { source.operands };
    // A multi-dimensional assignment target is represented as successive
    // index expressions.  Carry the inner selection forward so the outer
    // expression consumes the next remaining dimension instead of applying
    // dimension zero again.  The inner selected subtype also naturally
    // switches to the element type for an array whose element is an array.
    const auto parent = indexed
        ? hir_vhdl_array_selection(source.operands.front())
        : std::nullopt;
    if (indexed && !parent
        && hir_vhdl_member_selection(source.operands.front())) {
        // An index of an array-valued record member must remain on the
        // member-selection path.  Reinterpreting it from the root object's
        // array subtype loses an earlier dynamic element selection, including
        // that selection's range check (for example,
        // Value(Index).Detail.Data(0)).
        return std::nullopt;
    }
    const auto declaration_id = parent
        ? std::optional { parent->declaration }
        : indexed
        ? hir_target_declaration(source.operands.front())
        : hir_referenced_declaration(expression_id);
    const auto callable_range = !parent && declaration_id
        ? hir_vhdl_callable_formal_range(*declaration_id)
        : std::nullopt;
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    using Form = semantic::vhdl::DeclarationForm;
    const auto object = declaration && declaration->vhdl != nullptr
        && (declaration->vhdl->form == Form::generic_constant
            || declaration->vhdl->form == Form::port
            || declaration->vhdl->form == Form::signal
            || declaration->vhdl->form == Form::constant
            || declaration->vhdl->form == Form::variable
            || declaration->vhdl->form == Form::alias)
        && declaration->vhdl->subtype;
    auto subtype = parent
        ? std::optional { parent->subtype }
        : object
        ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
        : std::nullopt;
    if (callable_range) {
        if (!*callable_range || !subtype) {
            return std::nullopt;
        }
        const auto binding = hir_runtime_binding(
            *declaration_id, hir_process_scope_, false);
        const auto& packed_range = **callable_range;
        const auto distance = index_distance(
            packed_range.left, packed_range.right);
        if (!binding || binding->width == 0U
            || distance == std::numeric_limits<std::uint64_t>::max()
            || distance + 1U != binding->width
            || (packed_range.left != packed_range.right
                && packed_range.descending
                    != (packed_range.left > packed_range.right))) {
            return std::nullopt;
        }
        semantic::vhdl::RangeConstraint constraint;
        constraint.kind = semantic::vhdl::RangeKind::array_index;
        constraint.left = packed_range.left;
        constraint.right = packed_range.right;
        constraint.descending = packed_range.descending;
        const auto existing = std::ranges::find_if(
            subtype->constraints,
            [](const auto& candidate) {
                return candidate.kind
                    == semantic::vhdl::RangeKind::array_index;
            });
        if (existing == subtype->constraints.end()) {
            subtype->constraints.insert(
                subtype->constraints.begin(), std::move(constraint));
        } else {
            *existing = std::move(constraint);
        }
        subtype->unconstrained = false;
        subtype->executable_width = binding->width;
    }
    const auto binding = declaration_id
        ? hir_runtime_binding(*declaration_id, hir_process_scope_, false)
        : std::nullopt;
    const VhdlArrayMetadata* runtime_array = nullptr;
    if (!parent && binding && binding->signal
        && *binding->signal < design_.signal_info_.size()) {
        runtime_array
            = design_.signal_info_[*binding->signal].vhdl_array.get();
    }
    const auto* array = subtype && subtype->type_mark.target.valid()
        ? vhdl_array_definition(
              *specialized_hir_unit_, subtype->type_mark.target)
        : nullptr;
    const auto builtin_logic_vector
        = subtype
            && (subtype->builtin_type
                    == semantic::vhdl::BuiltinTypeIdentity::
                        ieee_std_logic_1164_std_logic_vector
                || subtype->builtin_type
                    == semantic::vhdl::BuiltinTypeIdentity::
                        ieee_std_logic_1164_std_ulogic_vector);
    const auto predefined_domain = builtin_logic_vector
        ? std::optional { semantic::vhdl::ValueDomain::logic9 }
        : subtype && !subtype->type_mark.target.valid()
        ? vhdl_predefined_packed_vector_domain(
              subtype->type_mark.spelling)
        : std::nullopt;
    const auto predefined_array = array == nullptr && predefined_domain;
    const auto dimension_count = subtype
            && (array != nullptr || predefined_array)
        ? runtime_array != nullptr
                && !runtime_array->dimensions.empty()
            ? runtime_array->dimensions.size()
            : !subtype->constraints.empty()
            ? subtype->constraints.size()
            : array != nullptr
            ? array->array_dimensions.size()
            : 1U
        : 0U;
    auto selected_root_width = subtype ? vhdl_runtime_width(*subtype)
                                       : std::nullopt;
    if (!parent && binding && binding->width != 0U) {
        // Runtime storage is authoritative even when older or partially
        // materialized signal metadata has no VHDL-array description.  A
        // definition-level executable width can describe another constrained
        // occurrence of the same array type and must never be used to form a
        // slice offset into this signal.
        selected_root_width = binding->width;
    }
    const auto root_width = parent
        ? std::optional { parent->root_width }
        : selected_root_width;
    if ((!predefined_array
            && (array == nullptr || !array->element_subtype))
        || selectors.size() > dimension_count
        || !root_width || *root_width == 0U
        || !selected_root_width || *selected_root_width == 0U) {
        return std::nullopt;
    }
    const auto constraint_bound = [&](
                                      const std::optional<std::int64_t> value,
                                      const std::optional<
                                          semantic::ExpressionId> residual) {
        return value ? value
            : residual ? hir_constant_integer(*residual)
                       : std::nullopt;
    };
    auto offset = parent ? parent->offset : std::size_t { };
    auto selected_width = *selected_root_width;
    std::optional<semantic::vhdl::RangeConstraint> retained_range;
    auto dynamic_indices = parent
        ? parent->dynamic_indices
        : std::vector<HirVhdlArrayIndex> { };
    for (std::size_t index = 0U;
        index < selectors.size(); ++index) {
        const semantic::vhdl::RangeConstraint* range = nullptr;
        std::optional<semantic::vhdl::RangeConstraint> occurrence_range;
        if (runtime_array != nullptr
            && index < runtime_array->dimensions.size()
            && runtime_array->dimensions[index].range) {
            const auto& dimension = runtime_array->dimensions[index];
            occurrence_range.emplace();
            occurrence_range->kind
                = semantic::vhdl::RangeKind::array_index;
            occurrence_range->left = dimension.range->left;
            occurrence_range->right = dimension.range->right;
            occurrence_range->descending
                = dimension.range->descending;
            occurrence_range->null = dimension.null;
            range = &*occurrence_range;
        } else if (index < subtype->constraints.size()) {
            range = &subtype->constraints[index];
        } else if (array != nullptr
            && index < array->array_dimensions.size()
            && array->array_dimensions[index].constraint) {
            range = &*array->array_dimensions[index].constraint;
        }
        const auto left = range != nullptr
            ? constraint_bound(range->left, range->left_expression)
            : std::nullopt;
        const auto right = range != nullptr
            ? constraint_bound(range->right, range->right_expression)
            : std::nullopt;
        const auto count = left && right && !range->null
            ? index_distance(*left, *right) + 1U
            : 0U;
        if (!left || !right || count == 0U
            || count > selected_width || selected_width % count != 0U
            || (*left != *right && !range->direction_from_bounds
                && range->descending != (*left > *right))) {
            return std::nullopt;
        }
        const auto range_descending
            = range->direction_from_bounds && *left != *right
            ? *left > *right
            : range->descending;
        // An occurrence layout is authoritative for an aliased unconstrained
        // port.  Prefer its flattened stride instead of re-deriving one from
        // definition-level constraints, but only when the stride describes
        // this remaining flattened width exactly.
        const auto derived_element_width = selected_width
            / static_cast<std::size_t>(count);
        auto element_width = derived_element_width;
        if (runtime_array != nullptr
            && index < runtime_array->dimensions.size()) {
            const auto occurrence_stride
                = runtime_array->dimensions[index].stride;
            if (occurrence_stride != 0U
                && occurrence_stride
                    <= std::numeric_limits<std::size_t>::max()
                && count
                    <= std::numeric_limits<std::size_t>::max()
                        / occurrence_stride
                && static_cast<std::size_t>(count)
                        * static_cast<std::size_t>(occurrence_stride)
                    == selected_width) {
                element_width = static_cast<std::size_t>(
                    occurrence_stride);
            }
        }
        const auto operand = specialized_hir_unit_->find_expression(
            selectors[index]);
        const auto range_operand = operand && operand->vhdl != nullptr
                && operand->vhdl->kind
                    == semantic::vhdl::ExpressionKind::binary
                && (operand->vhdl->text == "to"
                    || operand->vhdl->text == "downto")
                && operand->vhdl->operands.size() == 2U
            ? operand->vhdl
            : nullptr;
        if (range_operand != nullptr) {
            const auto selected_left = hir_constant_integer(
                range_operand->operands.front());
            const auto selected_right = hir_constant_integer(
                range_operand->operands.back());
            const auto selected_descending
                = range_operand->text == "downto";
            if (index + 1U != selectors.size()
                || !selected_left || !selected_right
                || (*selected_left != *selected_right
                    && selected_descending != range_descending)
                || *selected_left < std::min(*left, *right)
                || *selected_left > std::max(*left, *right)
                || *selected_right < std::min(*left, *right)
                || *selected_right > std::max(*left, *right)) {
                return std::nullopt;
            }
            const auto selected_count = index_distance(
                *selected_left, *selected_right) + 1U;
            if (selected_count
                > std::numeric_limits<std::size_t>::max()
                    / element_width) {
                return std::nullopt;
            }
            selected_width = static_cast<std::size_t>(selected_count)
                * element_width;
            const auto ordinal = index_distance(
                *selected_right, *right);
            if (ordinal
                    > std::numeric_limits<std::size_t>::max()
                        / element_width
                || static_cast<std::size_t>(ordinal) * element_width
                    > std::numeric_limits<std::size_t>::max() - offset) {
                return std::nullopt;
            }
            offset += static_cast<std::size_t>(ordinal) * element_width;
            retained_range.emplace();
            retained_range->kind
                = semantic::vhdl::RangeKind::array_index;
            retained_range->left = *selected_left;
            retained_range->right = *selected_right;
            retained_range->descending = selected_descending;
            retained_range->source = range_operand->source;
            continue;
        }
        if (const auto selected = hir_constant_integer(
                selectors[index])) {
            if (*selected < std::min(*left, *right)
                || *selected > std::max(*left, *right)) {
                return std::nullopt;
            }
            const auto ordinal = index_distance(*selected, *right);
            if (ordinal
                    > std::numeric_limits<std::size_t>::max()
                        / element_width
                || static_cast<std::size_t>(ordinal) * element_width
                    > std::numeric_limits<std::size_t>::max() - offset) {
                return std::nullopt;
            }
            offset += static_cast<std::size_t>(ordinal) * element_width;
        } else {
            const auto index_width = hir_expression_width(
                selectors[index], hir_process_scope_);
            const auto index_domain = hir_expression_domain(
                selectors[index], hir_process_scope_);
            if (!index_width
                || (((*index_width != 32U && *index_width != 64U)
                        || index_domain != frontend::ValueDomain::Integer)
                    && !hir_vhdl_enumeration_index(
                        selectors[index], hir_process_scope_))
                || element_width
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            dynamic_indices.push_back(HirVhdlArrayIndex {
                selectors[index],
                *left,
                *right,
                element_width,
                range_descending,
            });
        }
        selected_width = element_width;
    }

    if (offset > *root_width
        || selected_width > *root_width - offset) {
        return std::nullopt;
    }

    semantic::vhdl::SubtypeIndication selected_subtype;
    std::optional<semantic::vhdl::SubtypeIndication>
        predefined_array_element;
    if (predefined_domain) {
        predefined_array_element.emplace();
        predefined_array_element->domain = *predefined_domain;
        predefined_array_element->executable_width = 1U;
        predefined_array_element->type_mark.spelling
            = *predefined_domain == semantic::vhdl::ValueDomain::bit2
            ? "bit"
            : "std_logic";
    }
    if (selectors.size() == dimension_count && !retained_range) {
        selected_subtype = array != nullptr
                && array->element_subtype
            ? *array->element_subtype
            : predefined_array_element
            ? *predefined_array_element
            : semantic::vhdl::SubtypeIndication { };
        if (array == nullptr && !predefined_array_element) {
            return std::nullopt;
        }
    } else {
        selected_subtype = *subtype;
        selected_subtype.constraints.clear();
        if (retained_range) {
            selected_subtype.constraints.push_back(*retained_range);
        }
        for (std::size_t index = selectors.size();
            index < dimension_count; ++index) {
            if (runtime_array != nullptr
                && index < runtime_array->dimensions.size()
                && runtime_array->dimensions[index].range) {
                const auto& dimension
                    = runtime_array->dimensions[index];
                semantic::vhdl::RangeConstraint occurrence_range;
                occurrence_range.kind
                    = semantic::vhdl::RangeKind::array_index;
                occurrence_range.left = dimension.range->left;
                occurrence_range.right = dimension.range->right;
                occurrence_range.descending
                    = dimension.range->descending;
                occurrence_range.null = dimension.null;
                selected_subtype.constraints.push_back(
                    std::move(occurrence_range));
            } else if (index < subtype->constraints.size()) {
                selected_subtype.constraints.push_back(
                    subtype->constraints[index]);
            } else if (array != nullptr
                && index < array->array_dimensions.size()
                && array->array_dimensions[index].constraint) {
                selected_subtype.constraints.push_back(
                    *array->array_dimensions[index].constraint);
            } else {
                return std::nullopt;
            }
        }
    }
    selected_subtype.executable_width = selected_width;
    auto effective_selected = hir_effective_vhdl_subtype(
        selected_subtype);
    if (!effective_selected) {
        return std::nullopt;
    }
    effective_selected->executable_width = selected_width;
    const auto domain = scalar_domain(vhdl_domain(
            effective_selected->domain))
        ? vhdl_domain(effective_selected->domain)
        : binding ? binding->domain : frontend::ValueDomain::Unknown;
    if (!scalar_domain(domain)) {
        return std::nullopt;
    }
    const auto signed_value = effective_selected->signed_value
        || effective_selected->domain
            == semantic::vhdl::ValueDomain::integer;
    return HirVhdlArraySelection {
        *declaration_id,
        std::move(*effective_selected),
        *root_width,
        offset,
        selected_width,
        domain,
        signed_value,
        std::move(dynamic_indices),
    };
}

std::optional<Lowerer::HirVhdlMemberSelection>
Lowerer::hir_vhdl_member_selection(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    constexpr auto prefix = std::string_view { "@vhdl-member:" };
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (const auto selection = hir_vhdl_array_selection(expression_id);
        selection && selection->dynamic_indices.empty()) {
        return HirVhdlMemberSelection {
            selection->declaration,
            semantic::ExpressionId { },
            selection->subtype,
            selection->offset,
            selection->width,
            selection->domain,
            selection->signed_value,
            std::nullopt,
            0U,
        };
    }
    const bool indexed_member
        = source.kind == semantic::vhdl::ExpressionKind::call
        && source.operands.size() == 1U
        && source.text.find('.') != std::string::npos;
    if (source.kind == semantic::vhdl::ExpressionKind::name
        || indexed_member) {
        const auto separator = source.text.find('.');
        const auto declaration_id = separator != std::string::npos
            ? hir_referenced_declaration(expression_id)
            : std::nullopt;
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return std::nullopt;
        }
        auto subtype = hir_effective_vhdl_subtype(
            *declaration->vhdl->subtype);
        auto remaining = std::string_view { source.text }.substr(
            separator + 1U);
        std::size_t total_offset { };
        if (!indexed_member
            && remaining.find('.') == std::string_view::npos
            && subtype) {
            if (auto predefined = vhdl_environment_time_record_member(
                    *specialized_hir_unit_, *subtype,
                    declaration->vhdl->scope, remaining)) {
                return HirVhdlMemberSelection {
                    *declaration_id,
                    semantic::ExpressionId { },
                    std::move(predefined->subtype),
                    predefined->offset,
                    predefined->width,
                    predefined->domain,
                    predefined->signed_value,
                    std::nullopt,
                    0U,
                };
            }
        }
        while (subtype && !remaining.empty()) {
            if (!subtype->type_mark.target.valid()) {
                return std::nullopt;
            }
            const auto definition = specialized_hir_unit_->find_type(
                subtype->type_mark.target);
            if (!definition || definition->vhdl == nullptr
                || definition->vhdl->form
                    != semantic::vhdl::TypeForm::record) {
                return std::nullopt;
            }
            const auto member_separator = remaining.find('.');
            const auto member_name = remaining.substr(0U, member_separator);
            const auto& elements = definition->vhdl->record_elements;
            const auto selected = std::ranges::find_if(
                elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return same_hir_identifier(
                        element.name, member_name, true);
                });
            if (selected == elements.end()) {
                return std::nullopt;
            }
            std::size_t member_offset { };
            for (auto element = std::next(selected);
                element != elements.end(); ++element) {
                const auto following = hir_effective_vhdl_subtype(
                    element->subtype);
                const auto width = following
                    ? vhdl_runtime_width(*following)
                    : std::nullopt;
                if (!width
                    || *width
                        > std::numeric_limits<std::size_t>::max()
                            - member_offset) {
                    return std::nullopt;
                }
                member_offset += *width;
            }
            if (member_offset
                > std::numeric_limits<std::size_t>::max()
                    - total_offset) {
                return std::nullopt;
            }
            total_offset += member_offset;
            subtype = hir_effective_vhdl_subtype(selected->subtype);
            if (member_separator == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(member_separator + 1U);
        }
        const auto width = subtype ? vhdl_runtime_width(*subtype)
                                   : std::nullopt;
        const auto packed_domain = [&](const auto& self,
                                       const semantic::vhdl::SubtypeIndication&
                                           candidate,
                                       std::unordered_set<std::uint32_t>&
                                           visiting)
            -> frontend::ValueDomain {
            const auto effective = hir_effective_vhdl_subtype(candidate);
            if (!effective) {
                return frontend::ValueDomain::Unknown;
            }
            const auto direct = vhdl_domain(effective->domain);
            if (scalar_domain(direct)) {
                return direct;
            }
            if (!effective->type_mark.target.valid()
                || !visiting.insert(
                        effective->type_mark.target.value()).second) {
                return frontend::ValueDomain::Unknown;
            }
            const auto type = specialized_hir_unit_->find_type(
                effective->type_mark.target);
            if (!type || type->vhdl == nullptr) {
                visiting.erase(effective->type_mark.target.value());
                return frontend::ValueDomain::Unknown;
            }
            const auto& definition = *type->vhdl;
            auto result = frontend::ValueDomain::Unknown;
            const auto merge = [&](const frontend::ValueDomain domain) {
                if (!scalar_domain(domain)) {
                    return false;
                }
                if (result == frontend::ValueDomain::Unknown) {
                    result = domain;
                    return true;
                }
                return result == domain;
            };
            bool valid = true;
            if (definition.form
                == semantic::vhdl::TypeForm::record) {
                for (const auto& element : definition.record_elements) {
                    if (!merge(self(
                            self, element.subtype, visiting))) {
                        valid = false;
                        break;
                    }
                }
            } else if (definition.form
                    == semantic::vhdl::TypeForm::array
                && definition.element_subtype) {
                valid = merge(self(
                    self, *definition.element_subtype, visiting));
            } else if (definition.form
                    == semantic::vhdl::TypeForm::subtype
                || definition.form
                    == semantic::vhdl::TypeForm::alias) {
                valid = merge(self(self, definition.base, visiting));
            } else {
                valid = false;
            }
            visiting.erase(effective->type_mark.target.value());
            return valid ? result : frontend::ValueDomain::Unknown;
        };
        std::unordered_set<std::uint32_t> domain_visiting;
        const auto selected_domain = subtype
            ? packed_domain(packed_domain, *subtype, domain_visiting)
            : frontend::ValueDomain::Unknown;
        if (!subtype || !width || *width == 0U
            || !scalar_domain(selected_domain)) {
            return std::nullopt;
        }
        std::size_t element_width { };
        if (indexed_member) {
            const auto range = subtype->constraints.empty()
                ? nullptr
                : &subtype->constraints.front();
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
            const auto count = left && right && !range->null
                ? std::optional {
                      index_distance(*left, *right) + 1U
                  }
                : std::nullopt;
            if (!count || *count == 0U
                || *count > *width
                || *width % *count != 0U) {
                return std::nullopt;
            }
            element_width = *width / static_cast<std::size_t>(*count);
        }
        return HirVhdlMemberSelection {
            *declaration_id,
            semantic::ExpressionId { },
            *subtype,
            total_offset,
            *width,
            selected_domain,
            subtype->signed_value
                || subtype->domain
                    == semantic::vhdl::ValueDomain::integer,
            indexed_member
                ? std::optional { source.operands.front() }
                : std::nullopt,
            element_width,
        };
    }
    if (source.kind != semantic::vhdl::ExpressionKind::call
        || !expression->vhdl->text.starts_with(prefix)
        || expression->vhdl->operands.size() != 1U) {
        return std::nullopt;
    }
    const auto member_name = std::string_view {
        expression->vhdl->text
    }
                                 .substr(prefix.size());
    if (member_name.empty()) {
        return std::nullopt;
    }
    const auto base_expression = expression->vhdl->operands.front();
    const auto base_subtype = hir_vhdl_expression_subtype(
        base_expression);
    if (!base_subtype) {
        return std::nullopt;
    }
    if (auto predefined = vhdl_environment_time_record_member(
            *specialized_hir_unit_, *base_subtype,
            expression->vhdl->scope, member_name)) {
        const auto declaration = hir_target_declaration(base_expression);
        // A selected TIME_RECORD function result has no object declaration;
        // its root expression supplies the complete 515-bit value directly.
        // Object-backed selections retain their declaration for local/signal
        // binding, while expression-backed selections deliberately carry an
        // invalid declaration that is never consulted when `root` is valid.
        return HirVhdlMemberSelection {
            declaration.value_or(semantic::DeclarationId { }),
            base_expression,
            std::move(predefined->subtype),
            predefined->offset,
            predefined->width,
            predefined->domain,
            predefined->signed_value,
            std::nullopt,
            0U,
        };
    }
    if (!base_subtype->type_mark.target.valid()) {
        return std::nullopt;
    }
    const auto definition = specialized_hir_unit_->find_type(
        base_subtype->type_mark.target);
    if (!definition || definition->vhdl == nullptr
        || definition->vhdl->form != semantic::vhdl::TypeForm::record) {
        return std::nullopt;
    }
    const auto& elements = definition->vhdl->record_elements;
    const auto selected = std::ranges::find_if(
        elements,
        [&](const semantic::vhdl::RecordElement& element) {
            return same_hir_identifier(
                element.name, member_name, true);
        });
    if (selected == elements.end()) {
        return std::nullopt;
    }
    const auto subtype = hir_effective_vhdl_subtype(
        selected->subtype);
    const auto width = subtype ? vhdl_runtime_width(*subtype)
                               : std::nullopt;
    if (!subtype || !width || *width == 0U
        || !scalar_domain(vhdl_domain(subtype->domain))) {
        return std::nullopt;
    }
    std::size_t offset { };
    for (auto element = std::next(selected); element != elements.end();
        ++element) {
        const auto following = hir_effective_vhdl_subtype(
            element->subtype);
        const auto following_width = following
            ? vhdl_runtime_width(*following)
            : std::nullopt;
        if (!following_width
            || *following_width
                > std::numeric_limits<std::size_t>::max() - offset) {
            return std::nullopt;
        }
        offset += *following_width;
    }

    semantic::ExpressionId root = base_expression;
    auto total_offset = offset;
    std::optional<semantic::DeclarationId> declaration;
    if (const auto parent = hir_vhdl_member_selection(base_expression)) {
        root = parent->root;
        declaration = parent->declaration;
        if (parent->offset
            > std::numeric_limits<std::size_t>::max() - total_offset) {
            return std::nullopt;
        }
        total_offset += parent->offset;
    } else {
        declaration = hir_target_declaration(base_expression);
    }
    if (!declaration) {
        return std::nullopt;
    }
    return HirVhdlMemberSelection {
        *declaration,
        root,
        *subtype,
        total_offset,
        *width,
        vhdl_domain(subtype->domain),
        subtype->signed_value
            || subtype->domain == semantic::vhdl::ValueDomain::integer,
        std::nullopt,
        0U,
    };
}

std::optional<Lowerer::HirCasePatternBinding>
Lowerer::hir_case_pattern_binding(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr
        || hir_case_pattern_binding_frames_.empty()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto name = std::string_view {
        expression->systemverilog->text
    };
    for (auto frame = hir_case_pattern_binding_frames_.rbegin();
        frame != hir_case_pattern_binding_frames_.rend(); ++frame) {
        const auto binding = std::ranges::find(
            *frame, name, &HirCasePatternBinding::name);
        if (binding != frame->end()) {
            return *binding;
        }
    }
    return std::nullopt;
}

bool Lowerer::can_lower_hir_expression(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>& visiting) const
{
    if (const auto actual = hir_generic_actual(expression_id)) {
        return can_lower_hir_expression(*actual, process_scope, visiting);
    }
    if (const auto actual = hir_let_actual(expression_id)) {
        return can_lower_hir_expression(*actual, process_scope, visiting);
    }
    if (const auto declaration = hir_let_declaration(expression_id)) {
        if (!expression_id.valid()
            || !visiting.insert(expression_id.value()).second) {
            return false;
        }
        if (!push_hir_let_frame(expression_id, *declaration)) {
            visiting.erase(expression_id.value());
            return false;
        }
        const auto supported = can_lower_hir_expression(
            declaration->expression, process_scope, visiting);
        hir_let_frames_.pop_back();
        visiting.erase(expression_id.value());
        return supported;
    }
    if (specialized_hir_unit_ == nullptr || !expression_id.valid()) {
        return false;
    }
    const bool vhdl_reflection
        = is_hir_vhdl_reflection_expression(expression_id);
    const bool vhdl_environment
        = is_hir_vhdl_environment_expression(expression_id);
    if ((!vhdl_reflection && !vhdl_environment
            && hir_expression_is_residual(expression_id))
        || !visiting.insert(expression_id.value()).second) {
        return false;
    }
    const auto erase = [&] { visiting.erase(expression_id.value()); };
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        erase();
        return false;
    }
    const auto expression_supported = [&](const semantic::ExpressionId child) {
        return can_lower_hir_expression(child, process_scope, visiting);
    };
    if (const auto element = hir_container_element_binding(expression_id)) {
        const auto supported = element->width
                <= std::numeric_limits<std::uint32_t>::max()
            && std::ranges::all_of(
                element->indices,
                [&](const auto index) {
                    return hir_constant_integer(index).has_value()
                        || expression_supported(index);
                });
        erase();
        return supported;
    }
    const auto compound_supported = [&]<typename Range>(
                                        const Range& operands,
                                        const bool index,
                                        const bool slice,
                                        const bool concatenation,
                                        const bool replication,
                                        const bool conditional,
                                        const bool vhdl) {
        if (index || slice) {
            const auto expected = index ? 2U : 3U;
            const auto selection = operands.size() == expected
                ? hir_constant_selection(expression_id, process_scope)
                : std::nullopt;
            const auto dynamic_index = index && operands.size() == expected
                && !selection
                && hir_dynamic_index_supported(
                    operands[0], operands[1], process_scope);
            const auto dynamic_part = slice && operands.size() == expected
                ? hir_dynamic_part_width(expression_id, process_scope)
                : std::nullopt;
            const auto dynamic_index_width = dynamic_index
                ? vhdl
                    ? hir_expression_width(expression_id, process_scope)
                    : std::optional<std::size_t> { 1U }
                : std::nullopt;
            const auto domain = operands.empty()
                ? std::nullopt
                : vhdl && index
                ? hir_expression_domain(expression_id, process_scope)
                : hir_expression_domain(
                      operands.front(), process_scope);
            const auto width = selection
                ? std::optional { selection->width }
                : dynamic_index
                ? dynamic_index_width
                : dynamic_part;
            return width && *width != 0U
                && *width <= std::numeric_limits<std::uint32_t>::max()
                && (!selection
                    || selection->offset
                        <= std::numeric_limits<std::uint32_t>::max())
                && domain && scalar_domain(*domain)
                && expression_supported(operands.front())
                && (selection
                    || (dynamic_index
                        && expression_supported(operands[1]))
                    || (dynamic_part
                        && expression_supported(operands[1])));
        }
        if (concatenation || replication) {
            if (replication
                && (vhdl || operands.size() < 2U
                    || !hir_constant_integer(operands.front()))) {
                return false;
            }
            const auto first = replication ? 1U : 0U;
            const auto width = hir_expression_width(
                expression_id, process_scope);
            const auto domain = hir_expression_domain(
                expression_id, process_scope);
            const auto zero_width_replication =
                [&](const semantic::ExpressionId child) {
                    const auto record
                        = specialized_hir_unit_->find_expression(child);
                    if (!record || record->systemverilog == nullptr
                        || record->systemverilog->kind
                            != semantic::sv::ExpressionKind::replication
                        || record->systemverilog->operands.size() < 2U) {
                        return false;
                    }
                    const auto count = hir_constant_integer(
                        record->systemverilog->operands.front());
                    return count && *count == 0;
                };
            return width && *width != 0U
                && *width <= std::numeric_limits<std::uint32_t>::max()
                && domain && scalar_domain(*domain)
                && std::ranges::all_of(
                    std::span { operands }.subspan(first),
                    [&](const auto child) {
                        return (concatenation
                                && zero_width_replication(child))
                            || expression_supported(child);
                    });
        }
        if (conditional) {
            if (operands.size() != 3U) {
                return false;
            }
            const auto condition_domain = hir_expression_domain(
                operands[0], process_scope);
            const auto true_domain = hir_expression_domain(
                operands[1], process_scope);
            const auto false_domain = hir_expression_domain(
                operands[2], process_scope);
            const auto true_width = hir_expression_width(
                operands[1], process_scope);
            const auto false_width = hir_expression_width(
                operands[2], process_scope);
            const auto compatible_condition = condition_domain
                && (vhdl
                        ? *condition_domain
                                == frontend::ValueDomain::Boolean
                            || *condition_domain
                                == frontend::ValueDomain::Bit2
                            || *condition_domain
                                == frontend::ValueDomain::Logic4
                            || *condition_domain
                                == frontend::ValueDomain::Logic9
                        : *condition_domain
                                == frontend::ValueDomain::Bit2
                            || *condition_domain
                                == frontend::ValueDomain::Logic4
                            || *condition_domain
                                == frontend::ValueDomain::Integer);
            const auto systemverilog_packed_integral
                = [](const frontend::ValueDomain domain) {
                return domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Logic4
                    || domain == frontend::ValueDomain::Integer;
            };
            const auto compatible_results = true_domain && false_domain
                && (vhdl
                        ? *true_domain == *false_domain
                        : systemverilog_packed_integral(*true_domain)
                            && systemverilog_packed_integral(
                                *false_domain));
            return compatible_condition && compatible_results
                && true_width && *true_width != 0U
                && false_width && *false_width != 0U
                && std::ranges::all_of(
                    operands,
                    [&](const auto child) {
                        return expression_supported(child);
                    });
        }
        return false;
    };

    bool supported = false;
    std::span<const semantic::ExpressionId> operands;
    std::string_view operation;
    auto language = frontend::Language::SystemVerilog2017;
    bool integer_literal = false;
    bool update_expression = false;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        operands = source.operands;
        operation = source.text;
        if (source.kind == semantic::sv::ExpressionKind::name
            && source.text.ends_with(".triggered")) {
            supported = hir_systemverilog_event_signal(
                            expression_id, process_scope)
                            .has_value();
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with("@sv-sync-new:")) {
            supported = source.operands.size() <= 1U
                && std::ranges::all_of(
                    source.operands,
                    [&](const auto operand) {
                        return expression_supported(operand);
                    });
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with('.')
            && !source.operands.empty()) {
            const auto declaration_id = hir_target_declaration(
                source.operands.front());
            const auto declaration = declaration_id
                ? specialized_hir_unit_->find_declaration(*declaration_id)
                : std::nullopt;
            const auto* type = declaration
                    && declaration->systemverilog != nullptr
                    && declaration->systemverilog->type
                ? &*declaration->systemverilog->type
                : nullptr;
            const auto type_name = type != nullptr
                ? std::string_view { type->target.spelling }
                : std::string_view { };
            const bool mailbox = type_name == "mailbox";
            const bool semaphore = type_name == "semaphore";
            if (mailbox || semaphore) {
                const auto method
                    = std::string_view { source.text }.substr(1U);
                const auto arguments = source.operands.size() - 1U;
                const bool arity = mailbox
                    ? (method == "num" && arguments == 0U)
                        || ((method == "put" || method == "try_put"
                                || method == "get"
                                || method == "try_get"
                                || method == "peek"
                                || method == "try_peek")
                            && arguments == 1U)
                    : (method == "get" || method == "try_get"
                            || method == "put")
                        && arguments <= 1U;
                supported = arity && std::ranges::all_of(
                    source.operands,
                    [&](const auto operand) {
                        return expression_supported(operand);
                    });
                erase();
                return supported;
            }
        }
        constexpr auto container_index_prefix
            = std::string_view { "@sv-container-index:" };
        constexpr auto container_method_prefix
            = std::string_view { "@sv-container-method:" };
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_index_prefix)) {
            supported = source.operands.size() == 2U
                && hir_expression_width(
                       source.operands[0], process_scope)
                    == std::optional<std::size_t> { 64U }
                && expression_supported(source.operands[0])
                && expression_supported(source.operands[1]);
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(container_method_prefix)) {
            const auto container_operation
                = std::string_view { source.text }.substr(
                    container_method_prefix.size());
            supported = source.operands.size() == 1U
                && (container_operation.starts_with("pop_front:")
                    || container_operation.starts_with("size:"))
                && hir_expression_width(
                       source.operands.front(), process_scope)
                    == std::optional<std::size_t> { 64U }
                && expression_supported(source.operands.front());
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == ".size" || source.text == ".num"
                || source.text == ".sum"
                || source.text == ".product" || source.text == ".and"
                || source.text == ".or" || source.text == ".xor"
                || source.text == ".exists" || source.text == ".first"
                || source.text == ".last" || source.text == ".next"
                || source.text == ".prev"
                || source.text == ".pop_front"
                || source.text == ".pop_back"
                || source.text == ".reverse" || source.text == ".sort"
                || source.text == ".rsort" || source.text == ".shuffle"
                || source.text == ".min" || source.text == ".max"
                || source.text == ".unique"
                || source.text == ".unique_index"
                || source.text == ".find"
                || source.text == ".find_index"
                || source.text == ".find_first"
                || source.text == ".find_first_index"
                || source.text == ".find_last"
                || source.text == ".find_last_index")) {
            // Direct lowering owns method-family arity, receiver, iterator,
            // result-use, and type diagnostics. Iterator pseudo-names are not
            // ordinary expressions and therefore must not enter recursive
            // packed-expression preflight.
            supported = !source.operands.empty();
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::string_literal) {
            supported = source.decoded_string.has_value();
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == ".len") {
            supported = source.operands.size() == 1U
                && can_lower_hir_string_expression(
                    source.operands.front(), process_scope);
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == ".getc"
                || source.text == ".compare"
                || source.text == ".icompare"
                || source.text == ".atoi"
                || source.text == ".atohex"
                || source.text == ".atooct"
                || source.text == ".atobin"
                || source.text == ".atoreal")) {
            const bool conversion = source.text == ".atoi"
                || source.text == ".atohex"
                || source.text == ".atooct"
                || source.text == ".atobin"
                || source.text == ".atoreal";
            supported = source.operands.size()
                    == (conversion ? 1U : 2U)
                && can_lower_hir_string_expression(
                    source.operands.front(), process_scope);
            if (supported && !conversion) {
                if (source.text == ".getc") {
                    supported = expression_supported(
                        source.operands[1]);
                } else {
                    supported = can_lower_hir_string_expression(
                        source.operands[1], process_scope);
                }
            }
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "$isunbounded") {
            const auto value
                = specialized_hir_unit_->evaluate_integral_expression(
                    expression_id);
            supported = source.operands.size() == 1U
                && value.has_value();
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && systemverilog_sampled_value_call(source.text)) {
            // Sampled-value lowering owns the exact arity, direct-signal,
            // tick-count, gate, and clocking-event diagnostics. Admit the
            // retained call here even when malformed so those diagnostics
            // survive the AST lifetime boundary.
            supported = true;
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::index
            && source.operands.size() == 2U
            && hir_expression_is_string(
                source.operands.front(), process_scope)) {
            supported = can_lower_hir_string_expression(
                            source.operands.front(), process_scope)
                && expression_supported(source.operands.back());
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$left" || source.text == "$right"
                || source.text == "$low" || source.text == "$high"
                || source.text == "$increment" || source.text == "$size"
                || source.text == "$bits"
                || source.text == "$dimensions"
                || source.text == "$unpacked_dimensions")) {
            const bool packed_bits = source.text == "$bits"
                && source.operands.size() == 1U
                && !hir_static_container_expression_type(
                    source.operands.front())
                && hir_expression_width(
                    source.operands.front(), process_scope)
                && expression_supported(source.operands.front());
            const bool runtime_dimension
                = source.operands.size() == 1U
                || (source.operands.size() == 2U
                    && (source.text == "$left"
                        || source.text == "$right"
                        || source.text == "$low"
                        || source.text == "$high"
                        || source.text == "$increment"
                        || source.text == "$size")
                    && hir_constant_integer(source.operands.back())
                        == std::optional<std::int64_t> { 1 });
            const auto runtime_binding = runtime_dimension
                ? hir_container_object_binding(source.operands.front())
                : std::nullopt;
            const bool runtime_query = runtime_binding
                && runtime_binding->type != nullptr
                && !runtime_binding->type->fixed
                && (source.text == "$size" || source.text == "$bits"
                    || (!runtime_binding->type->associative
                        && (source.text == "$left"
                            || source.text == "$right"
                            || source.text == "$low"
                            || source.text == "$high"
                            || source.text == "$increment")));
            const bool packed_query = !source.operands.empty()
                && !hir_static_container_expression_type(
                    source.operands.front())
                && hir_expression_width(
                    source.operands.front(), process_scope)
                    .value_or(0U)
                    != 0U;
            supported = hir_systemverilog_container_query(expression_id)
                            .has_value()
                || runtime_query || packed_bits || packed_query;
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$signed" || source.text == "$unsigned")) {
            supported = source.operands.size() == 1U
                && expression_supported(source.operands.front());
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "$system"
                || source.text == "$q_full"
                || source.text == "$isunknown"
                || source.text == "$onehot"
                || source.text == "$onehot0"
                || source.text == "$countones"
                || source.text == "$countbits"
                || source.text == "$urandom"
                || source.text == "$random"
                || source.text == "$urandom_range")) {
            // Direct lowering owns the argument diagnostics for these IEEE
            // system functions. Keep malformed calls inside the HIR path so
            // they do not collapse into the generic unavailable-adapter
            // diagnostic during process preflight.
            if (source.text == "$system") {
                supported = source.operands.size() <= 1U
                    && (source.operands.empty()
                        || can_lower_hir_string_expression(
                            source.operands.front(), process_scope));
            } else if (source.text == "$q_full") {
                supported = source.operands.size() == 2U
                    && std::ranges::all_of(
                        source.operands,
                        [&](const auto operand) {
                            return expression_supported(operand);
                        });
            } else if (source.text == "$isunknown") {
                supported = source.operands.size() == 1U
                    && expression_supported(source.operands.front());
            } else if (source.text == "$onehot"
                || source.text == "$onehot0"
                || source.text == "$countones") {
                supported = source.operands.size() == 1U
                    && expression_supported(source.operands.front());
            } else if (source.text == "$countbits") {
                supported = source.operands.size() >= 2U
                    && std::ranges::all_of(
                        source.operands,
                        [&](const auto operand) {
                            return expression_supported(operand);
                        });
            } else if (source.text == "$urandom_range") {
                supported = !source.operands.empty()
                    && source.operands.size() <= 2U
                    && std::ranges::all_of(
                        source.operands,
                        [&](const auto operand) {
                            return expression_supported(operand);
                        });
            } else {
                supported = source.operands.empty();
            }
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "std::randomize") {
            // Direct lowering owns the language, argument, and local-storage
            // diagnostics for this built-in scope function.
            supported = true;
            erase();
            return supported;
        }
        if (const auto function = hir_systemverilog_math_function(
                expression_id)) {
            using Function = runtime::SystemVerilogMathFunction;
            const bool time_function
                = *function == Function::Time
                || *function == Function::Stime
                || *function == Function::Realtime;
            const auto arity = time_function
                ? 0U
                : *function == Function::Pow
                    || *function == Function::Atan2
                    || *function == Function::Hypot
                ? 2U
                : 1U;
            supported = source.operands.size() == arity
                && std::ranges::all_of(
                    source.operands,
                    [&](const auto operand) {
                        return expression_supported(operand);
                    });
            erase();
            return supported;
        }
        if (const auto cast = hir_systemverilog_cast_profile(
                expression_id)) {
            const auto source_domain = hir_expression_domain(
                source.operands.front(), process_scope);
            supported = cast->width != 0U
                && scalar_domain(cast->domain)
                && source_domain && scalar_domain(*source_domain)
                && expression_supported(source.operands.front());
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && systemverilog_random_distribution_kind(source.text)) {
            const auto kind = *systemverilog_random_distribution_kind(
                source.text);
            const auto three_arguments
                = kind == runtime::simir::RandomDistributionKind::uniform
                || kind == runtime::simir::RandomDistributionKind::normal
                || kind == runtime::simir::RandomDistributionKind::erlang;
            const auto required_arity = three_arguments ? 3U : 2U;
            const auto seed_declaration = source.operands.empty()
                ? std::nullopt
                : hir_target_declaration(source.operands.front());
            const auto seed_binding = seed_declaration
                ? hir_runtime_binding(
                      *seed_declaration, process_scope, false)
                : std::nullopt;
            const auto integral_argument = [&](
                                               const semantic::ExpressionId
                                                   operand) {
                const auto scalar_kind
                    = hir_systemverilog_scalar_kind(operand);
                return scalar_kind
                    != frontend::SystemVerilogScalarKind::ShortReal
                    && scalar_kind
                    != frontend::SystemVerilogScalarKind::Real
                    && scalar_kind
                    != frontend::SystemVerilogScalarKind::Realtime
                    && scalar_kind
                    != frontend::SystemVerilogScalarKind::Chandle
                    && expression_supported(operand);
            };
            supported = source.operands.size() == required_arity
                && seed_binding && seed_binding->width >= 32U
                && (seed_binding->kind == HirRuntimeBindingKind::local
                    || (seed_binding->signal
                        && !read_only_signals_.contains(
                            *seed_binding->signal)))
                && std::ranges::all_of(
                    std::span { source.operands }.subspan(1U),
                    integral_argument);
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && is_hir_systemverilog_file_call(expression_id)) {
            erase();
            return true;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && is_hir_systemverilog_coverage_call(expression_id)) {
            erase();
            return true;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && (source.text == "@stream-left"
                || source.text == "@stream-right")) {
            const auto slice_size = source.operands.empty()
                ? std::nullopt
                : hir_constant_integer(source.operands.front());
            std::size_t width { };
            supported = source.operands.size() >= 2U
                && slice_size && *slice_size > 0;
            for (std::size_t index = 1U;
                supported && index < source.operands.size(); ++index) {
                const auto operand_width = hir_expression_width(
                    source.operands[index], process_scope);
                const auto operand_domain = hir_expression_domain(
                    source.operands[index], process_scope);
                supported = operand_width && *operand_width != 0U
                    && *operand_width
                        <= std::numeric_limits<std::uint32_t>::max() - width
                    && operand_domain
                    && (*operand_domain == frontend::ValueDomain::Bit2
                        || *operand_domain
                            == frontend::ValueDomain::Logic4)
                    && expression_supported(source.operands[index]);
                if (supported) {
                    width += *operand_width;
                }
            }
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_property
            || source.kind
                == semantic::sv::ExpressionKind::class_static_property) {
            const auto profile = hir_class_property_profile(expression_id);
            supported = profile
                && (!profile->receiver
                    || (hir_expression_width(
                            *profile->receiver, process_scope)
                            == std::optional<std::size_t> { 64U }
                        && expression_supported(*profile->receiver)));
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_null) {
            erase();
            return true;
        }
        if (source.kind
            == semantic::sv::ExpressionKind::class_allocation) {
            supported = !source.class_identity.empty()
                && std::ranges::all_of(
                    source.operands,
                    [&](const auto operand) {
                        return can_lower_hir_string_expression(
                                   operand, process_scope)
                            || expression_supported(operand);
                    });
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::class_cast) {
            if (source.operands.size() != 2U
                || source.class_member_identity.empty()) {
                erase();
                return false;
            }
            const auto target = hir_target_declaration(
                source.operands.front());
            const auto binding = target
                ? hir_runtime_binding(*target, process_scope, false)
                : std::nullopt;
            supported = binding && binding->width == 64U
                && (binding->kind == HirRuntimeBindingKind::local
                    || (binding->signal
                        && !read_only_signals_.contains(*binding->signal)))
                && hir_expression_width(
                       source.operands.front(), process_scope)
                    == std::optional<std::size_t> { 64U }
                && hir_expression_width(
                       source.operands.back(), process_scope)
                    == std::optional<std::size_t> { 64U }
                && expression_supported(source.operands.front())
                && expression_supported(source.operands.back());
            erase();
            return supported;
        }
        if (source.kind
                == semantic::sv::ExpressionKind::class_method_call
            || source.kind
                == semantic::sv::ExpressionKind::class_static_method_call) {
            supported = can_lower_hir_class_method_call(
                expression_id, process_scope, visiting);
            erase();
            return supported;
        }
        const auto type_operator_call = [&](const semantic::ExpressionId id) {
            const auto operand = specialized_hir_unit_->find_expression(id);
            return operand && operand->systemverilog != nullptr
                && operand->systemverilog->kind
                == semantic::sv::ExpressionKind::call
                && operand->systemverilog->text == "@sv-type";
        };
        if (source.kind == semantic::sv::ExpressionKind::binary
            && source.operands.size() == 2U
            && (source.text == "==" || source.text == "!="
                || source.text == "===" || source.text == "!==")
            && (type_operator_call(source.operands[0])
                || type_operator_call(source.operands[1]))) {
            // Type operands are compile-time descriptors rather than runtime
            // values. Direct lowering validates both sides and their static
            // expression types without recursively lowering either call.
            erase();
            return true;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "@sv-type") {
            // Keep invalid standalone use in the direct HIR path so it emits
            // the dedicated type-operator diagnostic.
            erase();
            return true;
        }
        if (source.kind == semantic::sv::ExpressionKind::binary
            && source.operands.size() == 2U
            && (source.text == "==" || source.text == "!=")
            && can_lower_hir_string_expression(
                source.operands[0], process_scope)
            && can_lower_hir_string_expression(
                source.operands[1], process_scope)) {
            erase();
            return true;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "inside") {
            const auto item_supported = [&](
                                            const semantic::ExpressionId item) {
                const auto record = specialized_hir_unit_->find_expression(
                    item);
                if (!record || record->systemverilog == nullptr) {
                    return false;
                }
                const auto& candidate = *record->systemverilog;
                if (candidate.kind == semantic::sv::ExpressionKind::call
                    && (candidate.text == "@inside-range"
                        || candidate.text
                            == "@inside-absolute-tolerance"
                        || candidate.text
                            == "@inside-relative-tolerance")) {
                    const auto tolerance
                        = candidate.text != "@inside-range";
                    return (!tolerance
                               || systemverilog_standard_
                                   == frontend::StandardRevision::SystemVerilog2023)
                        && candidate.operands.size() == 2U
                        && std::ranges::all_of(
                            candidate.operands,
                            [&](const auto operand) {
                                return expression_supported(operand);
                            });
                }
                return expression_supported(item);
            };
            const auto lhs_width = source.operands.empty()
                ? std::nullopt
                : hir_expression_width(
                      source.operands.front(), process_scope);
            const auto lhs_domain = source.operands.empty()
                ? std::nullopt
                : hir_expression_domain(
                      source.operands.front(), process_scope);
            supported = source.operands.size() >= 2U
                && lhs_width && *lhs_width != 0U
                && lhs_domain && scalar_domain(*lhs_domain)
                && expression_supported(source.operands.front())
                && std::ranges::all_of(
                    std::span { source.operands }.subspan(1U),
                    item_supported);
            erase();
            return supported;
        }
        if (const auto aggregate_member
            = hir_container_aggregate_selection(expression_id)) {
            supported = aggregate_member->leaf.element_width != 0U
                && aggregate_member->leaf.element_width
                    <= std::numeric_limits<std::uint32_t>::max()
                && std::ranges::all_of(
                    aggregate_member->element.indices,
                    expression_supported);
            erase();
            return supported;
        }
        const auto compound = source.kind == semantic::sv::ExpressionKind::index
            || source.kind == semantic::sv::ExpressionKind::slice
            || source.kind
                == semantic::sv::ExpressionKind::concatenation
            || source.kind == semantic::sv::ExpressionKind::replication
            || (source.kind == semantic::sv::ExpressionKind::call
                && source.text == "?:");
        if (compound) {
            if (source.kind
                    == semantic::sv::ExpressionKind::concatenation
                && source.operands.empty()) {
                erase();
                return true;
            }
            supported = compound_supported(
                source.operands,
                source.kind == semantic::sv::ExpressionKind::index,
                source.kind == semantic::sv::ExpressionKind::slice,
                source.kind
                    == semantic::sv::ExpressionKind::concatenation,
                source.kind == semantic::sv::ExpressionKind::replication,
                source.kind == semantic::sv::ExpressionKind::call,
                false);
            erase();
            return supported;
        }
        if (source.kind == semantic::sv::ExpressionKind::call) {
            supported = can_lower_hir_function_call(
                expression_id, process_scope, visiting);
            erase();
            return supported;
        }
        switch (source.kind) {
        case semantic::sv::ExpressionKind::name: {
            if (source.text == "this"
                && hir_class_receiver_register_) {
                supported = true;
                break;
            }
            if (hir_case_pattern_binding(expression_id)) {
                supported = true;
                break;
            }
            if (hir_direct_signal(expression_id)) {
                supported = true;
                break;
            }
            const auto root_separator = source.text.find('.');
            if (root_separator != std::string::npos
                && std::ranges::find(design_.roots_,
                       source.text.substr(0U, root_separator))
                    != design_.roots_.end()) {
                // Let lowering emit the dedicated cross-root diagnostic for
                // a missing or deeper-than-direct root signal reference.
                supported = true;
                break;
            }
            const auto declaration = hir_referenced_declaration(
                expression_id);
            const auto initializer = declaration
                ? hir_constant_initializer(*declaration)
                : std::nullopt;
            supported = declaration
                && (hir_runtime_binding(
                        *declaration, process_scope, false)
                        .has_value()
                    || hir_string_binding(
                        *declaration, process_scope, false)
                        .has_value()
                    || (initializer
                        && hir_constant_integer(expression_id).has_value())
                    || (initializer
                        && expression_supported(*initializer)));
            break;
        }
        case semantic::sv::ExpressionKind::integer_literal:
            integer_literal = true;
            supported = supported_systemverilog_integer_literal(source.text);
            break;
        case semantic::sv::ExpressionKind::boolean_literal:
        case semantic::sv::ExpressionKind::logic_literal:
            supported = true;
            break;
        case semantic::sv::ExpressionKind::unary:
            supported = operands.size() == 1U;
            break;
        case semantic::sv::ExpressionKind::update:
            update_expression = true;
            supported = operands.size() == 1U
                && (source.text == "pre++" || source.text == "pre--"
                    || source.text == "post++"
                    || source.text == "post--")
                && hir_target_declaration(operands.front()).has_value();
            break;
        case semantic::sv::ExpressionKind::binary:
            supported = operands.size() == 2U;
            break;
        default:
            break;
        }
    } else {
        language = frontend::Language::Vhdl2008;
        const auto& source = *expression->vhdl;
        operands = source.operands;
        operation = source.text;
        if (is_hir_vhdl_access_expression(expression_id)
            || is_hir_vhdl_physical_expression(expression_id)
            || is_hir_vhdl_environment_expression(expression_id)
            || is_hir_vhdl_reflection_expression(expression_id)
            || is_hir_vhdl_protected_expression(expression_id)) {
            erase();
            return true;
        }
        if (hir_vhdl_assert_expression_api(expression_id)) {
            erase();
            return true;
        }
        if (const auto selection
            = hir_vhdl_environment_call_path_member_selection(
                expression_id);
            selection && selection->member == 3U) {
            erase();
            return true;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::binary
            && source.operands.size() == 2U
            && (source.text == "=" || source.text == "/=")
            && can_lower_hir_string_expression(
                source.operands[0], process_scope)
            && can_lower_hir_string_expression(
                source.operands[1], process_scope)) {
            erase();
            return true;
        }
        if (const auto selection = hir_vhdl_array_selection(
                expression_id)) {
            supported = selection->width != 0U
                && selection->width
                    <= std::numeric_limits<std::uint32_t>::max()
                && std::ranges::all_of(
                    selection->dynamic_indices,
                    [&](const HirVhdlArrayIndex& array_index) {
                        return expression_supported(
                            array_index.expression);
                    })
                && hir_runtime_binding(
                    selection->declaration, process_scope, false)
                       .has_value();
            erase();
            return supported;
        }
        const auto compound = source.kind
                == semantic::vhdl::ExpressionKind::index
            || source.kind == semantic::vhdl::ExpressionKind::slice
            || source.kind
                == semantic::vhdl::ExpressionKind::concatenation
            || source.kind
                == semantic::vhdl::ExpressionKind::replication
            || source.kind == semantic::vhdl::ExpressionKind::conditional;
        if (compound) {
            supported = compound_supported(
                source.operands,
                source.kind == semantic::vhdl::ExpressionKind::index,
                source.kind == semantic::vhdl::ExpressionKind::slice,
                source.kind
                    == semantic::vhdl::ExpressionKind::concatenation,
                source.kind
                    == semantic::vhdl::ExpressionKind::replication,
                source.kind
                    == semantic::vhdl::ExpressionKind::conditional,
                true);
            erase();
            return supported;
        }
        if (const auto member = hir_vhdl_member_selection(
                expression_id)) {
            const auto width = member->index
                ? member->element_width
                : member->width;
            supported = width != 0U
                && width
                    <= std::numeric_limits<std::uint32_t>::max()
                && (!member->index
                    || expression_supported(*member->index))
                && (member->root.valid()
                        ? expression_supported(member->root)
                        : hir_runtime_binding(
                              member->declaration,
                              process_scope,
                              false)
                              .has_value());
            erase();
            return supported;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::aggregate) {
            supported = !source.associations.empty()
                && std::ranges::all_of(
                    source.associations,
                    [&](const semantic::vhdl::AggregateAssociation&
                            association) {
                        // Aggregate choices describe positions or ranges;
                        // they are consumed by the aggregate planner and are
                        // not runtime values that must lower independently.
                        return expression_supported(association.value);
                    });
            erase();
            return supported;
        }
        if (source.kind == semantic::vhdl::ExpressionKind::call) {
            if (hir_vhdl_vital_expression_profile(expression_id)) {
                // VITAL intrinsics own contextual aggregate/null-array
                // interpretation and their precise argument diagnostics.
                erase();
                return true;
            }
            if (is_hir_vhdl_vital_memory_expression(expression_id)) {
                erase();
                return true;
            }
            if (is_hir_vhdl_file_call(expression_id)) {
                erase();
                return true;
            }
            if (is_hir_vhdl_float_function(expression_id)
                || is_hir_vhdl_fixed_function(expression_id)) {
                erase();
                return true;
            }
            if (const auto attribute = hir_vhdl_signal_attribute_profile(
                    expression_id, process_scope)) {
                supported = attribute->width != 0U
                    && scalar_domain(attribute->domain);
                erase();
                return supported;
            }
            if (source.text == "'driving_value"
                || source.text == "'stable"
                || source.text == "'quiet"
                || source.text == "'transaction"
                || source.text == "'delayed") {
                // Keep malformed signal-attribute calls in the direct HIR
                // path. The expression lowerer owns their precise arity,
                // signal-target, delay, and result-shape diagnostics.
                erase();
                return true;
            }
            if (is_hir_vhdl_vital_mux2(expression_id)) {
                const auto positional
                    = source.argument_names.empty()
                    || (source.argument_names.size()
                            == source.operands.size()
                        && std::ranges::all_of(
                            source.argument_names,
                            [](const std::string& name) {
                                return name.empty();
                            }));
                supported = positional && source.operands.size() == 3U
                    && std::ranges::all_of(
                        source.operands,
                        [&](const semantic::ExpressionId operand) {
                            const auto width = hir_expression_width(
                                operand, process_scope);
                            const auto domain = hir_expression_domain(
                                operand, process_scope);
                            return width == std::optional<std::size_t> { 1U }
                            && domain && scalar_domain(*domain)
                                && expression_supported(operand);
                        });
                erase();
                return supported;
            }
            if (const auto conversion = hir_vhdl_conversion_profile(
                    expression_id)) {
                const auto operand_domain = source.operands.empty()
                    ? std::nullopt
                    : hir_expression_domain(
                          source.operands.front(), process_scope);
                supported = source.operands.size() == 1U
                    && conversion->width != 0U
                    && scalar_domain(conversion->domain)
                    && operand_domain && scalar_domain(*operand_domain)
                    && expression_supported(source.operands.front());
                erase();
                return supported;
            }
            constexpr auto qualified_prefix
                = std::string_view { "@vhdl-qualified:" };
            if (source.text.starts_with(qualified_prefix)
                && source.operands.size() == 1U) {
                // Keep malformed qualifications on the HIR-native path so
                // the expression lowerer can issue the precise VHQUAL
                // diagnostic instead of the generic capability fallback.
                supported = expression_supported(source.operands.front());
                erase();
                return supported;
            }
            if ((source.text == "rising_edge"
                    || source.text == "falling_edge")
                && source.operands.size() == 1U) {
                const auto declaration = hir_referenced_declaration(
                    source.operands.front());
                const auto binding = declaration
                    ? hir_runtime_binding(
                          *declaration, process_scope, false)
                    : std::nullopt;
                supported = binding
                    && binding->kind == HirRuntimeBindingKind::signal
                    && binding->signal && binding->width == 1U;
                erase();
                return supported;
            }
            const auto attribute = hir_vhdl_attribute_profile(
                expression_id, process_scope);
            supported = attribute
                ? attribute->constant.has_value()
                    || (attribute->value
                        && expression_supported(*attribute->value))
                : can_lower_hir_function_call(
                      expression_id, process_scope, visiting);
            if (!supported && source.referenced_name) {
                supported = !hir_vhdl_callable_candidates(
                    *source.referenced_name, source.scope).empty();
            }
            erase();
            return supported;
        }
        const auto overloaded_operator
            = (source.kind == semantic::vhdl::ExpressionKind::unary
                  || source.kind == semantic::vhdl::ExpressionKind::binary)
            && source.referenced_name
            && resolve_hir_vhdl_function_call(
                expression_id, process_scope, 0U);
        if (overloaded_operator
            && can_lower_hir_function_call(
                expression_id, process_scope, visiting)) {
            erase();
            return true;
        }
        switch (source.kind) {
        case semantic::vhdl::ExpressionKind::name: {
            if (hir_vhdl_vital_expression_profile(expression_id)) {
                supported = true;
                break;
            }
            if (hir_vhdl_standard_enumeration_literal(expression_id)
                || hir_vhdl_character_literal_code(expression_id, 0U)) {
                supported = true;
                break;
            }
            if (is_hir_vhdl_environment_status_literal(expression_id)) {
                supported = true;
                break;
            }
            if (source.text.size() >= 3U
                && source.text.front() == '\''
                && source.text.back() == '\''
                && std::ranges::any_of(
                    specialized_hir_unit_->vhdl_types(),
                    [&](const auto& type) {
                        return std::ranges::any_of(
                            type.enumeration_literals,
                            [&](const auto& literal) {
                                return literal.spelling == source.text;
                            });
                    })) {
                supported = true;
                break;
            }
            const auto declaration = hir_referenced_declaration(
                expression_id);
            const auto initializer = declaration
                ? hir_constant_initializer(*declaration)
                : std::nullopt;
            supported = declaration
                && (hir_runtime_binding(
                        *declaration, process_scope, false)
                        .has_value()
                    || (initializer
                        && hir_constant_integer(expression_id).has_value())
                    || (initializer
                        && expression_supported(*initializer)));
            break;
        }
        case semantic::vhdl::ExpressionKind::integer_literal:
            integer_literal = true;
            supported = hir_constant_integer(expression_id).has_value();
            break;
        case semantic::vhdl::ExpressionKind::real_literal:
            supported = systemverilog_real_literal(source.text).has_value();
            break;
        case semantic::vhdl::ExpressionKind::string_literal:
            supported = source.decoded_string
                && !source.decoded_string->empty()
                && std::ranges::all_of(
                    *source.decoded_string, vhdl_logic_character);
            break;
        case semantic::vhdl::ExpressionKind::boolean_literal:
        case semantic::vhdl::ExpressionKind::logic_literal:
            supported = true;
            break;
        case semantic::vhdl::ExpressionKind::unary:
            supported = operands.size() == 1U;
            break;
        case semantic::vhdl::ExpressionKind::binary:
            supported = operands.size() == 2U;
            break;
        default:
            break;
        }
    }
    if (supported) {
        supported = std::ranges::all_of(
            operands,
            expression_supported);
    }
    if (supported && !integer_literal && !update_expression
        && operands.size() == 1U) {
        const auto selected = hir_referenced_declaration(operands.front());
        const auto binding = selected
            ? hir_runtime_binding(*selected, process_scope, false)
            : std::nullopt;
        const auto domain = binding
            ? std::optional { binding->domain }
            : hir_expression_domain(operands.front(), process_scope);
        supported = domain
            && supported_unary_operator(language, operation, *domain);
    }
    if (supported && operands.size() == 2U) {
        const auto expression_width = [&](const auto operand) {
            const auto selected = hir_referenced_declaration(operand);
            const auto binding = selected
                ? hir_runtime_binding(*selected, process_scope, false)
                : std::nullopt;
            return binding ? std::optional { binding->width }
                           : hir_expression_width(
                                 operand, process_scope);
        };
        const auto expression_domain = [&](const auto operand) {
            const auto selected = hir_referenced_declaration(operand);
            const auto binding = selected
                ? hir_runtime_binding(*selected, process_scope, false)
                : std::nullopt;
            return binding ? std::optional { binding->domain }
                           : hir_expression_domain(
                                 operand, process_scope);
        };
        const auto left_width = expression_width(operands[0]);
        const auto right_width = expression_width(operands[1]);
        const auto left_domain = expression_domain(operands[0]);
        const auto right_domain = expression_domain(operands[1]);
        supported = left_width && right_width
            && *left_width != 0U && *right_width != 0U
            && left_domain && right_domain
            && supported_binary_operator(
                language, operation, *left_domain, *right_domain);
    }
    erase();
    return supported;
}

std::optional<semantic::SourceSpanId>
Lowerer::hir_vhdl_unspecified_inference_failure(
    const semantic::ProcessId process) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    return VhdlUnspecifiedInferenceScanner { *specialized_hir_unit_ }
        .scan(process);
}

std::optional<semantic::SourceSpanId>
Lowerer::hir_vhdl_unspecified_inference_failure(
    const semantic::StatementId statement) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    return VhdlUnspecifiedInferenceScanner { *specialized_hir_unit_ }
        .scan(statement);
}

} // namespace fsim::elaboration
