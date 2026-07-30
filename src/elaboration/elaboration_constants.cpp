// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

ConstantTypeInfo::ConstantTypeInfo() = default;

ConstantTypeInfo::ConstantTypeInfo(
        const frontend::ValueDomain value)
    : domain(value) {}

ConstantTypeInfo::ConstantTypeInfo(
        const frontend::ValueDomain value,
        const bool enumeration,
        std::string nominal)
    : domain(value),
      vhdl_enumeration(enumeration),
      nominal_type(std::move(nominal)) {}


[[nodiscard]] bool is_two_state_domain(
    const frontend::ValueDomain domain) noexcept {
    return domain == frontend::ValueDomain::Bit2
        || domain == frontend::ValueDomain::Boolean
        || domain == frontend::ValueDomain::Integer;
}



[[nodiscard]] runtime::simir::ValueKind value_kind(
    const frontend::ValueDomain domain) noexcept {
    return domain == frontend::ValueDomain::Logic9
        ? runtime::simir::ValueKind::logic9
        : runtime::simir::ValueKind::logic4;
}

std::int64_t normalize_systemverilog_parameter_value(
    const std::int64_t value,
    const frontend::Type& type) noexcept {
    if (type.spelling == "implicit" && type.named_type.empty()) {
        return value;
    }
    const auto width = type.width();
    if (!width || *width == 0 || *width >= 64) {
        return value;
    }
    const auto mask =
        (std::uint64_t{1} << *width) - std::uint64_t{1};
    auto bits = static_cast<std::uint64_t>(value) & mask;
    if (type.is_signed
        && (bits & (std::uint64_t{1} << (*width - 1U))) != 0) {
        bits |= ~mask;
    }
    return static_cast<std::int64_t>(bits);
}



std::string simple_top_name(std::string_view top) {
    if (const auto colon = top.rfind(':'); colon != std::string_view::npos) {
        top.remove_prefix(colon + 1);
    }
    if (const auto dot = top.rfind('.'); dot != std::string_view::npos) {
        top.remove_prefix(dot + 1);
    }
    if (const auto architecture = top.find('('); architecture != std::string_view::npos) {
        top = top.substr(0, architecture);
    }
    return std::string{top};
}



std::optional<std::uint64_t> unsigned_decimal(std::string_view text) {
    std::string cleaned{text};
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '_'), cleaned.end());
    std::uint64_t value{};
    const auto result =
        std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (result.ec != std::errc{} || result.ptr != cleaned.data() + cleaned.size()) {
        return std::nullopt;
    }
    return value;
}



std::optional<std::int64_t> constant_index(
    const Expression& expression) {
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        std::string cleaned{expression.text};
        cleaned.erase(
            std::remove(cleaned.begin(), cleaned.end(), '_'),
            cleaned.end());
        std::int64_t value{};
        const auto result = std::from_chars(
            cleaned.data(), cleaned.data() + cleaned.size(), value);
        if (result.ec == std::errc{}
            && result.ptr == cleaned.data() + cleaned.size()) {
            return value;
        }
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "+" || expression.text == "-")) {
        const auto value = constant_index(expression.operands[0]);
        if (!value) {
            return std::nullopt;
        }
        if (expression.text == "+") {
            return value;
        }
        if (*value == std::numeric_limits<std::int64_t>::min()) {
            return std::nullopt;
        }
        return -*value;
    }
    if (expression.kind == ExpressionKind::Binary
        || expression.kind == ExpressionKind::Call) {
        std::string error;
        return evaluate_constant_expression(
            expression, {}, error);
    }
    return std::nullopt;
}



std::uint64_t index_distance(
    const std::int64_t lhs,
    const std::int64_t rhs) noexcept {
    return lhs >= rhs
        ? static_cast<std::uint64_t>(lhs)
              - static_cast<std::uint64_t>(rhs)
        : static_cast<std::uint64_t>(rhs)
              - static_cast<std::uint64_t>(lhs);
}



PackedLogic4 unsigned_value(const std::uint64_t value, const std::size_t width) {
    PackedLogic4 result(width, Logic4::zero);
    for (std::size_t bit = 0; bit < width && bit < 64; ++bit) {
        result.set(bit, ((value >> bit) & 1U) != 0 ? Logic4::one : Logic4::zero);
    }
    return result;
}



PackedLogic4 integer_value(const std::int64_t value) {
    const auto bits = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(value));
    return unsigned_value(bits, 32);
}



PackedLogic4 default_packed_value(
    const frontend::Type& type,
    const std::size_t width) {
    if (!type.enumeration_literals.empty()
        && type.enumeration_range) {
        return unsigned_value(
            static_cast<std::uint64_t>(
                type.enumeration_range->left),
            width);
    }
    auto result = PackedLogic4(
        width,
        is_two_state_domain(type.domain)
            ? Logic4::zero
            : Logic4::x);
    if (type.domain == frontend::ValueDomain::Logic9) {
        result.fill(runtime::Logic9::u);
    }
    if (type.domain == frontend::ValueDomain::Integer && width != 0) {
        return integer_value(
            type.integer_range
                ? type.integer_range->left
                : std::numeric_limits<std::int32_t>::min());
    }
    for (const auto& member : type.packed_members) {
        const auto member_width = member.width();
        if (!member_width
            || member.lsb_offset > width
            || *member_width > width - member.lsb_offset) {
            continue;
        }
        for (std::uint64_t bit = 0; bit < *member_width; ++bit) {
            const auto index =
                static_cast<std::size_t>(member.lsb_offset + bit);
            if (result.is_logic9()) {
                result.set_logic9(
                    index,
                    member.domain == frontend::ValueDomain::Logic9
                        ? runtime::Logic9::u
                        : member.domain == frontend::ValueDomain::Logic4
                            ? runtime::Logic9::x
                            : runtime::Logic9::zero);
            } else {
                result.set(
                    index,
                    is_two_state_domain(member.domain)
                        ? Logic4::zero
                        : Logic4::x);
            }
        }
    }
    return result;
}



std::optional<std::int64_t> vhdl_enumeration_ordinal(
    const Expression& expression,
    const frontend::Type& type) {
    if (type.enumeration_literals.empty()) {
        return std::nullopt;
    }
    constexpr std::string_view internal_prefix{"@fsim-enum:"};
    if (expression.kind == ExpressionKind::LogicLiteral
        && expression.text.starts_with(internal_prefix)) {
        std::int64_t ordinal = 0;
        const auto text =
            std::string_view{expression.text}.substr(
                internal_prefix.size());
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), ordinal);
        if (parsed.ec == std::errc{}
            && parsed.ptr == text.data() + text.size()
            && ordinal >= 0
            && static_cast<std::uint64_t>(ordinal)
                < type.enumeration_literals.size()) {
            return ordinal;
        }
        return std::nullopt;
    }
    if (expression.kind != ExpressionKind::Identifier
        && expression.kind != ExpressionKind::LogicLiteral) {
        return std::nullopt;
    }
    const auto separator = expression.text.find_last_of('.');
    const auto literal_name =
        separator == std::string::npos
            ? std::string_view{expression.text}
            : std::string_view{expression.text}.substr(separator + 1);
    const auto literal = std::find(
        type.enumeration_literals.begin(),
        type.enumeration_literals.end(),
        literal_name);
    if (literal == type.enumeration_literals.end()) {
        return std::nullopt;
    }
    const auto ordinal = static_cast<std::size_t>(
        std::distance(type.enumeration_literals.begin(), literal));
    if (ordinal
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(ordinal);
}



const frontend::Type* vhdl_enumeration_type_mark(
    const DesignUnit& unit,
    const std::string_view name) {
    const auto alias = std::find_if(
        unit.type_aliases.begin(),
        unit.type_aliases.end(),
        [&](const auto& candidate) {
            return candidate.name == name
                && !candidate.type.enumeration_literals.empty();
        });
    if (alias != unit.type_aliases.end()) {
        return &alias->type;
    }
    const frontend::Type* found = nullptr;
    const auto consider =
        [&](const frontend::Type& type) {
          if (found == nullptr
              && type.spelling == name
              && !type.enumeration_literals.empty()) {
              found = &type;
          }
        };
    for (const auto& parameter : unit.parameters) {
        consider(parameter.type);
    }
    for (const auto& port : unit.ports) {
        consider(port.type);
    }
    for (const auto& signal : unit.signals) {
        consider(signal.type);
    }
    return found;
}



const frontend::Type* vhdl_object_type(
    const DesignUnit& unit,
    const std::string_view name) {
    const auto parameter = std::find_if(
        unit.parameters.begin(),
        unit.parameters.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    if (parameter != unit.parameters.end()) {
        return &parameter->type;
    }
    const auto port = std::find_if(
        unit.ports.begin(),
        unit.ports.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    if (port != unit.ports.end()) {
        return &port->type;
    }
    const auto signal = std::find_if(
        unit.signals.begin(),
        unit.signals.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    return signal == unit.signals.end()
        ? nullptr
        : &signal->type;
}



std::optional<FoldedEnumerationAttribute>
evaluate_vhdl_enumeration_attribute(
    const Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    if (expression.kind != ExpressionKind::Call
        || expression.operands.empty()
        || expression.operands.front().kind
            != ExpressionKind::Identifier) {
        return std::nullopt;
    }
    const auto* type = vhdl_enumeration_type_mark(
        unit, expression.operands.front().text);
    if (type == nullptr) {
        return std::nullopt;
    }
    const auto count = type->enumeration_literals.size();
    if (count == 0
        || count
            > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
        error = "enumeration attribute prefix has no representable range";
        return std::nullopt;
    }
    auto range =
        type->enumeration_range.value_or(
            frontend::EnumerationRange{
                0,
                static_cast<std::int64_t>(count - 1U),
                false});
    if (type->enumeration_range_expression) {
        const auto evaluate_bound =
            [&](const Expression& bound)
                -> std::optional<std::int64_t> {
              if (const auto ordinal =
                      vhdl_enumeration_ordinal(
                          bound, *type)) {
                  return ordinal;
              }
              if (bound.kind == ExpressionKind::Identifier) {
                  if (const auto found =
                          environment.find(bound.text);
                      found != environment.end()) {
                      return found->second;
                  }
              }
              return std::nullopt;
            };
        const auto left = evaluate_bound(
            type->enumeration_range_expression->left);
        const auto right = evaluate_bound(
            type->enumeration_range_expression->right);
        if (!left || !right) {
            error =
                "enumeration attribute subtype bounds are not locally "
                "static in this specialization";
            return std::nullopt;
        }
        range = frontend::EnumerationRange{
            *left,
            *right,
            type->enumeration_range_expression->descending};
    }
    const auto lower = std::min(range.left, range.right);
    const auto upper = std::max(range.left, range.right);
    const auto require_arity =
        [&](const std::size_t expected) {
          if (expression.operands.size() != expected + 1U) {
              error = expression.text + " requires "
                  + std::to_string(expected)
                  + (expected == 1 ? " argument" : " arguments");
              return false;
          }
          return true;
        };
    if (expression.text == "'left") {
        if (!require_arity(0)) {
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            range.left, true, false};
    }
    if (expression.text == "'right") {
        if (!require_arity(0)) {
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            range.right, true, false};
    }
    if (expression.text == "'low"
        || expression.text == "'high") {
        if (!require_arity(0)) {
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            expression.text == "'low" ? lower : upper,
            true,
            false};
    }
    if (expression.text == "'length") {
        if (!require_arity(0)) {
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            upper - lower + 1, false, false};
    }
    if (expression.text == "'ascending") {
        if (!require_arity(0)) {
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            range.descending ? 0 : 1, false, true};
    }
    const bool position = expression.text == "'pos";
    const bool value = expression.text == "'val";
    const bool successor =
        expression.text == "'succ"
        || (expression.text == "'leftof"
            && range.descending)
        || (expression.text == "'rightof"
            && !range.descending);
    const bool predecessor =
        expression.text == "'pred"
        || (expression.text == "'leftof"
            && !range.descending)
        || (expression.text == "'rightof"
            && range.descending);
    if (!position && !value && !successor && !predecessor) {
        return std::nullopt;
    }
    if (!require_arity(1)) {
        return std::nullopt;
    }
    const auto& argument_expression = expression.operands[1];
    const auto* argument_object_type =
        argument_expression.kind == ExpressionKind::Identifier
            ? vhdl_object_type(unit, argument_expression.text)
            : nullptr;
    std::optional<std::int64_t> argument;
    if (value) {
        if (vhdl_enumeration_ordinal(
                argument_expression, *type)
            || (argument_object_type != nullptr
                && !argument_object_type
                        ->enumeration_literals.empty())) {
            error = "'val requires an integer-family argument";
            return std::nullopt;
        }
        argument = evaluate_constant_expression(
            argument_expression, environment, error);
    } else {
        argument = vhdl_enumeration_ordinal(
            argument_expression, *type);
        if (!argument
            && argument_expression.kind
                == ExpressionKind::Identifier) {
            if (argument_object_type != nullptr
                && (argument_object_type
                        ->enumeration_literals.empty()
                    || argument_object_type->nominal_type
                        != type->nominal_type)) {
                error = expression.text
                    + " requires a value of enumeration type '"
                    + type->spelling + "'";
                return std::nullopt;
            }
            argument = evaluate_constant_expression(
                argument_expression, environment, error);
        }
    }
    if (!argument) {
        if (error.empty()) {
            error = value
                ? "'val argument is not a locally static integer"
                : expression.text
                    + " requires a locally static enumeration value";
        }
        return std::nullopt;
    }
    if (position) {
        if (*argument < lower || *argument > upper) {
            range_error = true;
            error = "'pos argument is outside the enumeration range";
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            *argument, false, false};
    }
    if (value) {
        if (*argument < lower || *argument > upper) {
            range_error = true;
            error = "'val argument is outside the enumeration range";
            return std::nullopt;
        }
        return FoldedEnumerationAttribute{
            *argument, true, false};
    }
    if (*argument < lower || *argument > upper) {
        range_error = true;
        error = expression.text
            + " argument is outside the enumeration range";
        return std::nullopt;
    }
    const auto adjusted =
        successor ? *argument + 1 : *argument - 1;
    if (adjusted < lower || adjusted > upper) {
        range_error = true;
        error = expression.text
            + " argument has no result inside the enumeration range";
        return std::nullopt;
    }
    return FoldedEnumerationAttribute{
        adjusted, true, false};
}



std::optional<LoweredLiteral> literal_value(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Language language) {
    auto text = expression.text;
    if (expression.kind == ExpressionKind::BooleanLiteral
        && language == frontend::Language::Vhdl2008
        && (text == "true" || text == "false")) {
        return LoweredLiteral{
            PackedLogic4(
                1,
                text == "true" ? Logic4::one : Logic4::zero),
            frontend::ValueDomain::Boolean};
    }
    if (expression.kind == ExpressionKind::LogicLiteral && text.size() == 3
        && text.front() == '\'' && text.back() == '\'') {
        if (language == frontend::Language::Vhdl2008) {
            const auto parsed = runtime::parse_logic9(text[1]);
            if (!parsed) {
                return std::nullopt;
            }
            const auto domain =
                *parsed == runtime::Logic9::zero
                        || *parsed == runtime::Logic9::one
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic9;
            return LoweredLiteral{
                domain == frontend::ValueDomain::Logic9
                    ? PackedLogic4::from_logic9_msb_string(
                          std::string_view{text}.substr(1, 1))
                    : PackedLogic4(
                          1, runtime::to_logic4(*parsed)),
                domain};
        }
        const auto parsed = runtime::parse_logic4(text[1]);
        if (!parsed) {
            return std::nullopt;
        }
        const auto domain =
            *parsed == Logic4::zero || *parsed == Logic4::one
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
        return LoweredLiteral{PackedLogic4(1, *parsed), domain};
    }
    if (expression.kind == ExpressionKind::StringLiteral && text.size() >= 2
        && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
        try {
            if (language == frontend::Language::Vhdl2008) {
                const auto nine_state =
                    runtime::PackedLogic9::from_msb_string(text);
                auto domain = frontend::ValueDomain::Bit2;
                for (std::size_t bit = 0; bit < nine_state.width(); ++bit) {
                    const auto value = nine_state.get(bit);
                    if (value != runtime::Logic9::zero
                        && value != runtime::Logic9::one) {
                        domain = frontend::ValueDomain::Logic9;
                        break;
                    }
                }
                return LoweredLiteral{
                    domain == frontend::ValueDomain::Logic9
                        ? PackedLogic4::from_logic9_msb_string(text)
                        : runtime::collapse_to_logic4(nine_state),
                    domain};
            }
            auto value = PackedLogic4::from_msb_string(text);
            auto domain = frontend::ValueDomain::Bit2;
            for (std::size_t bit = 0; bit < value.width(); ++bit) {
                if (value.get(bit) != Logic4::zero
                    && value.get(bit) != Logic4::one) {
                    domain = frontend::ValueDomain::Logic4;
                    break;
                }
            }
            return LoweredLiteral{std::move(value), domain};
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

    const auto quote = text.find('\'');
    if (quote != std::string::npos) {
        const auto width_text = std::string_view{text}.substr(0, quote);
        std::size_t width = expected_width;
        if (!width_text.empty()) {
            const auto parsed_width = unsigned_decimal(width_text);
            if (!parsed_width || *parsed_width == 0
                || *parsed_width > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            width = static_cast<std::size_t>(*parsed_width);
        }
        auto digits = std::string_view{text}.substr(quote + 1);
        if (!digits.empty() && (digits.front() == 's' || digits.front() == 'S')) {
            digits.remove_prefix(1);
        }
        if (digits.empty()) {
            return std::nullopt;
        }
        const auto base = static_cast<char>(std::tolower(static_cast<unsigned char>(digits.front())));
        digits.remove_prefix(1);
        if (base == 'b') {
            std::string expanded;
            for (const char c : digits) {
                if (c != '_') {
                    expanded.push_back(
                        language != frontend::Language::Vhdl2008
                                && c == '?'
                            ? 'z'
                            : c);
                }
            }
            if (expanded.size() < width) {
                expanded.insert(expanded.begin(), width - expanded.size(), '0');
            } else if (expanded.size() > width) {
                expanded.erase(0, expanded.size() - width);
            }
            try {
                auto value = PackedLogic4::from_msb_string(expanded);
                auto domain = frontend::ValueDomain::Bit2;
                for (std::size_t bit = 0; bit < value.width(); ++bit) {
                    if (value.get(bit) != Logic4::zero
                        && value.get(bit) != Logic4::one) {
                        domain = frontend::ValueDomain::Logic4;
                        break;
                    }
                }
                return LoweredLiteral{std::move(value), domain};
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        }
        if (base == 'd') {
            const auto value = unsigned_decimal(digits);
            return value
                ? std::optional{LoweredLiteral{
                      unsigned_value(*value, width),
                      frontend::ValueDomain::Bit2}}
                : std::nullopt;
        }
        if (base == 'h') {
            std::uint64_t value{};
            std::string cleaned;
            for (const char c : digits) {
                if (c != '_') {
                    cleaned.push_back(c);
                }
            }
            const auto parsed =
                std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != cleaned.data() + cleaned.size()) {
                return std::nullopt;
            }
            return LoweredLiteral{
                unsigned_value(value, width),
                frontend::ValueDomain::Bit2};
        }
        return std::nullopt;
    }

    const auto value = unsigned_decimal(text);
    return value
        ? std::optional{LoweredLiteral{
              unsigned_value(*value, expected_width),
              frontend::ValueDomain::Bit2}}
        : std::nullopt;
}



std::optional<std::int64_t> constant_literal_integer(
    const Expression& expression,
    std::string& error) {
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        if (expression.text == "true") {
            return 1;
        }
        if (expression.text == "false") {
            return 0;
        }
        error = "Boolean literal is malformed";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral
        && expression.text.find('\'') == std::string::npos) {
        const auto value = unsigned_decimal(expression.text);
        if (!value
            || *value
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            error = "decimal literal is outside fsim's signed 64-bit "
                    "constant range";
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*value);
    }
    if (expression.kind != ExpressionKind::LogicLiteral) {
        error = "constant expression contains a non-integral literal";
        return std::nullopt;
    }
    const auto lowered = literal_value(
        expression, 64, frontend::Language::SystemVerilog2017);
    if (!lowered || lowered->value.width() > 64) {
        error = "literal is malformed or wider than 64 bits";
        return std::nullopt;
    }
    std::uint64_t bits = 0;
    for (std::size_t bit = 0; bit < lowered->value.width(); ++bit) {
        const auto value = lowered->value.get(bit);
        if (value == Logic4::x || value == Logic4::z) {
            error = "X and Z digits are not valid in an elaboration "
                    "constant";
            return std::nullopt;
        }
        if (value == Logic4::one) {
            bits |= std::uint64_t{1} << bit;
        }
    }
    const auto quote = expression.text.find('\'');
    const bool is_signed =
        quote != std::string::npos
        && quote + 1 < expression.text.size()
        && (expression.text[quote + 1] == 's'
            || expression.text[quote + 1] == 'S');
    const auto width = lowered->value.width();
    if (is_signed && width != 0
        && lowered->value.get(width - 1) == Logic4::one) {
        if (width < 64) {
            bits |= ~std::uint64_t{0} << width;
        }
        const auto magnitude = (~bits) + 1U;
        if (magnitude
            == (std::uint64_t{1} << 63U)) {
            return std::numeric_limits<std::int64_t>::min();
        }
        if (magnitude
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            error = "signed literal is outside fsim's signed 64-bit "
                    "constant range";
            return std::nullopt;
        }
        return -static_cast<std::int64_t>(magnitude);
    }
    if (bits
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        error = "unsigned literal is outside fsim's signed 64-bit "
                "constant range";
        return std::nullopt;
    }
    return static_cast<std::int64_t>(bits);
}



bool checked_add(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if ((right > 0
         && left
             > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0
            && left
                < std::numeric_limits<std::int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}



bool checked_subtract(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if ((right < 0
         && left
             > std::numeric_limits<std::int64_t>::max() + right)
        || (right > 0
            && left
                < std::numeric_limits<std::int64_t>::min() + right)) {
        return false;
    }
    result = left - right;
    return true;
}



bool checked_multiply(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if (left == 0 || right == 0) {
        result = 0;
        return true;
    }
    if ((left == -1
         && right == std::numeric_limits<std::int64_t>::min())
        || (right == -1
            && left == std::numeric_limits<std::int64_t>::min())) {
        return false;
    }
    if (left > 0) {
        if ((right > 0
             && left
                 > std::numeric_limits<std::int64_t>::max() / right)
            || (right < 0
                && right
                    < std::numeric_limits<std::int64_t>::min() / left)) {
            return false;
        }
    } else if (
        (right > 0
         && left
             < std::numeric_limits<std::int64_t>::min() / right)
        || (right < 0
            && left
                < std::numeric_limits<std::int64_t>::max() / right)) {
        return false;
    }
    result = left * right;
    return true;
}



bool checked_power(
    const std::int64_t base,
    const std::int64_t exponent,
    std::int64_t& result) {
    if (exponent < 0) {
        if (base == 0) {
            return false;
        }
        if (base == 1) {
            result = 1;
        } else if (base == -1) {
            result = (exponent & 1) != 0 ? -1 : 1;
        } else {
            result = 0;
        }
        return true;
    }
    result = 1;
    auto factor = base;
    auto remaining = static_cast<std::uint64_t>(exponent);
    while (remaining != 0) {
        if ((remaining & 1U) != 0) {
            if (!checked_multiply(result, factor, result)) {
                return false;
            }
        }
        remaining >>= 1U;
        if (remaining != 0
            && !checked_multiply(factor, factor, factor)) {
            return false;
        }
    }
    return true;
}



std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error) {
    if (expression.kind == ExpressionKind::IntegerLiteral
        || expression.kind == ExpressionKind::LogicLiteral
        || expression.kind == ExpressionKind::BooleanLiteral) {
        return constant_literal_integer(expression, error);
    }
    if (expression.kind == ExpressionKind::Identifier) {
        const auto found = environment.find(expression.text);
        if (found == environment.end()) {
            error =
                "unknown or forward parameter reference '"
                + expression.text + "'";
            return std::nullopt;
        }
        return found->second;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1) {
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (expression.text == "+") {
            return operand;
        }
        if (expression.text == "-") {
            if (*operand
                == std::numeric_limits<std::int64_t>::min()) {
                error = "constant unary negation overflows signed 64-bit "
                        "range";
                return std::nullopt;
            }
            return -*operand;
        }
        if (expression.text == "~") {
            return ~*operand;
        }
        if (expression.text == "!") {
            return *operand == 0 ? 1 : 0;
        }
        error =
            "unsupported unary constant operator '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$clog2") {
        if (expression.operands.size() != 1) {
            error = "$clog2 requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (*operand < 0) {
            error =
                "$clog2 requires a nonnegative integral argument in "
                "the current executable slice";
            return std::nullopt;
        }
        auto magnitude = static_cast<std::uint64_t>(*operand);
        std::int64_t result = 0;
        if (magnitude > 1) {
            --magnitude;
            while (magnitude != 0) {
                ++result;
                magnitude >>= 1U;
            }
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$signed"
            || expression.text == "$unsigned")) {
        if (expression.operands.size() != 1) {
            error =
                expression.text + " requires exactly one argument";
            return std::nullopt;
        }
        return evaluate_constant_expression(
            expression.operands.front(), environment, error);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        if (expression.operands.size() != 1) {
            error = "$isunknown requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        return 0;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3) {
        const auto condition = evaluate_constant_expression(
            expression.operands[0], environment, error);
        if (!condition) {
            return std::nullopt;
        }
        return evaluate_constant_expression(
            expression.operands[*condition != 0 ? 1 : 2],
            environment,
            error);
    }
    if (expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2) {
        error = "expression form is not an integral constant expression";
        return std::nullopt;
    }
    const auto left = evaluate_constant_expression(
        expression.operands[0], environment, error);
    if (!left) {
        return std::nullopt;
    }
    if (expression.text == "&&" && *left == 0) {
        return 0;
    }
    if (expression.text == "||" && *left != 0) {
        return 1;
    }
    const auto right = evaluate_constant_expression(
        expression.operands[1], environment, error);
    if (!right) {
        return std::nullopt;
    }
    std::int64_t result = 0;
    if (expression.text == "+") {
        if (!checked_add(*left, *right, result)) {
            error = "constant addition overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "-") {
        if (!checked_subtract(*left, *right, result)) {
            error = "constant subtraction overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "*") {
        if (!checked_multiply(*left, *right, result)) {
            error = "constant multiplication overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "**") {
        if (!checked_power(*left, *right, result)) {
            error =
                *left == 0 && *right < 0
                    ? "constant zero to a negative power is undefined"
                    : "constant exponentiation overflows signed 64-bit "
                      "range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "/" || expression.text == "%") {
        if (*right == 0) {
            error = "constant division by zero";
            return std::nullopt;
        }
        if (*left == std::numeric_limits<std::int64_t>::min()
            && *right == -1) {
            error = "constant division overflows signed 64-bit range";
            return std::nullopt;
        }
        return expression.text == "/"
            ? *left / *right
            : *left % *right;
    }
    if (expression.text == "<<"
        || expression.text == ">>"
        || expression.text == "<<<"
        || expression.text == ">>>"
        || expression.text == "sll"
        || expression.text == "srl"
        || expression.text == "sra") {
        if (*left < 0 || *right < 0 || *right >= 63) {
            error = "constant shifts require a nonnegative value and an "
                    "amount from 0 through 62";
            return std::nullopt;
        }
        if (expression.text == "<<"
            || expression.text == "<<<"
            || expression.text == "sll") {
            if (*left
                > (std::numeric_limits<std::int64_t>::max()
                   >> static_cast<unsigned>(*right))) {
                error = "constant left shift overflows signed 64-bit range";
                return std::nullopt;
            }
            return *left << static_cast<unsigned>(*right);
        }
        return *left >> static_cast<unsigned>(*right);
    }
    if (expression.text == "&") {
        return *left & *right;
    }
    if (expression.text == "|") {
        return *left | *right;
    }
    if (expression.text == "^") {
        return *left ^ *right;
    }
    if (expression.text == "~^" || expression.text == "^~") {
        return ~(*left ^ *right);
    }
    if (expression.text == "&&") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "||") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "==" || expression.text == "==="
        || expression.text == "=") {
        return *left == *right ? 1 : 0;
    }
    if (expression.text == "!=" || expression.text == "!=="
        || expression.text == "/=") {
        return *left != *right ? 1 : 0;
    }
    if (expression.text == "<") {
        return *left < *right ? 1 : 0;
    }
    if (expression.text == "<=") {
        return *left <= *right ? 1 : 0;
    }
    if (expression.text == ">") {
        return *left > *right ? 1 : 0;
    }
    if (expression.text == ">=") {
        return *left >= *right ? 1 : 0;
    }
    error =
        "unsupported binary constant operator '" + expression.text + "'";
    return std::nullopt;
}



Expression constant_expression(
    const std::int64_t value,
    const frontend::SourceSpan& span,
    const frontend::ValueDomain domain,
    const frontend::Language language,
    const bool vhdl_enumeration,
    std::string nominal_type) {
    if (vhdl_enumeration
        && language == frontend::Language::Vhdl2008) {
        Expression result{
            ExpressionKind::LogicLiteral,
            "@fsim-enum:" + std::to_string(value),
            {},
            span};
        result.nominal_type = std::move(nominal_type);
        return result;
    }
    if (domain == frontend::ValueDomain::Boolean) {
        return {
            ExpressionKind::BooleanLiteral,
            value == 0 ? "false" : "true",
            {},
            span};
    }
    if (domain == frontend::ValueDomain::Bit2
        && language == frontend::Language::Vhdl2008) {
        return {
            ExpressionKind::LogicLiteral,
            value == 0 ? "'0'" : "'1'",
            {},
            span};
    }
    if (value >= 0) {
        return {
            ExpressionKind::IntegerLiteral,
            std::to_string(value),
            {},
            span};
    }
    const auto magnitude =
        value == std::numeric_limits<std::int64_t>::min()
            ? std::uint64_t{1} << 63U
            : static_cast<std::uint64_t>(-value);
    return {
        ExpressionKind::Unary,
        "-",
        {
            Expression{
                ExpressionKind::IntegerLiteral,
                std::to_string(magnitude),
                {},
                span}},
        span};
}



bool fold_vhdl_enumeration_attributes(
    Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            if (!fold_vhdl_enumeration_attributes(
                    choice,
                    unit,
                    environment,
                    error,
                    range_error)) {
                return false;
            }
        }
    }
    for (auto& operand : expression.operands) {
        if (!fold_vhdl_enumeration_attributes(
                operand,
                unit,
                environment,
                error,
                range_error)) {
            return false;
        }
    }
    if (expression.kind != ExpressionKind::Call) {
        return true;
    }
    if (!expression.text.starts_with('\'')
        || expression.operands.empty()
        || expression.operands.front().kind
            != ExpressionKind::Identifier) {
        return true;
    }
    const auto* type_mark =
        vhdl_enumeration_type_mark(
            unit, expression.operands.front().text);
    if (type_mark == nullptr) {
        return true;
    }
    const auto folded =
        evaluate_vhdl_enumeration_attribute(
            expression,
            unit,
            environment,
            error,
            range_error);
    if (!folded) {
        return false;
    }
    const auto span = expression.span;
    expression = constant_expression(
        folded->value,
        span,
        folded->boolean_result
            ? frontend::ValueDomain::Boolean
            : folded->enumeration_result
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Integer,
        frontend::Language::Vhdl2008,
        folded->enumeration_result,
        folded->enumeration_result
            ? type_mark->nominal_type
            : std::string{});
    return true;
}



void substitute_parameters(
    Expression& expression,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            const auto domain = domains.find(expression.text);
            expression = constant_expression(
                found->second,
                expression.span,
                domain == domains.end()
                    ? frontend::ValueDomain::Integer
                    : domain->second.domain,
                language,
                domain != domains.end()
                    && domain->second.vhdl_enumeration);
            if (domain != domains.end()
                && domain->second.vhdl_enumeration) {
                expression.nominal_type =
                    domain->second.nominal_type;
            }
            return;
        }
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            substitute_parameters(
                choice, environment, domains, language);
        }
    }
    for (auto& operand : expression.operands) {
        substitute_parameters(
            operand, environment, domains, language);
    }
}



void substitute_parameters(
    frontend::Type& type,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    const auto evaluate_integer_range =
        [&](std::optional<frontend::IntegerRangeExpression>& expression,
            std::optional<frontend::IntegerRange>& range,
            const std::string_view description) {
          auto span = frontend::SourceSpan{};
          if (!expression) {
              return span;
          }
          span = expression->span;
          std::string error;
          const auto left = evaluate_constant_expression(
              expression->left, environment, error);
          const auto right = left
              ? evaluate_constant_expression(
                    expression->right, environment, error)
              : std::nullopt;
          if (!left || !right) {
              diagnostics.push_back({
                  "FSIM-ELAB-INTEGER-001",
                  "cannot evaluate VHDL " + std::string{description}
                      + " constraint: " + error,
                  expression->span});
              range.reset();
          } else {
              range = frontend::IntegerRange{
                  *left, *right, expression->descending};
          }
          expression.reset();
          return span;
        };
    const auto integer_base_range_span =
        evaluate_integer_range(
            type.integer_base_range_expression,
            type.integer_base_range,
            "base integer subtype");
    auto integer_range_span = frontend::SourceSpan{};
    integer_range_span =
        evaluate_integer_range(
            type.integer_range_expression,
            type.integer_range,
            "integer subtype");
    if (type.domain != frontend::ValueDomain::Integer
        && (type.integer_range
            || type.integer_base_range)) {
        diagnostics.push_back({
            "FSIM-ELAB-VHSUBTYPE-001",
            "a VHDL range constraint is valid only for an integer-family "
            "subtype in the current bounded scalar path",
            integer_range_span});
        type.integer_range.reset();
        type.integer_base_range.reset();
    }
    if (type.domain == frontend::ValueDomain::Integer
        && type.integer_range) {
        constexpr auto minimum =
            std::int64_t{std::numeric_limits<std::int32_t>::min()};
        constexpr auto maximum =
            std::int64_t{std::numeric_limits<std::int32_t>::max()};
        const auto& range = *type.integer_range;
        const bool null =
            range.descending ? range.left < range.right
                             : range.left > range.right;
        if (range.left < minimum || range.left > maximum
            || range.right < minimum || range.right > maximum
            || null) {
            diagnostics.push_back({
                "FSIM-ELAB-INTEGER-002",
                null
                    ? "null VHDL integer subtype constraints are not "
                      "executable in this bounded runtime"
                    : "VHDL integer subtype constraint lies outside the "
                      "portable signed 32-bit representation",
                integer_range_span});
            type.integer_range.reset();
        } else {
            const auto separator = type.spelling.find_last_of('.');
            const auto simple_name = type.spelling.substr(
                separator == std::string::npos ? 0 : separator + 1);
            const auto base_lower =
                simple_name == "positive"
                    ? std::int64_t{1}
                    : simple_name == "natural"
                        ? std::int64_t{0}
                        : minimum;
            const auto lower =
                std::min(range.left, range.right);
            if (lower < base_lower) {
                diagnostics.push_back({
                    "FSIM-ELAB-INTEGER-002",
                    "VHDL integer subtype constraint is outside the "
                    "range of base subtype '" + simple_name + "'",
                    integer_range_span});
                type.integer_range.reset();
            }
        }
        if (type.integer_range
            && type.integer_base_range) {
            const auto& base = *type.integer_base_range;
            const auto base_lower =
                std::min(base.left, base.right);
            const auto base_upper =
                std::max(base.left, base.right);
            const auto derived_lower =
                std::min(
                    type.integer_range->left,
                    type.integer_range->right);
            const auto derived_upper =
                std::max(
                    type.integer_range->left,
                    type.integer_range->right);
            if (derived_lower < base_lower
                || derived_upper > base_upper) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHSUBTYPE-002",
                    "derived VHDL integer subtype constraint lies outside "
                    "its resolved base subtype range",
                    integer_range_span.begin.offset != 0
                        ? integer_range_span
                        : integer_base_range_span});
                type.integer_range.reset();
            }
        }
    }
    const auto evaluate_enumeration_range =
        [&](std::optional<
                frontend::DiscreteRangeExpression>& expression,
            std::optional<frontend::EnumerationRange>& range,
            const std::string_view description) {
          auto span = frontend::SourceSpan{};
          if (!expression) {
              return span;
          }
          span = expression->span;
          const auto evaluate_bound =
              [&](const Expression& bound,
                  std::string& error)
                  -> std::optional<std::int64_t> {
                if (const auto ordinal =
                        vhdl_enumeration_ordinal(
                            bound, type)) {
                    return ordinal;
                }
                if (bound.kind != ExpressionKind::Identifier) {
                    error =
                        "enumeration range bound is not a literal, "
                        "constant, or prior generic";
                    return std::nullopt;
                }
                const auto value =
                    environment.find(bound.text);
                const auto domain =
                    domains.find(bound.text);
                if (value == environment.end()
                    || domain == domains.end()) {
                    error = "unknown enumeration range bound '"
                        + bound.text + "'";
                    return std::nullopt;
                }
                if (!domain->second.vhdl_enumeration
                    || domain->second.nominal_type
                        != type.nominal_type) {
                    error = "enumeration range bound '"
                        + bound.text
                        + "' does not have nominal type '"
                        + type.spelling + "'";
                    return std::nullopt;
                }
                return value->second;
              };
          std::string error;
          const auto left =
              evaluate_bound(expression->left, error);
          const auto right =
              left
                  ? evaluate_bound(
                        expression->right, error)
                  : std::nullopt;
          if (!left || !right) {
              diagnostics.push_back({
                  "FSIM-ELAB-VHENUMRANGE-001",
                  "cannot evaluate VHDL " + std::string{description}
                      + " constraint: " + error,
                  expression->span});
              range.reset();
          } else {
              range = frontend::EnumerationRange{
                  *left,
                  *right,
                  expression->descending};
          }
          expression.reset();
          return span;
        };
    const auto enumeration_base_range_span =
        evaluate_enumeration_range(
            type.enumeration_base_range_expression,
            type.enumeration_base_range,
            "base enumeration subtype");
    const auto enumeration_range_span =
        evaluate_enumeration_range(
            type.enumeration_range_expression,
            type.enumeration_range,
            "enumeration subtype");
    if (!type.enumeration_literals.empty()
        && !type.enumeration_range
        && !type.enumeration_range_expression) {
        type.enumeration_range =
            frontend::EnumerationRange{
                0,
                static_cast<std::int64_t>(
                    type.enumeration_literals.size() - 1U),
                false};
    }
    if (type.enumeration_range) {
        const auto& range = *type.enumeration_range;
        const auto count =
            type.enumeration_literals.size();
        const bool representable =
            range.left >= 0 && range.right >= 0
            && static_cast<std::uint64_t>(range.left) < count
            && static_cast<std::uint64_t>(range.right) < count;
        const bool null =
            range.descending ? range.left < range.right
                             : range.left > range.right;
        if (!representable || null) {
            diagnostics.push_back({
                "FSIM-ELAB-VHENUMRANGE-002",
                null
                    ? "null VHDL enumeration subtype constraints are "
                      "not executable in this bounded runtime"
                    : "VHDL enumeration subtype constraint lies outside "
                      "the base enumeration literal range",
                enumeration_range_span});
            type.enumeration_range.reset();
        }
    }
    if (type.enumeration_range
        && type.enumeration_base_range) {
        const auto& base =
            *type.enumeration_base_range;
        const auto base_lower =
            std::min(base.left, base.right);
        const auto base_upper =
            std::max(base.left, base.right);
        const auto derived_lower =
            std::min(
                type.enumeration_range->left,
                type.enumeration_range->right);
        const auto derived_upper =
            std::max(
                type.enumeration_range->left,
                type.enumeration_range->right);
        if (derived_lower < base_lower
            || derived_upper > base_upper) {
            diagnostics.push_back({
                "FSIM-ELAB-VHENUMRANGE-003",
                "derived VHDL enumeration subtype constraint lies "
                "outside its resolved base subtype range",
                enumeration_range_span.begin.offset != 0
                    ? enumeration_range_span
                    : enumeration_base_range_span});
            type.enumeration_range.reset();
        }
    }
    if (!type.packed_members.empty()) {
        const bool is_union =
            type.packed_aggregate
            == frontend::PackedAggregateKind::Union;
        std::uint64_t total_width = 0;
        std::optional<std::uint64_t> union_width;
        bool valid = true;
        for (auto& member : type.packed_members) {
            if (member.packed_range_expression) {
                std::string error;
                const auto left = evaluate_constant_expression(
                    member.packed_range_expression->left,
                    environment,
                    error);
                const auto right = left
                    ? evaluate_constant_expression(
                          member.packed_range_expression->right,
                          environment,
                          error)
                    : std::nullopt;
                if (!left || !right) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVSTRUCT-001",
                        "cannot evaluate packed struct member range: "
                            + error,
                        member.packed_range_expression->span});
                    valid = false;
                    continue;
                }
                member.packed_range = frontend::PackedRange{
                    *left, *right, *left >= *right};
                member.packed_range_expression.reset();
            }
            const auto width = member.width();
            if (!width || *width == 0) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVSTRUCT-001",
                    "packed aggregate member '" + member.name
                        + "' has an invalid or overflowing width",
                    member.span});
                valid = false;
                continue;
            }
            if (is_union) {
                if (union_width && *union_width != *width) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVUNION-001",
                        "packed union member '" + member.name
                            + "' has width "
                            + std::to_string(*width)
                            + " but every member must have width "
                            + std::to_string(*union_width),
                        member.span});
                    valid = false;
                    continue;
                }
                union_width = *width;
                total_width = *width;
            } else {
                if (*width
                    > std::numeric_limits<std::uint64_t>::max()
                        - total_width) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVSTRUCT-001",
                        "packed struct member '" + member.name
                            + "' overflows the aggregate width",
                        member.span});
                    valid = false;
                    continue;
                }
                total_width += *width;
            }
        }
        if (!valid || total_width == 0
            || total_width - 1U
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            if (valid) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVSTRUCT-001",
                    "packed aggregate total width exceeds the supported range",
                    type.packed_members.front().span});
            }
            type.packed_range.reset();
            return;
        }
        if (!is_union) {
            auto offset = total_width;
            for (auto& member : type.packed_members) {
                offset -= *member.width();
                member.lsb_offset = offset;
            }
        } else {
            for (auto& member : type.packed_members) {
                member.lsb_offset = 0;
            }
        }
        type.packed_range = frontend::PackedRange{
            static_cast<std::int64_t>(total_width - 1U),
            0,
            true};
        type.packed_range_expression.reset();
        return;
    }
    if (!type.packed_range_expression) {
        return;
    }
    std::string error;
    const auto left = evaluate_constant_expression(
        type.packed_range_expression->left, environment, error);
    const auto right = left
        ? evaluate_constant_expression(
              type.packed_range_expression->right,
              environment,
              error)
        : std::nullopt;
    if (!left || !right) {
        diagnostics.push_back({
            language == frontend::Language::Vhdl2008
                ? "FSIM-ELAB-GENERIC-006"
                : "FSIM-ELAB-PARAM-006",
            "cannot evaluate packed range: " + error,
            type.packed_range_expression->span});
        return;
    }
    const frontend::PackedRange range{
        *left,
        *right,
        type.packed_range_expression->descending.value_or(
            *left >= *right)};
    if (language == frontend::Language::Vhdl2008
        && type.packed_range_expression->descending) {
        const bool null =
            range.descending ? range.left < range.right
                             : range.left > range.right;
        if (null) {
            diagnostics.push_back({
                "FSIM-ELAB-VHARRAY-002",
                "null VHDL array constraints are not executable in the "
                "current packed runtime",
                type.packed_range_expression->span});
            type.packed_range.reset();
            type.packed_range_expression.reset();
            return;
        }
    }
    if (type.vhdl_array) {
        if (type.vhdl_array->index_base_range
            && (!type.vhdl_array->index_base_range->contains(range.left)
                || !type.vhdl_array->index_base_range->contains(
                    range.right))) {
            diagnostics.push_back({
                "FSIM-ELAB-VHARRAY-003",
                "VHDL array constraint lies outside index subtype '"
                    + type.vhdl_array->index_subtype + "'",
                type.packed_range_expression->span});
            type.packed_range.reset();
            type.packed_range_expression.reset();
            return;
        }
    }
    if (range.width() == 0) {
        diagnostics.push_back({
            type.vhdl_array
                ? "FSIM-ELAB-VHARRAY-004"
                : language == frontend::Language::Vhdl2008
                ? "FSIM-ELAB-GENERIC-007"
                : "FSIM-ELAB-PARAM-007",
            type.vhdl_array
                ? "VHDL array constraint width overflows fsim's 64-bit "
                  "packed representation"
                : "packed range width overflows fsim's 64-bit range",
            type.packed_range_expression->span});
        type.packed_range.reset();
        type.packed_range_expression.reset();
        return;
    }
    type.packed_range = range;
    type.packed_range_expression.reset();
    if (type.vhdl_array) {
        type.vhdl_array->unconstrained = false;
    }
}

} // namespace fsim::elaboration::elaboration_detail
